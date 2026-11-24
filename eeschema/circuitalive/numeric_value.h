/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <charconv>
#include <string>

namespace CIRCUITALIVE
{
inline std::string FormatSimulationNumber( double value )
{
    char buffer[64];
    const auto result = std::to_chars( buffer, buffer + sizeof( buffer ), value,
                                       std::chars_format::general, 12 );
    return { buffer, result.ptr };
}
}
