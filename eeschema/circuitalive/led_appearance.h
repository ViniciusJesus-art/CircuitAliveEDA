/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <array>
#include <map>
#include <string>
namespace CIRCUITALIVE
{
struct LED_APPEARANCE_PARAMETERS
{
    // R, G, B (0..255), intensity (%), circle diameter (mm), halo diameter (mm), opacity (%).
    std::array<std::string,7> ids{};
    bool Configurable() const { return !ids[0].empty(); }
};
inline constexpr std::array<const char*,7> LED_APPEARANCE_KEYS{
    "red_parameter", "green_parameter", "blue_parameter", "intensity_parameter",
    "diameter_parameter", "halo_diameter_parameter", "opacity_parameter" };
inline constexpr std::array<double,7> LED_APPEARANCE_LIMITS{255,255,255,100,50,100,100};
struct LED_APPEARANCE
{
    double red=1, green=66./255, blue=5./255;
    double intensity=1, diameterMm=3.4, haloDiameterMm=4.6, opacity=.8;
};
struct LED_DISPLAY
{
    double level, red, green, blue, fillOpacity, strokeOpacity, haloOpacity;
};
LED_APPEARANCE ResolveLedAppearance( const LED_APPEARANCE_PARAMETERS&, const std::map<std::string,double>& );
LED_DISPLAY LedDisplay( double aBrightness, const LED_APPEARANCE& );
}
