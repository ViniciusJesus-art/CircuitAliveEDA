/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "session_runner.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
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
    m_commands.push_back( { COMMAND_TYPE::START, m_revision, std::move( aCircuit ), 0, m_speed } );
    m_view = {};
    m_view.state = EDITOR_SESSION_STATE::PREPARING;
    m_view.speed = m_speed;
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
        m_view.speed = m_speed;
    }
    // Bound input pressure; Stop always bypasses the limit and supersedes pending input.
    if( m_commands.size() >= 128 )
        return;
    m_commands.push_back( { aType, m_revision, {}, aIndex, aType == COMMAND_TYPE::STOP ? m_speed : 0 } );
    m_wake.notify_one();
}

void SESSION_RUNNER::Resume() { enqueue( COMMAND_TYPE::RESUME ); }
void SESSION_RUNNER::Pause() { enqueue( COMMAND_TYPE::PAUSE ); }
void SESSION_RUNNER::Stop() { enqueue( COMMAND_TYPE::STOP ); }
void SESSION_RUNNER::ToggleSwitch( std::size_t aIndex ) { enqueue( COMMAND_TYPE::TOGGLE, aIndex ); }
void SESSION_RUNNER::CaptureMemory(std::size_t region) { enqueue(COMMAND_TYPE::MEMORY,region); }
void SESSION_RUNNER::Step() { enqueue( COMMAND_TYPE::STEP ); }

void SESSION_RUNNER::SetSpeed( double value )
{
    if( !std::isfinite( value ) || value < .0001 || value > 100 ) return;
    std::lock_guard lock( m_mutex );
    if( !m_commands.empty() && m_commands.back().type == COMMAND_TYPE::SPEED
        && m_commands.back().revision == m_revision )
        m_commands.back().value = value;
    else if( m_commands.size() < 128 )
        m_commands.push_back( { COMMAND_TYPE::SPEED, m_revision, {}, 0, value } );
    else return;
    m_speed = value;
    m_wake.notify_one();
}

void SESSION_RUNNER::SetAdjustment( std::size_t index, double value )
{
    if( !std::isfinite( value ) ) return;
    std::lock_guard lock( m_mutex );
    // Slider motion can arrive faster than the solver. Coalesce only consecutive
    // changes to the same control, preserving ordering around Pause/Start/Stop.
    if( !m_commands.empty() && m_commands.back().type == COMMAND_TYPE::ADJUST
        && m_commands.back().index == index && m_commands.back().revision == m_revision )
        m_commands.back().value = value;
    else if( m_commands.size() < 128 )
        m_commands.push_back( { COMMAND_TYPE::ADJUST, m_revision, {}, index, value } );
    m_wake.notify_one();
}

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
    SCOPE_HISTORY scopeHistory;
    bool hasScope = false;
    std::uint64_t revision = 0;
    bool running = false;
    double speed = 1;
    std::map<std::string, std::string> latchedWarnings;
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
                    speed = command.value;
                    session.reset();
                    circuit = std::move( command.circuit );
                    hasScope = std::any_of( circuit.measurements.begin(), circuit.measurements.end(), []( const auto& c ) { return c.scope; } );
                    scopeHistory = SCOPE_HISTORY( std::min<std::size_t>( 8192, 65536 / std::max<std::size_t>( 1, circuit.config.signals.size() ) ) );
                    session = std::make_unique<INTERACTIVE_SIM_SESSION>( m_worker );
                    session->Configure( circuit.netlist, circuit.config );
                    session->AdvanceTo( circuit.config.step );
                    view = {};
                    latchedWarnings.clear();
                    view.sample = session->Snapshot( true );
                    view.switches.assign( circuit.switches.size(), true );
                    for( const auto& control : circuit.adjustments )
                        view.adjustments.push_back( control.initial );
                    running = true;
                    next = std::chrono::steady_clock::now() + std::chrono::milliseconds( 20 );
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
                    speed = command.value;
                    session.reset();
                    view = {};
                    scopeHistory = SCOPE_HISTORY();
                    latchedWarnings.clear();
                    running = false;
                    break;
                case COMMAND_TYPE::TOGGLE:
                    if( session && command.index < view.switches.size()
                        && view.state != EDITOR_SESSION_STATE::COMPLETED )
                    {
                        bool closed = !view.switches[command.index];
                        const auto& control = circuit.switches[command.index];
                        session->SetSourceValue( control.controlSource, closed ? control.on : control.off );
                        view.switches[command.index] = closed;
                        view.sample = session->Snapshot( true );
                    }
                    break;
                case COMMAND_TYPE::ADJUST:
                    if( session && command.index < view.adjustments.size()
                        && view.sample.state != SIM_SESSION_STATE::COMPLETED )
                    {
                        const auto& control = circuit.adjustments.at( command.index );
                        const double value = std::clamp( command.value, control.minimum, control.maximum );
                        if( value != view.adjustments[command.index] )
                        {
                            session->SetSourceValue( control.source, value );
                            view.adjustments[command.index] = value;
                            view.sample = session->Snapshot( true );
                        }
                    }
                    break;
                case COMMAND_TYPE::SPEED:
                    speed = command.value;
                    break;
                case COMMAND_TYPE::MEMORY:
                    running = false;
                    view.memory.reset();
                    view.memoryError.clear();
                    ++view.memorySequence;
                    try
                    {
                        if( !session || command.index >= circuit.config.memories.size() )
                            throw std::runtime_error( "No active memory for this component" );
                        const auto& region = circuit.config.memories.at( command.index );
                        auto snapshot = std::make_shared<MEMORY_SNAPSHOT>();
                        snapshot->id = region.id;
                        const auto size = region.initial.data.size();
                        snapshot->image.data.reserve( size );
                        snapshot->image.known.reserve( size );
                        for( std::size_t offset = 0; offset < size; offset += 4096 )
                        {
                            const auto page = session->ReadMemory( region.id, offset,
                                                                   std::min<std::size_t>( 4096, size - offset ) );
                            snapshot->time = page.time;
                            snapshot->image.data.insert( snapshot->image.data.end(), page.image.data.begin(),
                                                         page.image.data.end() );
                            snapshot->image.known.insert( snapshot->image.known.end(), page.image.known.begin(),
                                                          page.image.known.end() );
                        }
                        view.memory = std::move( snapshot );
                    }
                    catch( const std::exception& e )
                    {
                        view.memoryError = e.what();
                    }
                    break;
                case COMMAND_TYPE::STEP:
                    if( session && !running && view.sample.state != SIM_SESSION_STATE::COMPLETED )
                    {
                        session->AdvanceTo( std::min( view.sample.latest.time + .02 * speed,
                                                     circuit.config.horizon ) );
                        view.sample = session->Snapshot( true );
                    }
                    break;
                }
            }
            if( session && running && std::chrono::steady_clock::now() >= next )
            {
                session->AdvanceTo( std::min( view.sample.latest.time + 0.02 * speed,
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
                view.measurements.clear();
                for( const auto& channel : circuit.measurements )
                    view.measurements.push_back( MeasurementValue( channel, view.sample.latest ) );
                view.logicLevels.clear();
                for( const auto& pin : circuit.logicPins ) view.logicLevels.push_back( PinLogicLevel( pin, view.sample.latest ) );
                auto warn = [&]( const std::string& name, double limit, std::size_t signal )
                {
                    bool exceeded = std::abs( view.sample.latest.values.at( signal ) ) > limit;
                    for( const auto& sample : view.sample.history ) exceeded |= std::abs( sample.values.at( signal ) ) > limit;
                    if( exceeded ) latchedWarnings.try_emplace( name,
                        "Overcurrent: " + name + " (> " + FormatMeasurement( limit, MEASURE_QUANTITY::CURRENT ) + ")" );
                };
                for( const auto& warning : circuit.currentWarnings ) warn( warning.name, warning.limit, warning.signal );
                for( const auto& led : circuit.leds )
                    if( led.ratedCurrent > 0 ) warn( led.reference.empty() ? "LED" : led.reference, led.ratedCurrent, led.signal );
                view.warnings.clear();
                for( const auto& [name, warning] : latchedWarnings ) view.warnings.push_back( warning );
                std::set<std::size_t> checkedSupplies;
                for( const auto& pin : circuit.logicPins )
                    if( pin.digital && checkedSupplies.insert( pin.component ).second )
                    {
                        auto v = [&]( std::size_t index ) { return index == NO_SIGNAL ? 0.0 : view.sample.latest.values.at( index ); };
                        const double supply = v( pin.vcc ) - v( pin.ground );
                        if( supply < pin.minSupply || supply > pin.maxSupply )
                            view.warnings.push_back( "Logic supply out of range: " + pin.reference );
                    }
                if( hasScope && !view.sample.history.empty() )
                {
                    scopeHistory.Append( std::move( view.sample.history ) );
                    view.scopeHistory = std::make_shared<const SCOPE_HISTORY>( scopeHistory );
                }
                view.sample.history.clear();
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
            view.speed = speed;
            if( revision == m_revision )
                m_view = view;
        }
    }
}
}
