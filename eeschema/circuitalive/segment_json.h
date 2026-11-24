/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_outputs.h"
#include "control_visual_json.h"

namespace CIRCUITALIVE
{
inline MODEL_SEGMENT ReadLuminousSegment( const nlohmann::json& item )
{
    MODEL_SEGMENT s;
    s.id=item.at("id");s.signal=item.at("signal");s.shape=item.value("shape","horizontal");
    s.visual=ReadControlVisual(item.value("visual",nlohmann::json::object()),.002);
    s.fullScale=item.value("full_scale",.01);s.fullScaleParameter=item.value("full_scale_parameter","");
    s.red=item.value("red",1.);s.green=item.value("green",.12);s.blue=item.value("blue",.02);
    if(item.contains("geometry"))
    {
        const auto& j=item.at("geometry");const auto& start=j.at("start");const auto& end=j.at("end");
        if(!start.is_array()||!end.is_array()||start.size()!=2||end.size()!=2)
            throw std::runtime_error("Luminous geometry needs two endpoints");
        s.geometry=SEGMENT_GEOMETRY{start[0],start[1],end[0],end[1],j.at("thickness"),j.value("filled",true)};
        const auto& g=*s.geometry;
        for(double p:{g.x1,g.y1,g.x2,g.y2})
            if(!std::isfinite(p)||std::abs(p)>10)throw std::runtime_error("Invalid luminous endpoint");
        if(!std::isfinite(g.thickness)||g.thickness<=0||g.thickness>10)
            throw std::runtime_error("Invalid luminous thickness");
        if(s.shape!="line"&&s.shape!="circle")throw std::runtime_error("Unknown luminous geometry shape");
    }
    else if(s.shape!="horizontal"&&s.shape!="vertical"&&s.shape!="slash"&&s.shape!="backslash"&&s.shape!="dot")
        throw std::runtime_error("Unknown luminous shape or missing geometry");
    return s;
}
inline nlohmann::json WriteLuminousSegment( const MODEL_SEGMENT& s )
{
    nlohmann::json j={{"id",s.id},{"signal",s.signal},{"shape",s.shape},{"visual",WriteControlVisual(s.visual)},
        {"full_scale",s.fullScale},{"red",s.red},{"green",s.green},{"blue",s.blue}};
    if(!s.fullScaleParameter.empty())j["full_scale_parameter"]=s.fullScaleParameter;
    if(s.geometry)
    {
        const auto& g=*s.geometry;
        j["geometry"]={{"start",{g.x1,g.y1}},{"end",{g.x2,g.y2}},{"thickness",g.thickness},{"filled",g.filled}};
    }
    return j;
}
}
