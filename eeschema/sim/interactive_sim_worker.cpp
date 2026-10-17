/*
 * CircuitAliveEDA stage-1 worker: ngspice globals never enter the editor process.
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interactive_sim_protocol.h"
#include "runtime_memory.h"
#include <filesystem>
#include <dlfcn.h>
#include <fcntl.h>
#include <ngspice/sharedspice.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <mutex>
#include <set>
#include <signal.h>
#include <sstream>
#include <unistd.h>

namespace CIRCUITALIVE
{
using namespace PROTOCOL;

static std::string Lower( std::string aText )
{
    std::transform( aText.begin(), aText.end(), aText.begin(),
                    []( unsigned char c ) { return std::tolower( c ); } );
    return aText;
}

static std::string Number( double aValue )
{
    std::ostringstream stream;
    stream.imbue( std::locale::classic() );
    stream << std::setprecision( 17 ) << aValue;
    return stream.str();
}

static bool Token( const std::string& aText )
{
    return !aText.empty() && aText.size() <= 256
           && aText.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "0123456789_/.$#+@()[]{}:-" ) == std::string::npos;
}

class ENGINE
{
public:
    ENGINE()
    {
        ngSpice_Init( &OnMessage, &OnStatus, &OnExit, &OnData, &OnInit, &OnBackground, this );
    }

    JSON Handle( const JSON& aRequest )
    {
        const std::string op = aRequest.at( "op" );

        if( op == "hello" )
            return { { "version", VERSION } };

        Refresh();

        if( op == "configure" )
            Configure( aRequest );
        else if( op == "play" )
            Play();
        else if( op == "pause" )
            Pause();
        else if( op == "stop" )
        {
            Stop();
            return { { "generation", m_generation } };
        }
        else if( op == "advance" )
            Advance( aRequest.at( "time" ) );
        else if( op == "source" )
            Source( aRequest.at( "reference" ), aRequest.at( "value" ) );
        else if( op == "memory" )
            return Memory( aRequest );
        else if( op == "snapshot" )
            return Snapshot( aRequest.value( "drain", false ) );
        else
            throw std::runtime_error( "Unknown simulation operation" );

        return JSON::object();
    }

    void Stop()
    {
        if( !Finished() )
            Halt();

        Command( "delete all" );
        Command( "destroy all" );
        Command( "remcirc" );
        ngSpice_Command( nullptr );
        m_commands = 0;
        m_state = SIM_SESSION_STATE::STOPPED;
        std::lock_guard lock( m_mutex );
        Clear();
        ++m_generation; // Invalidate all snapshots and pending visual state.
    }

private:
    void Configure( const JSON& aRequest )
    {
        if( m_state != SIM_SESSION_STATE::STOPPED )
            throw std::runtime_error( "Stop the session before changing the circuit" );

        SIM_SESSION_CONFIG cfg;
        cfg.step = aRequest.at( "step" );
        cfg.maxStep = aRequest.at( "max_step" );
        cfg.horizon = aRequest.at( "horizon" );
        cfg.useInitialConditions = aRequest.at( "uic" );
        cfg.historyCapacity = aRequest.at( "capacity" );
        cfg.signals = aRequest.at( "signals" ).get<std::vector<std::string>>();

        if( !std::isfinite( cfg.step ) || !std::isfinite( cfg.maxStep )
                || !std::isfinite( cfg.horizon ) || cfg.step <= 0 || cfg.maxStep <= 0
                || cfg.horizon <= 0 || cfg.step > cfg.horizon || cfg.maxStep > cfg.horizon
                || cfg.horizon / std::min( cfg.step, cfg.maxStep ) > 1e8
                || cfg.historyCapacity == 0 || cfg.historyCapacity > 4096
                || cfg.signals.empty() || cfg.signals.size() > 256
                || cfg.signals.size() * cfg.historyCapacity > 65536 )
            throw std::runtime_error( "Invalid simulation timing, signals or history capacity" );

        std::vector<std::string> names;
        std::set<std::string> unique;
        for( const auto& signal : cfg.signals )
        {
            if( !Token( signal ) )
                throw std::runtime_error( "Invalid signal token" );
            std::string name = Lower( signal );
            if( name.starts_with( "v(" ) && name.back() == ')' )
                name = name.substr( 2, name.size() - 3 );
            else if( name.starts_with( "i(" ) && name.back() == ')' )
                name = name.substr( 2, name.size() - 3 ) + "#branch";
            if( !unique.insert( name ).second )
                throw std::runtime_error( "Duplicate signal" );
            names.push_back( name );
        }

        std::string netlist = aRequest.at( "netlist" );
        if( netlist.size() > 1024 * 1024 || netlist.find( '\0' ) != std::string::npos )
            throw std::runtime_error( "Invalid or oversized netlist" );

        std::istringstream input( netlist );
        std::string line, deck;
        bool title = true, end = false;
        const std::set<std::string> allowed = { ".model", ".subckt", ".ends", ".param",
                                                ".func", ".ic", ".options", ".option",
                                                ".global", ".temp" };
        while( std::getline( input, line ) )
        {
            if( title )
            {
                deck += line + '\n';
                title = false;
                continue;
            }
            std::istringstream words( Lower( line ) );
            std::string first;
            words >> first;
            if( first.empty() || first[0] == '*' )
                continue;
            if( end )
                throw std::runtime_error( "Unexpected data after .end" );
            if( first == ".end" )
            {
                end = true;
                continue;
            }
            // Stage 1 accepts inline models only. Analysis and interpreter commands
            // (including commands hidden inside include files) cannot own this worker.
            if( first[0] == '.' && !allowed.contains( first ) )
                throw std::runtime_error( "Unsupported deck directive: " + first );
            deck += line + '\n';
        }
        if( title || !end )
            throw std::runtime_error( "A component deck with a title and .end is required" );

        deck += ".save none";
        for( const auto& signal : cfg.signals )
            deck += " " + signal;
        deck += "\n.tran " + Number( cfg.step ) + " " + Number( cfg.horizon ) + " 0 "
                + Number( cfg.maxStep ) + ( cfg.useInitialConditions ? " uic\n" : "\n" ) + ".end\n";

        if( aRequest.contains( "memories" ) )
            for( const auto& region : aRequest.at( "memories" ) )
                cfg.memories.push_back(
                        { region.at( "id" ),
                          region.at( "label" ),
                          region.at( "word_bits" ),
                          region.at( "writable" ),
                          { MemoryUnhex( region.at( "data" ) ), MemoryUnhex( region.at( "known" ) ) } } );
        m_memory.Reset( cfg.memories );
        m_config = std::move( cfg );
        m_names = std::move( names );
        m_deck = std::move( deck );
        m_configured = true;
        std::lock_guard lock( m_mutex );
        m_ring.assign( m_config.historyCapacity, SIM_SAMPLE{} );
        for( auto& sample : m_ring )
            sample.values.resize( m_names.size() );
        Clear();
    }

    void Clear() // m_mutex held; never called while the solver is active.
    {
        m_latest = {};
        m_samples = m_overwritten = m_events = 0;
        m_eventTime = 0;
        m_head = m_size = 0;
        m_indices.clear();
        m_error.clear();
        m_finished = true;
    }

    bool Finished()
    {
        std::lock_guard lock( m_mutex );
        return m_finished;
    }

    void Command( const std::string& aCommand )
    {
        if( ngSpice_Command( const_cast<char*>( aCommand.c_str() ) ) != 0 )
            throw std::runtime_error( "ngspice rejected: " + aCommand );

        // ngspice also retains interpreter command structures independently of plots.
        if( ++m_commands >= 32 && Finished() )
        {
            ngSpice_Command( nullptr );
            m_commands = 0;
        }
    }

    void CheckError()
    {
        std::lock_guard lock( m_mutex );
        if( m_error.empty() ) m_error = m_memory.Error();
        if( !m_error.empty() )
        {
            m_state = SIM_SESSION_STATE::ERROR;
            throw std::runtime_error( m_error );
        }
    }

    void Load()
    {
        if( !m_configured )
            throw std::runtime_error( "Configure a circuit first" );

        {
            std::lock_guard lock( m_mutex );
            Clear();
            ++m_generation;
        }
        m_memory.Reset( m_config.memories );
        if( !m_config.memories.empty() && !m_memoryModule )
        {
            const auto path =
                    ( std::filesystem::canonical( "/proc/self/exe" ).parent_path() / "circuitalive-memory.cm" )
                            .string();
            const int fd = open( path.c_str(), O_RDONLY | O_CLOEXEC );
            if( fd < 0 )
                throw std::runtime_error( "Memory engine file is missing: " + path );
            const auto modulePath = "/proc/self/fd/" + std::to_string( fd );
            m_memoryModule = dlopen( modulePath.c_str(), RTLD_NOW | RTLD_LOCAL );
            if( !m_memoryModule )
                throw std::runtime_error( std::string( "Cannot load memory engine: " ) + dlerror() );
            auto bind = reinterpret_cast<void ( * )( const CA_MEMORY_BACKEND* )>(
                    dlsym( m_memoryModule, "CircuitAliveMemorySetBackend" ) );
            if( !bind )
                throw std::runtime_error( "Invalid memory engine" );
            auto backend = m_memory.Backend();
            bind( &backend );
            Command( "codemodel " + modulePath );
            close( fd );
            CheckError();
        }
        std::istringstream stream( m_deck );
        std::vector<std::string> lines;
        for( std::string line; std::getline( stream, line ); )
            lines.push_back( std::move( line ) );
        std::vector<char*> pointers;
        for( auto& line : lines )
            pointers.push_back( line.data() );
        pointers.push_back( nullptr );
        if( ngSpice_Circ( pointers.data() ) != 0 )
        {
            m_state = SIM_SESSION_STATE::ERROR;
            throw std::runtime_error( "ngspice could not load the circuit" );
        }
        CheckError();
        Command( "esave none" ); // Event history is outside the analog stage-1 contract.
    }

    void Run( bool aFresh )
    {
        {
            std::lock_guard lock( m_mutex );
            m_finished = false; // Set before dispatch; even very short runs can finish immediately.
        }
        m_state = SIM_SESSION_STATE::RUNNING;
        try
        {
            Command( aFresh ? "bg_run" : "bg_resume" );
        }
        catch( ... )
        {
            std::lock_guard lock( m_mutex );
            m_finished = true;
            m_state = SIM_SESSION_STATE::ERROR;
            throw;
        }
    }

    void Play()
    {
        if( m_state == SIM_SESSION_STATE::RUNNING )
            return;
        const bool fresh = m_state == SIM_SESSION_STATE::STOPPED;
        if( !fresh && m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Stop the completed/failed session before restarting" );
        if( fresh )
            Load();
        else
            Command( "delete all" );
        Run( fresh );
    }

    void Halt()
    {
        Command( "bg_halt" );
        Wait(); // A pause is acknowledged only after the final callback.
    }

    void Wait()
    {
        std::unique_lock lock( m_mutex );
        if( !m_condition.wait_for( lock, std::chrono::seconds( 4 ), [&] { return m_finished; } ) )
            throw std::runtime_error( "ngspice did not acknowledge stopping" );
    }

    void Refresh()
    {
        bool finished, failed;
        {
            std::lock_guard lock( m_mutex );
            finished = m_finished;
            failed = !m_error.empty();
        }
        if( m_state != SIM_SESSION_STATE::RUNNING )
            return;
        if( failed && !finished )
        {
            Halt();
            finished = true;
        }
        if( finished )
        {
            std::lock_guard lock( m_mutex );
            if( failed || m_samples == 0 )
                m_state = SIM_SESSION_STATE::ERROR;
            else if( m_latest.time >= m_config.horizon * ( 1 - 1e-12 ) )
                m_state = SIM_SESSION_STATE::COMPLETED;
            else
                m_state = SIM_SESSION_STATE::PAUSED;
        }
    }

    void Pause()
    {
        if( m_state == SIM_SESSION_STATE::STOPPED )
            throw std::runtime_error( "No active simulation to pause" );
        if( m_state == SIM_SESSION_STATE::RUNNING )
            Halt();
        Refresh();
        CheckError();
    }

    void Advance( double aTime )
    {
        if( !m_configured || !std::isfinite( aTime ) || aTime <= 0 || aTime > m_config.horizon )
            throw std::runtime_error( "Target must be positive and within the configured horizon" );
        if( m_state == SIM_SESSION_STATE::RUNNING )
            Pause();
        if( m_state != SIM_SESSION_STATE::STOPPED && m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Cannot advance a completed/failed session" );
        {
            std::lock_guard lock( m_mutex );
            if( aTime <= m_latest.time )
                throw std::runtime_error( "Simulation time must advance monotonically" );
        }

        const bool fresh = m_state == SIM_SESSION_STATE::STOPPED;
        if( fresh )
            Load();
        else
            Command( "delete all" );
        // Before the first run, "delete all" would also remove the .save policy.
        // On resume the current plot already owns its storage and selected vectors.
        Command( "stop when time >= " + Number( aTime ) );
        Run( fresh );
        Wait();
        Refresh();
        CheckError();
        if( m_state == SIM_SESSION_STATE::ERROR )
            throw std::runtime_error( "Simulation produced no data" );
    }

    void Source( const std::string& aReference, double aValue )
    {
        if( m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Source changes require a confirmed pause" );
        if( !std::isfinite( aValue ) || !Token( aReference )
                || aReference.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                 "0123456789_" ) != std::string::npos
                || ( std::tolower( aReference.front() ) != 'v'
                     && std::tolower( aReference.front() ) != 'i' ) )
            throw std::runtime_error( "A V/I source reference and finite value are required" );

        Command( "alter " + aReference + " dc = " + Number( aValue ) );
        CheckError();
        std::lock_guard lock( m_mutex );
        ++m_events;
        m_eventTime = m_latest.time;
    }

    JSON Memory( const JSON& request )
    {
        if( !Finished() || ( m_state != SIM_SESSION_STATE::PAUSED && m_state != SIM_SESSION_STATE::COMPLETED ) )
            throw std::runtime_error( "Pause the simulation before reading memory" );
        CheckError();
        const std::size_t length = request.at( "length" );
        if( length > 4096 )
            throw std::runtime_error( "Memory page exceeds 4096 bytes" );
        const auto image = m_memory.Read( request.at( "id" ), request.at( "offset" ), length );
        return {
                { "time", m_latest.time }, { "data", MemoryHex( image.data ) }, { "known", MemoryHex( image.known ) } };
    }

    JSON Snapshot( bool aDrain )
    {
        // Never read a live ngspice vector. vecindex is an accepted-step counter,
        // not a portable measure of retained storage (ngspice versions differ).
        std::size_t retained = 0;
        if( Finished() && m_samples )
        {
            char timeName[] = "time";
            if( auto* vector = ngGet_Vec_Info( timeName ) )
                retained = vector->v_length;
            if( retained != 1 )
                throw std::runtime_error( "ngspice does not support single-point streaming storage ("
                                          + std::to_string( retained ) + " retained)" );
        }
        std::ifstream statm( "/proc/self/statm" );
        std::uint64_t pages = 0, resident = 0;
        statm >> pages >> resident;
        resident *= static_cast<std::uint64_t>( sysconf( _SC_PAGESIZE ) );

        std::lock_guard lock( m_mutex );
        JSON history = JSON::array();
        for( std::size_t i = 0; i < m_size; ++i )
        {
            const auto& sample = m_ring[( m_head + i ) % m_ring.size()];
            history.push_back( { { "time", sample.time }, { "values", sample.values } } );
        }
        JSON result = { { "state", StateName( m_state ) }, { "generation", m_generation },
                        { "time", m_latest.time }, { "values", m_latest.values },
                        { "history", std::move( history ) }, { "samples", m_samples },
                        { "overwritten", m_overwritten }, { "events", m_events },
                        { "event_time", m_eventTime }, { "retained", retained },
                        { "resident_bytes", resident }, { "error", m_error } };
        if( aDrain )
            m_head = m_size = 0;
        return result;
    }

    static int OnMessage( char* aMessage, int, void* aUser )
    {
        auto& self = *static_cast<ENGINE*>( aUser );
        const std::string message = Lower( aMessage );
        if( message.find( "error:" ) != std::string::npos
                || message.find( "fatal" ) != std::string::npos
                || message.find( "timestep too small" ) != std::string::npos )
        {
            std::lock_guard lock( self.m_mutex );
            if( self.m_error.empty() )
                self.m_error = std::string( aMessage ).substr( 0, 1024 );
        }
        return 0;
    }

    static int OnStatus( char*, int, void* ) { return 0; }
    static int OnInit( pvecinfoall, int, void* ) { return 0; }
    static int OnExit( int, NG_BOOL, NG_BOOL, int, void* aUser )
    {
        char message[] = "Error: ngspice requested a controlled exit";
        return OnMessage( message, 0, aUser );
    }

    static int OnBackground( NG_BOOL aFinished, int, void* aUser )
    {
        auto& self = *static_cast<ENGINE*>( aUser );
        {
            std::lock_guard lock( self.m_mutex );
            self.m_finished = aFinished;
        }
        self.m_condition.notify_all();
        return 0;
    }

    static int OnData( pvecvaluesall aData, int, int, void* aUser )
    {
        auto& self = *static_cast<ENGINE*>( aUser );
        std::lock_guard lock( self.m_mutex );
        if( !self.m_error.empty() )
            return 0;
        if( self.m_indices.empty() )
        {
            for( const auto& name : self.m_names )
            {
                int index = -1;
                for( int j = 0; j < aData->veccount; ++j )
                {
                    if( Lower( aData->vecsa[j]->name ) == name )
                        index = j;
                }
                if( index < 0 )
                {
                    self.m_error = "Signal not found: " + name;
                    return 0;
                }
                self.m_indices.push_back( index );
            }
            self.m_latest.values.resize( self.m_names.size() );
        }
        for( int j = 0; j < aData->veccount; ++j )
        {
            if( aData->vecsa[j]->is_scale )
                self.m_latest.time = aData->vecsa[j]->creal;
        }
        for( std::size_t i = 0; i < self.m_indices.size(); ++i )
        {
            auto* value = aData->vecsa[self.m_indices[i]];
            if( value->is_complex || !std::isfinite( value->creal ) )
            {
                self.m_error = "Non-finite/complex data in transient analysis";
                return 0;
            }
            self.m_latest.values[i] = value->creal;
        }
        try { self.m_memory.Commit(self.m_latest.time); }
        catch( const std::exception& e ) { self.m_error=e.what(); return 0; }
        if( !self.m_memory.Error().empty() ) { self.m_error=self.m_memory.Error(); return 0; }
        ++self.m_samples;
        const auto position = ( self.m_head + self.m_size ) % self.m_ring.size();
        self.m_ring[position] = self.m_latest;
        if( self.m_size == self.m_ring.size() )
        {
            self.m_head = ( self.m_head + 1 ) % self.m_ring.size();
            ++self.m_overwritten;
        }
        else
            ++self.m_size;
        return 0;
    }

    SIM_SESSION_STATE       m_state = SIM_SESSION_STATE::STOPPED; // Control thread only.
    RUNTIME_MEMORY m_memory;
    void* m_memoryModule = nullptr; // Kept loaded for the physical worker lifetime.
    SIM_SESSION_CONFIG      m_config;
    std::string             m_deck;
    std::vector<std::string> m_names;
    bool                    m_configured = false;
    unsigned                m_commands = 0;
    std::mutex              m_mutex;
    std::condition_variable m_condition;
    bool                    m_finished = true;
    std::string             m_error;
    SIM_SAMPLE              m_latest;
    std::vector<int>        m_indices;
    std::vector<SIM_SAMPLE> m_ring;
    std::size_t            m_head = 0, m_size = 0;
    std::uint64_t          m_samples = 0, m_overwritten = 0, m_events = 0, m_generation = 0;
    double                 m_eventTime = 0;
};
} // namespace CIRCUITALIVE

int main()
{
    using namespace CIRCUITALIVE;
    using namespace PROTOCOL;
    std::locale::global( std::locale::classic() );
    signal( SIGPIPE, SIG_IGN ); // Worker-local; never change the editor's signal handlers.

    // Keep accidental library/code-model stdout out of the protocol channel.
    FILE* wire = fdopen( dup( STDOUT_FILENO ), "w" );
    if( !wire || dup2( STDERR_FILENO, STDOUT_FILENO ) < 0 )
        return 2;

    ENGINE engine;
    std::string input;
    char chunk[8192];
    for( ;; )
    {
        const ssize_t count = read( STDIN_FILENO, chunk, sizeof( chunk ) );
        if( count < 0 && errno == EINTR )
            continue;
        if( count <= 0 )
            break;
        input.append( chunk, count );
        if( input.size() > MAX_MESSAGE )
            break;

        for( auto end = input.find( '\n' ); end != std::string::npos; end = input.find( '\n' ) )
        {
            JSON response;
            bool stopRequested = false;
            try
            {
                const auto request = JSON::parse( input.substr( 0, end ) );
                stopRequested = request.value( "op", "" ) == "stop";
                response = engine.Handle( request );
                response["ok"] = true;
            }
            catch( const std::exception& error )
            {
                response = { { "ok", false }, { "error", std::string( error.what() ).substr( 0, 2048 ) } };
            }
            input.erase( 0, end + 1 );
            const std::string message = response.dump() + '\n';
            if( fwrite( message.data(), 1, message.size(), wire ) != message.size()
                    || fflush( wire ) != 0 )
                goto done;
            // Stop ends this physical solver instance. The controller may start
            // a fresh worker, with the original circuit, for the next Play.
            if( stopRequested )
                std::_Exit( 0 );
        }
    }
done:
    try { engine.Stop(); } catch( ... ) {}
    // ngspice owns helper threads; OS teardown avoids C++ callback lifetime races at exit.
    std::_Exit( 0 );
}
