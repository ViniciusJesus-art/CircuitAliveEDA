/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

class SCH_EDIT_FRAME;

// Refresh before opening the native menu, so GTK can allocate its full height.
void UpdatePagesMenu( SCH_EDIT_FRAME& aFrame );
