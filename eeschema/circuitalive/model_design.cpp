/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_design.h"
#include "circuit_snapshot.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
namespace
{
bool Identifier( const std::string& s )
{
    return !s.empty() && s.size() <= 60 && std::all_of( s.begin(), s.end(), []( unsigned char c )
        { return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_'; } );
}
std::string Number( double value )
{
    std::ostringstream s; s.imbue( std::locale::classic() ); s << std::setprecision( 17 ) << value; return s.str();
}
std::string Expand( const std::string& text, const std::map<std::string, std::string>& tokens )
{
    std::string out; std::size_t at = 0;
    while( at < text.size() )
    {
        auto start = text.find( "${", at );
        if( start == std::string::npos ) { out += text.substr( at ); break; }
        out += text.substr( at, start - at ); auto end = text.find( '}', start );
        if( end == std::string::npos ) throw std::runtime_error( "Unclosed block placeholder" );
        auto key = text.substr( start + 2, end - start - 2 );
        auto found = tokens.find( key );
        if( found == tokens.end() ) throw std::runtime_error( "Unknown block placeholder: " + key );
        out += found->second; at = end + 1;
    }
    return out;
}
}
MODEL_DESIGN MODEL_DESIGN::Open( const std::string& text )
{
    auto model = MODEL_REPOSITORY::LoadText( text );
    const auto json = JSON::parse( text ); MODEL_DESIGN d;
    d.id = model.id; d.name = model.name; d.description = model.description;
    d.pins = model.pins; d.parameters = model.parameters;
    if( !model.libraryIds.empty() ) d.libraryId = model.libraryIds[0];
    d.baseJson = text;
    if( json.contains( "authoring" ) )
    {
        const auto& a = json.at( "authoring" );
        if( a.at( "version" ) != 1 ) throw std::runtime_error( "Unsupported model design version" );
        d.baseJson = a.value( "base", "" );
        d.libraryId = a.value( "symbol", d.libraryId );
        for( const auto& b : a.at( "blocks" ) )
            d.blocks.push_back( { b.at( "id" ), b.at( "model" ).dump(),
                b.at( "connections" ).get<std::map<std::string, std::string>>(),
                b.at( "values" ).get<std::map<std::string, std::string>>(), b.value( "x", 280 ), b.value( "y", 80 ) } );
    }
    return d;
}
std::vector<std::filesystem::path> MODEL_DESIGN::BlockDirectories()
{
    std::vector<std::filesystem::path> dirs;
    if( const char* p = std::getenv( "CIRCUITALIVE_BLOCK_DIR" ); p && *p ) dirs.emplace_back( p );
    dirs.emplace_back( CIRCUITALIVE_BUNDLED_BLOCK_DIR );
    std::error_code error; auto executable = std::filesystem::read_symlink( "/proc/self/exe", error );
    if( !error ) dirs.push_back( executable.parent_path().parent_path() / "share/circuitalive/blocks" );
    return dirs;
}
void MODEL_DESIGN::Connect( const std::string& block, const std::string& port,
                            const std::string& otherBlock, const std::string& otherPort )
{
    auto endpoint = [&]( const std::string& blockId, const std::string& p ) -> std::string
    {
        if( blockId.empty() )
        {
            if( std::none_of( pins.begin(), pins.end(), [&]( const auto& pin ) { return pin.id == p; } ) )
                throw std::runtime_error( "Unknown external terminal" );
            return "@" + p;
        }
        for( const auto& b : blocks ) if( b.id == blockId )
        {
            auto model = MODEL_REPOSITORY::LoadText( b.modelJson );
            if( std::none_of( model.pins.begin(), model.pins.end(), [&]( const auto& pin ) { return pin.id == p; } ) )
                throw std::runtime_error( "Unknown block terminal" );
            auto it = b.connections.find( p ); return it == b.connections.end() ? "" : it->second;
        }
        throw std::runtime_error( "Unknown block" );
    };
    auto a = endpoint( block, port ), b = endpoint( otherBlock, otherPort );
    if( a.starts_with( "@" ) && b.starts_with( "@" ) && a != b )
        throw std::runtime_error( "External terminals are distinct. Add a resistor/link block between them." );
    auto net = a.starts_with( "@" ) ? a : !b.empty() ? b : a;
    if( net.empty() )
    {
        std::set<std::string> used;
        for( const auto& item : blocks ) for( const auto& [p, n] : item.connections ) used.insert( n );
        std::size_t number = 1;
        while( used.contains( "wire" + std::to_string( number ) ) ) ++number;
        net = "wire" + std::to_string( number );
    }
    for( auto& item : blocks ) for( auto& [p, n] : item.connections )
        if( ( !a.empty() && n == a ) || ( !b.empty() && n == b ) ) n = net;
    for( auto& item : blocks )
    {
        if( item.id == block ) item.connections[port] = net;
        if( item.id == otherBlock ) item.connections[otherPort] = net;
    }
}
std::string MODEL_DESIGN::Export() const
{
    if( !Identifier( id ) || name.empty() || pins.empty() ) throw std::runtime_error( "Set a model ID, name and component pins" );
    if( blocks.size() > 128 ) throw std::runtime_error( "Model design supports up to 128 blocks" );
    JSON j = baseJson.empty() ? JSON::object() : JSON::parse( baseJson );
    j.erase( "authoring" );
    j["schema_version"] = 1; j["backend"] = "ngspice"; j["id"] = id; j["name"] = name;
    j["description"] = description; j["license"] = j.value( "license", "GPL-3.0-or-later" );
    j["pins"] = JSON::array(); j["parameters"] = JSON::array();
    std::set<std::string> ports, physical, parameterIds;
    for( const auto& p : pins )
    {
        if( !Identifier( p.id ) || p.defaultPin.empty() || !ports.insert( p.id ).second || !physical.insert( p.defaultPin ).second )
            throw std::runtime_error( "Each component pin needs a distinct port ID and physical number" );
        j["pins"].push_back( { { "id", p.id }, { "label", p.label }, { "role", p.role }, { "default_pin", p.defaultPin } } );
    }
    auto addParameter = [&]( const MODEL_PARAMETER& p )
    {
        if( !Identifier( p.id ) || !parameterIds.insert( p.id ).second ) throw std::runtime_error( "Invalid or repeated parameter: " + p.id );
        if( !std::isfinite( p.minimum ) || !std::isfinite( p.maximum ) || p.minimum > p.maximum )
            throw std::runtime_error( "Invalid parameter range: " + p.id );
        if( p.defaultValue != "$value" )
        {
            const auto value = ParseComponentValue( p.defaultValue );
            if( value < p.minimum || value > p.maximum ) throw std::runtime_error( "Parameter default outside limits: " + p.id );
        }
        j["parameters"].push_back( { { "id", p.id }, { "label", p.label }, { "default", p.defaultValue },
            { "unit", p.unit }, { "min", p.minimum }, { "max", p.maximum } } );
    };
    for( const auto& p : parameters ) addParameter( p );
    if( parameterIds.contains( "max_step" ) ) j["max_step_parameter"] = "max_step";
    // Author alternatives bind through the saved symbol profile. They must not make
    // existing automatic associations ambiguous with bundled models for that symbol.
    j.erase( "matches" );
    if( baseJson.empty() ) j["ignore_legacy_spice_fields"] = true;
    if( !j.contains( "spice" ) ) j["spice"] = { { "instance", JSON::array() }, { "definitions", JSON::array() } };
    if( !j["spice"].contains( "definitions" ) ) j["spice"]["definitions"] = JSON::array();
    std::vector<SIMULATION_MODEL> models;
    std::vector<std::map<std::string, std::string>> tokens;
    std::vector<std::map<std::string, std::string>> nets;
    std::set<std::string> blockIds;
    for( const auto& b : blocks )
    {
        if( !Identifier( b.id ) || !blockIds.insert( b.id ).second ) throw std::runtime_error( "Invalid/repeated block ID: " + b.id );
        auto model = MODEL_REPOSITORY::LoadText( b.modelJson );
        std::map<std::string, std::string> t{ { "id", "b_" + b.id + "_${id}" } }, connections;
        for( const auto& pin : model.pins )
        {
            auto it = b.connections.find( pin.id ); const auto net = it == b.connections.end() ? "" : it->second;
            if( net.starts_with( "@" ) )
            {
                if( !ports.contains( net.substr( 1 ) ) ) throw std::runtime_error( b.id + ": unknown component port " + net );
                t["pin:" + pin.id] = "${pin:" + net.substr( 1 ) + "}";
            }
            else
            {
                if( !net.empty() && !Identifier( net ) ) throw std::runtime_error( "Invalid internal net: " + net );
                t["pin:" + pin.id] = net.empty() ? "open_" + b.id + "_" + pin.id + "_${id}" : "wire_" + net + "_${id}";
            }
            connections[pin.id] = net;
        }
        for( const auto& [p, net] : b.connections )
            if( !connections.contains( p ) ) throw std::runtime_error( b.id + ": unknown block port " + p );
        for( const auto& p : model.parameters )
        {
            auto it = b.values.find( p.id ); auto value = it == b.values.end() || it->second.empty() ? p.defaultValue : it->second;
            if( value.starts_with( "@" ) )
            {
                auto found = std::find_if( parameters.begin(), parameters.end(), [&]( const auto& q ) { return q.id == value.substr( 1 ); } );
                if( found == parameters.end() ) throw std::runtime_error( b.id + ": unknown public parameter " + value );
                if( found->minimum < p.minimum || found->maximum > p.maximum )
                    throw std::runtime_error( b.id + ": parameter " + found->id + " range must fit the block parameter range" );
                t["param:" + p.id] = "${param:" + found->id + "}";
            }
            else if( value == "$value" )
            {
                auto exposed = p; exposed.id = "b_" + b.id + "_" + p.id; exposed.label = b.id + ": " + p.label;
                if( !parameterIds.contains( exposed.id ) ) addParameter( exposed );
                t["param:" + p.id] = "${param:" + exposed.id + "}";
            }
            else
            {
                const double number = ParseComponentValue( value );
                if( number < p.minimum || number > p.maximum ) throw std::runtime_error( b.id + ": " + p.label + " is outside its limits" );
                t["param:" + p.id] = Number( number );
            }
        }
        for( const auto& [p, value] : b.values )
            if( !t.contains( "param:" + p ) ) throw std::runtime_error( b.id + ": unknown block parameter " + p );
        models.push_back( std::move( model ) ); tokens.push_back( std::move( t ) ); nets.push_back( std::move( connections ) );
    }
    // Presence of an analog element is conservative, matching the editor's connectivity resolver.
    std::map<std::string, std::vector<std::string>> drivers, internalDrivers; std::set<std::string> analog;
    for( std::size_t i = 0; i < blocks.size(); ++i )
        for( const auto& p : models[i].pins )
        {
            const auto& net = nets[i].at( p.id ); if( net.empty() ) continue;
            if( models[i].logicVcc.empty() ) analog.insert( net );
            else if( models[i].driverEnable.contains( p.id ) ) drivers[net].push_back( Expand( models[i].driverEnable.at( p.id ), tokens[i] ) );
            if( models[i].internalDrive.contains( p.id ) )
            {
                auto expression = Expand( models[i].internalDrive.at( p.id ), tokens[i] );
                drivers[net].push_back( expression ); internalDrivers[net].push_back( std::move( expression ) );
            }
        }
    for( std::size_t i = 0; i < blocks.size(); ++i )
    {
        for( const auto& p : models[i].pins )
        {
            const auto& net = nets[i].at( p.id ); std::string drive = analog.contains( net ) ? "1" : "0";
            if( net.starts_with( "@" ) && !analog.contains( net ) ) drive = "${drive:" + net.substr( 1 ) + "}";
            else if( !analog.contains( net ) ) for( const auto& d : drivers[net] ) drive += "+(" + d + ")";
            tokens[i]["drive:" + p.id] = "(" + drive + ")";
        }
        for( const auto& line : models[i].instance ) j["spice"]["instance"].push_back( Expand( line, tokens[i] ) );
        for( const auto& line : models[i].definitions ) j["spice"]["definitions"].push_back( Expand( line, tokens[i] ) );
    }
    if( !j.contains( "measurement_ports" ) )
    {
        std::string power, ground;
        for( const auto& p : pins ) { if( p.role == "power" ) power = p.id; if( p.role == "reference" ) ground = p.id; }
        if( !power.empty() && !ground.empty() ) j["measurement_ports"] = { { "positive", power }, { "negative", ground } };
    }
    if( !j.contains( "logic" ) && !j.contains( "behavioral" ) )
        for( std::size_t i = 0; i < blocks.size(); ++i )
        {
            const auto& model = models[i];
            if( model.logicVcc.empty() ) continue;
            const auto power = nets[i].at( model.logicVcc ), ground = nets[i].at( model.logicGround );
            if( !power.starts_with( "@" ) || !ground.starts_with( "@" ) ) continue;
            JSON logic = { { "vcc", power.substr( 1 ) }, { "gnd", ground.substr( 1 ) },
                           { "driver_enable", JSON::object() }, { "driver_value", JSON::object() }, { "internal_drive", JSON::object() } };
            for( const auto& p : pins ) if( analog.contains( "@" + p.id ) || internalDrivers.contains( "@" + p.id ) )
            {
                const auto net = "@" + p.id;
                std::string drive = analog.contains( net ) ? "1" : "0";
                for( const auto& expression : internalDrivers[net] ) drive += "+(" + expression + ")";
                const auto node = "builder_internal_" + p.id + "_${id}";
                j["spice"]["instance"].push_back( "B" + node + " " + node + " 0 V={" + drive + "}" );
                logic["internal_drive"][p.id] = "v(" + node + ")";
            }
            for( const auto& [key, childParameter] : std::vector<std::pair<std::string, std::string>>{
                { "low_parameter", model.logicLow }, { "high_parameter", model.logicHigh },
                { "min_supply_parameter", model.logicMinSupply }, { "max_supply_parameter", model.logicMaxSupply } } )
            {
                const auto token = tokens[i].at( "param:" + childParameter );
                if( token.starts_with( "${param:" ) ) logic[key] = token.substr( 8, token.size() - 9 );
                else
                {
                    auto spec = *std::find_if( model.parameters.begin(), model.parameters.end(), [&]( const auto& p ) { return p.id == childParameter; } );
                    spec.id = "logic_" + childParameter; spec.defaultValue = token;
                    if( !parameterIds.contains( spec.id ) ) addParameter( spec );
                    logic[key] = spec.id;
                }
            }
            for( const auto& p : pins ) if( p.role == "output" || p.role == "bidirectional" )
            {
                std::vector<std::string> enables, values;
                const auto net = "@" + p.id;
                for( std::size_t k = 0; k < blocks.size(); ++k ) for( const auto& [port, expression] : models[k].driverEnable )
                    if( nets[k].at( port ) == net )
                    {
                        enables.push_back( Expand( expression, tokens[k] ) );
                        if( models[k].driverValue.contains( port ) ) values.push_back( Expand( models[k].driverValue.at( port ), tokens[k] ) );
                    }
                std::string enabled = analog.contains( net ) ? "1" : "0";
                for( const auto& e : enables ) enabled += "+(" + e + ")";
                const auto node = "builder_enable_" + p.id + "_${id}";
                j["spice"]["instance"].push_back( "B" + node + " " + node + " 0 V={" + enabled + "}" );
                logic["driver_enable"][p.id] = "v(" + node + ")";
                if( values.size() == 1 )
                {
                    const auto valueNode = "builder_value_" + p.id + "_${id}";
                    j["spice"]["instance"].push_back( "B" + valueNode + " " + valueNode + " 0 V={" + values[0] + "}" );
                    logic["driver_value"][p.id] = "v(" + valueNode + ")";
                }
            }
            j["logic"] = logic; break;
        }
    JSON saved = JSON::array();
    for( const auto& b : blocks ) saved.push_back( { { "id", b.id }, { "model", JSON::parse( b.modelJson ) },
        { "connections", b.connections }, { "values", b.values }, { "x", b.x }, { "y", b.y } } );
    j["authoring"] = { { "version", 1 }, { "symbol", libraryId }, { "base", baseJson }, { "blocks", saved } };
    const auto compiled = MODEL_REPOSITORY::LoadText( j.dump() );
    bool anchor = false;
    for( const auto& p : pins )
    {
        const auto token = "${pin:" + p.id + "}";
        const bool used = std::any_of( compiled.instance.begin(), compiled.instance.end(), [&]( const auto& s ) { return s.find( token ) != std::string::npos; } )
            || std::any_of( compiled.definitions.begin(), compiled.definitions.end(), [&]( const auto& s ) { return s.find( token ) != std::string::npos; } );
        if( !used )
        {
            anchor = true;
            j["spice"]["instance"].push_back( "Rbuilder_unused_" + p.id + "_${id} " + token + " builder_anchor_${id} 1e12" );
        }
    }
    if( anchor ) j["spice"]["instance"].push_back( "Rbuilder_anchor_${id} builder_anchor_${id} 0 1e12" );
    const auto result = j.dump( 2 ); MODEL_REPOSITORY::LoadText( result ); return result + '\n';
}
}
