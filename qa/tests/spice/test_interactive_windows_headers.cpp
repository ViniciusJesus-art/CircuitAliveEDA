/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
// Exercise the include order used by the GUI, where wxWidgets loads the Windows SDK first.
#ifdef _WIN32
#include <windows.h>
#else
// Also catch the collision on Linux builders, without requiring a Windows SDK.
#define ERROR 0
#define ReplaceFile CircuitAliveTestReplaceFileW
#endif

#include <sim/interactive_sim_protocol.h>
#include <sim/platform_runtime.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/logic_signals.h>
#include <circuitalive/scope_acquisition.h>
#include <type_traits>

#ifndef ERROR
#error "The simulator headers must coexist with the Windows ERROR macro"
#endif
#ifndef ReplaceFile
#error "The runtime headers must coexist with the Windows ReplaceFile macro"
#endif

static_assert( std::is_same_v<decltype( &CIRCUITALIVE::ReplaceSavedFile ),
               void (*)( const std::filesystem::path&, const std::filesystem::path& )> );

static_assert( CIRCUITALIVE::SIM_SESSION_STATE::FAILED
               != CIRCUITALIVE::SIM_SESSION_STATE::STOPPED );
static_assert( CIRCUITALIVE::EDITOR_SESSION_STATE::FAILED
               != CIRCUITALIVE::EDITOR_SESSION_STATE::STOPPED );
