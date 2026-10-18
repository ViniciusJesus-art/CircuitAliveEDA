/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "model_design.h"
namespace CIRCUITALIVE
{
struct DESIGN_OBSTACLE { int x, y, width, height; };
struct DESIGN_TERMINAL
{
    std::string block, port, net;
    DESIGN_POINT position;
    int direction = 1; // +1 exits right, -1 exits left.
};
struct DESIGN_WIRE
{
    DESIGN_TERMINAL from, to;
    std::vector<DESIGN_POINT> points;
    bool manual = false;
};
struct DESIGN_JUNCTION
{
    std::string net;
    DESIGN_POINT position;
};
// Orthogonal routes with obstacle avoidance and a cost for overlapping other nets.
std::vector<DESIGN_WIRE> RouteDesignWires( const std::vector<DESIGN_TERMINAL>& aTerminals,
    const std::vector<DESIGN_OBSTACLE>& aObstacles, const std::vector<DESIGN_WIRE_ROUTE>& aManual );
std::vector<DESIGN_POINT> OrthogonalWire( DESIGN_POINT aFrom, DESIGN_POINT aTo,
                                        const std::vector<DESIGN_POINT>& aBends );
// Moves an orthogonal segment without moving either electrical terminal.
DESIGN_WIRE MoveDesignWireSegment( const DESIGN_WIRE& aWire, std::size_t aSegment, int aDelta );
// Three or more arms of the same electrical net meet at a filled junction.
std::vector<DESIGN_JUNCTION> DesignWireJunctions( const std::vector<DESIGN_WIRE>& aWires );
}
