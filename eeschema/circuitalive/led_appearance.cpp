/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "led_appearance.h"
#include <algorithm>
namespace CIRCUITALIVE
{
LED_APPEARANCE ResolveLedAppearance( const LED_APPEARANCE_PARAMETERS& binding, const std::map<std::string,double>& values )
{
    LED_APPEARANCE appearance;
    double* fields[]={&appearance.red,&appearance.green,&appearance.blue,&appearance.intensity,
        &appearance.diameterMm,&appearance.haloDiameterMm,&appearance.opacity};
    constexpr double scales[]={255,255,255,100,1,1,100};
    for(std::size_t i=0;i<binding.ids.size();++i)
        if(!binding.ids[i].empty())*fields[i]=values.at(binding.ids[i])/scales[i];
    return appearance;
}
LED_DISPLAY LedDisplay( double brightness, const LED_APPEARANCE& appearance )
{
    const double level=std::clamp(brightness*appearance.intensity,0.,1.);
    const double shade=.25+.75*level;
    return {level,appearance.red*shade,appearance.green*shade,appearance.blue*shade,
        appearance.opacity*(.375+.625*level),appearance.opacity*(.4+.6*level),appearance.opacity*.75*level};
}
}
