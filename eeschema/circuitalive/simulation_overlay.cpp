/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_overlay.h"
#include <base_units.h>
#include <algorithm>

namespace CIRCUITALIVE
{
void SIMULATION_OVERLAY::Render( const SCHEMATIC_SNAPSHOT& aCircuit,
                                 const EDITOR_SESSION_VIEW& aSession,
                                 const wxString& aCurrentSheet )
{
    Clear();
    if( aSession.state == EDITOR_SESSION_STATE::STOPPED
        || aSession.state == EDITOR_SESSION_STATE::ERROR )
        return;
    SetLineWidth( schIUScale.mmToIU( 0.12 ) );
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
