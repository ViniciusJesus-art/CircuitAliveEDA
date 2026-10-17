/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sim/memory_image.h>
class wxWindow;
namespace CIRCUITALIVE
{
class SESSION_RUNNER;
void ShowMemoryViewer( wxWindow*, const std::string& reference, const std::vector<MEMORY_REGION_CONFIG>& regions,
                       SESSION_RUNNER* runner = nullptr, const std::vector<std::size_t>& slots = {} );
} // namespace CIRCUITALIVE
