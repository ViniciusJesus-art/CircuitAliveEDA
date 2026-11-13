/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "circuit_snapshot.h"
#include "simulation_model.h"

namespace CIRCUITALIVE
{
/// Keep saved connections first; defaults only fill valid, unused physical pins.
std::map<std::string, std::string> ModelPinMapping( const SIMULATION_MODEL& aModel,
                                                 const CIRCUIT_COMPONENT& aComponent );

/// Reassigning an occupied pin swaps the two connections, preserving a one-to-one map.
void AssignModelPin( std::map<std::string, std::string>& aMapping,
                     const std::string& aPort, const std::string& aPhysicalPin );
}
