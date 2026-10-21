/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "interactive_sim_session.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace CIRCUITALIVE::PROTOCOL
{
using JSON = nlohmann::json;
constexpr std::size_t MAX_MESSAGE = 8 * 1024 * 1024;
constexpr int VERSION = 3;

inline JSON SerializeProgrammableDevice( const PROGRAMMABLE_DEVICE_CONFIG& config )
{
    JSON bindings = JSON::array();
    for( const auto& p : config.bindings )
        bindings.push_back({{"id",p.id},{"signal",p.signal},{"high",p.highSource},
            {"low",p.lowSource},{"pullup",p.pullupSource},{"driver_slot",p.driverSlot}});
    return {{"id",config.id},{"engine",config.engine},{"device",config.device},
        {"source",config.source},{"clock_hz",config.clockHz},{"quantum",config.quantumSeconds},
        {"firmware",MemoryHex(config.firmware)},{"eeprom",MemoryHex(config.eeprom)},
        {"options",config.optionsJson},{"pins",config.pins},{"bindings",bindings},
        {"autonomous_window",config.autonomousWindowSeconds}};
}

inline PROGRAMMABLE_DEVICE_CONFIG DeserializeProgrammableDevice( const JSON& json )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id=json.at("id");config.engine=json.at("engine");config.device=json.at("device");
    config.source=json.at("source");config.clockHz=json.at("clock_hz");config.quantumSeconds=json.at("quantum");
    config.autonomousWindowSeconds=json.value("autonomous_window",0.0);
    config.firmware=MemoryUnhex(json.at("firmware"));config.eeprom=MemoryUnhex(json.at("eeprom"));
    config.optionsJson=json.at("options");config.pins=json.at("pins").get<std::vector<std::string>>();
    for( const auto& p : json.at("bindings") )
        config.bindings.push_back({p.at("id"),p.at("signal"),p.at("high"),p.at("low"),p.at("pullup"),p.value("driver_slot",-1)});
    return config;
}

inline const char* StateName( SIM_SESSION_STATE aState )
{
    switch( aState )
    {
    case SIM_SESSION_STATE::STOPPED: return "stopped";
    case SIM_SESSION_STATE::RUNNING: return "running";
    case SIM_SESSION_STATE::PAUSED: return "paused";
    case SIM_SESSION_STATE::COMPLETED: return "completed";
    case SIM_SESSION_STATE::FAILED: return "error";
    }
    throw std::runtime_error( "Invalid simulation state" );
}

inline SIM_SESSION_STATE ParseState( const std::string& aState )
{
    for( auto state : { SIM_SESSION_STATE::STOPPED, SIM_SESSION_STATE::RUNNING,
                       SIM_SESSION_STATE::PAUSED, SIM_SESSION_STATE::COMPLETED,
                       SIM_SESSION_STATE::FAILED } )
    {
        if( aState == StateName( state ) )
            return state;
    }
    throw std::runtime_error( "Invalid worker state" );
}
} // namespace CIRCUITALIVE::PROTOCOL
