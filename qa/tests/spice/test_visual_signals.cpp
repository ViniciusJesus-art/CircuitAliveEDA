/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/visual_signals.h>
#include <chrono>
#include <thread>

using namespace CIRCUITALIVE;

namespace
{
CIRCUIT_SNAPSHOT PulseCircuit( bool fast )
{
    CIRCUIT_SNAPSHOT circuit;
    circuit.netlist = "Visual pulse test\nVdrive out 0 PULSE(0 5 0 1u 1u "
            + std::string( fast ? "200u 1m" : "250m 500m" )
            + ")\nR1 out load 330\nVsense load anode 0\nD1 anode 0 DLED\n"
              ".model DLED D(IS=1e-18 N=2)\n.end\n";
    circuit.config.signals = { "i(Vsense)", "v(out)" };
    circuit.config.horizon = 2;
    circuit.config.step = circuit.config.maxStep = fast ? 1e-5 : 1e-3;
    circuit.config.historyCapacity = 1; // Deliberately cannot retain a whole cycle.
    circuit.leds.push_back( { 0, 0, .01, .02, "D1" } );
    LOGIC_PIN pin;
    pin.voltage = 1;
    circuit.logicPins.push_back( pin );
    ConfigureVisualSignals( circuit );
    return circuit;
}

template<typename Predicate>
EDITOR_SESSION_VIEW AwaitVisual( SESSION_RUNNER& runner, Predicate predicate )
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    for( ;; )
    {
        const auto view = runner.View();
        BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::ERROR, view.error );
        if( predicate( view ) ) return view;
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
}
}

BOOST_AUTO_TEST_CASE( VisualActivityRequiresRepeatedCyclesAndExpires )
{
    SIGNAL_ACTIVITY_TRACKER tracker;
    const SIM_ACTIVITY_MONITOR monitor{ 0, ACTIVITY_NO_SIGNAL, ACTIVITY_NO_SIGNAL, .3, .7 };
    tracker.Observe( monitor, 0, { 0 } );
    tracker.Observe( monitor, .001, { 1 } );
    tracker.Observe( monitor, .0011, { .5 } ); // Intermediate edge points are not extra edges.
    tracker.Observe( monitor, .0012, { 0 } );
    BOOST_CHECK( !tracker.Activity().RapidAt( .0012, 1 ) ); // Single short pulse.
    tracker.Observe( monitor, .002, { 1 } );
    tracker.Observe( monitor, .0022, { 0 } );
    const auto activity = tracker.Activity();
    BOOST_CHECK_CLOSE( activity.period, .001, 1e-8 );
    BOOST_CHECK( activity.RapidAt( .0029, 1 ) );
    BOOST_CHECK( !activity.RapidAt( .005, 1 ) ); // Stuck low must stop looking active.
    BOOST_CHECK( !activity.RapidAt( .0022, .001 ) ); // 1 Hz in slow motion.
    BOOST_CHECK( !activity.RapidAt( .0022, .05 ) ); // 50 Hz must still follow transitions.
    BOOST_CHECK( activity.RapidAt( .0022, .06 ) ); // 60 Hz presentation cutoff.
}

BOOST_AUTO_TEST_CASE( VisualActivityTracksSupplyRelativeThresholdsAndResetsOnPowerLoss )
{
    SIGNAL_ACTIVITY_TRACKER tracker;
    const SIM_ACTIVITY_MONITOR monitor{ 0, 1, 2, .3, .7, 1, 2, 6 };
    tracker.Observe( monitor, 0, { 2, 2, 7 } );
    for( unsigned edge = 1; edge <= 8; ++edge )
        tracker.Observe( monitor, edge * .001, { edge % 2 ? 7.0 : 2.0, 2, 7 } );
    BOOST_CHECK_CLOSE( tracker.Activity().period, .002, 1e-8 );
    BOOST_CHECK( tracker.Activity().RapidAt( .008, 1 ) );
    tracker.Observe( monitor, .009, { 2, 2, 2 } );
    BOOST_CHECK( !tracker.Activity().RapidAt( .009, 1 ) );
}

BOOST_AUTO_TEST_CASE( VisualLedUsesInstantaneousCurrentWithoutAveraging )
{
    const auto circuit = PulseCircuit( false );
    EDITOR_SESSION_VIEW view;
    view.state = EDITOR_SESSION_STATE::RUNNING;
    view.sample.latest = { .1, { .008, 5 } };
    view.sample.history = { { .09, { 0, 0 } } };
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK_EQUAL( view.ledCurrents.at( 0 ), .008 );
    BOOST_CHECK( view.logicLevels.at( 0 ) == LOGIC_LEVEL::HIGH );
    view.sample.latest = { .2, { 0, 0 } };
    view.sample.history = { { .19, { .008, 5 } } };
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK_EQUAL( view.ledCurrents.at( 0 ), 0 );
    BOOST_CHECK( view.logicLevels.at( 0 ) == LOGIC_LEVEL::LOW );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
}

BOOST_AUTO_TEST_CASE( VisualFastPulsesSurviveHistoryOverflowAndPauseAtActualState )
{
    auto circuit = PulseCircuit( true );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( .0198 ); // End in the off phase of a 1 kHz, 20% duty waveform.
    EDITOR_SESSION_VIEW view;
    view.state = EDITOR_SESSION_STATE::RUNNING;
    view.sample = session.Snapshot( true );
    BOOST_REQUIRE_EQUAL( view.sample.activity.size(), 2u );
    BOOST_REQUIRE_EQUAL( view.sample.history.size(), 1u );
    BOOST_CHECK_GT( view.sample.samplesOverwritten, 1000u );
    BOOST_CHECK_LT( view.sample.latest.values[0], 1e-6 );
    BOOST_CHECK_CLOSE( view.sample.activity[0].period, .001, 2 );
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK_GT( view.ledCurrents.at( 0 ), .005 );
    BOOST_CHECK( view.logicRapid.at( 0 ) );
    BOOST_CHECK( view.logicLevels.at( 0 ) == LOGIC_LEVEL::LOW );
    // Draining history must not discard frequency tracking across snapshots.
    BOOST_CHECK_EQUAL( session.Snapshot().activity[0].period, view.sample.activity[0].period );
    UpdateVisualSignals( circuit, view, .001 );
    BOOST_CHECK_LT( view.ledCurrents.at( 0 ), 1e-6 );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
    view.state = EDITOR_SESSION_STATE::PAUSED;
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK_LT( view.ledCurrents.at( 0 ), 1e-6 );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
    view.state = EDITOR_SESSION_STATE::RUNNING;
    view.sample.latest.values[1] = 2.5;
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK( view.logicLevels.at( 0 ) == LOGIC_LEVEL::UNKNOWN );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
    circuit.logicPins[0].digital = circuit.logicPins[0].output = true;
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK( view.logicLevels.at( 0 ) == LOGIC_LEVEL::HIGH_Z );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
    session.Stop();
    BOOST_CHECK_EQUAL( session.Snapshot().activity[0].period, 0 );
}

BOOST_AUTO_TEST_CASE( VisualSlowSquareWaveFollowsBothEdges )
{
    const auto circuit = PulseCircuit( false );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    EDITOR_SESSION_VIEW view;
    view.state = EDITOR_SESSION_STATE::RUNNING;
    for( const double time : { .1, .35, .6, .85, 1.1, 1.35 } )
    {
        session.AdvanceTo( time );
        view.sample = session.Snapshot( true );
        UpdateVisualSignals( circuit, view, 1 );
        BOOST_CHECK_EQUAL( view.ledCurrents.at( 0 ), view.sample.latest.values[0] );
        BOOST_CHECK( !view.logicRapid.at( 0 ) );
        const bool high = std::fmod( time, .5 ) < .25;
        BOOST_CHECK( view.logicLevels.at( 0 ) == ( high ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::LOW ) );
    }
}

BOOST_AUTO_TEST_CASE( VisualRunnerUpdatesRapidFlagsWhenPausingAndSlowingPlayback )
{
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    runner.Start( PulseCircuit( true ) );
    AwaitVisual( runner, []( const auto& v ) { return !v.logicRapid.empty() && v.logicRapid[0]; } );
    runner.Pause();
    auto view = AwaitVisual( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    BOOST_CHECK( !view.logicRapid[0] );
    BOOST_CHECK_EQUAL( view.ledCurrents[0], view.sample.latest.values[0] );
    runner.SetSpeed( .001 );
    runner.Resume();
    view = AwaitVisual( runner, []( const auto& v )
        { return v.state == EDITOR_SESSION_STATE::RUNNING && v.speed == .001; } );
    BOOST_CHECK( !view.logicRapid[0] );
    BOOST_CHECK_EQUAL( view.ledCurrents[0], view.sample.latest.values[0] );
}

BOOST_AUTO_TEST_CASE( VisualActivityRejectsInvalidSignalIndices )
{
    auto circuit = PulseCircuit( true );
    circuit.config.activityMonitors[0].signal = circuit.config.signals.size();
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    BOOST_CHECK_THROW( session.Configure( circuit.netlist, circuit.config ), std::runtime_error );
}

BOOST_AUTO_TEST_CASE( VisualConstantGroundPinIsNotAnOscillator )
{
    auto circuit = PulseCircuit( true );
    LOGIC_PIN ground;
    ground.voltage = NO_SIGNAL; // Ground is a constant, not a saved ngspice vector.
    circuit.logicPins.push_back( ground );
    ConfigureVisualSignals( circuit );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( .0198 );
    EDITOR_SESSION_VIEW view;
    view.state = EDITOR_SESSION_STATE::RUNNING;
    view.sample = session.Snapshot( true );
    UpdateVisualSignals( circuit, view, 1 );
    BOOST_CHECK( view.logicRapid.at( 0 ) );
    BOOST_CHECK( !view.logicRapid.at( 1 ) );
    BOOST_CHECK( view.logicLevels.at( 1 ) == LOGIC_LEVEL::LOW );
}

BOOST_AUTO_TEST_CASE( VisualPreferencesKeepLedAndDigitalCutoffsIndependent )
{
    const auto circuit = PulseCircuit( false );
    EDITOR_SESSION_VIEW view;
    view.state = EDITOR_SESSION_STATE::RUNNING;
    view.sample.latest = { .1, { 0, 0 } };
    view.sample.activity = { { .02, .099, .009 }, { .02, .099, 5 } }; // 50 Hz, currently low.
    SIMULATION_PREFERENCES preferences;
    preferences.ledCutoffHz = 40;
    preferences.digitalCutoffHz = 80;
    UpdateVisualSignals( circuit, view, 1, preferences );
    BOOST_CHECK_EQUAL( view.ledCurrents.at( 0 ), .009 );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
    preferences.ledCutoffHz = 80;
    preferences.digitalCutoffHz = 40;
    UpdateVisualSignals( circuit, view, 1, preferences );
    BOOST_CHECK_EQUAL( view.ledCurrents.at( 0 ), 0 );
    BOOST_CHECK( view.logicRapid.at( 0 ) );
    preferences.ledCutoffHz = 40;
    preferences.holdFastLeds = preferences.markFastDigital = false;
    UpdateVisualSignals( circuit, view, 1, preferences );
    BOOST_CHECK_EQUAL( view.ledCurrents.at( 0 ), 0 );
    BOOST_CHECK( !view.logicRapid.at( 0 ) );
}

BOOST_AUTO_TEST_CASE( VisualRunnerAppliesPreferencesWithoutRestartingOrOverwritingManualSpeed )
{
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    SIMULATION_PREFERENCES preferences;
    preferences.playbackSpeed = .05; // 1 kHz appears at 50 Hz, below the default cutoff.
    runner.SetPreferences( preferences );
    runner.Start( PulseCircuit( true ) ); // Start supersedes queued commands; preferences must survive.
    auto first = AwaitVisual( runner, []( const auto& v ) { return v.sample.latest.time > .003; } );
    BOOST_CHECK_EQUAL( first.speed, .05 );
    BOOST_CHECK( !first.logicRapid.at( 0 ) );
    BOOST_CHECK_EQUAL( first.ledCurrents.at( 0 ), first.sample.latest.values[0] );
    preferences.digitalCutoffHz = preferences.ledCutoffHz = 20;
    runner.SetPreferences( preferences );
    auto changed = AwaitVisual( runner, []( const auto& v )
        { return v.preferences.digitalCutoffHz == 20 && v.logicRapid.at( 0 ); } );
    BOOST_CHECK_EQUAL( changed.sample.generation, first.sample.generation );
    BOOST_CHECK_GE( changed.sample.latest.time, first.sample.latest.time );
    runner.SetSpeed( .025 );
    preferences.warnSupply = false;
    preferences.stepMs = 12;
    runner.SetPreferences( preferences );
    auto manual = AwaitVisual( runner, []( const auto& v ) { return !v.preferences.warnSupply; } );
    BOOST_CHECK_EQUAL( manual.speed, .025 );
    runner.Pause();
    const auto paused = AwaitVisual( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    runner.Step();
    const auto stepped = AwaitVisual( runner, [&]( const auto& v ) { return v.sample.latest.time > paused.sample.latest.time; } );
    const double expected = preferences.stepMs * .001 * manual.speed;
    BOOST_CHECK_GE( stepped.sample.latest.time - paused.sample.latest.time, expected * .999 );
    BOOST_CHECK_LE( stepped.sample.latest.time - paused.sample.latest.time, expected + 2e-5 );
    BOOST_CHECK( stepped.state == EDITOR_SESSION_STATE::PAUSED );
}

BOOST_AUTO_TEST_CASE( VisualPreferencesNormalizeInvalidValuesBeforeUse )
{
    SIMULATION_PREFERENCES preferences;
    preferences.ledCutoffHz = std::numeric_limits<double>::quiet_NaN();
    preferences.digitalCutoffHz = -1;
    preferences.refreshRateHz = 0;
    preferences.playbackSpeed = std::numeric_limits<double>::infinity();
    preferences.stepMs = -100;
    preferences.maxTimeStepUs = 0;
    preferences.durationSeconds = 0;
    preferences.workerTimeoutSeconds = -1;
    preferences.scopeHistorySamples = 0;
    preferences.Normalize();
    BOOST_CHECK_EQUAL( preferences.ledCutoffHz, 60 );
    BOOST_CHECK_EQUAL( preferences.digitalCutoffHz, 1 );
    BOOST_CHECK_GT( preferences.FrameInterval().count(), 0 );
    BOOST_CHECK_EQUAL( preferences.playbackSpeed, 1 );
    BOOST_CHECK_GT( preferences.stepMs, 0 );
    BOOST_CHECK_GT( preferences.maxTimeStepUs, 0 );
    BOOST_CHECK_GT( preferences.durationSeconds, 0 );
    BOOST_CHECK_GT( preferences.workerTimeoutSeconds, 0 );
    BOOST_CHECK_GE( preferences.scopeHistorySamples, 256 );
}
