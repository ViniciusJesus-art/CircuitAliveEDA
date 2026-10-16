/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "schematic_adapter.h"
#include <nlohmann/json.hpp>
#include <project.h>
#include <sch_connection.h>
#include <sch_field.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <sch_symbol.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <schematic.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
CIRCUIT_COMPONENT ReadSymbolModelSettings( SCH_SYMBOL& symbol, SCH_SHEET_PATH& sheet,
        const wxString& variant, const std::string& projectDirectory )
{
    CIRCUIT_COMPONENT component;
    component.libraryId = symbol.GetLibId().Format().c_str();
    component.reference = symbol.GetRef( &sheet ).ToStdString();
    component.value = symbol.GetValue( true, &sheet, true, variant ).ToStdString();
    component.projectDirectory = projectDirectory;
    auto field = [&]( const wxString& name ) -> std::string
    {
        const SCH_FIELD* f = symbol.GetField( name );
        return f ? f->GetShownText( &sheet, false, 0, variant ).ToStdString() : std::string();
    };
    component.deviceHint = field( "Sim.Device" );
    component.modelFile = field( "CA.Model" );
    auto map = [&]( const wxString& name ) -> std::map<std::string, std::string>
    {
        const auto value = field( name );
        if( value.empty() ) return {};
        try { return nlohmann::json::parse( value ).get<std::map<std::string, std::string>>(); }
        catch( const std::exception& )
            { throw std::runtime_error( component.reference + ": invalid " + name.ToStdString() ); }
    };
    component.modelPins = map( "CA.Pins" );
    component.modelParameters = map( "CA.Parameters" );
    const auto measurements = field( "CA.Measurements" );
    if( !measurements.empty() )
    {
        try
        {
            auto j = nlohmann::json::parse( measurements );
            component.measurements.voltage = j.value( "voltage", false );
            component.measurements.current = j.value( "current", false );
            component.measurements.power = j.value( "power", false );
            component.measurements.positive = j.value( "positive", "" );
            component.measurements.negative = j.value( "negative", "" );
        }
        catch( const std::exception& ) { throw std::runtime_error( component.reference + ": invalid CA.Measurements" ); }
    }
    for( const wxString name : { "Sim.Library", "Sim.Name", "Sim.Params", "Sim.Device", "Sim.Type", "Sim.Pins" } )
        if( auto value = field( name ); !value.empty() ) component.legacySpiceFields[name.ToStdString()] = value;
    component.explicitSpiceModel = !component.legacySpiceFields.empty();
    for( const SCH_PIN* pin : symbol.GetPins( &sheet ) )
        component.pins[pin->GetShownNumber().ToStdString()] = "";
    return component;
}

void WriteSymbolModelSettings( SCH_SYMBOL& symbol, const CIRCUIT_COMPONENT& settings )
{
    const std::map<wxString, std::string> fields = {
        { "CA.Model", settings.modelFile },
        { "CA.Pins", settings.modelPins.empty() ? "" : nlohmann::json( settings.modelPins ).dump() },
        { "CA.Parameters", settings.modelParameters.empty() ? "" : nlohmann::json( settings.modelParameters ).dump() },
        { "CA.Measurements", !settings.measurements.Any() ? "" : nlohmann::json{
            { "voltage", settings.measurements.voltage }, { "current", settings.measurements.current },
            { "power", settings.measurements.power }, { "positive", settings.measurements.positive },
            { "negative", settings.measurements.negative } }.dump() }
    };
    for( const auto& [name, value] : fields )
    {
        if( value.empty() ) { symbol.RemoveField( name ); continue; }
        auto* field = symbol.GetField( name );
        if( !field ) field = symbol.AddField( SCH_FIELD( &symbol, FIELD_T::USER, name ) );
        field->SetText( wxString::FromUTF8( value ) );
        field->SetVisible( false );
    }
}

SCHEMATIC_SNAPSHOT CaptureSchematic( SCHEMATIC& aSchematic )
{
    SCHEMATIC_SNAPSHOT result;
    std::vector<CIRCUIT_COMPONENT> components;
    std::vector<CIRCUIT_PROBE> probes;
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
            auto* probeField = symbol->GetField( "CA.Probe" );
            if( probeField )
            {
                CIRCUIT_PROBE probe;
                probe.kind = ParseProbeKind( probeField->GetText().ToStdString() );
                probe.name = symbol->GetField( FIELD_T::VALUE )->GetText().ToStdString();
                if( auto* f = symbol->GetField( "CA.ProbeSequence" ) )
                {
                    unsigned long index = 1; f->GetText().ToULong( &index ); probe.sequence = std::max<unsigned long>( 1, index );
                }
                for( const auto* pin : symbol->GetPins( &sheet ) )
                {
                    auto* connection = pin->Connection( &sheet );
                    auto name = connection ? connection->Name() : wxString();
                    if( connection && ( connection->IsUnconnected() || connection->LocalName().StartsWith( "unconnected-(" ) ) ) name.clear();
                    if( connection && ( connection->LocalName() == "GND" || connection->LocalName() == "0" ) ) name = "0";
                    probe.pins[pin->GetShownNumber().ToStdString()] = name.ToStdString();
                    if( pin->GetShownNumber() == "1" && connection )
                    {
                        auto label = connection->LocalName();
                        if( !label.StartsWith( "Net-(" ) && !label.StartsWith( "unconnected-(" ) ) probe.label = label.ToStdString();
                    }
                }
                const auto at = std::upper_bound( probes.begin(), probes.end(), probe.sequence,
                    []( std::size_t sequence, const CIRCUIT_PROBE& p ) { return sequence < p.sequence; } ) - probes.begin();
                probes.insert( probes.begin() + at, std::move( probe ) );
                result.probes.insert( result.probes.begin() + at, { sheet.PathAsString(), symbol->m_Uuid.AsString(),
                    symbol->GetRef( &sheet ), symbol->GetBodyBoundingBox(), symbol->GetPosition() } );
                continue;
            }
            if( symbol->IsPower() || lib.starts_with( "power:" )
                || symbol->GetExcludedFromSim( &sheet, variant ) )
                continue;

            CIRCUIT_COMPONENT component = ReadSymbolModelSettings( *symbol, sheet, variant,
                    aSchematic.Project().GetProjectPath().ToStdString() );

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
    result.circuit = BuildCircuitSnapshot( components, probes );
    return result;
}
}
