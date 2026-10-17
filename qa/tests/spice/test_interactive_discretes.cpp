/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <cmath>
#include <sstream>
using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT V( const std::string& ref, const std::string& net, const std::string& value )
{
    return { ref, "Simulation_SPICE:VDC", value, { { "1", net }, { "2", "0" } } };
}
CIRCUIT_COMPONENT Device( const std::string& library, const std::map<std::string, std::string>& nets )
{
    CIRCUIT_COMPONENT c; c.reference = "Q1"; c.libraryId = library; c.value = library.substr( library.find( ':' ) + 1 );
    auto model = MODEL_REPOSITORY().SelectModel( c );
    for( const auto& p : model.pins ) c.pins[p.defaultPin] = nets.at( p.id );
    return c;
}
SIM_SAMPLE Run( const CIRCUIT_SNAPSHOT& c, double time = .002 )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config ); session.AdvanceTo( time ); return session.Snapshot().latest;
}
double Measure( const CIRCUIT_SNAPSHOT& c, const SIM_SAMPLE& sample, MEASURE_QUANTITY q,
                const std::string& reference = "Q1" )
{
    for( const auto& channel : c.measurements )
        if( channel.name == reference && channel.quantity == q ) return MeasurementValue( channel, sample );
    throw std::runtime_error( "Missing measurement: " + reference );
}
CIRCUIT_SNAPSHOT Inject( CIRCUIT_COMPONENT c, const std::string& port, double current,
                        std::vector<CIRCUIT_COMPONENT> sources = {} )
{
    const auto index = sources.size(); sources.push_back( c ); auto circuit = BuildCircuitSnapshot( sources );
    const auto model = MODEL_REPOSITORY().SelectModel( c ); std::string number;
    for( const auto& p : model.pins ) if( p.id == port ) number = p.defaultPin;
    std::ostringstream line; line.precision( 17 );
    line << "Ibench 0 " << circuit.pinNodes[index].at( number ) << " DC " << current << '\n';
    circuit.netlist.insert( circuit.netlist.rfind( ".end" ), line.str() ); return circuit;
}
}
BOOST_AUTO_TEST_CASE( NewDiscreteModelsSelectAutomaticallyWithLibrarySpecificPinouts )
{
    const std::vector<std::pair<std::string, std::map<std::string, std::string>>> expected = {
        { "Transistor_BJT:TIP122", { { "b", "1" }, { "c", "2" }, { "e", "3" } } },
        { "Transistor_BJT:TIP127", { { "b", "1" }, { "c", "2" }, { "e", "3" } } },
        { "Transistor_BJT:TIP41C", { { "b", "1" }, { "c", "2" }, { "e", "3" } } },
        { "Transistor_BJT:TIP42C", { { "b", "1" }, { "c", "2" }, { "e", "3" } } },
        { "Transistor_BJT:BD139", { { "e", "1" }, { "c", "2" }, { "b", "3" } } },
        { "Transistor_BJT:BD140", { { "e", "1" }, { "c", "2" }, { "b", "3" } } },
        { "Transistor_BJT:PN2222A", { { "e", "1" }, { "b", "2" }, { "c", "3" } } },
        { "Transistor_BJT:2N3904", { { "e", "1" }, { "b", "2" }, { "c", "3" } } },
        { "Transistor_BJT:2N3906", { { "e", "1" }, { "b", "2" }, { "c", "3" } } },
        { "Transistor_FET:2N7000", { { "s", "1" }, { "g", "2" }, { "d", "3" } } },
        { "Transistor_FET:BS170", { { "d", "1" }, { "g", "2" }, { "s", "3" } } },
        { "Transistor_FET:IRF540N", { { "g", "1" }, { "d", "2" }, { "s", "3" } } },
        { "Diode:BZV55C5V1", { { "k", "1" }, { "a", "2" } } },
        { "Diode:BZV55C12", { { "k", "1" }, { "a", "2" } } },
        { "Triac_Thyristor:TIC106", { { "k", "1" }, { "a", "2" }, { "g", "3" } } } };
    MODEL_REPOSITORY repository;
    for( const auto& [lib, pins] : expected ) BOOST_TEST_CONTEXT( lib )
    {
        CIRCUIT_COMPONENT c; c.reference = "Q1"; c.libraryId = lib; c.value = lib;
        for( const auto& [port, number] : pins ) c.pins[number] = "";
        const auto resolved = repository.Resolve( c ); BOOST_CHECK( resolved.pins == pins );
        BOOST_CHECK( !resolved.model.warningSignal.empty() );
        BOOST_CHECK( resolved.model.maxStepParameter.empty() ); // No global nanosecond step imposed.
    }
}
BOOST_AUTO_TEST_CASE( CommonBipolarsAndDarlingtonsSwitchRealLoadsAndTurnOff )
{
    for( const auto* name : { "PN2222A", "2N3904", "2N3906", "BD139", "BD140", "TIP41C", "TIP42C", "TIP122", "TIP127" } ) BOOST_TEST_CONTEXT( name )
    {
        const std::string n = name; const bool pnp = n == "2N3906" || n == "BD140" || n == "TIP42C" || n == "TIP127";
        const bool small = n == "PN2222A" || n.starts_with( "2N39" ); const bool darlington = n == "TIP122" || n == "TIP127";
        auto q = Device( "Transistor_BJT:" + n, { { "c", "collector" }, { "b", "base" }, { "e", pnp ? "vcc" : "0" } } );
        q.measurements = { true, true, true };
        std::vector<CIRCUIT_COMPONENT> parts{ V( "V1", "vcc", "12" ), V( "V2", "drive", pnp ? "7" : "5" ),
            { "R1", "Device:R", small ? "1k" : darlington ? "60" : "24", { { "1", pnp ? "collector" : "vcc" }, { "2", pnp ? "0" : "collector" } } },
            { "R2", "Device:R", small ? "4.7k" : darlington ? "1k" : "100", { { "1", "drive" }, { "2", "base" } } }, q };
        parts[2].measurements.current = true;
        // Finite edges let the solver place breakpoints around charge storage and switching.
        parts[1].libraryId = "Simulation_SPICE:VPULSE";
        parts[1].value = "VPULSE";
        parts[1].modelParameters = { { "low", pnp ? "12" : "0" }, { "high", pnp ? "7" : "5" },
            { "delay", "1m" }, { "rise", "1u" }, { "fall", "1u" }, { "width", "1m" }, { "period", "10m" } };
        auto c = BuildCircuitSnapshot( parts ); INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( c.netlist, c.config ); session.AdvanceTo( .0015 ); auto sample = session.Snapshot().latest;
        BOOST_CHECK_GT( Measure( c, sample, MEASURE_QUANTITY::CURRENT, "R1" ), small ? .010 : darlington ? .16 : .45 );
        BOOST_CHECK_LT( std::abs( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ) ), darlington ? 2 : .8 );
        BOOST_CHECK_GT( Measure( c, sample, MEASURE_QUANTITY::POWER ), 0 );
        session.AdvanceTo( .004 );
        BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT, "R1" ), 5e-6 );
    }
}
BOOST_AUTO_TEST_CASE( BipolarGainFitsNominalDatasheetOperatingRegions )
{
    for( const auto* name : { "PN2222A", "2N3904", "2N3906", "BD139", "BD140", "TIP41C", "TIP42C" } ) BOOST_TEST_CONTEXT( name )
    {
        const std::string n = name; const bool pnp = n == "2N3906" || n == "BD140" || n == "TIP42C";
        const bool power = n.starts_with( "TIP" ); const bool medium = n.starts_with( "BD" );
        const double baseCurrent = power ? .03 : medium ? .0015 : n == "PN2222A" ? .0008 : .00005;
        auto q = Device( "Transistor_BJT:" + n, { { "c", "collector" }, { "b", "base" }, { "e", "0" } } );
        q.measurements.current = true;
        auto c = Inject( q, "b", pnp ? -baseCurrent : baseCurrent, { V( "V1", "collector", pnp ? "-5" : "5" ) } );
        auto sample = Run( c ); const double gain = std::abs( Measure( c, sample, MEASURE_QUANTITY::CURRENT ) ) / baseCurrent;
        BOOST_TEST_MESSAGE( n << ": measured DC gain = " << gain );
        BOOST_CHECK_GT( gain, power ? 25 : medium ? 40 : 100 );
        BOOST_CHECK_LT( gain, power ? 80 : medium ? 250 : 300 );
    }
}
BOOST_AUTO_TEST_CASE( DarlingtonsHaveTwoJunctionInputAndFiniteSaturationVoltage )
{
    for( const auto* name : { "TIP122", "TIP127" } ) BOOST_TEST_CONTEXT( name )
    {
        const bool pnp = std::string( name ) == "TIP127";
        auto q = Device( std::string( "Transistor_BJT:" ) + name, { { "c", "collector" }, { "b", "base" }, { "e", pnp ? "vcc" : "0" } } );
        q.measurements = { true, true, false };
        std::vector<CIRCUIT_COMPONENT> parts{ V( "V1", "vcc", "12" ), V( "V2", "drive", pnp ? "11" : "1" ),
            { "RL", "Device:R", "100", { { "1", pnp ? "collector" : "vcc" }, { "2", pnp ? "0" : "collector" } } },
            { "RB", "Device:R", "1k", { { "1", "drive" }, { "2", "base" } } }, q };
        auto c = BuildCircuitSnapshot( parts ); auto sample = Run( c );
        BOOST_CHECK_LT( std::abs( Measure( c, sample, MEASURE_QUANTITY::CURRENT ) ), .005 );
        parts[1].value = pnp ? "7" : "5"; parts[2].value = "3.3"; parts[3].value = "283";
        c = BuildCircuitSnapshot( parts ); sample = Run( c );
        BOOST_TEST_MESSAGE( name << ": |Vce| = " << std::abs( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ) )
                            << " V at |Ic| = " << std::abs( Measure( c, sample, MEASURE_QUANTITY::CURRENT ) ) << " A" );
        BOOST_CHECK_GT( std::abs( Measure( c, sample, MEASURE_QUANTITY::CURRENT ) ), 2.8 );
        BOOST_CHECK_LT( std::abs( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ) ), 2 );
        BOOST_CHECK_GT( std::abs( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ) ), .6 );
    }
}
BOOST_AUTO_TEST_CASE( MosfetOnResistanceMatchesCharacterizationGateVoltage )
{
    for( const auto* name : { "2N7000", "BS170", "IRF540N" } ) BOOST_TEST_CONTEXT( name )
    {
        const std::string n = name; const double current = n == "IRF540N" ? 16 : n == "BS170" ? .2 : .075;
        auto q = Device( "Transistor_FET:" + n, { { "d", "drain" }, { "g", "gate" }, { "s", "0" } } );
        q.measurements.voltage = true;
        auto c = Inject( q, "d", current, { V( "VG", "gate", "10" ) } );
        const auto sample = Run( c ); const double resistance = Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ) / current;
        BOOST_TEST_MESSAGE( n << ": Rds(on) = " << resistance << " Ohm at Vgs = 10 V, Id = " << current << " A" );
        BOOST_CHECK_GT( resistance, n == "IRF540N" ? .025 : .5 );
        BOOST_CHECK_LT( resistance, n == "IRF540N" ? .044 : 5 );
    }
}
BOOST_AUTO_TEST_CASE( MosfetsTurnOffAndTheirBodyDiodesConductInReverse )
{
    for( const auto* name : { "2N7000", "BS170", "IRF540N" } ) BOOST_TEST_CONTEXT( name )
    {
        auto q = Device( std::string( "Transistor_FET:" ) + name, { { "d", "drain" }, { "g", "gate" }, { "s", "0" } } );
        q.measurements = { true, true, false };
        std::vector<CIRCUIT_COMPONENT> parts{ V( "V1", "vcc", "12" ), V( "V2", "gate", "0" ),
            { "RL", "Device:R", "1k", { { "1", "vcc" }, { "2", "drain" } } }, q };
        auto c = BuildCircuitSnapshot( parts ); INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( c.netlist, c.config ); session.AdvanceTo( .001 );
        BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 5e-6 );
        session.SetSourceValue( "Vca2", 10 ); session.AdvanceTo( .002 );
        BOOST_CHECK_GT( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), .011 );
        session.SetSourceValue( "Vca2", 0 ); session.SetSourceValue( "Vca1", -5 ); session.AdvanceTo( .003 );
        auto sample = session.Snapshot().latest;
        BOOST_CHECK_LT( Measure( c, sample, MEASURE_QUANTITY::CURRENT ), -.004 );
        BOOST_CHECK_LT( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ), -.2 );
        BOOST_CHECK_GT( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ), -1 );
    }
}
BOOST_AUTO_TEST_CASE( ZenerNominalVoltageDynamicResistanceAndForwardDropAreIndependent )
{
    for( const auto* name : { "BZV55C5V1", "BZV55C12" } ) BOOST_TEST_CONTEXT( name )
    {
        const bool low = std::string( name ) == "BZV55C5V1"; const double nominal = low ? 5.1 : 12;
        auto z = Device( std::string( "Diode:" ) + name, { { "k", "out" }, { "a", "0" } } );
        z.measurements = { true, true, true };
        auto c = Inject( z, "k", .005 ); const auto sample = Run( c );
        BOOST_CHECK_CLOSE( -Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ), nominal, .2 );
        BOOST_CHECK_CLOSE( -Measure( c, sample, MEASURE_QUANTITY::CURRENT ), .005, .2 );
        BOOST_CHECK_GT( Measure( c, sample, MEASURE_QUANTITY::POWER ), 0 );
        auto c2 = Inject( z, "k", .006 );
        const double slope = ( -Measure( c2, Run( c2 ), MEASURE_QUANTITY::VOLTAGE ) + Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ) ) / .001;
        BOOST_TEST_MESSAGE( name << ": Vz = " << -Measure( c, sample, MEASURE_QUANTITY::VOLTAGE )
                            << " V at 5 mA; reverse slope = " << slope << " Ohm" );
        BOOST_CHECK_GT( slope, low ? 30 : 7 ); BOOST_CHECK_LT( slope, low ? 60 : 25 );
        auto forward = Inject( z, "k", -.01 ); const auto forwardSample = Run( forward );
        BOOST_CHECK_GT( Measure( forward, forwardSample, MEASURE_QUANTITY::VOLTAGE ), .5 );
        BOOST_CHECK_LT( Measure( forward, forwardSample, MEASURE_QUANTITY::VOLTAGE ), .9 );
    }
}
BOOST_AUTO_TEST_CASE( Tic106NeedsGateCurrentLatchesAndResetsBelowHoldingCurrent )
{
    auto scr = Device( "Triac_Thyristor:TIC106", { { "a", "anode" }, { "k", "0" }, { "g", "gate" } } );
    scr.measurements = { true, true, false };
    std::vector<CIRCUIT_COMPONENT> parts{ V( "V1", "vcc", "12" ), V( "VG", "drive", "0" ),
        { "RL", "Device:R", "100", { { "1", "vcc" }, { "2", "anode" } } },
        { "RG", "Device:R", "4.7k", { { "1", "drive" }, { "2", "gate" } } }, scr };
    const auto c = BuildCircuitSnapshot( parts ); INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config ); session.AdvanceTo( .001 );
    BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 1e-6 );
    session.SetSourceValue( "Vca2", .5 ); session.AdvanceTo( .002 );
    BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 1e-6 );
    session.SetSourceValue( "Vca2", 3.3 ); session.AdvanceTo( .003 );
    BOOST_CHECK_GT( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), .1 );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( .004 );
    BOOST_CHECK_GT( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), .1 );
    session.SetSourceValue( "Vca1", .9 ); session.AdvanceTo( .005 );
    BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 1e-6 );
    session.SetSourceValue( "Vca1", 12 ); session.AdvanceTo( .006 );
    BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 1e-6 );
    session.SetSourceValue( "Vca2", 3.3 ); session.AdvanceTo( .007 );
    BOOST_CHECK_GT( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), .1 );
    session.SetSourceValue( "Vca2", 0 ); session.SetSourceValue( "Vca1", -12 ); session.AdvanceTo( .008 );
    BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 1e-6 );
    session.SetSourceValue( "Vca1", 12 ); session.AdvanceTo( .009 );
    BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 1e-6 );
}
BOOST_AUTO_TEST_CASE( Tic106OnVoltageAtFiveAmpsIsNearDatasheetLimit )
{
    auto scr = Device( "Triac_Thyristor:TIC106", { { "a", "anode" }, { "k", "0" }, { "g", "gate" } } );
    scr.measurements = { true, true, false };
    const auto c = BuildCircuitSnapshot( { V( "V1", "vcc", "12" ), V( "VG", "drive", "3.3" ),
        { "RL", "Device:R", "2.06", { { "1", "vcc" }, { "2", "anode" } } },
        { "RG", "Device:R", "4.7k", { { "1", "drive" }, { "2", "gate" } } }, scr } );
    const auto sample = Run( c );
    BOOST_CHECK_CLOSE( Measure( c, sample, MEASURE_QUANTITY::CURRENT ), 5, .1 );
    BOOST_CHECK_CLOSE( Measure( c, sample, MEASURE_QUANTITY::VOLTAGE ), 1.7, .1 );
}
BOOST_AUTO_TEST_CASE( Existing1N4148ForwardVoltageRemainsInDatasheetEnvelope )
{
    auto d = Device( "Diode:1N4148", { { "a", "out" }, { "k", "0" } } ); d.measurements.voltage = true;
    for( double current : { .01, .1 } )
    {
        const auto c = Inject( d, "a", current ); const double drop = Measure( c, Run( c ), MEASURE_QUANTITY::VOLTAGE );
        BOOST_TEST_MESSAGE( "1N4148: Vf = " << drop << " V at If = " << current << " A" );
        BOOST_CHECK_GT( drop, .55 ); BOOST_CHECK_LT( drop, 1 );
    }
}
BOOST_AUTO_TEST_CASE( StoredChargeModelsRemainDampedAcrossInteractiveDriveChanges )
{
    for( const auto* name : { "2N3904", "BD139", "TIP122" } ) BOOST_TEST_CONTEXT( name )
    {
        auto q = Device( std::string( "Transistor_BJT:" ) + name,
                         { { "c", "collector" }, { "b", "base" }, { "e", "0" } } );
        q.measurements.current = true;
        const auto c = BuildCircuitSnapshot( { V( "V1", "vcc", "12" ), V( "V2", "drive", "5" ),
            { "RL", "Device:R", "1k", { { "1", "vcc" }, { "2", "collector" } } },
            { "RB", "Device:R", "1k", { { "1", "drive" }, { "2", "base" } } }, q } );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
        session.AdvanceTo( .002 );
        BOOST_CHECK_GT( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), .01 );
        session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( .004 );
        BOOST_CHECK_SMALL( Measure( c, session.Snapshot().latest, MEASURE_QUANTITY::CURRENT ), 5e-6 );
    }
}
