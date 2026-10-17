/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "memory_image.h"
#include "memory_backend.h"
#include <array>
#include <map>
#include <memory>
#include <set>

namespace CIRCUITALIVE
{
// Solver-thread writes; the controller only reads after ngspice confirms a pause.
// Immutable pages plus version tokens participate in XSPICE event rollback.
class RUNTIME_MEMORY
{
  public:
    void Reset( const std::vector<MEMORY_REGION_CONFIG>& regions );
    CA_MEMORY_BACKEND Backend();
    void Commit( double time );
    MEMORY_IMAGE Read( const std::string& id, std::size_t offset, std::size_t length ) const;
    const std::string& Error() const
    {
        return m_error;
    }
    std::size_t Versions() const;

  private:
    struct PAGE
    {
        std::array<std::uint8_t, 256> data{}, known{};
    };
    using PAGES = std::vector<std::shared_ptr<const PAGE>>;
    struct REGION
    {
        MEMORY_REGION_CONFIG config;
        std::map<std::uint64_t, PAGES> versions;
        std::vector<std::pair<double, std::uint64_t>> pending;
        std::uint64_t next = 1, current = 0, previous = 0;
    };
    std::vector<REGION> m_regions;
    std::string m_error;
    std::uint32_t read( unsigned slot, std::uint64_t token, unsigned address, std::uint32_t* known );
    std::uint64_t write( unsigned slot, std::uint64_t token, double time, unsigned address, std::uint32_t data,
                         std::uint32_t known, bool invalidate );
    void mark( unsigned slot, std::uint64_t token, double time );
};
} // namespace CIRCUITALIVE
