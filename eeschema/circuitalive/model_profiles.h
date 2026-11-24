/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <filesystem>
#include "circuit_snapshot.h"
#include "simulation_model.h"
namespace CIRCUITALIVE
{
/// Legacy snapshots and explicit bindings to the live model file.
class MODEL_PROFILES
{
public:
    static std::filesystem::path DefaultFile();
    explicit MODEL_PROFILES( std::filesystem::path aFile = DefaultFile() ) : m_file( std::move( aFile ) ) {}
    // Explicit defaults refer to the live model, rather than cloning its parameter values.
    void MakeDefault( const CIRCUIT_COMPONENT&, const SIMULATION_MODEL&, bool aExcluded = false ) const;
    std::optional<std::string> BoundModel( const std::string& aLibraryId ) const;
    bool DefaultExcluded( const std::string& aLibraryId ) const;
    bool Apply( CIRCUIT_COMPONENT& aComponent ) const;
    void Save( const CIRCUIT_COMPONENT& aComponent ) const;
    // Keep per-symbol parameter/measurement defaults when updating this model's links.
    void UpdateLinks( const std::filesystem::path& aModel, const std::string& aModelId,
                      const std::map<std::string,std::map<std::string,std::string>>& aPins,
                      const std::vector<std::string>& aRemoved, bool aReplaceOtherDefaults ) const;
    void Remove( const std::string& aLibraryId ) const;
private:
    std::filesystem::path m_file;
};
}
