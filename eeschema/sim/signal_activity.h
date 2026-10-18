/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace CIRCUITALIVE
{
constexpr std::size_t ACTIVITY_NO_SIGNAL = std::numeric_limits<std::size_t>::max();

/// Thresholds are relative to (supply - reference), or defaultSupply when supply is absent.
struct SIM_ACTIVITY_MONITOR
{
    std::size_t signal = 0, reference = ACTIVITY_NO_SIGNAL, supply = ACTIVITY_NO_SIGNAL;
    double low = 0, high = 1, defaultSupply = 1, minSupply = .1, maxSupply = 1e6;
};

struct SIM_SIGNAL_ACTIVITY
{
    double period = 0, lastTransition = 0, highValue = 0;

    bool RapidAt( double aTime, double aPlaybackSpeed, double aCutoffHz = 60 ) const
    {
        // Presentation cutoff, in cycles per displayed second. Slowing playback
        // makes individual transitions visible again. A stopped clock expires.
        return period > 0 && period * aCutoffHz <= aPlaybackSpeed * ( 1 + 1e-9 )
               && aTime >= lastTransition && aTime - lastTransition <= 1.25 * period;
    }
};

/// Observe every solver step, independently of the bounded history sent to the GUI.
class SIGNAL_ACTIVITY_TRACKER
{
public:
    void Observe( const SIM_ACTIVITY_MONITOR& aMonitor, double aTime,
                  const std::vector<double>& aValues )
    {
        auto value = [&]( std::size_t i, double fallback = 0.0 )
            { return i == ACTIVITY_NO_SIGNAL ? fallback : aValues.at( i ); };
        const double reference = value( aMonitor.reference );
        const double supply = value( aMonitor.supply, aMonitor.defaultSupply ) - reference;
        const double raw = value( aMonitor.signal );
        const double ratio = ( raw - reference ) / supply;
        if( !std::isfinite( ratio ) || !std::isfinite( supply )
            || supply < aMonitor.minSupply || supply > aMonitor.maxSupply )
        {
            *this = {};
            return;
        }
        // Hysteresis ignores intermediate solver points on each edge.
        const int level = ratio <= aMonitor.low ? 0 : ratio >= aMonitor.high ? 1 : -1;
        if( level < 0 ) return;
        if( m_level >= 0 && level != m_level )
        {
            if( m_edges[level] >= 0 ) m_periods[level] = aTime - m_edges[level];
            m_edges[level] = aTime;
            m_lastTransition = aTime;
            if( level == 0 )
            {
                m_previousPeak = m_peak;
                m_peak = 0;
            }
        }
        m_level = level;
        if( level == 1 ) m_peak = std::max( m_peak, raw );
    }

    SIM_SIGNAL_ACTIVITY Activity() const
    {
        // Require two rising and two falling edges: a single narrow pulse is
        // not an oscillator. Use full periods so asymmetric duty cycles work.
        return { m_periods[0] > 0 && m_periods[1] > 0
                         ? std::max( m_periods[0], m_periods[1] ) : 0,
                 m_lastTransition, std::max( m_peak, m_previousPeak ) };
    }

private:
    int m_level = -1;
    double m_edges[2] = { -1, -1 }, m_periods[2] = { 0, 0 };
    double m_lastTransition = 0, m_peak = 0, m_previousPeak = 0;
};
}
