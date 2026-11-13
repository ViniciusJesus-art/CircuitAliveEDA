/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sim/interactive_sim_session.h>
#include <set>

namespace CIRCUITALIVE
{
struct ANALOG_PARTITION
{
    std::string netlist;
    SIM_SESSION_CONFIG config;
    std::vector<std::size_t> signals, activities;
    std::set<std::string> elements, models, memories;
};

/// Conservative, compiler-certified decomposition. Unsupported syntax returns the original deck.
/// Fixed grounded DC sources may be replicated; branch-current observations add their contributions.
std::vector<ANALOG_PARTITION> BuildAnalogPartitions( const std::string& netlist,
        const SIM_SESSION_CONFIG& config, unsigned maximum = 0 );

/// Persistent isolated solvers. All advances finish at a common public time before publication.
class ANALOG_SIM_SESSION
{
public:
    explicit ANALOG_SIM_SESSION( std::string worker,
            std::chrono::milliseconds timeout = std::chrono::seconds( 5 ) );
    ~ANALOG_SIM_SESSION();
    void Configure( const std::string& netlist, const SIM_SESSION_CONFIG& config,
                    bool partition = true, unsigned maximum = 0 );
    void AdvanceTo( double time );
    void SetSourceValue( const std::string& reference, double value,
                        const std::vector<SIM_PARAMETER_CHANGE>& changes = {} );
    MEMORY_SNAPSHOT ReadMemory( const std::string& id, std::size_t offset, std::size_t length );
    SIM_SESSION_SNAPSHOT Snapshot( bool drain = false );
    std::size_t PartitionCount() const;
private:
    struct IMPL;
    std::unique_ptr<IMPL> m_impl;
};
}
