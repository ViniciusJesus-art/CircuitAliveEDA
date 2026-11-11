/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "source_probe.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
void ValidateSourceProbe( PROBE_KIND kind, const SOURCE_PROBE_SETTINGS& s )
{
    if( !IsSourceProbe( kind ) ) throw std::runtime_error( "Not a source probe" );
    auto range = []( double value, double low, double high, const char* label )
    {
        if( !std::isfinite( value ) || value < low || value > high )
            throw std::runtime_error( std::string( label ) + " is outside its allowed range" );
    };
    range( s.resistance, .01, 1e9, "Output resistance (0.01 to 1G Ohm)" );
    if( kind == PROBE_KIND::SINE )
    {
        range( s.amplitude, 0, 1e6, "Peak amplitude (0 to 1M V)" );
        range( s.offset, -1e6, 1e6, "DC offset (-1M to 1M V)" );
        range( s.phase, -360, 360, "Phase (-360 to 360 degrees)" );
    }
    else
    {
        range( s.low, -1e6, 1e6, "Low voltage (-1M to 1M V)" );
        range( s.high, -1e6, 1e6, "High voltage (-1M to 1M V)" );
        if( s.high <= s.low ) throw std::runtime_error( "High voltage must exceed low voltage" );
    }
    if( kind == PROBE_KIND::LOGIC ) return;
    range( s.frequency, .001, 1e7, "Frequency (1m to 10M Hz)" );
    range( s.delay, 0, 3600, "Start delay (0 to 3600 s)" );
    if( kind != PROBE_KIND::SINE )
    {
        range( s.duty, 0, 1, "Duty cycle (0 to 100%)" );
        range( s.rise, 1e-12, 3600, "Rise time (1p to 3600 s)" );
        range( s.fall, 1e-12, 3600, "Fall time (1p to 3600 s)" );
        if( s.rise + s.fall >= 1 / s.frequency )
            throw std::runtime_error( "Rise and fall times together must be shorter than one period" );
    }
}

std::string SerializeSourceProbe( PROBE_KIND kind, const SOURCE_PROBE_SETTINGS& s )
{
    ValidateSourceProbe( kind, s );
    return nlohmann::json{ { "version", 1 }, { "low", s.low }, { "high", s.high },
        { "initial_high", s.initialHigh }, { "frequency", s.frequency }, { "duty", s.duty },
        { "amplitude", s.amplitude }, { "offset", s.offset }, { "phase", s.phase },
        { "delay", s.delay }, { "rise", s.rise }, { "fall", s.fall }, { "resistance", s.resistance } }.dump();
}

SOURCE_PROBE_SETTINGS ParseSourceProbe( PROBE_KIND kind, const std::string& text )
{
    SOURCE_PROBE_SETTINGS s;
    if( !text.empty() )
    {
        const auto j = nlohmann::json::parse( text );
        if( j.at( "version" ).get<int>() != 1 ) throw std::runtime_error( "Unsupported source probe version" );
        s.low = j.at( "low" ); s.high = j.at( "high" ); s.initialHigh = j.at( "initial_high" );
        s.frequency = j.at( "frequency" ); s.duty = j.at( "duty" ); s.amplitude = j.at( "amplitude" );
        s.offset = j.at( "offset" ); s.phase = j.at( "phase" ); s.delay = j.at( "delay" );
        s.rise = j.at( "rise" ); s.fall = j.at( "fall" ); s.resistance = j.at( "resistance" );
    }
    ValidateSourceProbe( kind, s );
    return s;
}

CIRCUIT_COMPONENT SourceProbeComponent( PROBE_KIND kind, const SOURCE_PROBE_SETTINGS& s,
        const std::string& reference, const std::string& net )
{
    ValidateSourceProbe( kind, s );
    CIRCUIT_COMPONENT c;
    c.reference = reference; c.libraryId = "CircuitAlive_Instruments:Probe_" + ProbeKindName( kind );
    c.sourceProbe = kind;
    // Only pin 1 exists on the schematic. Pin 2 is the instrument's internal ground reference.
    c.pins = { { "1", net }, { "2", "0" } };
    c.modelFile = "builtin:probe_" + std::string( kind == PROBE_KIND::LOGIC ? "logic"
            : kind == PROBE_KIND::SINE ? "sine" : "pulse" );
    auto set = [&]( const char* key, double value )
    {
        std::ostringstream number; number.imbue( std::locale::classic() ); number << std::setprecision( 17 ) << value;
        c.modelParameters[key] = number.str();
    };
    set( "resistance", s.resistance );
    if( kind != PROBE_KIND::SINE ) { set( "low", s.low ); set( "high", s.high ); }
    if( kind == PROBE_KIND::LOGIC ) set( "initial", s.initialHigh ? 1 : 0 );
    else
    {
        set( "frequency", s.frequency ); set( "delay", s.delay );
        if( kind == PROBE_KIND::SINE )
        {
            set( "amplitude", s.amplitude ); set( "offset", s.offset ); set( "phase", s.phase );
        }
        else
        {
            set( "period", 1 / s.frequency ); set( "duty", s.duty );
            set( "rise", s.rise ); set( "fall", s.fall );
        }
    }
    return c;
}
}
