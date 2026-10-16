/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "circuit_topology.h"
#include "component_catalog.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace CIRCUITALIVE
{
CIRCUIT_TOPOLOGY BuildCircuitTopology( const std::vector<CIRCUIT_COMPONENT>& aComponents )
{
    CIRCUIT_TOPOLOGY result;
    result.nodes.resize( aComponents.size() );
    std::map<std::string, std::size_t> nodeIds;
    std::vector<std::map<std::string, std::size_t>> pins( aComponents.size() );
    std::vector<BUILTIN_BEHAVIOR> behaviors;
    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& c = aComponents[i];
        auto behavior = FindBuiltinBehavior( c.libraryId, c.deviceHint );
        behaviors.push_back( behavior );
        if( behavior == BUILTIN_BEHAVIOR::UNKNOWN ) continue;
        if( c.pins.size() != 2 || !c.pins.contains( "1" ) || !c.pins.contains( "2" ) )
            throw std::runtime_error( c.reference + ": this basic model requires physical pins 1 and 2" );
        for( const auto& [pin, rawName] : c.pins )
        {
            std::string name = rawName;
            if( name == "GND" ) name = "0";
            std::string key = "net:" + name;
            if( name.empty() )
            {
                key = "open:" + std::to_string( i ) + ":" + pin;
                ++result.openPins;
            }
            auto [node, inserted] = nodeIds.emplace( key, nodeIds.size() );
            pins[i][pin] = node->second;
        }
    }

    std::vector<std::size_t> parents( nodeIds.size() );
    std::iota( parents.begin(), parents.end(), 0 );
    auto root = [&]( std::size_t node )
    {
        while( parents[node] != node )
        {
            parents[node] = parents[parents[node]];
            node = parents[node];
        }
        return node;
    };
    for( const auto& pair : pins )
        if( !pair.empty() )
            parents[root( pair.at( "2" ) )] = root( pair.at( "1" ) );

    struct ISLAND
    {
        std::size_t referenceNode;
        std::string representative;
        std::string source;
        bool explicitGround = false;
    };
    std::map<std::size_t, ISLAND> islands;
    const auto ground = nodeIds.find( "net:0" );
    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        if( pins[i].empty() ) continue;
        auto id = root( pins[i].at( "1" ) );
        auto [it, inserted] = islands.try_emplace( id, pins[i].at( "1" ), aComponents[i].reference, "", false );
        auto& island = it->second;
        if( ground != nodeIds.end() && root( ground->second ) == id )
        {
            island.explicitGround = true;
            island.referenceNode = ground->second;
        }
        else if( behaviors[i] == BUILTIN_BEHAVIOR::DC_VOLTAGE
                 && ( island.source.empty() || aComponents[i].reference < island.source ) )
        {
            island.source = aComponents[i].reference;
            island.referenceNode = pins[i].at( "2" );
        }
    }
    std::vector<std::string> spiceNames( nodeIds.size() );
    std::size_t number = 1;
    for( const auto& [id, island] : islands )
    {
        spiceNames[island.referenceNode] = "0";
        if( !island.explicitGround )
        {
            if( !island.source.empty() )
                result.automaticReferences.push_back( island.source + ".2" );
            else
                result.floatingIslands.push_back( island.representative );
        }
    }
    for( auto& name : spiceNames )
        if( name.empty() ) name = "n" + std::to_string( number++ );
    for( std::size_t i = 0; i < pins.size(); ++i )
        for( const auto& [pin, id] : pins[i] )
            result.nodes[i][pin] = spiceNames[id];
    return result;
}
}
