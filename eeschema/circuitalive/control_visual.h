/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

namespace CIRCUITALIVE
{
// Fractions of the displayed symbol body. Negative positions place a control outside it.
// The default uses the entire symbol, preserving existing switch/potentiometer models.
struct CONTROL_VISUAL
{
    bool custom = false;
    double x = 0, y = 0, width = 1, height = 1;
    bool visible = true;
    bool operator==( const CONTROL_VISUAL& ) const = default;
};
}
