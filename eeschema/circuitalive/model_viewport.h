/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <math/vector2d.h>
#include <algorithm>

namespace CIRCUITALIVE
{
// Screen translation is independent of document coordinates: panning never
// moves blocks or clamps the view to the positive quadrant.
struct MODEL_VIEWPORT
{
    double scale = 1;
    VECTOR2D offset = { 0, 0 };
    VECTOR2D ToWorld( VECTOR2D screen ) const { return ( screen - offset ) / scale; }
    void ZoomAt( double factor, VECTOR2D screen )
    {
        const auto anchor = ToWorld( screen );
        scale = std::clamp( scale * factor, .1, 6. );
        offset = screen - anchor * scale;
    }
};
}
