/*
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interactive_sim_protocol.h"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <filesystem>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace CIRCUITALIVE
{
using namespace PROTOCOL;
using CLOCK = std::chrono::steady_clock;

#ifdef _WIN32
#include "interactive_sim_windows.h"
#else
struct INTERACTIVE_SIM_SESSION::IMPL
{
    mutable std::mutex        mutex;
    pid_t                     pid = -1;
    int                       socket = -1;
    std::chrono::milliseconds timeout;
    std::string               input;
    std::string               workerPath;
    JSON                      configuration;
    std::uint64_t             generationOffset = 0;

    explicit IMPL( std::chrono::milliseconds aTimeout ) : timeout( aTimeout ) {}

    ~IMPL() { Terminate(); }

    void Terminate() noexcept
    {
        if( socket >= 0 )
        {
            shutdown( socket, SHUT_RDWR );
            close( socket );
            socket = -1;
        }

        if( pid <= 0 )
            return;

        // EOF normally shuts down the worker. Bound cleanup even after a solver crash/hang.
        for( int attempt = 0; attempt < 50; ++attempt )
        {
            int status;
            pid_t result = waitpid( pid, &status, WNOHANG );

            if( result == pid || ( result < 0 && errno == ECHILD ) )
            {
                pid = -1;
                return;
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        kill( pid, SIGKILL );
        while( waitpid( pid, nullptr, 0 ) < 0 && errno == EINTR ) {}
        pid = -1;
    }

    void Spawn()
    {
        input.clear();
        int sockets[2];
        if( socketpair( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets ) != 0 )
            throw std::runtime_error( "Cannot create simulation socket" );

        // A GUI launcher may have closed stdin/stdout. Keep spawn actions independent
        // of those descriptor numbers, so closing an endpoint cannot close child stdin.
        for( int& descriptor : sockets )
        {
            if( descriptor < 3 )
            {
                int replacement = fcntl( descriptor, F_DUPFD_CLOEXEC, 3 );
                if( replacement < 0 )
                {
                    close( sockets[0] );
                    close( sockets[1] );
                    throw std::runtime_error( "Cannot duplicate simulation socket" );
                }
                close( descriptor );
                descriptor = replacement;
            }
        }

        posix_spawn_file_actions_t actions;
        int result = posix_spawn_file_actions_init( &actions );
        if( result != 0 )
        {
            close( sockets[0] );
            close( sockets[1] );
            throw std::runtime_error( "Cannot prepare simulation worker spawn" );
        }
        auto recordError = [&]( int error ) { if( result == 0 ) result = error; };
        recordError( posix_spawn_file_actions_adddup2( &actions, sockets[1], STDIN_FILENO ) );
        recordError( posix_spawn_file_actions_adddup2( &actions, sockets[1], STDOUT_FILENO ) );
        recordError( posix_spawn_file_actions_addclose( &actions, sockets[0] ) );
        recordError( posix_spawn_file_actions_addclose( &actions, sockets[1] ) );
        char* args[] = { const_cast<char*>( workerPath.c_str() ), nullptr };
        if( result == 0 )
            result = posix_spawn( &pid, workerPath.c_str(), &actions, nullptr, args, environ );
        posix_spawn_file_actions_destroy( &actions );
        close( sockets[1] );
        socket = sockets[0];

        if( result != 0 )
            throw std::runtime_error( std::string( "Cannot start simulation worker: " )
                                      + std::strerror( result ) );

        auto hello = Request( { { "op", "hello" } } );
        if( hello.at( "version" ).get<int>() != VERSION )
            throw std::runtime_error( "Incompatible simulation worker" );
    }

    void Wait( short aEvents, CLOCK::time_point aDeadline )
    {
        for( ;; )
        {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>( aDeadline
                                                                                  - CLOCK::now() );
            if( remaining.count() <= 0 )
                throw std::runtime_error( "Simulation worker timed out" );

            pollfd descriptor{ socket, aEvents, 0 };
            int result = poll( &descriptor, 1, static_cast<int>( remaining.count() ) );

            if( result < 0 && errno == EINTR )
                continue;

            if( result <= 0 )
                throw std::runtime_error( "Simulation worker timed out or poll failed" );

            // Drain a final reply before handling POLLHUP.
            if( descriptor.revents & aEvents )
                return;

            throw std::runtime_error( "Simulation worker disconnected" );
        }
    }

    JSON Request( JSON aRequest )
    {
        if( socket < 0 )
            throw std::runtime_error( "Simulation worker is unavailable" );

        JSON response;

        try
        {
            const auto deadline = CLOCK::now() + timeout;
            std::string message = aRequest.dump() + '\n';

            if( message.size() > MAX_MESSAGE )
                throw std::runtime_error( "Simulation request exceeds protocol limit" );

            std::size_t sent = 0;
            while( sent < message.size() )
            {
                Wait( POLLOUT, deadline );
                ssize_t count = send( socket, message.data() + sent, message.size() - sent,
                                      MSG_NOSIGNAL | MSG_DONTWAIT );
                if( count < 0 && ( errno == EINTR || errno == EAGAIN ) )
                    continue;
                if( count <= 0 )
                    throw std::runtime_error( std::string( "Cannot write to simulation worker: " )
                                              + std::strerror( errno ) );
                sent += count;
            }

            while( input.find( '\n' ) == std::string::npos )
            {
                Wait( POLLIN, deadline );
                char data[8192];
                ssize_t count = recv( socket, data, sizeof( data ), MSG_DONTWAIT );
                if( count < 0 && ( errno == EINTR || errno == EAGAIN ) )
                    continue;
                if( count <= 0 )
                    throw std::runtime_error( "Simulation worker exited without a reply" );
                input.append( data, count );
                if( input.size() > MAX_MESSAGE )
                    throw std::runtime_error( "Simulation reply exceeds protocol limit" );
            }

            const auto end = input.find( '\n' );
            response = JSON::parse( input.substr( 0, end ) );
            input.erase( 0, end + 1 );
            response.at( "ok" ).get<bool>();
        }
        catch( ... )
        {
            Terminate();
            throw;
        }

        if( !response.at( "ok" ).get<bool>() )
            throw std::runtime_error( response.at( "error" ).get<std::string>() );

        return response;
    }
};

#endif

INTERACTIVE_SIM_SESSION::INTERACTIVE_SIM_SESSION(
        const std::string& aWorkerExecutable, std::chrono::milliseconds aTimeout ) :
        m_impl( std::make_unique<IMPL>( aTimeout ) )
{
    if( aTimeout.count() <= 0 || aTimeout > std::chrono::minutes( 10 ) )
        throw std::invalid_argument( "Invalid simulation request timeout" );

    m_impl->workerPath = aWorkerExecutable;
    m_impl->Spawn();
}

INTERACTIVE_SIM_SESSION::~INTERACTIVE_SIM_SESSION() = default;

void INTERACTIVE_SIM_SESSION::Configure( const std::string& aNetlist,
                                       const SIM_SESSION_CONFIG& aConfig )
{
    std::lock_guard lock( m_impl->mutex );
    JSON request = { { "op", "configure" }, { "netlist", aNetlist },
                      { "step", aConfig.step }, { "horizon", aConfig.horizon },
                      { "max_step", aConfig.maxStep }, { "uic", aConfig.useInitialConditions },
                      { "capacity", aConfig.historyCapacity }, { "signals", aConfig.signals } };
    request["activity_monitors"] = JSON::array();
    for( const auto& monitor : aConfig.activityMonitors )
        request["activity_monitors"].push_back( {
                { "signal", monitor.signal }, { "reference", monitor.reference },
                { "supply", monitor.supply }, { "low", monitor.low }, { "high", monitor.high },
                { "default_supply", monitor.defaultSupply }, { "min_supply", monitor.minSupply },
                { "max_supply", monitor.maxSupply } } );
    request["memories"]=JSON::array();
    for(const auto& region:aConfig.memories)request["memories"].push_back({{"id",region.id},{"label",region.label},
        {"word_bits",region.wordBits},{"writable",region.writable},{"data",MemoryHex(region.initial.data)},{"known",MemoryHex(region.initial.known)}});
    m_impl->Request( request );
    m_impl->configuration = std::move( request );
}

void INTERACTIVE_SIM_SESSION::Play()
{
    std::lock_guard lock( m_impl->mutex );
    m_impl->Request( { { "op", "play" } } );
}

void INTERACTIVE_SIM_SESSION::Pause()
{
    std::lock_guard lock( m_impl->mutex );
    m_impl->Request( { { "op", "pause" } } );
}

void INTERACTIVE_SIM_SESSION::Stop()
{
    std::lock_guard lock( m_impl->mutex );
    if( m_impl->pid > 0 )
    {
        try
        {
            auto response = m_impl->Request( { { "op", "stop" } } );
            m_impl->generationOffset += response.at( "generation" ).get<std::uint64_t>();
        }
        catch( ... )
        {
            // A dead/unresponsive solver can still be reset by discarding the process.
            ++m_impl->generationOffset;
        }
    }
    else
        ++m_impl->generationOffset;

    m_impl->Terminate();
    m_impl->Spawn();
    if( !m_impl->configuration.is_null() )
        m_impl->Request( m_impl->configuration );
}

void INTERACTIVE_SIM_SESSION::AdvanceTo( double aTime )
{
    std::lock_guard lock( m_impl->mutex );
    m_impl->Request( { { "op", "advance" }, { "time", aTime } } );
}

void INTERACTIVE_SIM_SESSION::SetSourceValue( const std::string& aReference, double aValue,
                                            const std::vector<SIM_PARAMETER_CHANGE>& aChanges )
{
    std::lock_guard lock( m_impl->mutex );
    JSON request = { { "op", "source" }, { "reference", aReference }, { "value", aValue } };
    request["changes"] = JSON::array();
    for( const auto& change : aChanges ) request["changes"].push_back( {
        {"reference",change.reference}, {"parameter",change.parameter},
        {"expressions",change.expressions}, {"model",change.model} } );
    m_impl->Request( request );
}

int INTERACTIVE_SIM_SESSION::WorkerPid() const
{
    std::lock_guard lock( m_impl->mutex );
    return m_impl->pid;
}

MEMORY_SNAPSHOT INTERACTIVE_SIM_SESSION::ReadMemory(const std::string& id,std::size_t offset,std::size_t length)
{
    std::lock_guard lock(m_impl->mutex);
    const auto response=m_impl->Request({{"op","memory"},{"id",id},{"offset",offset},{"length",length}});
    return {id,response.at("time"),{MemoryUnhex(response.at("data")),MemoryUnhex(response.at("known"))}};
}

SIM_SESSION_SNAPSHOT INTERACTIVE_SIM_SESSION::Snapshot( bool aDrainHistory )
{
    std::lock_guard lock( m_impl->mutex );
    auto response = m_impl->Request( { { "op", "snapshot" }, { "drain", aDrainHistory } } );
    SIM_SESSION_SNAPSHOT result;
    result.state = ParseState( response.at( "state" ) );
    result.generation = response.at( "generation" ).get<std::uint64_t>()
                        + m_impl->generationOffset;
    result.latest.time = response.at( "time" );
    result.latest.values = response.at( "values" ).get<std::vector<double>>();
    result.samplesReceived = response.at( "samples" );
    result.samplesOverwritten = response.at( "overwritten" );
    result.eventsApplied = response.at( "events" );
    result.lastEventTime = response.at( "event_time" );
    result.engineRetainedPoints = response.at( "retained" );
    result.residentBytes = response.at( "resident_bytes" );
    result.error = response.at( "error" );
    if( response.contains( "activity" ) )
        for( const auto& activity : response.at( "activity" ) )
            result.activity.push_back( { activity.at( "period" ), activity.at( "last_transition" ),
                                         activity.at( "high_value" ) } );

    for( const auto& sample : response.at( "history" ) )
        result.history.push_back( { sample.at( "time" ), sample.at( "values" ).get<std::vector<double>>() } );

    return result;
}
} // namespace CIRCUITALIVE
