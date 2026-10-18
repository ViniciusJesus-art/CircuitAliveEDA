/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "session_runner.h"

namespace CIRCUITALIVE
{
void ConfigureVisualSignals( CIRCUIT_SNAPSHOT& aCircuit );
void UpdateVisualSignals( const CIRCUIT_SNAPSHOT& aCircuit, EDITOR_SESSION_VIEW& aView,
                          double aPlaybackSpeed, const SIMULATION_PREFERENCES& aPreferences = {} );
}
