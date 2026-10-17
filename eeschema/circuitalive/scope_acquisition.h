/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "measurements.h"
#include "scope_history.h"
#include <cmath>
#include <deque>
#include <optional>

namespace CIRCUITALIVE
{
enum class SCOPE_TRIGGER_MODE { AUTO, NORMAL, SINGLE, ROLL };
enum class SCOPE_CAPTURE_STATE { WAITING, POST_TRIGGER, TRIGGERED, AUTO, HOLD, SINGLE_DONE, ROLL };

struct SCOPE_TIMEBASE
{
    double perDivision = .02; // Ten horizontal divisions.
    double pretrigger = .5;  // Trigger position, fraction of the screen.
    double offset = 0;      // Seconds; positive moves the window later.
    double Span() const { return 10 * perDivision; }
};

struct SCOPE_TRIGGER
{
    SCOPE_TRIGGER_MODE mode = SCOPE_TRIGGER_MODE::AUTO;
    double level = 2.5, hysteresis = .02, holdoff = 0;
    bool rising = true;
};

struct SCOPE_FRAME
{
    std::vector<SIM_SAMPLE> samples;
    double begin = 0, end = 0;
    std::optional<double> triggerTime;
    bool partial = true, decimated = false;
    std::uint64_t serial = 0;
};

/// Bounded, GUI-independent edge acquisition. Accepts overlapping history snapshots.
/// Interpolates edge time and waits for the requested post-trigger interval before publishing.
class SCOPE_ACQUISITION
{
public:
    void Configure( SCOPE_TIMEBASE aTimebase, SCOPE_TRIGGER aTrigger,
                    MEASUREMENT_CHANNEL aSource, std::size_t aCapacity = 8192,
                    std::vector<MEASUREMENT_CHANNEL> aChannels = {} );
    void Feed( const std::vector<SIM_SAMPLE>& aHistory );
    void Feed( const SCOPE_HISTORY& aHistory );
    void SetTriggerLevel( double aLevel );
    void SetHold( bool aHold );
    void Reframe( SCOPE_TIMEBASE aTimebase ); ///< Zoom/pan a held capture without resuming acquisition.
    void Arm( const std::vector<SIM_SAMPLE>& aRecentHistory = {} );
    void Arm( const SCOPE_HISTORY& aHistory ) { Arm( aHistory.Copy() ); }
    const SCOPE_FRAME& Frame() const { return m_frame; }
    SCOPE_CAPTURE_STATE State() const;

private:
    bool consume( const SIM_SAMPLE& );
    void finishFeed();
    void publish( SCOPE_FRAME aFrame );
    void rolling( bool aAuto );
    SCOPE_TIMEBASE m_timebase;
    SCOPE_TRIGGER m_trigger;
    MEASUREMENT_CHANNEL m_source;
    std::vector<MEASUREMENT_CHANNEL> m_channels;
    double m_reducedUntil = -1;
    std::size_t m_capacity = 8192;
    std::deque<SIM_SAMPLE> m_buffer;
    SCOPE_FRAME m_frame;
    std::optional<SCOPE_FRAME> m_pending;
    std::optional<SIM_SAMPLE> m_previous;
    double m_lastTime = -1, m_lastCapture = -1, m_nextTrigger = -1;
    bool m_edgeArmed = false, m_hold = false, m_singleDone = false;
    SCOPE_CAPTURE_STATE m_state = SCOPE_CAPTURE_STATE::WAITING;
};

struct SCOPE_STATISTICS
{
    double minimum = NAN, maximum = NAN, peakToPeak = NAN, mean = NAN, rms = NAN;
    double frequency = NAN, period = NAN, duty = NAN, maxGap = 0, duration = 0;
    std::size_t samples = 0;
    bool undersampled = false;
};

/// Time-weighted integrals of piecewise linear samples, clipped to the visible window.
SCOPE_STATISTICS ScopeStatistics( const SCOPE_FRAME&, const MEASUREMENT_CHANNEL& );
/// No extrapolation across missing history. Returns NaN outside the available capture.
double ScopeValueAt( const SCOPE_FRAME&, const MEASUREMENT_CHANNEL&, double aTime );
/// Engineering 1–2–5 scale, rounded upward, for automatic V/div and s/div.
double ScopeNiceScale( double aValue );
std::string ScopeFormat( double aValue, const std::string& aUnit );
/// Strict finite SI input (e.g. 20 ms, 2.5 V, 500 mV); accepts decimal comma.
std::optional<double> ScopeParse( std::string aText, const std::string& aUnit );
}
