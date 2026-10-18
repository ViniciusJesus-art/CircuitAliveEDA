/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_preferences.h"
#include <algorithm>
#include <cmath>

namespace CIRCUITALIVE
{
void SIMULATION_PREFERENCES::Normalize()
{
    const SIMULATION_PREFERENCES defaults;
    auto number = []( double value, double low, double high, double fallback )
        { return std::isfinite( value ) ? std::clamp( value, low, high ) : fallback; };
    ledCutoffHz = number( ledCutoffHz, 1, 1000, defaults.ledCutoffHz );
    digitalCutoffHz = number( digitalCutoffHz, 1, 1000, defaults.digitalCutoffHz );
    refreshRateHz = std::clamp( refreshRateHz, 10, 240 );
    playbackSpeed = number( playbackSpeed, .0001, 100, defaults.playbackSpeed );
    stepMs = number( stepMs, .001, 1000, defaults.stepMs );
    maxTimeStepUs = number( maxTimeStepUs, .001, 10000, defaults.maxTimeStepUs );
    durationSeconds = number( durationSeconds, 1, 86400, defaults.durationSeconds );
    workerTimeoutSeconds = number( workerTimeoutSeconds, .1, 60, defaults.workerTimeoutSeconds );
    scopeHistorySamples = std::clamp( scopeHistorySamples, 256, 65536 );
}

std::chrono::milliseconds SIMULATION_PREFERENCES::FrameInterval() const
{
    return std::chrono::milliseconds( std::lround( 1000.0 / std::clamp( refreshRateHz, 10, 240 ) ) );
}
}
