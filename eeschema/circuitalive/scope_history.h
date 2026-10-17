/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sim/interactive_sim_session.h>
#include <deque>
#include <functional>
#include <memory>

namespace CIRCUITALIVE
{
/// Cheap immutable snapshots: copying this history shares sample blocks, never their signal arrays.
/// Only the runner mutates its builder; published const copies are safe for GUI readers.
class SCOPE_HISTORY
{
public:
    explicit SCOPE_HISTORY( std::size_t aCapacity = 8192 ) : m_capacity( aCapacity ) {}
    void Append( std::vector<SIM_SAMPLE> aSamples );
    bool empty() const { return m_size == 0; }
    std::size_t size() const { return m_size; }
    const SIM_SAMPLE& front() const { return m_blocks.front().samples->at( m_blocks.front().first ); }
    const SIM_SAMPLE& back() const { return m_blocks.back().samples->back(); }
    void ForEachAfter( double aTime, const std::function<bool( const SIM_SAMPLE& )>& ) const;
    std::vector<SIM_SAMPLE> Copy() const; // Explicit deep copy, used for manual Auto/rearm only.
private:
    struct BLOCK { std::shared_ptr<const std::vector<SIM_SAMPLE>> samples; std::size_t first = 0; };
    std::deque<BLOCK> m_blocks;
    std::size_t m_capacity, m_size = 0;
};
}
