/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <chrono>

namespace CIRCUITALIVE
{
/// Application preferences; copied into the simulation owner thread.
struct SIMULATION_PREFERENCES
{
    double ledCutoffHz = 60;
    double digitalCutoffHz = 60;
    bool holdFastLeds = true;
    bool markFastDigital = true;
    bool showDigitalIndicators = false;
    int refreshRateHz = 125;
    double playbackSpeed = 1;
    double stepMs = 20;
    double maxTimeStepUs = 100;
    double durationSeconds = 3600;
    double workerTimeoutSeconds = 5;
    bool pushButtonsReleased = true;
    bool autoOpenInstruments = true;
    bool warnOvercurrent = true;
    bool warnSupply = true;
    int scopeHistorySamples = 8192;

    void Normalize();
    std::chrono::milliseconds FrameInterval() const;
    bool operator==( const SIMULATION_PREFERENCES& ) const = default;
};
}
