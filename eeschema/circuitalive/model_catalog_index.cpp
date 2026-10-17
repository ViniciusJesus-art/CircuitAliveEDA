/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_catalog_index.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace CIRCUITALIVE
{
namespace
{
std::string fold( std::string s )
{
    for( auto& c : s ) if( static_cast<unsigned char>( c ) < 128 ) c = std::tolower( static_cast<unsigned char>( c ) );
    return s;
}
}
MODEL_CATALOG_INDEX::MODEL_CATALOG_INDEX( const std::vector<SIMULATION_MODEL>& models )
{
    m_records.reserve( models.size() );
    for( std::size_t i = 0; i < models.size(); ++i )
    {
        const auto& m = models[i];
        std::string category = m.category, libraries;
        for( const auto& id : m.libraryIds )
        {
            if( !libraries.empty() ) libraries += ", ";
            libraries += id;
            if( category.empty() )
            {
                category = id.substr( 0, id.find( ':' ) );
                std::replace( category.begin(), category.end(), '_', ' ' );
            }
        }
        if( category.empty() ) category = "Custom";
        while( category.ends_with( '/' ) ) category.pop_back();
        while( category.starts_with( '/' ) ) category.erase( 0, 1 );
        const auto group = category + ( m.family.empty() ? "" : "/" + m.family );
        std::string terms = m.name + " " + m.id + " " + group + " " + libraries + " " + m.description;
        for( const auto& name : m.symbolNames ) terms += " " + name;
        for( const auto& name : m.partNames ) terms += " " + name;
        m_records.push_back( { i, m.name, category, m.family, group, libraries, fold( terms ) } );
    }
}
std::vector<std::size_t> MODEL_CATALOG_INDEX::Find( const std::string& query, const std::string& group ) const
{
    std::istringstream words( fold( query ) );
    std::vector<std::string> terms;
    for( std::string term; words >> term; ) terms.push_back( std::move( term ) );
    std::vector<std::size_t> found;
    for( std::size_t i = 0; i < m_records.size(); ++i )
    {
        const auto& r = m_records[i];
        if( !group.empty() && r.group != group && !r.group.starts_with( group + "/" ) ) continue;
        if( std::all_of( terms.begin(), terms.end(), [&]( const auto& term ) { return r.search.find( term ) != std::string::npos; } ) )
            found.push_back( i );
    }
    std::sort( found.begin(), found.end(), [&]( auto a, auto b )
    {
        const auto& x = m_records[a]; const auto& y = m_records[b];
        return x.name != y.name ? x.name < y.name : x.model < y.model;
    } );
    return found;
}
}
