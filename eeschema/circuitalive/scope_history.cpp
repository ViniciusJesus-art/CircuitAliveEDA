/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_history.h"
#include <algorithm>
#include <limits>

namespace CIRCUITALIVE
{
void SCOPE_HISTORY::Append( std::vector<SIM_SAMPLE> samples )
{
    if( samples.empty() || m_capacity == 0 ) return;
    auto first = samples.begin();
    if( !empty() ) first = std::upper_bound( samples.begin(), samples.end(), back().time,
        []( double t, const SIM_SAMPLE& s ) { return t < s.time; } );
    const std::size_t offset = first - samples.begin();
    if( offset == samples.size() ) return;
    m_size += samples.size() - offset;
    m_blocks.push_back( { std::make_shared<const std::vector<SIM_SAMPLE>>( std::move( samples ) ), offset } );
    while( m_size > m_capacity )
    {
        auto& block = m_blocks.front();
        const auto remove = std::min( m_size - m_capacity, block.samples->size() - block.first );
        block.first += remove; m_size -= remove;
        if( block.first == block.samples->size() ) m_blocks.pop_front();
    }
}
void SCOPE_HISTORY::ForEachAfter( double time, const std::function<bool( const SIM_SAMPLE& )>& consume ) const
{
    for( const auto& block : m_blocks )
    {
        if( block.samples->back().time <= time ) continue;
        const auto first = std::upper_bound( block.samples->begin() + block.first, block.samples->end(), time,
            []( double t, const SIM_SAMPLE& s ) { return t < s.time; } );
        for( auto it = first; it != block.samples->end(); ++it ) if( !consume( *it ) ) return;
    }
}
std::vector<SIM_SAMPLE> SCOPE_HISTORY::Copy() const
{
    std::vector<SIM_SAMPLE> samples; samples.reserve( size() );
    ForEachAfter( -std::numeric_limits<double>::infinity(), [&]( const auto& s ) { samples.push_back( s ); return true; } );
    return samples;
}
}
