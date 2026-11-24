/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace CIRCUITALIVE
{
inline std::string DisplayPresentationKey( const std::string& presentation )
{
    try
    {
        auto state = nlohmann::json::parse( presentation );
        // The write/read address moves during firmware transactions, but
        // affects drawing only when the cursor or blinking block is visible.
        if( !state.value( "cursor", false ) && !state.value( "blink", false ) )
            state.erase( "address" );
        return state.dump();
    }
    catch( const std::exception& ) { return presentation; }
}
}
