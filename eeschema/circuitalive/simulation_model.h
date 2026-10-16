/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace CIRCUITALIVE
{
struct CIRCUIT_COMPONENT;

struct MODEL_PIN
{
    std::string id;
    std::string label;
    std::string role;
    std::string defaultPin;
};

struct MODEL_PARAMETER
{
    std::string id;
    std::string label;
    std::string unit;
    std::string defaultValue; // "$value" follows the schematic Value field.
    double minimum;
    double maximum;
};

struct SIMULATION_MODEL
{
    std::filesystem::path file;
    std::string id;
    std::string name;
    std::string description;
    std::vector<MODEL_PIN> pins;
    std::vector<MODEL_PARAMETER> parameters;
    std::vector<std::string> libraryIds, symbolNames, deviceHints;
    std::vector<std::string> instance, definitions;
    std::vector<std::vector<std::string>> electricalGroups;
    std::string referencePin;
    std::string indicatorSignal, brightnessParameter, ratingParameter;
    std::string controlSource;
    double controlOn = 5, controlOff = 0;
    bool ignoreLegacyFields = false;
    std::map<std::string, std::string> legacyFields;
};

struct RESOLVED_MODEL
{
    SIMULATION_MODEL model;
    std::map<std::string, std::string> pins; // Model port -> schematic physical pin.
    std::map<std::string, std::string> parameterText;
    std::map<std::string, double> parameters;
};

/// Model data is read afresh for each Play; no schematic/library pointers or code loading.
class MODEL_REPOSITORY
{
public:
    static std::vector<std::filesystem::path> DefaultDirectories( const std::string& aProjectDirectory = {} );
    explicit MODEL_REPOSITORY( std::vector<std::filesystem::path> aDirectories = DefaultDirectories() );
    const std::vector<SIMULATION_MODEL>& List() const;
    SIMULATION_MODEL SelectModel( const CIRCUIT_COMPONENT& aComponent ) const;
    RESOLVED_MODEL Resolve( const CIRCUIT_COMPONENT& aComponent ) const;
    static SIMULATION_MODEL Load( const std::filesystem::path& aFile );

private:
    std::vector<std::filesystem::path> m_directories;
    mutable std::optional<std::vector<SIMULATION_MODEL>> m_models;
};

std::string ExpandModelTemplate( const std::string& aText, const RESOLVED_MODEL& aModel,
        const std::map<std::string, std::string>& aPhysicalNodes, std::size_t aIndex );
}
