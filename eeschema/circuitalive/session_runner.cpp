/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "session_runner.h"
#include "visual_signals.h"
#include "control_references.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <sim/platform_runtime.h>

namespace CIRCUITALIVE
{
std::string FindSessionWorker( const std::string& aApplicationExecutable )
{
    namespace fs = std::filesystem;
    const auto directory = fs::path( aApplicationExecutable ).parent_path();
    const std::string name =
#ifdef _WIN32
        "circuitalive-sim-worker.exe";
#else
        "circuitalive-sim-worker";
#endif
    const fs::path candidates[] = {
        directory / name,
        directory / "sim" / name,
        directory.parent_path() / "eeschema/sim" / name
    };
    for( const auto& candidate : candidates )
    {
        std::error_code error;
        if( fs::is_regular_file( candidate, error ) )
            return PathUtf8(candidate);
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
    m_commands.push_back( { COMMAND_TYPE::START, m_revision, std::move( aCircuit ), 0, m_speed, m_preferences } );
    m_view = {};
    m_view.state = EDITOR_SESSION_STATE::PREPARING;
    m_view.speed = m_speed;
    m_view.preferences = m_preferences;
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
        m_view.preferences = m_preferences;
    }
    // Bound input pressure; Stop always bypasses the limit and supersedes pending input.
    if( m_commands.size() >= 128 )
        return;
    m_commands.push_back( { aType, m_revision, {}, aIndex,
                            aType == COMMAND_TYPE::STOP ? m_speed : 0, m_preferences } );
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

void SESSION_RUNNER::SetPreferences( SIMULATION_PREFERENCES preferences )
{
    preferences.Normalize();
    std::lock_guard lock( m_mutex );
    if( preferences.playbackSpeed != m_preferences.playbackSpeed ) m_speed = preferences.playbackSpeed;
    m_preferences = preferences;
    // Coalesce preference edits, and capture them again on Start even when it
    // supersedes the queued update. Runtime speed selections remain independent.
    if( !m_commands.empty() && m_commands.back().type == COMMAND_TYPE::PREFERENCES )
        m_commands.back().preferences = preferences;
    else
        m_commands.push_back( { COMMAND_TYPE::PREFERENCES, m_revision, {}, 0, 0, preferences } );
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
    std::map<std::string,double> controlValues;
    EDITOR_SESSION_VIEW view;
    SCOPE_HISTORY scopeHistory;
    bool hasScope = false;
    std::uint64_t revision = 0;
    bool running = false;
    double speed = 1;
    SIMULATION_PREFERENCES preferences;
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
                    preferences = command.preferences;
                    session.reset();
                    circuit = std::move( command.circuit );
                    controlValues.clear();
                    for(const auto& update:circuit.liveUpdates)for(std::size_t n=0;n<update.sources.size();++n)
                        controlValues[update.sources[n]]=update.initials.at(n);
                    for(const auto& c:circuit.switches)controlValues[c.controlSource]=c.initiallyClosed?c.on:c.off;
                    for(const auto& c:circuit.adjustments)controlValues[c.source]=c.initial;
                    ConfigureVisualSignals( circuit );
                    hasScope = std::any_of( circuit.measurements.begin(), circuit.measurements.end(), []( const auto& c ) { return c.scope; } );
                    // Latest values drive every indicator. Historical values
                    // only serve scope traces and peak-current warnings; sending
                    // every internal MCU/pin vector in every batch overwhelms IPC.
                    {
                        std::set<std::size_t> selected;
                        auto retain = [&]( std::size_t signal )
                        {
                            if( signal != NO_SIGNAL ) selected.insert( signal );
                        };
                        for( const auto& channel : circuit.measurements ) if( channel.scope )
                        {
                            retain( channel.positive ); retain( channel.negative ); retain( channel.current );
                        }
                        for( const auto& warning : circuit.currentWarnings ) retain( warning.signal );
                        for( const auto& led : circuit.leds )
                            if( led.ratedCurrent > 0 ) retain( led.signal );
                        circuit.config.historySignals = std::vector<std::size_t>( selected.begin(), selected.end() );
                    }
                    session = std::make_unique<INTERACTIVE_SIM_SESSION>( m_worker,
                            std::chrono::milliseconds( std::lround( preferences.workerTimeoutSeconds * 1000 ) ) );
                    session->Configure( circuit.netlist, circuit.config );
                    session->AdvanceTo( circuit.config.step );
                    view = {};
                    for( const auto& channel : circuit.measurements ) if( channel.scope )
                    {
                        auto projected = channel;
                        projected.positive = view.scopeChannels.size();
                        projected.negative = projected.current = NO_SIGNAL;
                        projected.directSample = true;
                        view.scopeChannels.push_back( std::move( projected ) );
                    }
                    scopeHistory = SCOPE_HISTORY( std::min<std::size_t>( preferences.scopeHistorySamples,
                            65536 / std::max<std::size_t>( 1, view.scopeChannels.size() ) ), view.scopeChannels );
                    latchedWarnings.clear();
                    view.sample = session->Snapshot( true );
                    for( const auto& control : circuit.switches )
                        view.switches.push_back( control.initiallyClosed );
                    for( const auto& control : circuit.adjustments )
                        view.adjustments.push_back( control.initial );
                    running = true;
                    next = std::chrono::steady_clock::now() + preferences.FrameInterval();
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
                    preferences = command.preferences;
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
                        controlValues[control.controlSource]=closed?control.on:control.off;
                        session->SetSourceValue( control.controlSource, closed ? control.on : control.off,
                            LiveParameterChanges(circuit,controlValues,control.controlSource) );
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
                            controlValues[control.source]=value;
                            session->SetSourceValue( control.source, value, LiveParameterChanges(circuit,controlValues,control.source) );
                            view.adjustments[command.index] = value;
                            view.sample = session->Snapshot( true );
                        }
                    }
                    break;
                case COMMAND_TYPE::SPEED:
                    speed = command.value;
                    next = std::chrono::steady_clock::now();
                    break;
                case COMMAND_TYPE::PREFERENCES:
                    if( preferences.playbackSpeed != command.preferences.playbackSpeed )
                        speed = command.preferences.playbackSpeed;
                    preferences = command.preferences;
                    next = std::chrono::steady_clock::now() + preferences.FrameInterval();
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
                        session->AdvanceTo( std::min( view.sample.latest.time + preferences.stepMs * .001 * speed,
                                                     circuit.config.horizon ) );
                        view.sample = session->Snapshot( true );
                    }
                    break;
                }
            }
            if( session && running && std::chrono::steady_clock::now() >= next )
            {
                double advance = std::chrono::duration<double>( preferences.FrameInterval() ).count() * speed;
                if( !circuit.config.programmableDevices.empty() )
                {
                    double quantum = 1e-3;
                    for( const auto& device : circuit.config.programmableDevices )
                        quantum = std::min( quantum, device.quantumSeconds );
                    // Bound firmware work per request so input, Pause and Stop
                    // are processed between batches even on a slow computer.
                    double batch = quantum * 512 / circuit.config.programmableDevices.size();
                    if( circuit.config.programmableDevices.size() == 1 )
                        batch = std::max( batch, circuit.config.programmableDevices.front().autonomousWindowSeconds * 8 );
                    advance = std::min( advance, batch );
                }
                session->AdvanceTo( std::min( view.sample.latest.time
                                             + advance,
                                             circuit.config.horizon ) );
                view.sample = session->Snapshot( true );
                if( view.sample.state == SIM_SESSION_STATE::FAILED )
                    throw std::runtime_error( view.sample.error );
                running = view.sample.state != SIM_SESSION_STATE::COMPLETED;
                // Keep an absolute schedule: solver work, publication and OS wake
                // latency all belong inside the budget. Catch up without skipping
                // simulated time if a frame finishes late.
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>( advance / speed ) );
            }
            if( session )
            {
                view.state = view.sample.state == SIM_SESSION_STATE::COMPLETED
                        ? EDITOR_SESSION_STATE::COMPLETED
                        : running ? EDITOR_SESSION_STATE::RUNNING : EDITOR_SESSION_STATE::PAUSED;
                UpdateVisualSignals( circuit, view, speed, preferences );
                view.measurements.clear();
                for( const auto& channel : circuit.measurements )
                    view.measurements.push_back( MeasurementValue( channel, view.sample.latest ) );
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
                if( preferences.warnOvercurrent )
                    for( const auto& [name, warning] : latchedWarnings ) view.warnings.push_back( warning );
                std::set<std::size_t> checkedSupplies;
                for( const auto& pin : circuit.logicPins )
                    if( preferences.warnSupply && pin.digital && checkedSupplies.insert( pin.component ).second )
                    {
                        auto v = [&]( std::size_t index ) { return index == NO_SIGNAL ? 0.0 : view.sample.latest.values.at( index ); };
                        const double supply = v( pin.vcc ) - v( pin.ground );
                        if( supply < pin.minSupply || supply > pin.maxSupply )
                            view.warnings.push_back( "Logic supply out of range: " + pin.reference );
                    }
                if( hasScope && !view.sample.history.empty() )
                {
                    for( auto& sample : view.sample.history )
                    {
                        std::vector<double> measured;
                        measured.reserve( view.scopeChannels.size() );
                        for( const auto& channel : circuit.measurements ) if( channel.scope )
                            measured.push_back( MeasurementValue( channel, sample ) );
                        sample.values = std::move( measured );
                    }
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
            view.state = EDITOR_SESSION_STATE::FAILED;
            view.error = e.what();
        }
        {
            std::lock_guard lock( m_mutex );
            view.speed = speed;
            view.preferences = preferences;
            if( revision == m_revision )
                m_view = view;
        }
    }
}
}
