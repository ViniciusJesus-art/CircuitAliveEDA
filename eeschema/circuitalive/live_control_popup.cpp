/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "live_control_popup.h"
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <algorithm>
#include <cmath>

namespace CIRCUITALIVE
{
LIVE_CONTROL_POPUP::LIVE_CONTROL_POPUP( wxWindow* parent, const std::string& reference,
        const std::vector<CIRCUIT_ADJUSTMENT>& controls, const std::vector<double>& values,
        std::size_t component, std::function<void( std::size_t, double )> change, std::size_t selected ) :
        wxPopupTransientWindow( parent, wxBORDER_SIMPLE )
{
    auto* panel = new wxPanel( this ); auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* heading = new wxStaticText( panel, wxID_ANY, wxString::FromUTF8( reference ) );
    heading->SetFont( heading->GetFont().Bold() ); layout->Add( heading, 0, wxALL, 8 );
    for( std::size_t i = 0; i < controls.size(); ++i )
    {
        const auto& c = controls[i]; if( c.component != component ) continue;
        if( selected != NO_SIGNAL && i != selected ) continue;
        layout->Add( new wxStaticText( panel, wxID_ANY, ModelText( c.label ) ), 0, wxLEFT | wxRIGHT, 8 );
        auto* row = new wxBoxSizer( wxHORIZONTAL );
        const int steps = std::clamp( int( std::min( 10000., std::ceil( ( c.maximum - c.minimum ) / c.step ) ) ), 1, 10000 );
        const double initialValue = i < values.size() ? values[i] : c.initial;
        auto* slider = new wxSlider( panel, wxID_ANY,
            std::lround( ( initialValue - c.minimum ) / ( c.maximum - c.minimum ) * steps ), 0, steps,
            wxDefaultPosition, wxSize( FromDIP( 220 ), -1 ) );
        const double factor = c.unit == "%" ? 100 : 1;
        auto* number = new wxSpinCtrlDouble( panel, wxID_ANY, {}, wxDefaultPosition,
            wxSize( FromDIP( 100 ), -1 ), wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,
            c.minimum * factor, c.maximum * factor, initialValue * factor, c.step * factor );
        number->SetDigits( c.unit == "%" ? 1 : 4 );
        row->Add( slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
        row->Add( number, 0, wxALIGN_CENTER_VERTICAL );
        row->Add( new wxStaticText( panel, wxID_ANY, wxString::FromUTF8( c.unit ) ), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4 );
        layout->Add( row, 0, wxEXPAND | wxALL, 8 );
        slider->Bind( wxEVT_SLIDER, [change, i, c, steps, number, factor]( wxCommandEvent& e )
        {
            const double value = c.minimum + ( c.maximum - c.minimum ) * e.GetInt() / steps;
            number->SetValue( value * factor ); change( i, value );
        } );
        auto apply = [change, i, c, steps, slider, number, factor]()
        {
            const double value = std::clamp( number->GetValue() / factor, c.minimum, c.maximum );
            slider->SetValue( std::lround( ( value - c.minimum ) / ( c.maximum - c.minimum ) * steps ) );
            change( i, value );
        };
        number->Bind( wxEVT_SPINCTRLDOUBLE, [apply]( wxSpinDoubleEvent& ) { apply(); } );
        number->Bind( wxEVT_TEXT_ENTER, [apply]( wxCommandEvent& ) { apply(); } );
    }
    auto* hint = new wxStaticText( panel, wxID_ANY, _( "Changes apply live. Esc or click outside closes." ) );
    layout->Add( hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8 ); panel->SetSizerAndFit( layout );
    auto* outer = new wxBoxSizer( wxVERTICAL ); outer->Add( panel ); SetSizerAndFit( outer );
    Bind( wxEVT_CHAR_HOOK, [this]( wxKeyEvent& event )
    {
        if( event.GetKeyCode() == WXK_ESCAPE ) Dismiss();
        else event.Skip();
    } );
}
}
