/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "source_probe_dialog.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace CIRCUITALIVE
{
bool EditSourceProbe( wxWindow* parent, PROBE_KIND kind, SOURCE_PROBE_SETTINGS& settings,
                      std::string& name )
{
    wxDialog dialog( parent, wxID_ANY, _( "Source probe properties" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* info = new wxStaticText( &dialog, wxID_ANY,
        _( "Connect the probe to a wire or pin endpoint. Its voltage is referenced to GND.\n"
           "During simulation: click 0/1 to toggle, or PWM/square to adjust duty cycle.\n"
           "Select a placed probe and press Ctrl+Alt+M to edit its settings." ) );
    layout->Add( info, 0, wxALL, 12 );
    auto* grid = new wxFlexGridSizer( 2, 8, 12 ); grid->AddGrowableCol( 1 );
    grid->Add( new wxStaticText( &dialog, wxID_ANY, _( "Probe name (optional)" ) ), 0, wxALIGN_CENTER_VERTICAL );
    auto* nameText = new wxTextCtrl( &dialog, wxID_ANY, wxString::FromUTF8( name ) ); grid->Add( nameText, 1, wxEXPAND );
    SOURCE_PROBE_SETTINGS candidate = settings;
    struct INPUT { wxTextCtrl* text; double* value; double scale; };
    std::vector<INPUT> inputs;
    auto add = [&]( const wxString& label, double& value, double scale = 1 )
    {
        grid->Add( new wxStaticText( &dialog, wxID_ANY, label ), 0, wxALIGN_CENTER_VERTICAL );
        auto* text = new wxTextCtrl( &dialog, wxID_ANY, wxString::Format( "%.12g", value * scale ) );
        text->SetMinSize( dialog.FromDIP( wxSize( 170, -1 ) ) );
        grid->Add( text, 1, wxEXPAND ); inputs.push_back( { text, &value, scale } );
    };
    wxCheckBox* initial = nullptr;
    if( kind == PROBE_KIND::SINE )
    {
        add( _( "Peak amplitude (V)" ), candidate.amplitude ); add( _( "DC offset (V)" ), candidate.offset );
        add( _( "Phase (degrees)" ), candidate.phase );
    }
    else
    {
        add( _( "Logic 0 / low voltage (V)" ), candidate.low ); add( _( "Logic 1 / high voltage (V)" ), candidate.high );
    }
    if( kind == PROBE_KIND::LOGIC )
    {
        grid->Add( new wxStaticText( &dialog, wxID_ANY, _( "Initial state" ) ), 0, wxALIGN_CENTER_VERTICAL );
        initial = new wxCheckBox( &dialog, wxID_ANY, _( "Start at logic 1" ) );
        initial->SetValue( candidate.initialHigh ); grid->Add( initial );
    }
    else
    {
        add( _( "Frequency (Hz)" ), candidate.frequency ); add( _( "Start delay (s)" ), candidate.delay );
        if( kind != PROBE_KIND::SINE )
        {
            add( _( "Duty cycle (%)" ), candidate.duty, 100 );
            add( _( "Rise time (s)" ), candidate.rise ); add( _( "Fall time (s)" ), candidate.fall );
        }
    }
    add( _( "Output resistance (Ohm)" ), candidate.resistance );
    layout->Add( grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 12 );
    auto* help = new wxStaticText( &dialog, wxID_ANY,
        _( "Numeric suffixes: 1k = 1000, 1m = 0.001, 1u = 0.000001.\n"
           "Finite output resistance limits drive current and causes voltage drop under load.\n"
           "PWM: 0% = constant low, 100% = constant high; short pulses shorten their edges." ) );
    layout->Add( help, 0, wxALL, 12 );
    layout->Add( dialog.CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 12 );
    dialog.SetSizerAndFit( layout ); dialog.SetMinSize( dialog.GetSize() ); dialog.CentreOnParent();
    dialog.Bind( wxEVT_BUTTON, [&]( wxCommandEvent& )
    {
        try
        {
            for( auto& input : inputs )
            {
                try { *input.value = ParseComponentValue( input.text->GetValue().ToStdString() ) / input.scale; }
                catch( const std::exception& ) { input.text->SetFocus(); throw; }
            }
            if( initial ) candidate.initialHigh = initial->GetValue();
            ValidateSourceProbe( kind, candidate );
            settings = candidate; name = nameText->GetValue().ToStdString(); dialog.EndModal( wxID_OK );
        }
        catch( const std::exception& e )
        {
            wxMessageBox( wxString::FromUTF8( e.what() ), _( "Invalid source settings" ), wxOK | wxICON_ERROR, &dialog );
        }
    }, wxID_OK );
    return dialog.ShowModal() == wxID_OK;
}
}
