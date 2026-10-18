/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#define BOOST_TEST_MODULE CircuitAliveEDA_InteractiveSession
#include <boost/test/unit_test.hpp>
#include <interactive_sim_session.h>
#include <platform_runtime.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#include <tlhelp32.h>
#include <io.h>
#else
#include <signal.h>
#include <sstream>
#include <sys/wait.h>
#endif
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace CIRCUITALIVE;
using namespace std::chrono_literals;

static bool Reaped(int pid)
{
#ifdef _WIN32
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(pid));
    if(!process)return true;
    const bool exited=WaitForSingleObject(process,0)==WAIT_OBJECT_0;CloseHandle(process);return exited;
#else
    return waitpid(pid,nullptr,WNOHANG)==-1;
#endif
}
static void KillWorker(int pid)
{
#ifdef _WIN32
    HANDLE process=OpenProcess(PROCESS_TERMINATE,FALSE,static_cast<DWORD>(pid));
    BOOST_REQUIRE(process);TerminateProcess(process,1);CloseHandle(process);
#else
    kill(pid,SIGKILL);
#endif
}
static void SuspendWorker(int pid)
{
#ifdef _WIN32
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);BOOST_REQUIRE(snapshot!=INVALID_HANDLE_VALUE);
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==static_cast<DWORD>(pid))
    {HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);if(thread){SuspendThread(thread);CloseHandle(thread);}}}while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
#else
    kill(pid,SIGSTOP);
#endif
}

static const std::string RC = "RC continuity test\nV1 in 0 1\nR1 in out 1k\n"
                              "C1 out 0 1u ic=0\n.end\n";

static SIM_SESSION_CONFIG Config()
{
    SIM_SESSION_CONFIG cfg;
    cfg.step = cfg.maxStep = 1e-6;
    cfg.horizon = 0.1;
    cfg.historyCapacity = 32;
    cfg.signals = { "v(out)", "i(v1)" };
    return cfg;
}

static double Charge( double time ) { return 1 - std::exp( -time / 0.001 ); }

BOOST_AUTO_TEST_CASE( CapacitorPauseResumeAndReset )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( RC, Config() );
    session.AdvanceTo( 0.0005 );
    auto paused = session.Snapshot();
    BOOST_REQUIRE( paused.state == SIM_SESSION_STATE::PAUSED );
    BOOST_REQUIRE_EQUAL( paused.latest.values.size(), 2 );
    BOOST_CHECK_SMALL( paused.latest.time - 0.0005, 1e-12 );
    BOOST_CHECK_SMALL( paused.latest.values[0] - Charge( paused.latest.time ), 2e-5 );
    BOOST_CHECK_SMALL( paused.latest.values[1] + ( 1 - paused.latest.values[0] ) / 1000, 1e-9 );

    std::this_thread::sleep_for( 10ms );
    auto frozen = session.Snapshot();
    BOOST_CHECK_EQUAL( frozen.latest.time, paused.latest.time );
    BOOST_CHECK( frozen.latest.values == paused.latest.values );
    session.AdvanceTo( 0.001 );
    auto resumed = session.Snapshot();
    BOOST_CHECK( resumed.generation == paused.generation );
    BOOST_CHECK_SMALL( resumed.latest.values[0] - Charge( resumed.latest.time ), 2e-5 );
    BOOST_CHECK_GT( resumed.samplesReceived, paused.samplesReceived );

    const int oldPid = session.WorkerPid();
    session.Stop();
    BOOST_CHECK_NE( session.WorkerPid(), oldPid );
    BOOST_CHECK(Reaped(oldPid));
    auto stopped = session.Snapshot();
    BOOST_CHECK( stopped.state == SIM_SESSION_STATE::STOPPED );
    BOOST_CHECK_EQUAL( stopped.latest.time, 0 );
    BOOST_CHECK( stopped.latest.values.empty() );
    BOOST_CHECK( stopped.history.empty() );
    session.AdvanceTo( 0.0005 );
    auto reset = session.Snapshot();
    BOOST_CHECK_GT( reset.generation, paused.generation );
    BOOST_CHECK_SMALL( reset.latest.values[0] - paused.latest.values[0], 1e-9 );
}

BOOST_AUTO_TEST_CASE( BackgroundPlayAndConfirmedPause )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    auto cfg = Config();
    cfg.horizon = 10;
    session.Configure( RC, cfg );
    session.Play();
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while( session.Snapshot( true ).samplesReceived == 0 )
    {
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( 1ms );
    }
    session.Pause();
    auto paused = session.Snapshot();
    BOOST_REQUIRE( paused.state == SIM_SESSION_STATE::PAUSED );
    std::this_thread::sleep_for( 10ms );
    BOOST_CHECK_EQUAL( session.Snapshot().latest.time, paused.latest.time );
    session.Play();
    deadline = std::chrono::steady_clock::now() + 3s;
    while( session.Snapshot().latest.time <= paused.latest.time )
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
    session.Pause();
    BOOST_CHECK_EQUAL( session.Snapshot().generation, paused.generation );
    session.Stop();
    BOOST_CHECK( session.Snapshot().state == SIM_SESSION_STATE::STOPPED );
}

BOOST_AUTO_TEST_CASE( OrderedSourceChangesPreserveStoredCharge )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( RC, Config() );
    session.AdvanceTo( 0.0005 );
    const auto before = session.Snapshot();
    session.SetSourceValue( "V1", 0.25 );
    session.SetSourceValue( "V1", 0 );
    auto changed = session.Snapshot();
    BOOST_CHECK_EQUAL( changed.eventsApplied, 2 );
    BOOST_CHECK_EQUAL( changed.lastEventTime, before.latest.time );
    BOOST_CHECK_EQUAL( changed.latest.values[0], before.latest.values[0] );
    session.AdvanceTo( 0.001 );
    BOOST_CHECK_SMALL( session.Snapshot().latest.values[0]
                       - before.latest.values[0] * std::exp( -0.5 ), 2e-5 );
    session.Stop();
    session.AdvanceTo( 0.0005 );
    BOOST_CHECK_SMALL( session.Snapshot().latest.values[0] - before.latest.values[0], 1e-9 );
}

BOOST_AUTO_TEST_CASE( SwitchAndLedRespondToCircuitCurrent )
{
    std::ifstream file( CIRCUITALIVE_TEST_DEMO );
    BOOST_REQUIRE( file.good() );
    std::ostringstream deck;
    deck << file.rdbuf();
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    auto cfg = Config();
    cfg.signals = { "cap", "@dled[id]" };
    session.Configure( deck.str(), cfg );
    session.AdvanceTo( 0.005 );
    auto on = session.Snapshot();
    BOOST_REQUIRE_EQUAL( on.latest.values.size(), 2 );
    BOOST_CHECK_GT( on.latest.values[0], 1.5 );
    BOOST_CHECK_GT( on.latest.values[1], 1e-4 );
    session.SetSourceValue( "Vcontrol", 0 );
    BOOST_CHECK_EQUAL( session.Snapshot().latest.values[0], on.latest.values[0] );
    session.AdvanceTo( 0.05 );
    auto off = session.Snapshot();
    BOOST_CHECK_LT( off.latest.values[1], on.latest.values[1] / 100 );
    BOOST_CHECK_LT( off.latest.values[0], on.latest.values[0] / 10 );
}

BOOST_AUTO_TEST_CASE( BoundedHistoryAndNativeStorageAcrossWindows )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    auto cfg = Config();
    cfg.historyCapacity = 16;
    session.Configure( RC, cfg );
    std::uint64_t count = 0;
    for( int window = 1; window <= 100; ++window )
    {
        session.AdvanceTo( window * 0.0005 );
        auto snapshot = session.Snapshot();
        BOOST_REQUIRE( snapshot.state == SIM_SESSION_STATE::PAUSED );
        BOOST_CHECK_GT( snapshot.samplesReceived, count );
        count = snapshot.samplesReceived;
        BOOST_CHECK_LE( snapshot.history.size(), cfg.historyCapacity );
        BOOST_CHECK_EQUAL( snapshot.engineRetainedPoints, 1 );
        for( std::size_t i = 1; i < snapshot.history.size(); ++i )
            BOOST_CHECK_GT( snapshot.history[i].time, snapshot.history[i-1].time );
    }
    auto snapshot = session.Snapshot( true );
    BOOST_CHECK_GT( snapshot.samplesOverwritten, 1000 );
    BOOST_CHECK( session.Snapshot().history.empty() );
    BOOST_CHECK_SMALL( snapshot.latest.values[0] - Charge( snapshot.latest.time ), 2e-5 );
    BOOST_TEST_MESSAGE( "100 windows: samples=" << count << ", retained="
                        << snapshot.engineRetainedPoints << ", RSS=" << snapshot.residentBytes );
}

BOOST_AUTO_TEST_CASE( ExplicitHorizonCompletesWithoutSilentRestart )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    auto cfg = Config();
    cfg.horizon = 0.001;
    session.Configure( RC, cfg );
    session.AdvanceTo( cfg.horizon );
    auto end = session.Snapshot();
    BOOST_CHECK( end.state == SIM_SESSION_STATE::COMPLETED );
    BOOST_CHECK_SMALL( end.latest.time - cfg.horizon, 1e-12 );
    BOOST_CHECK_THROW( session.Play(), std::runtime_error );
    BOOST_CHECK_THROW( session.AdvanceTo( 0.002 ), std::runtime_error );
    BOOST_CHECK_EQUAL( session.Snapshot().generation, end.generation );
    session.Stop();
    session.AdvanceTo( cfg.horizon );
    BOOST_CHECK_SMALL( session.Snapshot().latest.values[0] - end.latest.values[0], 1e-9 );
}

BOOST_AUTO_TEST_CASE( InvalidRequestsDoNotMutateActiveCircuit )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    BOOST_CHECK_THROW( session.Play(), std::runtime_error );
    BOOST_CHECK_THROW( session.Pause(), std::runtime_error );
    auto cfg = Config();
    cfg.historyCapacity = 0;
    BOOST_CHECK_THROW( session.Configure( RC, cfg ), std::runtime_error );
    cfg = Config(); cfg.historyCapacity = 1; cfg.signals.clear();
    for( int i = 0; i < 257; ++i ) cfg.signals.push_back( "v(n" + std::to_string( i ) + ")" );
    BOOST_CHECK_THROW( session.Configure( RC, cfg ), std::runtime_error );
    cfg = Config();
    BOOST_CHECK_THROW( session.Configure( "Bad\n.control\nquit\n.endc\n.end\n", cfg ),
                        std::runtime_error );
    session.Configure( RC, cfg );
    session.AdvanceTo( 0.0005 );
    const auto before = session.Snapshot();
    BOOST_CHECK_THROW( session.AdvanceTo( 0.0001 ), std::runtime_error );
    BOOST_CHECK_THROW( session.AdvanceTo( std::numeric_limits<double>::quiet_NaN() ),
                        std::exception );
    BOOST_CHECK_THROW( session.SetSourceValue( "V1;quit", 1 ), std::runtime_error );
    BOOST_CHECK_THROW( session.Configure( RC, cfg ), std::runtime_error );
    BOOST_CHECK_EQUAL( session.Snapshot().latest.time, before.latest.time );
    session.AdvanceTo( 0.001 );
}

BOOST_AUTO_TEST_CASE( TwoHundredFiftySixSignalsKeepBoundedHistory )
{
    auto cfg = Config(); cfg.signals.clear(); cfg.historyCapacity = 4;
    std::string deck = "Many pin signals\nV1 supply 0 5\n";
    for( int i = 0; i < 256; ++i )
    {
        const auto n = std::to_string( i );
        deck += "Ra" + n + " supply n" + n + " 1k\nRb" + n + " n" + n + " 0 1k\n";
        cfg.signals.push_back( "v(n" + n + ")" );
    }
    deck += ".end\n";
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( deck, cfg ); session.AdvanceTo( .001 );
    const auto snapshot = session.Snapshot();
    BOOST_REQUIRE_EQUAL( snapshot.latest.values.size(), 256 );
    BOOST_CHECK_LE( snapshot.history.size(), 4 );
    for( double value : snapshot.latest.values ) BOOST_CHECK_CLOSE( value, 2.5, 1e-5 );
}

BOOST_AUTO_TEST_CASE( MissingSignalFailsAndStopAllowsRecovery )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    auto cfg = Config();
    cfg.signals = { "nonexistent" };
    session.Configure( RC, cfg );
    BOOST_CHECK_THROW( session.AdvanceTo( 0.0005 ), std::runtime_error );
    BOOST_CHECK( session.Snapshot().state == SIM_SESSION_STATE::ERROR );
    session.Stop();
    session.Configure( RC, Config() );
    session.AdvanceTo( 0.0005 );
    BOOST_CHECK_SMALL( session.Snapshot().latest.values[0] - Charge( 0.0005 ), 2e-5 );
}

BOOST_AUTO_TEST_CASE( SessionsAreIsolatedAndChildrenAreReaped )
{
    int firstPid;
    {
        INTERACTIVE_SIM_SESSION first( CIRCUITALIVE_TEST_WORKER );
        INTERACTIVE_SIM_SESSION second( CIRCUITALIVE_TEST_WORKER );
        firstPid = first.WorkerPid();
        BOOST_CHECK_NE( firstPid, second.WorkerPid() );
        first.Configure( RC, Config() );
        second.Configure( RC, Config() );
        first.AdvanceTo( 0.0005 );
        second.AdvanceTo( 0.001 );
        first.SetSourceValue( "V1", 0 );
        first.AdvanceTo( 0.001 );
        BOOST_CHECK_SMALL( second.Snapshot().latest.values[0] - Charge( 0.001 ), 2e-5 );
        first.Play(); // Destruction while running must stop/reap its process.
    }
    errno = 0;
    BOOST_CHECK(Reaped(firstPid));
#ifndef _WIN32
    BOOST_CHECK_EQUAL( errno, ECHILD );
#endif
}

BOOST_AUTO_TEST_CASE( WorkerFailureIsContained )
{
    BOOST_CHECK_THROW( INTERACTIVE_SIM_SESSION( "/nonexistent/circuitalive-worker" ),
                        std::runtime_error );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER, 1s );
    session.Configure( RC, Config() );
    session.AdvanceTo( 0.0005 );
    const auto before = session.Snapshot();
    int pid = session.WorkerPid();
    KillWorker(pid);
    BOOST_CHECK_THROW( session.Snapshot(), std::runtime_error );
    BOOST_CHECK_EQUAL( session.WorkerPid(), -1 );
    BOOST_CHECK(Reaped(pid));
    session.Stop();
    session.AdvanceTo( 0.0005 );
    BOOST_CHECK_GT( session.Snapshot().generation, before.generation );
    BOOST_CHECK_SMALL( session.Snapshot().latest.values[0] - before.latest.values[0], 1e-9 );
}

BOOST_AUTO_TEST_CASE( WorkerPathWithSpaces )
{
    const auto directory = std::filesystem::temp_directory_path()
            / ("circuitalive worker " + std::to_string(ProcessId()));
    BOOST_REQUIRE( std::filesystem::create_directory(directory) );
#ifdef _WIN32
    const auto link = directory / "simulation worker.exe";
#else
    const auto link = directory / "simulation worker";
#endif
    try
    {
#ifdef _WIN32
        // Copying does not require the Windows symlink privilege.
        std::filesystem::copy_file( CIRCUITALIVE_TEST_WORKER, link );
        std::filesystem::copy( std::filesystem::path(CIRCUITALIVE_TEST_WORKER).parent_path()/"ngspice",
                directory/"ngspice", std::filesystem::copy_options::recursive );
#else
        std::filesystem::create_symlink( CIRCUITALIVE_TEST_WORKER, link );
#endif
        INTERACTIVE_SIM_SESSION session( link.string() );
        session.Configure( RC, Config() );
        session.AdvanceTo( 0.0005 );
        BOOST_CHECK_SMALL( session.Snapshot().latest.values[0] - Charge( 0.0005 ), 2e-5 );
    }
    catch( ... )
    {
        std::filesystem::remove_all( directory );
        throw;
    }
    std::filesystem::remove_all( directory );
}

BOOST_AUTO_TEST_CASE( ClosedStandardInputDoesNotBreakWorkerTransport )
{
#ifdef _WIN32
    const int saved=_dup(0);_close(0);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
    session.Configure(RC,Config());session.AdvanceTo(.0005);
    if(saved>=0){_dup2(saved,0);_close(saved);}
#else
    pid_t child = fork();
    BOOST_REQUIRE( child >= 0 );
    if( child == 0 )
    {
        close( STDIN_FILENO );
        try
        {
            INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
            session.Configure( RC, Config() );
            session.AdvanceTo( 0.0005 );
        }
        catch( ... ) { _exit( 1 ); }
        _exit( 0 );
    }
    int status;
    BOOST_REQUIRE_EQUAL( waitpid( child, &status, 0 ), child );
    BOOST_CHECK( WIFEXITED( status ) && WEXITSTATUS( status ) == 0 );
#endif
}

BOOST_AUTO_TEST_CASE( WorkerTimeoutIsBounded )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER, 200ms );
    int pid = session.WorkerPid();
    SuspendWorker(pid);
    const auto before = std::chrono::steady_clock::now();
    BOOST_CHECK_THROW( session.Snapshot(), std::runtime_error );
    BOOST_CHECK( std::chrono::steady_clock::now() - before < 2s );
    BOOST_CHECK_EQUAL( session.WorkerPid(), -1 );
}
