/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "component_catalog.h"
#include <utility>

namespace CIRCUITALIVE
{
BUILTIN_BEHAVIOR FindBuiltinBehavior( std::string_view aLibraryId )
{
    static constexpr std::pair<std::string_view, BUILTIN_BEHAVIOR> catalogue[] = {
        { "Device:R", BUILTIN_BEHAVIOR::RESISTOR },
        { "Device:R_Small", BUILTIN_BEHAVIOR::RESISTOR },
        { "Device:R_US", BUILTIN_BEHAVIOR::RESISTOR },
        { "Device:C", BUILTIN_BEHAVIOR::CAPACITOR },
        { "Device:C_Small", BUILTIN_BEHAVIOR::CAPACITOR },
        { "Device:C_US", BUILTIN_BEHAVIOR::CAPACITOR },
        { "Device:C_Polarized", BUILTIN_BEHAVIOR::CAPACITOR },
        { "Device:C_Polarized_Small", BUILTIN_BEHAVIOR::CAPACITOR },
        { "Device:C_Polarized_US", BUILTIN_BEHAVIOR::CAPACITOR },
        { "Simulation_SPICE:VDC", BUILTIN_BEHAVIOR::DC_VOLTAGE },
        { "Device:Battery", BUILTIN_BEHAVIOR::DC_VOLTAGE },
        { "Device:Battery_Cell", BUILTIN_BEHAVIOR::DC_VOLTAGE },
        { "Device:LED", BUILTIN_BEHAVIOR::RED_LED },
        { "Device:LED_Small", BUILTIN_BEHAVIOR::RED_LED },
        { "Switch:SW_SPST", BUILTIN_BEHAVIOR::SWITCH }
    };
    for( const auto& [id, behavior] : catalogue )
        if( id == aLibraryId )
            return behavior;
    return BUILTIN_BEHAVIOR::UNKNOWN;
}
}
