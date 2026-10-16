/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "schematic_adapter.h"
#include "session_runner.h"
#include <view/view_overlay.h>

namespace CIRCUITALIVE
{
class SIMULATION_OVERLAY : public KIGFX::VIEW_OVERLAY
{
public:
    void Render( const SCHEMATIC_SNAPSHOT& aCircuit, const EDITOR_SESSION_VIEW& aSession,
                 const wxString& aCurrentSheet );
    int SwitchAt( const SCHEMATIC_SNAPSHOT& aCircuit, const wxString& aCurrentSheet,
                  const VECTOR2I& aPosition ) const;
};
}
