/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "visual_signals.h"
#include <algorithm>

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
    for(const auto& segment:circuit.segments)
        monitors.push_back({segment.signal,NO_SIGNAL,NO_SIGNAL,.01*segment.appearance.fullScale,.05*segment.appearance.fullScale});
    for( const auto& pin : circuit.logicPins )
        monitors.push_back( { pin.voltage, pin.ground, pin.vcc, pin.low, pin.high,
                              pin.digital && !pin.observer ? 1.0 : 5.0, pin.minSupply, pin.maxSupply, pin.thresholdReference } );
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
    view.segmentLevels.clear();
    for(std::size_t i=0;i<circuit.segments.size();++i)
    {
        const auto& segment=circuit.segments[i];const auto index=circuit.leds.size()+i;
        const double value=preferences.holdFastLeds&&rapid(index,preferences.ledCutoffHz)?view.sample.activity[index].highValue:view.sample.latest.values.at(segment.signal);
        view.segmentLevels.push_back(std::clamp(value/segment.appearance.fullScale,0.0,1.0));
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
                                   && rapid( circuit.leds.size() + circuit.segments.size() + i, preferences.digitalCutoffHz ) );
    }
    view.netContentions.clear();
    view.contentionWarnings.clear();
    for( const auto& net : circuit.drivenNets )
    {
        const auto drivers = ContendingDrivers( net, view.sample.latest );
        view.netContentions.push_back( !drivers.empty() );
        if( drivers.empty() ) continue;
        std::string warning = "Bus contention: ";
        for( const auto i : drivers )
        {
            if( warning.back() != ' ' ) warning += ", ";
            const auto& driver = net.drivers[i];
            warning += driver.reference + "." + driver.pin;
        }
        view.contentionWarnings.push_back( std::move( warning ) );
    }
}
}
