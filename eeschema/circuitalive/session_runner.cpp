/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "session_runner.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace CIRCUITALIVE
{
std::string FindSessionWorker( const std::string& aApplicationExecutable )
{
    namespace fs = std::filesystem;
    const auto directory = fs::path( aApplicationExecutable ).parent_path();
    const fs::path candidates[] = {
        directory / "circuitalive-sim-worker",
        directory / "sim/circuitalive-sim-worker",
        directory.parent_path() / "eeschema/sim/circuitalive-sim-worker"
    };
    for( const auto& candidate : candidates )
    {
        std::error_code error;
        if( fs::is_regular_file( candidate, error ) && access( candidate.c_str(), X_OK ) == 0 )
            return candidate.string();
    }
    // Keep startup usable if the install is incomplete; the control thread reports a Play error.
    return candidates[0].string();
}

SESSION_RUNNER::SESSION_RUNNER( std::string aWorker ) : m_worker( std::move( aWorker ) ),
        m_thread( &SESSION_RUNNER::run, this ) {}

SESSION_RUNNER::~SESSION_RUNNER()
{
    { std::lock_guard lock( m_mutex ); m_quit = true; }
    m_wake.notify_one();
    m_thread.join();
}

void SESSION_RUNNER::Start( CIRCUIT_SNAPSHOT aCircuit )
{
    std::lock_guard lock( m_mutex );
    ++m_revision;
    m_commands.clear();
    m_commands.push_back( { COMMAND_TYPE::START, m_revision, std::move( aCircuit ) } );
    m_view = {};
    m_view.state = EDITOR_SESSION_STATE::PREPARING;
    m_wake.notify_one();
}

void SESSION_RUNNER::enqueue( COMMAND_TYPE aType, std::size_t aIndex )
{
    std::lock_guard lock( m_mutex );
    if( aType == COMMAND_TYPE::STOP )
    {
        ++m_revision;
        m_commands.clear();
        m_view = {};
    }
    // Bound input pressure; Stop always bypasses the limit and supersedes pending input.
    if( m_commands.size() >= 128 )
        return;
    m_commands.push_back( { aType, m_revision, {}, aIndex } );
    m_wake.notify_one();
}

void SESSION_RUNNER::Resume() { enqueue( COMMAND_TYPE::RESUME ); }
void SESSION_RUNNER::Pause() { enqueue( COMMAND_TYPE::PAUSE ); }
void SESSION_RUNNER::Stop() { enqueue( COMMAND_TYPE::STOP ); }
void SESSION_RUNNER::ToggleSwitch( std::size_t aIndex ) { enqueue( COMMAND_TYPE::TOGGLE, aIndex ); }

EDITOR_SESSION_VIEW SESSION_RUNNER::View() const
{
    std::lock_guard lock( m_mutex );
    return m_view;
}

void SESSION_RUNNER::run()
{
    std::unique_ptr<INTERACTIVE_SIM_SESSION> session;
    CIRCUIT_SNAPSHOT circuit;
    EDITOR_SESSION_VIEW view;
    std::uint64_t revision = 0;
    bool running = false;
    auto next = std::chrono::steady_clock::now();

    for( ;; )
    {
        std::deque<COMMAND> commands;
        {
            std::unique_lock lock( m_mutex );
            if( running )
                m_wake.wait_until( lock, next, [&] { return m_quit || !m_commands.empty(); } );
            else
                m_wake.wait( lock, [&] { return m_quit || !m_commands.empty(); } );
            if( m_quit )
                break;
            commands.swap( m_commands );
        }
        try
        {
            for( auto& command : commands )
            {
                revision = command.revision;
                switch( command.type )
                {
                case COMMAND_TYPE::START:
                    session.reset();
                    circuit = std::move( command.circuit );
                    session = std::make_unique<INTERACTIVE_SIM_SESSION>( m_worker );
                    session->Configure( circuit.netlist, circuit.config );
                    session->AdvanceTo( circuit.config.step );
                    view = {};
                    view.sample = session->Snapshot( true );
                    view.switches.assign( circuit.switches.size(), true );
                    running = true;
                    next = std::chrono::steady_clock::now();
                    break;
                case COMMAND_TYPE::RESUME:
                    if( session && view.sample.state != SIM_SESSION_STATE::COMPLETED )
                        running = true;
                    next = std::chrono::steady_clock::now();
                    break;
                case COMMAND_TYPE::PAUSE:
                    running = false; // AdvanceTo has already confirmed a quiescent solver.
                    if( session && view.sample.state != SIM_SESSION_STATE::COMPLETED )
                        view.state = EDITOR_SESSION_STATE::PAUSED;
                    break;
                case COMMAND_TYPE::STOP:
                    session.reset();
                    view = {};
                    running = false;
                    break;
                case COMMAND_TYPE::TOGGLE:
                    if( session && command.index < view.switches.size()
                        && view.state != EDITOR_SESSION_STATE::COMPLETED )
                    {
                        bool closed = !view.switches[command.index];
                        session->SetSourceValue( circuit.switches[command.index].controlSource,
                                                 closed ? 5.0 : 0.0 );
                        view.switches[command.index] = closed;
                        view.sample = session->Snapshot( true );
                    }
                    break;
                }
            }
            if( session && running && std::chrono::steady_clock::now() >= next )
            {
                session->AdvanceTo( std::min( view.sample.latest.time + 0.02,
                                             circuit.config.horizon ) );
                view.sample = session->Snapshot( true );
                if( view.sample.state == SIM_SESSION_STATE::ERROR )
                    throw std::runtime_error( view.sample.error );
                running = view.sample.state != SIM_SESSION_STATE::COMPLETED;
                next = std::chrono::steady_clock::now() + std::chrono::milliseconds( 20 );
            }
            if( session )
            {
                view.state = view.sample.state == SIM_SESSION_STATE::COMPLETED
                        ? EDITOR_SESSION_STATE::COMPLETED
                        : running ? EDITOR_SESSION_STATE::RUNNING : EDITOR_SESSION_STATE::PAUSED;
                view.ledCurrents.clear();
                for( const auto& led : circuit.leds )
                {
                    double sum = 0;
                    for( const auto& sample : view.sample.history )
                        sum += std::max( 0.0, sample.values.at( led.signal ) );
                    view.ledCurrents.push_back( view.sample.history.empty()
                            ? view.sample.latest.values.at( led.signal )
                            : sum / view.sample.history.size() );
                }
                view.sample.history.clear(); // GUI only receives the reduced display values.
            }
        }
        catch( const std::exception& e )
        {
            running = false;
            session.reset();
            view.state = EDITOR_SESSION_STATE::ERROR;
            view.error = e.what();
        }
        {
            std::lock_guard lock( m_mutex );
            if( revision == m_revision )
                m_view = view;
        }
    }
}
}
