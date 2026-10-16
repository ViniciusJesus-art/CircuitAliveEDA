/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/session_runner.h>
#include <chrono>
#include <cmath>
#include <thread>

using namespace CIRCUITALIVE;
using namespace std::chrono_literals;

static std::vector<CIRCUIT_COMPONENT> Components()
{
    return {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "0" } } },
        { "SW1", "Switch:SW_SPST", "SW_SPST", { { "1", "supply" }, { "2", "charge" } } },
        { "R1", "Device:R", "330", { { "1", "charge" }, { "2", "cap" } } },
        { "C1", "Device:C", "100u", { { "1", "cap" }, { "2", "0" } } },
        { "R2", "Device:R", "10k", { { "1", "cap" }, { "2", "0" } } },
        { "R3", "Device:R", "330", { { "1", "cap" }, { "2", "anode" } } },
        { "D1", "Device:LED", "RED", { { "1", "0" }, { "2", "anode" } } }
    };
}

template<typename PREDICATE>
static EDITOR_SESSION_VIEW Await( SESSION_RUNNER& runner, PREDICATE predicate )
{
    auto deadline = std::chrono::steady_clock::now() + 5s;
    for( ;; )
    {
        auto view = runner.View();
        BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::ERROR, view.error );
        if( predicate( view ) )
            return view;
        BOOST_REQUIRE_MESSAGE( std::chrono::steady_clock::now() < deadline,
                               "Editor session did not acknowledge the requested state" );
        std::this_thread::sleep_for( 2ms );
    }
}

BOOST_AUTO_TEST_CASE( InteractiveValuesAndDisplayResponse )
{
    BOOST_CHECK_CLOSE( ParseComponentValue( "100 uF" ), 100e-6, 1e-9 );
    BOOST_CHECK_CLOSE( ParseComponentValue( "1kOhm" ), 1000, 1e-9 );
    BOOST_CHECK_CLOSE( ParseComponentValue( "2.2Meg" ), 2.2e6, 1e-9 );
    BOOST_CHECK_CLOSE( ParseComponentValue( "1M" ), 1e-3, 1e-9 );
    BOOST_CHECK_THROW( ParseComponentValue( "1\nVhack a 0 9" ), std::runtime_error );
    BOOST_CHECK_THROW( ParseComponentValue( "NaN" ), std::runtime_error );
    BOOST_CHECK_THROW( ParseComponentValue( "1e999" ), std::runtime_error );
    BOOST_CHECK_EQUAL( LedBrightness( -0.01 ), 0 );
    BOOST_CHECK_EQUAL( LedBrightness( 0 ), 0 );
    BOOST_CHECK_GT( LedBrightness( 0.005 ), LedBrightness( 0.001 ) );
    BOOST_CHECK_EQUAL( LedBrightness( 1 ), 1 );
}

BOOST_AUTO_TEST_CASE( InteractiveCatalogueRejectsIncompleteOrCustomCircuits )
{
    auto components = Components();
    components[6].libraryId = "Device:D";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Components();
    components[6].value = "LED";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Components();
    components[0].explicitSpiceModel = true;
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Components();
    components[2].value = "0";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Components();
    components[2].pins.erase( "1" );
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Components();
    components[1].reference = "V1";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    for( auto& c : components )
        for( auto& [pin, net] : c.pins )
            if( net == "0" ) net = "floating";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
}

BOOST_AUTO_TEST_CASE( SchematicSnapshotPreservesLedPolarityAndResistorEffect )
{
    auto components = Components();
    auto measure = []( const auto& c )
    {
        auto circuit = BuildCircuitSnapshot( c );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        session.AdvanceTo( 0.5 );
        return session.Snapshot().latest.values.at( 0 );
    };
    double forward = measure( components );
    BOOST_CHECK_GT( forward, 0.003 );
    BOOST_CHECK_LT( forward, 0.006 );
    components[5].value = "3.3k";
    BOOST_CHECK_LT( measure( components ), forward / 3 );
    components = Components();
    std::swap( components[6].pins["1"], components[6].pins["2"] );
    BOOST_CHECK_LE( measure( components ), 0 );
}

BOOST_AUTO_TEST_CASE( InteractiveCircuitWithoutLedStillHasAnObservableSignal )
{
    auto components = Components();
    components.resize( 5 );
    auto circuit = BuildCircuitSnapshot( components );
    BOOST_CHECK( circuit.leds.empty() );
    BOOST_REQUIRE_EQUAL( circuit.config.signals.size(), 1 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( 0.5 );
    BOOST_CHECK_GT( session.Snapshot().latest.values.at( 0 ), 4 );
}

BOOST_AUTO_TEST_CASE( EditorRunnerPauseResumeSwitchAndReplay )
{
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    auto circuit = BuildCircuitSnapshot( Components() );
    runner.Start( circuit );
    auto charged = Await( runner, []( const auto& v ) { return v.sample.latest.time > 0.2; } );
    BOOST_REQUIRE_EQUAL( charged.ledCurrents.size(), 1 );
    BOOST_CHECK_GT( charged.ledCurrents[0], 0.003 );
    runner.Pause();
    auto paused = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    std::this_thread::sleep_for( 40ms );
    BOOST_CHECK_EQUAL( runner.View().sample.latest.time, paused.sample.latest.time );
    runner.ToggleSwitch( 0 );
    auto opened = Await( runner, []( const auto& v ) { return !v.switches.at( 0 ); } );
    BOOST_CHECK_EQUAL( opened.sample.eventsApplied, 1 );
    BOOST_CHECK_EQUAL( opened.sample.latest.time, paused.sample.latest.time );
    runner.Resume();
    auto discharged = Await( runner, [&]( const auto& v )
                            { return v.sample.latest.time > paused.sample.latest.time + 0.2; } );
    BOOST_CHECK_LT( discharged.ledCurrents[0], charged.ledCurrents[0] / 5 );
    BOOST_CHECK_EQUAL( discharged.sample.generation, paused.sample.generation );
    for( int i = 0; i < 4; ++i ) runner.ToggleSwitch( 0 );
    auto ordered = Await( runner, []( const auto& v ) { return v.sample.eventsApplied == 5; } );
    BOOST_CHECK( !ordered.switches[0] );
    runner.Stop();
    BOOST_CHECK( runner.View().state == EDITOR_SESSION_STATE::STOPPED );
    runner.Start( circuit );
    auto restarted = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING; } );
    BOOST_CHECK( restarted.switches[0] );
    BOOST_CHECK_EQUAL( restarted.sample.eventsApplied, 0 );
    BOOST_CHECK_LT( restarted.sample.latest.time, paused.sample.latest.time );
}

BOOST_AUTO_TEST_CASE( EditorRunnerCancellationAndFiniteCompletion )
{
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    auto circuit = BuildCircuitSnapshot( Components() );
    runner.Start( circuit );
    runner.Stop();
    std::this_thread::sleep_for( 60ms );
    BOOST_CHECK( runner.View().state == EDITOR_SESSION_STATE::STOPPED );
    circuit.config.horizon = 0.03;
    runner.Start( circuit );
    auto completed = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::COMPLETED; } );
    BOOST_CHECK_CLOSE( completed.sample.latest.time, 0.03, 1e-7 );
    runner.Resume();
    std::this_thread::sleep_for( 40ms );
    BOOST_CHECK( runner.View().state == EDITOR_SESSION_STATE::COMPLETED );
}

BOOST_AUTO_TEST_CASE( EditorRunnerSurfacesWorkerFailure )
{
    SESSION_RUNNER runner( "/circuitalive/missing-worker" );
    runner.Start( BuildCircuitSnapshot( Components() ) );
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while( runner.View().state != EDITOR_SESSION_STATE::ERROR )
    {
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( 2ms );
    }
    BOOST_CHECK( !runner.View().error.empty() );
    runner.Stop();
    BOOST_CHECK( runner.View().state == EDITOR_SESSION_STATE::STOPPED );
}
