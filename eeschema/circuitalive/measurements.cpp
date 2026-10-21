/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "measurements.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
std::string AlphabeticProbeName( std::size_t sequence )
{
    sequence = std::max<std::size_t>( 1, sequence );
    std::string name;
    do { --sequence; name.push_back( 'A' + sequence % 26 ); sequence /= 26; } while( sequence );
    std::reverse( name.begin(), name.end() );
    return name;
}

const char* QuantityUnit( MEASURE_QUANTITY quantity )
{
    switch( quantity )
    {
    case MEASURE_QUANTITY::VOLTAGE: return "V";
    case MEASURE_QUANTITY::CURRENT: return "A";
    case MEASURE_QUANTITY::POWER: return "W";
    }
    return "";
}

double MeasurementValue( const MEASUREMENT_CHANNEL& channel, const SIM_SAMPLE& sample )
{
    if( !channel.connected ) return std::numeric_limits<double>::quiet_NaN();
    auto value = [&]( std::size_t index ) { return index == NO_SIGNAL ? 0.0 : sample.values.at( index ); };
    if( channel.directSample ) return value( channel.positive );
    const double voltage = value( channel.positive ) - value( channel.negative );
    if( channel.quantity == MEASURE_QUANTITY::VOLTAGE ) return voltage;
    if( channel.quantity == MEASURE_QUANTITY::CURRENT ) return value( channel.current );
    return voltage * value( channel.current );
}

std::string FormatMeasurement( double value, MEASURE_QUANTITY quantity )
{
    if( !std::isfinite( value ) ) return "—";
    const double magnitude = std::abs( value );
    double scale = 1;
    const char* prefix = "";
    if( magnitude >= 1e6 ) { scale = 1e6; prefix = "M"; }
    else if( magnitude >= 1e3 ) { scale = 1e3; prefix = "k"; }
    else if( magnitude > 0 && magnitude < 1e-6 ) { scale = 1e-9; prefix = "n"; }
    else if( magnitude > 0 && magnitude < 1e-3 ) { scale = 1e-6; prefix = "µ"; }
    else if( magnitude > 0 && magnitude < 1 ) { scale = 1e-3; prefix = "m"; }
    std::ostringstream text;
    text.imbue( std::locale::classic() );
    text << std::setprecision( 5 ) << value / scale << ' ' << prefix << QuantityUnit( quantity );
    return text.str();
}

std::string ProbeKindName( PROBE_KIND kind )
{
    switch( kind )
    {
    case PROBE_KIND::VOLTAGE: return "voltage";
    case PROBE_KIND::CURRENT: return "current";
    case PROBE_KIND::POWER: return "power";
    case PROBE_KIND::OSCILLOSCOPE: return "oscilloscope";
    }
    throw std::runtime_error( "Unknown probe kind" );
}

PROBE_KIND ParseProbeKind( const std::string& text )
{
    for( auto kind : { PROBE_KIND::VOLTAGE, PROBE_KIND::CURRENT, PROBE_KIND::POWER, PROBE_KIND::OSCILLOSCOPE } )
        if( text == ProbeKindName( kind ) ) return kind;
    throw std::runtime_error( "Unknown probe kind: " + text );
}
}
