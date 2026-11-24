/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "control_geometry.h"

namespace CIRCUITALIVE
{
// Keep the model's screen size, including its default symbol-sized rectangle.
// Instance positions may lie anywhere on the sheet, outside the symbol body.
inline CONTROL_VISUAL MoveDisplayVisual( const BOX2I& body, CONTROL_VISUAL visual,
                                        const VECTOR2D& delta )
{
    const auto box = ControlBounds( body, visual );
    const double width = std::max<double>( body.GetWidth(), schIUScale.mmToIU( 1 ) );
    const double height = std::max<double>( body.GetHeight(), schIUScale.mmToIU( 1 ) );
    visual.custom = true;
    visual.x = ( box.GetX() - body.GetX() + delta.x ) / width;
    visual.y = ( box.GetY() - body.GetY() + delta.y ) / height;
    visual.width = box.GetWidth() / width;
    visual.height = box.GetHeight() / height;
    return visual;
}
}
