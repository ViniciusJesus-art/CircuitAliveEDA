/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_routing.h"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
namespace CIRCUITALIVE
{
namespace
{
bool Same( const DESIGN_TERMINAL& t, const std::string& block, const std::string& port )
{ return t.block == block && t.port == port; }
bool Crosses( DESIGN_POINT a, DESIGN_POINT b, const DESIGN_OBSTACLE& r )
{
    if( a.x == b.x ) return a.x > r.x && a.x < r.x + r.width
        && std::max( a.y, b.y ) > r.y && std::min( a.y, b.y ) < r.y + r.height;
    return a.y > r.y && a.y < r.y + r.height
        && std::max( a.x, b.x ) > r.x && std::min( a.x, b.x ) < r.x + r.width;
}
void Simplify( std::vector<DESIGN_POINT>& p )
{
    p.erase( std::unique( p.begin(), p.end() ), p.end() );
    for( std::size_t i = 1; i + 1 < p.size(); )
        if( ( p[i-1].x == p[i].x && p[i].x == p[i+1].x )
            || ( p[i-1].y == p[i].y && p[i].y == p[i+1].y ) ) p.erase( p.begin() + i );
        else ++i;
}
bool Inside( DESIGN_POINT p, const DESIGN_OBSTACLE& box )
{
    return p.x >= box.x && p.x <= box.x + box.width
        && p.y >= box.y && p.y <= box.y + box.height;
}
std::vector<DESIGN_POINT> AutoRoute( DESIGN_POINT start, DESIGN_POINT end,
    const std::vector<DESIGN_OBSTACLE>& obstacles, const std::vector<DESIGN_WIRE>& previous, int lane,
    const std::string& net )
{
    std::vector<int> xs{ start.x, end.x }, ys{ start.y, end.y };
    const int margin = 16 + lane % 8 * 7;
    for( const auto& r : obstacles )
    {
        xs.insert( xs.end(), { std::max(8, r.x - margin), r.x + r.width + margin } );
        ys.insert( ys.end(), { std::max(8, r.y - margin), r.y + r.height + margin } );
    }
    auto unique = []( auto& v ) { std::sort( v.begin(), v.end() ); v.erase( std::unique( v.begin(), v.end() ), v.end() ); };
    unique( xs ); unique( ys );
    std::map<int,std::vector<std::pair<int,int>>> occupiedX, occupiedY;
    for( const auto& wire : previous ) if( wire.from.net != net )
        for( std::size_t k=1; k<wire.points.size(); ++k )
        {
            const auto a=wire.points[k-1], b=wire.points[k];
            if( a.x==b.x ) occupiedX[a.x].push_back({std::min(a.y,b.y),std::max(a.y,b.y)});
            else occupiedY[a.y].push_back({std::min(a.x,b.x),std::max(a.x,b.x)});
        }
    const int nx = xs.size(), ny = ys.size();
    // Three states per node: initial, horizontal arrival, vertical arrival.
    const int count = nx * ny * 3;
    std::vector<double> cost( count, std::numeric_limits<double>::infinity() );
    std::vector<int> parent( count, -1 );
    auto index = [&]( DESIGN_POINT p ) { return ( int( std::lower_bound( ys.begin(), ys.end(), p.y ) - ys.begin() ) * nx
        + int( std::lower_bound( xs.begin(), xs.end(), p.x ) - xs.begin() ) ) * 3; };
    auto point = [&]( int state ) { return DESIGN_POINT{ xs[state / 3 % nx], ys[state / 3 / nx] }; };
    const int first = index( start ), last = index( end );
    using ENTRY = std::pair<double, int>;
    std::priority_queue<ENTRY, std::vector<ENTRY>, std::greater<ENTRY>> queue;
    cost[first] = 0; queue.push( { 0, first } ); int finish = -1;
    while( !queue.empty() )
    {
        auto [priority, state] = queue.top(); queue.pop(); const auto a = point( state );
        const double heuristic = std::abs( a.x - end.x ) + std::abs( a.y - end.y );
        if( priority > cost[state] + heuristic + .01 ) continue;
        if( state / 3 == last / 3 ) { finish = state; break; }
        const int x = state / 3 % nx, y = state / 3 / nx;
        for( const auto& [dx, dy] : std::array<std::pair<int,int>,4>{ { {1,0}, {-1,0}, {0,1}, {0,-1} } } )
        {
            const int xx = x + dx, yy = y + dy;
            if( xx < 0 || yy < 0 || xx >= nx || yy >= ny ) continue;
            const DESIGN_POINT b{ xs[xx], ys[yy] };
            if( std::any_of( obstacles.begin(), obstacles.end(), [&]( const auto& r ) { return Crosses( a, b, r ); } ) ) continue;
            const int direction = dx ? 1 : 2, next = ( yy * nx + xx ) * 3 + direction;
            double length = std::abs( a.x - b.x ) + std::abs( a.y - b.y );
            double penalty = state % 3 && state % 3 != direction ? 24 : 0;
            const auto& occupied=dx ? occupiedY : occupiedX;
            if( const auto it=occupied.find(dx ? a.y : a.x); it!=occupied.end() )
                for( const auto& [low,high]:it->second )
                    penalty+=12*std::max(0,std::min(dx ? std::max(a.x,b.x) : std::max(a.y,b.y),high)
                        -std::max(dx ? std::min(a.x,b.x) : std::min(a.y,b.y),low));
            const double candidate = cost[state] + length + penalty;
            if( candidate >= cost[next] ) continue;
            cost[next] = candidate; parent[next] = state;
            queue.push( { candidate + std::abs( b.x - end.x ) + std::abs( b.y - end.y ), next } );
        }
    }
    // A manually overlapped block can cover a terminal. Still draw a movable wire.
    if( finish < 0 ) return { start, { start.x, end.y }, end };
    std::vector<DESIGN_POINT> result;
    for( int state = finish; state >= 0; state = parent[state] ) result.push_back( point( state ) );
    std::reverse( result.begin(), result.end() ); Simplify( result ); return result;
}
}
std::vector<DESIGN_POINT> OrthogonalWire( DESIGN_POINT from, DESIGN_POINT to, const std::vector<DESIGN_POINT>& bends )
{
    std::vector<DESIGN_POINT> result{ from };
    auto add = [&]( DESIGN_POINT p )
    {
        const auto last = result.back();
        if( last.x != p.x && last.y != p.y ) result.push_back( { p.x, last.y } );
        result.push_back( p );
    };
    for( const auto& bend : bends ) add( bend );
    add( to ); Simplify( result ); return result;
}
DESIGN_WIRE MoveDesignWireSegment( const DESIGN_WIRE& original, std::size_t segment, int delta )
{
    if( segment + 1 >= original.points.size() ) throw std::out_of_range( "Unknown wire segment" );
    auto wire = original;
    if( segment == 0 ) { wire.points.insert( wire.points.begin(), wire.points.front() ); ++segment; }
    if( segment + 2 == wire.points.size() ) wire.points.push_back( wire.points.back() );
    auto& a = wire.points[segment]; auto& b = wire.points[segment+1];
    if( a.x == b.x ) a.x = b.x = std::max( 8, a.x + delta );
    else a.y = b.y = std::max( 8, a.y + delta );
    return wire;
}

namespace
{
std::vector<DESIGN_POINT> AttachedManualWire( const DESIGN_WIRE& wire,
    std::vector<DESIGN_POINT> bends, const std::vector<DESIGN_OBSTACLE>& obstacles,
    const std::vector<DESIGN_WIRE>& previous, int lane )
{
    constexpr int stub = 18;
    const DESIGN_POINT start{ wire.from.position.x + wire.from.direction * stub, wire.from.position.y };
    const DESIGN_POINT finish{ wire.to.position.x + wire.to.direction * stub, wire.to.position.y };
    // A dragged first/last segment can leave its corner on the block boundary.
    // Keep its chosen height, but attach it to an outward-facing lead instead.
    for( const auto& box : obstacles )
    {
        if( bends.empty() ) break;
        if( Inside( wire.from.position, box ) && Inside( bends.front(), box ) ) bends.front().x = start.x;
        if( Inside( wire.to.position, box ) && Inside( bends.back(), box ) ) bends.back().x = finish.x;
    }
    bends.erase( std::remove_if( bends.begin(), bends.end(), [&]( auto p )
        { return std::any_of( obstacles.begin(), obstacles.end(), [&]( const auto& box ) { return Inside( p, box ); } ); } ), bends.end() );
    std::vector<DESIGN_POINT> result{ wire.from.position, start };
    bends.push_back( finish );
    for( const auto& target : bends )
    {
        const auto from = result.back();
        auto leg = OrthogonalWire( from, target, {} );
        bool blocked = false;
        for( std::size_t i = 1; i < leg.size(); ++i )
            for( const auto& box : obstacles ) blocked |= Crosses( leg[i-1], leg[i], box );
        if( blocked ) leg = AutoRoute( from, target, obstacles, previous, lane, wire.from.net );
        result.insert( result.end(), leg.begin() + 1, leg.end() );
    }
    result.push_back( wire.to.position ); Simplify( result ); return result;
}
}
std::vector<DESIGN_WIRE> RouteDesignWires( const std::vector<DESIGN_TERMINAL>& terminals,
    const std::vector<DESIGN_OBSTACLE>& obstacles, const std::vector<DESIGN_WIRE_ROUTE>& manual )
{
    // The pin leads cross this margin; all other segments stay outside it, so a
    // block's fill/border cannot hide an endpoint connector after a manual edit.
    auto clearances = obstacles;
    for( auto& box : clearances ) { box.x -= 6; box.y -= 6; box.width += 12; box.height += 12; }
    std::map<std::string, std::vector<DESIGN_TERMINAL>> nets;
    for( const auto& t : terminals ) if( !t.net.empty() ) nets[t.net].push_back( t );
    std::vector<DESIGN_WIRE> wires;
    for( auto& [net, ends] : nets )
    {
        std::sort( ends.begin(), ends.end(), []( const auto& a, const auto& b )
            { return std::tie( a.block, a.port ) < std::tie( b.block, b.port ); } );
        // Share short branches on fan-out nets instead of drawing a separate
        // long return to the component's pin for every block. Manual branches
        // are kept first, so moving blocks never discards a user's routing.
        std::vector<std::pair<std::size_t,std::size_t>> branches;
        std::vector<std::size_t> parent(ends.size());
        for(std::size_t i=0;i<ends.size();++i)parent[i]=i;
        auto root=[&](std::size_t i){while(parent[i]!=i)i=parent[i];return i;};
        auto join=[&](std::size_t a,std::size_t b)
        {
            if(root(a)==root(b))return;
            parent[root(b)]=root(a);branches.emplace_back(a,b);
        };
        for(const auto& r:manual)
        {
            auto a=std::find_if(ends.begin(),ends.end(),[&](const auto& t){return Same(t,r.fromBlock,r.fromPort);});
            auto b=std::find_if(ends.begin(),ends.end(),[&](const auto& t){return Same(t,r.toBlock,r.toPort);});
            if(a!=ends.end()&&b!=ends.end())join(a-ends.begin(),b-ends.begin());
        }
        if(ends.size()<=512)
        {
            std::vector<std::tuple<int,std::size_t,std::size_t>> candidates;
            for(std::size_t a=0;a<ends.size();++a)for(std::size_t b=a+1;b<ends.size();++b)
                candidates.emplace_back(std::abs(ends[a].position.x-ends[b].position.x)+std::abs(ends[a].position.y-ends[b].position.y),a,b);
            std::sort(candidates.begin(),candidates.end());
            for(const auto& [distance,a,b]:candidates)join(a,b);
        }
        else for(std::size_t i=1;i<ends.size();++i)join(i-1,i);
        for( const auto& [from,to] : branches )
        {
            DESIGN_WIRE wire{ ends[from], ends[to], {}, false };
            for( const auto& r : manual )
            {
                bool forward = Same( wire.from, r.fromBlock, r.fromPort ) && Same( wire.to, r.toBlock, r.toPort );
                bool reverse = Same( wire.to, r.fromBlock, r.fromPort ) && Same( wire.from, r.toBlock, r.toPort );
                if( !forward && !reverse ) continue;
                auto bends = r.bends; if( reverse ) std::reverse( bends.begin(), bends.end() );
                wire.points = AttachedManualWire( wire, std::move( bends ), clearances, wires, wires.size() );
                wire.manual = true; break;
            }
            if( !wire.manual )
            {
                constexpr int stub = 18;
                DESIGN_POINT a{ wire.from.position.x + wire.from.direction * stub, wire.from.position.y };
                DESIGN_POINT b{ wire.to.position.x + wire.to.direction * stub, wire.to.position.y };
                auto route = AutoRoute( a, b, clearances, wires, wires.size(), net );
                wire.points.push_back( wire.from.position );
                wire.points.insert( wire.points.end(), route.begin(), route.end() ); wire.points.push_back( wire.to.position );
                Simplify( wire.points );
            }
            wires.push_back( std::move( wire ) );
        }
    }
    return wires;
}

std::vector<DESIGN_JUNCTION> DesignWireJunctions( const std::vector<DESIGN_WIRE>& wires )
{
    using SEGMENT = std::pair<DESIGN_POINT, DESIGN_POINT>;
    std::map<std::string, std::vector<SEGMENT>> nets;
    for( const auto& wire : wires ) for( std::size_t i = 1; i < wire.points.size(); ++i )
        if( wire.points[i-1] != wire.points[i] && !wire.from.net.empty() )
            nets[wire.from.net].emplace_back( wire.points[i-1], wire.points[i] );
    std::vector<DESIGN_JUNCTION> result;
    for( const auto& [net, segments] : nets )
    {
        std::set<std::pair<int,int>> candidates;
        for( std::size_t i = 0; i < segments.size(); ++i ) for( std::size_t j = i+1; j < segments.size(); ++j )
        {
            auto [a,b] = segments[i]; auto [c,d] = segments[j];
            const bool horizontal = a.y == b.y, otherHorizontal = c.y == d.y;
            if( horizontal != otherHorizontal )
            {
                if( !horizontal ) { std::swap(a,c); std::swap(b,d); }
                if( c.x >= std::min(a.x,b.x) && c.x <= std::max(a.x,b.x)
                    && a.y >= std::min(c.y,d.y) && a.y <= std::max(c.y,d.y) ) candidates.emplace(c.x,a.y);
            }
            else if( horizontal && a.y == c.y )
            {
                const int low = std::max(std::min(a.x,b.x),std::min(c.x,d.x));
                const int high = std::min(std::max(a.x,b.x),std::max(c.x,d.x));
                if( low <= high ) { candidates.emplace(low,a.y); candidates.emplace(high,a.y); }
            }
            else if( !horizontal && a.x == c.x )
            {
                const int low = std::max(std::min(a.y,b.y),std::min(c.y,d.y));
                const int high = std::min(std::max(a.y,b.y),std::max(c.y,d.y));
                if( low <= high ) { candidates.emplace(a.x,low); candidates.emplace(a.x,high); }
            }
        }
        for( const auto& [x,y] : candidates )
        {
            unsigned arms = 0;
            for( const auto& [a,b] : segments )
            {
                if( a.y == b.y && y == a.y && x >= std::min(a.x,b.x) && x <= std::max(a.x,b.x) )
                { if( x > std::min(a.x,b.x) ) arms |= 1; if( x < std::max(a.x,b.x) ) arms |= 2; }
                if( a.x == b.x && x == a.x && y >= std::min(a.y,b.y) && y <= std::max(a.y,b.y) )
                { if( y > std::min(a.y,b.y) ) arms |= 4; if( y < std::max(a.y,b.y) ) arms |= 8; }
            }
            int count = 0; for( unsigned bits = arms; bits; bits >>= 1 ) count += bits & 1;
            if( count >= 3 ) result.push_back( { net, {x,y} } );
        }
    }
    return result;
}
}
