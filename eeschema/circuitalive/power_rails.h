/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace CIRCUITALIVE
{
/// Captured native connectivity, never inferred from the position or rotation of the drawing.
struct POWER_SYMBOL
{
    std::string reference, value, net;
};
struct POWER_RAIL
{
    std::string reference, name, net;
    double voltage;
};
bool IsSimulationGround( const std::string& aName );
std::optional<double> PowerSymbolVoltage( const std::string& aValue );
std::vector<POWER_RAIL> CollectPowerRails( const std::vector<POWER_SYMBOL>& aSymbols );

/// Potentials constrained by ideal DC sources/sensors. Avoid redundant automatic sources
/// and reject conflicting voltage loops before handing the deck to ngspice.
class VOLTAGE_CONSTRAINTS
{
public:
    bool Add( const std::string& aPositive, const std::string& aNegative, double aVoltage,
              const std::string& aOrigin );
    std::optional<double> Difference( const std::string& aPositive, const std::string& aNegative );
private:
    struct NODE { std::size_t parent; double offset = 0; std::string origin; };
    std::map<std::string, std::size_t> m_ids;
    std::vector<NODE> m_nodes;
    std::pair<std::size_t, double> Find( std::size_t aNode );
    std::size_t Node( const std::string& aName );
};
}
