/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <cmath>
using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( std::string ref, std::string node, std::string value )
{
    return { ref, "Simulation_SPICE:VDC", value, { { "1", node }, { "2", "0" } } };
}
CIRCUIT_COMPONENT Part( const std::string& lib )
{
    CIRCUIT_COMPONENT c{ "U1", lib, lib, {} };
    auto m = MODEL_REPOSITORY().SelectModel( c );
    for( const auto& p : m.pins ) c.pins[p.defaultPin] = "";
    return c;
}
double Measure( CIRCUIT_SNAPSHOT c, std::size_t component, const std::string& pin, double time = .001 )
{
    c.config.signals = { "v(" + c.pinNodes[component].at( pin ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config ); session.AdvanceTo( time );
    return session.Snapshot().latest.values.at( 0 );
}
}
BOOST_AUTO_TEST_CASE( BehavioralGatesFollowTheirTruthTables )
{
    for( const auto* chip : { "74HC02", "74HC04", "74HC86", "74LS08", "74LS32" } )
    for( int bits = 0; bits < 4; ++bits )
    {
        auto u = Part( std::string( "74xx:" ) + chip );
        auto m = MODEL_REPOSITORY().SelectModel( u );
        u.pins["14"] = "vcc"; u.pins["7"] = "0";
        std::string out;
        for( const auto& p : m.pins )
        {
            if( p.role == "input" ) u.pins[p.defaultPin] = p.id == "i1_0" ? "a" : p.id == "i1_1" ? "b" : "0";
            if( p.id == "y1" ) out = p.defaultPin;
        }
        std::vector<CIRCUIT_COMPONENT> parts = { Source( "V1", "vcc", "5" ),
            Source( "VA", "a", bits & 1 ? "5" : "0" ), Source( "VB", "b", bits & 2 ? "5" : "0" ), u };
        bool expected = std::string( chip ) == "74HC02" ? bits == 0 : std::string( chip ) == "74HC04" ? !( bits & 1 )
            : std::string( chip ) == "74HC86" ? bits == 1 || bits == 2 : std::string( chip ) == "74LS08" ? bits == 3 : bits != 0;
        const bool ttl = std::string( chip ).starts_with( "74LS" );
        BOOST_CHECK_SMALL( Measure( BuildCircuitSnapshot( parts ), 3, out ) - ( expected ? ( ttl ? 3.6 : 5 ) : ( ttl ? .2 : 0 ) ), .01 );
    }
}
BOOST_AUTO_TEST_CASE( FlipFlopRetainsStateAcrossPauseAndRespondsToAsyncReset )
{
    auto u = Part( "74xx:74HC74" );
    u.pins["14"] = "vcc"; u.pins["7"] = "0";
    u.pins["2"] = "data"; u.pins["3"] = "clock"; u.pins["4"] = "vcc"; u.pins["1"] = "clear";
    u.pins["10"] = u.pins["13"] = "vcc"; u.pins["12"] = u.pins["11"] = "0";
    auto c = BuildCircuitSnapshot( { Source( "VCC", "vcc", "5" ), Source( "VD", "data", "5" ),
        Source( "VK", "clock", "0" ), Source( "VR", "clear", "5" ), u } );
    c.config.signals = { "v(" + c.pinNodes[4].at( "5" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
    session.AdvanceTo( 1e-6 ); session.SetSourceValue( "Vca3", 5 ); session.AdvanceTo( 2e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.9 );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( 3e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.9 );
    const auto paused = session.Snapshot();
    BOOST_CHECK_EQUAL( paused.latest.time, session.Snapshot().latest.time );
    session.SetSourceValue( "Vca4", 0 ); session.AdvanceTo( 4e-6 );
    BOOST_CHECK_SMALL( session.Snapshot().latest.values[0], .01 );
}
BOOST_AUTO_TEST_CASE( LinearRegulatorsRegulateDropOutAndLimitCurrent )
{
    for( int voltage : { 5, 6, 8, 9, 12, 15, 18, 24 } )
    {
        const auto chip = "Regulator_Linear:L78" + ( voltage < 10 ? std::string( "0" ) : "" ) + std::to_string( voltage );
        auto u = Part( chip ); u.pins["1"] = "in"; u.pins["2"] = "0"; u.pins["3"] = "out";
        std::vector<CIRCUIT_COMPONENT> parts = { Source( "V1", "in", std::to_string( voltage + 5 ) ), u,
            { "R1", "Device:R", "100", { { "1", "out" }, { "2", "0" } } } };
        BOOST_CHECK_CLOSE( Measure( BuildCircuitSnapshot( parts ), 1, "3" ), voltage, .1 );
        parts[0].value = std::to_string( voltage + 1 );
        BOOST_CHECK_CLOSE( Measure( BuildCircuitSnapshot( parts ), 1, "3" ), voltage - 1, .1 );
        parts[2].value = "1";
        BOOST_CHECK_CLOSE( Measure( BuildCircuitSnapshot( parts ), 1, "3" ), 1.5, .1 );
    }
    auto u = Part( "Regulator_Linear:LM317_TO-220" ); u.pins = { { "1", "adj" }, { "2", "out" }, { "3", "in" } };
    auto c = BuildCircuitSnapshot( { Source( "V1", "in", "12" ), u,
        { "R1", "Device:R", "240", { { "1", "out" }, { "2", "adj" } } },
        { "R2", "Device:R", "720", { { "1", "adj" }, { "2", "0" } } } } );
    BOOST_CHECK_CLOSE( Measure( c, 1, "2" ), 5.036, .1 );
}
BOOST_AUTO_TEST_CASE( Ne555AstableOscillatesBetweenSupplyRelativeThresholdsAndResets )
{
    auto u = Part( "Timer:NE555P" );
    u.pins = { { "1", "0" }, { "2", "timing" }, { "3", "out" }, { "4", "reset" },
        { "5", "ctrl" }, { "6", "timing" }, { "7", "dis" }, { "8", "vcc" } };
    auto c = BuildCircuitSnapshot( { Source( "VCC", "vcc", "9" ), Source( "VR", "reset", "9" ), u,
        { "RA", "Device:R", "10k", { { "1", "vcc" }, { "2", "dis" } } },
        { "RB", "Device:R", "10k", { { "1", "dis" }, { "2", "timing" } } },
        { "CT", "Device:C", "1u", { { "1", "timing" }, { "2", "0" } } },
        { "RL", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } } } );
    BOOST_CHECK_LE( c.config.maxStep, 100e-6 );
    c.config.signals = { "v(" + c.pinNodes[2].at( "3" ) + ")", "v(" + c.pinNodes[2].at( "2" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
    bool high = false, low = false; int rising = 0; double lastRise = 0, period = 0;
    for( int i = 1; i <= 1000; ++i )
    {
        session.AdvanceTo( i * 1e-4 ); auto s = session.Snapshot( true ).latest;
        const bool nowHigh = s.values[0] > 4;
        if( nowHigh && !high ) { if( lastRise > .01 ) period = s.time - lastRise; lastRise = s.time; ++rising; }
        high = nowHigh; low |= !nowHigh;
        if( s.time > .025 ) { BOOST_CHECK_GT( s.values[1], 2.9 ); BOOST_CHECK_LT( s.values[1], 6.1 ); }
    }
    BOOST_CHECK_GE( rising, 4 ); BOOST_CHECK( low ); BOOST_CHECK_CLOSE( period, .020794, 2 );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( .101 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[0], .2 );
}
BOOST_AUTO_TEST_CASE( Ne555AdaptiveStepsPreserveSlowAndFastOscillationAndExplicitLimits )
{
    for( const auto& [capacitance,value] : std::vector<std::pair<double,std::string>>{
            { 1e-6, "1u" }, { 10e-9, "10n" }, { 1e-9, "1n" } } )
    {
        auto timer = Part( "Timer:NE555P" );
        timer.pins = { { "1", "0" }, { "2", "timing" }, { "3", "out" }, { "4", "vcc" },
            { "5", "ctrl" }, { "6", "timing" }, { "7", "dis" }, { "8", "vcc" } };
        const std::vector<CIRCUIT_COMPONENT> parts = { Source( "VCC", "vcc", "9" ), timer,
            { "RA", "Device:R", "10k", { { "1", "vcc" }, { "2", "dis" } } },
            { "RB", "Device:R", "10k", { { "1", "dis" }, { "2", "timing" } } },
            { "CT", "Device:C", value, { { "1", "timing" }, { "2", "0" } } },
            { "RL", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } } };
        auto circuit = BuildCircuitSnapshot( parts );
        const double expected = std::log( 2. ) * 30000 * capacitance;
        circuit.config.signals = { "v(" + circuit.pinNodes[1].at( "3" ) + ")" };
        circuit.config.historyCapacity = 4096;
        std::vector<double> periods;
        for( const bool reference : { false, true } )
        {
            auto config = circuit.config;
            if( reference ) config.step = config.maxStep = expected / 200;
            config.horizon = expected * 10;
            INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
            session.Configure( circuit.netlist, config ); session.AdvanceTo( expected * 6 );
            const auto snapshot = session.Snapshot( true );
            std::vector<double> rises;
            bool high = false;
            for( const auto& sample : snapshot.history )
            {
                const bool nowHigh = sample.values[0] > 4;
                if( nowHigh && !high ) rises.push_back( sample.time );
                high = nowHigh;
            }
            BOOST_REQUIRE_GE( rises.size(), 4 );
            periods.push_back( ( rises.back() - rises[1] ) / ( rises.size() - 2 ) );
            BOOST_CHECK_CLOSE( periods.back(), expected, 5 );
        }
        BOOST_CHECK_CLOSE( periods[0], periods[1], 3 );
        auto explicitParts = parts;
        explicitParts[1].modelParameters["max_step"] = "2u";
        BOOST_CHECK_CLOSE( BuildCircuitSnapshot( explicitParts ).config.maxStep,
                           std::min( circuit.config.maxStep, 2e-6 ), 1e-8 );
    }
}

BOOST_AUTO_TEST_CASE( SramStoresIndependentWordsAndTristatesItsDataBus )
{
    auto u = Part( "Memory_RAM:CY62256-70PC" ); auto m = MODEL_REPOSITORY().SelectModel( u );
    std::vector<CIRCUIT_COMPONENT> parts{ Source( "VCC", "vcc", "5" ), Source( "VA", "address", "0" ),
        Source( "VW", "write", "0" ), Source( "VS", "select", "0" ), Source( "VO", "oe", "0" ) };
    for( const auto& p : m.pins ) u.pins[p.defaultPin] = p.id == "vcc" ? "vcc" : p.id == "we" ? "write"
        : p.id == "cs" ? "select" : p.id == "oe" ? "oe" : p.id == "a0" ? "address" : p.id.starts_with( "d" ) ? p.id : "0";
    for( int bit = 0; bit < 8; ++bit )
    {
        parts.push_back( Source( "VD" + std::to_string( bit ), "input" + std::to_string( bit ), ( 0xa5 >> bit ) & 1 ? "5" : "0" ) );
        parts.push_back( { "R" + std::to_string( bit ), "Device:R", "10k",
            { { "1", "input" + std::to_string( bit ) }, { "2", "d" + std::to_string( bit ) } } } );
    }
    parts.push_back( u ); auto c = BuildCircuitSnapshot( parts );
    c.config.signals.clear();
    for( int bit = 0; bit < 8; ++bit )
        c.config.signals.push_back( "v(" + c.pinNodes.back().at( m.pins[0].defaultPin ) + ")" );
    // Locate data pins by logical ID, independently of JSON pin order.
    for( const auto& p : m.pins ) if( p.id.starts_with( "d" ) ) c.config.signals[std::stoi( p.id.substr( 1 ) )] = "v(" + c.pinNodes.back().at( p.defaultPin ) + ")";
    c.config.signals.push_back( "v(bh_code_d0_ca22)" );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
    session.AdvanceTo( 1e-6 ); session.SetSourceValue( "Vca3", 5 ); session.AdvanceTo( 2e-6 ); // finish write A5 @ 0
    session.SetSourceValue( "Vca4", 5 ); session.SetSourceValue( "Vca2", 5 ); // deselect, address 1, change data
    for( int bit = 0; bit < 8; ++bit ) session.SetSourceValue( "Vca" + std::to_string( 6 + 2 * bit ), ( 0x3c >> bit ) & 1 ? 5 : 0 );
    session.AdvanceTo( 3e-6 ); session.SetSourceValue( "Vca3", 0 ); session.SetSourceValue( "Vca4", 0 );
    session.AdvanceTo( 4e-6 ); session.SetSourceValue( "Vca3", 5 ); session.AdvanceTo( 5e-6 );
    for( int address = 0; address < 2; ++address )
    {
        session.SetSourceValue( "Vca2", address ? 5 : 0 ); session.AdvanceTo( ( 6 + address ) * 1e-6 );
        const auto sample = session.Snapshot().latest;
        for( int bit = 0; bit < 8; ++bit ) BOOST_CHECK_EQUAL( sample.values[bit] > 2.5, bool( ( ( address ? 0x3c : 0xa5 ) >> bit ) & 1 ) );
    }
    session.SetSourceValue( "Vca5", 5 ); session.AdvanceTo( 8e-6 ); // OE disables drive: weak external pattern visible
    const auto sample = session.Snapshot().latest;
    for( int bit = 0; bit < 8; ++bit ) BOOST_CHECK_SMALL( sample.values[bit] - ( ( 0x3c >> bit ) & 1 ? 5 : 0 ), .01 );
    session.SetSourceValue( "Vca1", 0 ); session.AdvanceTo( 9e-6 );
    session.SetSourceValue( "Vca1", 5 ); session.SetSourceValue( "Vca5", 0 ); session.AdvanceTo( 10e-6 );
    BOOST_CHECK_CLOSE( session.Snapshot().latest.values[8], .5, .01 ); // volatile contents become X
}

BOOST_AUTO_TEST_CASE( RegulatorInputCurrentIncludesDeliveredLoadAndQuiescentCurrent )
{
    auto u = Part( "Regulator_Linear:L7805" ); u.pins = { { "1", "in" }, { "2", "0" }, { "3", "out" } };
    auto source = Source( "V1", "in", "12" ); source.measurements.current = true;
    CIRCUIT_COMPONENT load{ "R1", "Device:R", "100", { { "1", "out" }, { "2", "0" } } };
    load.measurements.current = true;
    auto c = BuildCircuitSnapshot( { source, u, load } );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config ); session.AdvanceTo( .001 );
    const auto sample = session.Snapshot().latest;
    BOOST_CHECK_CLOSE( -MeasurementValue( c.measurements[0], sample ), MeasurementValue( c.measurements[1], sample ) + .005, .001 );
    u = Part( "Regulator_Linear:LM317_TO3" );
    u.pins = { { "1", "0" }, { "2", "in" }, { "3", "out" } };
    BOOST_CHECK_CLOSE( Measure( BuildCircuitSnapshot( { source, u, load } ), 1, "3" ), 1.25, .1 );
}
