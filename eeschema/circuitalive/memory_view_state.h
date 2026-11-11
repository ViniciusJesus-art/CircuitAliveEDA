/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sim/memory_image.h>
#include <memory>
#include <cstdint>

namespace CIRCUITALIVE
{
/// Manual refreshes in the same pause keep the previous pause as their baseline.
class MEMORY_PAUSE_COMPARISON
{
public:
    void Accept( std::shared_ptr<const MEMORY_SNAPSHOT> snapshot, std::uint64_t revision, std::uint64_t pause,
                 std::shared_ptr<const MEMORY_SNAPSHOT> initial = {} )
    {
        if( revision != m_revision ) { m_current.reset(); m_baseline.reset(); }
        if( !m_current ) m_baseline = std::move( initial );
        else if( pause != m_pause ) m_baseline = m_current;
        m_revision = revision;
        m_pause = pause;
        m_current = std::move( snapshot );
        m_changed.assign( m_current->image.data.size(), false );
        if( !m_baseline ) return;
        for( std::size_t i = 0; i < m_changed.size(); ++i )
        {
            const auto& a = m_baseline->image;
            const auto& b = m_current->image;
            m_changed[i] = i >= a.data.size() || a.known[i] != b.known[i]
                || ( ( a.data[i] ^ b.data[i] ) & a.known[i] & b.known[i] ) != 0;
        }
    }
    const auto& Snapshot() const { return m_current; }
    const auto& Changed() const { return m_changed; }
private:
    std::uint64_t m_revision = 0, m_pause = 0;
    std::shared_ptr<const MEMORY_SNAPSHOT> m_current, m_baseline;
    std::vector<bool> m_changed;
};
}
