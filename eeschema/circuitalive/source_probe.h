/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "circuit_snapshot.h"

namespace CIRCUITALIVE
{
struct SOURCE_PROBE_SETTINGS
{
    double low = 0, high = 5;
    bool initialHigh = false;
    double frequency = 1000, duty = .5;
    double amplitude = 2.5, offset = 2.5, phase = 0;
    double delay = 0, rise = 10e-9, fall = 10e-9, resistance = 1;
};

void ValidateSourceProbe( PROBE_KIND aKind, const SOURCE_PROBE_SETTINGS& aSettings );
std::string SerializeSourceProbe( PROBE_KIND aKind, const SOURCE_PROBE_SETTINGS& aSettings );
SOURCE_PROBE_SETTINGS ParseSourceProbe( PROBE_KIND aKind, const std::string& aJson );
CIRCUIT_COMPONENT SourceProbeComponent( PROBE_KIND aKind, const SOURCE_PROBE_SETTINGS& aSettings,
        const std::string& aReference, const std::string& aNet );
}
