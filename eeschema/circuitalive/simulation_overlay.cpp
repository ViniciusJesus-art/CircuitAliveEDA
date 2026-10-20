/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_overlay.h"
#include "control_geometry.h"
#include "control_references.h"
#include <base_units.h>
#include <algorithm>
#include <set>

namespace CIRCUITALIVE
{
void SIMULATION_OVERLAY::Render( const SCHEMATIC_SNAPSHOT& aCircuit,
                                 const EDITOR_SESSION_VIEW& aSession,
                                 const wxString& aCurrentSheet, bool aLogicLevels )
{
    Clear();
    if( aSession.state == EDITOR_SESSION_STATE::STOPPED
        || aSession.state == EDITOR_SESSION_STATE::FAILED )
        return;
    SetLineWidth( schIUScale.mmToIU( 0.12 ) );
    if( aLogicLevels )
        for( const auto& location : aCircuit.pins )
        {
            if( location.sheet != aCurrentSheet ) continue;
            for( std::size_t i = 0; i < aCircuit.circuit.logicPins.size() && i < aSession.logicLevels.size(); ++i )
            {
                const auto& pin = aCircuit.circuit.logicPins[i];
                if( pin.component != location.component || pin.pin != location.pin ) continue;
                const auto level = aSession.logicLevels[i];
                const bool rapid = i < aSession.logicRapid.size() && aSession.logicRapid[i];
                const auto color = rapid ? KIGFX::COLOR4D( .75, .2, 1, 1 )
                    : level == LOGIC_LEVEL::HIGH ? KIGFX::COLOR4D( .95, .12, .08, 1 )
                    : level == LOGIC_LEVEL::LOW ? KIGFX::COLOR4D( .08, .35, 1, 1 )
                    : level == LOGIC_LEVEL::HIGH_Z ? KIGFX::COLOR4D( .55, .57, .6, 1 ) : KIGFX::COLOR4D( 1, .7, .05, 1 );
                SetIsStroke( true ); SetIsFill( true ); SetStrokeColor( KIGFX::COLOR4D( .1, .1, .1, 1 ) ); SetFillColor( color );
                Circle( VECTOR2D( location.position ), schIUScale.mmToIU( .45 ) );
                SetIsFill( false ); SetStrokeColor( color );
                SetGlyphSize( VECTOR2I( schIUScale.mmToIU( .8 ), schIUScale.mmToIU( .8 ) ) );
                const char* text = rapid ? "~" : level == LOGIC_LEVEL::HIGH ? "1" : level == LOGIC_LEVEL::LOW ? "0"
                    : level == LOGIC_LEVEL::HIGH_Z ? "Z" : "X";
                BitmapText( text, location.position + VECTOR2I( schIUScale.mmToIU( .7 ), schIUScale.mmToIU( -.7 ) ), ANGLE_0 );
                break;
            }
        }
    for( const auto& channel : aCircuit.circuit.measurements )
    {
        if( channel.probe == NO_SIGNAL || channel.probe >= aCircuit.probes.size() ) continue;
        const auto& location = aCircuit.probes[channel.probe]; if( location.sheet != aCurrentSheet ) continue;
        SetIsFill( false ); SetIsStroke( true ); SetStrokeColor( KIGFX::COLOR4D( .12, .5, .9, 1 ) );
        SetGlyphSize( VECTOR2I( schIUScale.mmToIU( 1 ), schIUScale.mmToIU( 1 ) ) );
        BitmapText( wxString::FromUTF8( channel.name ), location.position + VECTOR2I( schIUScale.mmToIU( 3.5 ), schIUScale.mmToIU( -1 ) ), ANGLE_0 );
    }
    for( std::size_t i = 0; i < aCircuit.circuit.leds.size(); ++i )
    {
        const auto& location = aCircuit.locations.at( aCircuit.circuit.leds[i].component );
        if( location.sheet != aCurrentSheet )
            continue;
        const auto& appearance=aCircuit.circuit.leds[i].appearance;
        const double brightness = i < aSession.ledCurrents.size()
                ? LedBrightness( aSession.ledCurrents[i], aCircuit.circuit.leds[i].brightnessCurrent ) : 0;
        const auto display=LedDisplay(brightness,appearance);
        SetIsFill( true );
        SetIsStroke( true );
        SetStrokeColor( KIGFX::COLOR4D( appearance.red,appearance.green,appearance.blue,display.strokeOpacity ) );
        SetFillColor( KIGFX::COLOR4D( display.red,display.green,display.blue,display.fillOpacity ) );
        if(appearance.diameterMm>0)Circle( VECTOR2D( location.position ), schIUScale.mmToIU(appearance.diameterMm/2) );
        SetIsFill( false );
        SetStrokeColor( KIGFX::COLOR4D( appearance.red,appearance.green,appearance.blue,display.haloOpacity ) );
        if(appearance.haloDiameterMm>0&&display.haloOpacity>0)
            Circle( VECTOR2D( location.position ), schIUScale.mmToIU(appearance.haloDiameterMm/2) );
    }
    std::set<std::size_t> adjusted;
    for( std::size_t i = 0; i < aCircuit.circuit.adjustments.size(); ++i )
    {
        const auto& control = aCircuit.circuit.adjustments[i];
        if( !control.visual.visible ) continue;
        if( !control.visual.custom && !adjusted.insert( control.component ).second ) continue;
        const auto& location = aCircuit.locations.at( control.component );
        if( location.sheet != aCurrentSheet ) continue;
        const double value = i < aSession.adjustments.size() ? aSession.adjustments[i] : control.initial;
        SetIsFill( false ); SetIsStroke( true ); SetStrokeColor( KIGFX::COLOR4D( .05, .7, .75, 1 ) );
        const BOX2I box = ControlBounds( location.body, control.visual );
        Rectangle( VECTOR2D( box.GetOrigin() ), VECTOR2D( box.GetEnd() ) );
        SetGlyphSize( VECTOR2I( schIUScale.mmToIU( 1 ), schIUScale.mmToIU( 1 ) ) );
        const auto label=LiveControlText(aCircuit.circuit,aSession.switches,aSession.adjustments,control.component,control.id,"label",control.label);
        const auto unit=LiveControlText(aCircuit.circuit,aSession.switches,aSession.adjustments,control.component,control.id,"unit",control.unit);
        auto text = unit == "%" ? wxString::Format( "%.1f%%", 100 * value )
            : wxString::Format( "%.3g %s", value, wxString::FromUTF8( unit ) );
        if( !label.empty() && control.visual.custom ) text.Prepend( wxString::FromUTF8( label ) + ": " );
        BitmapText( text, box.GetOrigin() - VECTOR2I( 0, schIUScale.mmToIU( 1.5 ) ), ANGLE_0 );
    }
    for( std::size_t i = 0; i < aCircuit.circuit.switches.size(); ++i )
    {
        const auto& control = aCircuit.circuit.switches[i];
        if( !control.visual.visible ) continue;
        const auto& location = aCircuit.locations.at( control.component );
        if( location.sheet != aCurrentSheet )
            continue;
        bool closed = i < aSession.switches.size() && aSession.switches[i];
        SetIsFill( false );
        SetIsStroke( true );
        SetStrokeColor( closed ? KIGFX::COLOR4D( 0.05, 0.8, 0.25, 1 )
                               : KIGFX::COLOR4D( 1, 0.55, 0.1, 1 ) );
        const BOX2I box = ControlBounds( location.body, control.visual );
        Rectangle( VECTOR2D( box.GetOrigin() ), VECTOR2D( box.GetEnd() ) );
        SetGlyphSize( VECTOR2I( schIUScale.mmToIU( 1 ), schIUScale.mmToIU( 1 ) ) );
        const auto label=LiveControlText(aCircuit.circuit,aSession.switches,aSession.adjustments,control.component,control.id,"label",control.label);
        const auto text = ( label.empty() ? wxString() : wxString::FromUTF8( label ) + ": " )
                + ( closed ? "ON" : "OFF" );
        BitmapText( text, box.GetOrigin() - VECTOR2I( 0, schIUScale.mmToIU( 1.5 ) ),
                    ANGLE_0 );
    }
}

int SIMULATION_OVERLAY::SwitchAt( const SCHEMATIC_SNAPSHOT& aCircuit,
                                  const wxString& aCurrentSheet,
                                  const VECTOR2I& aPosition ) const
{
    for( bool custom : { true, false } )
    for( std::size_t i = 0; i < aCircuit.circuit.switches.size(); ++i )
    {
        const auto& control = aCircuit.circuit.switches[i];
        if( control.visual.custom != custom ) continue;
        const auto& location = aCircuit.locations.at( control.component );
        const BOX2I box = ControlBounds( location.body, control.visual );
        if( location.sheet == aCurrentSheet && box.Contains( aPosition ) )
            return static_cast<int>( i );
    }
    return -1;
}

int SIMULATION_OVERLAY::AdjustmentAt( const SCHEMATIC_SNAPSHOT& circuit,
                                    const wxString& sheet, const VECTOR2I& position ) const
{
    // Explicit rectangles take priority over whole-symbol targets of either kind.
    for( bool custom : { true, false } )
    for( std::size_t i = 0; i < circuit.circuit.adjustments.size(); ++i )
    {
        const auto& control = circuit.circuit.adjustments[i];
        if( control.visual.custom != custom ) continue;
        const auto& location = circuit.locations.at( control.component );
        if( !custom )
            for( const auto& button : circuit.circuit.switches )
                if( button.visual.custom && circuit.locations.at( button.component ).sheet == sheet
                    && ControlBounds( circuit.locations.at( button.component ).body, button.visual ).Contains( position ) )
                    return -1;
        const BOX2I box = ControlBounds( location.body, control.visual );
        if( location.sheet == sheet && box.Contains( position ) ) return i;
    }
    return -1;
}
}
