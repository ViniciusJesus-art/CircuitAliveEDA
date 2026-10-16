/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <nlohmann/json_fwd.hpp>
namespace CIRCUITALIVE
{
struct SIMULATION_MODEL;
/// Compile file-defined event blocks and electrical pin interfaces into XSPICE templates.
void CompileBehavioralModel( const nlohmann::json& aBehavior, SIMULATION_MODEL& aModel );
}
