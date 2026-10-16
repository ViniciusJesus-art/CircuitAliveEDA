/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "measurements.h"
#include <math/vector2d.h>
#include <memory>

class SCH_EDIT_FRAME;
class SCH_SCREEN;
class SCH_SYMBOL;
class SCH_SHEET_PATH;
class SCH_LINE;
namespace CIRCUITALIVE
{
struct PROBE_PLACEMENT
{
    SCH_LINE* wire = nullptr;
    VECTOR2I anchor, first, second;
    bool vertical = false;
};

PROBE_PLACEMENT PlanProbePlacement( SCH_SCREEN& aScreen, const VECTOR2I& aPosition,
                                   int aTolerance, PROBE_KIND aKind );
std::unique_ptr<SCH_SYMBOL> MakeProbeSymbol( PROBE_KIND aKind, SCH_SHEET_PATH& aSheet,
        const VECTOR2I& aPosition, bool aVertical, std::size_t aSequence, const std::string& aName );
void PlaceProbe( SCH_EDIT_FRAME& aFrame, PROBE_KIND aKind, const std::string& aName,
                 const VECTOR2I& aPosition, int aTolerance );
}
