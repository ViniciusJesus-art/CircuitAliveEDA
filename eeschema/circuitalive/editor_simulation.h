/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

class SCH_EDIT_FRAME;
class TOOL_MANAGER;

namespace CIRCUITALIVE
{
#ifdef __linux__
void RegisterEditorSimulation( TOOL_MANAGER* aManager );
void InstallEditorSimulation( SCH_EDIT_FRAME* aFrame );
void ShutdownEditorSimulation( SCH_EDIT_FRAME* aFrame );
void InvalidateEditorSimulation( SCH_EDIT_FRAME* aFrame );
void RebindEditorSimulation( SCH_EDIT_FRAME* aFrame );
void ReloadEditorSimulationPreferences( SCH_EDIT_FRAME* aFrame );
#else
inline void RegisterEditorSimulation( TOOL_MANAGER* ) {}
inline void InstallEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void ShutdownEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void InvalidateEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void RebindEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void ReloadEditorSimulationPreferences( SCH_EDIT_FRAME* ) {}
#endif
}
