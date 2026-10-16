/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_model.h"
#include "circuit_snapshot.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

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

MODEL_REPOSITORY::MODEL_REPOSITORY( std::vector<fs::path> aDirectories ) :
        m_directories( std::move( aDirectories ) ) {}

std::vector<fs::path> MODEL_REPOSITORY::DefaultDirectories( const std::string& aProjectDirectory )
{
    std::vector<fs::path> paths;
    if( !aProjectDirectory.empty() ) paths.push_back( fs::path( aProjectDirectory ) / "models" );
    if( const char* directory = std::getenv( "CIRCUITALIVE_MODEL_DIR" ); directory && *directory )
        paths.emplace_back( directory );
    std::error_code error;
    auto executable = fs::read_symlink( "/proc/self/exe", error );
    if( !error ) paths.push_back( executable.parent_path().parent_path() / "share/circuitalive/models" );
    paths.emplace_back( CIRCUITALIVE_BUNDLED_MODEL_DIR );
    std::vector<fs::path> unique;
    for( const auto& path : paths )
        if( std::find( unique.begin(), unique.end(), path ) == unique.end() ) unique.push_back( path );
    return unique;
}

SIMULATION_MODEL MODEL_REPOSITORY::Load( const fs::path& aFile )
{
    try
    {
        std::error_code error;
        const auto size = fs::file_size( aFile, error );
        if( error || size > 512 * 1024 ) throw std::runtime_error( "Missing or oversized model file" );
        std::ifstream stream( aFile );
        const auto j = JSON::parse( stream );
        if( j.at( "schema_version" ) != 1 || j.at( "backend" ) != "ngspice" )
            throw std::runtime_error( "Unsupported model version/backend; digital runtime is not implemented yet" );
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
        const auto& spice = j.at( "spice" );
        m.instance = spice.at( "instance" ).get<std::vector<std::string>>();
        m.definitions = spice.value( "definitions", std::vector<std::string>{} );
        if( m.instance.empty() ) throw std::runtime_error( "Model has no SPICE instance" );
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
        }
        if( j.contains( "control" ) )
        {
            const auto& c = j.at( "control" );
            m.controlSource = c.at( "source" );
            m.controlOn = c.value( "on", 5.0 ); m.controlOff = c.value( "off", 0.0 );
            if( !std::isfinite( m.controlOn ) || !std::isfinite( m.controlOff ) )
                throw std::runtime_error( "Invalid control levels" );
        }
        m.ignoreLegacyFields = j.value( "ignore_legacy_spice_fields", false );
        m.legacyFields = j.value( "legacy_spice_fields", std::map<std::string, std::string>{} );
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
        fs::path path( c.modelFile );
        if( path.is_relative() ) path = fs::path( c.projectDirectory ) / path;
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
            else if( has( model.symbolNames, shortName ) ) score = 2;
            else if( !c.deviceHint.empty() && has( model.deviceHints, c.deviceHint ) ) score = 1;
            if( score > 0 && score == best ) ambiguous = true;
            if( score > best ) { best = score; selected = model; ambiguous = false; }
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
    const bool explicitFile = !c.modelFile.empty();
    if( !explicitFile && c.explicitSpiceModel && !r.model.ignoreLegacyFields )
    {
        if( c.legacySpiceFields.empty() ) throw std::runtime_error( "Custom SPICE fields require an explicit model file" );
        for( const auto& [key, value] : c.legacySpiceFields )
        {
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
    for( const auto& p : r.model.parameters )
    {
        auto it = c.modelParameters.find( p.id );
        auto text = it == c.modelParameters.end() ? p.defaultValue : it->second;
        if( text.empty() ) text = p.defaultValue;
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
        else throw std::runtime_error( "Unknown model placeholder: " + token );
        cursor = end + 1;
    }
    return result;
}
}
