/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sim/interactive_sim_session.h>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace CIRCUITALIVE
{
enum class PROBE_KIND { VOLTAGE, CURRENT, POWER, OSCILLOSCOPE };
enum class MEASURE_QUANTITY { VOLTAGE, CURRENT, POWER };

struct MEASUREMENT_SELECTION
{
    bool voltage = false, current = false, power = false;
    std::string positive, negative; // Logical model ports; defaults come from the model file.
    bool Any() const { return voltage || current || power; }
};

struct CIRCUIT_PROBE
{
    PROBE_KIND kind = PROBE_KIND::VOLTAGE;
    std::string name, label;
    std::map<std::string, std::string> pins;
    std::size_t sequence = 1;
};

constexpr std::size_t NO_SIGNAL = std::numeric_limits<std::size_t>::max();
struct MEASUREMENT_CHANNEL
{
    std::string name;
    MEASURE_QUANTITY quantity = MEASURE_QUANTITY::VOLTAGE;
    bool scope = false;
    std::size_t positive = NO_SIGNAL, negative = NO_SIGNAL, current = NO_SIGNAL;
    std::size_t probe = NO_SIGNAL;
    bool connected = true;
};

struct CURRENT_WARNING
{
    std::string name;
    std::size_t signal = NO_SIGNAL;
    double limit = 0;
};

std::string AlphabeticProbeName( std::size_t aSequence );
const char* QuantityUnit( MEASURE_QUANTITY aQuantity );
double MeasurementValue( const MEASUREMENT_CHANNEL& aChannel, const SIM_SAMPLE& aSample );
std::string FormatMeasurement( double aValue, MEASURE_QUANTITY aQuantity );
std::string ProbeKindName( PROBE_KIND aKind );
PROBE_KIND ParseProbeKind( const std::string& aKind );
}
