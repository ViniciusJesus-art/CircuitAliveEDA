/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "circuit_topology.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace CIRCUITALIVE
{
CIRCUIT_TOPOLOGY BuildCircuitTopology( const std::vector<CIRCUIT_COMPONENT>& aComponents,
                                     const std::vector<RESOLVED_MODEL>& aModels )
{
    CIRCUIT_TOPOLOGY result;
    result.nodes.resize( aComponents.size() );
    std::map<std::string, std::size_t> nodeIds;
    std::vector<std::map<std::string, std::size_t>> pins( aComponents.size() );
    for( std::size_t i = 0; i < aComponents.size(); ++i )
    {
        const auto& c = aComponents[i];
        for( const auto& [pin, rawName] : c.pins )
        {
            std::string name = rawName;
            if( IsSimulationGround( name ) ) name = "0";
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
    for( std::size_t i = 0; i < pins.size(); ++i )
        for( const auto& group : aModels.at( i ).model.electricalGroups )
        {
            auto first = pins[i].at( aModels[i].pins.at( group.front() ) );
            for( const auto& port : group )
                parents[root( pins[i].at( aModels[i].pins.at( port ) ) )] = root( first );
        }

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
        for( const auto& [pin, node] : pins[i] )
        {
            auto id = root( node );
            auto [it, inserted] = islands.try_emplace( id, node, aComponents[i].reference, "", false );
            auto& island = it->second;
            if( ground != nodeIds.end() && root( ground->second ) == id )
            {
                island.explicitGround = true;
                island.referenceNode = ground->second;
            }
            else if( !aModels[i].model.referencePin.empty()
                     && aModels[i].pins.at( aModels[i].model.referencePin ) == pin
                     && ( island.source.empty() || aComponents[i].reference < island.source ) )
            {
                island.source = aComponents[i].reference + "." + pin;
                island.referenceNode = node;
            }
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
                result.automaticReferences.push_back( island.source );
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
