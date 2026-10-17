/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kicommon.h>

namespace CIRCUITALIVE
{
// Call after common settings are loaded, before creating the GUI's first window.
KICOMMON_API void InitializeDesktopIdentity();
}
