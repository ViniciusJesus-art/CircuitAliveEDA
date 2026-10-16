/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "measurements.h"

namespace CIRCUITALIVE
{
enum class LOGIC_LEVEL { LOW, HIGH, HIGH_Z, UNKNOWN };
struct LOGIC_PIN
{
    std::size_t component = 0;
    std::string pin, reference;
    std::size_t voltage = NO_SIGNAL, ground = NO_SIGNAL, vcc = NO_SIGNAL;
    std::size_t driverValue = NO_SIGNAL;
    double low = .3, high = .7, minSupply = .1, maxSupply = 1e6;
    bool digital = false, output = false, externalDrive = true;
    std::vector<std::size_t> enables;
};
LOGIC_LEVEL PinLogicLevel( const LOGIC_PIN& aPin, const SIM_SAMPLE& aSample );
}
