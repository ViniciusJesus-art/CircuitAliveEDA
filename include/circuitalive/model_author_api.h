/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
class wxWindow;
class KIWAY;
namespace CIRCUITALIVE
{
// Private CircuitAlive extension to the existing KiCad KIFACE address interface.
inline constexpr int MODEL_AUTHOR_INTERFACE = 0x43414d41;
using MODEL_AUTHOR_ENTRY = void (*)( wxWindow*, KIWAY* );
void ShowModelAuthor( wxWindow* aParent, KIWAY* aKiway );
}
