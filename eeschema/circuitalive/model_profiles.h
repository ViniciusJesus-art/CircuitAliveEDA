/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <filesystem>
#include "circuit_snapshot.h"
namespace CIRCUITALIVE
{
/// User defaults are copied into newly placed symbols, never consulted by a running circuit.
class MODEL_PROFILES
{
public:
    static std::filesystem::path DefaultFile();
    explicit MODEL_PROFILES( std::filesystem::path aFile = DefaultFile() ) : m_file( std::move( aFile ) ) {}
    bool Apply( CIRCUIT_COMPONENT& aComponent ) const;
    void Save( const CIRCUIT_COMPONENT& aComponent ) const;
    void Remove( const std::string& aLibraryId ) const;
private:
    std::filesystem::path m_file;
};
}
