/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "scope_plot.h"
#include "scope_panel.h"
#include <wx/dcbuffer.h>
#include <algorithm>
#include <cmath>

namespace CIRCUITALIVE
{
namespace
{
wxString utf8( const std::string& s ) { return wxString::FromUTF8( s ); }
wxColour color( std::size_t i )
{
    static const wxColour colors[] = { { 85, 225, 115 }, { 255, 208, 70 }, { 80, 185, 255 },
        { 255, 120, 185 }, { 195, 150, 255 }, { 80, 225, 215 } }; return colors[i % 6];
}
const wxColour TRIGGER( 255, 165, 55 );
}
SCOPE_PLOT::SCOPE_PLOT( wxWindow* parent, SCOPE_PANEL& owner ) : wxPanel( parent ), m_owner( owner )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT ); SetMinSize( wxSize( 240, 200 ) );
    Bind( wxEVT_PAINT, &SCOPE_PLOT::paint, this );
    Bind( wxEVT_SIZE, [this]( wxSizeEvent& e ) { m_trace = {}; Refresh( false ); e.Skip(); } );
    Bind( wxEVT_LEFT_DOWN, &SCOPE_PLOT::down, this );
    Bind( wxEVT_MOTION, &SCOPE_PLOT::hover, this );
    Bind( wxEVT_LEFT_UP, &SCOPE_PLOT::up, this );
    Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_drag = 0; } );
    Bind( wxEVT_MOUSEWHEEL, [this]( wxMouseEvent& e )
    {
        auto& p = m_owner.m_preferences; const double factor = e.GetWheelRotation() > 0 ? .45 : 1.8;
        if( e.ShiftDown() && !m_owner.m_channels.empty() )
        {
            auto& c = m_owner.style( m_owner.selected() ); c.automatic = false;
            const auto zero = ScopeVerticalFraction( 0, c );
            c.perDivision = std::clamp( ScopeNiceScale( c.perDivision * factor ), 1e-12, 1e12 );
            c.center = ScopeCenterForZero( zero, c ); m_owner.syncControls(); Refresh( false );
        }
        else
        {
            p.timebase.perDivision = std::clamp( ScopeNiceScale( p.timebase.perDivision * factor ), 1e-9, 1e3 );
            m_owner.retime(); m_owner.syncControls();
        }
    } );
}
wxRect SCOPE_PLOT::area() const
{
    const auto s = GetClientSize(); return { 90, 30, std::max( 1, s.x - 105 ), std::max( 1, s.y - 65 ) };
}
int SCOPE_PLOT::y( double v, std::size_t channel ) const
{
    const auto p = area();
    return p.y + int( std::clamp( ScopeVerticalFraction( v, m_owner.style( channel ) ) * p.height, -1000000., 1000000. ) );
}
int SCOPE_PLOT::x( double fraction ) const
{ const auto p = area(); return p.x + int( std::clamp( fraction * p.width, -1000000., 1000000. ) ); }
std::size_t SCOPE_PLOT::triggerSource() const
{
    for( std::size_t c = 0; c < m_owner.m_channels.size(); ++c )
        if( m_owner.m_channels[c].name == m_owner.m_preferences.triggerSource ) return c;
    return 0;
}
std::vector<SCOPE_PLOT::MARKER> SCOPE_PLOT::markers() const
{
    std::vector<MARKER> result; const auto p = area();
    for( std::size_t c = 0; c < m_owner.m_channels.size(); ++c )
    {
        if( !m_owner.style( c ).visible ) continue;
        const int actual = y( 0, c ), level = std::clamp( actual, p.y, p.GetBottom() );
        bool collision = false;
        for( std::size_t other = 0; other < m_owner.m_channels.size(); ++other )
            if( other != c && m_owner.style( other ).visible && std::abs( y( 0, other ) - actual ) < 17 ) collision = true;
        const int width = collision ? 30 : 62, left = collision ? 2 + 31 * ( c % 2 ) : 2;
        result.push_back( { wxRect( left, level - 8, width, 17 ), int( c ), actual != level } );
    }
    if( !m_owner.m_channels.empty() && m_owner.m_preferences.trigger.mode != SCOPE_TRIGGER_MODE::ROLL )
    {
        const int actual = y( m_owner.m_preferences.trigger.level, triggerSource() );
        const int level = std::clamp( actual, p.y, p.GetBottom() );
        result.push_back( { wxRect( p.x - 23, level - 8, 22, 17 ), -1, actual != level } );
    }
    return result;
}
void SCOPE_PLOT::down( wxMouseEvent& e )
{
    const auto pos = e.GetPosition(); const auto p = area(); auto& pref = m_owner.m_preferences;
    for( const auto& marker : markers() )
    {
        if( !marker.bounds.Contains( pos ) ) continue;
        m_drag = marker.channel < 0 ? 10 : 100 + marker.channel;
        m_dragOffset = pos.y - ( marker.bounds.y + 8 );
        if( marker.channel >= 0 )
        { pref.selectedChannel = m_owner.m_channels[marker.channel].name; m_owner.syncControls(); }
        CaptureMouse(); return;
    }
    if( !p.Contains( pos ) || !pref.cursorMode || m_owner.m_channels.empty() ) return;
    m_dragOffset = 0; int nearest = 12;
    auto consider = [&]( int distance, int id ) { if( distance < nearest ) { nearest = distance; m_drag = id; } };
    if( pref.cursorMode & 1 )
    { consider( std::abs( pos.x - x( pref.cursorA ) ), 1 ); consider( std::abs( pos.x - x( pref.cursorB ) ), 2 ); }
    if( pref.cursorMode & 2 )
    { consider( std::abs( pos.y - y( pref.levelA, m_owner.selected() ) ), 3 ); consider( std::abs( pos.y - y( pref.levelB, m_owner.selected() ) ), 4 ); }
    if( !m_drag ) m_drag = ( pref.cursorMode & 1 ? 1 : 3 ) + ( e.ShiftDown() ? 1 : 0 );
    CaptureMouse(); move( pos );
}
void SCOPE_PLOT::move( wxPoint pos )
{
    const auto p = area(); auto& pref = m_owner.m_preferences;
    const double fraction = std::clamp( double( pos.y - m_dragOffset - p.y ) / p.height, 0., 1. );
    if( m_drag >= 100 )
    {
        auto& c = m_owner.style( m_drag - 100 ); c.automatic = false;
        c.center = ScopeCenterForZero( fraction, c );
        m_owner.syncControls();
    }
    else if( m_drag == 10 )
    {
        pref.trigger.level = ScopeVerticalValue( fraction, m_owner.style( triggerSource() ) );
        m_owner.syncControls(); // Detection is rearmed once on release, not once per mouse event.
    }
    else if( m_drag <= 2 )
        ( m_drag == 1 ? pref.cursorA : pref.cursorB ) = std::clamp( double( pos.x - p.x ) / p.width, 0., 1. );
    else
        ( m_drag == 3 ? pref.levelA : pref.levelB ) = ScopeVerticalValue( fraction, m_owner.style( m_owner.selected() ) );
    m_owner.updateCursors(); Refresh( false );
}
void SCOPE_PLOT::up( wxMouseEvent& e )
{
    if( !m_drag ) return;
    move( e.GetPosition() ); const bool trigger = m_drag == 10; m_drag = 0;
    if( HasCapture() ) ReleaseMouse();
    if( trigger ) m_owner.triggerDragged();
}
void SCOPE_PLOT::hover( wxMouseEvent& e )
{
    if( m_drag && e.Dragging() )
    {
        const auto now = std::chrono::steady_clock::now();
        if( now - m_lastDrag >= std::chrono::milliseconds( 25 ) ) { m_lastDrag = now; move( e.GetPosition() ); }
        return;
    }
    wxString tip = _( "Drag 0 V arrows to move signals; drag T to change trigger level. Wheel: time; Shift-wheel: amplitude." );
    bool markerHit = false;
    for( const auto& marker : markers() )
        if( marker.bounds.Contains( e.GetPosition() ) )
        {
            markerHit = true;
            tip = marker.channel < 0 ? _( "Drag trigger level" ) : utf8( m_owner.m_channels[marker.channel].name ) + _( " · physical 0 V · drag to move this signal" );
            if( marker.outside ) tip += _( " · outside visible range" );
        }
    if( tip != m_tooltip ) { m_tooltip = tip; SetToolTip( tip ); }
    if( markerHit != m_markerCursor )
    { m_markerCursor = markerHit; SetCursor( wxCursor( markerHit ? wxCURSOR_SIZENS : wxCURSOR_ARROW ) ); }
}
void SCOPE_PLOT::rebuild()
{
    const auto size = GetClientSize(); if( size.x < 1 || size.y < 1 ) return;
    const auto& frame = m_owner.m_acquisition.Frame();
    bool changed = !m_trace.IsOk() || m_trace.GetSize() != size || m_cachedSerial != frame.serial
                   || m_cachedStyles.size() != m_owner.m_channels.size();
    for( std::size_t c = 0; !changed && c < m_cachedStyles.size(); ++c )
    {
        const auto& old = m_cachedStyles[c]; const auto& now = m_owner.style( c );
        changed |= old.perDivision != now.perDivision || old.center != now.center || old.inverted != now.inverted || old.visible != now.visible;
    }
    if( !changed ) return;
    m_trace = wxBitmap( size.x, size.y ); wxMemoryDC dc( m_trace ); const auto p = area();
    dc.SetBackground( wxBrush( wxColour( 18, 23, 28 ) ) ); dc.Clear();
    dc.SetPen( wxPen( wxColour( 43, 55, 64 ) ) );
    for( int i = 0; i <= 10; ++i ) dc.DrawLine( p.x + p.width * i / 10, p.y, p.x + p.width * i / 10, p.GetBottom() );
    for( int i = 0; i <= 8; ++i ) dc.DrawLine( p.x, p.y + p.height * i / 8, p.GetRight(), p.y + p.height * i / 8 );
    m_cachedStyles.clear(); m_cachedSerial = frame.serial;
    for( std::size_t c = 0; c < m_owner.m_channels.size(); ++c )
    {
        const auto style = m_owner.style( c ); m_cachedStyles.push_back( style );
        if( !style.visible || frame.end <= frame.begin ) continue;
        const double xScale = p.width / ( frame.end - frame.begin );
        const auto trace = ScopeTrace( frame, m_owner.m_channels[c], p.width );
        std::vector<wxPoint> path; path.reserve( trace.size() );
        const double gain = p.height / ( 8 * style.perDivision ) * ( style.inverted ? -1 : 1 );
        auto py = [&]( double v ) { return p.y + p.height / 2 - int( std::clamp( ( v - style.center ) * gain, -1000000., 1000000. ) ); };
        for( const auto& point : trace )
            path.emplace_back( p.x + std::clamp( int( ( point.time - frame.begin ) * xScale ), 0, p.width ), py( point.value ) );
        dc.SetClippingRegion( p ); dc.SetPen( wxPen( color( c ), 2 ) );
        // A single native polyline replaces thousands of separate Cairo stroke calls.
        if( path.size() > 1 ) dc.DrawLines( path.size(), path.data() );
        dc.DestroyClippingRegion();
    }
    dc.SelectObject( wxNullBitmap );
}
void SCOPE_PLOT::paint( wxPaintEvent& )
{
    rebuild(); wxAutoBufferedPaintDC dc( this );
    if( m_trace.IsOk() ) dc.DrawBitmap( m_trace, 0, 0 );
    else { dc.SetBackground( wxBrush( wxColour( 18, 23, 28 ) ) ); dc.Clear(); }
    const auto p = area(); const auto& frame = m_owner.m_acquisition.Frame(); const auto& pref = m_owner.m_preferences;
    dc.SetFont( GetFont().Smaller() ); dc.SetTextForeground( wxColour( 190, 205, 215 ) );
    if( frame.end > frame.begin )
        for( int i = 0; i <= 10; i += p.width < 450 ? 5 : 2 )
        {
            double t = frame.begin + ( frame.end - frame.begin ) * i / 10 - m_owner.origin();
            if( std::abs( t ) < ( frame.end - frame.begin ) * 1e-10 ) t = 0;
            const auto label = utf8( ScopeFormat( t, "s" ) ); const int width = dc.GetTextExtent( label ).x;
            dc.DrawText( label, std::clamp( p.x + p.width * i / 10 - width / 2, 0, std::max( 0, GetClientSize().x - width ) ), p.GetBottom() + 7 );
        }
    int legend = 3;
    for( std::size_t c = 0; c < m_owner.m_channels.size(); ++c )
    {
        const auto& style = m_owner.style( c ); if( !style.visible ) continue;
        dc.SetTextForeground( color( c ) );
        const auto label = utf8( m_owner.m_channels[c].name + " " + ScopeFormat( style.perDivision, "V" ) + "/div" );
        if( legend < GetClientSize().x - 50 ) { dc.DrawText( label, legend, 6 ); legend += dc.GetTextExtent( label ).x + 12; }
    }
    if( !m_owner.m_channels.empty() )
    {
        const auto selected = m_owner.selected(); dc.SetTextForeground( color( selected ) );
        for( double f : { 0., .5, 1. } )
        {
            const auto label = utf8( ScopeFormat( ScopeVerticalValue( f, m_owner.style( selected ) ), "V" ) );
            dc.DrawText( label, p.GetRight() - dc.GetTextExtent( label ).x - 3, p.y + int( f * ( p.height - 14 ) ) );
        }
        if( pref.trigger.mode != SCOPE_TRIGGER_MODE::ROLL )
        {
            const int level = y( pref.trigger.level, triggerSource() );
            dc.SetClippingRegion( p ); dc.SetPen( wxPen( TRIGGER, 1, wxPENSTYLE_DOT ) );
            dc.DrawLine( p.x, level, p.GetRight(), level ); dc.DestroyClippingRegion();
        }
    }
    if( frame.triggerTime && frame.end > frame.begin )
    {
        const int tx = x( ( *frame.triggerTime - frame.begin ) / ( frame.end - frame.begin ) );
        if( tx >= p.x && tx <= p.GetRight() )
        { dc.SetPen( wxPen( TRIGGER, 1, wxPENSTYLE_DOT ) ); dc.DrawLine( tx, p.y, tx, p.GetBottom() ); dc.SetTextForeground( TRIGGER ); dc.DrawText( "T", tx + 3, p.y ); }
    }
    // These hit targets are deliberately outside the trace clipping area.
    for( const auto& marker : markers() )
    {
        const auto r = marker.bounds; const auto ink = marker.channel < 0 ? TRIGGER : color( marker.channel );
        const int tip = r.GetRight(), cy = r.y + 8;
        const wxPoint arrow[] = { { tip - 7, cy - 5 }, { tip, cy }, { tip - 7, cy + 5 } };
        dc.SetPen( wxPen( ink ) ); dc.SetBrush( marker.outside ? *wxTRANSPARENT_BRUSH : wxBrush( ink ) ); dc.DrawPolygon( 3, arrow );
        dc.SetTextForeground( ink );
        wxString label = marker.channel < 0 ? wxString( "T" ) : utf8( m_owner.m_channels[marker.channel].name );
        while( !label.empty() && dc.GetTextExtent( label ).x > r.width - 10 ) label.RemoveLast();
        dc.DrawText( label, r.x, cy - 7 );
        if( marker.outside ) dc.DrawText( "!", tip - 5, cy - 8 );
    }
    if( !m_owner.m_channels.empty() && pref.cursorMode )
    {
        dc.SetClippingRegion( p ); dc.SetPen( wxPen( wxColour( 230, 235, 240 ), 1, wxPENSTYLE_SHORT_DASH ) ); dc.SetTextForeground( wxColour( 230, 235, 240 ) );
        if( pref.cursorMode & 1 )
        {
            dc.DrawLine( x( pref.cursorA ), p.y, x( pref.cursorA ), p.GetBottom() ); dc.DrawText( "A", x( pref.cursorA ) + 3, p.y + 18 );
            dc.DrawLine( x( pref.cursorB ), p.y, x( pref.cursorB ), p.GetBottom() ); dc.DrawText( "B", x( pref.cursorB ) + 3, p.y + 34 );
        }
        if( pref.cursorMode & 2 )
        {
            const int a = y( pref.levelA, m_owner.selected() ), b = y( pref.levelB, m_owner.selected() );
            dc.DrawLine( p.x, a, p.GetRight(), a ); dc.DrawText( "YA", p.x + 4, a - 16 );
            dc.DrawLine( p.x, b, p.GetRight(), b ); dc.DrawText( "YB", p.x + 4, b - 16 );
        }
        dc.DestroyClippingRegion();
    }
    if( frame.samples.empty() ) { dc.SetTextForeground( wxColour( 205, 220, 225 ) ); dc.DrawText( _( "Waiting for capture" ), p.x + 8, p.y + 60 ); }
}
}
