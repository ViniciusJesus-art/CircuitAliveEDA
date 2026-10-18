/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/session_runner.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

using namespace CIRCUITALIVE;
using namespace std::chrono_literals;

static std::vector<CIRCUIT_COMPONENT> Load()
{
    return {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "return" } } },
        { "R1", "Device:R", "1k", { { "1", "supply" }, { "2", "return" } } }
    };
}

static SIM_SAMPLE Simulate( const CIRCUIT_SNAPSHOT& circuit )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( .02 );
    return session.Snapshot().latest;
}

template<typename PREDICATE>
static EDITOR_SESSION_VIEW WaitFor( SESSION_RUNNER& runner, PREDICATE predicate )
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for( ;; )
    {
        const auto view = runner.View();
        BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
        if( predicate( view ) ) return view;
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( 5ms );
    }
}

BOOST_AUTO_TEST_CASE( ComponentMeasurementsUseSignedPassivePowerConvention )
{
    auto components = Load();
    for( auto& c : components ) c.measurements = { true, true, true };
    const auto circuit = BuildCircuitSnapshot( components );
    BOOST_REQUIRE_EQUAL( circuit.measurements.size(), 6 );
    const auto sample = Simulate( circuit );
    double totalPower = 0;
    for( const auto& channel : circuit.measurements )
    {
        const auto value = MeasurementValue( channel, sample );
        const bool supply = channel.name == "V1";
        if( channel.quantity == MEASURE_QUANTITY::VOLTAGE ) BOOST_CHECK_CLOSE( value, 5, 1e-5 );
        else if( channel.quantity == MEASURE_QUANTITY::CURRENT ) BOOST_CHECK_CLOSE( value, supply ? -.005 : .005, 1e-4 );
        else { BOOST_CHECK_CLOSE( value, supply ? -.025 : .025, 1e-4 ); totalPower += value; }
    }
    BOOST_CHECK_SMALL( totalPower, 1e-9 );
    components[1].measurements.positive = "n"; components[1].measurements.negative = "p";
    const auto reversed = BuildCircuitSnapshot( components ); const auto reversedSample = Simulate( reversed );
    for( const auto& channel : reversed.measurements )
        if( channel.name == "R1" )
        {
            const double value = MeasurementValue( channel, reversedSample );
            if( channel.quantity == MEASURE_QUANTITY::POWER ) BOOST_CHECK_GT( value, 0 );
            else BOOST_CHECK_LT( value, 0 );
        }
}

BOOST_AUTO_TEST_CASE( InlineCurrentAndPowerProbeMeasureTheSelectedBranch )
{
    auto components = Load(); components[1].pins["1"] = "measured_branch";
    components.push_back( { "R2", "Device:R", "2k", { { "1", "supply" }, { "2", "return" } } } );
    std::vector<CIRCUIT_PROBE> probes = {
        { PROBE_KIND::POWER, "Feed", "", { { "1", "supply" }, { "2", "measured_branch" } }, 1 },
        { PROBE_KIND::VOLTAGE, "", "LOAD", { { "1", "measured_branch" } }, 2 }
    };
    auto circuit = BuildCircuitSnapshot( components, probes ); auto sample = Simulate( circuit );
    BOOST_REQUIRE_EQUAL( circuit.measurements.size(), 2 );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[0], sample ), .025, 1e-4 );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[1], sample ), 5, 1e-5 );
    BOOST_CHECK_CLOSE( std::abs( sample.values.at( circuit.currentWarnings[0].signal ) ), .0075, 1e-4 );
    probes[0].kind = PROBE_KIND::CURRENT;
    circuit = BuildCircuitSnapshot( components, probes ); sample = Simulate( circuit );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[0], sample ), .005, 1e-4 );
    std::swap( probes[0].pins["1"], probes[0].pins["2"] );
    circuit = BuildCircuitSnapshot( components, probes ); sample = Simulate( circuit );
    BOOST_CHECK_CLOSE( MeasurementValue( circuit.measurements[0], sample ), -.005, 1e-4 );
}

BOOST_AUTO_TEST_CASE( ProbeNamesAndUnconnectedStateAreExplicit )
{
    BOOST_CHECK_EQUAL( AlphabeticProbeName( 1 ), "A" ); BOOST_CHECK_EQUAL( AlphabeticProbeName( 26 ), "Z" );
    BOOST_CHECK_EQUAL( AlphabeticProbeName( 27 ), "AA" ); BOOST_CHECK_EQUAL( AlphabeticProbeName( 52 ), "AZ" );
    const std::vector<CIRCUIT_PROBE> probes = {
        { PROBE_KIND::VOLTAGE, "Named", "LABEL", { { "1", "supply" } }, 1 },
        { PROBE_KIND::VOLTAGE, "", "LABEL", { { "1", "supply" } }, 2 },
        { PROBE_KIND::VOLTAGE, "", "", { { "1", "return" } }, 27 },
        { PROBE_KIND::VOLTAGE, "", "", { { "1", "" } }, 28 }
    };
    const auto circuit = BuildCircuitSnapshot( Load(), probes ); const auto sample = Simulate( circuit );
    BOOST_CHECK_EQUAL( circuit.measurements[0].name, "Named" );
    BOOST_CHECK_EQUAL( circuit.measurements[1].name, "LABEL" );
    BOOST_CHECK_EQUAL( circuit.measurements[2].name, "AA" );
    BOOST_CHECK_EQUAL( MeasurementValue( circuit.measurements[2], sample ), 0 );
    BOOST_CHECK( std::isnan( MeasurementValue( circuit.measurements[3], sample ) ) );
}

BOOST_AUTO_TEST_CASE( CurrentProbeRejectsBypassedTerminalsAndSupplyShortIsReported )
{
    CIRCUIT_PROBE probe{ PROBE_KIND::CURRENT, "Branch", "", { { "1", "supply" }, { "2", "supply" } }, 1 };
    BOOST_CHECK_THROW( BuildCircuitSnapshot( Load(), { probe } ), std::runtime_error );
    auto components = Load(); components[0].pins["1"] = "return";
    BOOST_CHECK_EXCEPTION( BuildCircuitSnapshot( components ), std::runtime_error,
        []( const std::exception& e ) { return std::string( e.what() ).find( "Short circuit" ) != std::string::npos; } );
}

BOOST_AUTO_TEST_CASE( ScopeHistoryIsBoundedOrderedAndFreezesDuringPause )
{
    auto components = Load(); components[1].pins["2"] = "capacitor";
    components.push_back( { "C1", "Device:C", "100u", { { "1", "capacitor" }, { "2", "return" } } } );
    const auto circuit = BuildCircuitSnapshot( components,
        { { PROBE_KIND::OSCILLOSCOPE, "RC", "", { { "1", "capacitor" } }, 1 } } );
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER ); runner.Start( circuit );
    auto view = WaitFor( runner, []( const auto& v ) { return v.sample.latest.time > 1.; } );
    BOOST_REQUIRE( view.scopeHistory );
    BOOST_REQUIRE( !view.scopeHistory->empty() );
    BOOST_CHECK_LE( view.scopeHistory->size() * circuit.config.signals.size(), 65536 );
    BOOST_CHECK_LE( view.scopeHistory->size(), 8192 );
    const auto points = view.scopeHistory->Copy();
    for( std::size_t i = 1; i < points.size(); ++i ) BOOST_CHECK_GT( points[i].time, points[i-1].time );
    BOOST_CHECK_GT( view.measurements.at( 0 ), 4.9 );
    runner.Pause(); view = WaitFor( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    const auto time = view.sample.latest.time, first = view.scopeHistory->front().time;
    std::this_thread::sleep_for( 60ms );
    BOOST_CHECK_EQUAL( runner.View().sample.latest.time, time );
    BOOST_CHECK_EQUAL( runner.View().scopeHistory->front().time, first );
    runner.Resume(); WaitFor( runner, [&]( const auto& v ) { return v.sample.latest.time > time; } );
    runner.Stop(); view = runner.View(); BOOST_CHECK( !view.scopeHistory ); BOOST_CHECK( view.measurements.empty() );
}

BOOST_AUTO_TEST_CASE( OvercurrentWarningIsLatchedUntilStop )
{
    std::vector<CIRCUIT_COMPONENT> components = {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "return" } } },
        { "SW1", "Switch:SW_SPST", "SW_SPST", { { "1", "supply" }, { "2", "return" } } }
    };
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER ); runner.Start( BuildCircuitSnapshot( components ) );
    auto view = WaitFor( runner, []( const auto& v ) { return !v.warnings.empty(); } );
    BOOST_CHECK( view.warnings[0].find( "V1" ) != std::string::npos );
    runner.ToggleSwitch( 0 ); view = WaitFor( runner, []( const auto& v ) { return !v.switches[0]; } );
    BOOST_CHECK( !view.warnings.empty() );
    runner.Stop(); BOOST_CHECK( runner.View().warnings.empty() );
    components[0].modelParameters["current_warning"] = "0";
    BOOST_CHECK( BuildCircuitSnapshot( components ).currentWarnings.empty() );
}
