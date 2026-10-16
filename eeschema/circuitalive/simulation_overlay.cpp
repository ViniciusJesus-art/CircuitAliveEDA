/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_overlay.h"
#include <base_units.h>
#include <algorithm>

namespace CIRCUITALIVE
{
void SIMULATION_OVERLAY::Render( const SCHEMATIC_SNAPSHOT& aCircuit,
                                 const EDITOR_SESSION_VIEW& aSession,
                                 const wxString& aCurrentSheet, bool aLogicLevels )
{
    Clear();
    if( aSession.state == EDITOR_SESSION_STATE::STOPPED
        || aSession.state == EDITOR_SESSION_STATE::ERROR )
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
                const auto color = level == LOGIC_LEVEL::HIGH ? KIGFX::COLOR4D( .95, .12, .08, 1 )
                    : level == LOGIC_LEVEL::LOW ? KIGFX::COLOR4D( .08, .35, 1, 1 )
                    : level == LOGIC_LEVEL::HIGH_Z ? KIGFX::COLOR4D( .55, .57, .6, 1 ) : KIGFX::COLOR4D( 1, .7, .05, 1 );
                SetIsStroke( true ); SetIsFill( true ); SetStrokeColor( KIGFX::COLOR4D( .1, .1, .1, 1 ) ); SetFillColor( color );
                Circle( VECTOR2D( location.position ), schIUScale.mmToIU( .45 ) );
                SetIsFill( false ); SetStrokeColor( color );
                SetGlyphSize( VECTOR2I( schIUScale.mmToIU( .8 ), schIUScale.mmToIU( .8 ) ) );
                const char* text = level == LOGIC_LEVEL::HIGH ? "1" : level == LOGIC_LEVEL::LOW ? "0"
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
        double brightness = i < aSession.ledCurrents.size()
                ? LedBrightness( aSession.ledCurrents[i], aCircuit.circuit.leds[i].brightnessCurrent ) : 0;
        SetIsFill( true );
        SetIsStroke( true );
        SetStrokeColor( KIGFX::COLOR4D( 0.85, 0.15, 0.08, 1 ) );
        SetFillColor( KIGFX::COLOR4D( 0.25 + 0.75 * brightness, 0.06 + 0.2 * brightness,
                                     0.02, 0.3 + 0.5 * brightness ) );
        Circle( VECTOR2D( location.position ), schIUScale.mmToIU( 1.7 ) );
        SetIsFill( false );
        SetStrokeColor( KIGFX::COLOR4D( 1, 0.3, 0.05, brightness * 0.6 ) );
        Circle( VECTOR2D( location.position ), schIUScale.mmToIU( 2.3 ) );
    }
    for( std::size_t i = 0; i < aCircuit.circuit.switches.size(); ++i )
    {
        const auto& location = aCircuit.locations.at( aCircuit.circuit.switches[i].component );
        if( location.sheet != aCurrentSheet )
            continue;
        bool closed = i < aSession.switches.size() && aSession.switches[i];
        SetIsFill( false );
        SetIsStroke( true );
        SetStrokeColor( closed ? KIGFX::COLOR4D( 0.05, 0.8, 0.25, 1 )
                               : KIGFX::COLOR4D( 1, 0.55, 0.1, 1 ) );
        BOX2I box = location.body;
        box.Inflate( schIUScale.mmToIU( 0.6 ) );
        Rectangle( VECTOR2D( box.GetOrigin() ), VECTOR2D( box.GetEnd() ) );
        SetGlyphSize( VECTOR2I( schIUScale.mmToIU( 1 ), schIUScale.mmToIU( 1 ) ) );
        BitmapText( closed ? "ON" : "OFF", box.GetOrigin() - VECTOR2I( 0, schIUScale.mmToIU( 1.5 ) ),
                    ANGLE_0 );
    }
}

int SIMULATION_OVERLAY::SwitchAt( const SCHEMATIC_SNAPSHOT& aCircuit,
                                  const wxString& aCurrentSheet,
                                  const VECTOR2I& aPosition ) const
{
    for( std::size_t i = 0; i < aCircuit.circuit.switches.size(); ++i )
    {
        const auto& location = aCircuit.locations.at( aCircuit.circuit.switches[i].component );
        BOX2I box = location.body;
        box.Inflate( schIUScale.mmToIU( 0.8 ) );
        if( location.sheet == aCurrentSheet && box.Contains( aPosition ) )
            return static_cast<int>( i );
    }
    return -1;
}
}
