/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "circuit_snapshot.h"
#include "simulation_model.h"
#include "circuit_topology.h"

#include <algorithm>
#include <charconv>
#include <cctype>
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
    // Floating-point from_chars accepts '-', but not '+'. Accept one explicit
    // positive sign in every component/model editor, without accepting '+-5'.
    if( value.starts_with( "+" ) )
    {
        value.erase( 0, 1 );
        if( value.empty() || value[0] == '+' || value[0] == '-' )
            throw std::runtime_error( "Invalid numeric value: " + aValue );
    }
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
        { "V", 1 }, { "v", 1 }, { "F", 1 }, { "H", 1 }, { "deg", 1 }, { "A", 1 }, { "s", 1 }, { "Hz", 1 },
        { "f", 1e-15 }, { "p", 1e-12 }, { "n", 1e-9 },
        { "u", 1e-6 }, { "U", 1e-6 }, { "µ", 1e-6 }, { "μ", 1e-6 }, { "m", 1e-3 }, { "M", 1e6 },
        { "k", 1e3 }, { "K", 1e3 }, { "Meg", 1e6 }, { "meg", 1e6 }, { "G", 1e9 }, { "g", 1e9 }
    };
    // Permit customary unit suffixes, e.g. 100uF, 1kOhm, 5V.
    auto scale = scales.find( suffix );
    if( scale == scales.end() )
    {
        for( const std::string unit : { "Ohm", "ohm", "Ω", "F", "H", "deg", "V", "v", "A", "s", "Hz" } )
        {
            if( suffix.ends_with( unit ) )
            {
                scale = scales.find( suffix.substr( 0, suffix.size() - unit.size() ) );
                // A unit may follow a multiplier, never another unit (5vv,
                // 10Vdeg). All nonempty multipliers have a scale other than 1.
                if( scale != scales.end() && !scale->first.empty() && scale->second == 1 ) scale = scales.end();
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
                                     const std::vector<CIRCUIT_PROBE>& aProbes, bool aPinIndicators,
                                     const std::vector<POWER_SYMBOL>& aPowerSymbols,
                                     const SIMULATION_PREFERENCES& aPreferences,
                                     const std::vector<CIRCUIT_COMPONENT>& aPinObservers )
{
    CIRCUIT_SNAPSHOT result;
    result.config.accelerateExpressions = true;
    result.config.partitionable = true;
    auto preferences = aPreferences;
    preferences.Normalize();
    result.config.step = result.config.maxStep = preferences.maxTimeStepUs * 1e-6;
    result.config.horizon = std::min( preferences.durationSeconds, result.config.maxStep * 9e7 );
    result.config.historyCapacity = 256;
    std::string projectDirectory;
    for( const auto& component : aComponents )
        if( !component.projectDirectory.empty() ) { projectDirectory = component.projectDirectory; break; }
    MODEL_REPOSITORY repository( MODEL_REPOSITORY::DefaultDirectories( projectDirectory ) );
    std::vector<RESOLVED_MODEL> models;
    std::size_t memoryBytes=0;
    for( const auto& component : aComponents )
    {
        try
        {
            auto model = repository.Resolve( component );
            if( !preferences.pushButtonsReleased ) model.controlInitiallyClosed = true;
            const auto componentIndex = models.size();
            if( component.sourceProbe )
            {
                result.sourceProbes.emplace( componentIndex, *component.sourceProbe );
                if( *component.sourceProbe == PROBE_KIND::LOGIC )
                    model.controlInitiallyClosed = model.parameters.at( "initial" ) != 0;
            }
            for( auto region : InitialModelMemories( component, model.model ) )
            {
                memoryBytes += region.initial.data.size();
                if( memoryBytes > MAX_MEMORY_BYTES || result.config.memories.size() >= 128 )
                    throw std::runtime_error( "Circuit memory limit: 1 MiB and 128 regions" );
                const auto id = region.id;
                const auto slot = result.config.memories.size();
                model.memorySlots[id] = slot;
                result.memories.push_back( { componentIndex, id, region.label, slot, component.reference } );
                region.id = "ca" + std::to_string( componentIndex + 1 ) + "_" + id;
                region.label = component.reference + " — " + region.label;
                result.config.memories.push_back( std::move( region ) );
            }
            models.push_back( std::move( model ) );
        }
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
    const auto powerRails = CollectPowerRails( aPowerSymbols );
    const auto powerStart = allComponents.size();
    for( const auto& rail : powerRails )
    {
        CIRCUIT_COMPONENT c;
        c.reference = rail.reference; c.pins = { { "1", rail.net }, { "2", "0" } };
        RESOLVED_MODEL model;
        model.pins = { { "p", "1" }, { "n", "2" } };
        model.model.electricalGroups = { { "p", "n" } }; model.model.referencePin = "n";
        allComponents.push_back( std::move( c ) ); models.push_back( std::move( model ) );
    }
    std::set<std::string> groundNets;
    for( const auto& symbol : aPowerSymbols )
        if( !symbol.net.empty() && IsSimulationGround( symbol.value ) ) groundNets.insert( symbol.net );
    const auto topology = BuildCircuitTopology( allComponents, models, groundNets );
    struct NET_DRIVE
    {
        bool external = false;
        std::set<std::string> enables;
        std::set<std::pair<std::string, std::string>> drivers;
    };
    std::map<std::string, NET_DRIVE> drives;
    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& model = models[i];
        for( const auto& pin : model.model.pins )
        {
            auto& drive = drives[topology.nodes[i].at( model.pins.at( pin.id ) )];
            // A high-impedance input does not drive its own net. Analog models
            // can use ${drive:port} without declaring digital output metadata.
            if( model.model.logicVcc.empty() )
            {
                // A capacitor is not a DC source. Resistors/inductors carry
                // drive from their other terminal instead of inventing a LOW.
                if( pin.role != "input" && model.model.id != "resistor"
                    && model.model.id != "capacitor" && model.model.id != "inductor" )
                    drive.external = true;
            }
            else
            {
                if( auto enable = model.model.driverEnable.find( pin.id ); enable != model.model.driverEnable.end() )
                {
                    const auto enabled = ExpandModelTemplate( enable->second, model, topology.nodes[i], i );
                    drive.enables.insert( enabled );
                    if( auto code = model.model.driverValue.find( pin.id ); code != model.model.driverValue.end() )
                        drive.drivers.emplace( enabled, ExpandModelTemplate( code->second, model, topology.nodes[i], i ) );
                }
                if( auto internal = model.model.internalDrive.find( pin.id ); internal != model.model.internalDrive.end() )
                    drive.enables.insert( ExpandModelTemplate( internal->second, model, topology.nodes[i], i ) );
            }
        }
    }
    for( std::size_t i = 0; i < powerRails.size(); ++i )
        for( const auto& [pin, node] : topology.nodes[powerStart + i] ) drives[node].external = true;
    drives["0"].external = true;
    std::vector<std::pair<std::string, std::string>> drivePaths, sensorPaths;
    for( std::size_t i = 0; i < aProbes.size(); ++i )
        if( aProbes[i].kind == PROBE_KIND::CURRENT || aProbes[i].kind == PROBE_KIND::POWER )
            sensorPaths.emplace_back( topology.nodes[aComponents.size()+i].at( "1" ),
                                      topology.nodes[aComponents.size()+i].at( "2" ) );
    drivePaths = sensorPaths;
    for( std::size_t i = 0; i < aComponents.size(); ++i )
        if( models[i].model.id == "resistor" || models[i].model.id == "inductor" )
            drivePaths.emplace_back( topology.nodes[i].at( models[i].pins.at( "p" ) ),
                                     topology.nodes[i].at( models[i].pins.at( "n" ) ) );
    // Fixed point also handles chains of resistors and multiple inline meters.
    // Only presence is propagated: ngspice still resolves voltage, impedance,
    // input loading and the resulting current through each individual element.
    for( std::size_t pass = 0; pass <= drivePaths.size(); ++pass )
    {
        bool changed = false;
        for( const auto& [first, second] : drivePaths )
        {
            auto& a = drives[first]; auto& b = drives[second];
            const auto oldSize = a.enables.size() + b.enables.size() + a.drivers.size() + b.drivers.size();
            const bool external = a.external || b.external;
            changed |= a.external != external || b.external != external;
            a.external = b.external = external;
            a.enables.insert( b.enables.begin(), b.enables.end() ); b.enables = a.enables;
            a.drivers.insert( b.drivers.begin(), b.drivers.end() ); b.drivers = a.drivers;
            changed |= oldSize != a.enables.size() + b.enables.size() + a.drivers.size() + b.drivers.size();
        }
        if( !changed ) break;
    }
    for( std::size_t i = 0; i < aComponents.size(); ++i )
        for( const auto& pin : models[i].model.pins )
        {
            const auto& drive = drives.at( topology.nodes[i].at( models[i].pins.at( pin.id ) ) );
            std::string expression = drive.external ? "1" : "0";
            if( !drive.external ) for( const auto& enabled : drive.enables ) expression += "+(" + enabled + ")";
            models[i].driveExpressions[pin.id] = "(" + expression + ")";
        }
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
    std::map<std::string, std::size_t> expressionSignals;
    auto expressionSignal = [&]( const std::string& expression ) -> std::size_t
    {
        auto [it, inserted] = expressionSignals.try_emplace( expression, NO_SIGNAL );
        if( inserted )
        {
            if( expression.size() > 3 && expression.back() == ')'
                && ( expression.starts_with( "v(" ) || expression.starts_with( "i(" ) )
                && expression.find( '(', 2 ) == std::string::npos
                && expression.find( ',' ) == std::string::npos )
                it->second = signal( expression );
            else
            {
                const auto node = "ca_logic_monitor" + std::to_string( expressionSignals.size() );
                deck << 'B' << node << ' ' << node << " 0 V={" << expression << "}\n";
                it->second = voltage( node );
            }
        }
        return it->second;
    };
    // Current probes split solver nodes but not the electrical bus. Passive
    // resistors deliberately do not merge buses for contention detection.
    std::map<std::string, std::string> busParents;
    auto busRoot = [&]( std::string node )
    {
        while( busParents.contains( node ) && busParents.at( node ) != node ) node = busParents.at( node );
        return node;
    };
    for( const auto& [a, b] : sensorPaths )
        if( busRoot( a ) != busRoot( b ) ) busParents[busRoot( b )] = busRoot( a );
    struct DRIVER_PORT { std::size_t component; std::string port; bool programmable; };
    std::map<std::string, std::vector<DRIVER_PORT>> busPorts;
    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& model = models[i].model;
        for( const auto& pin : model.pins )
        {
            const bool programmable = model.runtime && std::any_of( model.runtime->pins.begin(), model.runtime->pins.end(),
                    [&]( const auto& p ) { return p.port == pin.id && p.drive; } );
            if( model.driverEnable.contains( pin.id ) || programmable
                || ( model.logicVcc.empty() && pin.role == "output" )
                || ( !model.voltageConstraint.empty() && pin.id == model.measurePositive ) )
                busPorts[busRoot( topology.nodes[i].at( models[i].pins.at( pin.id ) ) )].push_back(
                        { i, pin.id, programmable } );
        }
    }
    for( std::size_t i = 0; i < powerRails.size(); ++i )
        busPorts[busRoot( topology.nodes[powerStart+i].at( "1" ) )].push_back( { powerStart+i, "p", false } );
    std::map<std::pair<std::size_t, std::string>, std::pair<std::size_t, std::size_t>> driverPorts;
    for( const auto& [root, ports] : busPorts )
    {
        if( ports.size() < 2 ) continue;
        DRIVEN_NET net;
        std::set<std::string> nodes;
        for( const auto& [node, drive] : drives ) if( busRoot( node ) == root ) nodes.insert( node );
        net.nodes.assign( nodes.begin(), nodes.end() );
        for( const auto& port : ports )
        {
            NET_DRIVER driver;
            const auto& model = models[port.component];
            driver.reference = allComponents[port.component].reference;
            driver.pin = model.pins.at( port.port );
            driver.programmable = port.programmable;
            if( model.model.driverEnable.contains( port.port ) )
                driver.state.enable = expressionSignal( ExpandModelTemplate(
                    model.model.driverEnable.at( port.port ), model, topology.nodes[port.component], port.component ) );
            if( model.model.driverValue.contains( port.port ) )
                driver.state.value = expressionSignal( ExpandModelTemplate(
                    model.model.driverValue.at( port.port ), model, topology.nodes[port.component], port.component ) );
            if( port.programmable )
            {
                driver.voltage = voltage( topology.nodes[port.component].at( driver.pin ) );
                driver.ground = voltage( topology.nodes[port.component].at( model.pins.at( model.model.runtime->ground ) ) );
                driver.vcc = voltage( topology.nodes[port.component].at( model.pins.at( model.model.runtime->vcc ) ) );
            }
            driverPorts[{ port.component, port.port }] = { result.drivenNets.size(), net.drivers.size() };
            net.drivers.push_back( std::move( driver ) );
        }
        result.drivenNets.push_back( std::move( net ) );
    }
    auto senseDrivers = [&]( std::size_t component, std::map<std::string, std::string>& nodes )
    {
        for( const auto& [port, location] : driverPorts )
        {
            if( port.first != component ) continue;
            const auto& [netIndex, driverIndex] = location;
            const auto stem = "ca_bus" + std::to_string( netIndex + 1 ) + "_driver" + std::to_string( driverIndex + 1 );
            const auto& physical = models[component].pins.at( port.second );
            deck << 'V' << stem << ' ' << nodes.at( physical ) << ' ' << stem << " 0\n";
            nodes[physical] = stem;
            result.drivenNets[netIndex].drivers[driverIndex].current = signal( "i(V" + stem + ")" );
        }
    };
    std::vector<std::pair<std::string, CURRENT_WARNING>> warnings;
    VOLTAGE_CONSTRAINTS supplies;
    int programmableDriverSlot = 0;
    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& m = models[i];
        if( !m.model.voltageConstraint.empty() )
            supplies.Add( topology.nodes[i].at( m.pins.at( m.model.measurePositive ) ),
                topology.nodes[i].at( m.pins.at( m.model.measureNegative ) ),
                m.parameters.at( m.model.voltageConstraint ), aComponents[i].reference );
    }

    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& c = aComponents[i];
        const auto expansionStart = static_cast<std::size_t>( deck.tellp() );
        try
        {
            if( c.reference.empty() || c.reference.find( '?' ) != std::string::npos
                || !references.insert( c.reference ).second )
                throw std::runtime_error( "Annotate the schematic: reference missing or repeated" );
            const auto& model = models.at( i );
            if( !model.model.maxStepParameter.empty() )
            {
                const double step = model.parameters.at( model.model.maxStepParameter );
                if( step <= 0 ) throw std::runtime_error( "Maximum time step must be positive" );
                result.config.step = result.config.maxStep = std::min( result.config.maxStep, step );
                result.config.horizon = std::min( result.config.horizon, result.config.maxStep * 9e7 );
            }
            for( const auto& port : model.model.timingRcPorts )
            {
                const auto& node = topology.nodes[i].at( model.pins.at( port ) );
                double capacitance = 0, conductance = 0;
                for( std::size_t part = 0; part < models.size(); ++part )
                {
                    const auto& passive = models[part];
                    const bool resistor = passive.model.id == "resistor";
                    if( !resistor && passive.model.id != "capacitor" ) continue;
                    const auto& a = topology.nodes[part].at( passive.pins.at( "p" ) );
                    const auto& b = topology.nodes[part].at( passive.pins.at( "n" ) );
                    if( a == b || ( a != node && b != node ) ) continue;
                    if( resistor ) conductance += 1 / passive.parameters.at( "resistance" );
                    else capacitance += passive.parameters.at( "capacitance" );
                }
                // A local RC bound scales with the timer instead of forcing
                // the entire project to use a fast timer's fixed step. Unknown
                // or actively driven timing networks retain the conservative cap.
                const double step = capacitance > 0 && conductance > 0
                        ? capacitance / conductance / 32 : model.model.timingRcFallback;
                result.config.step = result.config.maxStep = std::min( result.config.maxStep, step );
                result.config.horizon = std::min( result.config.horizon, result.config.maxStep * 9e7 );
            }
            if( !model.model.frequencyParameter.empty() )
            {
                const double frequency = model.parameters.at( model.model.frequencyParameter );
                if( frequency <= 0 ) throw std::runtime_error( "Frequency must be positive" );
                // This caps smooth-source sampling; ngspice's truncation error
                // still shortens the step as needed. Pulse and firmware edges
                // have their own breakpoints and do not wait for this interval.
                result.config.step = result.config.maxStep = std::min( result.config.maxStep, 1 / ( frequency * 24 ) );
                result.config.horizon = std::min( result.config.horizon, result.config.maxStep * 9e7 );
            }
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
            senseDrivers( i, modelNodes );
            auto expand = [&]( const std::string& text )
                { return ExpandModelTemplate( text, model, modelNodes, i ); };
            for( const auto& line : model.model.instance ) deck << expand( line ) << '\n';
            for( const auto& line : model.model.definitions ) deck << expand( line ) << '\n';
            if( model.model.runtime )
            {
                auto runtime = InitialModelRuntime( c, model );
                const auto& descriptor = *model.model.runtime;
                const auto ground = modelNodes.at( model.pins.at( descriptor.ground ) );
                const auto vcc = modelNodes.at( model.pins.at( descriptor.vcc ) );
                const auto prefix = "ca" + std::to_string( i + 1 );
                for( const auto& pin : descriptor.pins )
                {
                    PROGRAMMABLE_PIN_BINDING binding;
                    binding.id = pin.id;
                    const auto node = modelNodes.at( model.pins.at( pin.port ) );
                    auto stem = prefix + "_" + pin.id;
                    std::transform( stem.begin(), stem.end(), stem.begin(),
                        []( unsigned char character ) { return std::tolower( character ); } );
                    // Read existing nodes directly. The backend subtracts its
                    // GND input, avoiding one extra voltage source per pin.
                    if( node != "0" ) { binding.signal = "v(" + node + ")"; signal( binding.signal ); }
                    if( pin.drive )
                    {
                        binding.driverSlot = programmableDriverSlot++;
                        deck << ".model " << stem << " ca_programmable_pin(slot=" << binding.driverSlot << ")\n";
                        deck << 'A' << stem << " %gd " << node << ' ' << ground
                             << " %gd " << node << ' ' << vcc << ' ' << stem << '\n';
                    }
                    else if( pin.id == "RESET" )
                        deck << "R" << stem << "_pullup " << node << ' ' << vcc << " 30000\n";
                    else if( pin.id == "AREF" )
                        deck << "R" << stem << "_default " << node << ' ' << vcc << " 1Meg\n";
                    deck << "R" << stem << "_input " << node << ' ' << ground << " 1e12\n";
                    runtime.bindings.push_back( std::move( binding ) );
                }
                if( result.config.programmableDevices.size() >= 32 )
                    throw std::runtime_error( "Circuit supports at most 32 programmable devices" );
                result.config.programmableDevices.push_back( std::move( runtime ) );
            }
            for( const auto& update : model.model.liveUpdates )
            {
                CIRCUIT_SNAPSHOT::LIVE_UPDATE u; u.change=update.change;
                u.change.reference=expand(u.change.reference);
                result.config.mutableElements.push_back(u.change.reference);
                for(auto& expression:u.change.expressions)expression=expand(expression);
                for(const auto& source:update.sources)u.sources.push_back(expand(source));
                for(const auto& value:update.initials)u.initials.push_back(ParseComponentValue(expand(value)));
                result.liveUpdates.push_back(std::move(u));
            }
            for(const auto& t:model.model.liveTexts)result.liveTexts.push_back({i,t.target,t.field,expand(t.source),
                t.onText,t.offText,ParseComponentValue(expand(t.initial)),t.on,t.off,t.slider});
            if( !model.model.indicatorSignal.empty() )
            {
                result.leds.push_back( { i, result.config.signals.size(),
                    model.parameters.at( model.model.brightnessParameter ),
                    model.model.ratingParameter.empty() ? 0 : model.parameters.at( model.model.ratingParameter ), c.reference,
                    ResolveLedAppearance(model.model.ledAppearance,model.parameters) } );
                result.config.signals.push_back( expand( model.model.indicatorSignal ) );
            }
            if( !model.model.controlSource.empty() )
                result.switches.push_back( { i, expand( model.model.controlSource ),
                                            model.model.controlOn, model.model.controlOff,
                                            model.controlInitiallyClosed, {}, {}, model.model.controlVisual } );
            for( const auto& control : model.model.controls )
                result.switches.push_back( { i, expand( control.source ), control.on, control.off,
                    control.initiallyClosed, control.id, control.label, control.visual } );
            for( const auto& control : model.model.adjustments )
            {
                const double initial = model.parameters.at( control.parameter );
                if( initial < control.minimum || initial > control.maximum )
                    throw std::runtime_error( "Live adjustment initial value is out of range" );
                result.adjustments.push_back( { i, control.id, control.label, expand( control.source ),
                    control.unit, control.minimum, control.maximum, control.step, initial, control.visual } );
            }
            auto currentWarnings = model.model.currentWarnings;
            if( !model.model.warningSignal.empty() )
                currentWarnings.push_back( { "", model.model.warningSignal, model.model.warningParameter } );
            std::size_t warningIndex = 0;
            for( const auto& descriptor : currentWarnings )
            {
                const auto index = warningIndex++;
                if( model.parameters.at( descriptor.parameter ) <= 0 ) continue;
                auto warning = expand( descriptor.signal );
                // .save accepts native vectors, not expressions such as abs(i(...))
                // or the maximum of several channel currents. Evaluate those in
                // the circuit and save a voltage carrying their numerical value.
                const bool native = warning.size() > 3 && warning.back() == ')'
                    && ( warning.starts_with( "i(" ) || warning.starts_with( "v(" ) )
                    && warning.find( '(', 2 ) == std::string::npos
                    && warning.find( ',' ) == std::string::npos;
                if( !native )
                {
                    const auto node = "ca" + std::to_string( i + 1 ) + "_current_warning"
                        + ( index ? "_" + std::to_string( index ) : "" );
                    deck << 'B' << node << ' ' << node << " 0 V={" << warning << "}\n";
                    warning = "v(" + node + ")";
                }
                warnings.push_back( { warning,
                    { c.reference + ( descriptor.label.empty() ? "" : " " + descriptor.label ),
                      NO_SIGNAL, model.parameters.at( descriptor.parameter ) } } );
            }
        }
        catch( const std::exception& e )
        {
            errors.push_back( c.reference + ": " + e.what() );
        }
        result.config.analogCouplings.emplace_back( expansionStart,
                static_cast<std::size_t>( deck.tellp() ) );
    }
    const bool observersOnly = aPinIndicators && !aPinObservers.empty()
                               && aComponents.empty() && powerRails.empty();
    if( aComponents.empty() && powerRails.empty() && !observersOnly )
        errors.push_back( "The schematic has no supported components" );
    std::string voltageSignal;
    for( const auto& pins : topology.nodes )
        for( const auto& [pin, name] : pins )
            if( name != "0" && voltageSignal.empty() ) voltageSignal = "v(" + name + ")";
    if( voltageSignal.empty() && !observersOnly )
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
            supplies.Add( nodes.at( "1" ), nodes.at( "2" ), 0, name );
            deck << source << ' ' << nodes.at( "1" ) << ' ' << nodes.at( "2" ) << " 0\n";
            channel.current = signal( "i(" + source + ")" );
            channel.quantity = probe.kind == PROBE_KIND::POWER ? MEASURE_QUANTITY::POWER : MEASURE_QUANTITY::CURRENT;
        }
        result.measurements.push_back( std::move( channel ) );
    }
    for( std::size_t i = 0; i < powerRails.size(); ++i )
    {
        const auto& rail = powerRails[i]; auto nodes = topology.nodes[powerStart + i];
        if( supplies.Add( nodes.at( "1" ), nodes.at( "2" ), rail.voltage,
                         rail.reference + " (" + rail.name + ")" ) )
        {
            senseDrivers( powerStart + i, nodes );
            const auto source = "Vpower" + std::to_string( i + 1 );
            deck << source << ' ' << nodes.at( "1" ) << ' ' << nodes.at( "2" ) << " DC " << rail.voltage << '\n';
            warnings.push_back( { "i(" + source + ")", { rail.name, NO_SIGNAL, 10 } } );
        }
    }
    for( auto& runtime : result.config.programmableDevices )
    {
        const auto component = std::find_if( aComponents.begin(), aComponents.end(),
                [&]( const auto& item ) { return item.reference == runtime.id; } );
        const auto index = std::size_t( component - aComponents.begin() );
        const auto& model = models.at( index );
        const auto& descriptor = *model.model.runtime;
        const auto& nodes = topology.nodes.at( index );
        const auto& ground = nodes.at( model.pins.at( descriptor.ground ) );
        const auto& vcc = nodes.at( model.pins.at( descriptor.vcc ) );
        bool fixed = supplies.Difference( vcc, ground ).has_value();
        const auto reset = std::find_if( descriptor.pins.begin(), descriptor.pins.end(),
                []( const auto& pin ) { return pin.id == "RESET"; } );
        if( reset != descriptor.pins.end() )
        {
            const auto& physical = model.pins.at( reset->port );
            const auto connection = component->pins.find( physical );
            fixed = fixed && ( connection == component->pins.end() || connection->second.empty()
                || supplies.Difference( nodes.at( physical ), ground ).has_value() );
        }
        if( fixed ) runtime.autonomousWindowSeconds = .001;
        const auto window = std::max( runtime.quantumSeconds, runtime.autonomousWindowSeconds );
        result.config.step = result.config.maxStep = std::min( result.config.maxStep, window );
        result.config.horizon = std::min( result.config.horizon, result.config.maxStep * 9e7 );
    }
    if( !errors.empty() )
    {
        std::string message;
        for( const auto& error : errors )
            message += error + '\n';
        throw std::runtime_error( message );
    }
    if( result.config.signals.empty() && !observersOnly )
        result.config.signals.push_back( voltageSignal );
    for( auto& [name, warning] : warnings )
    { warning.signal = signal( name ); result.currentWarnings.push_back( warning ); }
    if( aPinIndicators )
        for( std::size_t i = 0; i < aComponents.size(); ++i )
        {
            const auto& model = models[i];
            for( const auto& pin : model.model.pins )
            {
                LOGIC_PIN channel;
                channel.component = i; channel.pin = model.pins.at( pin.id ); channel.reference = aComponents[i].reference;
                channel.voltage = voltage( topology.nodes[i].at( channel.pin ) );
                channel.digital = model.model.runtime.has_value() || !model.model.logicVcc.empty();
                if( model.model.runtime )
                {
                    // Firmware models declare their power ports in the runtime
                    // descriptor rather than the SPICE logic-driver metadata.
                    // Their indicators must follow that same local supply.
                    const auto& runtime = *model.model.runtime;
                    channel.vcc = voltage( topology.nodes[i].at( model.pins.at( runtime.vcc ) ) );
                    channel.ground = voltage( topology.nodes[i].at( model.pins.at( runtime.ground ) ) );
                    channel.minSupply = runtime.minimumSupply;
                    channel.maxSupply = runtime.maximumSupply;
                }
                else if( channel.digital )
                {
                    channel.vcc = voltage( topology.nodes[i].at( model.pins.at( model.model.logicVcc ) ) );
                    channel.ground = voltage( topology.nodes[i].at( model.pins.at( model.model.logicGround ) ) );
                    channel.low = model.parameters.at( model.model.logicLow ); channel.high = model.parameters.at( model.model.logicHigh );
                    channel.minSupply = model.parameters.at( model.model.logicMinSupply ); channel.maxSupply = model.parameters.at( model.model.logicMaxSupply );
                    if( !model.model.logicThresholdReference.empty() )
                        channel.thresholdReference = model.parameters.at( model.model.logicThresholdReference );
                    channel.output = model.model.driverEnable.contains( pin.id );
                    if( model.model.driverValue.contains( pin.id ) )
                        channel.driverValue = expressionSignal( ExpandModelTemplate( model.model.driverValue.at( pin.id ), model, topology.nodes[i], i ) );
                    const auto& drive = drives.at( topology.nodes[i].at( channel.pin ) );
                    channel.externalDrive = drive.external || pin.role == "power" || pin.role == "reference";
                    if( channel.output )
                        channel.driverEnable = expressionSignal( ExpandModelTemplate( model.model.driverEnable.at( pin.id ), model, topology.nodes[i], i ) );
                    for( const auto& enabled : drive.enables ) channel.enables.push_back( expressionSignal( enabled ) );
                    for( const auto& [enabled, code] : drive.drivers )
                        channel.drivers.push_back( { expressionSignal( enabled ), expressionSignal( code ) } );
                }
                result.logicPins.push_back( std::move( channel ) );
            }
        }
    if( aPinIndicators && !aPinObservers.empty() )
    {
        // Observers never enter model resolution or topology. In particular, they
        // cannot join islands, create ground references, load a net or enable a driver.
        std::map<std::string, std::string> netNodes;
        for( std::size_t i = 0; i < allComponents.size(); ++i )
            for( const auto& [port, pin] : models[i].pins )
            {
                const auto& net = allComponents[i].pins.at( pin );
                if( !net.empty() ) netNodes[net] = topology.nodes[i].at( pin );
            }

        const std::size_t activePins = result.logicPins.size();
        for( const auto& observer : aPinObservers )
        {
            const auto index = result.pinNodes.size();
            result.pinNodes.emplace_back();
            for( const auto& [pin, net] : observer.pins )
            {
                std::string node;
                if( !net.empty() && ( IsSimulationGround( net ) || groundNets.contains( net ) ) )
                    node = "0";
                else if( const auto found = netNodes.find( net ); !net.empty() && found != netNodes.end() )
                    node = found->second;
                result.pinNodes.back()[pin] = node;

                LOGIC_PIN channel;
                // Reuse the existing bus thresholds and supply when a digital pin
                // already monitors it; otherwise use the standard 5 V indication.
                for( std::size_t i = 0; !node.empty() && i < activePins; ++i )
                {
                    const auto& peer = result.logicPins[i];
                    if( peer.digital && result.pinNodes[peer.component].at( peer.pin ) == node )
                    {
                        channel = peer;
                        break;
                    }
                }
                channel.component = index; channel.pin = pin; channel.reference = observer.reference;
                channel.observer = channel.digital = true;
                channel.output = false;
                channel.driverEnable = channel.driverValue = NO_SIGNAL;
                channel.voltage = node.empty() ? NO_SIGNAL : voltage( node );
                channel.externalDrive = false;
                channel.enables.clear(); channel.drivers.clear();
                if( const auto drive = drives.find( node ); drive != drives.end() )
                {
                    channel.externalDrive = drive->second.external;
                    for( const auto& enabled : drive->second.enables )
                        channel.enables.push_back( expressionSignal( enabled ) );
                    for( const auto& [enabled, code] : drive->second.drivers )
                        channel.drivers.push_back( { expressionSignal( enabled ), expressionSignal( code ) } );
                }
                result.logicPins.push_back( std::move( channel ) );
            }
        }
    }
    if( result.config.signals.empty() )
    {
        if( observersOnly )
        {
            // A solver heartbeat for an entirely excluded schematic. This internal
            // node is isolated from every observed terminal, which remains Z.
            deck << "Bca_observer_clock ca_observer_clock 0 V=0\n";
            voltageSignal = "v(ca_observer_clock)";
        }
        result.config.signals.push_back( voltageSignal );
    }
    if( result.config.signals.size() > MAX_SESSION_SIGNALS )
        throw std::runtime_error( "Too many saved signals; reduce the circuit or measurements" );
    result.config.historyCapacity = std::min<std::size_t>( 256, MAX_SESSION_HISTORY_VALUES / result.config.signals.size() );
    if( std::any_of( result.measurements.begin(), result.measurements.end(), []( const auto& c ) { return c.scope; } ) )
        result.config.historyCapacity = std::min<std::size_t>( 4096, MAX_SESSION_HISTORY_VALUES / result.config.signals.size() );
    // 1 Tohm numerical leakage keeps genuinely open/capacitive nodes solvable.
    // It is not a wire or logic pull-down; floating islands remain identified in the snapshot.
    deck << ".options rshunt=1e12\n.end\n";
    result.netlist = deck.str();
    return result;
}
}
