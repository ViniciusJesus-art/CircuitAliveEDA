/*
 * CircuitAliveEDA programmable device runtime.
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "programmable_device.h"
#include <memory>

namespace CIRCUITALIVE
{

/** Isolated script contexts: no filesystem, sockets, native imports or wall-clock API.
 * Public methods must be called sequentially. An internal bounded executor can
 * advance different contexts concurrently; a context has only one active owner.
 * Ownership can move between threads while the context is idle. Errors throw std::runtime_error.
 * Each call has bounded script execution, memory use and output count.
 */
class PROGRAMMABLE_RUNTIME
{
public:
    PROGRAMMABLE_RUNTIME();
    ~PROGRAMMABLE_RUNTIME();
    PROGRAMMABLE_RUNTIME( const PROGRAMMABLE_RUNTIME& ) = delete;
    PROGRAMMABLE_RUNTIME& operator=( const PROGRAMMABLE_RUNTIME& ) = delete;

    /// aMaxWorkers includes the calling thread: 0 selects the CPU-dependent
    /// bound, 1 keeps the same coordinator entirely sequential (diagnostics).
    void Configure( const std::vector<PROGRAMMABLE_DEVICE_CONFIG>& aDevices,
                    unsigned aMaxWorkers = 0 );
    /// Restore original images and peripheral state; simulated time returns to zero.
    void Reset();
    /// Inputs apply at the previously confirmed runtime time. Events retain backend timestamps.
    /// Cooperative configurations may yield earlier; Time() reports the actual boundary.
    /// Peer windows can be computed ahead, but States()/returned events never
    /// expose times after Time(). Requests must respect the user's batch limit.
    std::vector<PROGRAMMABLE_PIN_STATE> AdvanceTo(
            double aTime, const std::vector<PROGRAMMABLE_PIN_INPUT>& aInputs = {} );
    std::vector<PROGRAMMABLE_PIN_STATE> States() const;
    double NextEventTime() const;
    double Time() const;
    /// False only when the cooperative backend guarantees no input observation
    /// before its next barrier. Enables bounded, causal CPU prefetch.
    bool NeedsInputs() const;

    struct STATISTICS
    {
        unsigned workerThreads = 0;
        std::uint64_t windows = 0, parallelBatches = 0;
    };
    STATISTICS Statistics() const;

private:
    struct IMPL;
    std::unique_ptr<IMPL> m_impl;
};

} // namespace CIRCUITALIVE
