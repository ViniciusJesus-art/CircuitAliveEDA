/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/model_design.h>
#include <circuitalive/model_design_presets.h>
#include <circuitalive/circuit_snapshot.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace CIRCUITALIVE;
namespace fs = std::filesystem;
namespace
{
struct DESIGN_FILES
{
    fs::path directory = fs::temp_directory_path() / ( "ca-author-" + std::to_string( getpid() ) );
    std::map<std::string, std::string> templates;
    DESIGN_FILES()
    {
        fs::create_directories( directory );
        auto dirs = MODEL_REPOSITORY::DefaultDirectories(); const auto blocks = MODEL_DESIGN::BlockDirectories(); dirs.insert( dirs.end(), blocks.begin(), blocks.end() );
        for( const auto& dir : dirs )
        {
            std::error_code e; if( !fs::is_directory( dir, e ) ) continue;
            for( const auto& entry : fs::directory_iterator( dir ) ) if( entry.path().extension() == ".casim" )
            {
                std::ifstream in( entry.path() ); std::string text( std::istreambuf_iterator<char>( in ), {} );
                auto model = MODEL_REPOSITORY::LoadText( text ); templates.try_emplace( model.id, std::move( text ) );
            }
        }
    }
    ~DESIGN_FILES() { std::error_code e; fs::remove_all( directory, e ); }
    std::string Write( const MODEL_DESIGN& d )
    {
        auto file = directory / ( d.id + ".casim" ); std::ofstream( file ) << d.Export(); return file.string();
    }
    MODEL_DESIGN Divider()
    {
        MODEL_DESIGN d; d.id = "visual_divider"; d.name = "Visual divider"; d.libraryId = "Custom:Divider";
        d.pins = { { "in", "Input", "input", "10" }, { "out", "Output", "output", "20" }, { "gnd", "Return", "reference", "30" } };
        d.parameters = { { "upper", "Upper resistance", "Ohm", "1k", 1, 1e6 } };
        d.blocks = { { "R1", templates.at( "resistor" ), { { "p", "@in" }, { "n", "@out" } }, { { "resistance", "@upper" } }, 300, 200 },
            { "R2", templates.at( "resistor" ), { { "p", "@out" }, { "n", "@gnd" } }, { { "resistance", "1k" } }, 600, 200 } };
        return d;
    }
};
CIRCUIT_COMPONENT Source( std::string ref, std::string node, std::string value )
{
    return { ref, "Simulation_SPICE:VDC", value, { { "1", node }, { "2", "0" } } };
}
}
BOOST_FIXTURE_TEST_CASE( VisualModelRoundTripKeepsEmbeddedBlocksAndPublicParameters, DESIGN_FILES )
{
    auto d = Divider(); const auto text = d.Export(); auto restored = MODEL_DESIGN::Open( text );
    BOOST_REQUIRE_EQUAL( restored.blocks.size(), 2 ); BOOST_CHECK_EQUAL( restored.libraryId, d.libraryId );
    BOOST_CHECK_EQUAL( restored.blocks[0].x, 300 ); BOOST_CHECK_EQUAL( restored.blocks[0].connections.at( "n" ), "@out" );
    BOOST_CHECK_EQUAL( restored.blocks[0].values.at( "resistance" ), "@upper" );
    BOOST_CHECK_EQUAL( restored.Export(), text );
    auto model = MODEL_REPOSITORY::LoadText( text ); BOOST_CHECK( model.libraryIds.empty() ); // profiles bind alternatives without ambiguity
    CIRCUIT_COMPONENT u{ "U1", "Custom:Divider", "Divider", { { "10", "in" }, { "20", "out" }, { "30", "0" } } };
    u.modelFile = Write( restored ); auto u2 = u; u2.reference = "U2"; u2.pins["20"] = "out2"; u2.modelParameters["upper"] = "3k";
    auto c = BuildCircuitSnapshot( { Source( "V1", "in", "5" ), u, u2 } );
    c.config.signals = { "v(" + c.pinNodes[1].at( "20" ) + ")", "v(" + c.pinNodes[2].at( "20" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config ); session.AdvanceTo( .02 );
    auto sample = session.Snapshot().latest; BOOST_CHECK_CLOSE( sample.values[0], 2.5, 1e-5 ); BOOST_CHECK_CLOSE( sample.values[1], 1.25, 1e-5 );
}
BOOST_FIXTURE_TEST_CASE( VisualConnectionsMergeWiresAndRejectShortingExternalPorts, DESIGN_FILES )
{
    auto d = Divider(); d.blocks[0].connections.clear(); d.blocks[1].connections.clear();
    d.Connect( "R1", "n", "R2", "p" );
    BOOST_CHECK_EQUAL( d.blocks[0].connections.at( "n" ), d.blocks[1].connections.at( "p" ) );
    d.Connect( "R1", "n", "", "out" );
    BOOST_CHECK_EQUAL( d.blocks[1].connections.at( "p" ), "@out" );
    d.Connect( "R1", "p", "", "in" );
    BOOST_CHECK_THROW( d.Connect( "R1", "p", "", "gnd" ), std::runtime_error );
    BOOST_CHECK_THROW( d.Connect( "R1", "missing", "R2", "p" ), std::runtime_error );
}
BOOST_FIXTURE_TEST_CASE( VisualModelRejectsInvalidMappingsAndParameterBindings, DESIGN_FILES )
{
    auto d = Divider(); d.blocks[0].values["resistance"] = "@missing"; BOOST_CHECK_THROW( d.Export(), std::runtime_error );
    d = Divider(); d.parameters[0].defaultValue = "oops"; BOOST_CHECK_THROW( d.Export(), std::runtime_error );
    d = Divider(); d.blocks[0].connections["p"] = "@missing"; BOOST_CHECK_THROW( d.Export(), std::runtime_error );
    d = Divider(); d.blocks[1].id = "R1"; BOOST_CHECK_THROW( d.Export(), std::runtime_error );
    d = Divider(); d.pins[1].defaultPin = "10"; BOOST_CHECK_THROW( d.Export(), std::runtime_error );
    d = Divider(); d.parameters[0].minimum = 0; BOOST_CHECK_THROW( d.Export(), std::runtime_error ); // resistor minimum is positive
}
BOOST_FIXTURE_TEST_CASE( ComposedDigitalBlockKeepsFloatingInputsAndDriverState, DESIGN_FILES )
{
    MODEL_DESIGN d = MODEL_DESIGN::Open( templates.at( "block_nand" ) ); d.baseJson.clear(); d.id = "visual_nand"; d.libraryId = "Custom:Nand";
    DESIGN_BLOCK block; block.id = "NAND"; block.modelJson = templates.at( "block_nand" );
    for( const auto& p : d.pins ) block.connections[p.id] = "@" + p.id;
    d.blocks.push_back( block );
    CIRCUIT_COMPONENT u{ "U1", d.libraryId, "NAND", {} }; u.modelFile = Write( d );
    for( const auto& p : d.pins ) u.pins[p.defaultPin] = p.id == "vcc" || p.id == "a" ? "vcc" : p.id == "gnd" ? "0" : p.id == "y" ? "out" : "";
    auto c = BuildCircuitSnapshot( { Source( "V1", "vcc", "5" ), u,
        { "R1", "Device:R", "10k", { { "1", "vcc" }, { "2", "out" } } } }, {}, true );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config ); session.AdvanceTo( .02 );
    const auto sample = session.Snapshot().latest;
    for( const auto& p : c.logicPins ) if( p.component == 1 )
    {
        if( p.pin == "2" ) BOOST_CHECK( PinLogicLevel( p, sample ) == LOGIC_LEVEL::HIGH_Z );
        if( p.pin == "3" ) BOOST_CHECK( PinLogicLevel( p, sample ) == LOGIC_LEVEL::UNKNOWN );
    }
}
BOOST_FIXTURE_TEST_CASE( Visual555PresetOscillatesAndResetDominatesItsLatch, DESIGN_FILES )
{
    auto d = MakeTimer555Design( templates ); BOOST_REQUIRE_EQUAL( d.blocks.size(), 11 );
    d = MODEL_DESIGN::Open( d.Export() );
    CIRCUIT_COMPONENT u{ "U1", "Timer:NE555P", "555", { { "1", "0" }, { "2", "timing" }, { "3", "out" },
        { "4", "reset" }, { "5", "control" }, { "6", "timing" }, { "7", "discharge" }, { "8", "vcc" } } };
    u.modelFile = Write( d );
    auto c = BuildCircuitSnapshot( { Source( "V1", "vcc", "9" ), Source( "VR", "reset", "9" ), u,
        { "RA", "Device:R", "10k", { { "1", "vcc" }, { "2", "discharge" } } },
        { "RB", "Device:R", "10k", { { "1", "discharge" }, { "2", "timing" } } },
        { "CT", "Device:C", "1u", { { "1", "timing" }, { "2", "0" } } },
        { "RL", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } } }, {}, true );
    c.config.signals = { "v(" + c.pinNodes[2].at( "3" ) + ")", "v(" + c.pinNodes[2].at( "2" ) + ")" };
    BOOST_CHECK_LE( c.config.maxStep, 1e-5 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
    bool high = false; double lastRise = 0, period = 0;
    for( int i = 1; i <= 1000; ++i )
    {
        session.AdvanceTo( i * 1e-4 ); auto sample = session.Snapshot( true ).latest; bool now = sample.values[0] > 4;
        if( now && !high ) { if( lastRise > .02 ) period = sample.time - lastRise; lastRise = sample.time; }
        high = now;
        if( sample.time > .025 ) { BOOST_CHECK_GT( sample.values[1], 2.9 ); BOOST_CHECK_LT( sample.values[1], 6.1 ); }
    }
    BOOST_CHECK_CLOSE( period, .020794, 2 );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( .101 ); BOOST_CHECK_LT( session.Snapshot().latest.values[0], .2 );
}
BOOST_FIXTURE_TEST_CASE( NestedVisualModelsKeepInternalPullupsAndLogicLevels, DESIGN_FILES )
{
    auto inner = MODEL_DESIGN::Open( templates.at( "block_nand" ) ); inner.baseJson.clear(); inner.id = "pulled_nand";
    DESIGN_BLOCK gate{ "Gate", templates.at( "block_nand" ), {}, {} };
    for( const auto& p : inner.pins ) gate.connections[p.id] = "@" + p.id;
    inner.blocks = { gate, { "Pullup", templates.at( "resistor" ), { { "p", "@b" }, { "n", "@vcc" } }, { { "resistance", "10k" } } } };
    const auto innerText = inner.Export();
    auto outer = MODEL_DESIGN::Open( innerText ); outer.baseJson.clear(); outer.id = "nested_nand";
    outer.blocks = { { "Inner", innerText, gate.connections, {} } };
    auto loaded = MODEL_REPOSITORY::LoadText( outer.Export() ); BOOST_CHECK( loaded.internalDrive.contains( "b" ) );
    CIRCUIT_COMPONENT u{ "U1", "Custom:Nand", "NAND", { { "1", "vcc" }, { "2", "" }, { "3", "out" }, { "4", "vcc" }, { "5", "0" } } };
    u.modelFile = Write( outer );
    auto c = BuildCircuitSnapshot( { Source( "V1", "vcc", "5" ), u }, {}, true );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config ); session.AdvanceTo( .02 );
    const auto sample = session.Snapshot().latest;
    bool checkedInput = false, checkedOutput = false;
    for( const auto& p : c.logicPins ) if( p.component == 1 )
    {
        if( p.pin == "2" ) { checkedInput = true; BOOST_CHECK( PinLogicLevel( p, sample ) == LOGIC_LEVEL::HIGH ); }
        if( p.pin == "3" ) { checkedOutput = true; BOOST_CHECK( PinLogicLevel( p, sample ) == LOGIC_LEVEL::LOW ); }
    }
    BOOST_CHECK( checkedInput ); BOOST_CHECK( checkedOutput );
}
BOOST_FIXTURE_TEST_CASE( ExistingInlineModelsCanBeDuplicatedWithoutManualJsonEditing, DESIGN_FILES )
{
    auto d = MODEL_DESIGN::Open( templates.at( "lm317" ) ); d.id = "my_lm317"; d.name = "My regulator";
    const auto text = d.Export(); auto restored = MODEL_DESIGN::Open( text );
    BOOST_CHECK_EQUAL( restored.id, "my_lm317" ); BOOST_CHECK( !restored.baseJson.empty() );
    auto model = MODEL_REPOSITORY::LoadText( restored.Export() ); BOOST_CHECK_EQUAL( model.pins.size(), 3 );
}
