/*
 * CircuitAliveEDA interactive simulation, based on KiCad.
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "memory_image.h"
#include "signal_activity.h"

namespace CIRCUITALIVE
{

enum class SIM_SESSION_STATE { STOPPED, RUNNING, PAUSED, COMPLETED, ERROR };

struct SIM_SESSION_CONFIG
{
    double                   step = 1e-5;
    double                   horizon = 60.0;
    double                   maxStep = 1e-5;
    bool                     useInitialConditions = true;
    std::size_t              historyCapacity = 256;
    std::vector<std::string> signals;
    std::vector<MEMORY_REGION_CONFIG> memories;
    std::vector<SIM_ACTIVITY_MONITOR> activityMonitors;
};

struct SIM_SAMPLE
{
    double              time = 0.0;
    std::vector<double> values;  ///< Same order as SIM_SESSION_CONFIG::signals.
};

struct SIM_SESSION_SNAPSHOT
{
    SIM_SESSION_STATE        state = SIM_SESSION_STATE::STOPPED;
    std::uint64_t            generation = 0;
    SIM_SAMPLE               latest;
    std::vector<SIM_SAMPLE>   history;
    std::vector<SIM_SIGNAL_ACTIVITY> activity;
    std::uint64_t            samplesReceived = 0;
    std::uint64_t            samplesOverwritten = 0;
    std::uint64_t            eventsApplied = 0;
    double                   lastEventTime = 0.0;
    std::size_t              engineRetainedPoints = 0; ///< Measured when paused; 0 while running.
    std::uint64_t            residentBytes = 0;
    std::string              error;
};

/**
 * Linux stage-1 controller. Each instance owns an isolated ngspice worker.
 *
 * Requests are serialized and synchronous, with a deadline. A future GUI must
 * call these methods from its control thread, then publish snapshots to wx.
 * No engine callback accesses the schematic or any GUI object.
 *
 * A worker crash/timeout raises std::runtime_error and terminates/reaps that
 * worker. Invalid requests also raise, but leave the worker available for Stop
 * and recovery. Destruction always shuts down/reaps the owned process.
 */
class INTERACTIVE_SIM_SESSION
{
public:
    explicit INTERACTIVE_SIM_SESSION(
            const std::string& aWorkerExecutable,
            std::chrono::milliseconds aTimeout = std::chrono::seconds( 5 ) );
    ~INTERACTIVE_SIM_SESSION();

    INTERACTIVE_SIM_SESSION( const INTERACTIVE_SIM_SESSION& ) = delete;
    INTERACTIVE_SIM_SESSION& operator=( const INTERACTIVE_SIM_SESSION& ) = delete;

    /// Component deck with one .end; analysis/control commands are owned by the session.
    void Configure( const std::string& aNetlist, const SIM_SESSION_CONFIG& aConfig );
    void Play();
    void Pause();
    /// Discard/reap the solver process and prepare a fresh idle worker with the original deck.
    void Stop();

    /// Run/resume to an absolute simulated time, then pause with solver state intact.
    /// The configured finite horizon is explicit; it is never silently extended/reset.
    void AdvanceTo( double aTime );

    /// Change a V/I source's DC value at the confirmed paused time, in request order.
    void SetSourceValue( const std::string& aReference, double aValue );
    MEMORY_SNAPSHOT ReadMemory( const std::string& id, std::size_t offset, std::size_t length );

    SIM_SESSION_SNAPSHOT Snapshot( bool aDrainHistory = false );
    int WorkerPid() const;

private:
    struct IMPL;
    std::unique_ptr<IMPL> m_impl;
};

} // namespace CIRCUITALIVE
