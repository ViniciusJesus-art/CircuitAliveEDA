/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "panel_simulation_preferences.h"
#include <eeschema_settings.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <widgets/ui_common.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <cmath>

namespace CIRCUITALIVE
{
PANEL_SIMULATION_PREFERENCES::PANEL_SIMULATION_PREFERENCES( wxWindow* parent ) :
        RESETTABLE_PANEL( parent )
{
    auto* scroll = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxVSCROLL | wxTAB_TRAVERSAL );
    scroll->SetScrollRate( 0, 10 );
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto note = [&]( const wxString& text )
    {
        auto* label = new wxStaticText( scroll, wxID_ANY, text );
        label->SetFont( KIUI::GetSmallInfoFont( label ) );
        label->Wrap( FromDIP( 620 ) );
        layout->Add( label, 0, wxEXPAND | wxALL, 8 );
    };
    note( _( "Configure the simulation that runs directly on the schematic. Component models and probe settings remain with the circuit." ) );
    auto group = [&]( const wxString& title )
    {
        auto* box = new wxStaticBoxSizer( wxVERTICAL, scroll, title );
        layout->Add( box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );
        return box;
    };
    auto grid = [&]( wxStaticBoxSizer* box )
    {
        auto* rows = new wxFlexGridSizer( 3, 6, 10 );
        rows->AddGrowableCol( 0 );
        box->Add( rows, 0, wxEXPAND | wxALL, 8 );
        return rows;
    };
    auto number = [&]( wxFlexGridSizer* rows, const wxString& label, const wxString& unit,
                       double low, double high, int digits, double increment, const wxString& help )
    {
        rows->Add( new wxStaticText( scroll, wxID_ANY, label ), 0, wxALIGN_CENTER_VERTICAL );
        auto* control = new wxSpinCtrlDouble( scroll, wxID_ANY, wxEmptyString, wxDefaultPosition,
                FromDIP( wxSize( 120, -1 ) ), wxSP_ARROW_KEYS, low, high, low, increment );
        control->SetDigits( digits );
        control->SetName( label );
        control->SetToolTip( help );
        rows->Add( control, 0, wxALIGN_CENTER_VERTICAL );
        rows->Add( new wxStaticText( scroll, wxID_ANY, unit ), 0, wxALIGN_CENTER_VERTICAL );
        m_numbers.push_back( { control, label } );
        return control;
    };
    auto checkbox = [&]( wxStaticBoxSizer* box, const wxString& label, const wxString& help )
    {
        auto* control = new wxCheckBox( scroll, wxID_ANY, label );
        control->SetToolTip( help );
        box->Add( control, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );
        return control;
    };

    auto* indicators = group( _( "LEDs and digital indicators" ) );
    auto* indicatorRows = grid( indicators );
    m_ledCutoff = number( indicatorRows, _( "LED frequency cutoff:" ), _( "Hz" ), 1, 1000, 1, 1,
            _( "At or above this frequency, keep the LED lit at the brightness of its on phase. The cutoff follows playback speed." ) );
    m_digitalCutoff = number( indicatorRows, _( "Digital indicator frequency cutoff:" ), _( "Hz" ), 1, 1000, 1, 1,
            _( "At or above this frequency, show a purple dot with ~ for fast switching. The cutoff follows playback speed." ) );
    m_holdLeds = checkbox( indicators, _( "Keep rapidly blinking LEDs lit" ),
            _( "When disabled, LEDs always display instantaneous current, including fast pulses." ) );
    m_markDigital = checkbox( indicators, _( "Mark fast digital switching in purple (~)" ),
            _( "When disabled, pin indicators always display their instantaneous logic state." ) );
    m_showDigital = checkbox( indicators, _( "Show digital pin indicators" ),
            _( "The pin indicator toolbar button controls this same preference." ) );
    m_ledCutoff->Bind( wxEVT_UPDATE_UI, [this]( wxUpdateUIEvent& e ) { e.Enable( m_holdLeds->GetValue() ); } );
    m_digitalCutoff->Bind( wxEVT_UPDATE_UI, [this]( wxUpdateUIEvent& e ) { e.Enable( m_markDigital->GetValue() ); } );

    auto* playback = group( _( "Playback and interaction" ) );
    auto* playbackRows = grid( playback );
    m_refreshRate = number( playbackRows, _( "Target visual update rate:" ), _( "Hz" ), 10, 240, 0, 5,
            _( "Higher rates follow visible transitions more closely and use more processing power. Aim above twice the frequency you want to see blinking." ) );
    m_speed = number( playbackRows, _( "Initial playback speed:" ), _( "×" ), .0001, 100, 4, .1,
            _( "Applied on a new simulation. The instruments panel can change speed during playback without changing component timing." ) );
    m_step = number( playbackRows, _( "Single-step duration:" ), _( "ms" ), .001, 1000, 3, 1,
            _( "Simulated time advanced by Step at 1× speed. The selected playback speed scales this duration." ) );
    m_pushReleased = checkbox( playback, _( "Start push buttons released (open)" ),
            _( "Sets the initial SW_Push state when a new simulation starts." ) );
    m_autoInstruments = checkbox( playback, _( "Automatically open instruments when probes are present" ),
            _( "Open the measurements and oscilloscope panel when starting a circuit with probes." ) );

    auto* solver = group( _( "Solver — applied to new simulations" ) );
    auto* solverRows = grid( solver );
    m_timeStep = number( solverRows, _( "Maximum time step:" ), _( "µs" ), .001, 10000, 3, 10,
            _( "Upper limit for solver time steps. Component models may require smaller steps for accuracy." ) );
    m_duration = number( solverRows, _( "Maximum session duration:" ), _( "s" ), 1, 86400, 1, 60,
            _( "Maximum simulated time before completion. Very small model time steps may shorten this limit." ) );
    m_timeout = number( solverRows, _( "Simulation response timeout:" ), _( "s" ), .1, 60, 1, 1,
            _( "How long to wait for a simulation response before reporting an error." ) );

    auto* other = group( _( "Oscilloscope and warnings" ) );
    auto* otherRows = grid( other );
    m_history = number( otherRows, _( "Oscilloscope history:" ), _( "samples" ), 256, 65536, 0, 256,
            _( "Requested retained samples per signal for new simulations. Circuit size also limits history to keep memory use bounded." ) );
    m_overcurrent = checkbox( other, _( "Show overcurrent warnings" ),
            _( "Display warnings for LEDs and components exceeding their configured current ratings." ) );
    m_supply = checkbox( other, _( "Show logic supply warnings" ),
            _( "Display warnings when a digital component's supply is outside its model's operating range." ) );
    note( _( "Visual, playback and warning changes apply when you click OK. Solver limits, history and initial switch state apply after Stop, then Play." ) );
    scroll->SetSizer( layout );
    auto* outer = new wxBoxSizer( wxVERTICAL );
    outer->Add( scroll, 1, wxEXPAND | wxALL, 4 );
    SetSizer( outer );
}

void PANEL_SIMULATION_PREFERENCES::loadValues( SIMULATION_PREFERENCES p )
{
    p.Normalize();
    m_ledCutoff->SetValue( p.ledCutoffHz ); m_digitalCutoff->SetValue( p.digitalCutoffHz );
    m_refreshRate->SetValue( p.refreshRateHz ); m_speed->SetValue( p.playbackSpeed );
    m_step->SetValue( p.stepMs ); m_timeStep->SetValue( p.maxTimeStepUs );
    m_duration->SetValue( p.durationSeconds ); m_timeout->SetValue( p.workerTimeoutSeconds );
    m_history->SetValue( p.scopeHistorySamples );
    m_holdLeds->SetValue( p.holdFastLeds ); m_markDigital->SetValue( p.markFastDigital );
    m_showDigital->SetValue( p.showDigitalIndicators ); m_pushReleased->SetValue( p.pushButtonsReleased );
    m_autoInstruments->SetValue( p.autoOpenInstruments ); m_overcurrent->SetValue( p.warnOvercurrent );
    m_supply->SetValue( p.warnSupply );
}

bool PANEL_SIMULATION_PREFERENCES::TransferDataToWindow()
{
    if( auto* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) ) loadValues( cfg->m_RealtimeSimulation );
    return true;
}

bool PANEL_SIMULATION_PREFERENCES::TransferDataFromWindow()
{
    for( const auto& entry : m_numbers )
    {
        auto* control = entry.control;
        if( !control->IsEnabled() ) continue;
        double value = 0;
        if( !control->GetTextValue().ToDouble( &value ) || !std::isfinite( value )
            || value < control->GetMin() || value > control->GetMax() )
        {
            wxMessageBox( wxString::Format( _( "Enter a value between %g and %g for %s" ),
                    control->GetMin(), control->GetMax(), entry.label ), _( "Real-time Simulation" ),
                    wxOK | wxICON_INFORMATION, this );
            control->SetFocus();
            return false;
        }
    }
    auto number = []( wxSpinCtrlDouble* control )
    {
        double value = control->GetValue();
        control->GetTextValue().ToDouble( &value );
        return value;
    };
    SIMULATION_PREFERENCES p;
    p.ledCutoffHz = number( m_ledCutoff ); p.digitalCutoffHz = number( m_digitalCutoff );
    p.refreshRateHz = std::lround( number( m_refreshRate ) ); p.playbackSpeed = number( m_speed );
    p.stepMs = number( m_step ); p.maxTimeStepUs = number( m_timeStep );
    p.durationSeconds = number( m_duration ); p.workerTimeoutSeconds = number( m_timeout );
    p.scopeHistorySamples = std::lround( number( m_history ) );
    p.holdFastLeds = m_holdLeds->GetValue(); p.markFastDigital = m_markDigital->GetValue();
    p.showDigitalIndicators = m_showDigital->GetValue(); p.pushButtonsReleased = m_pushReleased->GetValue();
    p.autoOpenInstruments = m_autoInstruments->GetValue(); p.warnOvercurrent = m_overcurrent->GetValue();
    p.warnSupply = m_supply->GetValue();
    p.Normalize();
    if( auto* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) ) cfg->m_RealtimeSimulation = p;
    return true;
}

void PANEL_SIMULATION_PREFERENCES::ResetPanel()
{
    loadValues( {} );
}
}
