/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "logic_signals.h"
#include <algorithm>
#include <cmath>

namespace CIRCUITALIVE
{
LOGIC_LEVEL PinLogicLevel( const LOGIC_PIN& pin, const SIM_SAMPLE& sample )
{
    auto value = [&]( std::size_t i, double fallback = 0.0 )
        { return i == NO_SIGNAL ? fallback : sample.values.at( i ); };
    const bool driven = std::any_of( pin.enables.begin(), pin.enables.end(),
        [&]( auto i ) { return value( i ) > .5; } );
    // A disabled output remains Z even when another device pulls its net high.
    if( pin.digital && ( pin.output || !pin.externalDrive ) && !driven ) return LOGIC_LEVEL::HIGH_Z;
    const double ground = value( pin.ground ), supply = value( pin.vcc, pin.digital ? 0 : 5 ) - ground;
    if( !std::isfinite( supply ) || supply < pin.minSupply || supply > pin.maxSupply ) return LOGIC_LEVEL::UNKNOWN;
    // Internal DAC codes use 0/0.5/1 independently of the physical input thresholds (e.g. TTL).
    if( pin.driverValue != NO_SIGNAL && value( pin.driverValue ) > .25 && value( pin.driverValue ) < .75 )
        return LOGIC_LEVEL::UNKNOWN;
    const double ratio = ( value( pin.voltage ) - ground ) / supply;
    if( !std::isfinite( ratio ) || ratio < -.1 || ratio > 1.1 ) return LOGIC_LEVEL::UNKNOWN;
    if( ratio <= pin.low ) return LOGIC_LEVEL::LOW;
    if( ratio >= pin.high ) return LOGIC_LEVEL::HIGH;
    return LOGIC_LEVEL::UNKNOWN;
}
}
