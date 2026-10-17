/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_model.h"
#include <optional>
class wxWindow;
namespace CIRCUITALIVE
{
std::optional<std::size_t> ChooseSimulationModel( wxWindow* aParent,
        const std::vector<SIMULATION_MODEL>& aModels, int aSelected = -1 );
}
