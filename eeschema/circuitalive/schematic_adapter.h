/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "circuit_snapshot.h"
#include <math/box2.h>
#include <wx/string.h>

class SCHEMATIC;
class SCH_SYMBOL;
class SCH_SHEET_PATH;
namespace CIRCUITALIVE
{
struct SYMBOL_LOCATION
{
    wxString sheet;
    wxString uuid;
    wxString reference;
    BOX2I body;
    VECTOR2I position;
};

struct SCHEMATIC_SNAPSHOT
{
    CIRCUIT_SNAPSHOT circuit;
    std::vector<SYMBOL_LOCATION> locations;
    std::vector<SYMBOL_LOCATION> probes;
};

/// The only adapter that reads KiCad symbols/connectivity. Call exclusively on the GUI thread.
SCHEMATIC_SNAPSHOT CaptureSchematic( SCHEMATIC& aSchematic );
CIRCUIT_COMPONENT ReadSymbolModelSettings( SCH_SYMBOL& aSymbol, SCH_SHEET_PATH& aSheet,
        const wxString& aVariant, const std::string& aProjectDirectory );
void WriteSymbolModelSettings( SCH_SYMBOL& aSymbol, const CIRCUIT_COMPONENT& aSettings );
}
