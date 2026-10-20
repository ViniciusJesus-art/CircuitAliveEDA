/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kiid.h>
#include <math/vector2d.h>
#include <tool/tool_action.h>
#include <wx/string.h>
#include <memory>
#include <vector>

class SCH_EDIT_FRAME;
class SCH_SHEET_PATH;
class SCH_SYMBOL;
class TOOL_MANAGER;

namespace CIRCUITALIVE
{
struct USED_SYMBOL_ENTRY
{
    wxString key;
    wxString label;
    KIID symbol;
};

std::vector<USED_SYMBOL_ENTRY> BuildUsedSymbolList( SCH_SHEET_PATH& aSheet );
std::unique_ptr<SCH_SYMBOL> CopyUsedSymbol( const SCH_SYMBOL& aSource,
                                           SCH_SHEET_PATH& aSheet, const VECTOR2I& aPosition );

struct USED_SYMBOL_ACTIONS
{
    static TOOL_ACTION showPanel;
};

void RegisterUsedSymbols( TOOL_MANAGER* aManager );
void InstallUsedSymbols( SCH_EDIT_FRAME* aFrame );
void RestoreUsedSymbolsVisibility( SCH_EDIT_FRAME* aFrame );
void ShutdownUsedSymbols( SCH_EDIT_FRAME* aFrame );
void RebindUsedSymbols( SCH_EDIT_FRAME* aFrame );
bool BeginUsedSymbolPlacement( SCH_EDIT_FRAME* aFrame, const VECTOR2I& aPosition );
// Return true when cancelling our preview while retaining the palette choice.
bool CancelUsedSymbolPlacement( SCH_EDIT_FRAME* aFrame, const SCH_SYMBOL* aPreview = nullptr );
void FinishUsedSymbolPlacement( SCH_EDIT_FRAME* aFrame, const SCH_SYMBOL* aPreview );
bool UsedSymbolPlacementOverExisting( SCH_EDIT_FRAME* aFrame, const SCH_SYMBOL* aSymbol,
                                       const VECTOR2I& aPosition );
}
