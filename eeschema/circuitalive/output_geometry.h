/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "luminous_geometry.h"
#include <view/view_overlay.h>

namespace CIRCUITALIVE
{
inline void DrawLuminousSegment(KIGFX::VIEW_OVERLAY& overlay,const BOX2I& body,
        const MODEL_SEGMENT& segment,double level,VECTOR2I axisX={1,0},VECTOR2I axisY={0,1})
{
    if(!segment.visual.visible)return;
    const auto g=LuminousGeometry(body,segment,axisX,axisY);
    const double intensity=.10+.90*std::sqrt(std::clamp(level,0.0,1.0));
    const KIGFX::COLOR4D color(segment.red*intensity,segment.green*intensity,segment.blue*intensity,1);
    overlay.SetIsStroke(true);overlay.SetStrokeColor(color);overlay.SetIsFill(false);overlay.SetLineWidth(g.thickness);
    if(g.circle)
    {
        overlay.SetIsFill(g.filled);overlay.SetFillColor(color);
        if(g.filled)overlay.SetLineWidth(0);
        overlay.Circle((g.start+g.end)/2,(g.end-g.start).EuclideanNorm()/2);
    }
    else overlay.Line(g.start,g.end);
    overlay.SetIsFill(false);
}
}
