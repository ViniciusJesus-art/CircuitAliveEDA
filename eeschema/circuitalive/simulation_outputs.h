/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "control_visual.h"
#include <string>
#include <optional>

namespace CIRCUITALIVE
{
struct SEGMENT_GEOMETRY
{
    // Positions relative to the symbol body; thickness relative to its shorter dimension.
    double x1=.2, y1=.5, x2=.8, y2=.5, thickness=.04;
    bool filled=true;
};
struct MODEL_SEGMENT
{
    std::string fullScaleParameter;
    std::string id, signal, shape = "horizontal";
    CONTROL_VISUAL visual;
    double fullScale = .01;
    double red = 1, green = .12, blue = .02;
    std::optional<SEGMENT_GEOMETRY> geometry;
};
struct MODEL_DISPLAY
{
    std::string type;
    CONTROL_VISUAL visual;
};
struct MODEL_AUDIO
{
    std::string signal, fullScaleParameter, volumeParameter;
    double fullScale = 5, volume = .2;
};
struct MODEL_LINK
{
    std::string medium, role, signal, input;
    bool Transmits() const { return role == "transmit" || role == "transceiver"; }
    bool Receives() const { return role == "receive" || role == "transceiver"; }
    bool Compatible( const MODEL_LINK& peer ) const
    {
        return !medium.empty() && medium == peer.medium
            && ( ( Transmits() && peer.Receives() ) || ( Receives() && peer.Transmits() ) );
    }
};
}
