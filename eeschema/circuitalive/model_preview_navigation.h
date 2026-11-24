/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <class_draw_panel_gal.h>
#include <view/view_controls.h>

namespace CIRCUITALIVE
{
inline void ConfigureModelPreviewNavigation( EDA_DRAW_PANEL_GAL* canvas )
{
    auto* controls = canvas->GetViewControls();
    auto settings = controls->GetSettings();
    settings.m_warpCursor = false;
    settings.m_scrollModifierZoom = 0;
    settings.m_dragMiddle = MOUSE_DRAG_ACTION::PAN;
    controls->ApplySettings( settings );
}
}
