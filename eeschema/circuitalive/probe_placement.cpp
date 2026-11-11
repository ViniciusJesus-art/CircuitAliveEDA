/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe_placement.h"
#include "probe_geometry.h"
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
#include <set>
#include <limits>

namespace CIRCUITALIVE
{
static bool Series( PROBE_KIND kind ) { return kind == PROBE_KIND::CURRENT || kind == PROBE_KIND::POWER; }

PROBE_PLACEMENT PlanProbePlacement( SCH_SCREEN& screen, const VECTOR2I& position,
                                   int tolerance, PROBE_KIND kind, const SCH_SHEET_PATH* sheet, bool allowFloating )
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
    if( !plan.wire )
    {
        if( allowFloating )
        {
            plan.anchor = position;
            plan.first = plan.second = position;
            if( Series( kind ) )
            {
                const VECTOR2I delta( schIUScale.mmToIU( 1.27 ), 0 );
                plan.first -= delta;
                plan.second += delta;
            }
            return plan;
        }
        throw std::runtime_error( Series( kind )
                ? "Current and power probes need a wire section for their series sensor"
                : "Click on an electrical wire or a component pin endpoint to place the probe" );
    }
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
        shape->SetStroke( STROKE_PARAMS( schIUScale.mmToIU( IsSourceProbe( kind ) ? SOURCE_PROBE_STROKE : .18 ), LINE_STYLE::SOLID ) );
        lib.AddDrawItem( shape );
    };
    if( IsSourceProbe( kind ) )
    {
        auto point = []( double x, double y ) { return VECTOR2I( schIUScale.mmToIU( x ), schIUScale.mmToIU( y ) ); };
        std::vector<VECTOR2I> points;
        for( auto [x, y] : SOURCE_PROBE_OUTLINE ) points.push_back( point( x, y ) );
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
    if( IsSourceProbe( kind ) )
    {
        WriteSourceProbeSettings( *symbol, kind, settings );
        SCH_FIELD appearance( symbol.get(), FIELD_T::USER, "CA.ProbeAppearance" );
        appearance.SetText( SOURCE_PROBE_APPEARANCE ); appearance.SetVisible( false ); symbol->AddField( appearance );
    }
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
        state->SetHorizJustify( GR_TEXT_H_ALIGN_CENTER ); state->SetVertJustify( GR_TEXT_V_ALIGN_CENTER );
    }
    state->SetPosition( symbol.GetPosition() + symbol.GetTransform().TransformCoordinate(
            VECTOR2I( schIUScale.mmToIU( SOURCE_PROBE_TEXT_X ), 0 ) ) );
    const int textSize = schIUScale.mmToIU( kind == PROBE_KIND::LOGIC
            ? SOURCE_PROBE_LOGIC_TEXT_SIZE : SOURCE_PROBE_WAVE_TEXT_SIZE );
    state->SetTextSize( VECTOR2I( textSize, textSize ) );
    state->SetText( kind == PROBE_KIND::LOGIC ? ( settings.initialHigh ? "1" : "0" )
                    : kind == PROBE_KIND::PWM ? "PWM" : kind == PROBE_KIND::SINE ? "~" : "CLK" );
    state->SetVisible( true ); state->SetNameShown( false );
}

std::size_t NextProbeSequence( SCHEMATIC& schematic )
{
    std::size_t sequence = 1;
    auto reserve = [&]( const wxString& text )
    {
        unsigned long long index = 0;
        if( !text.ToULongLong( &index ) ) return;
        if( index >= std::numeric_limits<std::size_t>::max() )
            throw std::runtime_error( "Probe sequence is too large" );
        sequence = std::max( sequence, std::size_t( index ) + 1 );
    };
    for( auto& sheet : schematic.Hierarchy() )
        for( const auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            const auto* symbol = static_cast<const SCH_SYMBOL*>( item );
            if( auto* field = symbol->GetField( "CA.ProbeSequence" ) ) reserve( field->GetText() );
            const auto reference = symbol->GetRef( &sheet );
            if( reference.StartsWith( "#P" ) ) reserve( reference.Mid( 2 ) );
        }
    return sequence;
}

void EnsureUniqueProbeIdentities( SCHEMATIC& schematic, const std::set<SCH_SYMBOL*>& newSymbols,
                                  SCH_COMMIT* commit )
{
    std::size_t next = NextProbeSequence( schematic );
    std::set<wxString> references;
    std::map<unsigned long long, SCH_SYMBOL*> sequences;
    std::set<SCH_SYMBOL*> visited, modified;
    // Ordinary symbol references also reserve names, but are never changed here.
    for( auto& sheet : schematic.Hierarchy() )
        for( auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            auto* symbol = static_cast<SCH_SYMBOL*>( item );
            if( !symbol->GetField( "CA.Probe" ) ) references.insert( symbol->GetRef( &sheet ) );
        }
    for( auto& sheet : schematic.Hierarchy() )
        for( auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            auto* symbol = static_cast<SCH_SYMBOL*>( item );
            if( !symbol->GetField( "CA.Probe" ) ) continue;
            const auto reference = symbol->GetRef( &sheet );
            const auto* field = symbol->GetField( "CA.ProbeSequence" );
            unsigned long long index = 0;
            const bool firstOccurrence = visited.insert( symbol ).second;
            const bool validSequence = field && field->GetText().ToULongLong( &index ) && index > 0;
            const bool repeatedSequence = validSequence && sequences.contains( index ) && sequences.at( index ) != symbol;
            const bool fresh = newSymbols.contains( symbol ) && firstOccurrence;
            if( fresh || reference.IsEmpty() || reference.Contains( '?' ) || references.contains( reference )
                || ( firstOccurrence && ( !validSequence || repeatedSequence ) ) )
            {
                if( commit && !newSymbols.contains( symbol ) && modified.insert( symbol ).second )
                    commit->Modify( symbol, sheet.LastScreen() );
                if( next == std::numeric_limits<std::size_t>::max() )
                    throw std::runtime_error( "Probe sequence is too large" );
                index = next++;
                symbol->SetRef( &sheet, wxString::Format( "#P%zu", std::size_t( index ) ) );
                auto* sequence = symbol->GetField( "CA.ProbeSequence" );
                if( !sequence ) sequence = symbol->AddField( SCH_FIELD( symbol, FIELD_T::USER, "CA.ProbeSequence" ) );
                sequence->SetText( wxString::Format( "%zu", std::size_t( index ) ) );
                sequence->SetVisible( false );
            }
            sequences[index] = symbol;
            references.insert( symbol->GetRef( &sheet ) );
        }
}

void NormalizeSourceProbeSymbols( SCHEMATIC& schematic )
{
    std::set<SCH_SCREEN*> seen;
    for( auto& sheet : schematic.Hierarchy() )
    {
        auto* screen = sheet.LastScreen();
        if( !seen.insert( screen ).second ) continue;
        std::vector<SCH_SYMBOL*> probes;
        for( auto* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            auto* symbol = static_cast<SCH_SYMBOL*>( item );
            const auto* kind = symbol->GetField( "CA.Probe" );
            const auto* appearance = symbol->GetField( "CA.ProbeAppearance" );
            if( !kind || ( appearance && appearance->GetText() == SOURCE_PROBE_APPEARANCE ) ) continue;
            if( kind->GetText() == "logic" || kind->GetText() == "pwm"
                || kind->GetText() == "sine" || kind->GetText() == "square" ) probes.push_back( symbol );
        }
        for( auto* symbol : probes )
        {
            const auto kind = ParseProbeKind( symbol->GetField( "CA.Probe" )->GetText().ToStdString() );
            SOURCE_PROBE_SETTINGS settings;
            try { settings = ReadSourceProbeSettings( *symbol ); }
            catch( const std::exception& ) { continue; } // Keep malformed settings editable.
            auto prototype = MakeProbeSymbol( kind, sheet, symbol->GetPosition(),
                    wxString( symbol->GetLibId().GetLibItemName() ).EndsWith( "_vertical" ), 1, "", settings );
            auto lib = std::make_unique<LIB_SYMBOL>( *prototype->GetLibSymbolRef() );
            screen->Remove( symbol );
            symbol->SetLibSymbol( lib.release() );
            WriteSourceProbeSettings( *symbol, kind, settings );
            auto* appearance = symbol->GetField( "CA.ProbeAppearance" );
            if( !appearance ) appearance = symbol->AddField( SCH_FIELD( symbol, FIELD_T::USER, "CA.ProbeAppearance" ) );
            appearance->SetText( SOURCE_PROBE_APPEARANCE ); appearance->SetVisible( false );
            AlignProbeForPlacement( *symbol, PlanProbePlacement( *screen, symbol->GetPosition(), 1, kind, &sheet, true ) );
            screen->Append( symbol );
        }
    }
}

void AlignProbeForPlacement( SCH_SYMBOL& symbol, const PROBE_PLACEMENT& plan )
{
    symbol.Move( plan.anchor - symbol.GetPosition() );
    if( plan.pin )
    {
        auto outward = plan.pin->GetPosition() - plan.pin->GetPinRoot();
        if( outward == VECTOR2I( 0, 0 ) )
            outward = plan.pin->GetPosition() - plan.pin->GetParentSymbol()->GetPosition();
        // Use the transformed body, so both sources and measurement arrows
        // extend away from the component rather than covering it.
        for( int turns = 0; turns < 4; ++turns )
        {
            const auto body = symbol.GetBodyBoundingBox().GetCenter() - symbol.GetPosition();
            if( double( body.x ) * outward.x + double( body.y ) * outward.y >= 1 ) break;
            if( outward == VECTOR2I( 0, 0 ) ) break;
            symbol.Rotate( plan.anchor, true );
        }
    }
    const auto pins = symbol.GetPins();
    if( plan.wire && pins.size() == 2
        && ( pins[0]->GetPosition().x == pins[1]->GetPosition().x ) != plan.vertical )
        symbol.Rotate( plan.anchor, true );
}

void PlaceProbe( SCH_EDIT_FRAME& frame, PROBE_KIND kind, const std::string& name,
                 const VECTOR2I& position, int tolerance, const SOURCE_PROBE_SETTINGS& settings,
                 bool allowFloating, const SCH_SYMBOL* preview )
{
    auto* screen = frame.GetScreen();
    auto target = allowFloating ? position : frame.GetNearestGridPosition( position );
    if( !Series( kind ) )
        if( const auto* pin = FindPinConnectionAt( *screen, &frame.GetCurrentSheet(), position, tolerance ) )
            target = pin->GetPosition();
    const auto plan = PlanProbePlacement( *screen, target, tolerance, kind, &frame.GetCurrentSheet(), allowFloating );
    const auto sequence = NextProbeSequence( frame.Schematic() );
    auto symbol = preview ? std::make_unique<SCH_SYMBOL>( *preview )
                          : MakeProbeSymbol( kind, frame.GetCurrentSheet(), plan.anchor, plan.vertical, sequence, name, settings );
    // A preview may outlive other placements. Allocate its identity again at commit.
    symbol->SetRef( &frame.GetCurrentSheet(), wxString::Format( "#P%zu", sequence ) );
    symbol->GetField( "CA.ProbeSequence" )->SetText( wxString::Format( "%zu", sequence ) );
    AlignProbeForPlacement( *symbol, plan );
    symbol->ClearFlags();
    symbol->RunOnChildren( []( SCH_ITEM* item ) { item->ClearFlags(); }, RECURSE_MODE::RECURSE );
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
