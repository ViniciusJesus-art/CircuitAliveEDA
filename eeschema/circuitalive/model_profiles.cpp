/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_profiles.h"
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
        ReplaceFile( temporary, file );
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
bool MODEL_PROFILES::Apply( CIRCUIT_COMPONENT& c ) const
{
    if( !c.modelFile.empty() || !c.modelPins.empty() || !c.modelParameters.empty() ) return false;
    const auto j = Read( m_file );
    auto it = j.at( "symbols" ).find( c.libraryId );
    if( it == j.at( "symbols" ).end() ) return false;
    // Parse into a copy so malformed data cannot partially change the symbol.
    auto copy = c;
    copy.modelFile = it->at( "model" );
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
void MODEL_PROFILES::Remove( const std::string& id ) const
{
    LOCK lock( m_file ); auto j = Read( m_file ); j["symbols"].erase( id ); Write( m_file, j );
}
}
