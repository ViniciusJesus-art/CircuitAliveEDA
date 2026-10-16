/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <sim/interactive_sim_session.h>
#include <map>
#include <string>
#include <vector>

namespace CIRCUITALIVE
{
// Value objects only: no KiCad, wx, or schematic pointers cross the control-thread boundary.
struct CIRCUIT_COMPONENT
{
    std::string reference;
    std::string libraryId;
    std::string value;
    std::map<std::string, std::string> pins; // physical pin number -> connectivity graph net
    bool explicitSpiceModel = false;
    std::string deviceHint = {}; // KiCad's Sim.Device, including passives with project-local IDs.
};

struct CIRCUIT_INDICATOR
{
    std::size_t component;
    std::size_t signal;
};

struct CIRCUIT_SWITCH
{
    std::size_t component;
    std::string controlSource;
};

struct CIRCUIT_SNAPSHOT
{
    std::string netlist;
    SIM_SESSION_CONFIG config;
    std::vector<CIRCUIT_INDICATOR> leds;
    std::vector<CIRCUIT_SWITCH> switches;
    std::vector<std::map<std::string, std::string>> pinNodes; // Physical pins -> solver nodes, including open pins.
    std::vector<std::string> automaticReferences; // Source reference + negative pin number.
    std::vector<std::string> floatingIslands;     // Representative component; no source or explicit ground.
    std::size_t openPins = 0;
};

/// Standard symbols use the initial catalogue; R/C always use Value and their fixed ideal behaviour.
/// Unknown symbols and unsupported custom models on other devices remain errors.
CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents );
double ParseComponentValue( const std::string& aValue );
double LedBrightness( double aForwardCurrent );
}
