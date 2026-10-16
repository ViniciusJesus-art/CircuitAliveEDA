/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "instrument_panel.h"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace CIRCUITALIVE
{
class SCOPE_PLOT : public wxPanel
{
public:
    explicit SCOPE_PLOT( wxWindow* parent ) : wxPanel( parent )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT ); SetMinSize( wxSize( 240, 260 ) );
        Bind( wxEVT_PAINT, &SCOPE_PLOT::paint, this );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& e ) { Refresh(); e.Skip(); } );
    }
    void SetWindow( double duration ) { m_window = duration; Refresh(); }
    void SetData( const CIRCUIT_SNAPSHOT& circuit, const EDITOR_SESSION_VIEW& session )
    {
        if( m_time == session.sample.latest.time && !m_history.empty() ) return;
        m_time = session.sample.latest.time;
        m_channels.clear();
        for( const auto& c : circuit.measurements ) if( c.scope && c.connected ) m_channels.push_back( c );
        m_history = session.scopeHistory; Refresh();
    }
private:
    void paint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        dc.SetBackground( wxBrush( wxColour( 18, 23, 28 ) ) ); dc.Clear();
        dc.SetTextForeground( wxColour( 205, 220, 225 ) );
        const auto size = GetClientSize();
        const wxRect plot( 55, 32, std::max( 1, size.x - 66 ), std::max( 1, size.y - 68 ) );
        const double end = std::max( m_window, m_time ), begin = end - m_window;
        double low = std::numeric_limits<double>::infinity(), high = -low;
        for( const auto& sample : m_history )
            if( sample.time >= begin )
                for( const auto& channel : m_channels )
                {
                    const auto value = MeasurementValue( channel, sample );
                    if( std::isfinite( value ) ) { low = std::min( low, value ); high = std::max( high, value ); }
                }
        if( !std::isfinite( low ) ) { low = -1; high = 1; }
        if( high - low < 1e-6 ) { low -= .5; high += .5; }
        else { const auto margin = ( high - low ) * .1; low -= margin; high += margin; }
        dc.SetPen( wxPen( wxColour( 47, 60, 69 ) ) );
        for( int i = 0; i <= 10; ++i ) dc.DrawLine( plot.x + plot.width * i / 10, plot.y, plot.x + plot.width * i / 10, plot.GetBottom() );
        for( int i = 0; i <= 8; ++i ) dc.DrawLine( plot.x, plot.y + plot.height * i / 8, plot.GetRight(), plot.y + plot.height * i / 8 );
        dc.DrawText( wxString::Format( "%.3g V", high ), 2, plot.y );
        dc.DrawText( wxString::Format( "%.3g V", low ), 2, plot.GetBottom() - 12 );
        dc.DrawText( wxString::Format( "%.4g s", begin ), plot.x, plot.GetBottom() + 8 );
        dc.DrawText( wxString::Format( "%.4g s", end ), std::max( plot.x, plot.GetRight() - 60 ), plot.GetBottom() + 8 );
        const wxColour colors[] = { wxColour( 85, 225, 115 ), wxColour( 255, 208, 70 ),
            wxColour( 80, 185, 255 ), wxColour( 255, 120, 185 ), wxColour( 195, 150, 255 ) };
        auto y = [&]( double value ) { return plot.GetBottom() - int( ( value - low ) / ( high - low ) * plot.height ); };
        int legendX = 6;
        for( std::size_t c = 0; c < m_channels.size(); ++c )
        {
            const auto& channel = m_channels[c];
            dc.SetTextForeground( colors[c % 5] );
            const auto label = wxString::FromUTF8( channel.name );
            if( legendX < size.x - 30 ) { dc.DrawText( label, legendX, 6 ); legendX += dc.GetTextExtent( label ).x + 16; }
            std::vector<double> minimum( plot.width + 1, std::numeric_limits<double>::infinity() );
            std::vector<double> maximum( plot.width + 1, -std::numeric_limits<double>::infinity() );
            for( const auto& sample : m_history )
            {
                if( sample.time < begin || sample.time > end ) continue;
                const double value = MeasurementValue( channel, sample );
                if( !std::isfinite( value ) ) continue;
                const int x = std::clamp( int( ( sample.time - begin ) / m_window * plot.width ), 0, plot.width );
                minimum[x] = std::min( minimum[x], value ); maximum[x] = std::max( maximum[x], value );
            }
            dc.SetClippingRegion( plot ); dc.SetPen( wxPen( colors[c % 5], 2 ) );
            wxPoint previous; bool connected = false;
            for( int x = 0; x <= plot.width; ++x )
            {
                if( !std::isfinite( minimum[x] ) ) continue;
                wxPoint point( plot.x + x, y( ( minimum[x] + maximum[x] ) / 2 ) );
                dc.DrawLine( point.x, y( minimum[x] ), point.x, y( maximum[x] ) );
                if( connected ) dc.DrawLine( previous, point );
                previous = point; connected = true;
            }
            dc.DestroyClippingRegion();
        }
        if( m_history.empty() )
        { dc.SetTextForeground( wxColour( 205, 220, 225 ) ); dc.DrawText( _( "Waiting for samples" ), plot.x + 8, plot.y + 12 ); }
    }
    double m_window = .25, m_time = -1;
    std::vector<MEASUREMENT_CHANNEL> m_channels;
    std::vector<SIM_SAMPLE> m_history;
};

INSTRUMENT_PANEL::INSTRUMENT_PANEL( wxWindow* parent, std::function<void( PROBE_KIND )> choose ) : wxPanel( parent )
{
    auto* layout = new wxBoxSizer( wxVERTICAL );
    m_tabs = new wxNotebook( this, wxID_ANY );
    auto* tools = new wxPanel( m_tabs );
    auto* contents = new wxBoxSizer( wxVERTICAL );
    contents->Add( new wxStaticText( tools, wxID_ANY, _( "Place a measurement probe on a wire" ) ), 0, wxALL, 8 );
    auto* buttons = new wxGridSizer( 2, 6, 6 );
    const wxString labels[] = { _( "Voltage (V)" ), _( "Current (A)" ), _( "Power (W)" ), _( "Oscilloscope" ) };
    const char* letters[] = { "V", "A", "W", "~" };
    for( int i = 0; i < 4; ++i )
    {
        auto* button = new wxToggleButton( tools, wxID_ANY, labels[i], wxDefaultPosition, wxSize( 145, 42 ) );
        const std::string svg = "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'>"
            "<path d='M3 21L18 6M9 6H18V15' stroke='#318fd8' stroke-width='2' fill='none'/>"
            "<text x='1' y='10' font-size='10' fill='#318fd8'>" + std::string( letters[i] ) + "</text></svg>";
        button->SetBitmap( wxBitmapBundle::FromSVG( svg.c_str(), wxSize( 24, 24 ) ) );
        button->Bind( wxEVT_TOGGLEBUTTON, [choose, i]( wxCommandEvent& ) { choose( static_cast<PROBE_KIND>( i ) ); } );
        buttons->Add( button, 1, wxEXPAND ); m_buttons.push_back( button );
    }
    contents->Add( buttons, 0, wxEXPAND | wxALL, 8 );
    contents->Add( new wxStaticText( tools, wxID_ANY, _( "Probe name (optional)" ) ), 0, wxLEFT | wxRIGHT | wxTOP, 8 );
    m_name = new wxTextCtrl( tools, wxID_ANY ); contents->Add( m_name, 0, wxEXPAND | wxALL, 8 );
    m_help = new wxStaticText( tools, wxID_ANY, _( "Choose a tool, click a wire. Esc finishes placement." ) );
    m_help->Wrap( 300 ); contents->Add( m_help, 0, wxEXPAND | wxALL, 8 );
    auto* description = new wxStaticText( tools, wxID_ANY,
        _( "V and scope use the circuit reference.\nA and W insert an ideal series sensor.\nCurrent: left → right or top → bottom.\nWire power: V(reference) × I.\n\nComponent V / I / P: Simulation model..." ) );
    description->Wrap( 300 ); contents->Add( description, 0, wxALL, 8 );
    tools->SetSizer( contents ); m_tabs->AddPage( tools, _( "Probes" ), true );
    layout->Add( m_tabs, 1, wxEXPAND ); SetSizer( layout );
}

std::string INSTRUMENT_PANEL::ProbeName() const { return m_name->GetValue().ToStdString(); }
void INSTRUMENT_PANEL::ClearProbeName() { m_name->Clear(); }
void INSTRUMENT_PANEL::SelectTools() { m_tabs->SetSelection( 0 ); }
void INSTRUMENT_PANEL::SetPlacement( std::optional<PROBE_KIND> kind )
{
    for( std::size_t i = 0; i < m_buttons.size(); ++i ) m_buttons[i]->SetValue( kind && static_cast<std::size_t>( *kind ) == i );
    m_help->SetLabel( kind ? _( "Click a wire to place the probe. Esc finishes placement." ) : _( "Choose a tool, click a wire. Esc finishes placement." ) );
    m_help->Wrap( 300 );
}

void INSTRUMENT_PANEL::UpdateSession( const SCHEMATIC_SNAPSHOT& snapshot, const EDITOR_SESSION_VIEW& session, bool active )
{
    bool values = false, scope = false;
    for( const auto& channel : snapshot.circuit.measurements )
        if( channel.scope ) scope = true; else values = true;
    values &= active; scope &= active;
    auto remove = [&]( wxPanel*& page )
    {
        if( !page ) return;
        const int index = m_tabs->FindPage( page );
        if( index != wxNOT_FOUND ) m_tabs->DeletePage( index );
        page = nullptr;
    };
    if( !values ) { remove( m_measurePage ); m_values = nullptr; m_rows.clear(); }
    if( !scope ) { remove( m_scopePage ); m_plot = nullptr; m_scopeInfo = nullptr; }
    if( values && !m_measurePage )
    {
        m_measurePage = new wxPanel( m_tabs ); auto* layout = new wxBoxSizer( wxVERTICAL );
        m_values = new wxListCtrl( m_measurePage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL );
        m_values->InsertColumn( 0, _( "Name" ), wxLIST_FORMAT_LEFT, 108 );
        m_values->InsertColumn( 1, _( "Type" ), wxLIST_FORMAT_LEFT, 42 );
        m_values->InsertColumn( 2, _( "Value" ), wxLIST_FORMAT_LEFT, 140 );
        layout->Add( m_values, 1, wxEXPAND | wxALL, 4 );
        auto* help = new wxStaticText( m_measurePage, wxID_ANY, _( "Component P: positive = absorbed, negative = delivered." ) );
        help->Wrap( 300 ); layout->Add( help, 0, wxALL, 6 ); m_measurePage->SetSizer( layout );
        m_tabs->AddPage( m_measurePage, _( "Measurements" ) );
    }
    if( scope && !m_scopePage )
    {
        m_scopePage = new wxPanel( m_tabs ); auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* timebase = new wxChoice( m_scopePage, wxID_ANY );
        for( const wxString label : { "1 ms", "10 ms", "50 ms", "100 ms", "250 ms", "500 ms" } ) timebase->Append( label );
        timebase->SetSelection( 4 ); layout->Add( new wxStaticText( m_scopePage, wxID_ANY, _( "Time window · automatic voltage range" ) ), 0, wxALL, 6 );
        layout->Add( timebase, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6 );
        m_plot = new SCOPE_PLOT( m_scopePage ); layout->Add( m_plot, 1, wxEXPAND | wxALL, 4 );
        timebase->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& event )
            { const double windows[] = { .001, .01, .05, .1, .25, .5 }; if( m_plot ) m_plot->SetWindow( windows[event.GetSelection()] ); } );
        m_scopeInfo = new wxStaticText( m_scopePage, wxID_ANY, "" ); layout->Add( m_scopeInfo, 0, wxEXPAND | wxALL, 6 );
        m_scopePage->SetSizer( layout ); m_tabs->AddPage( m_scopePage, _( "Oscilloscope" ) );
    }
    if( m_values )
    {
        std::size_t row = 0;
        for( std::size_t i = 0; i < snapshot.circuit.measurements.size(); ++i )
        {
            const auto& channel = snapshot.circuit.measurements[i]; if( channel.scope ) continue;
            if( row >= m_rows.size() )
            {
                m_values->InsertItem( row, wxString::FromUTF8( channel.name ) );
                m_values->SetItem( row, 1, wxString::FromUTF8( QuantityUnit( channel.quantity ) ) ); m_rows.push_back( channel.name );
            }
            m_values->SetItem( row, 0, wxString::FromUTF8( channel.name ) );
            const auto value = i < session.measurements.size() && session.state != EDITOR_SESSION_STATE::ERROR
                    ? FormatMeasurement( session.measurements[i], channel.quantity ) : "—";
            m_values->SetItem( row, 2, wxString::FromUTF8( value ) ); ++row;
        }
    }
    if( m_plot )
    {
        m_plot->SetData( snapshot.circuit, session );
        const double span = session.scopeHistory.size() > 1 ? session.scopeHistory.back().time - session.scopeHistory.front().time : 0;
        m_scopeInfo->SetLabel( wxString::Format( _( "Max step: %.4g µs\nAvailable history: %.4g ms (%zu samples)" ),
            snapshot.circuit.config.maxStep * 1e6, span * 1e3, session.scopeHistory.size() ) );
    }
    if( active && !m_wasActive )
    {
        auto* page = m_measurePage ? m_measurePage : m_scopePage;
        if( page ) m_tabs->SetSelection( m_tabs->FindPage( page ) );
    }
    m_wasActive = active;
}
}
