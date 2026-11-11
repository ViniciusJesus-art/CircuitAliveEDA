/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/simulation_model.h>
#include <circuitalive/source_probe.h>
#include <circuitalive/visual_signals.h>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace CIRCUITALIVE;

namespace
{
CIRCUIT_COMPONENT Supply( const std::string& reference, const std::string& net, const std::string& value )
{
    return { reference, "Simulation_SPICE:VDC", value, { { "1", net }, { "2", "0" } } };
}

CIRCUIT_COMPONENT Memory( const std::string& reference, const std::string& modelId )
{
    CIRCUIT_COMPONENT part;
    part.reference = reference;
    part.modelFile = "builtin:" + modelId;
    const auto model = MODEL_REPOSITORY().SelectModel( part );
    for( const auto& pin : model.pins )
        part.pins[pin.defaultPin] = pin.id == "vcc" ? "vcc" : pin.id == "oe" ? reference + "_oe"
            : pin.id == "we" ? "vcc" : pin.id.starts_with( "d" ) ? "bus" + pin.id.substr( 1 ) : "0";
    return part;
}

const LOGIC_PIN& Pin( const CIRCUIT_SNAPSHOT& circuit, const std::string& reference, const std::string& pin )
{
    const auto it = std::find_if( circuit.logicPins.begin(), circuit.logicPins.end(),
        [&]( const auto& p ) { return p.reference == reference && p.pin == pin; } );
    if( it == circuit.logicPins.end() ) throw std::runtime_error( "Missing bus test pin" );
    return *it;
}

EDITOR_SESSION_VIEW View( const CIRCUIT_SNAPSHOT& circuit, INTERACTIVE_SIM_SESSION& session, double time )
{
    session.AdvanceTo( time );
    EDITOR_SESSION_VIEW view;
    view.state = EDITOR_SESSION_STATE::RUNNING;
    view.sample = session.Snapshot();
    UpdateVisualSignals( circuit, view, 1 );
    return view;
}

std::size_t Conflicts( const EDITOR_SESSION_VIEW& view )
{
    return std::count( view.netContentions.begin(), view.netContentions.end(), true );
}

CIRCUIT_COMPONENT Buffer( const std::string& ref, const std::string& enable, const std::string& input,
                          const std::string& output = "bus" )
{
    CIRCUIT_COMPONENT part{ ref, "74xx:74HC125", "74HC125", {} };
    for( int pin = 1; pin <= 14; ++pin ) part.pins[std::to_string( pin )] = "";
    part.pins["14"] = "vcc"; part.pins["7"] = "0";
    part.pins["1"] = enable; part.pins["2"] = input; part.pins["3"] = output;
    return part;
}
}

BOOST_AUTO_TEST_CASE( DigitalBusDisabledEpromReadsRamAndFloatingBusReturnsToZ )
{
    auto circuit = BuildCircuitSnapshot( { Supply( "VS", "vcc", "5" ),
        Supply( "VROMOE", "ROM_oe", "5" ), Supply( "VRAMOE", "RAM_oe", "0" ),
        Memory( "ROM", "eprom_27c512_55" ), Memory( "RAM", "sram_62256" ) }, {}, true );
    BOOST_REQUIRE_EQUAL( circuit.config.memories.size(), 2u );
    circuit.config.memories[1].initial.data[0] = 0xa5;
    circuit.config.memories[1].initial.known[0] = 0xff;
    BOOST_REQUIRE_EQUAL( circuit.drivenNets.size(), 8u );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    auto view = View( circuit, session, 1e-6 );
    BOOST_CHECK_EQUAL( Conflicts( view ), 0u );
    unsigned bit = 0;
    for( const auto* physical : { "11", "12", "13", "15", "16", "17", "18", "19" } )
    {
        const auto& rom = Pin( circuit, "ROM", physical );
        const auto& ram = Pin( circuit, "RAM", physical );
        BOOST_CHECK( PinDriveLevel( rom, view.sample.latest ) == LOGIC_LEVEL::HIGH_Z );
        const auto level = ( 0xa5 & ( 1 << bit++ ) ) ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::LOW;
        BOOST_CHECK( PinLogicLevel( rom, view.sample.latest ) == level );
        BOOST_CHECK( PinLogicLevel( ram, view.sample.latest ) == level );
        BOOST_CHECK_SMALL( view.sample.latest.values.at( rom.voltage ) - view.sample.latest.values.at( ram.voltage ), 1e-9 );
    }
    session.SetSourceValue( "Vca3", 5 );
    view = View( circuit, session, 2e-6 );
    for( const auto* physical : { "11", "12", "13", "15", "16", "17", "18", "19" } )
    {
        BOOST_CHECK( PinLogicLevel( Pin( circuit, "ROM", physical ), view.sample.latest ) == LOGIC_LEVEL::HIGH_Z );
        BOOST_CHECK( PinLogicLevel( Pin( circuit, "RAM", physical ), view.sample.latest ) == LOGIC_LEVEL::HIGH_Z );
    }
    session.SetSourceValue( "Vca2", 0 );
    view = View( circuit, session, 3e-6 );
    for( const auto* physical : { "11", "12", "13", "15", "16", "17", "18", "19" } )
    {
        BOOST_CHECK( PinLogicLevel( Pin( circuit, "RAM", physical ), view.sample.latest ) == LOGIC_LEVEL::HIGH );
        BOOST_CHECK( PinDriveLevel( Pin( circuit, "RAM", physical ), view.sample.latest ) == LOGIC_LEVEL::HIGH_Z );
    }
    // ROM's erased FF fights only the four LOW bits of the RAM's A5.
    session.SetSourceValue( "Vca3", 0 );
    view = View( circuit, session, 4e-6 );
    BOOST_CHECK_EQUAL( Conflicts( view ), 4u );
    for( std::size_t i = 0; i < circuit.drivenNets.size(); ++i )
        if( view.netContentions[i] )
        {
            const auto& net = circuit.drivenNets[i];
            BOOST_CHECK_EQUAL( ContendingDrivers( net, view.sample.latest ).size(), 2u );
            for( const auto& driver : net.drivers )
                BOOST_CHECK_GT( std::abs( view.sample.latest.values.at( driver.current ) ), .04 );
        }
    session.SetSourceValue( "Vca2", 5 );
    view = View( circuit, session, 5e-6 );
    BOOST_CHECK_EQUAL( Conflicts( view ), 0u );
    BOOST_CHECK( view.contentionWarnings.empty() );
}

BOOST_AUTO_TEST_CASE( DigitalBusPullsResolveDisabledOutputsWithoutContention )
{
    for( const bool high : { false, true } )
    {
        const auto circuit = BuildCircuitSnapshot( { Supply( "VS", "vcc", "5" ),
            Buffer( "U1", "vcc", "0" ), Buffer( "U2", "vcc", "0" ),
            { "R1", "Device:R", "10k", { { "1", "bus" }, { "2", high ? "vcc" : "0" } } } }, {}, true );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        const auto view = View( circuit, session, 1e-6 );
        for( const auto* ref : { "U1", "U2" } )
        {
            const auto& pin = Pin( circuit, ref, "3" );
            BOOST_CHECK( PinDriveLevel( pin, view.sample.latest ) == LOGIC_LEVEL::HIGH_Z );
            BOOST_CHECK( PinLogicLevel( pin, view.sample.latest ) == ( high ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::LOW ) );
        }
        BOOST_CHECK_EQUAL( Conflicts( view ), 0u );
    }
}

BOOST_AUTO_TEST_CASE( DigitalBusOpposingOutputsWithInlineMeterRespectResistanceAndRecover )
{
    const auto circuit = BuildCircuitSnapshot( { Supply( "VS", "vcc", "5" ),
        Supply( "VOE", "oe", "0" ), Buffer( "U1", "0", "vcc", "left" ),
        Buffer( "U2", "oe", "0", "right" ) },
        { { PROBE_KIND::CURRENT, "Bus current", "", { { "1", "left" }, { "2", "right" } }, 1 } }, true );
    BOOST_REQUIRE_EQUAL( circuit.drivenNets.size(), 1u );
    BOOST_CHECK_EQUAL( circuit.drivenNets[0].nodes.size(), 2u );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    auto view = View( circuit, session, 1e-6 );
    BOOST_CHECK_EQUAL( Conflicts( view ), 1u );
    const double current = MeasurementValue( circuit.measurements.at( 0 ), view.sample.latest );
    BOOST_CHECK_CLOSE( current, .1, .01 ); // 5 V / (25 + 25 ohms), no ideal short.
    session.SetSourceValue( "Vca2", 5 );
    view = View( circuit, session, 2e-6 );
    BOOST_CHECK_EQUAL( Conflicts( view ), 0u );
    BOOST_CHECK( PinLogicLevel( Pin( circuit, "U2", "3" ), view.sample.latest ) == LOGIC_LEVEL::HIGH );
}

BOOST_AUTO_TEST_CASE( DigitalBusSameLevelDriversAndNormalResistiveLoadsDoNotConflict )
{
    for( const bool high : { false, true } )
    {
        const auto circuit = BuildCircuitSnapshot( { Supply( "VS", "vcc", "5" ),
            Buffer( "U1", "0", high ? "vcc" : "0" ), Buffer( "U2", "0", high ? "vcc" : "0" ),
            { "R1", "Device:R", "1k", { { "1", "bus" }, { "2", high ? "0" : "vcc" } } } }, {}, true );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        const auto view = View( circuit, session, 1e-6 );
        BOOST_CHECK_EQUAL( Conflicts( view ), 0u );
        BOOST_CHECK( PinLogicLevel( Pin( circuit, "U1", "3" ), view.sample.latest )
                     == ( high ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::LOW ) );
    }
}

BOOST_AUTO_TEST_CASE( DigitalBusOutputAgainstPowerLabelWarnsButPullResistanceDoesNot )
{
    for( const bool resistor : { false, true } )
    {
        std::vector<CIRCUIT_COMPONENT> parts{ Buffer( "U1", "0", "0" ) };
        if( resistor ) parts.push_back( { "R1", "Device:R", "10k", { { "1", "vcc" }, { "2", "bus" } } } );
        const auto circuit = BuildCircuitSnapshot( parts, {}, true,
            { { "#PWR1", "+5V", "vcc" }, { "#PWR2", "+5V", resistor ? "vcc" : "bus" } } );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        const auto view = View( circuit, session, 1e-6 );
        BOOST_CHECK_EQUAL( Conflicts( view ), resistor ? 0u : 1u );
    }
}

BOOST_AUTO_TEST_CASE( DigitalBusFloatingResistorAndCapacitorDoNotInventADriver )
{
    const auto circuit = BuildCircuitSnapshot( { Supply( "VS", "vcc", "5" ), Buffer( "U1", "vcc", "0" ),
        { "R1", "Device:R", "10k", { { "1", "bus" }, { "2", "floating" } } },
        { "C1", "Device:C", "10p", { { "1", "bus" }, { "2", "0" } } } }, {}, true );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    const auto view = View( circuit, session, 1e-6 );
    BOOST_CHECK( PinLogicLevel( Pin( circuit, "U1", "3" ), view.sample.latest ) == LOGIC_LEVEL::HIGH_Z );
}

BOOST_AUTO_TEST_CASE( DigitalBusOpenCollectorWiredOutputsAndPullupDoNotConflict )
{
    const auto circuit = BuildCircuitSnapshot( { Supply( "VS", "vcc", "5" ), Supply( "VIN", "in", "0" ),
        { "U1", "Comparator:LM393", "LM393", { { "8", "vcc" }, { "4", "0" },
            { "3", "in" }, { "2", "vcc" }, { "1", "bus" },
            { "5", "0" }, { "6", "vcc" }, { "7", "bus" } } },
        { "R1", "Device:R", "10k", { { "1", "bus" }, { "2", "vcc" } } } }, {}, true );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    const auto view = View( circuit, session, 1e-4 );
    BOOST_CHECK_EQUAL( Conflicts( view ), 0u );
    BOOST_CHECK( PinLogicLevel( Pin( circuit, "U1", "1" ), view.sample.latest ) == LOGIC_LEVEL::LOW );
}

BOOST_AUTO_TEST_CASE( DigitalBusProgrammableWeakPullupIsNotAnOpposingOutput )
{
    DRIVEN_NET net;
    NET_DRIVER gpio; gpio.current = 0; gpio.voltage = 2; gpio.vcc = 3; gpio.programmable = true;
    NET_DRIVER low; low.current = 1;
    net.drivers = { gpio, low };
    SIM_SAMPLE sample{ 0, { -5.0 / 30000, 5.0 / 30000, 0, 5 } };
    BOOST_CHECK( ContendingDrivers( net, sample ).empty() );
    sample.values = { -.1, .1, 2.5, 5 };
    BOOST_CHECK_EQUAL( ContendingDrivers( net, sample ).size(), 2u );
}

BOOST_AUTO_TEST_CASE( DigitalBusContentionWarnsWithoutPausingAndClearsWhileRunning )
{
    SOURCE_PROBE_SETTINGS high; high.initialHigh = true; high.resistance = 50;
    SOURCE_PROBE_SETTINGS low; low.resistance = 50;
    const auto circuit = BuildCircuitSnapshot( { SourceProbeComponent( PROBE_KIND::LOGIC, high, "#P1", "bus" ),
        SourceProbeComponent( PROBE_KIND::LOGIC, low, "#P2", "bus" ) } ); // Indicators need not be enabled.
    BOOST_REQUIRE_EQUAL( circuit.drivenNets.size(), 1u );
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    runner.Start( circuit );
    auto await = [&]( bool conflict )
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        for( ;; )
        {
            const auto view = runner.View();
            BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
            if( view.state == EDITOR_SESSION_STATE::RUNNING && view.sample.latest.time > 0
                && !view.netContentions.empty() && view.netContentions[0] == conflict ) return view;
            BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
    };
    const auto conflict = await( true );
    BOOST_REQUIRE( !conflict.warnings.empty() );
    BOOST_CHECK( conflict.warnings[0].starts_with( "Bus contention:" ) );
    runner.ToggleSwitch( 1 );
    const auto resolved = await( false );
    BOOST_CHECK( resolved.contentionWarnings.empty() );
    BOOST_CHECK_GT( resolved.sample.latest.time, conflict.sample.latest.time );
}
