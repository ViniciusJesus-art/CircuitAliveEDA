/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <lib_id.h>
#include <math/vector2d.h>
#include <tool/tool_action.h>
#include <vector>

class BOARD;
class NETLIST;
class PCB_EDIT_FRAME;
class TOOL_MANAGER;

namespace CIRCUITALIVE
{
struct PENDING_FOOTPRINT
{
    wxString reference, value, key;
    LIB_ID footprint;
};

std::vector<PENDING_FOOTPRINT> BuildPendingFootprints( BOARD& aBoard, NETLIST& aNetlist );
struct PENDING_FOOTPRINT_ACTIONS
{
    static TOOL_ACTION showPanel;
    static TOOL_ACTION place;
};
void RegisterPendingFootprints( TOOL_MANAGER* aManager );
void InstallPendingFootprints( PCB_EDIT_FRAME* aFrame );
void SavePendingFootprints( PCB_EDIT_FRAME* aFrame );
void ShutdownPendingFootprints( PCB_EDIT_FRAME* aFrame );
bool BeginPendingFootprint( PCB_EDIT_FRAME* aFrame, const VECTOR2I& aPosition );
}
