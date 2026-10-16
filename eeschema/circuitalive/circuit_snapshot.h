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
};

/// Small, explicit stage-2 catalogue. Unknown/custom models are errors, never substituted.
CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents );
double ParseComponentValue( const std::string& aValue );
double LedBrightness( double aForwardCurrent );
}
