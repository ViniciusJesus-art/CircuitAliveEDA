/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <array>
#include <utility>

namespace CIRCUITALIVE
{
// Millimetres in the symbol's local axes. Including the stroke, the body fits
// within a 2.54 mm pin pitch. Shared with the live overlay so it cannot grow
// back over neighbouring probes when simulation starts.
inline constexpr std::array<std::pair<double, double>, 6> SOURCE_PROBE_OUTLINE = {{
    { -4.5, -1.15 }, { -1.65, -1.15 }, { -.6, 0 }, { -1.65, 1.15 }, { -4.5, 1.15 }, { -4.5, -1.15 }
}};
inline constexpr double SOURCE_PROBE_TEXT_X = -2.975;
inline constexpr double SOURCE_PROBE_LOGIC_TEXT_SIZE = .925;
inline constexpr double SOURCE_PROBE_WAVE_TEXT_SIZE = .875;
inline constexpr double SOURCE_PROBE_STROKE = .15;
inline constexpr const char* SOURCE_PROBE_APPEARANCE = "midpoint-2";
}
