/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <tool/tool_action.h>

class SCH_EDIT_FRAME;
namespace CIRCUITALIVE
{
struct SIMULATION_ACTIONS
{
    static TOOL_ACTION play;
    static TOOL_ACTION pause;
    static TOOL_ACTION stop;
    static TOOL_ACTION configure;
    static TOOL_ACTION memory;
    static TOOL_ACTION probes;
    static TOOL_ACTION logicLevels;
};

/// Append native-sized actions to the existing main toolbar, including after customization/recreation.
void EnsureSimulationToolbar( SCH_EDIT_FRAME& aFrame );
}
