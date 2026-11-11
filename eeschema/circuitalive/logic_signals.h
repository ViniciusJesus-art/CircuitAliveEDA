/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "measurements.h"

namespace CIRCUITALIVE
{
enum class LOGIC_LEVEL { LOW, HIGH, HIGH_Z, UNKNOWN };
struct LOGIC_DRIVER_STATE
{
    std::size_t enable = NO_SIGNAL, value = NO_SIGNAL;
};
struct LOGIC_PIN
{
    std::size_t component = 0;
    std::string pin, reference;
    std::size_t voltage = NO_SIGNAL, ground = NO_SIGNAL, vcc = NO_SIGNAL;
    std::size_t driverValue = NO_SIGNAL;
    std::size_t driverEnable = NO_SIGNAL;
    double low = .3, high = .7, minSupply = .1, maxSupply = 1e6;
    bool digital = false, output = false, externalDrive = true;
    std::vector<std::size_t> enables;
    std::vector<LOGIC_DRIVER_STATE> drivers;
    double thresholdReference = 0; // Zero follows VCC; positive values give absolute input thresholds.
    bool observer = false; // Excluded symbol: reads the net, never supplies or drives it.
};
LOGIC_LEVEL PinLogicLevel( const LOGIC_PIN& aPin, const SIM_SAMPLE& aSample );
/// The local output stage can be Z while PinLogicLevel reads another device's 0/1.
LOGIC_LEVEL PinDriveLevel( const LOGIC_PIN& aPin, const SIM_SAMPLE& aSample );

struct NET_DRIVER
{
    std::string reference, pin;
    std::size_t current = NO_SIGNAL, voltage = NO_SIGNAL;
    LOGIC_DRIVER_STATE state;
    // Firmware GPIO weak pull-ups are bias resistors, not push-pull drivers.
    bool programmable = false;
    std::size_t ground = NO_SIGNAL, vcc = NO_SIGNAL;
};
struct DRIVEN_NET
{
    std::vector<std::string> nodes; // Includes both sides of ideal inline sensors.
    std::vector<NET_DRIVER> drivers;
};
/// Returns the opposing, actively sourcing/sinking output stages, using actual current.
std::vector<std::size_t> ContendingDrivers( const DRIVEN_NET& aNet, const SIM_SAMPLE& aSample );
}
