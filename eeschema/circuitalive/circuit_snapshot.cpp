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

CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents,
                                     const std::vector<CIRCUIT_PROBE>& aProbes )
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
    auto allComponents = aComponents;
    for( std::size_t i = 0; i < aProbes.size(); ++i )
    {
        const auto& probe = aProbes[i];
        CIRCUIT_COMPONENT c;
        c.reference = "#probe" + std::to_string( i + 1 ); c.pins = probe.pins;
        const bool series = probe.kind == PROBE_KIND::CURRENT || probe.kind == PROBE_KIND::POWER;
        if( !c.pins.contains( "1" ) || ( series && !c.pins.contains( "2" ) ) )
            throw std::runtime_error( "Invalid probe pin mapping" );
        RESOLVED_MODEL model;
        std::vector<std::string> group;
        for( const auto& [pin, net] : c.pins ) { model.pins[pin] = pin; group.push_back( pin ); }
        model.model.electricalGroups.push_back( group );
        allComponents.push_back( std::move( c ) ); models.push_back( std::move( model ) );
    }
    const auto topology = BuildCircuitTopology( allComponents, models );
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
    auto signal = [&]( const std::string& name ) -> std::size_t
    {
        auto it = std::find( result.config.signals.begin(), result.config.signals.end(), name );
        if( it != result.config.signals.end() ) return it - result.config.signals.begin();
        result.config.signals.push_back( name ); return result.config.signals.size() - 1;
    };
    auto voltage = [&]( const std::string& node )
        { return node == "0" ? NO_SIGNAL : signal( "v(" + node + ")" ); };
    std::vector<std::pair<std::string, CURRENT_WARNING>> warnings;

    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& c = aComponents[i];
        try
        {
            if( c.reference.empty() || c.reference.find( '?' ) != std::string::npos
                || !references.insert( c.reference ).second )
                throw std::runtime_error( "Annotate the schematic: reference missing or repeated" );
            const auto& model = models.at( i );
            auto modelNodes = topology.nodes.at( i );
            if( !model.model.voltageConstraint.empty() && model.parameters.at( model.model.voltageConstraint ) != 0
                && modelNodes.at( model.pins.at( model.model.measurePositive ) )
                    == modelNodes.at( model.pins.at( model.model.measureNegative ) ) )
                throw std::runtime_error( "Short circuit across voltage-source terminals" );
            const auto& selected = c.measurements;
            if( selected.Any() )
            {
                auto p = selected.positive.empty() ? model.model.measurePositive : selected.positive;
                auto n = selected.negative.empty() ? model.model.measureNegative : selected.negative;
                if( p.empty() && model.model.pins.size() == 2 ) p = model.model.pins.front().id;
                if( n.empty() && model.model.pins.size() == 2 ) n = model.model.pins.back().id;
                if( p.empty() || n.empty() || p == n || !model.pins.contains( p ) || !model.pins.contains( n ) )
                    throw std::runtime_error( "Select the measurement's positive and negative model ports" );
                const auto pp = model.pins.at( p ), np = model.pins.at( n );
                MEASUREMENT_CHANNEL channel;
                channel.name = c.reference;
                if( selected.voltage || selected.power )
                { channel.positive = voltage( modelNodes.at( pp ) ); channel.negative = voltage( modelNodes.at( np ) ); }
                if( selected.current || selected.power )
                {
                    const auto source = "Vmeas" + std::to_string( i + 1 );
                    const auto node = "sense_ca" + std::to_string( i + 1 );
                    deck << source << ' ' << modelNodes.at( pp ) << ' ' << node << " 0\n";
                    modelNodes[pp] = node;
                    channel.current = signal( "i(" + source + ")" );
                }
                for( auto [enabled, quantity] : { std::pair{ selected.voltage, MEASURE_QUANTITY::VOLTAGE },
                        std::pair{ selected.current, MEASURE_QUANTITY::CURRENT },
                        std::pair{ selected.power, MEASURE_QUANTITY::POWER } } )
                    if( enabled ) { channel.quantity = quantity; result.measurements.push_back( channel ); }
            }
            auto expand = [&]( const std::string& text )
                { return ExpandModelTemplate( text, model, modelNodes, i ); };
            for( const auto& line : model.model.instance ) deck << expand( line ) << '\n';
            for( const auto& line : model.model.definitions ) deck << expand( line ) << '\n';
            if( !model.model.indicatorSignal.empty() )
            {
                result.leds.push_back( { i, result.config.signals.size(),
                    model.parameters.at( model.model.brightnessParameter ),
                    model.model.ratingParameter.empty() ? 0 : model.parameters.at( model.model.ratingParameter ), c.reference } );
                result.config.signals.push_back( expand( model.model.indicatorSignal ) );
            }
            if( !model.model.controlSource.empty() )
                result.switches.push_back( { i, expand( model.model.controlSource ),
                                            model.model.controlOn, model.model.controlOff } );
            if( !model.model.warningSignal.empty() && model.parameters.at( model.model.warningParameter ) > 0 )
                warnings.push_back( { expand( model.model.warningSignal ),
                    { c.reference, NO_SIGNAL, model.parameters.at( model.model.warningParameter ) } } );
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
    std::set<std::string> probeNames;
    for( std::size_t i = 0; i < aProbes.size(); ++i )
    {
        const auto& probe = aProbes[i];
        const auto& nodes = topology.nodes.at( aComponents.size() + i );
        auto name = !probe.name.empty() ? probe.name : !probe.label.empty() ? probe.label : AlphabeticProbeName( probe.sequence );
        const auto base = name;
        for( std::size_t suffix = 2; !probeNames.insert( name ).second; ++suffix ) name = base + " " + std::to_string( suffix );
        MEASUREMENT_CHANNEL channel;
        channel.name = name; channel.probe = i;
        channel.connected = !probe.pins.at( "1" ).empty();
        channel.scope = probe.kind == PROBE_KIND::OSCILLOSCOPE;
        if( probe.kind != PROBE_KIND::CURRENT ) channel.positive = voltage( nodes.at( "1" ) );
        if( probe.kind == PROBE_KIND::CURRENT || probe.kind == PROBE_KIND::POWER )
        {
            channel.connected = channel.connected && !probe.pins.at( "2" ).empty();
            if( nodes.at( "1" ) == nodes.at( "2" ) )
                throw std::runtime_error( name + ": current probe is bypassed by another wire or equal net labels" );
            const auto source = "Vprobe" + std::to_string( i + 1 );
            deck << source << ' ' << nodes.at( "1" ) << ' ' << nodes.at( "2" ) << " 0\n";
            channel.current = signal( "i(" + source + ")" );
            channel.quantity = probe.kind == PROBE_KIND::POWER ? MEASURE_QUANTITY::POWER : MEASURE_QUANTITY::CURRENT;
        }
        result.measurements.push_back( std::move( channel ) );
    }
    if( !errors.empty() )
    {
        std::string message;
        for( const auto& error : errors )
            message += error + '\n';
        throw std::runtime_error( message );
    }
    if( result.config.signals.empty() )
        result.config.signals.push_back( voltageSignal );
    for( auto& [name, warning] : warnings )
    { warning.signal = signal( name ); result.currentWarnings.push_back( warning ); }
    if( result.config.signals.size() > 64 ) throw std::runtime_error( "This session supports up to 64 saved signals; reduce the measurements" );
    if( std::any_of( result.measurements.begin(), result.measurements.end(), []( const auto& c ) { return c.scope; } ) )
        result.config.historyCapacity = std::min<std::size_t>( 4096, 65536 / result.config.signals.size() );
    // 1 Tohm numerical leakage keeps genuinely open/capacitive nodes solvable.
    // It is not a wire or logic pull-down; floating islands remain identified in the snapshot.
    deck << ".options rshunt=1e12\n.end\n";
    result.netlist = deck.str();
    return result;
}
}
