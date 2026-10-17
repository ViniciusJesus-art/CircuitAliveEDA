/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_acquisition.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
namespace
{
double value( const MEASUREMENT_CHANNEL& channel, const SIM_SAMPLE& sample )
{
    // A disconnected probe or a stale/empty startup snapshot is not zero volts.
    try { return MeasurementValue( channel, sample ); }
    catch( const std::out_of_range& ) { return NAN; }
}
void trim( std::vector<SIM_SAMPLE>& samples, std::size_t capacity )
{
    if( samples.size() > capacity ) samples.erase( samples.begin(), samples.end() - capacity );
}
}

void SCOPE_ACQUISITION::Configure( SCOPE_TIMEBASE timebase, SCOPE_TRIGGER trigger,
                                  MEASUREMENT_CHANNEL source, std::size_t capacity )
{
    if( !std::isfinite( timebase.perDivision ) || timebase.perDivision < 1e-12
        || timebase.perDivision > 1e9 || !std::isfinite( timebase.offset )
        || !std::isfinite( timebase.pretrigger ) || timebase.pretrigger < 0 || timebase.pretrigger > 1
        || !std::isfinite( trigger.level ) || !std::isfinite( trigger.hysteresis )
        || trigger.hysteresis < 0 || !std::isfinite( trigger.holdoff ) || trigger.holdoff < 0 )
        throw std::invalid_argument( "Invalid oscilloscope acquisition settings" );
    m_timebase = timebase; m_trigger = trigger; m_source = std::move( source );
    m_capacity = std::clamp<std::size_t>( capacity, 2, 8192 );
    m_frame = {}; m_hold = false; m_buffer.clear(); m_previous.reset(); m_pending.reset();
    m_lastTime = m_lastCapture = m_nextTrigger = -1;
    m_edgeArmed = m_singleDone = false; m_state = SCOPE_CAPTURE_STATE::WAITING;
}

void SCOPE_ACQUISITION::Arm( const std::vector<SIM_SAMPLE>& history )
{
    if( !history.empty() )
        m_buffer.assign( history.end() - std::min( history.size(), m_capacity ), history.end() );
    m_pending.reset(); m_previous.reset();
    if( !m_buffer.empty() ) m_previous = m_buffer.back();
    m_lastTime = m_buffer.empty() ? -1 : m_buffer.back().time;
    m_lastCapture = m_nextTrigger = m_lastTime;
    m_edgeArmed = m_singleDone = m_hold = false;
    if( m_previous )
    {
        const double v = value( m_source, *m_previous );
        m_edgeArmed = m_trigger.rising ? v <= m_trigger.level - m_trigger.hysteresis
                                     : v >= m_trigger.level + m_trigger.hysteresis;
    }
    m_state = SCOPE_CAPTURE_STATE::WAITING;
}

void SCOPE_ACQUISITION::SetHold( bool hold )
{
    if( m_hold == hold ) return;
    if( !hold ) Arm();
    m_hold = hold;
}

void SCOPE_ACQUISITION::Reframe( SCOPE_TIMEBASE timebase )
{
    if( !std::isfinite( timebase.perDivision ) || timebase.perDivision <= 0
        || !std::isfinite( timebase.offset ) || !std::isfinite( timebase.pretrigger )
        || timebase.pretrigger < 0 || timebase.pretrigger > 1 )
        throw std::invalid_argument( "Invalid oscilloscope timebase" );
    auto frame = m_frame;
    const double anchor = frame.triggerTime.value_or( frame.end - m_timebase.offset );
    m_timebase = timebase;
    if( frame.triggerTime ) frame.begin = anchor - timebase.Span() * timebase.pretrigger + timebase.offset;
    else frame.begin = anchor - timebase.Span() + timebase.offset;
    frame.end = frame.begin + timebase.Span();
    publish( std::move( frame ) );
}

SCOPE_CAPTURE_STATE SCOPE_ACQUISITION::State() const
{
    return m_hold ? SCOPE_CAPTURE_STATE::HOLD : m_state;
}

void SCOPE_ACQUISITION::publish( SCOPE_FRAME frame )
{
    trim( frame.samples, m_capacity );
    frame.partial = frame.samples.size() < 2 || frame.samples.front().time > frame.begin + 1e-12
                    || frame.samples.back().time < frame.end - 1e-12;
    frame.serial = m_frame.serial + 1;
    m_frame = std::move( frame );
}

void SCOPE_ACQUISITION::rolling( bool automatic )
{
    if( m_buffer.empty() ) return;
    SCOPE_FRAME frame;
    frame.end = std::max( m_timebase.Span(), m_buffer.back().time ) + m_timebase.offset;
    frame.begin = frame.end - m_timebase.Span();
    auto first = std::lower_bound( m_buffer.begin(), m_buffer.end(), frame.begin,
                                  []( const auto& s, double t ) { return s.time < t; } );
    if( first != m_buffer.begin() ) --first; // Keep one interpolation point on each side.
    auto last = std::upper_bound( first, m_buffer.end(), frame.end,
                                 []( double t, const auto& s ) { return t < s.time; } );
    if( last != m_buffer.end() ) ++last;
    frame.samples.assign( first, last ); publish( std::move( frame ) );
    m_lastCapture = m_lastTime;
    m_state = automatic ? SCOPE_CAPTURE_STATE::AUTO : SCOPE_CAPTURE_STATE::ROLL;
}

void SCOPE_ACQUISITION::Feed( const std::vector<SIM_SAMPLE>& history )
{
    if( m_hold || m_singleDone || history.empty() ) return;
    if( history.back().time < m_lastTime )
        Configure( m_timebase, m_trigger, m_source, m_capacity ); // Never join separate runs.
    bool changed = false;
    for( const auto& sample : history )
    {
        if( !std::isfinite( sample.time ) || sample.time <= m_lastTime ) continue;
        changed = true; m_lastTime = sample.time;
        m_buffer.push_back( sample );
        // Amortized trimming; memory remains bounded even on long captures.
        if( m_buffer.size() > m_capacity ) m_buffer.pop_front();
        if( m_lastCapture < 0 ) m_lastCapture = sample.time;
        const double current = value( m_source, sample );
        const double previous = m_previous ? value( m_source, *m_previous ) : NAN;
        const bool lowSide = m_trigger.rising ? current <= m_trigger.level - m_trigger.hysteresis
                                              : current >= m_trigger.level + m_trigger.hysteresis;
        const bool crossing = std::isfinite( current ) && std::isfinite( previous ) && m_edgeArmed
                && ( m_trigger.rising ? previous < m_trigger.level && current >= m_trigger.level
                                      : previous > m_trigger.level && current <= m_trigger.level );
        if( !m_pending && crossing && sample.time >= m_nextTrigger
            && m_trigger.mode != SCOPE_TRIGGER_MODE::ROLL )
        {
            const double edge = m_previous->time + ( sample.time - m_previous->time )
                    * ( m_trigger.level - previous ) / ( current - previous );
            SCOPE_FRAME frame; frame.triggerTime = edge;
            frame.begin = edge - m_timebase.Span() * m_timebase.pretrigger + m_timebase.offset;
            frame.end = frame.begin + m_timebase.Span();
            auto first = std::lower_bound( m_buffer.begin(), m_buffer.end(), frame.begin,
                                          []( const auto& s, double t ) { return s.time < t; } );
            if( first != m_buffer.begin() ) --first;
            frame.samples.assign( first, m_buffer.end() );
            m_pending = std::move( frame ); m_state = SCOPE_CAPTURE_STATE::POST_TRIGGER;
        }
        else if( m_pending )
        {
            m_pending->samples.push_back( sample );
            if( m_pending->samples.size() > m_capacity + 128 ) trim( m_pending->samples, m_capacity );
        }
        if( crossing ) m_edgeArmed = false;
        if( lowSide && std::isfinite( current ) ) m_edgeArmed = true;
        m_previous = sample;
        if( m_pending && sample.time >= m_pending->end )
        {
            m_nextTrigger = sample.time + m_trigger.holdoff;
            publish( std::move( *m_pending ) ); m_pending.reset();
            m_lastCapture = sample.time;
            m_singleDone = m_trigger.mode == SCOPE_TRIGGER_MODE::SINGLE;
            m_state = m_singleDone ? SCOPE_CAPTURE_STATE::SINGLE_DONE : SCOPE_CAPTURE_STATE::TRIGGERED;
            if( m_singleDone ) break;
        }
    }
    if( !changed ) return;
    if( m_pending ) trim( m_pending->samples, m_capacity );
    if( m_trigger.mode == SCOPE_TRIGGER_MODE::ROLL ) rolling( false );
    else if( m_trigger.mode == SCOPE_TRIGGER_MODE::AUTO && !m_pending
             && ( m_frame.samples.empty() || m_lastTime - m_lastCapture >= m_timebase.Span() ) ) rolling( true );
    else if( !m_pending && !m_singleDone && m_lastTime - m_lastCapture >= m_timebase.Span() )
        m_state = SCOPE_CAPTURE_STATE::WAITING;
}

double ScopeValueAt( const SCOPE_FRAME& frame, const MEASUREMENT_CHANNEL& channel, double time )
{
    if( frame.samples.empty() || !std::isfinite( time ) || time < frame.samples.front().time
        || time > frame.samples.back().time || time < frame.begin || time > frame.end ) return NAN;
    auto next = std::lower_bound( frame.samples.begin(), frame.samples.end(), time,
                                 []( const auto& s, double t ) { return s.time < t; } );
    if( next == frame.samples.end() ) return NAN;
    if( next->time == time || next == frame.samples.begin() ) return value( channel, *next );
    const auto& previous = *( next - 1 );
    return value( channel, previous ) + ( value( channel, *next ) - value( channel, previous ) )
            * ( time - previous.time ) / ( next->time - previous.time );
}

SCOPE_STATISTICS ScopeStatistics( const SCOPE_FRAME& frame, const MEASUREMENT_CHANNEL& channel )
{
    SCOPE_STATISTICS result;
    std::vector<std::pair<double, double>> points;
    for( const auto& sample : frame.samples )
        if( sample.time >= frame.begin && sample.time <= frame.end && std::isfinite( value( channel, sample ) ) )
        { points.emplace_back( sample.time, value( channel, sample ) ); ++result.samples; }
    const auto left = ScopeValueAt( frame, channel, frame.begin ), right = ScopeValueAt( frame, channel, frame.end );
    if( std::isfinite( left ) && ( points.empty() || points.front().first > frame.begin ) )
        points.insert( points.begin(), { frame.begin, left } );
    if( std::isfinite( right ) && ( points.empty() || points.back().first < frame.end ) )
        points.emplace_back( frame.end, right );
    if( points.empty() ) return result;
    result.minimum = result.maximum = points.front().second;
    double integral = 0, squareIntegral = 0;
    for( std::size_t i = 0; i < points.size(); ++i )
    {
        const auto [t, v] = points[i];
        result.minimum = std::min( result.minimum, v ); result.maximum = std::max( result.maximum, v );
        if( i == 0 ) continue;
        const auto [t0, v0] = points[i-1]; const double dt = t - t0;
        if( dt <= 0 ) continue;
        integral += dt * ( v0 + v ) / 2;
        squareIntegral += dt * ( v0 * v0 + v0 * v + v * v ) / 3;
        result.duration += dt; result.maxGap = std::max( result.maxGap, dt );
    }
    result.peakToPeak = result.maximum - result.minimum;
    if( result.duration > 0 )
    { result.mean = integral / result.duration; result.rms = std::sqrt( std::max( 0.0, squareIntegral / result.duration ) ); }
    if( result.peakToPeak < std::max( 1e-12, std::max( std::abs( result.minimum ), std::abs( result.maximum ) ) * 1e-9 ) ) return result;
    const double middle = ( result.minimum + result.maximum ) / 2, hysteresis = result.peakToPeak * .1;
    std::vector<double> rises;
    bool armed = points.front().second <= middle - hysteresis;
    for( std::size_t i = 1; i < points.size(); ++i )
    {
        const auto [t, v] = points[i]; const auto [t0, v0] = points[i-1];
        if( armed && v0 < middle && v >= middle )
        { rises.push_back( t0 + ( t - t0 ) * ( middle - v0 ) / ( v - v0 ) ); armed = false; }
        if( v <= middle - hysteresis ) armed = true;
    }
    if( rises.size() < 2 ) return result;
    result.period = ( rises.back() - rises.front() ) / ( rises.size() - 1 );
    if( result.period <= 0 ) return result;
    result.frequency = 1 / result.period;
    double highTime = 0;
    for( std::size_t i = 1; i < points.size(); ++i )
    {
        const auto [t, v] = points[i]; const auto [t0, v0] = points[i-1];
        const double begin = std::max( t0, rises.front() ), end = std::min( t, rises.back() );
        if( end <= begin ) continue;
        const double a = v0 + ( v - v0 ) * ( begin - t0 ) / ( t - t0 );
        const double b = v0 + ( v - v0 ) * ( end - t0 ) / ( t - t0 );
        if( a >= middle && b >= middle ) highTime += end - begin;
        else if( ( a < middle ) != ( b < middle ) )
            highTime += ( end - begin ) * ( a >= middle ? ( a - middle ) : ( b - middle ) ) / std::abs( b - a );
    }
    result.duty = 100 * highTime / ( rises.back() - rises.front() );
    result.undersampled = result.maxGap > result.period / 10;
    return result;
}

double ScopeNiceScale( double number )
{
    if( !std::isfinite( number ) || number <= 0 ) return 1;
    const double decade = std::pow( 10.0, std::floor( std::log10( number ) ) );
    for( double step : { 1., 2., 5., 10. } ) if( step * decade >= number * ( 1 - 1e-12 ) ) return step * decade;
    return decade * 10;
}

std::string ScopeFormat( double number, const std::string& unit )
{
    if( !std::isfinite( number ) ) return "—";
    const double absolute = std::abs( number );
    static const char* prefixes[] = { "p", "n", "µ", "m", "", "k", "M", "G" };
    int power = absolute > 0 ? std::clamp( int( std::floor( std::log10( absolute ) / 3 ) ), -4, 3 ) : 0;
    std::ostringstream out; out.imbue( std::locale::classic() );
    out << std::setprecision( 5 ) << number / std::pow( 1000., power ) << ' ' << prefixes[power+4] << unit;
    return out.str();
}

std::optional<double> ScopeParse( std::string text, const std::string& unit )
{
    std::replace( text.begin(), text.end(), ',', '.' );
    for( const std::string micro : { "µ", "μ" } )
        for( auto pos = text.find( micro ); pos != std::string::npos; pos = text.find( micro ) ) text.replace( pos, micro.size(), "u" );
    static const std::regex pattern( R"(^\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*([pnumkMG]?)\s*([a-zA-Z%]*)\s*$)" );
    std::smatch match;
    if( !std::regex_match( text, match, pattern ) || ( !match[3].str().empty() && match[3] != unit ) ) return {};
    try
    {
        const std::string prefix = match[2]; const std::string letters = "pnum kMG";
        double scale = 1;
        if( !prefix.empty() ) scale = std::pow( 1000., int( letters.find( prefix ) ) - 4 );
        const double result = std::stod( match[1] ) * scale;
        if( std::isfinite( result ) ) return result;
    }
    catch( const std::exception& ) {}
    return {};
}
}
