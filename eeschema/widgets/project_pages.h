/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <wx/string.h>

class PROJECT;
class SCHEMATIC;
class SCH_SHEET_PATH;
class SCH_EDIT_FRAME;

namespace CIRCUITALIVE
{
bool IsExcludedProjectPage( PROJECT& aProject, const wxString& aFileName );
void IncludeProjectPage( PROJECT& aProject, const wxString& aFileName );
bool CanDeleteProjectPage( const SCHEMATIC& aSchematic, const SCH_SHEET_PATH& aPage );
// Detach the sheet and exclude unreferenced files from sibling discovery. The files stay
// on disk; the caller must release canvas/undo references before calling this function.
bool RemoveProjectPage( SCHEMATIC& aSchematic, const SCH_SHEET_PATH& aPage );
void DeleteCurrentPage( SCH_EDIT_FRAME& aFrame );
}
