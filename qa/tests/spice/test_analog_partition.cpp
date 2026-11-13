/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/analog_partition.h>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <cmath>

using namespace CIRCUITALIVE;
namespace
{
const std::string deck = "Independent RC loads\nVrail rail 0 DC 5\n"
    "R1 rail a 1000\nC1 a 0 1u ic=0\nR2 rail b 2000\nC2 b 0 2u ic=0\n.end\n";
SIM_SESSION_CONFIG configuration()
{
    SIM_SESSION_CONFIG cfg; cfg.step = cfg.maxStep = 1e-5; cfg.horizon = .1;
    cfg.signals = { "v(a)", "v(b)", "v(rail)", "i(Vrail)" };
    cfg.historyCapacity = 2048; cfg.partitionable = true;
    return cfg;
}
}
BOOST_AUTO_TEST_SUITE( AnalogPartitions )
BOOST_AUTO_TEST_CASE( SharedGroundAndIdealSupplyCanBeSeparated )
{
    auto cfg = configuration(); auto plan = BuildAnalogPartitions( deck, cfg, 2 );
    BOOST_REQUIRE_EQUAL( plan.size(), 2 );
    for( const auto& part : plan )
    {
        BOOST_CHECK( part.elements.contains( "vrail" ) );
        BOOST_CHECK( part.config.synchronizeAdvances );
        BOOST_CHECK( part.elements.contains( "r1" ) != part.elements.contains( "r2" ) );
    }
    cfg.partitionable = false;
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( deck, cfg, 2 ).size(), 1 );
}
BOOST_AUTO_TEST_CASE( ImpedanceAndControlledCurrentKeepDependentLoadsTogether )
{
    auto cfg = configuration();
    auto coupled = deck; coupled.insert( coupled.find( ".end" ), "Rcouple a b 10\n" );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( coupled, cfg, 2 ).size(), 1 );
    coupled = deck; coupled.insert( coupled.find( ".end" ), "Bfeedback b 0 I={i(Vrail)*.1}\n" );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( coupled, cfg, 2 ).size(), 1 );
    coupled = deck; coupled.replace( coupled.find( "Vrail rail" ), 10, "Vrail input" );
    coupled.insert( coupled.find( "R1" ), "Rfeed input rail 10\n" );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( coupled, cfg, 2 ).size(), 1 );
}
BOOST_AUTO_TEST_CASE( AtomicComponentsAndCrossDomainObservationsStayWhole )
{
    auto cfg = configuration();
    cfg.analogCouplings.push_back( { deck.find( "R1" ), deck.find( ".end" ) } );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( deck, cfg, 2 ).size(), 1 );
    cfg.analogCouplings.clear(); cfg.signals.push_back( "v(a,b)" );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( deck, cfg, 2 ).size(), 1 );
    cfg.signals.pop_back(); cfg.activityMonitors.push_back( { 0, 1 } );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( deck, cfg, 2 ).size(), 1 );
}
BOOST_AUTO_TEST_CASE( UnknownGrammarAndMutableModelsUseTheOriginalSolver )
{
    auto cfg = configuration();
    for( const auto& extra : { "Xcustom a b custom\n", ".ic v(a)=2\n", "Epoly a 0 POLY(1) b 0 1\n",
                              ".param read=@r1[i]\n", ".model symbolic ca_memory(slot={value})\n" } )
    {
        auto unsupported = deck; unsupported.insert( unsupported.find( ".end" ), extra );
        BOOST_CHECK_EQUAL( BuildAnalogPartitions( unsupported, cfg, 2 ).size(), 1 );
    }
    cfg.mutableElements.push_back( "R1" );
    BOOST_CHECK_EQUAL( BuildAnalogPartitions( deck, cfg, 2 ).size(), 1 );
}
BOOST_AUTO_TEST_CASE( IsolatedSolversPreserveVoltagesTotalCurrentAndSourceChanges )
{
    auto cfg = configuration(); cfg.synchronizeAdvances = true;
    cfg.activityMonitors.push_back( { 0, ACTIVITY_NO_SIGNAL, 2, .2, .8 } );
    ANALOG_SIM_SESSION parallel( CIRCUITALIVE_TEST_WORKER ), serial( CIRCUITALIVE_TEST_WORKER );
    parallel.Configure( deck, cfg, true, 2 ); serial.Configure( deck, cfg, false );
    BOOST_REQUIRE_GE( parallel.PartitionCount(), 2 );
    for( double time : { .001, .003, .01 } )
    {
        parallel.AdvanceTo( time ); serial.AdvanceTo( time );
        const auto a = parallel.Snapshot( true ), b = serial.Snapshot( true );
        BOOST_CHECK_SMALL( a.latest.time - time, 2e-11 );
        for( std::size_t n = 0; n < cfg.signals.size(); ++n )
            BOOST_CHECK_SMALL( a.latest.values[n] - b.latest.values[n], 5e-5 );
        BOOST_REQUIRE( !a.history.empty() );
        for( std::size_t n = 1; n < a.history.size(); ++n ) BOOST_CHECK_GT( a.history[n].time, a.history[n-1].time );
        for( const auto& point : a.history )
        {
            // Kirchhoff's law also verifies reconstruction on different local grids.
            const double current = -( ( point.values[2] - point.values[0] ) / 1000
                                   + ( point.values[2] - point.values[1] ) / 2000 );
            BOOST_CHECK_SMALL( point.values[3] - current, 1e-10 );
        }
    }
    parallel.SetSourceValue( "Vrail", 3.3 ); serial.SetSourceValue( "Vrail", 3.3 );
    parallel.AdvanceTo( .02 ); serial.AdvanceTo( .02 );
    auto a = parallel.Snapshot( true ), b = serial.Snapshot( true );
    BOOST_CHECK_EQUAL( a.eventsApplied, 1 );
    BOOST_CHECK_EQUAL( a.eventsApplied, b.eventsApplied );
    for( std::size_t n = 0; n < cfg.signals.size(); ++n ) BOOST_CHECK_SMALL( a.latest.values[n] - b.latest.values[n], 5e-5 );
    parallel.Configure( deck, cfg, true, 2 ); parallel.AdvanceTo( .001 );
    BOOST_CHECK_LT( parallel.Snapshot().latest.values[0], 4 );
}
BOOST_AUTO_TEST_CASE( SelectedHistoryAndPerChannelActivityAreMappedBack )
{
    auto cfg = configuration(); cfg.historySignals = std::vector<std::size_t>{ 1 };
    cfg.activityMonitors.push_back( { 1, ACTIVITY_NO_SIGNAL, 2, .2, .8 } );
    cfg.activityMonitors.push_back( { ACTIVITY_NO_SIGNAL, ACTIVITY_NO_SIGNAL, 2, .2, .8 } );
    ANALOG_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( deck, cfg, true, 2 ); session.AdvanceTo( .01 );
    auto sample = session.Snapshot( true ); BOOST_REQUIRE_EQUAL( sample.activity.size(), 2 );
    BOOST_REQUIRE( !sample.history.empty() );
    for( const auto& point : sample.history )
    {
        BOOST_CHECK( std::isnan( point.values[0] ) ); BOOST_CHECK( std::isfinite( point.values[1] ) );
        BOOST_CHECK( std::isnan( point.values[2] ) ); BOOST_CHECK( std::isnan( point.values[3] ) );
    }
    BOOST_CHECK( session.Snapshot( true ).history.empty() );
    BOOST_CHECK_EQUAL( sample.samplesOverwritten, 0 );
}
BOOST_AUTO_TEST_CASE( CompilerRecordsIndivisibleComponentExpansions )
{
    auto circuit = BuildCircuitSnapshot( {
        { "V1", "Simulation_SPICE:VDC", "12", { { "1", "supply" }, { "2", "0" } } },
        { "R1", "Device:R", "1k", { { "1", "supply" }, { "2", "a" } } },
        { "C1", "Device:C", "1u", { { "1", "a" }, { "2", "0" } } },
        { "R2", "Device:R", "2k", { { "1", "supply" }, { "2", "b" } } },
        { "C2", "Device:C", "2u", { { "1", "b" }, { "2", "0" } } }
    }, {}, true );
    BOOST_CHECK( circuit.config.partitionable ); BOOST_REQUIRE_EQUAL( circuit.config.analogCouplings.size(), 5 );
    BOOST_CHECK_GE( BuildAnalogPartitions( circuit.netlist, circuit.config, 2 ).size(), 2 );
}
BOOST_AUTO_TEST_CASE( RelayCoilContactsAndDelayedArmatureRemainInOneSolver )
{
    auto circuit = BuildCircuitSnapshot( {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "0" } } },
        { "VC", "Simulation_SPICE:VDC", "0", { { "1", "coil" }, { "2", "0" } } },
        { "K1", "Relay:Relay_SPDT", "12V", { { "A1", "coil" }, { "A2", "0" },
                { "11", "supply" }, { "12", "nc" }, { "14", "no" } } },
        { "RNC", "Device:R", "1k", { { "1", "nc" }, { "2", "0" } } },
        { "RNO", "Device:R", "1k", { { "1", "no" }, { "2", "0" } } },
        { "R2", "Device:R", "2k", { { "1", "supply" }, { "2", "b" } } },
        { "C2", "Device:C", "2u", { { "1", "b" }, { "2", "0" } } }
    }, {}, true );
    circuit.config.synchronizeAdvances = true;
    auto plan = BuildAnalogPartitions( circuit.netlist, circuit.config, 2 );
    BOOST_REQUIRE_EQUAL( plan.size(), 2 );
    for( const auto& part : plan ) if( part.elements.contains( "rcoil_ca3" ) )
    {
        BOOST_CHECK( part.elements.contains( "snc0_ca3" ) );
        BOOST_CHECK( part.elements.contains( "sno0_ca3" ) );
        BOOST_CHECK( part.elements.contains( "adelay_ca3" ) );
    }
    ANALOG_SIM_SESSION parallel( CIRCUITALIVE_TEST_WORKER ), serial( CIRCUITALIVE_TEST_WORKER );
    parallel.Configure( circuit.netlist, circuit.config, true, 2 );
    serial.Configure( circuit.netlist, circuit.config, false );
    for( double time : { .001, .003, .02 } )
    {
        parallel.AdvanceTo( time ); serial.AdvanceTo( time );
        auto a = parallel.Snapshot( true ), b = serial.Snapshot( true );
        for( std::size_t n = 0; n < a.latest.values.size(); ++n ) BOOST_CHECK_SMALL( a.latest.values[n] - b.latest.values[n], 1e-4 );
        if( time == .001 ) { parallel.SetSourceValue( "Vca2", 12 ); serial.SetSourceValue( "Vca2", 12 ); }
    }
}
BOOST_AUTO_TEST_CASE( MemoryReadsRouteToTheSolverThatActuallyPerformedTheWrite )
{
    std::vector<CIRCUIT_COMPONENT> parts{
        { "VCC", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "0" } } },
        { "VW", "Simulation_SPICE:VDC", "5", { { "1", "we" }, { "2", "0" } } }
    };
    for( int device = 0; device < 2; ++device ) for( int bit = 0; bit < 8; ++bit )
        parts.push_back( { "VD" + std::to_string( device ) + std::to_string( bit ), "Simulation_SPICE:VDC", "5",
                { { "1", "data" + std::to_string( device ) + "_d" + std::to_string( bit ) }, { "2", "0" } } } );
    for( int device = 0; device < 2; ++device )
    {
        CIRCUIT_COMPONENT ram{ "U" + std::to_string( device + 1 ), "Memory_RAM:CY62256-70PC", "RAM", {} };
        const auto model = MODEL_REPOSITORY().SelectModel( ram );
        for( const auto& pin : model.pins )
            ram.pins[pin.defaultPin] = pin.id == "vcc" || pin.id == "oe" ? "vcc"
                    : pin.id == "we" ? ( device ? "vcc" : "we" )
                    : pin.role == "bidirectional" ? "data" + std::to_string( device ) + "_" + pin.id : "0";
        parts.push_back( std::move( ram ) );
    }
    auto circuit = BuildCircuitSnapshot( parts, {}, true );
    ANALOG_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config, true, 2 );
    BOOST_REQUIRE_EQUAL( session.PartitionCount(), 2 );
    BOOST_TEST_CHECKPOINT( "Initial memory barrier" );
    session.AdvanceTo( 1e-6 ); session.Snapshot( true );
    BOOST_TEST_CHECKPOINT( "Memory write enable barrier" );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( 2e-6 ); session.Snapshot( true );
    BOOST_TEST_CHECKPOINT( "Memory write commit barrier" );
    session.SetSourceValue( "Vca2", 5 ); session.AdvanceTo( 3e-6 );
    BOOST_CHECK_EQUAL( session.ReadMemory( circuit.config.memories[0].id, 0, 1 ).image.data[0], 255 );
    BOOST_CHECK_EQUAL( session.ReadMemory( circuit.config.memories[1].id, 0, 1 ).image.data[0], 0 );
    session.Configure( circuit.netlist, circuit.config, true, 2 ); session.AdvanceTo( 1e-6 );
    BOOST_CHECK_EQUAL( session.ReadMemory( circuit.config.memories[0].id, 0, 1 ).image.data[0], 0 );
}
BOOST_AUTO_TEST_CASE( FailureDrainsEveryJobAndRequiresReconfiguration )
{
    auto cfg = configuration(); ANALOG_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    BOOST_CHECK_THROW( session.Snapshot(), std::runtime_error );
    session.Configure( deck, cfg, true, 2 );
    BOOST_CHECK( session.Snapshot().latest.values.empty() );
    BOOST_CHECK_THROW( session.AdvanceTo( 1 ), std::runtime_error );
    BOOST_CHECK_THROW( session.AdvanceTo( .001 ), std::runtime_error );
    session.Configure( deck, cfg, true, 2 ); session.AdvanceTo( .001 );
    BOOST_CHECK_GT( session.Snapshot().latest.time, 0 );
}
BOOST_AUTO_TEST_SUITE_END()
