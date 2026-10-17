/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_panel.h"
#include "scope_plot.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include "choice_controls.h"
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
CLICK_CHOICE* choice( wxWindow* parent, std::initializer_list<wxString> labels )
{
    auto* control = new CLICK_CHOICE( parent, wxID_ANY );
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
    if( !control->HasFocus() && control->GetValue() != utf8( value ) ) control->ChangeValue( utf8( value ) );
}
void change( CLICK_CHOICE* control, const std::string& value )
{
    if( !control->HasFocus() && control->GetValue() != utf8( value ) ) control->ChangeValue( utf8( value ) );
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
    if( dynamic_cast<CLICK_CHOICE*>( control ) )
        control->Bind( wxEVT_COMBOBOX, [commit]( wxCommandEvent& ) { commit( false ); } );
}
CLICK_CHOICE* scales( wxWindow* parent, const std::string& unit, int first, int last )
{
    auto* combo = new CLICK_CHOICE( parent, wxID_ANY, "", wxDefaultPosition, wxSize( 110, -1 ), 0, nullptr, wxTE_PROCESS_ENTER );
    for( int decade = first; decade <= last; ++decade )
        for( double factor : { 1., 2., 5. } ) combo->Append( utf8( ScopeFormat( factor * std::pow( 10., decade ), unit ) ) );
    combo->SetToolTip( _( "Choose a scale or type an SI value, for example 20 ms or 500 mV. Press Enter to apply." ) );
    return combo;
}
}

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
    m_status = new wxStaticText( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END );
    m_status->SetMinSize( wxSize( 0, FromDIP( 20 ) ) ); layout->Add( m_status, 0, wxEXPAND | wxALL, 5 );
    m_plot = new SCOPE_PLOT( this, *this ); layout->Add( m_plot, 3, wxEXPAND | wxLEFT | wxRIGHT, 4 );
    m_cursorSummary = new wxStaticText( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE );
    m_cursorSummary->SetMinSize( wxSize( 0, FromDIP( 36 ) ) );
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
    m_cursorInfo = new wxStaticText( cursors, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE );
    m_cursorInfo->SetMinSize( wxSize( 0, FromDIP( 100 ) ) ); cursors->GetSizer()->Add( m_cursorInfo, 0, wxEXPAND | wxALL, 8 );
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
    m_info = new wxStaticText( this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE );
    m_info->SetMinSize( wxSize( 0, FromDIP( 54 ) ) ); layout->Add( m_info, 0, wxEXPAND | wxALL, 5 );
    SetSizer( layout );

    automatic->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { autoScale(); } );
    enlarge->Bind( wxEVT_BUTTON, [expand]( wxCommandEvent& ) { expand(); } );
    arm->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    { m_hold->SetValue( false ); m_acquisition.Arm( history() ); present(); } );
    m_hold->Bind( wxEVT_TOGGLEBUTTON, [this]( wxCommandEvent& )
    { if( m_hold->GetValue() ) m_acquisition.SetHold( true ); else m_acquisition.Arm( history() ); present(); } );
    m_mode->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& e )
    {
        m_preferences.trigger.mode = static_cast<SCOPE_TRIGGER_MODE>( e.GetSelection() ); configure();
        if( m_preferences.trigger.mode == SCOPE_TRIGGER_MODE::SINGLE ) { m_acquisition.Arm( history() ); present(); }
    } );
    m_source->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& e )
    { m_preferences.triggerSource = m_channels.at( e.GetSelection() ).name; configure(); } );
    m_edge->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& e )
    { m_preferences.trigger.rising = e.GetSelection() == 0; configure(); } );
    m_channel->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& e )
    { m_preferences.selectedChannel = m_channels.at( e.GetSelection() ).name; syncControls(); updateCursors(); m_plot->Refresh(); } );
    m_cursorSource->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& e )
    { m_preferences.selectedChannel = m_channels.at( e.GetSelection() ).name; syncControls(); updateCursors(); m_plot->Refresh(); } );
    m_readings->Bind( wxEVT_LIST_ITEM_SELECTED, [this]( wxListEvent& e )
    { m_preferences.selectedChannel = m_channels.at( e.GetIndex() ).name; syncControls(); updateCursors(); m_plot->Refresh(); } );
    m_visible->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).visible = m_visible->GetValue(); m_plot->Refresh(); } );
    m_inverted->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    {
        if( !m_channels.empty() )
        {
            auto& c = style( selected() ); const double zero = ScopeVerticalFraction( 0, c );
            c.inverted = m_inverted->GetValue(); c.center = ScopeCenterForZero( zero, c ); syncControls();
        }
        m_plot->Refresh( false );
    } );
    m_automatic->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).automatic = m_automatic->GetValue(); m_serial = 0; present(); } );
    fit->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    { if( !m_channels.empty() ) style( selected() ).automatic = true; m_serial = 0; present(); } );
    m_cursorMode->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& e )
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
        auto& c = style( selected() ); const double zero = ScopeVerticalFraction( 0, c );
        c.automatic = false; c.perDivision = *v; c.center = ScopeCenterForZero( zero, c );
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
    m_controls->Bind( wxEVT_NOTEBOOK_PAGE_CHANGED, [this]( wxBookCtrlEvent& e ) { present(); e.Skip(); } );
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
    m_acquisition.Configure( m_preferences.timebase, m_preferences.trigger, *source, m_capacity, m_channels );
    m_serial = std::numeric_limits<std::uint64_t>::max();
    m_acquisition.Feed( history() ); syncControls(); present();
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
    auto select = []( CLICK_CHOICE* field, int value )
    { if( field->GetSelection() != value ) field->SetSelection( value ); };
    auto check = []( wxCheckBox* field, bool value )
    { if( field->GetValue() != value ) field->SetValue( value ); };
    change( m_timebase, ScopeFormat( p.timebase.perDivision, "s" ) );
    select( m_mode, static_cast<int>( p.trigger.mode ) ); select( m_edge, p.trigger.rising ? 0 : 1 );
    select( m_cursorMode, p.cursorMode );
    change( m_level, ScopeFormat( p.trigger.level, "V" ) ); change( m_hysteresis, ScopeFormat( p.trigger.hysteresis, "V" ) );
    change( m_holdoff, ScopeFormat( p.trigger.holdoff, "s" ) ); change( m_delay, ScopeFormat( p.timebase.offset, "s" ) );
    change( m_pretrigger, ScopeFormat( p.timebase.pretrigger * 100, "%" ) );
    const bool triggering = p.trigger.mode != SCOPE_TRIGGER_MODE::ROLL;
    for( wxWindow* field : std::initializer_list<wxWindow*>{ m_source, m_edge, m_level, m_hysteresis, m_holdoff, m_pretrigger } )
        if( field->IsEnabled() != triggering ) field->Enable( triggering );
    for( std::size_t i = 0; i < m_channels.size(); ++i )
        if( m_channels[i].name == p.triggerSource ) select( m_source, i );
    if( m_channels.empty() ) return;
    select( m_channel, selected() ); select( m_cursorSource, selected() );
    const auto& c = style( selected() );
    check( m_visible, c.visible ); check( m_inverted, c.inverted ); check( m_automatic, c.automatic );
    change( m_scale, ScopeFormat( c.perDivision, "V" ) ); change( m_center, ScopeFormat( c.center, "V" ) );
}

const SCOPE_HISTORY& SCOPE_PANEL::history() const
{
    static const SCOPE_HISTORY empty;
    return m_history ? *m_history : empty;
}
void SCOPE_PANEL::UpdateSession( const CIRCUIT_SNAPSHOT& circuit, const EDITOR_SESSION_VIEW& session )
{
    const bool changed = m_history != session.scopeHistory;
    m_latest = session.sample.latest.time; m_maxStep = circuit.config.maxStep;
    m_history = session.scopeHistory;
    if( m_channels.empty() )
    {
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
    else if( changed ) m_acquisition.Feed( history() );
    const bool visible = IsShownOnScreen();
    const auto now = std::chrono::steady_clock::now();
    if( visible && ( !m_wasVisible || now - m_lastPresentation >= std::chrono::milliseconds( 50 ) ) )
    { present(); m_lastPresentation = now; }
    m_wasVisible = visible;
}
void SCOPE_PANEL::triggerDragged()
{
    m_acquisition.SetTriggerLevel( m_preferences.trigger.level );
    syncControls(); present();
}
void SCOPE_PANEL::present()
{
    if( !IsShownOnScreen() ) return;
    const auto& frame = m_acquisition.Frame();
    const bool changed = m_serial != frame.serial;
    const wxString states[] = { _( "Waiting for trigger" ), _( "Acquiring post-trigger" ), _( "Triggered" ), _( "Auto · no edge" ),
        _( "Hold · capture frozen" ), _( "Single · capture complete" ), _( "Roll · trigger off" ) };
    const auto status = states[static_cast<int>( m_acquisition.State() )] + ( frame.partial && !frame.samples.empty() ? _( " · partial history" ) : "" );
    if( m_status->GetLabel() != status ) m_status->SetLabel( status );
    bool scaleChanged = false;
    if( changed )
    {
        m_serial = frame.serial; m_frameGap = 0;
        for( std::size_t i = 1; i < frame.samples.size(); ++i )
            if( frame.samples[i].time >= frame.begin && frame.samples[i-1].time <= frame.end )
                m_frameGap = std::max( m_frameGap, frame.samples[i].time - frame.samples[i-1].time );
        for( std::size_t i = 0; i < m_channels.size(); ++i )
        {
            auto& c = style( i ); if( !c.automatic ) continue;
            double low = INFINITY, high = -INFINITY;
            for( const auto& sample : frame.samples )
            {
                if( sample.time < frame.begin || sample.time > frame.end ) continue;
                double v = NAN; try { v = MeasurementValue( m_channels[i], sample ); } catch( const std::out_of_range& ) {}
                if( !std::isfinite( v ) ) continue;
                low = std::min( low, v ); high = std::max( high, v );
            }
            if( std::isfinite( low ) )
            {
                const auto oldScale = c.perDivision, oldCenter = c.center;
                ScopeFitRange( c, low, high, .8 - .15 * ( i % 4 ) );
                scaleChanged |= oldScale != c.perDivision || oldCenter != c.center;
            }
        }
    }
    // Full statistics and the native list are only updated while their page is visible, at <=5 Hz.
    const auto now = std::chrono::steady_clock::now();
    if( m_controls->GetSelection() == 3 && ( m_readingsSerial != frame.serial )
        && now - m_lastReadings >= std::chrono::milliseconds( 200 ) )
    {
        m_lastReadings = now; m_readingsSerial = frame.serial; m_statistics.clear();
        m_readings->Freeze();
        for( std::size_t i = 0; i < m_channels.size(); ++i )
        {
            const auto stats = ScopeStatistics( frame, m_channels[i] ); m_statistics.push_back( stats );
            const std::string values[] = { ScopeFormat( stats.minimum, "V" ), ScopeFormat( stats.maximum, "V" ), ScopeFormat( stats.peakToPeak, "V" ),
                ScopeFormat( stats.mean, "V" ), ScopeFormat( stats.rms, "V" ), ScopeFormat( stats.frequency, "Hz" ), ScopeFormat( stats.period, "s" ),
                ScopeFormat( stats.duty, "%" ), std::to_string( stats.samples ) };
            for( int j = 0; j < 9; ++j )
                if( m_readings->GetItemText( i, j + 1 ) != utf8( values[j] ) ) m_readings->SetItem( i, j + 1, utf8( values[j] ) );
        }
        m_readings->Thaw();
    }
    const auto& h = history(); const double span = h.empty() ? 0 : h.back().time - h.front().time;
    wxString info = _( "Solver step ≤ " ) + utf8( ScopeFormat( m_maxStep, "s" ) ) + _( " · gap " ) + utf8( ScopeFormat( m_frameGap, "s" ) );
    info += "\n" + _( "Input history: " ) + utf8( ScopeFormat( span, "s" ) ) + wxString::Format( " · %zu points", h.size() );
    if( frame.decimated ) info += "\n" + _( "Envelope capture · measurements are estimates" );
    else if( m_frameGap > m_preferences.timebase.perDivision / 2 ) info += "\n" + _( "Sparse samples · zoom adds no detail" );
    if( m_info->GetLabel() != info ) m_info->SetLabel( info );
    if( scaleChanged ) syncControls();
    if( changed ) { updateCursors(); m_plot->Refresh( false ); }
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
        frame.samples = history().Copy();
        if( !history().empty() ) { frame.begin = history().front().time; frame.end = history().back().time; }
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
    const bool layoutChanged = m_cursorSummary->IsShown() != ( p.cursorMode != 0 );
    if( layoutChanged ) m_cursorSummary->Show( p.cursorMode != 0 );
    for( auto* field : { m_cursorA, m_cursorB } ) if( field->IsEnabled() != time ) field->Enable( time );
    for( auto* field : { m_levelA, m_levelB } ) if( field->IsEnabled() != amplitude ) field->Enable( amplitude );
    if( !p.cursorMode ) { if( layoutChanged ) Layout(); return; }
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
                + "\n1/|Δt| " + ScopeFormat( dt ? 1 / std::abs( dt ) : NAN, "Hz" );
    }
    if( amplitude ) { result += "\nΔY: " + ScopeFormat( p.levelB - p.levelA, "V" ); summary += " · ΔY " + ScopeFormat( p.levelB - p.levelA, "V" ); }
    if( m_cursorSummary->GetLabel() != utf8( summary ) ) m_cursorSummary->SetLabel( utf8( summary ) );
    if( m_cursorInfo->GetLabel() != utf8( result ) ) m_cursorInfo->SetLabel( utf8( result ) );
    if( layoutChanged ) Layout();
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
