/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "circuit_snapshot.h"
#include "simulation_model.h"
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
        { "V", 1 }, { "F", 1 }, { "A", 1 }, { "s", 1 }, { "Hz", 1 },
        { "f", 1e-15 }, { "p", 1e-12 }, { "n", 1e-9 },
        { "u", 1e-6 }, { "U", 1e-6 }, { "µ", 1e-6 }, { "μ", 1e-6 }, { "m", 1e-3 }, { "M", 1e6 },
        { "k", 1e3 }, { "K", 1e3 }, { "Meg", 1e6 }, { "meg", 1e6 }, { "G", 1e9 }, { "g", 1e9 }
    };
    // Permit customary unit suffixes, e.g. 100uF, 1kOhm, 5V.
    auto scale = scales.find( suffix );
    if( scale == scales.end() )
    {
        for( const std::string unit : { "Ohm", "ohm", "Ω", "F", "V", "A", "s", "Hz" } )
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

double LedBrightness( double aForwardCurrent, double aBrightnessCurrent )
{
    // Display response, not an optical/damage model. Reverse current never lights the LED.
    return std::sqrt( std::clamp( aForwardCurrent / aBrightnessCurrent, 0.0, 1.0 ) );
}

CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents )
{
    CIRCUIT_SNAPSHOT result;
    result.config.step = result.config.maxStep = 1e-4;
    result.config.horizon = 3600; // Finite internal horizon: completion is explicit in the UI.
    result.config.historyCapacity = 256;
    std::string projectDirectory;
    for( const auto& component : aComponents )
        if( !component.projectDirectory.empty() ) { projectDirectory = component.projectDirectory; break; }
    MODEL_REPOSITORY repository( MODEL_REPOSITORY::DefaultDirectories( projectDirectory ) );
    std::vector<RESOLVED_MODEL> models;
    for( const auto& component : aComponents )
    {
        try { models.push_back( repository.Resolve( component ) ); }
        catch( const std::exception& e ) { throw std::runtime_error( component.reference + ": " + e.what() ); }
    }
    const auto topology = BuildCircuitTopology( aComponents, models );
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
            const auto& model = models.at( i );
            auto expand = [&]( const std::string& text )
                { return ExpandModelTemplate( text, model, topology.nodes.at( i ), i ); };
            for( const auto& line : model.model.instance ) deck << expand( line ) << '\n';
            for( const auto& line : model.model.definitions ) deck << expand( line ) << '\n';
            if( !model.model.indicatorSignal.empty() )
            {
                result.leds.push_back( { i, result.config.signals.size(),
                    model.parameters.at( model.model.brightnessParameter ),
                    model.model.ratingParameter.empty() ? 0 : model.parameters.at( model.model.ratingParameter ) } );
                result.config.signals.push_back( expand( model.model.indicatorSignal ) );
            }
            if( !model.model.controlSource.empty() )
                result.switches.push_back( { i, expand( model.model.controlSource ),
                                            model.model.controlOn, model.model.controlOff } );
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
    deck << ".options rshunt=1e12\n.end\n";
    result.netlist = deck.str();
    return result;
}
}
