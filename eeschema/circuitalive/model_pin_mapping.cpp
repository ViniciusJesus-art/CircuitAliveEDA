/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_pin_mapping.h"
#include <set>

namespace CIRCUITALIVE
{
std::map<std::string, std::string> ModelPinMapping( const SIMULATION_MODEL& model,
                                                 const CIRCUIT_COMPONENT& component )
{
    std::map<std::string, std::string> mapping;
    std::set<std::string> used;
    for( const auto& pin : model.pins )
    {
        auto& physical = mapping[pin.id];
        const auto saved = component.modelPins.find( pin.id );
        if( saved != component.modelPins.end() && component.pins.contains( saved->second )
            && used.insert( saved->second ).second )
            physical = saved->second;
    }
    for( const auto& pin : model.pins )
        if( !component.modelPins.contains( pin.id ) && component.pins.contains( pin.defaultPin )
            && used.insert( pin.defaultPin ).second )
            mapping.at( pin.id ) = pin.defaultPin;
    return mapping;
}

void AssignModelPin( std::map<std::string, std::string>& mapping,
                     const std::string& port, const std::string& physical )
{
    auto& current = mapping.at( port );
    if( !physical.empty() )
        for( auto& [other, assigned] : mapping )
            if( other != port && assigned == physical )
            {
                assigned = current;
                break;
            }
    current = physical;
}
}
