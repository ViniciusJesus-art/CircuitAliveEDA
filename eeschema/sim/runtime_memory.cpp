/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "runtime_memory.h"
#include <algorithm>
#include <stdexcept>
namespace CIRCUITALIVE
{
void RUNTIME_MEMORY::Reset( const std::vector<MEMORY_REGION_CONFIG>& regions )
{
    m_regions.clear();
    m_error.clear();
    std::size_t total = 0;
    std::set<std::string> ids;
    if( regions.size() > 128 )
        throw std::runtime_error( "Too many memory regions" );
    for( const auto& config : regions )
    {
        const auto size = config.initial.data.size();
        total += size;
        if( config.id.empty() || !ids.insert( config.id ).second || !size || size > MAX_MEMORY_BYTES ||
            total > MAX_MEMORY_BYTES || config.initial.known.size() != size || !config.wordBits ||
            config.wordBits > 32 || size % ( ( config.wordBits + 7 ) / 8 ) )
            throw std::runtime_error( "Invalid runtime memory configuration" );
        REGION region;
        region.config = config;
        PAGES pages;
        for( std::size_t start = 0; start < size; start += 256 )
        {
            auto page = std::make_shared<PAGE>();
            for( std::size_t j = 0; j < 256 && start + j < size; ++j )
            {
                page->data[j] = config.initial.data[start + j];
                page->known[j] = config.initial.known[start + j];
            }
            pages.push_back( std::move( page ) );
        }
        region.versions.emplace( 0, std::move( pages ) );
        m_regions.push_back( std::move( region ) );
    }
}
std::uint32_t RUNTIME_MEMORY::read( unsigned slot, std::uint64_t token, unsigned address, std::uint32_t* known )
{
    auto& region = m_regions.at( slot );
    const auto stride = ( region.config.wordBits + 7 ) / 8;
    const auto offset = std::uint64_t( address ) * stride;
    if( offset + stride > region.config.initial.data.size() )
        throw std::runtime_error( "Memory address out of bounds" );
    const auto& pages = region.versions.at( token );
    std::uint32_t result = 0;
    *known = 0;
    for( unsigned i = 0; i < stride; ++i )
    {
        const auto pos = offset + i;
        result |= std::uint32_t( pages[pos / 256]->data[pos % 256] ) << ( 8 * i );
        *known |= std::uint32_t( pages[pos / 256]->known[pos % 256] ) << ( 8 * i );
    }
    return result;
}
std::uint64_t RUNTIME_MEMORY::write( unsigned slot, std::uint64_t token, double, unsigned address, std::uint32_t data,
                                     std::uint32_t known, bool invalidate )
{
    auto& region = m_regions.at( slot );
    if( !region.config.writable )
        return token;
    auto pages = region.versions.at( token );
    if( invalidate )
    {
        auto empty = std::make_shared<PAGE>();
        std::fill( pages.begin(), pages.end(), empty );
    }
    else
    {
        std::uint32_t oldKnown = 0;
        const auto old = read( slot, token, address, &oldKnown );
        const auto mask = region.config.wordBits == 32 ? ~std::uint32_t( 0 )
                                                       : ( std::uint32_t( 1 ) << region.config.wordBits ) - 1;
        if( ( oldKnown & mask ) == ( known & mask ) && ( ( old ^ data ) & known & mask ) == 0 )
            return token;
        const unsigned stride = ( region.config.wordBits + 7 ) / 8;
        const std::size_t offset = std::size_t( address ) * stride;
        for( unsigned i = 0; i < stride; ++i )
        {
            const auto pos = offset + i;
            auto page = std::make_shared<PAGE>( *pages[pos / 256] );
            page->data[pos % 256] = data >> ( 8 * i );
            page->known[pos % 256] = known >> ( 8 * i );
            pages[pos / 256] = std::move( page );
        }
    }
    const auto next = region.next++;
    region.versions.emplace( next, std::move( pages ) );
    return next;
}
void RUNTIME_MEMORY::mark( unsigned slot, std::uint64_t token, double time )
{
    auto& r = m_regions.at( slot );
    if( !r.versions.contains( token ) )
        throw std::runtime_error( "Invalid memory version" );
    while( !r.pending.empty() && r.pending.back().first > time )
        r.pending.pop_back();
    if( r.pending.empty() && token == r.current )
        return;
    if( r.pending.size() > 65536 )
        throw std::runtime_error( "Excessive memory events within one timestep" );
    r.pending.emplace_back( time, token );
}
void RUNTIME_MEMORY::Commit( double time )
{
    for( auto& r : m_regions )
    {
        if( r.pending.empty() )
            continue;
        auto end = r.pending.begin();
        std::uint64_t accepted = r.current;
        while( end != r.pending.end() && end->first <= time )
        {
            accepted = end->second;
            ++end;
        }
        r.previous = r.current;
        r.current = accepted;
        r.pending.erase( r.pending.begin(), end );
        std::set<std::uint64_t> keep{ 0, r.current, r.previous };
        for( const auto& p : r.pending )
            keep.insert( p.second );
        for( auto it = r.versions.begin(); it != r.versions.end(); )
            if( !keep.contains( it->first ) )
                it = r.versions.erase( it );
            else
                ++it;
    }
}
MEMORY_IMAGE RUNTIME_MEMORY::Read( const std::string& id, std::size_t offset, std::size_t length ) const
{
    auto found = std::find_if( m_regions.begin(), m_regions.end(), [&]( const auto& r ) { return r.config.id == id; } );
    if( found == m_regions.end() || offset > found->config.initial.data.size() ||
        length > found->config.initial.data.size() - offset )
        throw std::runtime_error( "Invalid memory read range" );
    MEMORY_IMAGE image;
    image.data.reserve( length );
    image.known.reserve( length );
    const auto& pages = found->versions.at( found->current );
    for( std::size_t i = offset; i < offset + length; ++i )
    {
        image.data.push_back( pages[i / 256]->data[i % 256] );
        image.known.push_back( pages[i / 256]->known[i % 256] );
    }
    return image;
}
std::size_t RUNTIME_MEMORY::Versions() const
{
    std::size_t count = 0;
    for( const auto& r : m_regions )
        count += r.versions.size();
    return count;
}
CA_MEMORY_BACKEND RUNTIME_MEMORY::Backend()
{
    return { 1, this,
             []( void* u, unsigned s, std::uint64_t t, unsigned a, std::uint32_t* k ) -> std::uint32_t
             {
                 auto& self = *static_cast<RUNTIME_MEMORY*>( u );
                 try
                 {
                     return self.read( s, t, a, k );
                 }
                 catch( const std::exception& e )
                 {
                     self.m_error = e.what();
                     *k = 0;
                     return 0;
                 }
             },
             []( void* u, unsigned s, std::uint64_t t, double time, unsigned a, std::uint32_t d, std::uint32_t k,
                 int clear ) -> std::uint64_t
             {
                 auto& self = *static_cast<RUNTIME_MEMORY*>( u );
                 try
                 {
                     return self.write( s, t, time, a, d, k, clear );
                 }
                 catch( const std::exception& e )
                 {
                     self.m_error = e.what();
                     return t;
                 }
             },
             []( void* u, unsigned s, std::uint64_t t, double time )
             {
                 auto& self = *static_cast<RUNTIME_MEMORY*>( u );
                 try
                 {
                     self.mark( s, t, time );
                 }
                 catch( const std::exception& e )
                 {
                     self.m_error = e.what();
                 }
             } };
}
} // namespace CIRCUITALIVE
