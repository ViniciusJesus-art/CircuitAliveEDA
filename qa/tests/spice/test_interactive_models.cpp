/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <circuitalive/model_profiles.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace CIRCUITALIVE;
namespace fs = std::filesystem;

struct MODEL_FILES
{
    fs::path directory = fs::temp_directory_path() / ( "circuitalive-models-" + std::to_string( getpid() ) );
    MODEL_FILES() { fs::create_directories( directory ); }
    ~MODEL_FILES() { std::error_code error; fs::remove_all( directory, error ); }
    fs::path Write( const std::string& name, const std::string& text )
    {
        auto path = directory / name;
        std::ofstream stream( path ); stream << text; stream.close();
        return path;
    }
};

static const char* dividerModel = R"MODEL({
  "schema_version":1,"backend":"ngspice","id":"divider","name":"User three-pin divider",
  "pins":[
    {"id":"in","label":"Input","role":"input","default_pin":"10"},
    {"id":"out","label":"Output","role":"output","default_pin":"20"},
    {"id":"gnd","label":"Return","role":"reference","default_pin":"30"}
  ],
  "parameters":[{"id":"ratio","label":"Upper resistance ratio","default":"1","min":0.01,"max":100}],
  "spice":{
    "instance":["X${id} ${pin:in} ${pin:out} ${pin:gnd} divider${id}"],
    "definitions":[".subckt divider${id} in out gnd","Rupper in out {1000*${param:ratio}}",
                   "Rlower out gnd 1000",".ends divider${id}"]
  }
})MODEL";

static std::vector<CIRCUIT_COMPONENT> Divider( const fs::path& directory )
{
    std::vector<CIRCUIT_COMPONENT> c = {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "return" } } },
        { "U1", "MyLibrary:NeverCompiledIntoCircuitAlive", "Divider",
          { { "10", "supply" }, { "20", "output" }, { "30", "return" } } }
    };
    c[1].modelFile = "divider.casim";
    c[1].projectDirectory = directory.string();
    return c;
}

static double Output( const std::vector<CIRCUIT_COMPONENT>& components )
{
    auto circuit = BuildCircuitSnapshot( components );
    circuit.config.signals = { "v(" + circuit.pinNodes[1].at( "20" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( 0.02 );
    return session.Snapshot().latest.values.at( 0 );
}

BOOST_FIXTURE_TEST_CASE( UserModelWithThreePinsAndSubcircuitNeedsNoCatalogueEntry, MODEL_FILES )
{
    Write( "divider.casim", dividerModel );
    auto components = Divider( directory );
    BOOST_CHECK_CLOSE( Output( components ), 2.5, 1e-5 );
    components[1].modelParameters["ratio"] = "3";
    BOOST_CHECK_CLOSE( Output( components ), 1.25, 1e-5 );
    // Explicit physical mapping can use arbitrary pin numbers from another drawing.
    components[1].pins = { { "20", "output" }, { "A", "supply" }, { "G", "return" } };
    components[1].modelPins = { { "in", "A" }, { "out", "20" }, { "gnd", "G" } };
    BOOST_CHECK_CLOSE( Output( components ), 1.25, 1e-5 );
}

BOOST_FIXTURE_TEST_CASE( EditingModelFileTakesEffectOnNextSnapshotWithoutRecompilation, MODEL_FILES )
{
    Write( "divider.casim", dividerModel );
    const auto components = Divider( directory );
    const auto first = BuildCircuitSnapshot( components );
    BOOST_CHECK_CLOSE( Output( components ), 2.5, 1e-5 );
    std::string edited = dividerModel;
    const auto position = edited.find( "\"default\":\"1\"" );
    BOOST_REQUIRE( position != std::string::npos );
    edited.replace( position, std::string( "\"default\":\"1\"" ).size(), "\"default\":\"3\"" );
    Write( "divider.casim", edited );
    BOOST_CHECK_CLOSE( Output( components ), 1.25, 1e-5 );
    BOOST_CHECK_NE( first.netlist, BuildCircuitSnapshot( components ).netlist );
}

BOOST_FIXTURE_TEST_CASE( ModelFilesValidateSchemaParametersAndPinMapping, MODEL_FILES )
{
    Write( "divider.casim", dividerModel );
    auto components = Divider( directory );
    components[1].modelPins["out"] = "10";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Divider( directory );
    components[1].modelParameters["ratio"] = "0";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components[1].modelParameters["ratio"] = "1\n.control\nquit";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Divider( directory );
    components[1].modelParameters["typo"] = "1";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    components = Divider( directory );
    components[1].pins["99"] = "";
    BOOST_CHECK_THROW( BuildCircuitSnapshot( components ), std::runtime_error );
    Write( "divider.casim", "{\"schema_version\":99,\"backend\":\"digital\"}" );
    BOOST_CHECK_THROW( BuildCircuitSnapshot( Divider( directory ) ), std::runtime_error );
    Write( "divider.casim", "invalid json" );
    BOOST_CHECK_THROW( MODEL_REPOSITORY::Load( directory / "divider.casim" ), std::runtime_error );
}

BOOST_FIXTURE_TEST_CASE( AutomaticAssociationsComeFromModelFilesAndAmbiguityIsExplicit, MODEL_FILES )
{
    std::string text = dividerModel;
    text.insert( 1, "\"matches\":{\"library_ids\":[\"Custom:Divider\"]}," );
    Write( "divider.casim", text );
    auto components = Divider( directory );
    auto component = components[1]; component.modelFile.clear(); component.libraryId = "Custom:Divider";
    MODEL_REPOSITORY repository( { directory } );
    BOOST_CHECK_EQUAL( repository.Resolve( component ).model.id, "divider" );
    std::string other = text;
    auto pos = other.find( "\"id\":\"divider\"" );
    other.replace( pos, std::string( "\"id\":\"divider\"" ).size(), "\"id\":\"other\"" );
    Write( "other.casim", other );
    BOOST_CHECK_THROW( MODEL_REPOSITORY( { directory } ).Resolve( component ), std::runtime_error );
    component.modelFile = "divider.casim";
    BOOST_CHECK_EQUAL( MODEL_REPOSITORY( { directory } ).Resolve( component ).model.id, "divider" );
}

BOOST_AUTO_TEST_CASE( LedParametersArePerInstanceAndCurrentIsCalculatedByTheCircuit )
{
    std::vector<CIRCUIT_COMPONENT> components = {
        { "V1", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "0" } } },
        { "R1", "Device:R", "470R", { { "1", "supply" }, { "2", "a" } } },
        { "D1", "Device:LED", "LED", { { "1", "0" }, { "2", "a" } } },
        { "R2", "Device:R", "470R", { { "1", "supply" }, { "2", "b" } } },
        { "D2", "Device:LED", "LED", { { "1", "0" }, { "2", "b" } } }
    };
    components[2].modelParameters = { { "forward_voltage", "2V" }, { "reference_current", "20mA" },
        { "brightness_current", "5mA" }, { "rated_current", "1mA" } };
    components[4].modelParameters = { { "forward_voltage", "3.2V" }, { "reference_current", "20mA" } };
    auto circuit = BuildCircuitSnapshot( components );
    BOOST_CHECK_CLOSE( circuit.leds[0].brightnessCurrent, .005, 1e-8 );
    BOOST_CHECK_CLOSE( circuit.leds[0].ratedCurrent, .001, 1e-8 );
    BOOST_CHECK_CLOSE( circuit.leds[1].brightnessCurrent, .01, 1e-8 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( .02 );
    const auto currents = session.Snapshot().latest.values;
    BOOST_CHECK_GT( currents.at( 0 ), currents.at( 1 ) * 1.4 );
    // The rating warns; it never imposes a hidden current clamp.
    BOOST_CHECK_GT( currents[0], circuit.leds[0].ratedCurrent );
    BOOST_CHECK_LT( currents[0], .02 );
    BOOST_CHECK_EQUAL( LedBrightness( .005, .005 ), 1 );
    components[4].modelPins = { { "a", "1" }, { "k", "2" } };
    circuit = BuildCircuitSnapshot( components );
    session.Stop();
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( .02 );
    BOOST_CHECK_LE( session.Snapshot().latest.values.at( 1 ), 0 );
}

BOOST_AUTO_TEST_CASE( LedForwardVoltageIsCalibratedAtItsConfiguredReferenceCurrent )
{
    for( const auto& value : { "1.2", "2.194", "3.2", "3.6", "4", "6" } )
    {
        std::vector<CIRCUIT_COMPONENT> components = {
            { "V1", "Simulation_SPICE:VDC", value, { { "1", "a" }, { "2", "0" } } },
            { "D1", "Device:LED", "LED", { { "1", "0" }, { "2", "a" } } }
        };
        components[1].modelParameters = { { "forward_voltage", value }, { "reference_current", "20mA" } };
        auto circuit = BuildCircuitSnapshot( components );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        session.AdvanceTo( .02 );
        BOOST_TEST_CONTEXT( "Forward voltage " << value )
            BOOST_CHECK_CLOSE( session.Snapshot().latest.values.at( 0 ), .02, .1 );
    }
}

BOOST_FIXTURE_TEST_CASE( SavedProfilesCopyDefaultsIntoNewSymbolsWithoutChangingExistingInstances, MODEL_FILES )
{
    Write( "divider.casim", dividerModel );
    auto configured = Divider( directory )[1];
    configured.modelPins = { { "in", "10" }, { "out", "20" }, { "gnd", "30" } };
    configured.modelParameters = { { "ratio", "3" } };
    configured.measurements = { true, true, false, "in", "gnd" };
    const auto file = directory / "profiles.json";
    MODEL_PROFILES( file ).Save( configured );
    auto fresh = configured; fresh.modelFile.clear(); fresh.modelPins.clear(); fresh.modelParameters.clear();
    fresh.reference = "U99"; fresh.value = "Different value"; fresh.projectDirectory = "/tmp/other-project";
    BOOST_REQUIRE( MODEL_PROFILES( file ).Apply( fresh ) );
    BOOST_CHECK_EQUAL( fresh.modelFile, ( directory / "divider.casim" ).string() );
    BOOST_CHECK_EQUAL( fresh.modelParameters.at( "ratio" ), "3" );
    BOOST_CHECK_EQUAL( fresh.reference, "U99" ); BOOST_CHECK_EQUAL( fresh.value, "Different value" );
    BOOST_CHECK( fresh.measurements.current );
    configured.modelParameters["ratio"] = "2";
    MODEL_PROFILES( file ).Save( configured );
    BOOST_CHECK( !MODEL_PROFILES( file ).Apply( fresh ) );
    BOOST_CHECK_EQUAL( fresh.modelParameters.at( "ratio" ), "3" );
    fresh.modelFile.clear(); fresh.modelPins.clear(); fresh.modelParameters.clear(); fresh.libraryId = "Other:NeverCompiledIntoCode";
    BOOST_CHECK( !MODEL_PROFILES( file ).Apply( fresh ) );
    MODEL_PROFILES( file ).Remove( configured.libraryId );
    fresh.libraryId = configured.libraryId;
    BOOST_CHECK( !MODEL_PROFILES( file ).Apply( fresh ) );
}

BOOST_FIXTURE_TEST_CASE( SavedProfilesKeepValueExpressionsAndRejectCorruptStorage, MODEL_FILES )
{
    auto c = Divider( directory )[1]; c.modelFile = "builtin:capacitor";
    c.modelParameters = { { "capacitance", "$value" } };
    auto file = directory / "profiles.json";
    MODEL_PROFILES( file ).Save( c );
    auto fresh = c; fresh.modelFile.clear(); fresh.modelParameters.clear(); fresh.value = "100n";
    BOOST_REQUIRE( MODEL_PROFILES( file ).Apply( fresh ) );
    BOOST_CHECK_EQUAL( fresh.modelParameters.at( "capacitance" ), "$value" );
    Write( "profiles.json", "{\"schema_version\":99}" );
    BOOST_CHECK_THROW( MODEL_PROFILES( file ).Save( c ), std::exception );
    fresh.modelFile.clear(); fresh.modelParameters.clear();
    BOOST_CHECK_THROW( MODEL_PROFILES( file ).Apply( fresh ), std::exception );
    BOOST_CHECK( fresh.modelFile.empty() );
}

BOOST_FIXTURE_TEST_CASE( FileDefinedStateBlocksAreReusableAndRejectUnknownSignals, MODEL_FILES )
{
    CIRCUIT_COMPONENT u{ "U1", "74xx:74HC74", "74HC74", {} };
    auto original = MODEL_REPOSITORY().SelectModel( u );
    std::ifstream in( original.file ); std::string text( ( std::istreambuf_iterator<char>( in ) ), {} );
    auto replace = []( std::string& s, const std::string& from, const std::string& to )
    {
        std::size_t pos = 0;
        while( ( pos = s.find( from, pos ) ) != std::string::npos ) { s.replace( pos, from.size(), to ); pos += to.size(); }
    };
    for( const auto& p : original.pins ) u.pins[p.defaultPin] = "";
    u.pins["14"] = "supply"; u.pins["7"] = "0";
    u.pins["1"] = u.pins["4"] = u.pins["10"] = u.pins["13"] = "supply";
    u.pins["2"] = "set"; u.pins["3"] = "reset";
    replace( text, "\"type\": \"dff\"", "\"type\": \"sr_latch\", \"enable\": \"1\"" );
    replace( text, "\"data\":", "\"s\":" ); replace( text, "\"clock\":", "\"r\":" );
    u.modelFile = Write( "latches.casim", text ).string();
    auto c = BuildCircuitSnapshot( {
        { "VCC", "Simulation_SPICE:VDC", "5", { { "1", "supply" }, { "2", "0" } } },
        { "VS", "Simulation_SPICE:VDC", "5", { { "1", "set" }, { "2", "0" } } },
        { "VR", "Simulation_SPICE:VDC", "0", { { "1", "reset" }, { "2", "0" } } }, u } );
    c.config.signals = { "v(" + c.pinNodes[3].at( "5" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
    session.AdvanceTo( 1e-6 ); BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.9 );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( 2e-6 ); BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.9 );
    session.SetSourceValue( "Vca3", 5 ); session.AdvanceTo( 3e-6 ); BOOST_CHECK_LT( session.Snapshot().latest.values[0], .01 );
    replace( text, "\"sr_latch\"", "\"unimplemented_cpu\"" );
    BOOST_CHECK_THROW( MODEL_REPOSITORY::Load( Write( "bad.casim", text ) ), std::runtime_error );
    replace( text, "\"unimplemented_cpu\"", "\"sr_latch\"" );
    replace( text, "\"enable\": \"1\"", "\"enable\": \"undefined_net\"" );
    BOOST_CHECK_THROW( MODEL_REPOSITORY::Load( Write( "bad.casim", text ) ), std::runtime_error );
}
