/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe_placement.h"
#include <base_units.h>
#include <lib_symbol.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_field.h>
#include <sch_line.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_shape.h>
#include <sch_symbol.h>
#include <sch_text.h>
#include <tools/sch_tool_utils.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CIRCUITALIVE
{
static bool Series( PROBE_KIND kind ) { return kind == PROBE_KIND::CURRENT || kind == PROBE_KIND::POWER; }

PROBE_PLACEMENT PlanProbePlacement( SCH_SCREEN& screen, const VECTOR2I& position,
                                   int tolerance, PROBE_KIND kind, const SCH_SHEET_PATH* sheet )
{
    PROBE_PLACEMENT plan;
    if( !Series( kind ) ) plan.pin = FindPinConnectionAt( screen, sheet, position, tolerance );
    const auto target = plan.pin ? plan.pin->GetPosition() : position;
    double best = tolerance + 1;
    for( SCH_ITEM* item : screen.Items().OfType( SCH_LINE_T ) )
    {
        auto* wire = static_cast<SCH_LINE*>( item );
        if( wire->GetLayer() != LAYER_WIRE ) continue;
        const auto a = wire->GetStartPoint(), b = wire->GetEndPoint();
        const auto delta = b - a;
        const double length2 = double( delta.x ) * delta.x + double( delta.y ) * delta.y;
        if( length2 == 0 ) continue;
        const double fraction = std::clamp( ( double( target.x - a.x ) * delta.x
                + double( target.y - a.y ) * delta.y ) / length2, 0.0, 1.0 );
        VECTOR2I point( std::lround( a.x + fraction * delta.x ), std::lround( a.y + fraction * delta.y ) );
        const double distance = std::hypot( point.x - target.x, point.y - target.y );
        if( plan.pin && point != target ) continue;
        if( distance < best ) { best = distance; plan.wire = wire; plan.anchor = point; }
    }
    if( plan.pin )
    {
        plan.anchor = plan.first = plan.second = target;
        const auto root = plan.pin->GetPinRoot();
        plan.vertical = root.x == target.x && root.y != target.y;
        return plan;
    }
    if( !plan.wire ) throw std::runtime_error( Series( kind )
            ? "Current and power probes need a wire section for their series sensor"
            : "Click on an electrical wire or a component pin endpoint to place the probe" );
    const auto start = plan.wire->GetStartPoint(), end = plan.wire->GetEndPoint();
    plan.vertical = start.x == end.x;
    plan.first = plan.second = plan.anchor;
    if( Series( kind ) )
    {
        if( start.x != end.x && start.y != end.y )
            throw std::runtime_error( "Place current and power probes on a horizontal or vertical wire" );
        const int half = schIUScale.mmToIU( 1.27 );
        auto delta = plan.vertical ? VECTOR2I( 0, half ) : VECTOR2I( half, 0 );
        plan.first -= delta; plan.second += delta;
        auto coordinate = [&]( const VECTOR2I& p ) { return plan.vertical ? p.y : p.x; };
        const int low = std::min( coordinate( start ), coordinate( end ) );
        const int high = std::max( coordinate( start ), coordinate( end ) );
        if( coordinate( plan.first ) < low || coordinate( plan.second ) > high )
            throw std::runtime_error( "Choose a longer wire section, away from the endpoints" );
        auto inside = [&]( const VECTOR2I& p )
        {
            return ( plan.vertical ? p.x == plan.anchor.x : p.y == plan.anchor.y )
                    && coordinate( p ) > coordinate( plan.first ) && coordinate( p ) < coordinate( plan.second );
        };
        for( const auto* item : screen.Items() )
        {
            if( item == plan.wire ) continue;
            for( const auto& point : item->GetConnectionPoints() )
                if( inside( point ) ) throw std::runtime_error( "Place the probe away from junctions, labels and component pins" );
            if( item->Type() == SCH_LINE_T )
            {
                const auto* line = static_cast<const SCH_LINE*>( item );
                if( line->GetLayer() == LAYER_WIRE && line->HitTest( plan.anchor, 0 ) )
                {
                    const auto a = line->GetStartPoint(), b = line->GetEndPoint();
                    if( ( plan.vertical && a.x == b.x ) || ( !plan.vertical && a.y == b.y ) )
                        throw std::runtime_error( "Overlapping wires must be cleaned before placing a current probe" );
                }
            }
        }
    }
    return plan;
}

std::unique_ptr<SCH_SYMBOL> MakeProbeSymbol( PROBE_KIND kind, SCH_SHEET_PATH& sheet,
        const VECTOR2I& position, bool vertical, std::size_t sequence, const std::string& name,
        const SOURCE_PROBE_SETTINGS& settings )
{
    const auto symbolName = wxString::FromUTF8( "Probe_" + ProbeKindName( kind ) + ( vertical ? "_vertical" : "" ) );
    LIB_SYMBOL lib( symbolName );
    lib.SetUnitCount( 1, false ); lib.SetShowPinNames( false ); lib.SetShowPinNumbers( false );
    lib.GetReferenceField().SetText( "#P" );
    lib.SetExcludedFromBOM( true ); lib.SetExcludedFromBoard( true ); lib.SetExcludedFromPosFiles( true );
    const int half = schIUScale.mmToIU( 1.27 ), arrow = schIUScale.mmToIU( 2.54 );
    auto line = [&]( VECTOR2I a, VECTOR2I b )
    {
        // Library symbols serialize line segments as two-point polylines.
        auto* shape = new SCH_SHAPE( SHAPE_T::POLY, LAYER_DEVICE );
        shape->SetPolyPoints( { a, b } ); shape->SetUnit( 1 ); shape->SetBodyStyle( 1 );
        shape->SetStroke( STROKE_PARAMS( schIUScale.mmToIU( .18 ), LINE_STYLE::SOLID ) );
        lib.AddDrawItem( shape );
    };
    if( IsSourceProbe( kind ) )
    {
        auto point = []( double x, double y ) { return VECTOR2I( schIUScale.mmToIU( x ), schIUScale.mmToIU( y ) ); };
        const std::vector<VECTOR2I> points = { point(-5,-1.8), point(-1.8,-1.8), point(-.6,0),
                                              point(-1.8,1.8), point(-5,1.8), point(-5,-1.8) };
        for( std::size_t i = 1; i < points.size(); ++i ) line( points[i-1], points[i] );
        line( point(-.6,0), {0,0} );
    }
    else
    {
        line( { 0, 0 }, { arrow, -arrow } );
        line( { arrow / 3, -arrow }, { arrow, -arrow } );
        line( { arrow, -arrow }, { arrow, -arrow / 3 } );
        const wxString unit = kind == PROBE_KIND::VOLTAGE ? "V" : kind == PROBE_KIND::CURRENT ? "A"
                : kind == PROBE_KIND::POWER ? "W" : "~";
        auto* text = new SCH_TEXT( VECTOR2I( arrow + half, -arrow ), unit, LAYER_DEVICE );
        text->SetTextSize( VECTOR2I( schIUScale.mmToIU( 1 ), schIUScale.mmToIU( 1 ) ) );
        text->SetUnit( 1 ); text->SetBodyStyle( 1 ); lib.AddDrawItem( text );
    }
    auto pin = [&]( const wxString& number, VECTOR2I point )
    {
        auto* terminal = new SCH_PIN( &lib );
        terminal->SetNumber( number ); terminal->SetName( "" ); terminal->SetLength( 0 );
        terminal->SetType( IsSourceProbe( kind ) ? ELECTRICAL_PINTYPE::PT_OUTPUT : ELECTRICAL_PINTYPE::PT_PASSIVE );
        terminal->SetPosition( point );
        terminal->SetUnit( 1 ); terminal->SetBodyStyle( 1 ); lib.AddDrawItem( terminal );
    };
    if( Series( kind ) )
    {
        // KiCad 10 runtime library coordinates use Y downwards, like the schematic.
        // Physical pin 1 is left/top; pin 2 is right/bottom.
        VECTOR2I delta = vertical ? VECTOR2I( 0, half ) : VECTOR2I( half, 0 );
        pin( "1", -delta ); pin( "2", delta ); line( -delta, delta );
        line( { -half / 2, half / 2 }, { half / 2, half / 2 } );
    }
    else pin( "1", VECTOR2I( 0, 0 ) );
    auto symbol = std::make_unique<SCH_SYMBOL>( lib, LIB_ID( "CircuitAlive_Instruments", symbolName ),
                                               &sheet, 1, 1, position );
    symbol->SetRef( &sheet, wxString::Format( "#P%zu", sequence ) );
    symbol->GetField( FIELD_T::VALUE )->SetText( wxString::FromUTF8( name ) );
    for( auto& field : symbol->GetFields() ) field.SetVisible( false );
    for( auto [key, value] : { std::pair<std::string, std::string>{ "CA.Probe", ProbeKindName( kind ) },
                             { "CA.ProbeSequence", std::to_string( sequence ) } } )
    {
        SCH_FIELD field( symbol.get(), FIELD_T::USER, wxString::FromUTF8( key ) );
        field.SetText( wxString::FromUTF8( value ) ); field.SetVisible( false ); symbol->AddField( field );
    }
    if( IsSourceProbe( kind ) ) WriteSourceProbeSettings( *symbol, kind, settings );
    return symbol;
}

SOURCE_PROBE_SETTINGS ReadSourceProbeSettings( const SCH_SYMBOL& symbol )
{
    const auto* kind = symbol.GetField( "CA.Probe" );
    if( !kind ) throw std::runtime_error( "Not a simulation probe" );
    const auto* field = symbol.GetField( "CA.ProbeSettings" );
    return ParseSourceProbe( ParseProbeKind( kind->GetText().ToStdString() ),
                              field ? field->GetText().ToStdString() : "" );
}

void WriteSourceProbeSettings( SCH_SYMBOL& symbol, PROBE_KIND kind, const SOURCE_PROBE_SETTINGS& settings )
{
    const auto json = SerializeSourceProbe( kind, settings );
    auto* field = symbol.GetField( "CA.ProbeSettings" );
    if( !field ) field = symbol.AddField( SCH_FIELD( &symbol, FIELD_T::USER, "CA.ProbeSettings" ) );
    field->SetText( wxString::FromUTF8( json ) ); field->SetVisible( false );
    auto* state = symbol.GetField( "CA.ProbeState" );
    if( !state )
    {
        state = symbol.AddField( SCH_FIELD( &symbol, FIELD_T::USER, "CA.ProbeState" ) );
        state->SetPosition( symbol.GetPosition() + VECTOR2I( schIUScale.mmToIU(-3.2), 0 ) );
        state->SetTextSize( VECTOR2I( schIUScale.mmToIU(1.1), schIUScale.mmToIU(1.1) ) );
        state->SetHorizJustify( GR_TEXT_H_ALIGN_CENTER ); state->SetVertJustify( GR_TEXT_V_ALIGN_CENTER );
    }
    state->SetText( kind == PROBE_KIND::LOGIC ? ( settings.initialHigh ? "1" : "0" )
                    : kind == PROBE_KIND::PWM ? "PWM" : kind == PROBE_KIND::SINE ? "~" : "CLK" );
    state->SetVisible( true ); state->SetNameShown( false );
}

void PlaceProbe( SCH_EDIT_FRAME& frame, PROBE_KIND kind, const std::string& name,
                 const VECTOR2I& position, int tolerance, const SOURCE_PROBE_SETTINGS& settings )
{
    auto* screen = frame.GetScreen();
    auto target = frame.GetNearestGridPosition( position );
    if( !Series( kind ) )
        if( const auto* pin = FindPinConnectionAt( *screen, &frame.GetCurrentSheet(), position, tolerance ) )
            target = pin->GetPosition();
    const auto plan = PlanProbePlacement( *screen, target, tolerance, kind, &frame.GetCurrentSheet() );
    std::size_t sequence = 1;
    for( auto& sheet : frame.Schematic().Hierarchy() )
        for( const auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
            if( auto* field = static_cast<const SCH_SYMBOL*>( item )->GetField( "CA.ProbeSequence" ) )
            {
                unsigned long index = 0; field->GetText().ToULong( &index ); sequence = std::max<std::size_t>( sequence, index + 1 );
            }
    auto symbol = MakeProbeSymbol( kind, frame.GetCurrentSheet(), plan.anchor, plan.vertical, sequence, name, settings );
    auto* wire = plan.wire;
    SCH_COMMIT commit( &frame );
    if( wire )
    {
        const auto a = wire->GetStartPoint(), b = wire->GetEndPoint();
        auto first = plan.first, second = plan.second;
        if( double( b.x - a.x ) * ( second.x - first.x ) + double( b.y - a.y ) * ( second.y - first.y ) < 0 )
            std::swap( first, second );
        auto addWire = [&]( VECTOR2I start, VECTOR2I end )
        {
            if( start == end ) return;
            auto* line = new SCH_LINE( start, LAYER_WIRE ); line->SetEndPoint( end ); line->SetStroke( wire->GetStroke() );
            commit.Add( line, screen );
        };
        commit.Remove( wire, screen );
        addWire( a, first ); addWire( second, b );
    }
    commit.Add( symbol.release(), screen );
    commit.Push( _( "Place simulation probe" ) );
}
}
