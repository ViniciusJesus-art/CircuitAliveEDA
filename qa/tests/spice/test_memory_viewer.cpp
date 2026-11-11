/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/memory_view_state.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/source_probe.h>
#include <circuitalive/simulation_model.h>
#include <algorithm>
#include <chrono>
#include <thread>
using namespace CIRCUITALIVE;
namespace
{
auto Snapshot( MEMORY_IMAGE image )
{
    return std::make_shared<MEMORY_SNAPSHOT>( MEMORY_SNAPSHOT{ "ram", 1, std::move( image ) } );
}
template<typename Predicate> EDITOR_SESSION_VIEW Await( SESSION_RUNNER& runner, Predicate predicate )
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    for( ;; )
    {
        const auto view = runner.View();
        BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
        if( predicate( view ) ) return view;
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
}
CIRCUIT_SNAPSHOT RamCircuit()
{
    CIRCUIT_COMPONENT ram{ "U1", "Memory_RAM:CY62256-70PC", "CY62256-70PC", {} };
    for( const auto& pin : MODEL_REPOSITORY().SelectModel( ram ).pins )
        ram.pins[pin.defaultPin] = pin.id == "vcc" ? "vcc" : pin.id == "we" ? "write"
            : pin.id == "oe" ? "vcc" : pin.role == "bidirectional" ? "data" : "0";
    auto other = ram; other.reference = "U2";
    SOURCE_PROBE_SETTINGS data;
    SOURCE_PROBE_SETTINGS write; write.initialHigh = true;
    return BuildCircuitSnapshot( { { "V1", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "0" } } },
        SourceProbeComponent( PROBE_KIND::LOGIC, data, "#P1", "data" ),
        SourceProbeComponent( PROBE_KIND::LOGIC, write, "#P2", "write" ), ram, other } );
}
}

BOOST_AUTO_TEST_CASE( MemoryViewerHighlightsValuesAndKnownMasksBetweenPauses )
{
    MEMORY_PAUSE_COMPARISON comparison;
    auto data = EmptyMemory( 32, 0 ); data.known[3] = 0;
    comparison.Accept( Snapshot( data ), 1, 1 );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 0 );
    data.data[0] = 0xa5;
    data.known[1] = 0; // Known to unknown also matters.
    data.known[3] = 255; // Unknown to known, even though the byte is still 00.
    comparison.Accept( Snapshot( data ), 1, 2 );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 3 );
    for( const auto i : { 0, 1, 3 } ) BOOST_CHECK( comparison.Changed()[i] );
    data.data[1] = 255; // Payload bits covered by X do not represent a changed value.
    comparison.Accept( Snapshot( data ), 1, 3 );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 0 );
}

BOOST_AUTO_TEST_CASE( MemoryViewerManualCaptureKeepsPreviousPauseAndRestartClearsComparison )
{
    MEMORY_PAUSE_COMPARISON comparison;
    auto data = EmptyMemory( 16, 0 );
    comparison.Accept( Snapshot( data ), 1, 1 );
    data.data[0] = 1;
    comparison.Accept( Snapshot( data ), 1, 2 );
    comparison.Accept( Snapshot( data ), 1, 2 );
    BOOST_CHECK( comparison.Changed()[0] );
    data.data[1] = 2;
    comparison.Accept( Snapshot( data ), 1, 2 );
    BOOST_CHECK( comparison.Changed()[0] && comparison.Changed()[1] );
    comparison.Accept( Snapshot( data ), 1, 3 );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 0 );
    data.data[7] = 0xff;
    comparison.Accept( Snapshot( data ), 2, 1 );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 0 );
}

BOOST_AUTO_TEST_CASE( MemoryViewerFirstPauseComparesAgainstTheCurrentSessionInitialImage )
{
    MEMORY_PAUSE_COMPARISON comparison;
    auto initial = EmptyMemory( 16, 0xa5 );
    initial.known[3] = 0;
    auto written = initial;
    written.data[1] = 0x37;
    written.known[3] = 255;
    comparison.Accept( Snapshot( written ), 1, 1, Snapshot( initial ) );
    BOOST_CHECK( comparison.Changed()[1] && comparison.Changed()[3] );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 2 );
    comparison.Accept( Snapshot( written ), 1, 1, Snapshot( written ) );
    BOOST_CHECK( comparison.Changed()[1] && comparison.Changed()[3] );
    comparison.Accept( Snapshot( written ), 1, 2, Snapshot( initial ) );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 0 );
    // A new Play may load a different image; its first pause uses that image.
    auto replacement = EmptyMemory( 16, 0x55 );
    comparison.Accept( Snapshot( replacement ), 2, 1, Snapshot( replacement ) );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 0 );
    replacement.data[7] = 0xff;
    comparison.Accept( Snapshot( replacement ), 3, 1, Snapshot( EmptyMemory( 16, 0x55 ) ) );
    BOOST_CHECK( comparison.Changed()[7] );
}

BOOST_AUTO_TEST_CASE( MemoryViewerCaptureNeverPausesAndMultipleWindowsKeepTheirOwnResults )
{
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    runner.Start( RamCircuit() );
    auto view = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING; } );
    BOOST_REQUIRE_EQUAL( view.memoryRegions.size(), 2u );
    BOOST_CHECK_EQUAL( view.memoryRegions[0].reference, "U1" );
    const auto id0 = view.memoryRegions[0].id, id1 = view.memoryRegions[1].id;
    const auto live = runner.CaptureMemory( 0 );
    view = Await( runner, [&]( const auto& v ) { return v.memoryCaptures.contains( id0 ) && v.memoryCaptures.at( id0 ).request >= live; } );
    BOOST_CHECK( view.state == EDITOR_SESSION_STATE::RUNNING );
    BOOST_CHECK( !view.memoryCaptures.at( id0 ).snapshot );
    BOOST_CHECK( !view.memoryCaptures.at( id0 ).error.empty() );
    runner.Pause();
    view = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    const auto time = view.sample.latest.time;
    const auto revision = view.sessionRevision;
    const auto pause = view.pauseSequence;
    const auto first = runner.CaptureMemory( 0 ), second = runner.CaptureMemory( 1 );
    view = Await( runner, [&]( const auto& v ) { return v.memoryCaptures.contains( id1 ) && v.memoryCaptures.at( id1 ).request >= second; } );
    BOOST_CHECK_GE( view.memoryCaptures.at( id0 ).request, first );
    BOOST_REQUIRE( view.memoryCaptures.at( id0 ).snapshot );
    BOOST_REQUIRE( view.memoryCaptures.at( id1 ).snapshot );
    BOOST_CHECK_EQUAL( view.memoryCaptures.at( id0 ).snapshot->time, time );
    BOOST_CHECK_EQUAL( view.memoryCaptures.at( id1 ).snapshot->time, time );
    BOOST_CHECK_EQUAL( view.pauseSequence, pause );
    BOOST_CHECK_EQUAL( view.sample.latest.time, time );
    runner.Pause();
    runner.Step();
    view = Await( runner, [&]( const auto& v ) { return v.pauseSequence > pause; } );
    BOOST_CHECK( view.state == EDITOR_SESSION_STATE::PAUSED );
    runner.Stop(); runner.Start( RamCircuit() );
    view = Await( runner, [&]( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING && v.sessionRevision > revision; } );
    BOOST_CHECK( view.memoryCaptures.empty() );
}

BOOST_AUTO_TEST_CASE( MemoryViewerPausedCapturesFollowCircuitWritesWithoutAdvancingTime )
{
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    runner.Start( RamCircuit() );
    Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING; } );
    runner.Pause();
    auto view = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    const auto id = view.memoryRegions.at( 0 ).id;
    auto capture = [&]()
    {
        const auto request = runner.CaptureMemory( 0 );
        auto result = Await( runner, [&]( const auto& v ) { return v.memoryCaptures.contains( id ) && v.memoryCaptures.at( id ).request >= request; } );
        BOOST_REQUIRE( result.memoryCaptures.at( id ).snapshot );
        return result;
    };
    view = capture();
    MEMORY_PAUSE_COMPARISON comparison;
    comparison.Accept( view.memoryCaptures.at( id ).snapshot, view.sessionRevision, view.pauseSequence );
    runner.Resume(); runner.ToggleSwitch( 0 );
    Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING && v.switches[0]; } );
    runner.ToggleSwitch( 1 );
    auto writing = Await( runner, []( const auto& v ) { return !v.switches[1]; } );
    Await( runner, [&]( const auto& v ) { return v.sample.latest.time > writing.sample.latest.time; } );
    runner.ToggleSwitch( 1 );
    Await( runner, []( const auto& v ) { return v.switches[1]; } );
    runner.Pause();
    view = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    const auto time = view.sample.latest.time;
    view = capture();
    BOOST_CHECK_EQUAL( view.sample.latest.time, time );
    const auto snapshot = view.memoryCaptures.at( id ).snapshot;
    BOOST_CHECK_EQUAL( snapshot->image.data[0], 0xff );
    BOOST_CHECK_EQUAL( snapshot->image.data[1], 0 );
    comparison.Accept( snapshot, view.sessionRevision, view.pauseSequence );
    BOOST_REQUIRE( comparison.Changed()[0] );
    BOOST_CHECK_EQUAL( std::count( comparison.Changed().begin(), comparison.Changed().end(), true ), 1 );
    view = capture();
    comparison.Accept( view.memoryCaptures.at( id ).snapshot, view.sessionRevision, view.pauseSequence );
    BOOST_CHECK( comparison.Changed()[0] );
}
