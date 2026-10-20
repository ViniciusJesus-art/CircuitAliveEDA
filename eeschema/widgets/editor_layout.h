/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

class TOOL_MANAGER;

namespace CIRCUITALIVE
{
// Keep the editor's global preferences current before another project is opened.
void RegisterEditorLayout( TOOL_MANAGER* aManager );
}
