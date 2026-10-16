/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "circuit_snapshot.h"
#include "component_catalog.h"
#include "circuit_topology.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
double ParseComponentValue( const std::string& aValue )
{
    std::string value = aValue;
    value.erase( std::remove_if( value.begin(), value.end(),
                               []( unsigned char c ) { return c == ' ' || c == '\t'; } ), value.end() );
    // KiCad values also use the multiplier as a decimal marker: 4R7, 4k7, 2u2.
    const auto marker = value.find_first_not_of( "+-0123456789" );
    if( marker != std::string::npos && std::string( "RrKkMmGgUuNnPp" ).find( value[marker] ) != std::string::npos
        && marker + 1 < value.size() && value[marker + 1] >= '0' && value[marker + 1] <= '9' )
    {
        auto end = value.find_first_not_of( "0123456789", marker + 1 );
        if( end == std::string::npos ) end = value.size();
        const auto whole = marker == 0 ? "0" : value.substr( 0, marker );
        value = whole + "." + value.substr( marker + 1, end - marker - 1 )
                + value[marker] + value.substr( end );
    }
    double number = 0;
    auto [end, error] = std::from_chars( value.data(), value.data() + value.size(), number );
    if( error != std::errc() || !std::isfinite( number ) )
        throw std::runtime_error( "Invalid numeric value: " + aValue );

    std::string suffix( end, static_cast<std::size_t>( value.data() + value.size() - end ) );
    // These are schematic Value fields: follow KiCad/SI (M = mega, m = milli).
    // The netlist receives the parsed number, never the original text.
    const std::map<std::string, double> scales = {
        { "", 1 }, { "R", 1 }, { "r", 1 }, { "Ohm", 1 }, { "ohm", 1 }, { "Ω", 1 },
        { "V", 1 }, { "F", 1 }, { "f", 1e-15 }, { "p", 1e-12 }, { "n", 1e-9 },
        { "u", 1e-6 }, { "U", 1e-6 }, { "µ", 1e-6 }, { "μ", 1e-6 }, { "m", 1e-3 }, { "M", 1e6 },
        { "k", 1e3 }, { "K", 1e3 }, { "Meg", 1e6 }, { "meg", 1e6 }, { "G", 1e9 }, { "g", 1e9 }
    };
    // Permit customary unit suffixes, e.g. 100uF, 1kOhm, 5V.
    auto scale = scales.find( suffix );
    if( scale == scales.end() )
    {
        for( const std::string unit : { "Ohm", "ohm", "Ω", "F", "V" } )
        {
            if( suffix.ends_with( unit ) )
            {
                scale = scales.find( suffix.substr( 0, suffix.size() - unit.size() ) );
                break;
            }
        }
    }
    if( scale == scales.end() || !std::isfinite( number * scale->second ) )
        throw std::runtime_error( "Unsupported numeric suffix: " + aValue );
    return number * scale->second;
}

double LedBrightness( double aForwardCurrent )
{
    // Display response, not an optical/damage model. Reverse current never lights the LED.
    return std::sqrt( std::clamp( aForwardCurrent / 0.01, 0.0, 1.0 ) );
}

CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents )
{
    CIRCUIT_SNAPSHOT result;
    result.config.step = result.config.maxStep = 1e-4;
    result.config.horizon = 3600; // Finite internal horizon: completion is explicit in the UI.
    result.config.historyCapacity = 256;
    const auto topology = BuildCircuitTopology( aComponents );
    result.pinNodes = topology.nodes;
    result.automaticReferences = topology.automaticReferences;
    result.floatingIslands = topology.floatingIslands;
    result.openPins = topology.openPins;
    std::set<std::string> references;
    std::vector<std::string> errors;
    std::ostringstream deck;
    deck.imbue( std::locale::classic() );
    deck.precision( 17 );
    deck << "CircuitAliveEDA interactive schematic snapshot\n";

    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& c = aComponents[i];
        try
        {
            if( c.reference.empty() || c.reference.find( '?' ) != std::string::npos
                || !references.insert( c.reference ).second )
                throw std::runtime_error( "Annotate the schematic: reference missing or repeated" );
            const auto behavior = FindBuiltinBehavior( c.libraryId, c.deviceHint );
            auto node = [&]( const std::string& pin ) { return topology.nodes.at( i ).at( pin ); };
            const bool fixedPassive = behavior == BUILTIN_BEHAVIOR::RESISTOR
                                      || behavior == BUILTIN_BEHAVIOR::CAPACITOR;
            // R/C in this initial catalogue always use Value and the fixed ideal behaviour.
            // Existing Sim.* fields belong to KiCad's traditional simulator, not an external-model requirement.
            if( c.explicitSpiceModel && !fixedPassive )
                throw std::runtime_error( "Custom SPICE model is not supported for " + c.libraryId );

            const std::string id = std::to_string( i + 1 );
            if( fixedPassive )
            {
                double value = ParseComponentValue( c.value );
                if( value <= 0 )
                    throw std::runtime_error( "R and C must have a positive value" );
                bool capacitor = behavior == BUILTIN_BEHAVIOR::CAPACITOR;
                deck << ( capacitor ? "C" : "R" ) << "ca" << id << ' ' << node( "1" )
                     << ' ' << node( "2" ) << ' ' << value << ( capacitor ? " ic=0" : "" ) << '\n';
            }
            else if( behavior == BUILTIN_BEHAVIOR::DC_VOLTAGE )
            {
                deck << "Vca" << id << ' ' << node( "1" ) << ' ' << node( "2" )
                     << " DC " << ParseComponentValue( c.value ) << '\n';
            }
            else if( behavior == BUILTIN_BEHAVIOR::RED_LED )
            {
                // Initial fixed red teaching model; the symbol's display value is not a model selector.
                deck << "Dca" << id << ' ' << node( "2" ) << ' ' << node( "1" )
                     << " CA_RED\n"; // KiCad pin 1 = cathode, pin 2 = anode.
                result.leds.push_back( { i, result.config.signals.size() } );
                result.config.signals.push_back( "@dca" + id + "[id]" );
            }
            else if( behavior == BUILTIN_BEHAVIOR::SWITCH )
            {
                std::string source = "Vcontrol" + id;
                deck << source << " control" << id << " 0 5\n"
                     << "Sca" << id << ' ' << node( "1" ) << ' ' << node( "2" )
                     << " control" << id << " 0 CA_SW\n";
                result.switches.push_back( { i, source } );
            }
            else
                throw std::runtime_error( "No interactive model for " + c.libraryId );
        }
        catch( const std::exception& e )
        {
            errors.push_back( c.reference + ": " + e.what() );
        }
    }
    if( aComponents.empty() )
        errors.push_back( "The schematic has no supported components" );
    std::string voltageSignal;
    for( const auto& pins : topology.nodes )
        for( const auto& [pin, name] : pins )
            if( name != "0" && voltageSignal.empty() ) voltageSignal = "v(" + name + ")";
    if( voltageSignal.empty() )
        errors.push_back( "Connect at least one non-ground net" );
    if( result.config.signals.size() > 64 )
        errors.push_back( "This prototype supports at most 64 LED signals" );
    if( !errors.empty() )
    {
        std::string message;
        for( const auto& error : errors )
            message += error + '\n';
        throw std::runtime_error( message );
    }
    if( result.config.signals.empty() )
        result.config.signals.push_back( voltageSignal );
    // 1 Tohm numerical leakage keeps genuinely open/capacitive nodes solvable.
    // It is not a wire or logic pull-down; floating islands remain identified in the snapshot.
    deck << ".options rshunt=1e12\n"
         << ".model CA_SW SW(Ron=0.01 Roff=1e12 Vt=2.5 Vh=0.1)\n"
         << ".model CA_RED D(Is=1e-20 N=2 Rs=5)\n.end\n";
    result.netlist = deck.str();
    return result;
}
}
