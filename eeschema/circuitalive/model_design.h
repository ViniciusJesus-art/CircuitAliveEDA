/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_model.h"
namespace CIRCUITALIVE
{
struct DESIGN_POINT
{
    int x = 0, y = 0;
    bool operator==( const DESIGN_POINT& ) const = default;
};
struct DESIGN_WIRE_ROUTE
{
    std::string fromBlock, fromPort, toBlock, toPort;
    std::vector<DESIGN_POINT> bends;
};
struct DESIGN_BLOCK
{
    std::string id, modelJson;
    std::map<std::string, std::string> connections; // @port = external terminal; other identifiers = internal nets.
    std::map<std::string, std::string> values;      // @parameter = public parameter, otherwise an SI value.
    int x = 280, y = 80;
};
struct MODEL_DESIGN
{
    std::string id = "my_model", name = "My simulation model", description, libraryId;
    std::vector<MODEL_PIN> pins;
    std::vector<MODEL_PARAMETER> parameters;
    std::vector<DESIGN_BLOCK> blocks;
    std::vector<DESIGN_WIRE_ROUTE> wireRoutes; // Drawing only; connectivity remains in block.connections.
    std::string baseJson; // Preserves an existing SPICE/behavioral core and its metadata.
    std::string coreBlock; // Block owning the imported component's observations and controls.
    static MODEL_DESIGN Open( const std::string& aJson );
    std::string Export() const;
    // Expose an imported electrical core as a wired, parameter-bound block.
    // Keeps indicators, controls and component metadata on the outer model.
    void MakeCoreVisible();
    void RemoveBlock( const std::string& aId );
    // Joining existing nets updates all connections. External terminals stay distinct.
    void Connect( const std::string& aBlock, const std::string& aPort,
                  const std::string& aOtherBlock, const std::string& aOtherPort );
    static std::vector<std::filesystem::path> BlockDirectories();
};
std::string ModelBehaviorSummary( const std::string& aModelJson );
}
