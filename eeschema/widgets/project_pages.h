/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <wx/string.h>

class SCHEMATIC;
class SCH_SHEET_PATH;
class SCH_EDIT_FRAME;

namespace CIRCUITALIVE
{
bool CanDeleteProjectPage( const SCHEMATIC& aSchematic, const SCH_SHEET_PATH& aPage );
// Release canvas/undo references first. File deletion affects only sheets
// no longer referenced by any surviving page; failed staging leaves the hierarchy intact.
bool RemoveProjectPage( SCHEMATIC& aSchematic, const SCH_SHEET_PATH& aPage );
void DeleteCurrentPage( SCH_EDIT_FRAME& aFrame );
}
