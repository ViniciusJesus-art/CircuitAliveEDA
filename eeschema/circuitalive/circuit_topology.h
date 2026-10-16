/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "circuit_snapshot.h"
#include "simulation_model.h"

namespace CIRCUITALIVE
{
struct CIRCUIT_TOPOLOGY
{
    std::vector<std::map<std::string, std::string>> nodes;
    std::vector<std::string> automaticReferences;
    std::vector<std::string> floatingIslands;
    std::size_t openPins = 0;
};

/// Electrical node mapping: explicit ground first, otherwise one source negative per island.
/// Open physical pins get distinct nodes; missing connections never short unrelated pins together.
CIRCUIT_TOPOLOGY BuildCircuitTopology( const std::vector<CIRCUIT_COMPONENT>& aComponents,
                                     const std::vector<RESOLVED_MODEL>& aModels );
}
