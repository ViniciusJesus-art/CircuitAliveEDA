/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sim/memory_image.h>
#include <memory>
class wxWindow;
class wxFrame;
namespace CIRCUITALIVE
{
class SESSION_RUNNER;
wxFrame* ShowMemoryViewer( wxWindow*, const std::string& reference, const std::vector<MEMORY_REGION_CONFIG>& regions,
                          std::weak_ptr<SESSION_RUNNER> runner = {}, const std::vector<std::size_t>& slots = {} );
} // namespace CIRCUITALIVE
