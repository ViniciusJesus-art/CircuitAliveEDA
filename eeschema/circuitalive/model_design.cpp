/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <sim/platform_runtime.h>
#include "model_design.h"
#include "circuit_snapshot.h"
#include "control_visual_json.h"
#include "control_references.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
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
std::string ModelBehaviorSummary( const std::string& text )
{
    const auto j = JSON::parse( text ); const auto m = MODEL_REPOSITORY::LoadText( text );
    std::string result = m.name + "\n\n" + m.description + "\n";
    for( const auto& line : j.value( "equations", std::vector<std::string>{} ) ) result += "\n" + line + "\n";
    if( j.contains( "behavioral" ) )
    {
        result += "\nInternal operations:\n";
        for( const auto& b : j.at( "behavioral" ).at( "blocks" ) )
        {
            result += "  " + b.at( "type" ).get<std::string>();
            for( const auto* key : { "inputs", "data", "clock", "s", "r", "reset", "out", "q", "nq" } )
                if( b.contains( key ) ) result += "  " + std::string(key) + "=" + b.at(key).dump();
            result += "\n";
        }
    }
    return result;
}
MODEL_DESIGN MODEL_DESIGN::Open( const std::string& text )
{
    auto model = MODEL_REPOSITORY::LoadText( text );
    if( model.runtime )
        throw std::runtime_error( "Programmable models use external runtime modules. Edit their .casim/module files; block composition cannot preserve runtime behavior yet." );
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
        d.coreBlock = a.value( "core_block", "" );
        d.libraryId = a.value( "symbol", d.libraryId );
        d.interactionsConfigured = a.value( "interactions_configured", false );
        for( const auto& i : a.value( "interactions", JSON::array() ) )
        {
            if( d.interactions.size() >= 64 ) throw std::runtime_error( "Too many interactive controls" );
            d.interactions.push_back( { i.at( "block" ), i.at( "control" ), i.value( "label", "" ),
                ReadControlVisual( i.value( "visual", JSON::object() ) ) } );
            d.interactions.back().reference = i.value( "reference", "" );
            d.interactions.back().onValue = i.value( "on_value", "1" );
            d.interactions.back().offValue = i.value( "off_value", "0" );
        }
        for( const auto& b : a.at( "blocks" ) )
            d.blocks.push_back( { b.at( "id" ), b.at( "model" ).dump(),
                b.at( "connections" ).get<std::map<std::string, std::string>>(),
                b.at( "values" ).get<std::map<std::string, std::string>>(), b.value( "x", 280 ), b.value( "y", 80 ) } );
        for( const auto& r : a.value( "wire_routes", JSON::array() ) )
        {
            DESIGN_WIRE_ROUTE route{ r.at( "from_block" ), r.at( "from_port" ), r.at( "to_block" ), r.at( "to_port" ), {} };
            if( r.at( "bends" ).size() > 256 ) throw std::runtime_error( "Too many wire bends" );
            for( const auto& p : r.at( "bends" ) ) route.bends.push_back( { p.at( 0 ), p.at( 1 ) } );
            d.wireRoutes.push_back( std::move( route ) );
            if( d.wireRoutes.size() > 4096 ) throw std::runtime_error( "Too many wire routes" );
        }
    }
    return d;
}
void MODEL_DESIGN::MakeCoreVisible()
{
    if( baseJson.empty() || !blocks.empty() ) return;
    auto core = JSON::parse( baseJson );
    const auto model = MODEL_REPOSITORY::LoadText( core.dump() );
    core.erase( "authoring" );
    DESIGN_BLOCK block; block.id = "Core"; block.modelJson = core.dump(); block.x = 400; block.y = 70;
    for( const auto& p : model.pins ) block.connections[p.id] = "@" + p.id;
    for( const auto& p : model.parameters ) block.values[p.id] = "@" + p.id;
    // Compile the core once, in the block. Keep component-level observations on
    // the parent and point them at the namespaced devices inside that block.
    core.erase( "behavioral" );
    core.erase( "live_updates" );
    core.erase( "live_texts" );
    core["spice"] = { { "instance", JSON::array() }, { "definitions", JSON::array() } };
    if( !model.logicVcc.empty() )
        core["logic"] = { { "vcc", model.logicVcc }, { "gnd", model.logicGround },
            { "low_parameter", model.logicLow }, { "high_parameter", model.logicHigh },
            { "min_supply_parameter", model.logicMinSupply }, { "max_supply_parameter", model.logicMaxSupply },
            { "driver_enable", model.driverEnable }, { "driver_value", model.driverValue },
            { "internal_drive", model.internalDrive } };
    std::function<void( JSON& )> rename = [&]( JSON& item )
    {
        if( item.is_string() )
        {
            auto text = item.get<std::string>(); std::size_t pos = 0;
            while( ( pos = text.find( "${id}", pos ) ) != std::string::npos )
            { text.replace( pos, 5, "b_Core_${id}" ); pos += 12; }
            item = text;
        }
        else if( item.is_structured() ) for( auto& child : item ) rename( child );
    };
    for( const auto* key : { "logic", "indicator", "control", "controls", "adjustments", "current_warning" } )
        if( core.contains( key ) ) rename( core[key] );
    baseJson = core.dump(); coreBlock = block.id;
    // A native PN diode has a real series path followed by the nonlinear
    // junction (including its voltage-dependent charge and breakdown). Expose
    // that path without substituting a fixed-drop diode or losing its physics.
    auto junction = JSON::parse( block.modelJson );
    const auto nativeInstance = std::vector<std::string>{ "D${id} ${pin:a} ${pin:k} model_${id}" };
    if( model.instance == nativeInstance && model.definitions.size() == 1
        && model.definitions.front().find( "Rs=${param:series_resistance}" ) != std::string::npos )
    {
        auto definition = model.definitions.front();
        const auto pos = definition.find( "Rs=${param:series_resistance}" );
        definition.replace( pos, std::string("Rs=${param:series_resistance}").size(), "Rs=0" );
        junction["spice"]["definitions"] = { definition };
        junction["id"] = "pn_junction"; junction["name"] = "PN junction + stored charge";
        if( junction["pins"].size()==2 && junction["pins"][0].at("id")=="k" )
            std::swap( junction["pins"][0], junction["pins"][1] );
        junction["description"] = "Nonlinear one-way conduction, reverse breakdown and voltage-dependent charge storage. The series resistance is the separate Rs block.";
        junction["equations"] = { "Forward conduction: I = Is * (exp(Vj / (N * thermal voltage)) - 1).",
            "Reverse breakdown is set by BV. Cjo sets junction capacitance; Tt sets transit charge." };
        auto& params = junction["parameters"];
        params.erase( std::remove_if( params.begin(), params.end(), []( const auto& p )
            { return p.at("id") == "series_resistance"; } ), params.end() );
        block.values.erase("series_resistance"); block.connections["a"]="junction";
        block.modelJson=junction.dump(); block.x=800;
        // A sensed behavioral resistance also represents an exact zero-ohm
        // path; SPICE's plain R device replaces zero by its minimum resistance.
        JSON resistance = { {"schema_version",1}, {"backend","ngspice"}, {"id","series_resistance"},
            {"name","Series resistance"}, {"description","V(P,N) = I * R. A zero value is an ideal link."},
            {"pins", {{{"id","p"},{"label","Input"},{"role","passive"},{"default_pin","1"}},
                      {{"id","n"},{"label","Junction"},{"role","passive"},{"default_pin","2"}}}},
            {"parameters", {{{"id","resistance"},{"label","Resistance"},{"default","0.5"},{"unit","Ohm"},{"min",0},{"max",1e6}}}},
            {"spice", {{"instance", {"Vsense_${id} sensed_${id} ${pin:n} 0",
                "Bres_${id} ${pin:p} sensed_${id} V={i(Vsense_${id})*${param:resistance}}"}}}} };
        blocks.push_back( { "Rs", resistance.dump(), {{"p","@a"},{"n","junction"}},
            {{"resistance","@series_resistance"}}, 400, 100 } );
    }
    blocks.push_back( std::move( block ) );
}
std::vector<DESIGN_INTERACTION> MODEL_DESIGN::AvailableInteractions() const
{
    std::vector<DESIGN_INTERACTION> result;
    for( const auto& b : blocks )
    {
        const auto m = MODEL_REPOSITORY::LoadText( b.modelJson );
        if( !m.controlSource.empty() ) result.push_back( { b.id, "switch", "Button", m.controlVisual } );
        for( const auto& c : m.controls ) result.push_back( { b.id, "toggle:" + c.id, c.label, c.visual } );
        for( const auto& a : m.adjustments ) result.push_back( { b.id, "adjust:" + a.id, a.label, a.visual } );
    }
    return result;
}
void MODEL_DESIGN::ConfigureInteractions()
{
    if( interactionsConfigured ) return;
    // Existing imported components already expose their core's controls on the parent.
    for( const auto& i : AvailableInteractions() )
        if( i.block == coreBlock ) interactions.push_back( i );
    interactionsConfigured = true;
}
void MODEL_DESIGN::RemoveBlock( const std::string& blockId )
{
    blocks.erase( std::remove_if( blocks.begin(), blocks.end(), [&]( const auto& b ) { return b.id == blockId; } ), blocks.end() );
    wireRoutes.erase( std::remove_if( wireRoutes.begin(), wireRoutes.end(), [&]( const auto& r )
        { return r.fromBlock == blockId || r.toBlock == blockId; } ), wireRoutes.end() );
    std::erase_if( interactions, [&]( const auto& i ) { return i.block == blockId; } );
    if( coreBlock == blockId )
    {
        auto base = JSON::parse( baseJson );
        for( const auto* key : { "logic", "indicator", "control", "controls", "adjustments", "current_warning", "memories", "resources" } ) base.erase(key);
        baseJson = base.dump(); coreBlock.clear();
    }
}
std::vector<std::filesystem::path> MODEL_DESIGN::BlockDirectories()
{
    std::vector<std::filesystem::path> dirs;
    if( const char* p = std::getenv( "CIRCUITALIVE_BLOCK_DIR" ); p && *p ) dirs.emplace_back( p );
    dirs.emplace_back( CIRCUITALIVE_BUNDLED_BLOCK_DIR );
    std::error_code error; auto executable = ExecutablePath();
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
    if( j.contains( "runtime" ) )
        throw std::runtime_error( "Programmable runtime models cannot be composed as electrical blocks yet" );
    j.erase( "authoring" );
    if( !blocks.empty() ) { j.erase( "live_updates" ); j.erase( "live_texts" ); }
    if( interactionsConfigured )
        for( const auto* key : { "control", "controls", "adjustments" } ) j.erase( key );
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
    for( const auto& block : blocks )
    {
        auto model = MODEL_REPOSITORY::LoadText( block.modelJson );
        if( model.runtime )
            throw std::runtime_error( block.id + ": programmable runtime models cannot be composed as electrical blocks yet" );
        models.push_back( std::move( model ) );
    }
    CONTROL_REFERENCE_COMPILER references( *this, models );
    std::vector<std::map<std::string, std::string>> tokens;
    std::vector<std::map<std::string, std::string>> nets;
    std::set<std::string> blockIds;
    for( std::size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex )
    {
        const auto& b = blocks[blockIndex];
        if( !Identifier( b.id ) || !blockIds.insert( b.id ).second ) throw std::runtime_error( "Invalid/repeated block ID: " + b.id );
        const auto& model = models[blockIndex];
        std::map<std::string, std::string> t{ { "id", "b_" + b.id + "_${id}" } }, connections;
        t["control:initial"] = !interactionsConfigured && b.id == coreBlock ? "${control:initial}"
                : Number( model.controlInitiallyClosed ? model.controlOn : model.controlOff );
        for( const auto& memory : model.memories ) t["memory:" + memory.id] = "${memory:" + memory.id + "}";
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
                if( net.empty() && pin.id == "ref" && ( model.id == "interactive_value" || model.id == "interactive_button" ) )
                    t["pin:" + pin.id] = "0";
                if( net.empty() && pin.id == "n" && model.id == "interactive_switch"
                    && ( !b.connections.contains( "p" ) || b.connections.at( "p" ).empty() ) )
                    t["pin:" + pin.id] = "0";
            }
            connections[pin.id] = net;
        }
        for( const auto& [p, net] : b.connections )
            if( !connections.contains( p ) ) throw std::runtime_error( b.id + ": unknown block port " + p );
        for( const auto& p : model.parameters )
        {
            auto it = b.values.find( p.id ); auto value = it == b.values.end() || it->second.empty() ? p.defaultValue : it->second;
            if( value.starts_with( "#" ) )
            {
                if( std::any_of( model.adjustments.begin(), model.adjustments.end(), [&]( const auto& a ) { return a.parameter == p.id; } ) )
                    throw std::runtime_error( "A control's initial value cannot reference another control" );
                if( p.id == model.maxStepParameter || p.id == model.frequencyParameter )
                    throw std::runtime_error( "Solver timing parameters cannot change during simulation" );
                if( p.id == model.brightnessParameter || p.id == model.ratingParameter || p.id == model.warningParameter
                    || p.id == model.logicLow || p.id == model.logicHigh || p.id == model.logicMinSupply || p.id == model.logicMaxSupply
                    || std::find( model.ledAppearance.ids.begin(), model.ledAppearance.ids.end(), p.id ) != model.ledAppearance.ids.end() )
                    throw std::runtime_error( "This display or detection setting cannot change during simulation" );
                t["param:" + p.id] = references.Placeholder( value, p );
            }
            else if( value.starts_with( "@" ) )
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
        tokens.push_back( std::move( t ) ); nets.push_back( std::move( connections ) );
    }
    if( interactions.size() > 64 ) throw std::runtime_error( "Too many interactive controls" );
    std::set<std::pair<std::string, std::string>> exposed;
    for( const auto& interaction : interactions )
    {
        if( !interactionsConfigured ) throw std::runtime_error( "Interaction configuration is missing" );
        const auto b = std::find_if( blocks.begin(), blocks.end(), [&]( const auto& candidate ) { return candidate.id == interaction.block; } );
        if( b == blocks.end() || !exposed.emplace( interaction.block, interaction.control ).second )
            throw std::runtime_error( "Missing or repeated interaction block" );
        const std::size_t n = b - blocks.begin(); const auto& model = models[n];
        const auto visual = WriteControlVisual( interaction.visual ); ReadControlVisual( visual );
        const std::string prefix = "b_" + b->id + "_";
        if( interaction.control.starts_with( "adjust:" ) )
        {
            const auto controlId = interaction.control.substr( 7 );
            const auto a = std::find_if( model.adjustments.begin(), model.adjustments.end(), [&]( const auto& candidate ) { return candidate.id == controlId; } );
            if( a == model.adjustments.end() ) throw std::runtime_error( "Unknown slider control" );
            const auto token = tokens[n].at( "param:" + a->parameter );
            std::string parameter;
            if( token.starts_with( "${param:" ) ) parameter = token.substr( 8, token.size() - 9 );
            else
            {
                auto p = *std::find_if( model.parameters.begin(), model.parameters.end(), [&]( const auto& candidate ) { return candidate.id == a->parameter; } );
                p.id = "live_" + prefix + a->parameter; p.label = interaction.label; p.defaultValue = token;
                if( !parameterIds.contains( p.id ) ) addParameter( p );
                else for( auto& savedParameter : j["parameters"] )
                    if( savedParameter.at( "id" ) == p.id ) savedParameter["default"] = token;
                parameter = p.id;
                tokens[n]["param:" + a->parameter] = "${param:" + parameter + "}";
            }
            j["adjustments"].push_back( { { "id", prefix + controlId }, { "label", interaction.label },
                { "source", Expand( a->source, tokens[n] ) }, { "parameter", parameter },
                { "unit", a->unit }, { "min", a->minimum }, { "max", a->maximum }, { "step", a->step }, { "visual", visual } } );
        }
        else
        {
            MODEL_CONTROL c;
            if( interaction.control == "switch" && !model.controlSource.empty() )
                c = { "switch", interaction.label, model.controlSource, model.controlOn, model.controlOff,
                    model.controlInitiallyClosed, interaction.visual };
            else if( interaction.control.starts_with( "toggle:" ) )
            {
                const auto controlId = interaction.control.substr( 7 );
                const auto found = std::find_if( model.controls.begin(), model.controls.end(), [&]( const auto& candidate ) { return candidate.id == controlId; } );
                if( found == model.controls.end() ) throw std::runtime_error( "Unknown button control" );
                c = *found;
            }
            else throw std::runtime_error( "Unknown interaction control" );
            j["controls"].push_back( { { "id", prefix + c.id }, { "label", interaction.label },
                { "source", Expand( c.source, tokens[n] ) }, { "on", c.on }, { "off", c.off },
                { "initially_closed", c.initiallyClosed }, { "visual", visual } } );
        }
    }
    references.Resolve( tokens, j );
    // A visible LED core may override or rebind display values in its properties.
    // Its component-level indicator must observe those exact block bindings.
    if(j.contains("indicator")&&!coreBlock.empty())for(std::size_t i=0;i<blocks.size();++i)if(blocks[i].id==coreBlock)
    {
        auto bind=[&](JSON& target,const std::string& childId)
        {
            if(childId.empty())return;
            const auto& token=tokens[i].at("param:"+childId);
            if(token.starts_with("${param:"))target=token.substr(8,token.size()-9);
            else
            {
                auto spec=*std::find_if(models[i].parameters.begin(),models[i].parameters.end(),[&](const auto& p){return p.id==childId;});
                spec.id="b_"+blocks[i].id+"_"+childId;spec.label=blocks[i].id+": "+spec.label;spec.defaultValue=token;
                if(!parameterIds.contains(spec.id))addParameter(spec);
                else for(auto& p:j["parameters"])if(p.at("id")==spec.id)p["default"]=token;
                target=spec.id;
            }
        };
        bind(j["indicator"]["brightness_parameter"],models[i].brightnessParameter);
        if(!models[i].ratingParameter.empty())bind(j["indicator"]["rating_parameter"],models[i].ratingParameter);
        if(models[i].ledAppearance.Configurable())for(std::size_t n=0;n<LED_APPEARANCE_KEYS.size();++n)
            bind(j["indicator"]["appearance"][LED_APPEARANCE_KEYS[n]],models[i].ledAppearance.ids[n]);
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
        const auto liveTokens = references.Expressions( tokens[i], i );
        for( const auto& line : models[i].instance )
            j["spice"]["instance"].push_back( references.CompileLine( Expand( line, tokens[i] ), Expand( line, liveTokens ), false, j ) );
        for( const auto& line : models[i].definitions )
            j["spice"]["definitions"].push_back( references.CompileLine( Expand( line, tokens[i] ), Expand( line, liveTokens ), true, j ) );
        references.CopyNestedUpdates( models[i], tokens[i], j );
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
    j["authoring"]["core_block"] = coreBlock;
    j["authoring"]["interactions_configured"] = interactionsConfigured;
    j["authoring"]["interactions"] = JSON::array();
    for( const auto& i : interactions ) j["authoring"]["interactions"].push_back( {
        { "block", i.block }, { "control", i.control }, { "label", i.label }, { "visual", WriteControlVisual( i.visual ) },
        { "reference", i.reference }, { "on_value", i.onValue }, { "off_value", i.offValue } } );
    j["authoring"]["wire_routes"] = JSON::array();
    for( const auto& r : wireRoutes )
    {
        JSON bends = JSON::array();
        for( const auto& p : r.bends ) bends.push_back( { p.x, p.y } );
        j["authoring"]["wire_routes"].push_back( { { "from_block", r.fromBlock }, { "from_port", r.fromPort },
            { "to_block", r.toBlock }, { "to_port", r.toPort }, { "bends", bends } } );
    }
    // Put button defaults into the deck before the first timestep. Altering a source
    // after Configure would require running the solver first and would change initial state.
    for( const auto& control : j.value( "controls", JSON::array() ) )
    {
        const std::string source = control.at( "source" ); bool found = false;
        for( auto& line : j["spice"]["instance"] )
        {
            std::istringstream fields( line.get<std::string>() ); std::string device, p, n;
            fields >> device >> p >> n;
            if( device == source && !p.empty() && !n.empty() )
            {
                line = device + " " + p + " " + n + " DC " + Number( control.value( "initially_closed", false )
                        ? control.value( "on", 5.0 ) : control.value( "off", 0.0 ) );
                found = true; break;
            }
        }
        if( !found ) throw std::runtime_error( "Interactive button needs an independent V/I source in the component instance" );
    }
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
