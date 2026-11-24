/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_outputs.h"
#include <math/box2.h>
#include <algorithm>
#include <cmath>

namespace CIRCUITALIVE
{
struct LUMINOUS_GEOMETRY
{
    VECTOR2D start,end;
    double thickness;
    bool circle,filled;
};
inline LUMINOUS_GEOMETRY LuminousGeometry( const BOX2I& body,const MODEL_SEGMENT& s,
                                         VECTOR2I axisX={1,0},VECTOR2I axisY={0,1} )
{
    const double w=std::max<double>(1,axisX.x?body.GetWidth():body.GetHeight());
    const double h=std::max<double>(1,axisY.y?body.GetHeight():body.GetWidth());
    auto point=[&](double x,double y){return VECTOR2D(body.Centre())+VECTOR2D(axisX)*((x-.5)*w)+VECTOR2D(axisY)*((y-.5)*h);};
    if(s.geometry)
    {
        const auto& g=*s.geometry;
        return {point(g.x1,g.y1),point(g.x2,g.y2),g.thickness*std::min(w,h),s.shape=="circle",g.filled};
    }
    // Preserve existing rectangle-based models exactly until a user edits them.
    const auto& v=s.visual;
    const double x=v.custom?v.x:0,y=v.custom?v.y:0,sw=v.custom?v.width:1,sh=v.custom?v.height:1;
    double x1=x,y1=y+sh/2,x2=x+sw,y2=y+sh/2,thickness=sh*h;
    if(s.shape=="vertical"){x1=x2=x+sw/2;y1=y;y2=y+sh;thickness=sw*w;}
    else if(s.shape=="slash"){y1=y+sh;y2=y;thickness=std::min(sw*w,sh*h)*.18;}
    else if(s.shape=="backslash"){y1=y;y2=y+sh;thickness=std::min(sw*w,sh*h)*.18;}
    else if(s.shape=="dot")
    {
        const double radius=std::min(sw*w,sh*h)/2;
        const auto center=point(x+sw/2,y+sh/2);
        return {center-VECTOR2D(axisX)*radius,center+VECTOR2D(axisX)*radius,thickness,true,true};
    }
    return {point(x1,y1),point(x2,y2),thickness,false,false};
}
inline MODEL_SEGMENT EditableLuminousSegment( MODEL_SEGMENT s,const BOX2I& body )
{
    if(s.geometry)return s;
    const auto g=LuminousGeometry(body,s);
    const auto origin=VECTOR2D(body.GetOrigin());
    const double w=std::max<double>(1,body.GetWidth()),h=std::max<double>(1,body.GetHeight());
    s.shape=g.circle?"circle":"line";
    s.geometry=SEGMENT_GEOMETRY{(g.start.x-origin.x)/w,(g.start.y-origin.y)/h,
        (g.end.x-origin.x)/w,(g.end.y-origin.y)/h,g.thickness/std::min(w,h),g.filled};
    return s;
}
inline bool HitLuminousSegment( const LUMINOUS_GEOMETRY& g,VECTOR2D at,double tolerance )
{
    const auto d=g.end-g.start;
    if(g.circle)
    {
        const double r=d.EuclideanNorm()/2,dist=(at-(g.start+g.end)/2).EuclideanNorm();
        return g.filled?dist<=r+tolerance:std::abs(dist-r)<=g.thickness/2+tolerance;
    }
    const double len=d.SquaredEuclideanNorm();
    const double t=len>0?std::clamp((at-g.start).Dot(d)/len,0.,1.):0.;
    return (at-g.start-d*t).EuclideanNorm()<=g.thickness/2+tolerance;
}
}
