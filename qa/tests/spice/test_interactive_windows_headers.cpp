/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
// Exercise the include order used by the GUI, where wxWidgets loads the Windows SDK first.
#ifdef _WIN32
#include <windows.h>
#else
// Also catch the collision on Linux builders, without requiring a Windows SDK.
#define ERROR 0
#endif

#include <sim/interactive_sim_protocol.h>
#include <circuitalive/session_runner.h>

#ifndef ERROR
#error "The simulator headers must coexist with the Windows ERROR macro"
#endif

static_assert( CIRCUITALIVE::SIM_SESSION_STATE::FAILED
               != CIRCUITALIVE::SIM_SESSION_STATE::STOPPED );
static_assert( CIRCUITALIVE::EDITOR_SESSION_STATE::FAILED
               != CIRCUITALIVE::EDITOR_SESSION_STATE::STOPPED );
