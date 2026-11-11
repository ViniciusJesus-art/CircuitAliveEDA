/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

class SCH_EDIT_FRAME;
class TOOL_MANAGER;
class SCH_SYMBOL;

namespace CIRCUITALIVE
{
#if defined( __linux__ ) || defined( _WIN32 )
bool EditSimulationProbe( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aProbe );
void RegisterEditorSimulation( TOOL_MANAGER* aManager );
void InstallEditorSimulation( SCH_EDIT_FRAME* aFrame );
void ShutdownEditorSimulation( SCH_EDIT_FRAME* aFrame );
void InvalidateEditorSimulation( SCH_EDIT_FRAME* aFrame );
void RebindEditorSimulation( SCH_EDIT_FRAME* aFrame );
void ReloadEditorSimulationPreferences( SCH_EDIT_FRAME* aFrame );
#else
inline bool EditSimulationProbe( SCH_EDIT_FRAME*, SCH_SYMBOL* ) { return false; }
inline void RegisterEditorSimulation( TOOL_MANAGER* ) {}
inline void InstallEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void ShutdownEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void InvalidateEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void RebindEditorSimulation( SCH_EDIT_FRAME* ) {}
inline void ReloadEditorSimulationPreferences( SCH_EDIT_FRAME* ) {}
#endif
}
