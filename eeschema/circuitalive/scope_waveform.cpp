/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_waveform.h"
#include "scope_acquisition.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace CIRCUITALIVE
{
std::vector<SCOPE_TRACE_POINT> ScopeTrace( const SCOPE_FRAME& frame,
                                          const MEASUREMENT_CHANNEL& channel, std::size_t pixels )
{
    std::vector<SCOPE_TRACE_POINT> result;
    if( !pixels || frame.end <= frame.begin || frame.samples.empty() ) return result;
    std::array<SCOPE_TRACE_POINT, 4> bucket; // First, minimum, maximum, last.
    std::size_t column = 0;
    bool populated = false;
    auto flush = [&]
    {
        if( !populated ) return;
        auto ordered = bucket;
        std::sort( ordered.begin(), ordered.end(), []( const auto& a, const auto& b ) { return a.time < b.time; } );
        for( const auto& point : ordered )
            if( result.empty() || point.time > result.back().time ) result.push_back( point );
    };
    auto add = [&]( double time, double value )
    {
        if( !std::isfinite( value ) || time < frame.begin || time > frame.end ) return;
        const auto next = std::min( pixels, std::size_t( ( time - frame.begin ) / ( frame.end - frame.begin ) * pixels ) );
        if( !populated || next != column )
        {
            flush();
            bucket.fill( { time, value } );
            populated = true; column = next;
        }
        else
        {
            if( value < bucket[1].value ) bucket[1] = { time, value };
            if( value > bucket[2].value ) bucket[2] = { time, value };
            bucket[3] = { time, value };
        }
    };
    add( frame.begin, ScopeValueAt( frame, channel, frame.begin ) );
    for( const auto& sample : frame.samples )
    {
        try { add( sample.time, MeasurementValue( channel, sample ) ); }
        catch( const std::out_of_range& ) {}
    }
    add( frame.end, ScopeValueAt( frame, channel, frame.end ) );
    flush();
    return result;
}

double ScopeVerticalFraction( double v, const SCOPE_CHANNEL_STYLE& c )
{ return .5 - ( v - c.center ) / ( 8 * c.perDivision ) * ( c.inverted ? -1 : 1 ); }
double ScopeVerticalValue( double f, const SCOPE_CHANNEL_STYLE& c )
{ return c.center + ( .5 - f ) * 8 * c.perDivision * ( c.inverted ? -1 : 1 ); }
double ScopeCenterForZero( double f, const SCOPE_CHANNEL_STYLE& c )
{ return ( f - .5 ) * 8 * c.perDivision * ( c.inverted ? -1 : 1 ); }
void ScopeFitRange( SCOPE_CHANNEL_STYLE& c, double minimum, double maximum, double zero )
{
    zero = std::clamp( zero, .1, .9 );
    minimum = std::min( 0., minimum ); maximum = std::max( 0., maximum );
    if( c.inverted ) { const double old = minimum; minimum = -maximum; maximum = -old; }
    c.perDivision = ScopeNiceScale( std::max( { .01, maximum / ( 8 * zero * .9 ), -minimum / ( 8 * ( 1 - zero ) * .9 ) } ) );
    c.center = ScopeCenterForZero( zero, c );
}
void ScopeCompact( std::vector<SIM_SAMPLE>& samples, std::size_t budget,
                   const std::vector<MEASUREMENT_CHANNEL>& channels, std::optional<double> trigger )
{
    budget = std::max<std::size_t>( 2, budget );
    if( samples.size() <= budget ) return;
    // Reserve two endpoints, two trigger neighbours, and first/last/min/max per time bucket.
    const auto count = std::min( channels.size(), ( budget > 6 ? budget - 6 : 0 ) / 2 );
    const auto buckets = std::max<std::size_t>( 1, ( budget > 4 ? budget - 4 : 0 ) / ( 2 + 2 * count ) );
    const double begin = samples.front().time, span = samples.back().time - begin;
    if( span <= 0 ) return;
    std::vector<std::size_t> keep = { 0, samples.size() - 1 };
    if( trigger )
    {
        const auto edge = std::lower_bound( samples.begin(), samples.end(), *trigger, []( const auto& s, double t ) { return s.time < t; } );
        if( edge != samples.end() ) keep.push_back( edge - samples.begin() );
        if( edge != samples.begin() ) keep.push_back( edge - samples.begin() - 1 );
    }
    std::size_t first = 0;
    while( first < samples.size() )
    {
        const auto bucket = std::min( buckets - 1, std::size_t( ( samples[first].time - begin ) / span * buckets ) );
        std::size_t last = first + 1;
        while( last < samples.size() && ( samples[last].time - begin ) / span * buckets < bucket + 1 ) ++last;
        keep.push_back( first ); keep.push_back( last - 1 );
        for( std::size_t c = 0; c < count; ++c )
        {
            std::size_t lo = first, hi = first; double low = INFINITY, high = -INFINITY;
            for( auto i = first; i < last; ++i )
            {
                double v = NAN; try { v = MeasurementValue( channels[c], samples[i] ); } catch( const std::out_of_range& ) {}
                if( v < low ) { low = v; lo = i; } if( v > high ) { high = v; hi = i; }
            }
            keep.push_back( lo ); keep.push_back( hi );
        }
        first = last;
    }
    std::sort( keep.begin(), keep.end() ); keep.erase( std::unique( keep.begin(), keep.end() ), keep.end() );
    // Tiny budgets can only retain the endpoints; the normal budget is >=256 points.
    if( keep.size() > budget )
    {
        std::vector<std::size_t> limited;
        for( std::size_t i = 0; i < budget; ++i ) limited.push_back( keep[i * ( keep.size() - 1 ) / ( budget - 1 )] );
        keep = std::move( limited );
    }
    std::vector<SIM_SAMPLE> reduced; reduced.reserve( keep.size() );
    for( auto i : keep ) reduced.push_back( std::move( samples[i] ) );
    samples = std::move( reduced );
}
}
