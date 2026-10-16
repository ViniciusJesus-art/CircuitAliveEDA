/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "schematic_adapter.h"
#include "component_catalog.h"
#include <sch_connection.h>
#include <sch_field.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <sch_symbol.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <schematic.h>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
SCHEMATIC_SNAPSHOT CaptureSchematic( SCHEMATIC& aSchematic )
{
    SCHEMATIC_SNAPSHOT result;
    std::vector<CIRCUIT_COMPONENT> components;
    const wxString variant = aSchematic.GetCurrentVariant();
    for( SCH_SHEET_PATH& sheet : aSchematic.Hierarchy() )
    {
        if( sheet.GetExcludedFromSim( variant ) )
            continue;
        for( SCH_ITEM* item : sheet.LastScreen()->Items() )
        {
            if( item->ResolveExcludedFromSim() )
                continue;
            wxString text;
            if( item->Type() == SCH_TEXT_T )
                text = static_cast<SCH_TEXT*>( item )->GetShownText( &sheet, false );
            else if( item->Type() == SCH_TEXTBOX_T )
                text = static_cast<SCH_TEXTBOX*>( item )->GetShownText( nullptr, &sheet, false );
            std::istringstream lines( text.ToStdString() );
            std::string line;
            while( std::getline( lines, line ) )
            {
                auto start = line.find_first_not_of( " \t\r" );
                if( start != std::string::npos && line[start] == '.' )
                    throw std::runtime_error( "SPICE text directives are not supported in interactive mode. "
                            "Remove/exclude them or use the traditional Simulator. Sheet: "
                            + sheet.PathAsString().ToStdString() );
            }
        }
        for( SCH_ITEM* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            auto* symbol = static_cast<SCH_SYMBOL*>( item );
            std::string lib = symbol->GetLibId().Format().c_str();
            if( symbol->IsPower() || lib.starts_with( "power:" )
                || symbol->GetExcludedFromSim( &sheet, variant ) )
                continue;

            CIRCUIT_COMPONENT component;
            component.libraryId = lib;
            component.reference = symbol->GetRef( &sheet ).ToStdString();
            component.value = symbol->GetValue( true, &sheet, true, variant ).ToStdString();
            auto field = [&]( const wxString& name ) -> wxString
            {
                const SCH_FIELD* f = symbol->GetField( name );
                return f ? f->GetShownText( &sheet, false, 0, variant ) : wxString();
            };
            component.deviceHint = field( "Sim.Device" ).ToStdString();
            const auto behavior = FindBuiltinBehavior( lib, component.deviceHint );
            bool vdc = behavior == BUILTIN_BEHAVIOR::DC_VOLTAGE;
            bool led = behavior == BUILTIN_BEHAVIOR::RED_LED;
            component.explicitSpiceModel = !field( "Sim.Library" ).empty()
                    || !field( "Sim.Name" ).empty() || !field( "Sim.Params" ).empty()
                    || ( !field( "Sim.Device" ).empty() && !( vdc && field( "Sim.Device" ) == "V" ) )
                    || ( !field( "Sim.Type" ).empty() && !( vdc && field( "Sim.Type" ) == "DC" ) )
                    || ( !field( "Sim.Pins" ).empty()
                         && !( vdc && field( "Sim.Pins" ) == "1=+ 2=-" )
                         && !( led && field( "Sim.Pins" ) == "1=K 2=A" ) );

            for( const SCH_PIN* pin : symbol->GetPins( &sheet ) )
            {
                SCH_CONNECTION* connection = pin->Connection( &sheet );
                std::string name = connection ? connection->Name().ToStdString() : std::string();
                if( connection && ( connection->IsUnconnected()
                                     || connection->LocalName().StartsWith( "unconnected-(" ) ) )
                    name.clear();
                if( connection && ( connection->LocalName() == "GND" || connection->LocalName() == "0" ) )
                    name = "0";
                component.pins[pin->GetShownNumber().ToStdString()] = std::move( name );
            }
            components.push_back( std::move( component ) );
            result.locations.push_back( { sheet.PathAsString(), symbol->m_Uuid.AsString(),
                                          symbol->GetRef( &sheet ), symbol->GetBodyBoundingBox(),
                                          symbol->GetPosition() } );
        }
    }
    result.circuit = BuildCircuitSnapshot( components );
    return result;
}
}
