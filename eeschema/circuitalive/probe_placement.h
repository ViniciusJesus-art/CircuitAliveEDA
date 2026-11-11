/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "source_probe.h"
#include <math/vector2d.h>
#include <memory>
#include <set>

class SCH_EDIT_FRAME;
class SCH_SCREEN;
class SCH_SYMBOL;
class SCH_SHEET_PATH;
class SCH_LINE;
class SCH_PIN;
class SCHEMATIC;
class SCH_COMMIT;
namespace CIRCUITALIVE
{
struct PROBE_PLACEMENT
{
    SCH_LINE* wire = nullptr;
    SCH_PIN* pin = nullptr;
    VECTOR2I anchor, first, second;
    bool vertical = false;
};

PROBE_PLACEMENT PlanProbePlacement( SCH_SCREEN& aScreen, const VECTOR2I& aPosition,
                                   int aTolerance, PROBE_KIND aKind,
                                   const SCH_SHEET_PATH* aSheet = nullptr, bool aAllowFloating = false );
std::unique_ptr<SCH_SYMBOL> MakeProbeSymbol( PROBE_KIND aKind, SCH_SHEET_PATH& aSheet,
        const VECTOR2I& aPosition, bool aVertical, std::size_t aSequence, const std::string& aName,
        const SOURCE_PROBE_SETTINGS& aSettings = {} );
std::size_t NextProbeSequence( SCHEMATIC& aSchematic );
// Pasted/repeated probes always receive fresh identities. Other malformed or
// duplicated probe identities are repaired without touching ordinary symbols.
void EnsureUniqueProbeIdentities( SCHEMATIC& aSchematic,
        const std::set<SCH_SYMBOL*>& aNewSymbols = {}, SCH_COMMIT* aCommit = nullptr );
void NormalizeSourceProbeSymbols( SCHEMATIC& aSchematic );
// Preserve manual rotation; align a two-terminal series sensor to the wire it cuts.
void AlignProbeForPlacement( SCH_SYMBOL& aSymbol, const PROBE_PLACEMENT& aPlan );
SOURCE_PROBE_SETTINGS ReadSourceProbeSettings( const SCH_SYMBOL& aSymbol );
void WriteSourceProbeSettings( SCH_SYMBOL& aSymbol, PROBE_KIND aKind,
                               const SOURCE_PROBE_SETTINGS& aSettings );
void PlaceProbe( SCH_EDIT_FRAME& aFrame, PROBE_KIND aKind, const std::string& aName,
                 const VECTOR2I& aPosition, int aTolerance, const SOURCE_PROBE_SETTINGS& aSettings = {},
                 bool aAllowFloating = false, const SCH_SYMBOL* aPreview = nullptr );
}
