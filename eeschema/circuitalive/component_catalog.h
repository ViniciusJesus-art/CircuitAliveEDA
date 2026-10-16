/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <string_view>

namespace CIRCUITALIVE
{
enum class BUILTIN_BEHAVIOR { UNKNOWN, RESISTOR, CAPACITOR, DC_VOLTAGE, RED_LED, SWITCH };

/// Associates ordinary KiCad library symbols with our initial, fixed electrical behaviours.
/// Symbols remain in the standard libraries. Future behaviour choices belong in this module.
BUILTIN_BEHAVIOR FindBuiltinBehavior( std::string_view aLibraryId );
}
