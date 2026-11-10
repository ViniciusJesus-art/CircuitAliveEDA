/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <cmath>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;

namespace
{
struct RECTIFIER
{
    std::string library;
    double breakdown;
    bool schottky = false;
};

std::vector<RECTIFIER> Rectifiers()
{
    std::vector<RECTIFIER> result;
    for( const auto* name : { "D", "D_45deg", "D_45deg_Filled", "D_Filled", "D_Small", "D_Small_Filled" } )
        result.push_back( { std::string( "Device:" ) + name, 100 } );
    result.push_back( { "Simulation_SPICE:D", 100 } );
    result.push_back( { "Diode:1N4148", 100 } );
    result.push_back( { "Diode:1N4148W", 100 } );
    const double rectifierRatings[] = { 50, 100, 200, 400, 600, 800, 1000 };
    for( unsigned i = 0; i < 7; ++i )
        result.push_back( { "Diode:1N400" + std::to_string( i + 1 ), rectifierRatings[i] } );
    const double powerRatings[] = { 50, 100, 200, 300, 400, 500, 600, 800, 1000 };
    for( unsigned i = 0; i < 9; ++i )
        result.push_back( { "Diode:1N540" + std::to_string( i ), powerRatings[i] } );
    for( unsigned i = 0; i < 3; ++i )
    {
        result.push_back( { "Diode:1N581" + std::to_string( i + 7 ), 20. + 10 * i, true } );
        result.push_back( { "Diode:1N582" + std::to_string( i ), 20. + 10 * i, true } );
    }
    return result;
}

SIM_SAMPLE Run( const CIRCUIT_SNAPSHOT& circuit )
{
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( .001 );
    return session.Snapshot().latest;
}

double Measured( const CIRCUIT_SNAPSHOT& circuit, const SIM_SAMPLE& sample,
                 const std::string& name, MEASURE_QUANTITY quantity = MEASURE_QUANTITY::VOLTAGE )
{
    for( const auto& channel : circuit.measurements )
        if( channel.name == name && channel.quantity == quantity )
            return MeasurementValue( channel, sample );
    throw std::runtime_error( "Missing diode bench measurement: " + name );
}

const std::vector<CIRCUIT_PROBE> probes = {
    { PROBE_KIND::VOLTAGE, "A", "", { { "1", "anode" } } },
    { PROBE_KIND::VOLTAGE, "K", "", { { "1", "cathode" } } }
};
}

BOOST_AUTO_TEST_CASE( DiodeReverseBiasUnloadedProbeDoesNotDemonstrateReverseConduction )
{
    // Reproduce the schematic: K at +5 V, A connected only to an ideal voltage probe.
    std::vector<CIRCUIT_COMPONENT> parts = {
        { "D1", "Diode:1N4148", "1N4148", { { "1", "cathode" }, { "2", "anode" } } }
    };
    const std::vector<POWER_SYMBOL> rails = { { "#PWR01", "+5V", "cathode" } };
    auto circuit = BuildCircuitSnapshot( parts, probes, false, rails );
    auto sample = Run( circuit );
    BOOST_CHECK_SMALL( Measured( circuit, sample, "K" ) - 5, 1e-6 );
    BOOST_CHECK_GT( Measured( circuit, sample, "A" ), 4.99 );
    BOOST_TEST_MESSAGE( "1N4148, unloaded A: " << Measured( circuit, sample, "A" ) << " V" );

    // A real return path distinguishes leakage from a reversed or shorted diode.
    parts.push_back( { "RL", "Device:R", "1k", { { "1", "anode" }, { "2", "0" } } } );
    parts[0].measurements = { true, true, false };
    circuit = BuildCircuitSnapshot( parts, probes, false, rails );
    sample = Run( circuit );
    BOOST_CHECK_SMALL( Measured( circuit, sample, "A" ), 25e-6 );
    BOOST_CHECK_SMALL( Measured( circuit, sample, "D1" ) + 5, 25e-6 );
    BOOST_CHECK_SMALL( Measured( circuit, sample, "D1", MEASURE_QUANTITY::CURRENT ), 25e-9 );
    BOOST_TEST_MESSAGE( "1N4148, A with 1k to GND: " << Measured( circuit, sample, "A" )
                        << " V; I(A,K): " << Measured( circuit, sample, "D1", MEASURE_QUANTITY::CURRENT )
                        << " A" );
}

BOOST_AUTO_TEST_CASE( DiodeReverseBiasEveryBundledRectifierBlocksACathodeSupplyWithALoad )
{
    for( const auto& spec : Rectifiers() ) for( bool reverse : { true, false } )
        BOOST_TEST_CONTEXT( spec.library << ( reverse ? " reverse" : " forward" ) )
    {
        // Physical pin 1 is K and 2 is A in each associated stock drawing.
        CIRCUIT_COMPONENT diode{ "D1", spec.library, "D", { { "1", "cathode" }, { "2", "anode" } } };
        diode.measurements = { true, true, false };
        const auto supply = reverse ? "cathode" : "anode";
        const auto output = reverse ? "anode" : "cathode";
        auto circuit = BuildCircuitSnapshot( { diode,
            { "RL", "Device:R", "1k", { { "1", output }, { "2", "0" } } } },
            probes, false, { { "#PWR01", "+5V", supply } } );
        const auto sample = Run( circuit );
        const double anode = Measured( circuit, sample, "A" );
        const double cathode = Measured( circuit, sample, "K" );
        const double current = Measured( circuit, sample, "D1", MEASURE_QUANTITY::CURRENT );
        if( reverse )
        {
            // Schottky junctions have appreciably higher leakage than silicon PN parts.
            BOOST_CHECK_GE( anode, -1e-6 );
            BOOST_CHECK_LT( anode, spec.schottky ? .05 : .001 );
            BOOST_CHECK_LT( current, 0 );
            BOOST_CHECK_SMALL( current, spec.schottky ? 50e-6 : 1e-6 );
            BOOST_CHECK_SMALL( current + anode / 1000, 1e-9 );
        }
        else
        {
            BOOST_CHECK_GT( cathode, 4 );
            BOOST_CHECK_LT( cathode, 5 );
            BOOST_CHECK_GT( current, .004 );
            BOOST_CHECK_SMALL( current - cathode / 1000, 1e-9 );
            BOOST_CHECK_GT( anode - cathode, spec.schottky ? .05 : .4 );
        }
    }
}

BOOST_AUTO_TEST_CASE( DiodeReverseBiasRectifiersBlockBelowBreakdownAndAvalancheAboveIt )
{
    for( const auto& spec : Rectifiers() ) BOOST_TEST_CONTEXT( spec.library )
    {
        // Current-limited bench; no destructive/thermal failure is being modeled.
        auto circuit = BuildCircuitSnapshot( {
            { "VS", "Simulation_SPICE:VDC", std::to_string( spec.breakdown / 2 ),
                { { "1", "drive" }, { "2", "0" } } },
            { "RL", "Device:R", std::to_string( spec.breakdown * 1000 ),
                { { "1", "drive" }, { "2", "cathode" } } },
            { "D1", spec.library, "D", { { "1", "cathode" }, { "2", "0" } } }
        } );
        const auto cathode = circuit.pinNodes.at( 2 ).at( "1" );
        circuit.config.signals = { "v(" + cathode + ")", "i(Vca1)" };
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        session.AdvanceTo( .001 );
        auto values = session.Snapshot().latest.values;
        BOOST_CHECK_SMALL( values.at( 1 ), spec.schottky ? 50e-6 : 1e-6 );
        BOOST_CHECK_GT( values.at( 0 ), spec.breakdown * .4 );
        session.SetSourceValue( "Vca1", spec.breakdown * 2 );
        session.AdvanceTo( .002 );
        values = session.Snapshot().latest.values;
        BOOST_CHECK_GT( values.at( 0 ), spec.breakdown * .95 );
        BOOST_CHECK_LT( values.at( 0 ), spec.breakdown * 1.1 );
        BOOST_CHECK_LT( values.at( 1 ), -.0005 );
    }
}

BOOST_AUTO_TEST_CASE( DiodeReverseBiasZenersBlockACathodeSupplyBelowTheirKnee )
{
    const std::vector<std::pair<std::string, double>> voltages = {
        { "2V4", 2.4 }, { "2V7", 2.7 }, { "3V0", 3 }, { "3V3", 3.3 },
        { "3V6", 3.6 }, { "3V9", 3.9 }, { "4V3", 4.3 }, { "4V7", 4.7 },
        { "5V1", 5.1 }, { "5V6", 5.6 }, { "6V2", 6.2 }, { "6V8", 6.8 },
        { "7V5", 7.5 }, { "8V2", 8.2 }, { "9V1", 9.1 }, { "10", 10 },
        { "11", 11 }, { "12", 12 }, { "13", 13 }, { "15", 15 }, { "16", 16 },
        { "18", 18 }, { "20", 20 }, { "22", 22 }, { "24", 24 }, { "27", 27 },
        { "30", 30 }, { "33", 33 }, { "36", 36 }, { "39", 39 }, { "43", 43 },
        { "47", 47 }, { "51", 51 }, { "56", 56 }, { "62", 62 }, { "68", 68 }, { "75", 75 }
    };
    for( const auto& [suffix, voltage] : voltages ) for( const auto* grade : { "B", "C" } )
        BOOST_TEST_CONTEXT( "BZV55" << grade << suffix )
    {
        CIRCUIT_COMPONENT diode{ "D1", std::string( "Diode:BZV55" ) + grade + suffix,
            "Zener", { { "1", "cathode" }, { "2", "anode" } } };
        diode.measurements = { true, true, false };
        const auto circuit = BuildCircuitSnapshot( {
            { "VS", "Simulation_SPICE:VDC", std::to_string( voltage / 2 ),
                { { "1", "cathode" }, { "2", "0" } } },
            diode,
            { "RL", "Device:R", "1k", { { "1", "anode" }, { "2", "0" } } }
        }, probes );
        const auto sample = Run( circuit );
        const double anode = Measured( circuit, sample, "A" );
        const double current = Measured( circuit, sample, "D1", MEASURE_QUANTITY::CURRENT );
        BOOST_CHECK_GE( anode, -1e-6 );
        BOOST_CHECK_LT( anode, .05 );
        BOOST_CHECK_LT( current, 0 );
        BOOST_CHECK_SMALL( current, 50e-6 );
        BOOST_CHECK_SMALL( current + anode / 1000, 1e-9 );
    }
}
