/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "power_rails.h"
#include "circuit_snapshot.h"
#include <algorithm>
#include <cmath>
#include <regex>
#include <stdexcept>
namespace CIRCUITALIVE
{
bool IsSimulationGround( const std::string& name )
{
    return name == "0" || name == "GND" || name == "GNDREF";
}
std::optional<double> PowerSymbolVoltage( const std::string& value )
{
    std::string text = value;
    text.erase( std::remove_if( text.begin(), text.end(), []( unsigned char c )
        { return c == ' ' || c == '\t'; } ), text.end() );
    if( text.size() > 128 ) return {};
    // Native power names use both +3V3 and +3.3V. A suffix distinguishes rails
    // such as +5VA/+5VD while preserving their native net names.
    static const std::regex decimalMarker( R"(^([+-]?)([0-9]+)[Vv]([0-9]+)([A-Za-z_][A-Za-z0-9_]*)?$)" );
    static const std::regex volts( R"(^([+-]?)([0-9]+(?:\.[0-9]+)?)([fpnumkKMGg]?)[Vv]([A-Za-z_][A-Za-z0-9_]*)?$)" );
    static const std::regex bare( R"(^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$)" );
    std::smatch match;
    std::string number;
    if( std::regex_match( text, match, decimalMarker ) )
        number = match[1].str() + match[2].str() + "." + match[3].str();
    else if( std::regex_match( text, match, volts ) )
        number = match[1].str() + match[2].str() + match[3].str();
    else if( std::regex_match( text, bare ) ) number = text;
    else return {};
    const double voltage = ParseComponentValue( number );
    if( std::abs( voltage ) > 1e6 ) throw std::runtime_error( "Power symbol voltage exceeds 1 MV: " + value );
    return voltage;
}
std::vector<POWER_RAIL> CollectPowerRails( const std::vector<POWER_SYMBOL>& symbols )
{
    std::vector<POWER_RAIL> rails;
    std::map<std::string, std::size_t> nets;
    for( const auto& symbol : symbols )
    {
        const auto voltage = PowerSymbolVoltage( symbol.value );
        if( !voltage ) continue; // VCC, VDD, PWR_FLAG and other names remain labels.
        if( symbol.net.empty() ) throw std::runtime_error( symbol.reference + ": power symbol has no resolved net" );
        auto [it, inserted] = nets.emplace( symbol.net, rails.size() );
        if( !inserted )
        {
            const auto& other = rails[it->second];
            if( std::abs( other.voltage - *voltage ) > 1e-9 * std::max( 1.0, std::abs( *voltage ) ) )
                throw std::runtime_error( "Conflicting power symbols on the same net: " + other.reference
                    + " (" + other.name + ") and " + symbol.reference + " (" + symbol.value + ")" );
        }
        else rails.push_back( { symbol.reference, symbol.value, symbol.net, *voltage } );
    }
    return rails;
}
std::size_t VOLTAGE_CONSTRAINTS::Node( const std::string& name )
{
    auto [it, inserted] = m_ids.emplace( name, m_nodes.size() );
    if( inserted ) m_nodes.push_back( { it->second, 0, {} } );
    return it->second;
}
std::pair<std::size_t, double> VOLTAGE_CONSTRAINTS::Find( std::size_t node )
{
    double voltage = 0;
    while( node != m_nodes[node].parent )
    {
        voltage += m_nodes[node].offset;
        node = m_nodes[node].parent;
    }
    return { node, voltage };
}
std::optional<double> VOLTAGE_CONSTRAINTS::Difference( const std::string& positive, const std::string& negative )
{
    const auto [p, vp] = Find( Node( positive ) );
    const auto [n, vn] = Find( Node( negative ) );
    if( p == n ) return vp - vn;
    return {};
}
bool VOLTAGE_CONSTRAINTS::Add( const std::string& positive, const std::string& negative,
                              double voltage, const std::string& origin )
{
    const auto [p, vp] = Find( Node( positive ) );
    const auto [n, vn] = Find( Node( negative ) );
    if( p == n )
    {
        if( std::abs( vp - vn - voltage ) > 1e-8 * std::max( { 1.0, std::abs( voltage ), std::abs( vp - vn ) } ) )
            throw std::runtime_error( std::string( positive == negative ? "Short circuit across ideal supply: "
                    : "Conflicting ideal supply voltage: " ) + origin
                    + ( m_nodes[p].origin.empty() ? "" : " versus " + m_nodes[p].origin ) );
        return false;
    }
    m_nodes[p].parent = n;
    m_nodes[p].offset = voltage - vp + vn;
    if( m_nodes[n].origin.empty() ) m_nodes[n].origin = origin;
    return true;
}
}
