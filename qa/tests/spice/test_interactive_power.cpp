/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/power_rails.h>
using namespace CIRCUITALIVE;
namespace
{
SIM_SAMPLE Run( const CIRCUIT_SNAPSHOT& circuit )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( .02 );
    return session.Snapshot().latest;
}
CIRCUIT_COMPONENT Load( const std::string& positive = "rail", const std::string& negative = "GNDREF" )
{
    CIRCUIT_COMPONENT c{ "R1", "Device:R", "1k", { { "1", positive }, { "2", negative } } };
    c.measurements = { true, true, true }; return c;
}
}
BOOST_AUTO_TEST_CASE( PowerSymbolNamesParseSignedAndNativeDecimalVoltages )
{
    for( const auto& [name, expected] : std::vector<std::pair<std::string, double>>{
        { "+5V", 5 }, { "+24V", 24 }, { "-12V", -12 }, { "+3V3", 3.3 },
        { "+1V8", 1.8 }, { "+2.5V", 2.5 }, { "-3V3", -3.3 }, { "+5VA", 5 },
        { "+5VD", 5 }, { "500mV", .5 }, { " +24 V ", 24 }, { "0V", 0 } } )
    {
        auto value = PowerSymbolVoltage( name ); BOOST_REQUIRE( value ); BOOST_CHECK_CLOSE( *value, expected, 1e-8 );
    }
    for( const auto* name : { "VCC", "VDD", "VSS", "GND", "GNDREF", "PWR_FLAG", "+BATT", "24kOhm" } )
        BOOST_CHECK( !PowerSymbolVoltage( name ) );
    BOOST_CHECK( IsSimulationGround( "GNDREF" ) ); BOOST_CHECK( IsSimulationGround( "GND" ) );
    BOOST_CHECK( !IsSimulationGround( "GNDA" ) ); // Separate named ground domains are not silently shorted.
}
BOOST_AUTO_TEST_CASE( PowerSymbolsSupplyLoadsAndOneSourceServesRepeatedMarkers )
{
    const std::vector<POWER_SYMBOL> symbols{ { "#PWR01", "+5V", "rail" }, { "#PWR02", "+5V", "rail" } };
    BOOST_REQUIRE_EQUAL( CollectPowerRails( symbols ).size(), 1 );
    const auto circuit = BuildCircuitSnapshot( { Load() }, {}, false, symbols );
    const auto sample = Run( circuit );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[0], sample ), 5, 1e-6 );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[1], sample ), .005, 1e-6 );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[2], sample ), .025, 1e-6 );
    BOOST_CHECK_EQUAL( circuit.pinNodes[0].at( "2" ), "0" );
    BOOST_REQUIRE_EQUAL( circuit.currentWarnings.size(), 1 );
    BOOST_CHECK_EQUAL( circuit.currentWarnings[0].name, "+5V" );
    BOOST_CHECK( circuit.floatingIslands.empty() );
}
BOOST_AUTO_TEST_CASE( BipolarPowerSymbolsProduceTheirVoltagesAgainstCommonGround )
{
    std::vector<CIRCUIT_PROBE> probes{ { PROBE_KIND::VOLTAGE, "Positive", "", { { "1", "positive" } } },
        { PROBE_KIND::VOLTAGE, "Negative", "", { { "1", "negative" } } } };
    const auto c = BuildCircuitSnapshot( { Load( "positive", "negative" ) }, probes, false,
        { { "#PWR1", "+12V", "positive" }, { "#PWR2", "-12V", "negative" } } );
    const auto sample = Run( c );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], sample ), 24, 1e-6 );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[3], sample ), 12, 1e-6 );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[4], sample ), -12, 1e-6 );
}
BOOST_AUTO_TEST_CASE( ExistingDcSourceIsReusedIncludingReversePolarityAndSeriesSources )
{
    CIRCUIT_COMPONENT source{ "V1", "Simulation_SPICE:VDC", "5", { { "1", "rail" }, { "2", "0" } } };
    std::vector<POWER_SYMBOL> symbols{ { "#PWR1", "+5V", "rail" } };
    auto c = BuildCircuitSnapshot( { source, Load() }, {}, false, symbols );
    BOOST_CHECK( c.netlist.find( "Vpower" ) == std::string::npos );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], Run( c ) ), 5, 1e-6 );
    source.value = "-5"; source.pins = { { "1", "0" }, { "2", "rail" } };
    c = BuildCircuitSnapshot( { source, Load() }, {}, false, symbols );
    BOOST_CHECK( c.netlist.find( "Vpower" ) == std::string::npos );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], Run( c ) ), 5, 1e-6 );
    source.value = "5"; source.pins = { { "1", "middle" }, { "2", "0" } };
    CIRCUIT_COMPONENT upper{ "V2", "Simulation_SPICE:VDC", "7", { { "1", "rail" }, { "2", "middle" } } };
    symbols[0].value = "+12V";
    c = BuildCircuitSnapshot( { source, upper, Load() }, {}, false, symbols );
    BOOST_CHECK( c.netlist.find( "Vpower" ) == std::string::npos );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], Run( c ) ), 12, 1e-6 );
}
BOOST_AUTO_TEST_CASE( ConflictingSupplyLabelsAndGroundShortsFailBeforeStartingTheSolver )
{
    BOOST_CHECK_THROW( CollectPowerRails( { { "#PWR1", "+5V", "rail" }, { "#PWR2", "+24V", "rail" } } ), std::runtime_error );
    BOOST_CHECK_THROW( BuildCircuitSnapshot( { Load() }, {}, false, { { "#PWR1", "+5V", "0" } } ), std::runtime_error );
    CIRCUIT_COMPONENT source{ "V1", "Simulation_SPICE:VDC", "12", { { "1", "rail" }, { "2", "0" } } };
    BOOST_CHECK_THROW( BuildCircuitSnapshot( { source, Load() }, {}, false, { { "#PWR1", "+5V", "rail" } } ), std::runtime_error );
}
BOOST_AUTO_TEST_CASE( SeparateNativeNetsStaySeparateAndCurrentProbesDoNotDuplicateSources )
{
    auto c = BuildCircuitSnapshot( { Load( "left" ) },
        { { PROBE_KIND::VOLTAGE, "Other scope", "", { { "1", "right" } } } }, false,
        { { "#PWR1", "+5V", "left" }, { "#PWR2", "+5V", "right" } } );
    BOOST_CHECK_NE( c.pinNodes[0].at( "1" ), c.pinNodes[1].at( "1" ) );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements.back(), Run( c ) ), 5, 1e-6 );
    c = BuildCircuitSnapshot( { Load( "right" ) },
        { { PROBE_KIND::CURRENT, "Supply current", "", { { "1", "left" }, { "2", "right" } } } }, false,
        { { "#PWR1", "+5V", "left" }, { "#PWR2", "+5VA", "right" } } );
    BOOST_CHECK_EQUAL( c.currentWarnings.size(), 1 );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements.back(), Run( c ) ), .005, 1e-6 );
}
BOOST_AUTO_TEST_CASE( NumericPowerSymbolsDriveDigitalSupplyAndInputs )
{
    CIRCUIT_COMPONENT gate{ "U1", "74xx:74HC00", "74HC00", {} };
    for( int pin = 1; pin <= 14; ++pin ) gate.pins[std::to_string( pin )] = "";
    gate.pins["14"] = gate.pins["1"] = gate.pins["2"] = "supply"; gate.pins["7"] = "GNDREF";
    const auto c = BuildCircuitSnapshot( { gate }, {}, true, { { "#PWR1", "+5V", "supply" } } );
    const auto sample = Run( c );
    for( const auto& pin : c.logicPins )
    {
        if( pin.pin == "1" ) BOOST_CHECK( PinLogicLevel( pin, sample ) == LOGIC_LEVEL::HIGH );
        if( pin.pin == "3" ) BOOST_CHECK( PinLogicLevel( pin, sample ) == LOGIC_LEVEL::LOW );
    }
}

BOOST_AUTO_TEST_CASE( PowerSymbolsSupplyADirectCapacitorWithoutAnExtraVdc )
{
    CIRCUIT_COMPONENT capacitor{ "C1", "Device:C_Small_US", "100n", { { "1", "rail" }, { "2", "GNDREF" } } };
    capacitor.measurements = { true, true, true };
    const auto c = BuildCircuitSnapshot( { capacitor }, {}, false, { { "#PWR1", "+12V", "rail" } } );
    const auto sample = Run( c );
    BOOST_CHECK_CLOSE( MeasurementValue( c.measurements[0], sample ), 12, 1e-6 );
    BOOST_CHECK_SMALL( MeasurementValue( c.measurements[1], sample ), 1e-8 );
}
