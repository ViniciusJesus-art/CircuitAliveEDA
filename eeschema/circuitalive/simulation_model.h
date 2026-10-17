/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <sim/memory_image.h>

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

struct MODEL_ADJUSTMENT
{
    std::string id, label, source, parameter, unit;
    double minimum = 0, maximum = 1, step = .01;
};

struct MODEL_RESOURCE
{
    std::string id, label, kind;
    std::vector<std::string> formats;
    bool required = false;
    std::size_t maxBytes = MAX_MEMORY_BYTES;
};
struct MODEL_MEMORY
{
    std::string id, label, resource;
    unsigned addressBits = 0, wordBits = 8;
    bool writable = false;
    int fill = -1;
};

struct SIMULATION_MODEL
{
    std::filesystem::path file;
    std::string id;
    std::string name;
    std::string description;
    std::string category, family;
    std::vector<MODEL_PIN> pins;
    std::vector<MODEL_PARAMETER> parameters;
    std::vector<std::string> libraryIds, symbolNames, deviceHints;
    std::vector<std::string> partNames;
    std::map<std::string, std::vector<std::string>> pinNames;
    std::map<std::string, std::string> legacyParameterMap, legacyParameterDefaults;
    std::vector<std::string> legacyIgnoredParameters;
    bool legacyParametersOnlyIfValueInvalid = false;
    std::vector<std::string> instance, definitions;
    std::vector<std::vector<std::string>> electricalGroups;
    std::string referencePin;
    std::string indicatorSignal, brightnessParameter, ratingParameter;
    std::string controlSource;
    double controlOn = 5, controlOff = 0;
    std::vector<MODEL_ADJUSTMENT> adjustments;
    std::vector<MODEL_RESOURCE> resources;
    std::vector<MODEL_MEMORY> memories;
    bool ignoreLegacyFields = false;
    std::map<std::string, std::string> legacyFields;
    std::string measurePositive, measureNegative;
    std::string warningSignal, warningParameter;
    std::string voltageConstraint;
    std::string frequencyParameter;
    std::string maxStepParameter;
    // Mixed-signal behavior remains in the file's SPICE/XSPICE templates.
    std::string logicVcc, logicGround, logicLow, logicHigh, logicMinSupply, logicMaxSupply;
    std::map<std::string, std::string> driverEnable;
    std::map<std::string, std::string> driverValue;
    std::map<std::string, std::string> internalDrive;
};

struct RESOLVED_MODEL
{
    SIMULATION_MODEL model;
    std::map<std::string, std::string> pins; // Model port -> schematic physical pin.
    std::map<std::string, std::string> parameterText;
    std::map<std::string, double> parameters;
    std::map<std::string, std::string> driveExpressions;
    std::map<std::string, std::size_t> memorySlots;
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
    static SIMULATION_MODEL LoadText( const std::string& aJson, const std::filesystem::path& aContext = "model.casim" );

private:
    std::vector<std::filesystem::path> m_directories;
    mutable std::optional<std::vector<SIMULATION_MODEL>> m_models;
};

std::string ExpandModelTemplate( const std::string& aText, const RESOLVED_MODEL& aModel,
        const std::map<std::string, std::string>& aPhysicalNodes, std::size_t aIndex );
std::vector<MEMORY_REGION_CONFIG> InitialModelMemories( const CIRCUIT_COMPONENT&, const SIMULATION_MODEL& );
}
