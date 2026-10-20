/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "model_design_canvas.h"
#include <wx/dcbuffer.h>
#include <wx/control.h>
#include <wx/msgdlg.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <set>
namespace CIRCUITALIVE
{
namespace
{
wxString W( const std::string& s ) { return wxString::FromUTF8( s ); }
wxPoint P( DESIGN_POINT p ) { return { p.x, p.y }; }
const wxColour BACKGROUND( 28, 32, 37 ), WIRE( 66, 169, 197 ), HIGHLIGHT( 255, 191, 70 );
}
DESIGN_CANVAS::DESIGN_CANVAS( wxWindow* parent, MODEL_DESIGN& doc ) : wxScrolledWindow( parent ), design( doc )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT ); SetScrollRate( 16, 16 ); SetVirtualSize( 2800, 2400 );
    Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
    Bind( wxEVT_LEFT_DOWN, &DESIGN_CANVAS::down, this );
    Bind( wxEVT_MOTION, &DESIGN_CANVAS::motion, this );
    Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& ) { release(); updateExtent(); } );
    Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_drag = -1; m_dragWire.reset(); } );
    Bind( wxEVT_LEFT_DCLICK, [this]( wxMouseEvent& e )
    { down( e ); release(); if( selected >= 0 && edit ) edit(); } );
    Bind( wxEVT_RIGHT_DCLICK, &DESIGN_CANVAS::eraseAt, this );
    Bind( wxEVT_CHAR_HOOK, [this]( wxKeyEvent& e )
    { if( e.GetKeyCode() == WXK_ESCAPE ) CancelConnection(); else e.Skip(); } );
}
void DESIGN_CANVAS::eraseAt( wxMouseEvent& event )
{
    const auto point=logicalPosition(event.GetPosition());CancelConnection();
    bool removed=false;
    for(std::size_t i=0;i<design.blocks.size();++i)
    {
        const auto& b=design.blocks[i];
        if(wxRect(b.x,b.y,240,height(b)).Contains(point))
        {const auto id=b.id;design.RemoveBlock(id);m_models.erase(id);removed=true;break;}
    }
    if(!removed)
    {
        const auto wire=hitWire(point).first;
        if(wire>=0){const auto routes=wires();removed=DisconnectDesignWire(design,routes,wire);}
    }
    if(removed)
    {
        selected=-1;m_selectedWire=-1;m_geometry.clear();
        if(changed)changed();
        if(selectionChanged)selectionChanged();
        updateExtent();Refresh();
    }
}
void DESIGN_CANVAS::release()
{
    m_drag = -1; m_dragWire.reset(); if( HasCapture() ) ReleaseMouse(); SetCursor( wxNullCursor );
}
void DESIGN_CANVAS::CancelConnection() { m_pending.reset(); m_selectedWire = -1; release(); Refresh(); }
void DESIGN_CANVAS::ResetRoutes()
{
    design.wireRoutes.clear(); m_geometry.clear(); m_selectedWire = -1;
    if( changed ) changed();
    Refresh();
}
void DESIGN_CANVAS::Zoom( double factor )
{ m_zoom = std::clamp( m_zoom * factor, .3, 2.0 ); updateExtent(); Refresh(); }
void DESIGN_CANVAS::FitContent()
{
    const auto size = contentSize(); const auto client = GetClientSize();
    m_zoom = std::clamp( std::min( double( client.x - 24 ) / size.x, double( client.y - 24 ) / size.y ), .3, 1.0 );
    updateExtent(); Scroll( 0, 0 ); Refresh();
}
wxPoint DESIGN_CANVAS::logicalPosition( wxPoint p ) const
{ p = CalcUnscrolledPosition( p ); return { int( p.x / m_zoom ), int( p.y / m_zoom ) }; }
int DESIGN_CANVAS::height( const DESIGN_BLOCK& b ) const { return 115 + int( visualModel( b ).pins.size() ) * 23; }
wxSize DESIGN_CANVAS::contentSize() const
{
    wxSize size( 260, 110 + int( design.pins.size() ) * 25 );
    for( const auto& b : design.blocks ) { size.x = std::max( size.x, b.x + 270 ); size.y = std::max( size.y, b.y + height( b ) + 30 ); }
    for( const auto& r : design.wireRoutes ) for( const auto& p : r.bends )
    { size.x = std::max( size.x, p.x + 30 ); size.y = std::max( size.y, p.y + 30 ); }
    return size;
}
void DESIGN_CANVAS::updateExtent()
{
    const auto size = contentSize();
    SetVirtualSize( int( std::max( 2800, size.x ) * m_zoom ), int( std::max( 2400, size.y ) * m_zoom ) );
}
const SIMULATION_MODEL& DESIGN_CANVAS::visualModel( const DESIGN_BLOCK& b ) const
{
    auto& entry = m_models[b.id];
    if( entry.first != b.modelJson ) { entry.second = MODEL_REPOSITORY::LoadText( b.modelJson ); entry.first = b.modelJson; }
    return entry.second;
}
std::vector<DESIGN_TERMINAL> DESIGN_CANVAS::terminals() const
{
    std::vector<DESIGN_TERMINAL> result;
    for( std::size_t i = 0; i < design.pins.size(); ++i )
        result.push_back( { "", design.pins[i].id, "@" + design.pins[i].id, { 210, 76 + int( i ) * 25 }, 1 } );
    for( const auto& b : design.blocks )
    {
        const auto& model = visualModel( b );
        for( std::size_t i = 0; i < model.pins.size(); ++i )
        {
            const auto& p = model.pins[i]; const bool right = p.role == "output" || p.role == "bidirectional"
                || ( p.role == "passive" && i % 2 == 1 );
            auto it = b.connections.find( p.id );
            result.push_back( { b.id, p.id, it == b.connections.end() ? "" : it->second,
                { b.x + ( right ? 240 : 0 ), b.y + 95 + int( i ) * 23 }, right ? 1 : -1 } );
        }
    }
    return result;
}
const std::vector<DESIGN_WIRE>& DESIGN_CANVAS::wires()
{
    const auto points = terminals(); std::ostringstream signature;
    for( const auto& t : points ) signature << t.block << ':' << t.port << ':' << t.net << ':' << t.position.x << ',' << t.position.y << ';';
    for( const auto& r : design.wireRoutes )
    {
        signature << r.fromBlock << ':' << r.fromPort << ':' << r.toBlock << ':' << r.toPort;
        for( const auto& p : r.bends ) signature << ':' << p.x << ',' << p.y;
        signature << ';';
    }
    if( signature.str() != m_geometry )
    {
        std::optional<DESIGN_WIRE> selectedWire;
        if(m_selectedWire>=0&&m_selectedWire<int(m_wires.size()))selectedWire=m_wires[m_selectedWire];
        std::vector<DESIGN_OBSTACLE> obstacles{ {20, 28, 190, 65 + int( design.pins.size() ) * 25 } };
        for( const auto& b : design.blocks ) obstacles.push_back( { b.x, b.y, 240, height( b ) } );
        m_wires = RouteDesignWires( points, obstacles, design.wireRoutes ); m_geometry = signature.str();
        m_junctions = DesignWireJunctions( m_wires );
        if(selectedWire)
        {
            m_selectedWire=-1;
            for(std::size_t i=0;i<m_wires.size();++i)
            {
                const auto& w=m_wires[i];const auto& old=*selectedWire;
                auto same=[](const auto& a,const auto& b){return a.block==b.block&&a.port==b.port;};
                if((same(w.from,old.from)&&same(w.to,old.to))||(same(w.from,old.to)&&same(w.to,old.from)))m_selectedWire=i;
            }
        }
    }
    return m_wires;
}
std::pair<int,int> DESIGN_CANVAS::hitWire( wxPoint point )
{
    const auto& routes = wires(); double best = 8 / m_zoom; std::pair<int,int> result{ -1, -1 };
    for( std::size_t i = 0; i < routes.size(); ++i ) for( std::size_t k = 1; k < routes[i].points.size(); ++k )
    {
        const auto a = routes[i].points[k-1], b = routes[i].points[k];
        const double distance = a.x == b.x ? std::hypot( point.x - a.x, std::max( { std::min(a.y,b.y)-point.y, 0, point.y-std::max(a.y,b.y) } ) )
            : std::hypot( point.y - a.y, std::max( { std::min(a.x,b.x)-point.x, 0, point.x-std::max(a.x,b.x) } ) );
        if( distance < best ) { best = distance; result = { i, k-1 }; }
    }
    return result;
}
void DESIGN_CANVAS::storeRoute( const DESIGN_WIRE& wire )
{
    auto& routes = design.wireRoutes;
    routes.erase( std::remove_if( routes.begin(), routes.end(), [&]( const auto& r )
    { return ( r.fromBlock == wire.from.block && r.fromPort == wire.from.port && r.toBlock == wire.to.block && r.toPort == wire.to.port )
        || ( r.toBlock == wire.from.block && r.toPort == wire.from.port && r.fromBlock == wire.to.block && r.fromPort == wire.to.port ); } ), routes.end() );
    routes.push_back( { wire.from.block, wire.from.port, wire.to.block, wire.to.port,
        { wire.points.begin() + 1, wire.points.end() - 1 } } );
}
void DESIGN_CANVAS::down( wxMouseEvent& e )
{
    SetFocus(); const auto point = logicalPosition( e.GetPosition() );
    for( const auto& t : terminals() ) if( std::abs( point.x - t.position.x ) <= 9 && std::abs( point.y - t.position.y ) <= 9 )
    {
        if( m_pending )
        {
            try { design.Connect( m_pending->block, m_pending->port, t.block, t.port ); if( changed ) changed(); }
            catch( const std::exception& error ) { wxMessageBox( DiagnosticText( error.what() ), _( "Connect terminals" ), wxOK | wxICON_INFORMATION, this ); }
            m_pending.reset();
        }
        else m_pending = t;
        Refresh(); return;
    }
    m_pending.reset(); selected = -1; m_selectedWire = -1;
    for( std::size_t i = 0; i < design.blocks.size(); ++i )
    {
        const auto& b = design.blocks[i];
        if( wxRect( b.x, b.y, 240, height( b ) ).Contains( point ) )
        { selected = i; m_drag = i; m_offset = point - wxPoint( b.x, b.y ); CaptureMouse(); break; }
    }
    if( selected < 0 )
    {
        const auto [wire, segment] = hitWire( point ); m_selectedWire = wire;
        if( wire >= 0 )
        {
            m_dragWire = wires()[wire]; m_segment = segment; m_offset = point;
            CaptureMouse();
        }
    }
    if( selectionChanged ) selectionChanged();
    Refresh();
}
void DESIGN_CANVAS::motion( wxMouseEvent& e )
{
    const auto point = logicalPosition( e.GetPosition() );
    if( m_drag >= 0 && e.LeftIsDown() )
    {
        const auto p = point - m_offset; auto& b = design.blocks.at( m_drag );
        b.x = std::max( 260, p.x ); b.y = std::max( 10, p.y );
        if( changed ) changed();
        Refresh();
    }
    else if( m_dragWire && e.LeftIsDown() )
    {
        const auto& a = m_dragWire->points[m_segment]; const auto& b = m_dragWire->points[m_segment+1];
        auto wire = MoveDesignWireSegment( *m_dragWire, m_segment,
            a.x == b.x ? point.x - m_offset.x : point.y - m_offset.y );
        storeRoute( wire ); if( changed ) changed(); Refresh();
    }
    else
    {
        const auto [wire, segment] = hitWire( point );
        if( wire < 0 ) SetCursor( wxNullCursor );
        else { const auto& p = wires()[wire].points; SetCursor( wxCursor( p[segment].x == p[segment+1].x ? wxCURSOR_SIZEWE : wxCURSOR_SIZENS ) ); }
    }
}
void DESIGN_CANVAS::paint()
{
    wxAutoBufferedPaintDC dc( this ); PrepareDC( dc ); dc.SetUserScale( m_zoom, m_zoom ); dc.SetBackground( wxBrush( BACKGROUND ) ); dc.Clear();
    dc.SetTextForeground( wxColour( 225, 230, 235 ) );
    const auto points = terminals(); const auto& routes = wires(); std::set<std::string> highlighted;
    if( selected >= 0 && selected < int( design.blocks.size() ) )
        for( const auto& [port, net] : design.blocks[selected].connections ) if( !net.empty() ) highlighted.insert( net );
    std::map<std::string, std::vector<std::size_t>> nets;
    for( std::size_t i = 0; i < routes.size(); ++i ) nets[routes[i].from.net].push_back(i);
    auto activeWire = [&]( std::size_t i ) { return highlighted.contains(routes[i].from.net) || int(i) == m_selectedWire; };
    // Erase crossings once per electrical net, then draw every branch together.
    // A branch of the same net must never erase an existing connected branch.
    for( int pass = 0; pass < 2; ++pass ) for( const auto& [net, branches] : nets )
    {
        const bool active = std::any_of(branches.begin(),branches.end(),activeWire);
        if( active != bool(pass) ) continue;
        auto draw = [&]( std::size_t i )
        { std::vector<wxPoint> line; for( auto p : routes[i].points ) line.push_back(P(p)); dc.DrawLines(line.size(),line.data()); };
        dc.SetPen( wxPen( BACKGROUND, active ? 8 : 6 ) );
        for( const auto i : branches ) draw(i);
        for( int colorPass = 0; colorPass < 2; ++colorPass ) for( const auto i : branches )
        {
            if( activeWire(i) != bool(colorPass) ) continue;
            dc.SetPen( wxPen( activeWire(i) ? HIGHLIGHT : WIRE, activeWire(i) ? 3 : 2 ) ); draw(i);
        }
        dc.SetPen( *wxTRANSPARENT_PEN ); dc.SetBrush( wxBrush( active ? HIGHLIGHT : WIRE ) );
        for( const auto& junction : m_junctions ) if( junction.net == net ) dc.DrawCircle(P(junction.position),4);
    }
    if( m_selectedWire >= 0 && m_selectedWire < int(routes.size()) )
    {
        const auto& wire = routes[m_selectedWire];
        dc.SetPen( *wxTRANSPARENT_PEN ); dc.SetBrush( wxBrush(HIGHLIGHT) );
        for( std::size_t k = 1; k < wire.points.size(); ++k )
        {
            const auto a = wire.points[k-1], b = wire.points[k];
            dc.DrawRectangle( (a.x+b.x)/2-3, (a.y+b.y)/2-3, 6, 6 );
        }
    }
    dc.SetPen( wxPen( wxColour( 99, 114, 129 ) ) ); dc.SetBrush( wxBrush( wxColour( 42, 49, 57 ) ) );
    dc.DrawRoundedRectangle( 20, 28, 190, 65 + int( design.pins.size() ) * 25, 6 ); dc.DrawText( _( "Component pins" ), 35, 40 );
    for( std::size_t i = 0; i < design.pins.size(); ++i )
    { const auto& p = design.pins[i]; dc.DrawText( (W( p.defaultPin ) + "  " + ModelText( p.label )).Left( 24 ), 33, 68 + int( i ) * 25 ); }
    for( std::size_t index = 0; index < design.blocks.size(); ++index )
    {
        const auto& b = design.blocks[index]; const auto& m = visualModel( b );
        dc.SetPen( wxPen( int(index) == selected ? HIGHLIGHT : wxColour( 99, 114, 129 ), 2 ) );
        dc.SetBrush( wxBrush( wxColour( 42, 49, 57 ) ) ); dc.DrawRoundedRectangle( b.x, b.y, 240, height(b), 6 );
        auto fit = [&]( const wxString& text ) { return wxControl::Ellipsize( text, dc, wxELLIPSIZE_END, 220 ); };
        dc.DrawText( fit(W( b.id ) + " — " + ModelText( m.name )), b.x + 10, b.y + 10 );
        // Show the operation rather than an empty container of pins.
        wxString operation = ModelText( m.description );
        if( m.id == "diode" || m.id == "led" || m.id=="pn_junction" ) operation = "A  ──▷|──  K";
        else if( m.id == "resistor" ) operation = "I = (V1 − V2) / R";
        else if( m.id=="series_resistance" ) operation = "V1 − V2 = I × R";
        else if( m.id == "capacitor" ) operation = "I = C × dV/dt";
        else if( m.id == "inductor" ) operation = "V = L × dI/dt";
        dc.SetTextForeground( wxColour( 125, 211, 214 ) ); dc.DrawText( fit(operation.BeforeFirst('\n')), b.x+10, b.y+35 );
        wxString value;
        if( !m.parameters.empty() )
        { const auto& p = m.parameters.front(); auto it = b.values.find(p.id); value = ModelText( p.label ) + " = " + W(it == b.values.end() ? p.defaultValue : it->second); }
        dc.SetTextForeground( wxColour( 184, 195, 206 ) ); dc.DrawText( fit(value), b.x+10, b.y+60 );
        dc.SetTextForeground( wxColour( 225, 230, 235 ) );
        for( std::size_t i = 0; i < m.pins.size(); ++i )
        {
            const auto& p = m.pins[i]; const auto label = (ModelText( p.label ) + " [" + W(p.id) + "]").Left( 29 );
            bool right = p.role == "output" || p.role == "bidirectional" || (p.role == "passive" && i%2==1);
            dc.DrawText( label, b.x + (right ? 230-dc.GetTextExtent(label).x : 10), b.y+87+int(i)*23 );
        }
    }
    for( const auto& t : points )
    {
        bool pending = m_pending && t.block == m_pending->block && t.port == m_pending->port;
        dc.SetPen( *wxTRANSPARENT_PEN ); dc.SetBrush( wxBrush( pending || highlighted.contains(t.net) ? HIGHLIGHT
            : t.net.empty() ? wxColour(139,147,156) : wxColour(86,207,166) ) ); dc.DrawCircle( P(t.position), 5 );
    }
}
}
