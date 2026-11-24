/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_model.h"
#include "segment_json.h"
#include <algorithm>

namespace CIRCUITALIVE
{
inline void ApplySymbolParameterDefaults( std::vector<MODEL_PARAMETER>& parameters,
                                         const nlohmann::json& variant )
{
    const auto defaults=variant.value("parameter_defaults",std::map<std::string,std::string>{});
    for(const auto& [id,text]:defaults)
    {
        auto parameter=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==id;});
        if(parameter==parameters.end())throw std::runtime_error("Unknown symbol parameter: "+id);
        if(text!="$value")
        {
            const auto value=ParseComponentValue(text);
            if(value<parameter->minimum||value>parameter->maximum)
                throw std::runtime_error("Parameter default outside limits: "+id);
        }
        parameter->defaultValue=text;
    }
}
inline nlohmann::json SegmentPresentation( const nlohmann::json& segment )
{
    auto result=nlohmann::json::object();
    for(const char* key:{"shape","visual","geometry","red","green","blue"})
        if(segment.contains(key))result[key]=segment[key];
    return result;
}
inline void ApplySegmentPresentation( MODEL_SEGMENT& segment,const nlohmann::json& presentation )
{
    auto merged=WriteLuminousSegment(segment);
    // A legacy rectangle must also be able to replace an explicit endpoint layout.
    if(presentation.contains("shape")&&!presentation.contains("geometry"))merged.erase("geometry");
    merged.update(SegmentPresentation(presentation));segment=ReadLuminousSegment(merged);
    for(double color:{segment.red,segment.green,segment.blue})
        if(!std::isfinite(color)||color<0||color>1)throw std::runtime_error("Invalid symbol segment color");
}
}
