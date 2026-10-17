/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_panel.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>

namespace CIRCUITALIVE
{
namespace
{
wxString utf8( const std::string& text ) { return wxString::FromUTF8( text ); }
const wxColour COLORS[] = { { 85, 225, 115 }, { 255, 208, 70 }, { 80, 185, 255 },
                           { 255, 120, 185 }, { 195, 150, 255 }, { 80, 225, 215 } };
wxColour color( std::size_t channel ) { return COLORS[channel % 6]; }
wxChoice* choice( wxWindow* parent, std::initializer_list<wxString> labels )
{
    auto* control = new wxChoice( parent, wxID_ANY );
    for( const auto& label : labels ) control->Append( label );
    control->SetMinSize( wxSize( 100, -1 ) );
    return control;
}
wxTextCtrl* text( wxWindow* parent )
{
    auto* control = new wxTextCtrl( parent, wxID_ANY, "", wxDefaultPosition, wxSize( 110, -1 ), wxTE_PROCESS_ENTER );
    return control;
}
void row( wxFlexGridSizer* grid, wxWindow* page, const wxString& label, wxWindow* control )
{
    grid->Add( new wxStaticText( page, wxID_ANY, label ), 0, wxALIGN_CENTER_VERTICAL );
    grid->Add( control, 1, wxEXPAND );
}
wxScrolledWindow* page( wxNotebook* tabs, const wxString& name, wxFlexGridSizer*& grid )
{
    auto* panel = new wxScrolledWindow( tabs, wxID_ANY ); panel->SetScrollRate( 0, 10 );
    auto* box = new wxBoxSizer( wxVERTICAL );
    grid = new wxFlexGridSizer( 2, 5, 8 ); grid->AddGrowableCol( 1 );
    box->Add( grid, 0, wxEXPAND | wxALL, 8 ); panel->SetSizer( box );
    tabs->AddPage( panel, name ); return panel;
}
void change( wxTextCtrl* control, const std::string& value )
{
    if( !control->HasFocus() ) control->ChangeValue( utf8( value ) );
}
void change( wxComboBox* control, const std::string& value )
{
    if( !control->HasFocus() ) control->ChangeValue( utf8( value ) );
}
// Invalid entries stay highlighted on Enter; leaving the field restores its last valid text.
void bindNumber( wxWindow* control, std::function<bool( const std::string& )> apply )
{
    const wxColour original = control->GetBackgroundColour();
    auto lastValid = std::make_shared<wxString>();
    control->Bind( wxEVT_SET_FOCUS, [control, lastValid]( wxFocusEvent& e )
    { if( auto* entry = dynamic_cast<wxTextEntry*>( control ) ) *lastValid = entry->GetValue(); e.Skip(); } );
    auto commit = [control, apply, original, lastValid]( bool leaving )
    {
        auto* entry = dynamic_cast<wxTextEntry*>( control );
        if( control->IsBeingDeleted() ) return;
        // A formatted display value may be rounded. Merely focusing/leaving it must
        // not change acquisition settings or release a held capture.
        if( entry && entry->GetValue() == *lastValid )
        { control->SetBackgroundColour( original ); control->Refresh(); return; }
        const bool valid = entry && apply( entry->GetValue().ToStdString() );
        if( valid ) *lastValid = entry->GetValue();
        else if( leaving && entry ) entry->ChangeValue( *lastValid );
        control->SetBackgroundColour( valid || leaving ? original : wxColour( 112, 48, 48 ) ); control->Refresh();
    };
    control->Bind( wxEVT_TEXT_ENTER, [commit]( wxCommandEvent& ) { commit( false ); } );
    control->Bind( wxEVT_KILL_FOCUS, [commit]( wxFocusEvent& e ) { commit( true ); e.Skip(); } );
    if( dynamic_cast<wxComboBox*>( control ) )
        control->Bind( wxEVT_COMBOBOX, [commit]( wxCommandEvent& ) { commit( false ); } );
}
wxComboBox* scales( wxWindow* parent, const std::string& unit, int first, int last )
{
    auto* combo = new wxComboBox( parent, wxID_ANY, "", wxDefaultPosition, wxSize( 110, -1 ), 0, nullptr, wxTE_PROCESS_ENTER );
    for( int decade = first; decade <= last; ++decade )
        for( double factor : { 1., 2., 5. } ) combo->Append( utf8( ScopeFormat( factor * std::pow( 10., decade ), unit ) ) );
    combo->SetToolTip( _( "Choose a scale or type an SI value, for example 20 ms or 500 mV. Press Enter to apply." ) );
    return combo;
}
}

class SCOPE_PLOT : public wxPanel
{
public:
    SCOPE_PLOT( wxWindow* parent, SCOPE_PANEL& owner ) : wxPanel( parent ), m_owner( owner )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT ); SetMinSize( wxSize( 240, 200 ) );
        SetToolTip( _( "Drag cursor lines or click for A, Shift-click for B. Wheel: time scale; Shift-wheel: selected signal scale." ) );
        Bind( wxEVT_PAINT, &SCOPE_PLOT::paint, this );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& e ) { Refresh(); e.Skip(); } );
        Bind( wxEVT_LEFT_DOWN, &SCOPE_PLOT::down, this );
        Bind( wxEVT_MOTION, [this]( wxMouseEvent& e ) { if( m_drag && e.Dragging() ) move( e.GetPosition() ); } );
        Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& ) { m_drag = 0; if( HasCapture() ) ReleaseMouse(); } );
        Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_drag = 0; } );
        Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& e )
        {
            auto& preferences = m_owner.m_preferences;
            const double factor = e.GetWheelRotation() > 0 ? .45 : 1.8;
            if( e.ShiftDown() && !m_owner.m_channels.empty() )
            {
                auto& channel = m_owner.style( m_owner.selected() ); channel.automatic = false;
                channel.perDivision = std::clamp( ScopeNiceScale( channel.perDivision * factor ), 1e-12, 1e12 );
                m_owner.syncControls(); Refresh();
            }
            else
            {
                preferences.timebase.perDivision = std::clamp( ScopeNiceScale( preferences.timebase.perDivision * factor ), 1e-9, 1e3 );
                m_owner.retime(); m_owner.syncControls();
            }
        } );
    }
private:
    wxRect area() const { const auto s = GetClientSize(); return { 51, 30, std::max( 1, s.x - 66 ), std::max( 1, s.y - 65 ) }; }
    double fraction( double time ) const
    {
        const auto& f = m_owner.m_acquisition.Frame(); return ( time - f.begin ) / ( f.end - f.begin );
    }
    int x( double f ) const { return area().x + std::clamp( int( f * area().width ), -1000000, 1000000 ); }
    int y( double value, std::size_t channel ) const
    {
        const auto& c = m_owner.style( channel ); const auto plot = area();
        const double divisions = ( value - c.center ) / c.perDivision * ( c.inverted ? -1 : 1 );
        return plot.y + plot.height / 2 - int( std::clamp( divisions * plot.height / 8, -1000000., 1000000. ) );
    }
    void down( wxMouseEvent& e )
    {
        const auto p = area(); const auto pos = e.GetPosition();
        const auto& pref = m_owner.m_preferences;
        if( !p.Contains( pos ) || !pref.cursorMode || m_owner.m_channels.empty() ) return;
        int nearest = 12;
        auto consider = [&]( int distance, int id ) { if( distance < nearest ) { nearest = distance; m_drag = id; } };
        if( pref.cursorMode & 1 )
        { consider( std::abs( pos.x - x( pref.cursorA ) ), 1 ); consider( std::abs( pos.x - x( pref.cursorB ) ), 2 ); }
        if( pref.cursorMode & 2 )
        { consider( std::abs( pos.y - y( pref.levelA, m_owner.selected() ) ), 3 ); consider( std::abs( pos.y - y( pref.levelB, m_owner.selected() ) ), 4 ); }
        if( !m_drag ) m_drag = ( pref.cursorMode & 1 ? 1 : 3 ) + ( e.ShiftDown() ? 1 : 0 );
        CaptureMouse(); move( pos );
    }
    void move( wxPoint pos )
    {
        const auto p = area(); auto& pref = m_owner.m_preferences;
        if( m_drag <= 2 )
        {
            const double f = std::clamp( double( pos.x - p.x ) / p.width, 0., 1. );
            ( m_drag == 1 ? pref.cursorA : pref.cursorB ) = f;
        }
        else
        {
            const auto& c = m_owner.style( m_owner.selected() );
            const double v = c.center + ( .5 - std::clamp( double( pos.y - p.y ) / p.height, 0., 1. ) )
                    * 8 * c.perDivision * ( c.inverted ? -1 : 1 );
            ( m_drag == 3 ? pref.levelA : pref.levelB ) = v;
        }
        m_owner.updateCursors(); Refresh();
    }
    void paint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this ); const auto plot = area();
        dc.SetBackground( wxBrush( wxColour( 18, 23, 28 ) ) ); dc.Clear();
        dc.SetFont( GetFont().Smaller() );
        const auto& frame = m_owner.m_acquisition.Frame(); const auto& pref = m_owner.m_preferences;
        dc.SetPen( wxPen( wxColour( 43, 55, 64 ) ) );
        for( int i = 0; i <= 10; ++i ) dc.DrawLine( plot.x + plot.width * i / 10, plot.y, plot.x + plot.width * i / 10, plot.GetBottom() );
        for( int i = 0; i <= 8; ++i ) dc.DrawLine( plot.x, plot.y + plot.height * i / 8, plot.GetRight(), plot.y + plot.height * i / 8 );
        dc.SetPen( wxPen( wxColour( 84, 99, 109 ), 1, wxPENSTYLE_DOT ) );
        dc.DrawLine( plot.x, plot.y + plot.height / 2, plot.GetRight(), plot.y + plot.height / 2 );
        if( frame.end > frame.begin )
        {
            dc.SetTextForeground( wxColour( 185, 200, 211 ) );
            for( int i = 0; i <= 10; i += plot.width < 450 ? 5 : 2 )
            {
                double tick = frame.begin + ( frame.end - frame.begin ) * i / 10 - m_owner.origin();
                if( std::abs( tick ) < ( frame.end - frame.begin ) * 1e-10 ) tick = 0;
                const auto label = utf8( ScopeFormat( tick, "s" ) );
                const auto width = dc.GetTextExtent( label ).x;
                dc.DrawText( label, std::clamp( plot.x + plot.width * i / 10 - width / 2, 0, std::max( 0, GetClientSize().x - width ) ), plot.GetBottom() + 7 );
            }
        }
        if( !m_owner.m_channels.empty() )
        {
            const auto selected = m_owner.selected(); const auto& c = m_owner.style( selected );
            dc.SetTextForeground( color( selected ) );
            auto label = [&]( double divisions, int top )
            {
                dc.DrawText( utf8( ScopeFormat( c.center + divisions * c.perDivision * ( c.inverted ? -1 : 1 ), "V" ) ), 2, top );
            };
            label( 4, plot.y ); label( 0, plot.y + plot.height / 2 - 7 ); label( -4, plot.GetBottom() - 14 );
        }
        if( frame.triggerTime && frame.end > frame.begin )
        {
            const int tx = x( fraction( *frame.triggerTime ) );
            dc.SetPen( wxPen( wxColour( 255, 165, 55 ), 1, wxPENSTYLE_DOT ) );
            if( tx >= plot.x && tx <= plot.GetRight() )
            { dc.DrawLine( tx, plot.y, tx, plot.GetBottom() ); dc.DrawText( "T", tx + 3, plot.y ); }
        }
        if( pref.trigger.mode != SCOPE_TRIGGER_MODE::ROLL )
        {
            for( std::size_t c = 0; c < m_owner.m_channels.size(); ++c )
            {
                if( m_owner.m_channels[c].name != pref.triggerSource ) continue;
                const int level = y( pref.trigger.level, c );
                if( level < plot.y || level > plot.GetBottom() ) break;
                dc.SetPen( wxPen( wxColour( 255, 165, 55 ), 1, wxPENSTYLE_DOT ) );
                dc.SetTextForeground( wxColour( 255, 165, 55 ) );
                dc.DrawLine( plot.x, level, plot.GetRight(), level );
                dc.DrawText( utf8( "T " + ScopeFormat( pref.trigger.level, "V" ) ), plot.x + 4, level + 2 );
                break;
            }
        }
        int legend = 5;
        for( std::size_t c = 0; c < m_owner.m_channels.size(); ++c )
        {
            const auto& channel = m_owner.m_channels[c]; const auto& style = m_owner.style( c );
            if( !style.visible ) continue;
            dc.SetTextForeground( color( c ) );
            const auto name = utf8( channel.name + " " + ScopeFormat( style.perDivision, "V" ) + "/div" );
            if( legend < GetClientSize().x - 40 )
            { dc.DrawText( name, legend, 7 ); legend += dc.GetTextExtent( name ).x + 16; }
            if( frame.end <= frame.begin ) continue;
            std::vector<double> minimum( plot.width + 1, INFINITY ), maximum( plot.width + 1, -INFINITY );
            auto point = [&]( double time, double v )
            {
                if( !std::isfinite( v ) || time < frame.begin || time > frame.end ) return;
                const int bucket = std::clamp( int( fraction( time ) * plot.width ), 0, plot.width );
                minimum[bucket] = std::min( minimum[bucket], v ); maximum[bucket] = std::max( maximum[bucket], v );
            };
            for( const auto& sample : frame.samples )
            {
                if( sample.time < frame.begin || sample.time > frame.end ) continue;
                try { point( sample.time, MeasurementValue( channel, sample ) ); } catch( const std::out_of_range& ) {}
            }
            point( frame.begin, ScopeValueAt( frame, channel, frame.begin ) );
            point( frame.end, ScopeValueAt( frame, channel, frame.end ) );
            dc.SetClippingRegion( plot ); dc.SetPen( wxPen( color( c ), 2 ) );
            wxPoint previous; bool connected = false;
            for( int i = 0; i <= plot.width; ++i )
            {
                if( !std::isfinite( minimum[i] ) ) continue;
                const wxPoint current( plot.x + i, y( ( minimum[i] + maximum[i] ) / 2, c ) );
                dc.DrawLine( current.x, y( minimum[i], c ), current.x, y( maximum[i], c ) );
                if( connected ) dc.DrawLine( previous, current );
                previous = current; connected = true;
            }
            // Each channel has its own physical zero marker.
            dc.SetPen( wxPen( color( c ) ) );
            const int zero = y( 0, c ); dc.DrawLine( plot.x, zero, plot.x + 8, zero );
            dc.DestroyClippingRegion();
        }
        if( !m_owner.m_channels.empty() && pref.cursorMode )
        {
            dc.SetClippingRegion( plot );
            dc.SetPen( wxPen( wxColour( 230, 235, 240 ), 1, wxPENSTYLE_SHORT_DASH ) );
            dc.SetTextForeground( wxColour( 230, 235, 240 ) );
            if( pref.cursorMode & 1 )
            {
                dc.DrawLine( x( pref.cursorA ), plot.y, x( pref.cursorA ), plot.GetBottom() );
                dc.DrawLine( x( pref.cursorB ), plot.y, x( pref.cursorB ), plot.GetBottom() );
                dc.DrawText( "A", x( pref.cursorA ) + 3, plot.y + 18 ); dc.DrawText( "B", x( pref.cursorB ) + 3, plot.y + 34 );
            }
            if( pref.cursorMode & 2 )
            {
                const int a = y( pref.levelA, m_owner.selected() ), b = y( pref.levelB, m_owner.selected() );
                dc.DrawLine( plot.x, a, plot.GetRight(), a ); dc.DrawLine( plot.x, b, plot.GetRight(), b );
                dc.DrawText( "YA", plot.x + 12, a - 16 ); dc.DrawText( "YB", plot.x + 12, b - 16 );
            }
            dc.DestroyClippingRegion();
        }
        if( frame.samples.empty() )
        { dc.SetTextForeground( wxColour( 205, 220, 225 ) ); dc.DrawText( _( "Waiting for capture" ), plot.x + 8, plot.y + 60 ); }
    }
    SCOPE_PANEL& m_owner;
    int m_drag = 0;
};

SCOPE_PANEL::SCOPE_PANEL( wxWindow* parent, SCOPE_PREFERENCES& preferences, std::function<void()> expand ) :
        wxPanel( parent ), m_preferences( preferences )
{
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* top = new wxBoxSizer( wxHORIZONTAL );
    top->Add( new wxStaticText( this, wxID_ANY, _( "Time/div" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
    m_timebase = scales( this, "s", -6, 1 ); top->Add( m_timebase, 1, wxRIGHT, 4 );
    auto* automatic = new wxButton( this, wxID_ANY, _( "Auto" ), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    automatic->SetToolTip( _( "Fit all signal amplitudes, center trigger level, and show about three periods when measurable." ) );
    top->Add( automatic, 0 ); layout->Add( top, 0, wxEXPAND | wxALL, 5 );
    auto* actions = new wxBoxSizer( wxHORIZONTAL );
    m_hold = new wxToggleButton( this, wxID_ANY, _( "Hold" ), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    m_hold->SetToolTip( _( "Freeze this capture; the circuit simulation continues." ) );
    auto* arm = new wxButton( this, wxID_ANY, _( "Rearm" ), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    auto* enlarge = new wxButton( this, wxID_ANY, _( "Expand / dock" ), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
    actions->Add( m_hold, 0, wxRIGHT, 4 ); actions->Add( arm, 0, wxRIGHT, 4 ); actions->AddStretchSpacer(); actions->Add( enlarge, 0 );
    layout->Add( actions, 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );
    m_status = new wxStaticText( this, wxID_ANY, "" ); layout->Add( m_status, 0, wxEXPAND | wxALL, 5 );
    m_plot = new SCOPE_PLOT( this, *this ); layout->Add( m_plot, 3, wxEXPAND | wxLEFT | wxRIGHT, 4 );
    m_cursorSummary = new wxStaticText( this, wxID_ANY, "" );
    layout->Add( m_cursorSummary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5 ); m_cursorSummary->Hide();
    m_controls = new wxNotebook( this, wxID_ANY ); m_controls->SetMinSize( wxSize( 260, 260 ) );
    wxFlexGridSizer* grid;
    auto* acquisition = page( m_controls, _( "Trigger" ), grid );
    m_mode = choice( acquisition, { _( "Auto" ), _( "Normal" ), _( "Single" ), _( "Off / roll" ) } );
    row( grid, acquisition, _( "Mode" ), m_mode );
    m_source = choice( acquisition, {} ); row( grid, acquisition, _( "Source" ), m_source );
    m_edge = choice( acquisition, { _( "Rising ↑" ), _( "Falling ↓" ) } ); row( grid, acquisition, _( "Edge" ), m_edge );
    m_level = text( acquisition ); row( grid, acquisition, _( "Level (V)" ), m_level );
    m_hysteresis = text( acquisition ); row( grid, acquisition, _( "Hysteresis (V)" ), m_hysteresis );
    m_pretrigger = text( acquisition ); row( grid, acquisition, _( "Pretrigger (%)" ), m_pretrigger );
    m_holdoff = text( acquisition ); row( grid, acquisition, _( "Holdoff (s)" ), m_holdoff );
    m_delay = text( acquisition ); row( grid, acquisition, _( "Time offset (s)" ), m_delay );
    m_hysteresis->SetToolTip( _( "The signal must return this far past the level before the next edge can trigger." ) );
    m_delay->SetToolTip( _( "Positive moves the capture later; negative moves it earlier. Roll mode: negative browses older history." ) );
    auto* channels = page( m_controls, _( "Signals" ), grid );
    m_channel = choice( channels, {} ); row( grid, channels, _( "Signal" ), m_channel );
    m_visible = new wxCheckBox( channels, wxID_ANY, _( "Show trace" ) ); row( grid, channels, "", m_visible );
    m_automatic = new wxCheckBox( channels, wxID_ANY, _( "Automatic range" ) ); row( grid, channels, "", m_automatic );
    m_scale = scales( channels, "V", -6, 3 ); row( grid, channels, _( "Amplitude / div" ), m_scale );
    m_center = text( channels ); row( grid, channels, _( "Center (V)" ), m_center );
    m_center->SetToolTip( _( "Voltage at the middle horizontal line, independently for each signal." ) );
    m_inverted = new wxCheckBox( channels, wxID_ANY, _( "Invert display" ) ); row( grid, channels, "", m_inverted );
    auto* fit = new wxButton( channels, wxID_ANY, _( "Fit this signal" ) ); row( grid, channels, "", fit );
    auto* cursors = page( m_controls, _( "Cursors" ), grid );
    m_cursorMode = choice( cursors, { _( "Off" ), _( "Time" ), _( "Amplitude" ), _( "Time + amplitude" ) } );
    row( grid, cursors, _( "Cursors" ), m_cursorMode );
    m_cursorSource = choice( cursors, {} ); row( grid, cursors, _( "Signal" ), m_cursorSource );
    auto* help = new wxStaticText( cursors, wxID_ANY, _( "Drag lines; click A, Shift-click B.\nTimes are relative to T, or window start." ) );
    help->Wrap( 270 ); cursors->GetSizer()->Add( help, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8 );
    m_cursorA = text( cursors ); row( grid, cursors, _( "Time A (s)" ), m_cursorA );
    m_cursorB = text( cursors ); row( grid, cursors, _( "Time B (s)" ), m_cursorB );
    m_levelA = text( cursors ); row( grid, cursors, _( "Amplitude A (V)" ), m_levelA );
    m_levelB = text( cursors ); row( grid, cursors, _( "Amplitude B (V)" ), m_levelB );
    m_cursorInfo = new wxStaticText( cursors, wxID_ANY, "" ); cursors->GetSizer()->Add( m_cursorInfo, 0, wxEXPAND | wxALL, 8 );
    auto* readings = new wxPanel( m_controls ); auto* readingsLayout = new wxBoxSizer( wxVERTICAL );
    auto* exportButton = new wxButton( readings, wxID_ANY, _( "Export capture CSV…" ) );
    readingsLayout->Add( exportButton, 0, wxALL, 4 );
    auto* note = new wxStaticText( readings, wxID_ANY, _( "Visible capture · time-weighted mean / RMS\nPeriod, frequency and duty: midpoint crossings." ) );
    note->Wrap( 280 ); readingsLayout->Add( note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4 );
    m_readings = new wxListCtrl( readings, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL );
    const wxString columns[] = { _( "Signal" ), _( "Min" ), _( "Max" ), _( "Pk–pk" ), _( "Mean" ), _( "RMS" ),
        _( "Frequency ≈" ), _( "Period ≈" ), _( "Duty ≈" ), _( "Samples" ) };
    for( int i = 0; i < 10; ++i ) m_readings->InsertColumn( i, columns[i], wxLIST_FORMAT_LEFT, i == 0 ? 100 : 94 );
    readingsLayout->Add( m_readings, 1, wxEXPAND | wxALL, 4 ); readings->SetSizer( readingsLayout );
    m_controls->AddPage( readings, _( "Readings" ) );
    layout->Add( m_controls, 2, wxEXPAND | wxALL, 4 );
    m_info = new wxStaticText( this, wxID_ANY, "" ); layout->Add( m_info, 0, wxEXPAND | wxALL, 5 );
    SetSizer( layout );

    automatic->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { autoScale(); } );
    enlarge->Bind( wxEVT_BUTTON, [expand]( wxCommandEvent& ) { expand(); } );
    arm->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    { m_hold->SetValue( false ); m_acquisition.Arm( m_history ); present(); } );
    m_hold->Bind( wxEVT_TOGGLEBUTTON, [this]( wxCommandEvent& )
    { if( m_hold->GetValue() ) m_acquisition.SetHold( true ); else m_acquisition.Arm( m_history ); present(); } );
    m_mode->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& e )
    {
        m_preferences.trigger.mode = static_cast<SCOPE_TRIGGER_MODE>( e.GetSelection() ); configure();
        if( m_preferences.trigger.mode == SCOPE_TRIGGER_MODE::SINGLE ) { m_acquisition.Arm( m_history ); present(); }
    } );
    m_source->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& e )
    { m_preferences.triggerSource = m_channels.at( e.GetSelection() ).name; configure(); } );
    m_edge->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& e )
    { m_preferences.trigger.rising = e.GetSelection() == 0; configure(); } );
    m_channel->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& e )
    { m_preferences.selectedChannel = m_channels.at( e.GetSelection() ).name; syncControls(); updateCursors(); m_plot->Refresh(); } );
    m_cursorSource->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& e )
    { m_preferences.selectedChannel = m_channels.at( e.GetSelection() ).name; syncControls(); updateCursors(); m_plot->Refresh(); } );
    m_readings->Bind( wxEVT_LIST_ITEM_SELECTED, [this]( wxListEvent& e )
    { m_preferences.selectedChannel = m_channels.at( e.GetIndex() ).name; syncControls(); updateCursors(); m_plot->Refresh(); } );
    m_visible->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).visible = m_visible->GetValue(); m_plot->Refresh(); } );
    m_inverted->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).inverted = m_inverted->GetValue(); m_plot->Refresh(); } );
    m_automatic->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).automatic = m_automatic->GetValue(); m_serial = 0; present(); } );
    fit->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).automatic = true; m_serial = 0; present(); } );
    m_cursorMode->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& e )
    { m_preferences.cursorMode = e.GetSelection(); updateCursors(); m_plot->Refresh(); } );
    exportButton->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { exportCsv(); } );
    bindNumber( m_timebase, [this]( const std::string& s )
    {
        const auto v = ScopeParse( s, "s" ); if( !v || *v < 1e-9 || *v > 1e3 ) return false;
        if( m_preferences.timebase.perDivision != *v ) { m_preferences.timebase.perDivision = *v; retime(); } return true;
    } );
    auto acquisitionNumber = [this]( wxTextCtrl* field, double& target, const std::string& unit, double low, double high )
    {
        bindNumber( field, [this, &target, unit, low, high]( const std::string& s )
        {
            const auto v = ScopeParse( s, unit ); if( !v || *v < low || *v > high ) return false;
            if( target != *v ) { target = *v; if( unit == "s" && &target == &m_preferences.timebase.offset ) retime(); else configure(); } return true;
        } );
    };
    acquisitionNumber( m_level, preferences.trigger.level, "V", -1e12, 1e12 );
    acquisitionNumber( m_hysteresis, preferences.trigger.hysteresis, "V", 0, 1e12 );
    acquisitionNumber( m_holdoff, preferences.trigger.holdoff, "s", 0, 1e6 );
    acquisitionNumber( m_delay, preferences.timebase.offset, "s", -1e6, 1e6 );
    bindNumber( m_pretrigger, [this]( const std::string& s )
    {
        const auto v = ScopeParse( s, "%" ); if( !v || *v < 0 || *v > 100 ) return false;
        if( m_preferences.timebase.pretrigger != *v / 100 ) { m_preferences.timebase.pretrigger = *v / 100; retime(); } return true;
    } );
    bindNumber( m_scale, [this]( const std::string& s )
    {
        const auto v = ScopeParse( s, "V" ); if( !v || *v < 1e-12 || *v > 1e12 || m_channels.empty() ) return false;
        auto& c = style( selected() ); c.automatic = false; c.perDivision = *v;
        m_automatic->SetValue( false ); m_plot->Refresh(); return true;
    } );
    bindNumber( m_center, [this]( const std::string& s )
    {
        const auto v = ScopeParse( s, "V" ); if( !v || std::abs( *v ) > 1e12 || m_channels.empty() ) return false;
        auto& c = style( selected() ); c.automatic = false; c.center = *v;
        m_automatic->SetValue( false ); m_plot->Refresh(); return true;
    } );
    auto timeCursor = [this]( wxTextCtrl* field, double& fraction )
    {
        bindNumber( field, [this, &fraction]( const std::string& s )
        {
            const auto v = ScopeParse( s, "s" ); const auto& f = m_acquisition.Frame();
            if( !v || f.end <= f.begin ) return false;
            const double time = *v + origin(); if( time < f.begin || time > f.end ) return false;
            fraction = ( time - f.begin ) / ( f.end - f.begin ); updateCursors(); m_plot->Refresh(); return true;
        } );
    };
    timeCursor( m_cursorA, preferences.cursorA ); timeCursor( m_cursorB, preferences.cursorB );
    auto amplitudeCursor = [this]( wxTextCtrl* field, double& level )
    {
        bindNumber( field, [this, &level]( const std::string& s )
        {
            const auto v = ScopeParse( s, "V" ); if( !v || std::abs( *v ) > 1e12 ) return false;
            level = *v; updateCursors(); m_plot->Refresh(); return true;
        } );
    };
    amplitudeCursor( m_levelA, preferences.levelA ); amplitudeCursor( m_levelB, preferences.levelB );
    syncControls();
}

SCOPE_PANEL::~SCOPE_PANEL()
{
    // Deleting a focused control must not commit an edit into an already destroyed model.
    for( wxWindow* field : std::initializer_list<wxWindow*>{ m_timebase, m_scale, m_level, m_hysteresis,
            m_holdoff, m_pretrigger, m_delay, m_center, m_cursorA, m_cursorB, m_levelA, m_levelB } )
    { field->Disconnect( wxID_ANY, wxEVT_KILL_FOCUS ); field->Disconnect( wxID_ANY, wxEVT_SET_FOCUS ); }
}

std::size_t SCOPE_PANEL::selected() const
{
    for( std::size_t i = 0; i < m_channels.size(); ++i ) if( m_channels[i].name == m_preferences.selectedChannel ) return i;
    return 0;
}
SCOPE_CHANNEL_STYLE& SCOPE_PANEL::style( std::size_t c ) { return m_preferences.channels[m_channels.at( c ).name]; }
double SCOPE_PANEL::origin() const
{
    const auto& frame = m_acquisition.Frame(); return frame.triggerTime.value_or( frame.begin );
}

void SCOPE_PANEL::configure()
{
    if( m_channels.empty() ) return;
    auto source = std::find_if( m_channels.begin(), m_channels.end(), [this]( const auto& c ) { return c.name == m_preferences.triggerSource; } );
    if( source == m_channels.end() ) source = m_channels.begin();
    m_preferences.triggerSource = source->name;
    m_hold->SetValue( false );
    m_acquisition.Configure( m_preferences.timebase, m_preferences.trigger, *source, m_capacity );
    m_serial = std::numeric_limits<std::uint64_t>::max();
    m_acquisition.Feed( m_history ); present();
}

void SCOPE_PANEL::retime()
{
    const auto state = m_acquisition.State();
    if( state == SCOPE_CAPTURE_STATE::HOLD || state == SCOPE_CAPTURE_STATE::SINGLE_DONE )
    { m_acquisition.Reframe( m_preferences.timebase ); present(); }
    else configure();
}

void SCOPE_PANEL::syncControls()
{
    const auto& p = m_preferences;
    change( m_timebase, ScopeFormat( p.timebase.perDivision, "s" ) );
    m_mode->SetSelection( static_cast<int>( p.trigger.mode ) ); m_edge->SetSelection( p.trigger.rising ? 0 : 1 );
    m_cursorMode->SetSelection( p.cursorMode );
    change( m_level, ScopeFormat( p.trigger.level, "V" ) ); change( m_hysteresis, ScopeFormat( p.trigger.hysteresis, "V" ) );
    change( m_holdoff, ScopeFormat( p.trigger.holdoff, "s" ) ); change( m_delay, ScopeFormat( p.timebase.offset, "s" ) );
    change( m_pretrigger, ScopeFormat( p.timebase.pretrigger * 100, "%" ) );
    const bool triggering = p.trigger.mode != SCOPE_TRIGGER_MODE::ROLL;
    for( wxWindow* c : std::initializer_list<wxWindow*>{ m_source, m_edge, m_level, m_hysteresis, m_holdoff, m_pretrigger } ) c->Enable( triggering );
    for( std::size_t i = 0; i < m_channels.size(); ++i )
        if( m_channels[i].name == p.triggerSource ) m_source->SetSelection( i );
    if( m_channels.empty() ) return;
    m_channel->SetSelection( selected() ); m_cursorSource->SetSelection( selected() );
    const auto& c = style( selected() ); m_visible->SetValue( c.visible ); m_inverted->SetValue( c.inverted );
    m_automatic->SetValue( c.automatic ); change( m_scale, ScopeFormat( c.perDivision, "V" ) ); change( m_center, ScopeFormat( c.center, "V" ) );
}

void SCOPE_PANEL::UpdateSession( const CIRCUIT_SNAPSHOT& circuit, const EDITOR_SESSION_VIEW& session )
{
    if( m_latest == session.sample.latest.time && !m_channels.empty() ) return;
    m_latest = session.sample.latest.time; m_maxStep = circuit.config.maxStep;
    m_history = session.scopeHistory;
    if( m_channels.empty() )
    {
        // Keep disconnected channels visible in the controls with unavailable readings.
        for( const auto& c : circuit.measurements ) if( c.scope )
        {
            m_channels.push_back( c ); m_source->Append( utf8( c.name ) ); m_channel->Append( utf8( c.name ) );
            m_cursorSource->Append( utf8( c.name ) );
            m_readings->InsertItem( m_channels.size() - 1, utf8( c.name ) );
            m_readings->SetItemTextColour( m_channels.size() - 1, color( m_channels.size() - 1 ) );
        }
        m_capacity = std::min<std::size_t>( 8192, 65536 / std::max<std::size_t>( 1, circuit.config.signals.size() ) );
        configure();
    }
    else { m_acquisition.Feed( m_history ); present(); }
}

void SCOPE_PANEL::present()
{
    const auto& frame = m_acquisition.Frame();
    const wxString states[] = { _( "Waiting for trigger" ), _( "Acquiring post-trigger" ), _( "Triggered" ), _( "Auto · no edge" ),
        _( "Hold · capture frozen" ), _( "Single · capture complete" ), _( "Roll · trigger off" ) };
    m_status->SetLabel( states[static_cast<int>( m_acquisition.State() )] + ( frame.partial && !frame.samples.empty() ? _( " · partial history" ) : "" ) );
    m_status->Wrap( std::max( 240, GetClientSize().x - 15 ) );
    if( m_serial != frame.serial )
    {
        m_serial = frame.serial; m_statistics.clear();
        for( std::size_t i = 0; i < m_channels.size(); ++i )
        {
            const auto stats = ScopeStatistics( frame, m_channels[i] ); m_statistics.push_back( stats );
            auto& c = style( i );
            if( c.automatic && std::isfinite( stats.minimum ) )
            { c.center = ( stats.minimum + stats.maximum ) / 2; c.perDivision = ScopeNiceScale( std::max( stats.peakToPeak / 6, std::max( .01, std::abs( c.center ) * .02 ) ) ); }
            const std::string values[] = { ScopeFormat( stats.minimum, "V" ), ScopeFormat( stats.maximum, "V" ), ScopeFormat( stats.peakToPeak, "V" ),
                ScopeFormat( stats.mean, "V" ), ScopeFormat( stats.rms, "V" ), ScopeFormat( stats.frequency, "Hz" ), ScopeFormat( stats.period, "s" ),
                ScopeFormat( stats.duty, "%" ), std::to_string( stats.samples ) };
            for( int j = 0; j < 9; ++j ) m_readings->SetItem( i, j + 1, utf8( values[j] ) );
        }
    }
    double gap = 0; bool undersampled = false;
    for( const auto& stats : m_statistics ) { gap = std::max( gap, stats.maxGap ); undersampled |= stats.undersampled; }
    const double historySpan = m_history.size() > 1 ? m_history.back().time - m_history.front().time : 0;
    wxString info = _( "Solver max step: " ) + utf8( ScopeFormat( m_maxStep, "s" ) ) + _( " · largest displayed gap: " ) + utf8( ScopeFormat( gap, "s" ) );
    info += "\n" + _( "History: " ) + utf8( ScopeFormat( historySpan, "s" ) ) + wxString::Format( " · %zu samples", m_history.size() );
    if( undersampled || ( gap > 0 && gap > m_preferences.timebase.perDivision / 2 ) ) info += "\n" + _( "Sparse samples: zoom cannot add signal detail." );
    m_info->SetLabel( info ); m_info->Wrap( std::max( 240, GetClientSize().x - 15 ) );
    syncControls(); updateCursors(); m_plot->Refresh(); Layout();
}

void SCOPE_PANEL::autoScale()
{
    if( m_channels.empty() ) return;
    for( std::size_t i = 0; i < m_channels.size(); ++i ) style( i ).automatic = true;
    // Fit a frozen capture without replacing it; otherwise use the full available history.
    const bool frozen = m_acquisition.State() == SCOPE_CAPTURE_STATE::HOLD
                        || m_acquisition.State() == SCOPE_CAPTURE_STATE::SINGLE_DONE;
    SCOPE_FRAME frame = frozen ? m_acquisition.Frame() : SCOPE_FRAME{};
    if( !frozen )
    {
        frame.samples = m_history;
        if( !m_history.empty() ) { frame.begin = m_history.front().time; frame.end = m_history.back().time; }
    }
    const auto stats = ScopeStatistics( frame, m_channels[selected()] );
    if( std::isfinite( stats.period ) ) m_preferences.timebase.perDivision = ScopeNiceScale( stats.period * .2 );
    else if( frame.end > frame.begin ) m_preferences.timebase.perDivision = ScopeNiceScale( ( frame.end - frame.begin ) / 10 );
    if( frame.end > frame.begin )
        while( m_preferences.timebase.Span() > frame.end - frame.begin && m_preferences.timebase.perDivision > 1e-9 )
            m_preferences.timebase.perDivision = std::max( 1e-9, ScopeNiceScale( m_preferences.timebase.perDivision * .4 ) );
    const auto source = std::find_if( m_channels.begin(), m_channels.end(), [this]( const auto& c ) { return c.name == m_preferences.triggerSource; } );
    if( source != m_channels.end() )
    {
        const auto sourceStats = ScopeStatistics( frame, *source );
        if( std::isfinite( sourceStats.minimum ) ) m_preferences.trigger.level = ( sourceStats.minimum + sourceStats.maximum ) / 2;
    }
    if( frozen ) retime(); else configure();
    syncControls();
}

void SCOPE_PANEL::updateCursors()
{
    const auto& f = m_acquisition.Frame(); const auto& p = m_preferences;
    const bool time = p.cursorMode & 1, amplitude = p.cursorMode & 2;
    m_cursorSummary->Show( p.cursorMode != 0 );
    m_cursorA->Enable( time ); m_cursorB->Enable( time ); m_levelA->Enable( amplitude ); m_levelB->Enable( amplitude );
    const double a = f.begin + ( f.end - f.begin ) * p.cursorA, b = f.begin + ( f.end - f.begin ) * p.cursorB;
    change( m_cursorA, ScopeFormat( a - origin(), "s" ) ); change( m_cursorB, ScopeFormat( b - origin(), "s" ) );
    change( m_levelA, ScopeFormat( p.levelA, "V" ) ); change( m_levelB, ScopeFormat( p.levelB, "V" ) );
    if( m_channels.empty() ) return;
    std::string summary = m_channels[selected()].name;
    std::string result = m_channels[selected()].name + ( f.triggerTime ? " · t = 0: trigger" : " · t = 0: window start" );
    if( time )
    {
        const double va = ScopeValueAt( f, m_channels[selected()], a ), vb = ScopeValueAt( f, m_channels[selected()], b ), dt = b - a;
        result += "\nA: " + ScopeFormat( va, "V" ) + "    B: " + ScopeFormat( vb, "V" );
        result += "\nΔt: " + ScopeFormat( dt, "s" ) + "    1/|Δt|: " + ScopeFormat( dt ? 1 / std::abs( dt ) : NAN, "Hz" );
        result += "\nΔV(t): " + ScopeFormat( vb - va, "V" ) + "    ΔV/Δt: " + ScopeFormat( dt ? ( vb - va ) / dt : NAN, "V/s" );
        summary += " · Δt " + ScopeFormat( dt, "s" ) + " · ΔV " + ScopeFormat( vb - va, "V" )
                + " · 1/|Δt| " + ScopeFormat( dt ? 1 / std::abs( dt ) : NAN, "Hz" );
    }
    if( amplitude ) { result += "\nΔY: " + ScopeFormat( p.levelB - p.levelA, "V" ); summary += " · ΔY " + ScopeFormat( p.levelB - p.levelA, "V" ); }
    m_cursorSummary->SetLabel( utf8( summary ) ); m_cursorSummary->Wrap( std::max( 240, GetClientSize().x - 15 ) );
    m_cursorInfo->SetLabel( utf8( result ) ); m_cursorInfo->Wrap( std::max( 240, GetClientSize().x - 35 ) );
    Layout();
}

void SCOPE_PANEL::exportCsv()
{
    // Take a consistent snapshot before the modal chooser starts processing timer events.
    const auto frame = m_acquisition.Frame(); const auto channels = m_channels;
    if( frame.samples.empty() ) { wxMessageBox( _( "No capture to export." ), _( "Oscilloscope" ), wxOK | wxICON_INFORMATION, this ); return; }
    wxFileDialog dialog( this, _( "Export oscilloscope capture" ), "", "scope.csv", "CSV (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT );
    if( dialog.ShowModal() != wxID_OK ) return;
    std::ofstream out( dialog.GetPath().ToStdString() ); out.imbue( std::locale::classic() ); out << std::setprecision( 17 );
    out << "Time (s)";
    for( const auto& c : channels )
    {
        std::string name = c.name;
        for( std::size_t i = 0; ( i = name.find( '"', i ) ) != std::string::npos; i += 2 ) name.insert( i, 1, '"' );
        out << ",\"" << name << " (V)\"";
    }
    out << '\n';
    for( const auto& s : frame.samples )
    {
        if( s.time < frame.begin || s.time > frame.end ) continue;
        out << s.time;
        for( const auto& c : channels )
        {
            double v = NAN; try { v = MeasurementValue( c, s ); } catch( const std::out_of_range& ) {}
            out << ','; if( std::isfinite( v ) ) out << v;
        }
        out << '\n';
    }
    out.close();
    if( !out ) wxMessageBox( _( "Could not write the capture file." ), _( "Oscilloscope" ), wxOK | wxICON_ERROR, this );
}
}
