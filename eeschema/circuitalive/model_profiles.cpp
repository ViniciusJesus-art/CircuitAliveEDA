/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_profiles.h"
#include "model_design.h"
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <sim/platform_runtime.h>
namespace CIRCUITALIVE
{
namespace fs = std::filesystem;
using JSON = nlohmann::json;
namespace
{
JSON Read( const fs::path& file )
{
    if( !fs::exists( file ) ) return { { "schema_version", 1 }, { "symbols", JSON::object() } };
    if( fs::file_size( file ) > 4 * 1024 * 1024 ) throw std::runtime_error( "Simulation profiles file is too large" );
    std::ifstream in( file );
    auto j = JSON::parse( in );
    if( j.at( "schema_version" ) != 1 || !j.at( "symbols" ).is_object() )
        throw std::runtime_error( "Unsupported simulation profiles file: " + file.string() );
    return j;
}
// Multiple editor processes can save profiles without dropping another process's entries.
class LOCK
{
public:
    explicit LOCK( const fs::path& file )
    {
        fs::create_directories( file.parent_path() );
#ifdef _WIN32
        const auto name=fs::path(file.wstring()+L".lock");
        m_handle=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        OVERLAPPED lock{};
        if(m_handle==INVALID_HANDLE_VALUE||!LockFileEx(m_handle,LOCKFILE_EXCLUSIVE_LOCK,0,MAXDWORD,MAXDWORD,&lock))
        {
            if(m_handle!=INVALID_HANDLE_VALUE)CloseHandle(m_handle);
            throw std::runtime_error("Cannot lock simulation profiles file");
        }
#else
        m_fd = open( ( file.string() + ".lock" ).c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600 );
        if( m_fd < 0 || flock( m_fd, LOCK_EX ) != 0 )
        {
            if( m_fd >= 0 ) close( m_fd );
            throw std::runtime_error( "Cannot lock simulation profiles file" );
        }
#endif
    }
    ~LOCK()
    {
#ifdef _WIN32
        OVERLAPPED lock{};UnlockFileEx(m_handle,0,MAXDWORD,MAXDWORD,&lock);CloseHandle(m_handle);
#else
        flock(m_fd,LOCK_UN);close(m_fd);
#endif
    }
private:
#ifdef _WIN32
    HANDLE m_handle=INVALID_HANDLE_VALUE;
#else
    int m_fd;
#endif
};
void Write( const fs::path& file, const JSON& j )
{
    const auto temporary = file.string() + ".tmp." + std::to_string( ProcessId() );
    try
    {
        std::ofstream out( temporary ); out.exceptions( std::ios::failbit | std::ios::badbit );
        out << j.dump( 2 ) << '\n'; out.close();
        ReplaceSavedFile( temporary, file );
    }
    catch( ... ) { std::error_code error; fs::remove( temporary, error ); throw; }
}
}
fs::path MODEL_PROFILES::DefaultFile()
{
    if( const char* p = std::getenv( "CIRCUITALIVE_PROFILE_FILE" ); p && *p ) return fs::absolute( p );
    if( const char* p = std::getenv( "KICAD_CONFIG_HOME" ); p && *p ) return fs::path( p ) / "circuitalive/model-profiles.json";
    if( const char* p = std::getenv( "XDG_CONFIG_HOME" ); p && *p ) return fs::path( p ) / "circuitaliveeda/model-profiles.json";
#ifdef _WIN32
    if(const wchar_t* p=_wgetenv(L"APPDATA");p&&*p) return fs::path(p)/"CircuitAliveEDA/model-profiles.json";
#endif
    if( const char* p = std::getenv( "HOME" ); p && *p ) return fs::path( p ) / ".config/circuitaliveeda/model-profiles.json";
    throw std::runtime_error( "Cannot locate simulation profiles directory" );
}
std::optional<std::string> MODEL_PROFILES::BoundModel(const std::string& symbol) const
{
    const auto json=Read(m_file);const auto& symbols=json.at("symbols");
    if(!symbols.contains(symbol)||!symbols.at(symbol).value("from_model",false))return {};
    return symbols.at(symbol).at("model").get<std::string>();
}
bool MODEL_PROFILES::DefaultExcluded(const std::string& symbol) const
{
    const auto file=BoundModel(symbol);if(!file)return false;
    const auto model=MODEL_REPOSITORY::Load(*file);const auto variant=model.symbolVariants.find(symbol);
    return variant!=model.symbolVariants.end()&&JSON::parse(variant->second).value("settings",JSON::object()).value("excluded",false);
}
void MODEL_PROFILES::MakeDefault(const CIRCUIT_COMPONENT& component,const SIMULATION_MODEL& selected,bool excluded) const
{
    if(component.libraryId.empty())throw std::runtime_error("Choose a library symbol before saving defaults");
    const auto file=fs::absolute(selected.file).lexically_normal();
    const auto current=MODEL_REPOSITORY::Load(file);
    if(current.id!=selected.id)throw std::runtime_error("The model changed on disk; reload it before saving defaults");
    // Release the read handle before atomically replacing the model on Windows.
    auto json=[&]{std::ifstream input(file);return JSON::parse(input);}();
    auto model=current.ForSymbol(component.libraryId);std::set<std::string> used;
    std::map<std::string,std::string> pins;
    std::map<std::string,std::string> defaults;
    for(const auto& parameter:model.parameters)defaults[parameter.id]=parameter.defaultValue;
    for(const auto& pin:model.pins)
    {
        const auto found=component.modelPins.find(pin.id);const auto number=found==component.modelPins.end()?pin.defaultPin:found->second;
        if(!component.pins.contains(number)||!used.insert(number).second)throw std::runtime_error("Map every model port to a distinct physical pin before saving defaults");
        pins[pin.id]=number;
    }
    if(used.size()!=component.pins.size())throw std::runtime_error("Every physical symbol pin needs a model port");
    for(const auto& [id,number]:component.modelPins)if(!pins.contains(id))throw std::runtime_error("Unknown mapped model port: "+id);
    for(const auto& [id,value]:component.modelParameters)
    {
        auto parameter=std::find_if(model.parameters.begin(),model.parameters.end(),[&](const auto& p){return p.id==id;});
        if(parameter==model.parameters.end())throw std::runtime_error("Unknown model parameter: "+id);
        if(value.empty())continue;
        const auto number=ParseComponentValue(value=="$value"?component.value:value);
        if(number<parameter->minimum||number>parameter->maximum)throw std::runtime_error("Parameter default outside limits: "+id);
        defaults[id]=value;
    }
    auto links=json.value("matches",JSON::object()).value("library_ids",std::vector<std::string>{});
    if(std::find(links.begin(),links.end(),component.libraryId)==links.end())links.push_back(component.libraryId);
    json["matches"]["library_ids"]=links;
    auto exclusions=json["matches"].value("excluded_library_ids",std::vector<std::string>{});std::erase(exclusions,component.libraryId);
    json["matches"]["excluded_library_ids"]=exclusions;
    auto& variant=json["symbol_variants"][component.libraryId];variant["pin_map"]=pins;
    variant["parameter_defaults"]=defaults;
    const auto& measurement=component.measurements;
    for(const auto* port:{&measurement.positive,&measurement.negative})
        if(!port->empty()&&!pins.contains(*port))throw std::runtime_error("Unknown measurement port: "+*port);
    auto& settings=variant["settings"];
    settings["measurements"]={{"voltage",measurement.voltage},{"current",measurement.current},{"power",measurement.power},
        {"positive",measurement.positive},{"negative",measurement.negative}};
    settings["excluded"]=excluded;settings["resources"]=JSON::object();
    for(const auto& [id,selection]:component.resources)
    {
        if(std::none_of(model.resources.begin(),model.resources.end(),[&](const auto& resource){return resource.id==id;}))
            throw std::runtime_error("Unknown model resource: "+id);
        auto path=fs::path(selection.path);if(!path.empty()&&path.is_relative())path=fs::absolute(fs::path(component.projectDirectory)/path).lexically_normal();
        settings["resources"][id]={{"path",path.string()},{"format",selection.format},{"address",selection.address}};
    }
    if(json.contains("authoring"))
    {
        json=JSON::parse(MODEL_DESIGN::Open(json.dump()).Export());
    }
    MODEL_REPOSITORY::LoadText(json.dump(),file);
    Write(file,json);
    LOCK lock(m_file);auto profiles=Read(m_file);
    profiles["symbols"][component.libraryId]={{"model",file.string()},{"from_model",true}};
    Write(m_file,profiles);
}
bool MODEL_PROFILES::Apply( CIRCUIT_COMPONENT& c ) const
{
    if( !c.modelFile.empty() || !c.modelPins.empty() || !c.modelParameters.empty() ) return false;
    const auto j = Read( m_file );
    auto it = j.at( "symbols" ).find( c.libraryId );
    if( it == j.at( "symbols" ).end() ) return false;
    // Parse into a copy so malformed data cannot partially change the symbol.
    auto copy = c;
    copy.modelFile = it->at( "model" );
    if(it->value("from_model",false))
    {
        const auto model=MODEL_REPOSITORY::Load(copy.modelFile).ForSymbol(copy.libraryId);
        const auto variant=model.symbolVariants.find(copy.libraryId);
        const auto settings=variant==model.symbolVariants.end()?JSON::object():JSON::parse(variant->second).value("settings",JSON::object());
        const auto measurements=settings.value("measurements",JSON::object());
        copy.measurements={measurements.value("voltage",false),measurements.value("current",false),measurements.value("power",false),
            measurements.value("positive",std::string{}),measurements.value("negative",std::string{})};
        const auto resources=settings.value("resources",JSON::object());
        for(const auto& [id,resource]:resources.items())
            copy.resources[id]={resource.at("path"),resource.value("format","auto"),resource.value("address",std::uint64_t(0))};
        c=std::move(copy);return true;
    }
    copy.modelPins = it->at( "pins" ).get<std::map<std::string, std::string>>();
    copy.modelParameters = it->at( "parameters" ).get<std::map<std::string, std::string>>();
    const auto& m = it->at( "measurements" );
    copy.measurements = { m.at( "voltage" ), m.at( "current" ), m.at( "power" ), m.at( "positive" ), m.at( "negative" ) };
    c = std::move( copy ); return true;
}
void MODEL_PROFILES::Save( const CIRCUIT_COMPONENT& c ) const
{
    if( c.libraryId.empty() || c.modelFile.empty() ) throw std::runtime_error( "Choose a model before saving a symbol default" );
    auto model = c.modelFile;
    if( !model.starts_with( "builtin:" ) )
    {
        auto path = fs::path( model );
        if( path.is_relative() ) path = fs::path( c.projectDirectory ) / path;
        model = fs::absolute( path ).lexically_normal().string();
    }
    LOCK lock( m_file ); auto j = Read( m_file );
    const auto& m = c.measurements;
    j["symbols"][c.libraryId] = { { "model", model }, { "pins", c.modelPins },
        { "parameters", c.modelParameters }, { "measurements", { { "voltage", m.voltage },
        { "current", m.current }, { "power", m.power }, { "positive", m.positive }, { "negative", m.negative } } } };
    Write( m_file, j );
}
void MODEL_PROFILES::UpdateLinks(const fs::path& file,const std::string& modelId,
        const std::map<std::string,std::map<std::string,std::string>>& mappings,
        const std::vector<std::string>& removed,bool replaceOtherDefaults) const
{
    const auto path=fs::absolute(file).lexically_normal().string();
    LOCK lock(m_file);auto json=Read(m_file);auto& symbols=json["symbols"];
    auto owned=[&](const JSON& entry){const auto model=entry.value("model",std::string{});
        std::error_code error;
        const bool sameFile=!model.empty()&&!model.starts_with("builtin:")&&fs::equivalent(fs::path(model),file,error);
        return sameFile||model==path||(replaceOtherDefaults&&model=="builtin:"+modelId);};
    for(const auto& symbol:removed)
        if(symbols.contains(symbol)&&owned(symbols.at(symbol)))symbols.erase(symbol);
    for(const auto& [symbol,pins]:mappings)
    {
        if(symbols.contains(symbol)&&!owned(symbols.at(symbol))&&!replaceOtherDefaults)continue;
        auto entry=symbols.contains(symbol)&&owned(symbols.at(symbol))?symbols.at(symbol):JSON{
            {"parameters",JSON::object()}, {"measurements",{{"voltage",false},{"current",false},{"power",false},{"positive",""},{"negative",""}}}};
        entry["model"]=path;entry["pins"]=pins;symbols[symbol]=entry;
    }
    Write(m_file,json);
}
void MODEL_PROFILES::Remove( const std::string& id ) const
{
    LOCK lock( m_file ); auto j = Read( m_file ); j["symbols"].erase( id ); Write( m_file, j );
}
}
