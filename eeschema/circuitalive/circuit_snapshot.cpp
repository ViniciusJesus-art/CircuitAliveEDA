/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "circuit_snapshot.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
double ParseComponentValue( const std::string& aValue )
{
    std::string value = aValue;
    value.erase( std::remove_if( value.begin(), value.end(),
                               []( unsigned char c ) { return c == ' ' || c == '\t'; } ), value.end() );
    double number = 0;
    auto [end, error] = std::from_chars( value.data(), value.data() + value.size(), number );
    if( error != std::errc() || !std::isfinite( number ) )
        throw std::runtime_error( "Invalid numeric value: " + aValue );

    std::string suffix( end, static_cast<std::size_t>( value.data() + value.size() - end ) );
    // SPICE convention: M means milli; use Meg for mega.
    const std::map<std::string, double> scales = {
        { "", 1 }, { "R", 1 }, { "Ohm", 1 }, { "ohm", 1 }, { "Ω", 1 },
        { "V", 1 }, { "F", 1 }, { "f", 1e-15 }, { "p", 1e-12 }, { "n", 1e-9 },
        { "u", 1e-6 }, { "µ", 1e-6 }, { "μ", 1e-6 }, { "m", 1e-3 }, { "M", 1e-3 },
        { "k", 1e3 }, { "K", 1e3 }, { "Meg", 1e6 }, { "meg", 1e6 }, { "G", 1e9 }
    };
    // Permit customary unit suffixes, e.g. 100uF, 1kOhm, 5V.
    auto scale = scales.find( suffix );
    if( scale == scales.end() )
    {
        for( const std::string unit : { "Ohm", "ohm", "Ω", "F", "V" } )
        {
            if( suffix.ends_with( unit ) )
            {
                scale = scales.find( suffix.substr( 0, suffix.size() - unit.size() ) );
                break;
            }
        }
    }
    if( scale == scales.end() || !std::isfinite( number * scale->second ) )
        throw std::runtime_error( "Unsupported numeric suffix: " + aValue );
    return number * scale->second;
}

double LedBrightness( double aForwardCurrent )
{
    // Display response, not an optical/damage model. Reverse current never lights the LED.
    return std::sqrt( std::clamp( aForwardCurrent / 0.01, 0.0, 1.0 ) );
}

CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents )
{
    CIRCUIT_SNAPSHOT result;
    result.config.step = result.config.maxStep = 1e-4;
    result.config.horizon = 3600; // Finite internal horizon: completion is explicit in the UI.
    result.config.historyCapacity = 256;
    std::map<std::string, std::string> nets;
    std::set<std::string> references;
    std::vector<std::string> errors;
    bool grounded = false;
    std::ostringstream deck;
    deck.imbue( std::locale::classic() );
    deck.precision( 17 );
    deck << "CircuitAliveEDA interactive schematic snapshot\n";

    auto node = [&]( const CIRCUIT_COMPONENT& c, const std::string& pin ) -> std::string
    {
        auto it = c.pins.find( pin );
        if( it == c.pins.end() || it->second.empty() )
            throw std::runtime_error( "Pin " + pin + " has no electrical connection" );
        if( it->second == "GND" || it->second == "0" )
        {
            grounded = true;
            return "0";
        }
        auto [net, inserted] = nets.emplace( it->second, "n" + std::to_string( nets.size() + 1 ) );
        return net->second;
    };

    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& c = aComponents[i];
        try
        {
            if( c.reference.empty() || c.reference.find( '?' ) != std::string::npos
                || !references.insert( c.reference ).second )
                throw std::runtime_error( "Annotate the schematic: reference missing or repeated" );
            if( c.explicitSpiceModel )
                throw std::runtime_error( "Custom SPICE model is not supported by interactive mode yet" );

            const std::string id = std::to_string( i + 1 );
            const auto& lib = c.libraryId;
            if( lib == "Device:R" || lib == "Device:R_Small" || lib == "Device:R_US"
                || lib == "Device:C" || lib == "Device:C_Small" )
            {
                double value = ParseComponentValue( c.value );
                if( value <= 0 )
                    throw std::runtime_error( "R and C must have a positive value" );
                bool capacitor = lib.starts_with( "Device:C" );
                deck << ( capacitor ? "C" : "R" ) << "ca" << id << ' ' << node( c, "1" )
                     << ' ' << node( c, "2" ) << ' ' << value << ( capacitor ? " ic=0" : "" ) << '\n';
            }
            else if( lib == "Simulation_SPICE:VDC" )
            {
                deck << "Vca" << id << ' ' << node( c, "1" ) << ' ' << node( c, "2" )
                     << " DC " << ParseComponentValue( c.value ) << '\n';
            }
            else if( lib == "Device:LED" || lib == "Device:LED_Small" )
            {
                // Only the documented red teaching model. Do not assume a generic LED's colour.
                if( c.value != "RED" && c.value != "LED_RED" )
                    throw std::runtime_error( "Use value RED for the supported red LED model" );
                deck << "Dca" << id << ' ' << node( c, "2" ) << ' ' << node( c, "1" )
                     << " CA_RED\n"; // KiCad pin 1 = cathode, pin 2 = anode.
                result.leds.push_back( { i, result.config.signals.size() } );
                result.config.signals.push_back( "@dca" + id + "[id]" );
            }
            else if( lib == "Switch:SW_SPST" )
            {
                std::string source = "Vcontrol" + id;
                deck << source << " control" << id << " 0 5\n"
                     << "Sca" << id << ' ' << node( c, "1" ) << ' ' << node( c, "2" )
                     << " control" << id << " 0 CA_SW\n";
                result.switches.push_back( { i, source } );
            }
            else
                throw std::runtime_error( "No interactive model for " + lib );
        }
        catch( const std::exception& e )
        {
            errors.push_back( c.reference + ": " + e.what() );
        }
    }
    if( aComponents.empty() )
        errors.push_back( "The schematic has no supported components" );
    if( !grounded )
        errors.push_back( "Connect a reference ground (power:GND or a net named 0/GND)" );
    if( nets.empty() )
        errors.push_back( "Connect at least one non-ground net" );
    if( result.config.signals.size() > 64 )
        errors.push_back( "This prototype supports at most 64 LED signals" );
    if( !errors.empty() )
    {
        std::string message;
        for( const auto& error : errors )
            message += error + '\n';
        throw std::runtime_error( message );
    }
    if( result.config.signals.empty() )
        result.config.signals.push_back( "v(" + nets.begin()->second + ")" );
    deck << ".model CA_SW SW(Ron=0.01 Roff=1e12 Vt=2.5 Vh=0.1)\n"
         << ".model CA_RED D(Is=1e-20 N=2 Rs=5)\n.end\n";
    result.netlist = deck.str();
    return result;
}
}
