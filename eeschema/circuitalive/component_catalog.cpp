/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "component_catalog.h"
#include <utility>

namespace CIRCUITALIVE
{
BUILTIN_BEHAVIOR FindBuiltinBehavior( std::string_view aLibraryId, std::string_view aDeviceHint )
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
    // A project may embed/copy R or C under its own library nickname. The nickname
    // is not an electrical property (e.g. rectifier_schlib:R has the same two pins).
    auto name = aLibraryId.substr( aLibraryId.find_last_of( ':' ) + 1 );
    for( const auto& [id, behavior] : catalogue )
    {
        if( ( behavior == BUILTIN_BEHAVIOR::RESISTOR || behavior == BUILTIN_BEHAVIOR::CAPACITOR )
            && name == id.substr( id.find_last_of( ':' ) + 1 ) )
            return behavior;
    }
    if( aDeviceHint == "R" ) return BUILTIN_BEHAVIOR::RESISTOR;
    if( aDeviceHint == "C" ) return BUILTIN_BEHAVIOR::CAPACITOR;
    return BUILTIN_BEHAVIOR::UNKNOWN;
}
}
