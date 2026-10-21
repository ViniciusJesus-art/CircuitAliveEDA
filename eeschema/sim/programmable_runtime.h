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
 * Methods must be called sequentially; ownership can move between the control and
 * solver threads while neither is executing the context. Errors throw std::runtime_error.
 * Each call has bounded script execution, memory use and output count.
 */
class PROGRAMMABLE_RUNTIME
{
public:
    PROGRAMMABLE_RUNTIME();
    ~PROGRAMMABLE_RUNTIME();
    PROGRAMMABLE_RUNTIME( const PROGRAMMABLE_RUNTIME& ) = delete;
    PROGRAMMABLE_RUNTIME& operator=( const PROGRAMMABLE_RUNTIME& ) = delete;

    void Configure( const std::vector<PROGRAMMABLE_DEVICE_CONFIG>& aDevices );
    /// Restore original images and peripheral state; simulated time returns to zero.
    void Reset();
    /// Inputs apply at the previously confirmed runtime time. Events retain backend timestamps.
    /// Cooperative configurations may yield earlier; Time() reports the actual boundary.
    std::vector<PROGRAMMABLE_PIN_STATE> AdvanceTo(
            double aTime, const std::vector<PROGRAMMABLE_PIN_INPUT>& aInputs = {} );
    std::vector<PROGRAMMABLE_PIN_STATE> States() const;
    double NextEventTime() const;
    double Time() const;
    /// False only when the cooperative backend guarantees no input observation
    /// before its next barrier. Enables bounded, causal CPU prefetch.
    bool NeedsInputs() const;

private:
    struct IMPL;
    std::unique_ptr<IMPL> m_impl;
};

} // namespace CIRCUITALIVE
