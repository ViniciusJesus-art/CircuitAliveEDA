/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
class wxWindow;
namespace CIRCUITALIVE
{
struct CIRCUIT_COMPONENT;
struct SIMULATION_MODEL;
bool EditModelResources( wxWindow*, CIRCUIT_COMPONENT&, const SIMULATION_MODEL& );
} // namespace CIRCUITALIVE
