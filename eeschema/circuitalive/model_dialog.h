/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "circuit_snapshot.h"

class wxWindow;
namespace CIRCUITALIVE
{
/// Edits a copy; the caller owns the schematic commit/undo boundary.
bool EditSimulationModel( wxWindow* aParent, CIRCUIT_COMPONENT& aComponent );
}
