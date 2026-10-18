/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "visual_signals.h"

namespace CIRCUITALIVE
{
void ConfigureVisualSignals( CIRCUIT_SNAPSHOT& circuit )
{
    // Stable ordering: LED monitors, followed by pin monitors. Nothing depends
    // on how many solver samples survive in the GUI/scope history buffer.
    auto& monitors = circuit.config.activityMonitors;
    monitors.clear();
    for( const auto& led : circuit.leds )
        monitors.push_back( { led.signal, NO_SIGNAL, NO_SIGNAL,
                              .01 * led.brightnessCurrent, .05 * led.brightnessCurrent } );
    for( const auto& pin : circuit.logicPins )
        monitors.push_back( { pin.voltage, pin.ground, pin.vcc, pin.low, pin.high,
                              pin.digital ? 1.0 : 5.0, pin.minSupply, pin.maxSupply } );
}

void UpdateVisualSignals( const CIRCUIT_SNAPSHOT& circuit, EDITOR_SESSION_VIEW& view,
                          double speed, const SIMULATION_PREFERENCES& preferences )
{
    auto rapid = [&]( std::size_t i, double cutoff )
    {
        return view.state == EDITOR_SESSION_STATE::RUNNING && i < view.sample.activity.size()
               && view.sample.activity[i].RapidAt( view.sample.latest.time, speed, cutoff );
    };
    view.ledCurrents.clear();
    for( std::size_t i = 0; i < circuit.leds.size(); ++i )
    {
        const auto& led = circuit.leds[i];
        // Slow signals use the actual final current, with no time averaging or
        // artificial fade. Rapid pulses hold the brightness of their on phase.
        view.ledCurrents.push_back( preferences.holdFastLeds && rapid( i, preferences.ledCutoffHz )
                                              ? view.sample.activity[i].highValue
                                              : view.sample.latest.values.at( led.signal ) );
    }
    view.logicLevels.clear();
    view.logicRapid.clear();
    for( std::size_t i = 0; i < circuit.logicPins.size(); ++i )
    {
        const auto level = PinLogicLevel( circuit.logicPins[i], view.sample.latest );
        view.logicLevels.push_back( level );
        // Electrical X/Z retain their meaning; fast switching is presentation
        // metadata, separate from the instantaneous electrical logic level.
        view.logicRapid.push_back( ( level == LOGIC_LEVEL::LOW || level == LOGIC_LEVEL::HIGH )
                                   && preferences.markFastDigital
                                   && rapid( circuit.leds.size() + i, preferences.digitalCutoffHz ) );
    }
}
}
