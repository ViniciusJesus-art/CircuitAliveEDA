/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "control_visual.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace CIRCUITALIVE
{
inline CONTROL_VISUAL ReadControlVisual( const nlohmann::json& json )
{
    CONTROL_VISUAL v;
    v.visible = json.value( "visible", true );
    const auto target = json.value( "target", "symbol" );
    if( target == "symbol" ) return v;
    if( target != "rectangle" ) throw std::runtime_error( "Unknown interaction target" );
    v.custom = true;
    v.x = json.at( "x" ); v.y = json.at( "y" );
    v.width = json.at( "width" ); v.height = json.at( "height" );
    if( !std::isfinite( v.x ) || !std::isfinite( v.y ) || !std::isfinite( v.width )
        || !std::isfinite( v.height ) || std::abs( v.x ) > 10 || std::abs( v.y ) > 10
        || v.width < .05 || v.height < .05 || v.width > 10 || v.height > 10 )
        throw std::runtime_error( "Invalid interaction rectangle" );
    return v;
}
inline nlohmann::json WriteControlVisual( const CONTROL_VISUAL& v )
{
    if( !v.custom ) return { { "target", "symbol" }, { "visible", v.visible } };
    return { { "target", "rectangle" }, { "x", v.x }, { "y", v.y },
             { "width", v.width }, { "height", v.height }, { "visible", v.visible } };
}
}
