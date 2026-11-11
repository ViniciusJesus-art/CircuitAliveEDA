/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "logic_signals.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace CIRCUITALIVE
{
LOGIC_LEVEL PinLogicLevel( const LOGIC_PIN& pin, const SIM_SAMPLE& sample )
{
    auto value = [&]( std::size_t i, double fallback = 0.0 )
        { return i == NO_SIGNAL ? fallback : sample.values.at( i ); };
    const bool driven = std::any_of( pin.enables.begin(), pin.enables.end(),
        [&]( auto i ) { return value( i ) > .5; } );
    // Indicators read the shared electrical node, independently of the local output enable.
    if( pin.digital && !pin.externalDrive && !driven ) return LOGIC_LEVEL::HIGH_Z;
    const double ground = value( pin.ground ), supply = value( pin.vcc, pin.digital && !pin.observer ? 0 : 5 ) - ground;
    if( !std::isfinite( supply ) || supply < pin.minSupply || supply > pin.maxSupply ) return LOGIC_LEVEL::UNKNOWN;
    bool known = pin.externalDrive, unknown = false;
    for( const auto& driver : pin.drivers )
        if( value( driver.enable ) > .5 )
        {
            const double code = value( driver.value, .5 );
            known |= code <= .25 || code >= .75;
            unknown |= code > .25 && code < .75;
        }
    // A floating, unknown DAC must not become a valid TTL HIGH just because its
    // fallback voltage is above VIH. A different known driver can resolve it.
    if( unknown && !known )
        return LOGIC_LEVEL::UNKNOWN;
    const double railRatio = ( value( pin.voltage ) - ground ) / supply;
    if( !std::isfinite( railRatio ) || railRatio < -.1 || railRatio > 1.1 ) return LOGIC_LEVEL::UNKNOWN;
    const double ratio = ( value( pin.voltage ) - ground ) / ( pin.thresholdReference > 0 ? pin.thresholdReference : supply );
    if( ratio <= pin.low ) return LOGIC_LEVEL::LOW;
    if( ratio >= pin.high ) return LOGIC_LEVEL::HIGH;
    return LOGIC_LEVEL::UNKNOWN;
}

LOGIC_LEVEL PinDriveLevel( const LOGIC_PIN& pin, const SIM_SAMPLE& sample )
{
    if( !pin.output || pin.driverEnable == NO_SIGNAL
        || sample.values.at( pin.driverEnable ) <= .5 ) return LOGIC_LEVEL::HIGH_Z;
    if( pin.driverValue == NO_SIGNAL ) return LOGIC_LEVEL::UNKNOWN;
    const double code = sample.values.at( pin.driverValue );
    return !std::isfinite( code ) ? LOGIC_LEVEL::UNKNOWN
        : code <= .25 ? LOGIC_LEVEL::LOW : code >= .75 ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::UNKNOWN;
}

std::vector<std::size_t> ContendingDrivers( const DRIVEN_NET& net, const SIM_SAMPLE& sample )
{
    std::vector<std::size_t> sourcing, sinking;
    auto value = [&]( std::size_t i ) { return i == NO_SIGNAL ? 0.0 : sample.values.at( i ); };
    for( std::size_t i = 0; i < net.drivers.size(); ++i )
    {
        const auto& driver = net.drivers[i];
        if( driver.state.enable != NO_SIGNAL && value( driver.state.enable ) <= .5 ) continue;
        if( driver.state.value != NO_SIGNAL )
        {
            const double code = value( driver.state.value );
            if( !std::isfinite( code ) || ( code > .25 && code < .75 ) ) continue;
        }
        // Positive sensor current enters an output stage; negative current leaves it.
        // Ignore numerical leakage, but do not hide conflicts just because one
        // driver wins the voltage or the current is below its absolute maximum.
        const double current = value( driver.current );
        if( !std::isfinite( current ) || std::abs( current ) <= 1e-6 ) continue;
        if( driver.programmable )
        {
            const double rail = value( current < 0 ? driver.vcc : driver.ground );
            const double resistance = std::abs( value( driver.voltage ) - rail ) / std::abs( current );
            // GPIO outputs use 25 ohms; their internal pull-up uses 30 kohms.
            // Infer which path conducts without another control vector per GPIO.
            if( resistance > 1000 ) continue;
        }
        ( current < 0 ? sourcing : sinking ).push_back( i );
    }
    std::set<std::size_t> opposing;
    for( const auto source : sourcing ) for( const auto sink : sinking )
    {
        const auto a = net.drivers[source].state.value, b = net.drivers[sink].state.value;
        // Different families can have different VOH/VOL drops at the same
        // logic state. Their normal load sharing is not a logic disagreement.
        if( a != NO_SIGNAL && b != NO_SIGNAL && ( value( a ) >= .75 ) == ( value( b ) >= .75 ) ) continue;
        opposing.insert( source ); opposing.insert( sink );
    }
    return { opposing.begin(), opposing.end() };
}
}
