/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <json_common.h>
#include <circuitalive/model_design.h>
#include <circuitalive/model_profiles.h>
#include <sim/interactive_sim_session.h>
#include <sim/platform_runtime.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace CIRCUITALIVE;
namespace fs = std::filesystem;

namespace
{
struct TEMP_DIRECTORY
{
    fs::path path = fs::temp_directory_path() / ( "ca-json-link-" + std::to_string( ProcessId() )
        + "-" + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
    TEMP_DIRECTORY()
    {
        if( !fs::create_directory( path ) )
            throw std::runtime_error( "Cannot create isolated JSON link test directory" );
    }
    ~TEMP_DIRECTORY() { std::error_code error; fs::remove_all( path, error ); }
};

void Require( bool condition, const char* message )
{
    if( !condition ) throw std::runtime_error( message );
}
}

int main()
{
    try
    {
        TEMP_DIRECTORY temporary;
        std::ifstream input( CIRCUITALIVE_JSON_LINK_MODEL );
        Require( input.good(), "Cannot read test model" );
        const std::string text( std::istreambuf_iterator<char>( input ), {} );
        const auto model = MODEL_REPOSITORY::LoadText( text );
        const auto design = MODEL_DESIGN::Open( text );
        Require( MODEL_REPOSITORY::LoadText( design.Export() ).id == model.id, "Model round trip failed" );

        CIRCUIT_COMPONENT component;
        component.libraryId = "Device:R";
        component.modelFile = CIRCUITALIVE_JSON_LINK_MODEL;
        component.modelParameters["resistance"] = "2k";
        MODEL_PROFILES profiles( temporary.path / "profiles.json" );
        profiles.Save( component );
        CIRCUIT_COMPONENT restored;
        restored.libraryId = component.libraryId;
        Require( profiles.Apply( restored ) && restored.modelParameters == component.modelParameters,
                 "Profile round trip failed" );

        auto memory = EmptyMemory( 4, -1 );
        memory.data[0] = 42;
        memory.known[0] = 255;
        const auto file = temporary.path / "memory.camem";
        SaveMemoryImage( memory, file, "camem" );
        auto loaded = EmptyMemory( 4, 0 );
        LoadMemoryImage( loaded, file, "camem" );
        Require( loaded.data == memory.data && loaded.known == memory.known, "Memory round trip failed" );

        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        SIM_SESSION_CONFIG config;
        config.step = config.maxStep = 1e-6;
        config.horizon = .01;
        config.signals = { "v(out)" };
        session.Configure( "Desktop JSON link test\nV1 out 0 1\nR1 out 0 1k\n.end\n", config );
        session.AdvanceTo( .001 );
        const auto snapshot = session.Snapshot();
        Require( snapshot.latest.values.size() == 1 && snapshot.latest.values[0] > .99,
                 "Desktop session protocol failed" );

        // Force imported basic_json methods into the same link as all CA JSON users.
        const nlohmann::json document = { { "model", model.id }, { "voltage", snapshot.latest.values[0] } };
        Require( nlohmann::json::parse( document.dump() ).at( "model" ) == model.id, "Imported JSON failed" );
        std::cout << "Desktop JSON imports, models, profiles, memory and worker protocol passed.\n";
        return 0;
    }
    catch( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
