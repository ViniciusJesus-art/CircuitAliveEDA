/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "model_design.h"
#include <nlohmann/json.hpp>
#include <set>

namespace CIRCUITALIVE
{
struct CIRCUIT_SNAPSHOT;
// Resolves authoring references and emits paused-time changes for native SPICE parameters.
class CONTROL_REFERENCE_COMPILER
{
public:
    CONTROL_REFERENCE_COMPILER( const MODEL_DESIGN&, const std::vector<SIMULATION_MODEL>& );
    std::string Placeholder( const std::string&, const MODEL_PARAMETER& );
    void Resolve( std::vector<std::map<std::string,std::string>>&, nlohmann::json& );
    std::string CompileLine( const std::string& aStatic, const std::string& aDynamic,
                             bool aDefinition, nlohmann::json& );
    std::map<std::string,std::string> Expressions( const std::map<std::string,std::string>&, std::size_t ) const;
    void CopyNestedUpdates( const SIMULATION_MODEL&, const std::map<std::string,std::string>&, nlohmann::json& ) const;
private:
    struct REFERENCE
    {
        std::string block, control, initial, rawInitial, expression, source;
        double minimum = 0, maximum = 1;
        bool slider = true;
    };
    const MODEL_DESIGN& m_design;
    std::vector<REFERENCE> m_refs;
    std::map<std::string,std::vector<std::size_t>> m_names;
    std::map<std::string,std::size_t> m_pending;
    std::vector<std::map<std::string,std::string>> m_expressions;
    std::set<std::size_t> m_proxies;
};
std::vector<SIM_PARAMETER_CHANGE> LiveParameterChanges( const CIRCUIT_SNAPSHOT&,
        const std::map<std::string,double>& aValues, const std::string& aSource );
std::string LiveControlText( const CIRCUIT_SNAPSHOT&, const std::vector<bool>& aSwitches,
        const std::vector<double>& aAdjustments, std::size_t aComponent, const std::string& aId,
        const std::string& aField, const std::string& aFallback );
}
