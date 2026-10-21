/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <cmath>
using namespace CIRCUITALIVE;

static SIM_SAMPLE RunDevice( const CIRCUIT_SNAPSHOT& c, double time = .02 )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config ); session.AdvanceTo( time ); return session.Snapshot().latest;
}
static const LOGIC_PIN& Pin( const CIRCUIT_SNAPSHOT& c, const std::string& ref, const std::string& number )
{
    for( const auto& p : c.logicPins ) if( p.reference == ref && p.pin == number ) return p;
    throw std::runtime_error( "Missing test pin" );
}
static CIRCUIT_COMPONENT HC00()
{
    CIRCUIT_COMPONENT c{ "U1", "74xx:74HC00", "74HC00", {} };
    for( int p = 1; p <= 14; ++p ) c.pins[std::to_string( p )] = "";
    c.pins["14"] = "vcc"; c.pins["7"] = "gnd"; return c;
}
static std::vector<CIRCUIT_COMPONENT> LogicCircuit()
{
    auto gate = HC00();
    const char* numbers[] = { "1", "2", "4", "5", "9", "10", "12", "13" };
    for( const auto* n : numbers ) gate.pins[n] = "vcc";
    gate.pins["3"] = "out";
    return { { "V1", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "gnd" } } }, gate };
}
BOOST_AUTO_TEST_CASE( AcSourceProducesConfiguredSineAndPhase )
{
    std::vector<CIRCUIT_COMPONENT> parts = {
        { "V1", "Simulation_SPICE:VSIN", "VSIN", { { "1", "out" }, { "2", "0" } } },
        { "R1", "Device:R", "1k", { { "1", "out" }, { "2", "0" } } } };
    parts[1].measurements.voltage = true;
    auto c = BuildCircuitSnapshot( parts );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], RunDevice( c, .005 ) ), 5, .01 );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], RunDevice( c, .015 ) ), -5, .01 );
    parts[0].modelParameters = { { "amplitude", "2V" }, { "offset", "1V" }, { "frequency", "1kHz" }, { "phase", "90deg" } };
    c = BuildCircuitSnapshot( parts );
    BOOST_CHECK_CLOSE( c.config.maxStep, 1. / ( 1000 * 24 ), 1e-8 );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], RunDevice( c, .001 ) ), 3, .01 );
}
BOOST_AUTO_TEST_CASE( CommonDiodesRectifyWithoutLedBehavior )
{
    for( const auto* device : { "Device:D", "Diode:1N4148", "Diode:1N4001", "Diode:1N4007", "Diode:1N5819" } )
    {
        std::vector<CIRCUIT_COMPONENT> parts = {
            { "V1", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "0" } } },
            { "R1", "Device:R", "1k", { { "1", "vcc" }, { "2", "anode" } } },
            { "D1", device, "D", { { "1", "0" }, { "2", "anode" } } } };
        parts[2].measurements = { true, true, true };
        auto c = BuildCircuitSnapshot( parts ); auto sample = RunDevice( c );
        BOOST_CHECK( c.leds.empty() );
        BOOST_CHECK_GT( MeasurementValue( c.measurements[0], sample ), .1 );
        BOOST_CHECK_LT( MeasurementValue( c.measurements[0], sample ), 1 );
        BOOST_CHECK_GT( MeasurementValue( c.measurements[1], sample ), .004 );
        parts[0].value = "-5"; c = BuildCircuitSnapshot( parts ); sample = RunDevice( c );
        BOOST_CHECK_SMALL( MeasurementValue( c.measurements[1], sample ), 1e-5 );
    }
}
BOOST_AUTO_TEST_CASE( BasicNpnAndPnpTransistorsSwitchCollectorLoad )
{
    for( const auto* device : { "BC337", "BC547", "BC548", "BC327", "BC557", "BC558" } )
    {
        const bool pnp = std::string( device ) == "BC327" || std::string( device ).starts_with( "BC55" );
        std::vector<CIRCUIT_COMPONENT> parts = {
            { "V1", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "0" } } },
            { "R1", "Device:R", "1k", { { "1", pnp ? "collector" : "vcc" }, { "2", pnp ? "0" : "collector" } } },
            { "R2", "Device:R", "10k", { { "1", pnp ? "0" : "vcc" }, { "2", "base" } } },
            { "Q1", std::string( "Transistor_BJT:" ) + device, device,
                { { "1", "collector" }, { "2", "base" }, { "3", pnp ? "vcc" : "0" } } } };
        parts[1].measurements.current = true;
        auto c = BuildCircuitSnapshot( parts );
        BOOST_CHECK_GT( std::abs( MeasurementValue( c.measurements[0], RunDevice( c ) ) ), .004 );
        parts[2].pins["1"] = pnp ? "vcc" : "0";
        c = BuildCircuitSnapshot( parts );
        BOOST_CHECK_SMALL( MeasurementValue( c.measurements[0], RunDevice( c ) ), 1e-6 );
    }
}
BOOST_AUTO_TEST_CASE( InductorRetainsTransientState )
{
    std::vector<CIRCUIT_COMPONENT> parts = {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "0" } } },
        { "R1", "Device:R", "1k", { { "1", "vcc" }, { "2", "coil" } } },
        { "L1", "Device:L", "1H", { { "1", "coil" }, { "2", "0" } } } };
    parts[2].measurements.current = true;
    auto c = BuildCircuitSnapshot( parts );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], RunDevice( c, .001 ) ), .005 * ( 1 - std::exp( -1. ) ), 1 );
}
BOOST_AUTO_TEST_CASE( Hc00AllFourGatesFollowNandTruthTableWithShiftedGround )
{
    auto parts = LogicCircuit();
    parts.insert( parts.begin(), { "V0", "Simulation_SPICE:VDC", "2", { { "1", "gnd" }, { "2", "0" } } } );
    auto& gate = parts.back();
    gate.pins["1"] = gate.pins["2"] = "gnd";
    gate.pins["4"] = "gnd"; gate.pins["5"] = "vcc";
    gate.pins["9"] = "vcc"; gate.pins["10"] = "gnd";
    auto c = BuildCircuitSnapshot( parts, {}, true ); const auto sample = RunDevice( c );
    for( const auto* p : { "3", "6", "8" } ) BOOST_CHECK( PinLogicLevel( Pin( c, "U1", p ), sample ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "11" ), sample ) == LOGIC_LEVEL::LOW );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "14" ), sample ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "7" ), sample ) == LOGIC_LEVEL::LOW );
}
BOOST_AUTO_TEST_CASE( FloatingDigitalInputIsZAndIndeterminateVoltageIsX )
{
    auto parts = LogicCircuit(); auto& gate = parts.back();
    gate.pins["1"] = gate.pins["4"] = "undriven";
    gate.pins["5"] = "gnd";
    auto c = BuildCircuitSnapshot( parts, {}, true ); auto sample = RunDevice( c );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "1" ), sample ) == LOGIC_LEVEL::HIGH_Z );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "3" ), sample ) == LOGIC_LEVEL::UNKNOWN );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "6" ), sample ) == LOGIC_LEVEL::HIGH );
    parts.push_back( { "V2", "Simulation_SPICE:VDC", "2.5", { { "1", "undriven" }, { "2", "gnd" } } } );
    c = BuildCircuitSnapshot( parts, {}, true ); sample = RunDevice( c );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "1" ), sample ) == LOGIC_LEVEL::UNKNOWN );
}
BOOST_AUTO_TEST_CASE( UnpoweredOutputIsZEvenWithExternalPullup )
{
    auto parts = LogicCircuit(); parts[0].value = "0";
    parts.push_back( { "V2", "Simulation_SPICE:VDC", "5", { { "1", "pullup" }, { "2", "gnd" } } } );
    parts.push_back( { "R1", "Device:R", "1k", { { "1", "pullup" }, { "2", "out" } } } );
    auto c = BuildCircuitSnapshot( parts, {}, true ); const auto sample = RunDevice( c );
    const auto& output = Pin( c, "U1", "3" );
    BOOST_CHECK_GT( sample.values.at( output.voltage ), 4.9 );
    BOOST_CHECK( PinLogicLevel( output, sample ) == LOGIC_LEVEL::HIGH_Z );
}
BOOST_AUTO_TEST_CASE( Hc00PropagationDelayRunsInsideSolverAtNanosecondResolution )
{
    auto parts = LogicCircuit(); parts[1].pins["1"] = "input";
    parts[1].modelParameters["fall_delay"] = "20n";
    parts.push_back( { "V2", "Simulation_SPICE:VPULSE", "VPULSE", { { "1", "input" }, { "2", "gnd" } } } );
    parts.back().modelParameters = { { "delay", "1u" }, { "rise", "1p" }, { "fall", "1p" } };
    auto c = BuildCircuitSnapshot( parts, {}, true );
    c.config.step = c.config.maxStep = 1e-9; c.config.horizon = .001;
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
    session.AdvanceTo( 1.005e-6 );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "1" ), session.Snapshot().latest ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "3" ), session.Snapshot().latest ) == LOGIC_LEVEL::HIGH );
    session.AdvanceTo( 1.03e-6 );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "3" ), session.Snapshot().latest ) == LOGIC_LEVEL::LOW );
}

BOOST_AUTO_TEST_CASE( Hc125DisabledOutputIsZWhilePulledUpInputReadsOne )
{
    auto parts = LogicCircuit();
    parts[1].libraryId = "74xx:74HC125"; parts[1].value = "74HC125";
    parts[1].pins["1"] = "vcc"; // /OE high disables the first buffer.
    parts[1].pins["2"] = "gnd";
    auto receiver = HC00(); receiver.reference = "U2";
    receiver.pins["1"] = "out"; receiver.pins["2"] = "vcc";
    parts.push_back( receiver );
    parts.push_back( { "R1", "Device:R", "10k", { { "1", "vcc" }, { "2", "out" } } } );
    auto c = BuildCircuitSnapshot( parts, {}, true ); auto sample = RunDevice( c );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "3" ), sample ) == LOGIC_LEVEL::HIGH_Z );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U2", "1" ), sample ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U2", "3" ), sample ) == LOGIC_LEVEL::LOW );
    parts[1].pins["1"] = "gnd";
    c = BuildCircuitSnapshot( parts, {}, true ); sample = RunDevice( c );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U1", "3" ), sample ) == LOGIC_LEVEL::LOW );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U2", "1" ), sample ) == LOGIC_LEVEL::LOW );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U2", "3" ), sample ) == LOGIC_LEVEL::HIGH );
}

BOOST_AUTO_TEST_CASE( LogicOutputLoadDrawsCurrentFromComponentVccPin )
{
    auto parts = LogicCircuit(); parts[1].pins["1"] = "gnd";
    parts[1].measurements.current = true;
    parts.push_back( { "R1", "Device:R", "1k", { { "1", "out" }, { "2", "gnd" } } } );
    const auto c = BuildCircuitSnapshot( parts ); const auto sample = RunDevice( c );
    BOOST_REQUIRE_EQUAL( c.measurements.size(), 1 );
    const double current = MeasurementValue( c.measurements[0], sample );
    BOOST_CHECK_GT( current, .0048 ); BOOST_CHECK_LT( current, .005 );
}

BOOST_AUTO_TEST_CASE( InlineAmmeterPreservesDigitalDriverPresence )
{
    auto parts = LogicCircuit(); parts[1].pins["1"] = "gnd";
    auto receiver = HC00(); receiver.reference = "U2";
    receiver.pins["1"] = "after_probe"; receiver.pins["2"] = "vcc"; parts.push_back( receiver );
    const auto c = BuildCircuitSnapshot( parts,
        { { PROBE_KIND::CURRENT, "Branch", "", { { "1", "out" }, { "2", "after_probe" } }, 1 } }, true );
    const auto sample = RunDevice( c );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U2", "1" ), sample ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( PinLogicLevel( Pin( c, "U2", "3" ), sample ) == LOGIC_LEVEL::LOW );
}
