/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "measurements.h"
#include <optional>

namespace CIRCUITALIVE
{
struct SCOPE_FRAME;
struct SCOPE_TRACE_POINT { double time, value; };
/// Per-pixel first/min/max/last points in time order, with no synthetic midpoints.
std::vector<SCOPE_TRACE_POINT> ScopeTrace( const SCOPE_FRAME&, const MEASUREMENT_CHANNEL&,
                                          std::size_t aPixels );

struct SCOPE_CHANNEL_STYLE
{
    bool visible = true, automatic = true, inverted = false;
    double perDivision = 1, center = 0;
};
/// Fractions are relative to the plot height, top=0, bottom=1. Zero always means physical 0 V.
double ScopeVerticalFraction( double aValue, const SCOPE_CHANNEL_STYLE& );
double ScopeVerticalValue( double aFraction, const SCOPE_CHANNEL_STYLE& );
double ScopeCenterForZero( double aFraction, const SCOPE_CHANNEL_STYLE& );
void ScopeFitRange( SCOPE_CHANNEL_STYLE&, double aMinimum, double aMaximum, double aZeroFraction );

/// Time-bucket envelope reduction; preserves endpoints, per-channel extrema and trigger neighbours.
/// Used only when a full time window exceeds the point budget, never discards its oldest half.
void ScopeCompact( std::vector<SIM_SAMPLE>&, std::size_t aBudget,
                   const std::vector<MEASUREMENT_CHANNEL>&, std::optional<double> aTrigger = {} );
}
