/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "control_visual.h"
#include <base_units.h>
#include <math/box2.h>
#include <algorithm>
#include <cmath>

namespace CIRCUITALIVE
{
enum CONTROL_EDGE { CONTROL_LEFT=1, CONTROL_RIGHT=2, CONTROL_TOP=4, CONTROL_BOTTOM=8 };
inline CONTROL_VISUAL DragControlVisual( CONTROL_VISUAL original, int edges, double dx, double dy )
{
    CONTROL_VISUAL v=original;v.custom=true;
    if( edges==0 ) { v.x=std::clamp(v.x+dx,-10.,10.);v.y=std::clamp(v.y+dy,-10.,10.);return v; }
    if( edges&CONTROL_LEFT )
    {
        const double right=original.x+original.width;
        v.x=std::clamp(original.x+dx,std::max(-10.,right-10.),std::min(10.,right-.05));
        v.width=std::clamp(right-v.x,.05,10.);v.x=right-v.width;
    }
    else if( edges&CONTROL_RIGHT ) v.width=std::clamp(original.width+dx,.05,10.);
    if( edges&CONTROL_TOP )
    {
        const double bottom=original.y+original.height;
        v.y=std::clamp(original.y+dy,std::max(-10.,bottom-10.),std::min(10.,bottom-.05));
        v.height=std::clamp(bottom-v.y,.05,10.);v.y=bottom-v.height;
    }
    else if( edges&CONTROL_BOTTOM ) v.height=std::clamp(original.height+dy,.05,10.);
    return v;
}
inline BOX2I ControlBounds( const BOX2I& body, const CONTROL_VISUAL& visual )
{
    if( !visual.custom )
    {
        BOX2I box = body; box.Inflate( schIUScale.mmToIU( .6 ) ); return box;
    }
    const double w = std::max<double>( body.GetWidth(), schIUScale.mmToIU( 1 ) );
    const double h = std::max<double>( body.GetHeight(), schIUScale.mmToIU( 1 ) );
    return BOX2I( body.GetOrigin() + VECTOR2I( std::lround( visual.x * w ), std::lround( visual.y * h ) ),
            VECTOR2I( std::lround( visual.width * w ), std::lround( visual.height * h ) ) );
}
}
