/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_model.h"
#include "circuit_snapshot.h"
#include "behavioral_model.h"
#include "control_visual_json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sim/platform_runtime.h>

namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
namespace fs = std::filesystem;

static bool Identifier( const std::string& text )
{
    return !text.empty() && text.size() <= 80 && std::all_of( text.begin(), text.end(),
        []( unsigned char c ) { return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
                || ( c >= '0' && c <= '9' ) || c == '_'; } );
}

static std::string FoldName( std::string value )
{
    const auto first = value.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    value = value.substr( first, value.find_last_not_of( " \t\r\n" ) - first + 1 );
    for( auto& c : value ) if( static_cast<unsigned char>( c ) < 128 )
        c = std::toupper( static_cast<unsigned char>( c ) );
    return value;
}

static bool HasPartName( const SIMULATION_MODEL& model, const std::string& name )
{
    const auto folded = FoldName( name );
    return !folded.empty() && std::any_of( model.partNames.begin(), model.partNames.end(),
            [&]( const auto& alias ) { return FoldName( alias ) == folded; } );
}

static SIMULATION_MODEL BindNamedPins( SIMULATION_MODEL model, const CIRCUIT_COMPONENT& component )
{
    std::set<std::string> used;
    for( auto& port : model.pins )
    {
        std::string physical;
        const auto& aliases = model.pinNames.at( port.id );
        for( const auto& [number, name] : component.pinNames )
            if( component.pins.contains( number ) && std::any_of( aliases.begin(), aliases.end(),
                    [&]( const auto& alias ) { return FoldName( alias ) == FoldName( name ); } ) )
            {
                if( !physical.empty() ) throw std::runtime_error( "Ambiguous physical pin for model port " + port.id );
                physical = number;
            }
        if( physical.empty() || !used.insert( physical ).second )
            throw std::runtime_error( "Cannot associate " + model.name + ": check electrical pin names for " + port.id );
        port.defaultPin = physical;
    }
    if( used.size() != component.pins.size() )
        throw std::runtime_error( "Cannot associate " + model.name + ": physical pin count differs" );
    return model;
}

// Read literal key=value fields only. Expressions remain the responsibility of
// a behavioral model; they must never silently turn into different waveforms.
static std::map<std::string, std::string> LegacyParameters( const std::string& text )
{
    if( text.size() > 16384 ) throw std::runtime_error( "Oversized Sim.Params" );
    std::map<std::string, std::string> result;
    std::size_t at = 0;
    auto spaces = [&]() { while( at < text.size() && std::isspace( static_cast<unsigned char>( text[at] ) ) ) ++at; };
    while( at < text.size() )
    {
        spaces(); if( at == text.size() ) break;
        auto start = at;
        while( at < text.size() && ( std::isalnum( static_cast<unsigned char>( text[at] ) ) || text[at] == '_' ) ) ++at;
        const auto key = FoldName( text.substr( start, at - start ) ); spaces();
        if( key.empty() || at == text.size() || text[at++] != '=' ) throw std::runtime_error( "Invalid Sim.Params assignment" );
        spaces(); if( at == text.size() ) throw std::runtime_error( "Missing Sim.Params value" );
        std::string value;
        if( text[at] == '\'' || text[at] == '"' )
        {
            const auto quote = text[at++]; start = at;
            while( at < text.size() && text[at] != quote ) ++at;
            if( at == text.size() ) throw std::runtime_error( "Unclosed Sim.Params value" );
            value = text.substr( start, at++ - start );
        }
        else
        {
            start = at;
            while( at < text.size() && !std::isspace( static_cast<unsigned char>( text[at] ) ) ) ++at;
            value = text.substr( start, at - start );
        }
        if( !result.emplace( key, value ).second || result.size() > 128 )
            throw std::runtime_error( "Repeated or excessive Sim.Params assignments" );
    }
    return result;
}

MODEL_REPOSITORY::MODEL_REPOSITORY( std::vector<fs::path> aDirectories ) :
        m_directories( std::move( aDirectories ) ) {}

std::vector<fs::path> MODEL_REPOSITORY::DefaultDirectories( const std::string& aProjectDirectory )
{
    std::vector<fs::path> paths;
    if( !aProjectDirectory.empty() ) paths.push_back( fs::u8path( aProjectDirectory ) / "models" );
    if( const char* directory = std::getenv( "CIRCUITALIVE_MODEL_DIR" ); directory && *directory )
        paths.emplace_back( directory );
    std::error_code error;
    auto executable = ExecutablePath();
    if( !error ) paths.push_back( executable.parent_path().parent_path() / "share/circuitalive/models" );
    paths.emplace_back( CIRCUITALIVE_BUNDLED_MODEL_DIR );
    std::vector<fs::path> unique;
    for( const auto& path : paths )
        if( std::find( unique.begin(), unique.end(), path ) == unique.end() ) unique.push_back( path );
    return unique;
}

SIMULATION_MODEL MODEL_REPOSITORY::Load( const fs::path& aFile )
{
    std::error_code error;
    const auto size = fs::file_size( aFile, error );
    if( error || size > 512 * 1024 ) throw std::runtime_error( aFile.string() + ": Missing or oversized model file" );
    std::ifstream stream( aFile );
    return LoadText( std::string( std::istreambuf_iterator<char>( stream ), {} ), aFile );
}

SIMULATION_MODEL MODEL_REPOSITORY::LoadText( const std::string& text, const fs::path& aFile )
{
    try
    {
        if( text.size() > 512 * 1024 ) throw std::runtime_error( "Oversized model file" );
        const auto j = JSON::parse( text );
        const bool runtime = j.contains( "runtime" );
        if( ( !runtime && ( j.at( "schema_version" ) != 1 || j.at( "backend" ) != "ngspice" ) )
            || ( runtime && ( j.at( "schema_version" ) != 2 || j.at( "backend" ) != "ngspice+runtime" ) ) )
            throw std::runtime_error( "Unsupported model version/backend; programmable models require schema 2 and ngspice+runtime" );
        SIMULATION_MODEL m;
        m.file = fs::absolute( aFile ).lexically_normal();
        m.id = j.at( "id" );
        m.name = j.at( "name" );
        m.description = j.value( "description", "" );
        if( !Identifier( m.id ) || m.name.empty() ) throw std::runtime_error( "Invalid model identity" );
        std::set<std::string> ports, physical;
        for( const auto& p : j.at( "pins" ) )
        {
            MODEL_PIN pin{ p.at( "id" ), p.at( "label" ), p.value( "role", "passive" ),
                           p.value( "default_pin", "" ) };
            if( !Identifier( pin.id ) || !ports.insert( pin.id ).second )
                throw std::runtime_error( "Invalid or repeated model port" );
            if( !pin.defaultPin.empty() && !physical.insert( pin.defaultPin ).second )
                throw std::runtime_error( "Repeated default physical pin" );
            const std::set<std::string> roles{ "passive", "input", "output", "bidirectional", "power", "reference" };
            if( !roles.contains( pin.role ) ) throw std::runtime_error( "Unknown pin role: " + pin.role );
            m.pins.push_back( std::move( pin ) );
        }
        if( m.pins.empty() || m.pins.size() > 512 ) throw std::runtime_error( "Invalid model pin count" );
        std::set<std::string> parameters;
        for( const auto& p : j.value( "parameters", JSON::array() ) )
        {
            MODEL_PARAMETER param{ p.at( "id" ), p.at( "label" ), p.value( "unit", "" ),
                p.at( "default" ), p.value( "min", -std::numeric_limits<double>::max() ),
                p.value( "max", std::numeric_limits<double>::max() ) };
            if( !Identifier( param.id ) || !parameters.insert( param.id ).second
                || !std::isfinite( param.minimum ) || !std::isfinite( param.maximum )
                || param.minimum > param.maximum ) throw std::runtime_error( "Invalid parameter definition" );
            m.parameters.push_back( std::move( param ) );
        }
        const auto matches = j.value( "matches", JSON::object() );
        m.libraryIds = matches.value( "library_ids", std::vector<std::string>{} );
        m.symbolNames = matches.value( "symbol_names", std::vector<std::string>{} );
        m.deviceHints = matches.value( "device_hints", std::vector<std::string>{} );
        m.partNames = matches.value( "part_names", std::vector<std::string>{} );
        m.pinNames = matches.value( "pin_names", std::map<std::string, std::vector<std::string>>{} );
        if( !m.partNames.empty() )
        {
            if( m.partNames.size() > 512 || m.pinNames.size() != ports.size() )
                throw std::runtime_error( "Part aliases require names for every model port" );
            for( const auto& name : m.partNames )
                if( FoldName( name ).empty() || name.size() > 128 ) throw std::runtime_error( "Invalid part alias" );
            for( const auto& [port, names] : m.pinNames )
            {
                if( !ports.contains( port ) || names.empty() || names.size() > 16 ) throw std::runtime_error( "Invalid pin-name mapping" );
                for( const auto& name : names ) if( FoldName( name ).empty() ) throw std::runtime_error( "Empty pin-name alias" );
            }
        }
        if( j.contains( "legacy_parameters" ) )
        {
            const auto& legacy = j.at( "legacy_parameters" );
            m.legacyParameterMap = legacy.at( "map" ).get<std::map<std::string, std::string>>();
            m.legacyParameterDefaults = legacy.value( "defaults", std::map<std::string, std::string>{} );
            m.legacyIgnoredParameters = legacy.value( "ignored", std::vector<std::string>{} );
            m.legacyParametersOnlyIfValueInvalid = legacy.value( "only_if_value_invalid", false );
            for( const auto& [key, parameter] : m.legacyParameterMap )
                if( !Identifier( key ) || !parameters.contains( parameter ) ) throw std::runtime_error( "Invalid legacy parameter mapping" );
            for( const auto& [parameter, value] : m.legacyParameterDefaults )
                if( !parameters.contains( parameter ) ) throw std::runtime_error( "Unknown legacy default parameter" );
        }
        std::set<std::string> resourceIds, memoryIds;
        for( const auto& r : j.value( "resources", JSON::array() ) )
        {
            MODEL_RESOURCE resource{ r.at( "id" ),
                                     r.at( "label" ),
                                     r.value( "kind", "data" ),
                                     r.at( "formats" ).get<std::vector<std::string>>(),
                                     r.value( "required", false ),
                                     r.value( "max_bytes", MAX_MEMORY_BYTES ) };
            if( !Identifier( resource.id ) || !resourceIds.insert( resource.id ).second || resource.label.empty() ||
                resource.formats.empty() || !resource.maxBytes || resource.maxBytes > 8 * MAX_MEMORY_BYTES ||
                resourceIds.size() > 16 )
                throw std::runtime_error( "Invalid model resource" );
            for( const auto& format : resource.formats )
                if( !Identifier( format ) )
                    throw std::runtime_error( "Invalid resource format" );
            m.resources.push_back( std::move( resource ) );
        }
        for( const auto& r : j.value( "memories", JSON::array() ) )
        {
            MODEL_MEMORY memory{ r.at( "id" ),
                                 r.at( "label" ),
                                 r.value( "resource", "" ),
                                 r.at( "address_bits" ),
                                 r.value( "word_bits", 8u ),
                                 r.value( "writable", false ),
                                 r.value( "fill", -1 ) };
            if( !Identifier( memory.id ) || !memoryIds.insert( memory.id ).second || memory.label.empty() ||
                !memory.addressBits || memory.addressBits > 20 || !memory.wordBits || memory.wordBits > 32 ||
                memory.fill < -1 || memory.fill > 255 || memoryIds.size() > 16 ||
                ( ( std::size_t( 1 ) << memory.addressBits ) * ( ( memory.wordBits + 7 ) / 8 ) ) > MAX_MEMORY_BYTES ||
                ( !memory.resource.empty() && !resourceIds.contains( memory.resource ) ) )
                throw std::runtime_error( "Invalid model memory region" );
            m.memories.push_back( std::move( memory ) );
        }
        if( runtime )
        {
            const auto& r = j.at( "runtime" );
            MODEL_RUNTIME descriptor;
            if( r.at( "api_version" ) != 1 ) throw std::runtime_error( "Unsupported programmable runtime API" );
            descriptor.engine = r.at( "engine" );
            descriptor.device = r.at( "device" );
            descriptor.modules = r.at( "modules" ).get<std::vector<std::string>>();
            descriptor.clockParameter = r.at( "clock_parameter" );
            descriptor.firmwareResource = r.at( "firmware_resource" );
            descriptor.firmwareBytes = r.at( "firmware_bytes" );
            descriptor.eepromResource = r.value( "eeprom_resource", "" );
            descriptor.eepromBytes = r.value( "eeprom_bytes", std::size_t( 0 ) );
            descriptor.optionsJson = r.value( "options", JSON::object() ).dump();
            descriptor.vcc = r.at( "vcc" );
            descriptor.ground = r.at( "gnd" );
            descriptor.minimumSupply = r.value( "min_supply", 1.8 );
            descriptor.maximumSupply = r.value( "max_supply", 5.5 );
            if( descriptor.engine != "javascript" || !Identifier( descriptor.device )
                || descriptor.modules.empty() || descriptor.modules.size() > 8
                || !parameters.contains( descriptor.clockParameter )
                || !resourceIds.contains( descriptor.firmwareResource )
                || !descriptor.firmwareBytes || descriptor.firmwareBytes > MAX_MEMORY_BYTES
                || descriptor.eepromBytes > MAX_MEMORY_BYTES
                || ( descriptor.eepromBytes && !resourceIds.contains( descriptor.eepromResource ) )
                || !ports.contains( descriptor.vcc ) || !ports.contains( descriptor.ground )
                || descriptor.vcc == descriptor.ground || !r.value( "options", JSON::object() ).is_object()
                || descriptor.optionsJson.size() > 64 * 1024
                || !std::isfinite( descriptor.minimumSupply ) || !std::isfinite( descriptor.maximumSupply )
                || descriptor.minimumSupply <= 0 || descriptor.maximumSupply < descriptor.minimumSupply )
                throw std::runtime_error( "Invalid programmable runtime descriptor" );
            const auto firmware = std::find_if( m.resources.begin(), m.resources.end(),
                [&]( const auto& resource ) { return resource.id == descriptor.firmwareResource; } );
            if( !firmware->required ) throw std::runtime_error( "Programmable firmware resource must be required" );
            for( const auto& module : descriptor.modules )
                if( module.empty() || module.size() > 4096 || module.find_first_of( "\r\n" ) != std::string::npos )
                    throw std::runtime_error( "Invalid programmable runtime module path" );
            std::set<std::string> signalIds, mappedPorts;
            for( const auto& p : r.at( "pins" ) )
            {
                MODEL_RUNTIME_PIN pin{ p.at( "id" ), p.at( "port" ), p.value( "drive", false ) };
                if( !Identifier( pin.id ) || !ports.contains( pin.port )
                    || !signalIds.insert( pin.id ).second || !mappedPorts.insert( pin.port ).second )
                    throw std::runtime_error( "Invalid programmable runtime pin mapping" );
                descriptor.pins.push_back( std::move( pin ) );
            }
            if( descriptor.pins.empty() || descriptor.pins.size() > 256 )
                throw std::runtime_error( "Invalid programmable runtime pin count" );
            m.runtime = std::move( descriptor );
        }
        const auto spice = j.value( "spice", JSON::object() );
        m.instance = spice.value( "instance", std::vector<std::string>{} );
        m.definitions = spice.value( "definitions", std::vector<std::string>{} );
        if( j.contains( "behavioral" ) )
        {
            if( j.contains( "logic" ) ) throw std::runtime_error( "Behavioral models generate their own logic interfaces" );
            CompileBehavioralModel( j.at( "behavioral" ), m );
        }
        if( m.instance.empty() ) throw std::runtime_error( "Model has no SPICE instance or behavioral blocks" );
        m.referencePin = j.value( "reference_pin", "" );
        if( !m.referencePin.empty() && !ports.contains( m.referencePin ) )
            throw std::runtime_error( "Unknown reference port" );
        m.electricalGroups = j.value( "electrical_groups", std::vector<std::vector<std::string>>{} );
        if( m.electricalGroups.empty() )
        {
            std::vector<std::string> all;
            for( const auto& pin : m.pins ) all.push_back( pin.id );
            m.electricalGroups.push_back( std::move( all ) );
        }
        std::set<std::string> grouped;
        for( const auto& group : m.electricalGroups )
        {
            if( group.empty() ) throw std::runtime_error( "Empty electrical group" );
            for( const auto& pin : group )
                if( !ports.contains( pin ) || !grouped.insert( pin ).second )
                    throw std::runtime_error( "Invalid or repeated electrical-group port" );
        }
        if( grouped != ports ) throw std::runtime_error( "Electrical groups must cover all model ports" );
        if( j.contains( "indicator" ) )
        {
            const auto& i = j.at( "indicator" );
            if( i.at( "type" ) != "led" ) throw std::runtime_error( "Unknown indicator type" );
            m.indicatorSignal = i.at( "signal" );
            m.brightnessParameter = i.at( "brightness_parameter" );
            m.ratingParameter = i.value( "rating_parameter", "" );
            if( !parameters.contains( m.brightnessParameter )
                || ( !m.ratingParameter.empty() && !parameters.contains( m.ratingParameter ) ) )
                throw std::runtime_error( "Unknown indicator parameter" );
            if(i.contains("appearance"))
            {
                for(std::size_t n=0;n<LED_APPEARANCE_KEYS.size();++n)
                {
                    const auto id=i.at("appearance").at(LED_APPEARANCE_KEYS[n]).get<std::string>();
                    const auto spec=std::find_if(m.parameters.begin(),m.parameters.end(),[&](const auto& parameter){return parameter.id==id;});
                    if(spec==m.parameters.end() || spec->minimum<0 || spec->maximum>LED_APPEARANCE_LIMITS[n])
                        throw std::runtime_error("Invalid LED appearance parameter: "+id);
                    m.ledAppearance.ids[n]=id;
                }
            }
        }
        if( j.contains( "control" ) )
        {
            const auto& c = j.at( "control" );
            m.controlSource = c.at( "source" );
            m.controlOn = c.value( "on", 5.0 ); m.controlOff = c.value( "off", 0.0 );
            m.controlInitiallyClosed = c.value( "initially_closed", true );
            m.controlVisual = ReadControlVisual( c.value( "visual", JSON::object() ) );
            m.controlInitiallyOpenSymbols = c.value( "initially_open_symbols", std::vector<std::string>{} );
            if( !std::isfinite( m.controlOn ) || !std::isfinite( m.controlOff ) )
                throw std::runtime_error( "Invalid control levels" );
        }
        m.category = j.value( "category", "" );
        m.family = j.value( "family", "" );
        std::set<std::string> adjustmentIds, adjustmentSources;
        for( const auto& control : j.value( "adjustments", JSON::array() ) )
        {
            MODEL_ADJUSTMENT a{ control.at( "id" ), control.at( "label" ),
                control.at( "source" ), control.at( "parameter" ), control.value( "unit", "" ),
                control.at( "min" ), control.at( "max" ), control.value( "step", .01 ) };
            const auto parameter = std::find_if( m.parameters.begin(), m.parameters.end(),
                [&]( const auto& p ) { return p.id == a.parameter; } );
            if( !Identifier( a.id ) || !adjustmentIds.insert( a.id ).second
                || !adjustmentSources.insert( a.source ).second || a.source.empty()
                || a.source.find_first_of( " \t\r\n" ) != std::string::npos
                || ( a.source.front() != 'V' && a.source.front() != 'v'
                     && a.source.front() != 'I' && a.source.front() != 'i' )
                || parameter == m.parameters.end() || !std::isfinite( a.minimum )
                || !std::isfinite( a.maximum ) || !std::isfinite( a.step )
                || a.minimum >= a.maximum || a.step <= 0 || a.step > a.maximum - a.minimum
                || a.minimum < parameter->minimum || a.maximum > parameter->maximum )
                throw std::runtime_error( "Invalid live adjustment" );
            m.adjustments.push_back( std::move( a ) );
            m.adjustments.back().visual = ReadControlVisual( control.value( "visual", JSON::object() ) );
        }
        if( j.value( "controls", JSON::array() ).size() > 64 || m.adjustments.size() > 64 )
            throw std::runtime_error( "Too many interactive controls" );
        for( const auto& c : j.value( "controls", JSON::array() ) )
        {
            MODEL_CONTROL control{ c.at( "id" ), c.at( "label" ), c.at( "source" ),
                c.value( "on", 5.0 ), c.value( "off", 0.0 ), c.value( "initially_closed", false ),
                ReadControlVisual( c.value( "visual", JSON::object() ) ) };
            if( !Identifier( control.id ) || !adjustmentIds.insert( control.id ).second
                || !adjustmentSources.insert( control.source ).second || control.source.empty()
                || control.source.find_first_of( " \t\r\n" ) != std::string::npos
                || ( control.source.front() != 'V' && control.source.front() != 'v'
                     && control.source.front() != 'I' && control.source.front() != 'i' )
                || !std::isfinite( control.on ) || !std::isfinite( control.off ) || control.on == control.off
                || control.source == m.controlSource )
                throw std::runtime_error( "Invalid interactive button" );
            m.controls.push_back( std::move( control ) );
        }
        if( j.value("live_updates",JSON::array()).size()>1024 ) throw std::runtime_error("Too many live parameter updates");
        for( const auto& update : j.value("live_updates",JSON::array()) )
        {
            MODEL_LIVE_UPDATE u;
            u.change.reference=update.at("reference"); u.change.parameter=update.at("parameter");
            u.change.expressions=update.at("expressions").get<std::vector<std::string>>();
            u.change.model=update.value("model",false);
            u.sources=update.at("sources").get<std::vector<std::string>>();
            u.initials=update.at("initials").get<std::vector<std::string>>();
            if(u.sources.empty()||u.sources.size()>64||u.initials.size()!=u.sources.size()||u.change.expressions.empty()
                ||u.change.expressions.size()>256||!Identifier(u.change.parameter)||u.change.reference.empty()
                ||u.change.reference.find_first_of(" \t\r\n")!=std::string::npos)
                throw std::runtime_error("Invalid live parameter update");
            for(const auto& expression:u.change.expressions)
            {
                if(expression.size()>4096)throw std::runtime_error("Oversized live parameter expression");
                std::size_t at=0;
                while((at=expression.find('#',at))!=std::string::npos)
                {
                    const auto end=expression.find('#',at+1);const auto number=expression.substr(at+1,end-at-1);
                    if(end==std::string::npos||number.empty()||number.size()>2||number.find_first_not_of("0123456789")!=std::string::npos
                        ||std::stoul(number)>=u.sources.size())throw std::runtime_error("Invalid live control input");
                    at=end+1;
                }
            }
            m.liveUpdates.push_back(std::move(u));
        }
        if(j.value("live_texts",JSON::array()).size()>128)throw std::runtime_error("Too many live text fields");
        for(const auto& text:j.value("live_texts",JSON::array()))
        {
            MODEL_LIVE_TEXT t{text.at("target"),text.at("field"),text.at("source"),text.at("initial"),
                text.at("on_text"),text.at("off_text"),text.at("on"),text.at("off"),text.at("slider")};
            if((t.field!="label"&&t.field!="unit")||!Identifier(t.target)||t.onText.size()>256||t.offText.size()>256
                ||!std::isfinite(t.on)||!std::isfinite(t.off)||t.on==t.off)throw std::runtime_error("Invalid live text field");
            m.liveTexts.push_back(std::move(t));
        }
        m.ignoreLegacyFields = j.value( "ignore_legacy_spice_fields", false );
        m.legacyFields = j.value( "legacy_spice_fields", std::map<std::string, std::string>{} );
        if( j.contains( "measurement_ports" ) )
        {
            m.measurePositive = j.at( "measurement_ports" ).at( "positive" );
            m.measureNegative = j.at( "measurement_ports" ).at( "negative" );
            if( !ports.contains( m.measurePositive ) || !ports.contains( m.measureNegative )
                || m.measurePositive == m.measureNegative ) throw std::runtime_error( "Invalid measurement ports" );
        }
        if( j.contains( "current_warning" ) )
        {
            m.warningSignal = j.at( "current_warning" ).at( "signal" );
            m.warningParameter = j.at( "current_warning" ).at( "limit_parameter" );
            if( !parameters.contains( m.warningParameter ) ) throw std::runtime_error( "Unknown warning parameter" );
        }
        m.voltageConstraint = j.value( "voltage_constraint_parameter", "" );
        m.frequencyParameter = j.value( "frequency_parameter", "" );
        m.maxStepParameter = j.value( "max_step_parameter", "" );
        if( j.contains( "timing_rc" ) )
        {
            const auto& timing = j.at( "timing_rc" );
            m.timingRcPorts = timing.at( "ports" ).get<std::vector<std::string>>();
            m.timingRcFallback = timing.value( "fallback", 10e-6 );
            if( m.timingRcPorts.empty() || m.timingRcPorts.size() > m.pins.size()
                || !std::isfinite( m.timingRcFallback ) || m.timingRcFallback < 1e-12
                || m.timingRcFallback > 1 )
                throw std::runtime_error( "Invalid RC timing limits" );
            for( const auto& port : m.timingRcPorts )
                if( std::none_of( m.pins.begin(), m.pins.end(), [&]( const auto& pin ) { return pin.id == port; } ) )
                    throw std::runtime_error( "Unknown RC timing port" );
        }
        if( !m.maxStepParameter.empty() && !parameters.contains( m.maxStepParameter ) )
            throw std::runtime_error( "Unknown maximum step parameter" );
        if( !m.frequencyParameter.empty() && !parameters.contains( m.frequencyParameter ) )
            throw std::runtime_error( "Unknown frequency parameter" );
        if( !m.voltageConstraint.empty()
            && ( !parameters.contains( m.voltageConstraint ) || m.measurePositive.empty() || m.measureNegative.empty() ) )
            throw std::runtime_error( "Invalid voltage constraint" );
        if( j.contains( "logic" ) )
        {
            const auto& logic = j.at( "logic" );
            m.logicVcc = logic.at( "vcc" ); m.logicGround = logic.at( "gnd" );
            m.logicLow = logic.at( "low_parameter" ); m.logicHigh = logic.at( "high_parameter" );
            m.logicMinSupply = logic.at( "min_supply_parameter" );
            m.logicMaxSupply = logic.at( "max_supply_parameter" );
            if( !ports.contains( m.logicVcc ) || !ports.contains( m.logicGround ) || m.logicVcc == m.logicGround )
                throw std::runtime_error( "Invalid logic supply ports" );
            for( const auto& p : { m.logicLow, m.logicHigh, m.logicMinSupply, m.logicMaxSupply } )
                if( !parameters.contains( p ) ) throw std::runtime_error( "Unknown logic parameter: " + p );
            m.driverEnable = logic.at( "driver_enable" ).get<std::map<std::string, std::string>>();
            m.driverValue = logic.value( "driver_value", std::map<std::string, std::string>{} );
            m.internalDrive = logic.value( "internal_drive", std::map<std::string, std::string>{} );
            for( const auto& [port, expression] : m.internalDrive )
                if( !ports.contains( port ) || expression.empty() ) throw std::runtime_error( "Invalid internal drive port" );
            for( const auto& [port, expression] : m.driverEnable )
            {
                auto pin = std::find_if( m.pins.begin(), m.pins.end(), [&]( const auto& p ) { return p.id == port; } );
                if( pin == m.pins.end() || ( pin->role != "output" && pin->role != "bidirectional" ) || expression.empty() )
                    throw std::runtime_error( "Invalid logic driver: " + port );
            }
            for( const auto& [port, expression] : m.driverValue )
                if( !m.driverEnable.contains( port ) || expression.empty() )
                    throw std::runtime_error( "Invalid logic driver value: " + port );
        }
        return m;
    }
    catch( const std::exception& e )
    {
        throw std::runtime_error( aFile.string() + ": " + e.what() );
    }
}

const std::vector<SIMULATION_MODEL>& MODEL_REPOSITORY::List() const
{
    if( m_models ) return *m_models;
    std::vector<SIMULATION_MODEL> models;
    std::set<std::string> ids;
    for( const auto& directory : m_directories )
    {
        std::error_code error;
        if( !fs::is_directory( directory, error ) ) continue;
        std::vector<fs::path> files;
        for( const auto& entry : fs::directory_iterator( directory ) )
            if( entry.is_regular_file() && entry.path().extension() == ".casim" ) files.push_back( entry.path() );
        std::sort( files.begin(), files.end() );
        for( const auto& file : files )
        {
            auto model = Load( file );
            if( ids.insert( model.id ).second ) models.push_back( std::move( model ) );
        }
    }
    m_models = std::move( models );
    return *m_models;
}

SIMULATION_MODEL MODEL_REPOSITORY::SelectModel( const CIRCUIT_COMPONENT& c ) const
{
    SIMULATION_MODEL selected;
    if( !c.modelFile.empty() && !c.modelFile.starts_with( "builtin:" ) )
    {
        auto path = fs::u8path( c.modelFile );
        if( path.is_relative() ) path = fs::u8path( c.projectDirectory ) / path;
        selected = Load( path );
    }
    else
    {
        const auto& models = List();
        int best = 0;
        bool ambiguous = false;
        const auto shortName = c.libraryId.substr( c.libraryId.find_last_of( ':' ) + 1 );
        for( const auto& model : models )
        {
            auto has = []( const auto& list, const auto& value )
                { return std::find( list.begin(), list.end(), value ) != list.end(); };
            int score = 0;
            if( !c.modelFile.empty() ) score = c.modelFile == "builtin:" + model.id ? 100 : 0;
            else if( has( model.libraryIds, c.libraryId ) ) score = 3;
            else if( has( model.symbolNames, shortName ) && c.pins.size() == model.pins.size()
                     && std::all_of( model.pins.begin(), model.pins.end(),
                             [&]( const auto& pin ) { return c.pins.contains( pin.defaultPin ); } ) ) score = 2;
            else if( !c.deviceHint.empty() && has( model.deviceHints, c.deviceHint ) ) score = 1;
            std::optional<SIMULATION_MODEL> candidate;
            if( c.modelFile.empty() && !model.partNames.empty() && !c.pinNames.empty() )
            {
                auto spice = c.legacySpiceFields.find( "Sim.Name" );
                const bool byValue = HasPartName( model, c.value )
                        || ( spice != c.legacySpiceFields.end() && HasPartName( model, spice->second ) );
                if( byValue || HasPartName( model, shortName ) )
                {
                    candidate = BindNamedPins( model, c );
                    score = std::max( score, byValue ? 4 : 3 );
                }
            }
            if( score > 0 && score == best ) ambiguous = true;
            if( score > best )
            {
                best = score; selected = candidate ? std::move( *candidate ) : model; ambiguous = false;
            }
        }
        if( ambiguous ) throw std::runtime_error( "Ambiguous model association; choose a model file for " + c.reference );
        if( !best ) throw std::runtime_error( "No simulation model associated with " + c.libraryId
                + ". Select the symbol and use Simulation model..." );
    }
    return selected;
}

RESOLVED_MODEL MODEL_REPOSITORY::Resolve( const CIRCUIT_COMPONENT& c ) const
{
    RESOLVED_MODEL r;
    r.model = SelectModel( c );
    r.controlInitiallyClosed = r.model.controlInitiallyClosed;
    const auto symbol = c.libraryId.substr( c.libraryId.find( ':' ) + 1 );
    for( const auto& identity : r.model.controlInitiallyOpenSymbols )
        if( identity == c.libraryId || identity == symbol ) r.controlInitiallyClosed = false;
    for( std::size_t i = 0; i < r.model.memories.size(); ++i )
        r.memorySlots[r.model.memories[i].id] = i;
    for( const auto& [id, selection] : c.resources )
        if( !selection.path.empty() && std::none_of( r.model.resources.begin(), r.model.resources.end(),
                                                     [&]( const auto& item ) { return item.id == id; } ) )
            throw std::runtime_error( "Unknown model resource: " + id );
    for( const auto& spec : r.model.resources )
    {
        auto it = c.resources.find( spec.id );
        if( it == c.resources.end() || it->second.path.empty() )
        {
            if( spec.required )
                throw std::runtime_error( "Choose a file for " + spec.label );
            continue;
        }
        const auto& selection = it->second;
        if( selection.format != "auto" &&
            std::find( spec.formats.begin(), spec.formats.end(), selection.format ) == spec.formats.end() )
            throw std::runtime_error( "Unsupported file format for " + spec.label );
        auto path = fs::u8path( selection.path );
        if( path.is_relative() )
            path = fs::u8path( c.projectDirectory ) / path;
        std::error_code error;
        const auto size = fs::file_size( path, error );
        if( error || size > spec.maxBytes )
            throw std::runtime_error( "Missing or oversized resource: " + path.string() );
    }
    const bool explicitFile = !c.modelFile.empty();
    if( !explicitFile && c.explicitSpiceModel && !r.model.ignoreLegacyFields )
    {
        if( c.legacySpiceFields.empty() ) throw std::runtime_error( "Custom SPICE fields require an explicit model file" );
        for( const auto& [key, value] : c.legacySpiceFields )
        {
            if( key == "Sim.Params" && !r.model.legacyParameterMap.empty() ) continue;
            auto allowed = r.model.legacyFields.find( key );
            if( allowed == r.model.legacyFields.end() || allowed->second != value )
                throw std::runtime_error( "Custom SPICE fields require an explicit model file: " + key );
        }
    }
    std::set<std::string> usedPins;
    for( const auto& pin : r.model.pins )
    {
        auto it = c.modelPins.find( pin.id );
        const std::string physical = it == c.modelPins.end() ? pin.defaultPin : it->second;
        if( physical.empty() || !c.pins.contains( physical ) || !usedPins.insert( physical ).second )
            throw std::runtime_error( "Map model port " + pin.id + " to a distinct physical symbol pin" );
        r.pins[pin.id] = physical;
    }
    if( usedPins.size() != c.pins.size() )
        throw std::runtime_error( "Every physical symbol pin needs a model port (open wires are allowed)" );
    for( const auto& [port, physical] : c.modelPins )
        if( !r.pins.contains( port ) ) throw std::runtime_error( "Unknown mapped model port: " + port );
    std::map<std::string, std::string> inheritedParameters;
    if( !explicitFile && !r.model.legacyParameterMap.empty() )
    {
        bool use = true;
        if( r.model.legacyParametersOnlyIfValueInvalid )
        {
            try { ParseComponentValue( c.value ); use = false; } catch( const std::exception& ) {}
        }
        if( use )
        {
            inheritedParameters = r.model.legacyParameterDefaults;
            auto field = c.legacySpiceFields.find( "Sim.Params" );
            if( field != c.legacySpiceFields.end() )
                for( const auto& [key, value] : LegacyParameters( field->second ) )
                {
                    auto mapped = std::find_if( r.model.legacyParameterMap.begin(), r.model.legacyParameterMap.end(),
                            [&]( const auto& entry ) { return FoldName( entry.first ) == key; } );
                    if( mapped != r.model.legacyParameterMap.end() ) inheritedParameters[mapped->second] = value;
                    else if( !std::any_of( r.model.legacyIgnoredParameters.begin(), r.model.legacyIgnoredParameters.end(),
                                [&]( const auto& ignored ) { return FoldName( ignored ) == key; } ) )
                        throw std::runtime_error( "Unsupported Sim.Params parameter: " + key );
                }
        }
    }
    for( const auto& p : r.model.parameters )
    {
        auto it = c.modelParameters.find( p.id );
        auto text = it == c.modelParameters.end() ? p.defaultValue : it->second;
        if( text.empty() ) text = p.defaultValue;
        if( ( it == c.modelParameters.end() || it->second.empty() ) && inheritedParameters.contains( p.id ) )
            text = inheritedParameters.at( p.id );
        r.parameterText[p.id] = text;
        const double value = ParseComponentValue( text == "$value" ? c.value : text );
        if( value < p.minimum || value > p.maximum )
        {
            std::ostringstream range;
            range.imbue( std::locale::classic() );
            range << p.label << " must be between " << p.minimum << " and " << p.maximum << ' ' << p.unit;
            throw std::runtime_error( range.str() );
        }
        r.parameters[p.id] = value;
    }
    for( const auto& [id, value] : c.modelParameters )
        if( !r.parameters.contains( id ) ) throw std::runtime_error( "Unknown model parameter: " + id );
    if( !r.model.brightnessParameter.empty() && r.parameters.at( r.model.brightnessParameter ) <= 0 )
        throw std::runtime_error( "Brightness current must be positive" );
    if( !r.model.logicVcc.empty() && ( r.parameters.at( r.model.logicLow ) < 0
        || r.parameters.at( r.model.logicHigh ) > 1
        || r.parameters.at( r.model.logicLow ) >= r.parameters.at( r.model.logicHigh )
        || r.parameters.at( r.model.logicMinSupply ) <= 0
        || r.parameters.at( r.model.logicMaxSupply ) < r.parameters.at( r.model.logicMinSupply ) ) )
        throw std::runtime_error( "Invalid logic thresholds or supply range" );
    return r;
}

std::string ExpandModelTemplate( const std::string& text, const RESOLVED_MODEL& model,
        const std::map<std::string, std::string>& nodes, std::size_t index )
{
    std::string result;
    std::size_t cursor = 0;
    while( cursor < text.size() )
    {
        const auto start = text.find( "${", cursor );
        if( start == std::string::npos ) { result += text.substr( cursor ); break; }
        result += text.substr( cursor, start - cursor );
        const auto end = text.find( '}', start + 2 );
        if( end == std::string::npos ) throw std::runtime_error( "Unclosed model placeholder" );
        const auto token = text.substr( start + 2, end - start - 2 );
        if( token == "id" ) result += "ca" + std::to_string( index + 1 );
        else if( token.starts_with( "pin:" ) ) result += nodes.at( model.pins.at( token.substr( 4 ) ) );
        else if( token.starts_with( "param:" ) )
        {
            std::ostringstream number;
            number.imbue( std::locale::classic() );
            number << std::setprecision( 17 ) << model.parameters.at( token.substr( 6 ) );
            result += number.str();
        }
        else if( token == "control:initial" )
        {
            std::ostringstream number;
            number.imbue( std::locale::classic() );
            number << std::setprecision( 17 )
                   << ( model.controlInitiallyClosed ? model.model.controlOn : model.model.controlOff );
            result += number.str();
        }
        else if( token.starts_with( "memory:" ) ) result += std::to_string(model.memorySlots.at(token.substr(7)));
        else if( token.starts_with( "drive:" ) )
        {
            const auto port = token.substr( 6 );
            if( !model.pins.contains( port ) ) throw std::runtime_error( "Unknown driven port: " + port );
            const auto it = model.driveExpressions.find( port );
            result += it == model.driveExpressions.end() ? "1" : it->second;
        }
        else throw std::runtime_error( "Unknown model placeholder: " + token );
        cursor = end + 1;
    }
    return result;
}

std::vector<MEMORY_REGION_CONFIG> InitialModelMemories( const CIRCUIT_COMPONENT& c, const SIMULATION_MODEL& m )
{
    std::vector<MEMORY_REGION_CONFIG> regions;
    for( const auto& spec : m.memories )
    {
        MEMORY_REGION_CONFIG region;
        region.id = spec.id;
        region.label = spec.label;
        region.wordBits = spec.wordBits;
        region.writable = spec.writable;
        region.initial =
                EmptyMemory( ( std::size_t( 1 ) << spec.addressBits ) * ( ( spec.wordBits + 7 ) / 8 ), spec.fill );
        auto it = c.resources.find( spec.resource );
        if( it != c.resources.end() && !it->second.path.empty() )
        {
            auto path = fs::u8path( it->second.path );
            if( path.is_relative() )
                path = fs::u8path( c.projectDirectory ) / path;
            LoadMemoryImage( region.initial, path, it->second.format, it->second.address );
        }
        regions.push_back( std::move( region ) );
    }
    return regions;
}

PROGRAMMABLE_DEVICE_CONFIG InitialModelRuntime( const CIRCUIT_COMPONENT& component, const RESOLVED_MODEL& model )
{
    if( !model.model.runtime ) throw std::runtime_error( "Model has no programmable runtime" );
    const auto& descriptor = *model.model.runtime;
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id = component.reference;
    config.engine = descriptor.engine;
    config.device = descriptor.device;
    config.clockHz = model.parameters.at( descriptor.clockParameter );
    if( config.clockHz <= 0 ) throw std::runtime_error( "Runtime clock must be positive" );
    auto options = JSON::parse( descriptor.optionsJson );
    options["supplyMin"] = descriptor.minimumSupply;
    options["supplyMax"] = descriptor.maximumSupply;
    options["hasEepromImage"] = component.resources.contains( descriptor.eepromResource )
        && !component.resources.at( descriptor.eepromResource ).path.empty();
    if( model.parameters.contains( "sample_period" ) )
        config.quantumSeconds = model.parameters.at( "sample_period" );
    if( config.quantumSeconds < 1e-9 || config.quantumSeconds > 1e-3 )
        throw std::runtime_error( "Runtime input sampling period must be between 1 ns and 1 ms" );
    config.optionsJson = options.dump();
    for( const auto& pin : descriptor.pins ) config.pins.push_back( pin.id );
    for( const auto& module : descriptor.modules )
    {
        auto path = fs::u8path( module );
        if( path.is_relative() ) path = model.model.file.parent_path() / path;
        std::error_code error;
        const auto size = fs::file_size( path, error );
        if( error || size > 4 * 1024 * 1024 || config.source.size() + size > 4 * 1024 * 1024 )
            throw std::runtime_error( "Missing or oversized programmable module: " + path.string() );
        std::ifstream stream( path, std::ios::binary );
        if( !stream ) throw std::runtime_error( "Cannot read programmable module: " + path.string() );
        config.source.append( std::istreambuf_iterator<char>( stream ), {} );
        config.source += "\n;\n";
    }
    auto image = [&]( const std::string& resource, std::size_t bytes, bool required )
    {
        auto contents = EmptyMemory( bytes, 255 );
        const auto selection = component.resources.find( resource );
        if( selection == component.resources.end() || selection->second.path.empty() )
        {
            if( required ) throw std::runtime_error( "Choose a firmware file before simulating " + component.reference );
            return contents.data;
        }
        auto path = fs::u8path( selection->second.path );
        if( path.is_relative() ) path = fs::u8path( component.projectDirectory ) / path;
        auto data = EmptyMemory( bytes, -1 );
        LoadMemoryImage( data, path, selection->second.format, selection->second.address );
        bool loaded = false;
        for( std::size_t i = 0; i < data.data.size(); ++i )
        {
            if( data.known[i] != 0 && data.known[i] != 255 )
                throw std::runtime_error( "Firmware and EEPROM images require fully known bytes" );
            if( data.known[i] ) { contents.data[i] = data.data[i]; loaded = true; }
        }
        if( required && !loaded ) throw std::runtime_error( "Firmware image contains no data" );
        return contents.data;
    };
    config.firmware = image( descriptor.firmwareResource, descriptor.firmwareBytes, true );
    if( descriptor.eepromBytes ) config.eeprom = image( descriptor.eepromResource, descriptor.eepromBytes, false );
    return config;
}
} // namespace CIRCUITALIVE
