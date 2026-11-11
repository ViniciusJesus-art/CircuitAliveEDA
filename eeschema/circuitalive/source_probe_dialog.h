/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "source_probe.h"
class wxWindow;
namespace CIRCUITALIVE
{
bool EditSourceProbe( wxWindow* aParent, PROBE_KIND aKind, SOURCE_PROBE_SETTINGS& aSettings,
                      std::string& aName );
}
