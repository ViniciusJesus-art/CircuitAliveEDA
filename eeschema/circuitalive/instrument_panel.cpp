/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "instrument_panel.h"
#include "choice_controls.h"
#include <eeschema_settings.h>
#include <paths.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <widgets/wx_aui_utils.h>
#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/tooltip.h>
#include <algorithm>

namespace CIRCUITALIVE
{
INSTRUMENT_PANEL::INSTRUMENT_PANEL( wxWindow* parent, std::function<void( PROBE_KIND )> choose,
        std::function<void( double )> speed, std::function<void()> step ) : wxPanel( parent )
{
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* time = new wxBoxSizer( wxHORIZONTAL );
    time->Add( new wxStaticText( this, wxID_ANY, _( "Speed" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
    m_speed = new CLICK_CHOICE( this );
    for( const char* label : { "1×", "0.1×", "0.01×", "0.001×" } ) m_speed->Append( wxString::FromUTF8( label ) );
    m_speed->SetSelection( 0 );
    m_speed->SetToolTip( _( "Slow the simulation to see sequential logic. This changes the playback rate, not component timing." ) );
    m_speed->Bind( wxEVT_COMBOBOX, [this, speed]( wxCommandEvent& event )
    {
        const auto index = event.GetSelection();
        if( index >= 0 && index < int( m_speedRates.size() ) ) speed( m_speedRates[index] );
    } );
    time->Add( m_speed, 1, wxRIGHT, 8 );
    m_step = new wxButton( this, wxID_ANY, _( "Step" ) );
    m_step->SetToolTip( _( "While paused, advance by the step duration configured in Real-time Simulation preferences." ) );
    m_step->Bind( wxEVT_BUTTON, [step]( wxCommandEvent& ) { step(); } );
    m_step->Disable(); time->Add( m_step );
    layout->Add( time, 0, wxEXPAND | wxALL, 6 );
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
    m_measurePage = new wxPanel( m_tabs );
    auto* measurements = new wxBoxSizer( wxVERTICAL );
    m_values = new wxListCtrl( m_measurePage, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL );
    m_values->InsertColumn( 0, _( "Name" ), wxLIST_FORMAT_LEFT, 108 );
    m_values->InsertColumn( 1, _( "Type" ), wxLIST_FORMAT_LEFT, 42 );
    m_values->InsertColumn( 2, _( "Value" ), wxLIST_FORMAT_LEFT, 140 );
    measurements->Add( m_values, 1, wxEXPAND | wxALL, 4 );
    auto* help = new wxStaticText( m_measurePage, wxID_ANY,
                                  _( "Component P: positive = absorbed, negative = delivered." ) );
    help->Wrap( 300 );
    measurements->Add( help, 0, wxALL, 6 );
    m_measurePage->SetSizer( measurements );
    m_tabs->AddPage( m_measurePage, _( "Measurements" ) );

    m_scopePage = new wxPanel( m_tabs );
    auto* scope = new wxBoxSizer( wxVERTICAL );
    m_scope = new SCOPE_PANEL( m_scopePage, m_scopePreferences, [this]()
    {
        if( auto* manager = wxAuiManager::GetManager( GetParent() ) )
        {
            auto& pane = manager->GetPane( this );
            const bool docking = pane.IsFloating();
            const int dockWidth = pane.best_size.x;
            if( docking )
                pane.Dock();
            else
                pane.Float().FloatingPosition( GetParent()->GetScreenPosition() + FromDIP( wxPoint( 40, 40 ) ) );
            manager->Update();
            if( docking && dockWidth > 0 )
                SetAuiPaneSize( *manager, pane, dockWidth, -1 );
        }
    } );
    scope->Add( m_scope, 1, wxEXPAND );
    m_scopePage->SetSizer( scope );
    m_tabs->AddPage( m_scopePage, _( "Oscilloscope" ) );
    if( auto* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) )
    {
        const auto selected = cfg->Get<int>( "appearance.simulation_tab" );
        m_hasTabPreference = selected.has_value();
        m_tabs->ChangeSelection( std::clamp( selected.value_or( 0 ), 0, 2 ) );
    }
    m_tabs->Bind( wxEVT_NOTEBOOK_PAGE_CHANGED, &INSTRUMENT_PANEL::tabChanged, this );
    layout->Add( m_tabs, 1, wxEXPAND ); SetSizer( layout );
}

INSTRUMENT_PANEL::~INSTRUMENT_PANEL()
{
    // Scope controls reference m_scopePreferences; destroy them before that member.
    delete m_tabs;
}

std::string INSTRUMENT_PANEL::ProbeName() const { return m_name->GetValue().ToStdString(); }
void INSTRUMENT_PANEL::ClearProbeName() { m_name->Clear(); }
void INSTRUMENT_PANEL::tabChanged( wxBookCtrlEvent& event )
{
    m_hasTabPreference = true;
    if( auto* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) )
    {
        cfg->Set<int>( "appearance.simulation_tab", m_tabs->GetSelection() );
        cfg->SaveToFile( PATHS::GetUserSettingsPath(), true );
    }
    event.Skip();
}
void INSTRUMENT_PANEL::SetPlacement( std::optional<PROBE_KIND> kind )
{
    for( std::size_t i = 0; i < m_buttons.size(); ++i ) m_buttons[i]->SetValue( kind && static_cast<std::size_t>( *kind ) == i );
    m_help->SetLabel( kind ? _( "Click a wire to place the probe. Esc finishes placement." ) : _( "Choose a tool, click a wire. Esc finishes placement." ) );
    m_help->Wrap( 300 );
}

void INSTRUMENT_PANEL::UpdateSession( const SCHEMATIC_SNAPSHOT& snapshot, const EDITOR_SESSION_VIEW& session, bool active )
{
    m_step->Enable( active && session.state == EDITOR_SESSION_STATE::PAUSED );
    auto rate = std::find( m_speedRates.begin(), m_speedRates.end(), session.speed );
    int speedIndex = rate - m_speedRates.begin();
    if( rate == m_speedRates.end() )
    {
        speedIndex = 4;
        const auto label = wxString::Format( "%.4g×", session.speed );
        if( m_speedRates.size() == 4 ) { m_speedRates.push_back( session.speed ); m_speed->Append( label ); }
        else { m_speedRates[4] = session.speed; m_speed->SetString( 4, label ); }
    }
    if( m_speed->GetSelection() != speedIndex ) m_speed->SetSelection( speedIndex );
    const auto stepTip = wxString::Format( _( "While paused, advance %.3g ms × playback speed, preserving the circuit state." ),
                                          session.preferences.stepMs );
    if( !m_step->GetToolTip() || m_step->GetToolTip()->GetTip() != stepTip ) m_step->SetToolTip( stepTip );
    bool values = false, scope = false;
    for( const auto& channel : snapshot.circuit.measurements )
        if( channel.scope ) scope = true; else values = true;
    // Keep the controls alive across Stop/Play, including their column widths, scope
    // settings and notebook selections. Only the acquired circuit data is reset.
    if( active != m_wasActive )
        m_scope->ResetSession();
    const auto now = std::chrono::steady_clock::now();
    if( m_values && m_values->IsShownOnScreen() && now - m_lastValues >= std::chrono::milliseconds( 100 ) )
    {
        m_lastValues = now;
        std::size_t row = 0;
        for( std::size_t i = 0; i < snapshot.circuit.measurements.size(); ++i )
        {
            const auto& channel = snapshot.circuit.measurements[i]; if( channel.scope ) continue;
            if( row >= m_rows.size() )
            {
                m_values->InsertItem( row, wxString::FromUTF8( channel.name ) );
                m_values->SetItem( row, 1, wxString::FromUTF8( QuantityUnit( channel.quantity ) ) ); m_rows.push_back( channel.name );
            }
            if( m_values->GetItemText( row ) != wxString::FromUTF8( channel.name ) ) m_values->SetItem( row, 0, wxString::FromUTF8( channel.name ) );
            const auto value = i < session.measurements.size() && session.state != EDITOR_SESSION_STATE::FAILED
                    ? FormatMeasurement( session.measurements[i], channel.quantity ) : "—";
            if( m_values->GetItemText( row, 2 ) != wxString::FromUTF8( value ) ) m_values->SetItem( row, 2, wxString::FromUTF8( value ) );
            ++row;
        }
        while( m_values->GetItemCount() > static_cast<long>( row ) )
            m_values->DeleteItem( m_values->GetItemCount() - 1 );
        m_rows.resize( row );
    }
    if( m_scope ) m_scope->UpdateSession( snapshot.circuit, session );
    if( active && !m_wasActive && !m_hasTabPreference )
    {
        if( values || scope )
            m_tabs->SetSelection( values ? 1 : 2 );
    }
    m_wasActive = active;
}
}
