/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "interactive_sim_session.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace CIRCUITALIVE::PROTOCOL
{
using JSON = nlohmann::json;
constexpr std::size_t MAX_MESSAGE = 8 * 1024 * 1024;
constexpr int VERSION = 2;

inline const char* StateName( SIM_SESSION_STATE aState )
{
    switch( aState )
    {
    case SIM_SESSION_STATE::STOPPED: return "stopped";
    case SIM_SESSION_STATE::RUNNING: return "running";
    case SIM_SESSION_STATE::PAUSED: return "paused";
    case SIM_SESSION_STATE::COMPLETED: return "completed";
    case SIM_SESSION_STATE::ERROR: return "error";
    }
    throw std::runtime_error( "Invalid simulation state" );
}

inline SIM_SESSION_STATE ParseState( const std::string& aState )
{
    for( auto state : { SIM_SESSION_STATE::STOPPED, SIM_SESSION_STATE::RUNNING,
                       SIM_SESSION_STATE::PAUSED, SIM_SESSION_STATE::COMPLETED,
                       SIM_SESSION_STATE::ERROR } )
    {
        if( aState == StateName( state ) )
            return state;
    }
    throw std::runtime_error( "Invalid worker state" );
}
} // namespace CIRCUITALIVE::PROTOCOL
