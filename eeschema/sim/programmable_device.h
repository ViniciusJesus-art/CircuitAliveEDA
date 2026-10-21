/*
 * CircuitAliveEDA programmable device runtime.
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CIRCUITALIVE
{

/** Electrical bridge owned by the solver; the script sees only the pin ID. */
struct PROGRAMMABLE_PIN_BINDING
{
    std::string id;
    std::string signal;
    std::string highSource;
    std::string lowSource;
    std::string pullupSource;
    int driverSlot = -1;
};

/** A frozen, architecture-independent package. No runtime file access is needed. */
struct PROGRAMMABLE_DEVICE_CONFIG
{
    std::string id;
    std::string engine = "javascript";
    std::string device;
    std::string source;
    double clockHz = 16000000.0;
    std::vector<std::uint8_t> firmware;
    std::vector<std::uint8_t> eeprom;
    std::string optionsJson = "{}";
    std::vector<std::string> pins;
    std::vector<PROGRAMMABLE_PIN_BINDING> bindings;
    /// Maximum interval between electrical input samples; independent of GUI refresh.
    double quantumSeconds = 1e-6;
    /// Cooperative lookahead with fixed supply/reset; yield before observing inputs.
    double autonomousWindowSeconds = 0;
};

struct PROGRAMMABLE_PIN_INPUT
{
    std::string device;
    std::string pin;
    double voltage = 0.0;
};

enum class PROGRAMMABLE_DRIVE : int
{
    HIGH_IMPEDANCE = 0,
    LOW = 1,
    HIGH = 2,
    PULL_UP = 3
};

/** A pin drive transition, stamped in seconds of simulated time. */
struct PROGRAMMABLE_PIN_STATE
{
    std::string device;
    std::string pin;
    PROGRAMMABLE_DRIVE drive = PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE;
    double time = 0.0;
};

} // namespace CIRCUITALIVE
