/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_waveform.h"
#include "scope_acquisition.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace CIRCUITALIVE
{
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
