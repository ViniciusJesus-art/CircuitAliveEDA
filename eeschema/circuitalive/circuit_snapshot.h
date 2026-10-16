/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <sim/interactive_sim_session.h>
#include "measurements.h"
#include "logic_signals.h"
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
    std::string modelFile = {}; // builtin:<id> or a project-relative/absolute .casim file.
    std::map<std::string, std::string> modelPins = {};
    std::map<std::string, std::string> modelParameters = {};
    std::string projectDirectory = {};
    std::map<std::string, std::string> legacySpiceFields = {};
    MEASUREMENT_SELECTION measurements = {};
};

struct CIRCUIT_INDICATOR
{
    std::size_t component;
    std::size_t signal;
    double brightnessCurrent = 0.01;
    double ratedCurrent = 0.02;
    std::string reference = {};
};

struct CIRCUIT_SWITCH
{
    std::size_t component;
    std::string controlSource;
    double on = 5;
    double off = 0;
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
    std::vector<MEASUREMENT_CHANNEL> measurements;
    std::vector<CURRENT_WARNING> currentWarnings;
    std::vector<LOGIC_PIN> logicPins;
};

/// Resolve model files, parameters and physical pins, then freeze a component deck.
CIRCUIT_SNAPSHOT BuildCircuitSnapshot( const std::vector<CIRCUIT_COMPONENT>& aComponents,
                                     const std::vector<CIRCUIT_PROBE>& aProbes = {},
                                     bool aPinIndicators = false );
double ParseComponentValue( const std::string& aValue );
double LedBrightness( double aForwardCurrent, double aBrightnessCurrent = 0.01 );
}
