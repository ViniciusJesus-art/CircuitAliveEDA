/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/model_design.h>
#include <circuitalive/model_design_presets.h>
#include <circuitalive/model_routing.h>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/control_references.h>
#include <circuitalive/model_profiles.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <sim/platform_runtime.h>
using namespace CIRCUITALIVE;
namespace fs = std::filesystem;
namespace
{
struct DESIGN_FILES
{
    fs::path directory = fs::temp_directory_path() / ( "ca-author-" + std::to_string( CIRCUITALIVE::ProcessId() ) );
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

CIRCUIT_SNAPSHOT IdealBlockCircuit( DESIGN_FILES& files, const std::string& id,
        const std::map<std::string,std::string>& nets,
        const std::vector<std::pair<std::string,std::string>>& sources,
        const std::map<std::string,std::string>& values, const std::string& output,
        const std::string& bias = "0" )
{
    const auto model = MODEL_REPOSITORY::LoadText(files.templates.at(id));
    MODEL_DESIGN d; d.id = "test_"+id; d.name = model.name; d.pins = model.pins;
    DESIGN_BLOCK block{"Core",files.templates.at(id),{},values};
    CIRCUIT_COMPONENT u{"U1","Custom:Ideal",id,{}};
    std::string outputPin;
    for( const auto& pin : model.pins )
    {
        block.connections[pin.id] = "@"+pin.id; u.pins[pin.defaultPin] = nets.at(pin.id);
        if( pin.id == output ) outputPin = pin.defaultPin;
    }
    d.blocks.push_back(block);
    const auto exported = d.Export(); auto restored = MODEL_DESIGN::Open(exported);
    BOOST_CHECK_EQUAL(restored.Export(),exported); u.modelFile = files.Write(restored);
    std::vector<CIRCUIT_COMPONENT> parts;
    for( const auto& [node,value] : sources ) parts.push_back(Source("V"+std::to_string(parts.size()+1),node,value));
    const auto component = parts.size(); parts.push_back(u);
    parts.push_back({"Rload","Device:R","1k",{{"1",nets.at(output)},{"2",bias}}});
    auto circuit = BuildCircuitSnapshot(parts);
    circuit.config.signals = {"v("+circuit.pinNodes[component].at(outputPin)+")"};
    return circuit;
}
}
BOOST_FIXTURE_TEST_CASE( VisualModelRoundTripKeepsEmbeddedBlocksAndPublicParameters, DESIGN_FILES )
{
    auto d = Divider(); const auto text = d.Export(); auto restored = MODEL_DESIGN::Open( text );
    BOOST_REQUIRE_EQUAL( restored.blocks.size(), 2 ); BOOST_CHECK_EQUAL( restored.libraryId, d.libraryId );
    BOOST_CHECK_EQUAL( restored.blocks[0].x, 300 ); BOOST_CHECK_EQUAL( restored.blocks[0].connections.at( "n" ), "@out" );
    BOOST_CHECK_EQUAL( restored.blocks[0].values.at( "resistance" ), "@upper" );
    BOOST_CHECK_EQUAL( restored.Export(), text );
    auto model = MODEL_REPOSITORY::LoadText( text ); BOOST_REQUIRE_EQUAL( model.libraryIds.size(),1 ); BOOST_CHECK_EQUAL(model.libraryIds[0],"Custom:Divider");
    CIRCUIT_COMPONENT u{ "U1", "Custom:Divider", "Divider", { { "10", "in" }, { "20", "out" }, { "30", "0" } } };
    u.modelFile = Write( restored ); auto u2 = u; u2.reference = "U2"; u2.pins["20"] = "out2"; u2.modelParameters["upper"] = "3k";
    auto c = BuildCircuitSnapshot( { Source( "V1", "in", "5" ), u, u2 } );
    c.config.signals = { "v(" + c.pinNodes[1].at( "20" ) + ")", "v(" + c.pinNodes[2].at( "20" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config ); session.AdvanceTo( .02 );
    auto sample = session.Snapshot().latest; BOOST_CHECK_CLOSE( sample.values[0], 2.5, 1e-5 ); BOOST_CHECK_CLOSE( sample.values[1], 1.25, 1e-5 );
}

BOOST_FIXTURE_TEST_CASE( CurrentLoadDrawsConfiguredSupplyCurrentAndRejectsNegativeConsumption, DESIGN_FILES )
{
    MODEL_DESIGN d; d.id = "load_test"; d.name = "Load test";
    d.pins = {{"vcc","Supply","power","1"},{"gnd","Return","reference","2"}};
    d.blocks = {{"Load",templates.at("current_load"),{{"p","@vcc"},{"n","@gnd"}},{{"current","20m"}}}};
    auto restored = MODEL_DESIGN::Open(d.Export());
    CIRCUIT_COMPONENT u{"U1","Custom:Load","Load",{{"1","load"},{"2","0"}}};
    u.modelFile = Write(restored);
    CIRCUIT_COMPONENT resistance{"R1","Device:R","10",{{"1","supply"},{"2","load"}}};
    auto circuit = BuildCircuitSnapshot({Source("V1","supply","5"),resistance,u});
    circuit.config.signals = {"v("+circuit.pinNodes[0].at("1")+")","v("+circuit.pinNodes[2].at("1")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
    session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);
    const auto sample = session.Snapshot().latest;
    BOOST_CHECK_CLOSE((sample.values[0]-sample.values[1])/10,.02,1e-5);
    BOOST_CHECK_CLOSE(sample.values[1],4.8,1e-5);
    restored.blocks[0].values["current"] = "-20m";
    BOOST_CHECK_THROW(restored.Export(),std::runtime_error);
    // Existing files using the general signed current source remain supported.
    BOOST_CHECK(templates.contains("constant_current"));
}

BOOST_FIXTURE_TEST_CASE( VisualInteractionsRoundTripAndDriveIndependentInstancesWithoutRebuilding, DESIGN_FILES )
{
    MODEL_DESIGN d; d.id = "interactive_sensor"; d.name = "Interactive sensor";
    d.pins = { { "light", "Light", "output", "1" }, { "button", "Button", "output", "2" },
               { "gnd", "Ground", "reference", "3" } };
    d.blocks = {
        { "Light", templates.at( "interactive_value" ), {{"out","@light"},{"ref","@gnd"}}, {{"value",".25"}} },
        { "Press", templates.at( "interactive_button" ), {{"out","@button"},{"ref","@gnd"}}, {} } };
    d.ConfigureInteractions();
    d.interactions = { { "Light", "adjust:value", "Received light", {true,1.3,0,.5,.3} },
                       { "Press", "switch", "Object present", {true,1.3,.6,.5,.3} } };
    const auto text = d.Export(); auto restored = MODEL_DESIGN::Open( text );
    BOOST_REQUIRE_EQUAL( restored.interactions.size(), 2 );
    BOOST_CHECK( restored.interactions[0].visual == d.interactions[0].visual );
    BOOST_CHECK_EQUAL( restored.Export(), text );
    CIRCUIT_COMPONENT u{ "U1", "Custom:IR", "IR", {{"1","light1"},{"2","button1"},{"3","0"}} };
    u.modelFile = Write( restored ); auto u2 = u; u2.reference = "U2"; u2.pins["1"] = "light2"; u2.pins["2"] = "button2";
    auto c = BuildCircuitSnapshot( {u,u2} );
    BOOST_REQUIRE_EQUAL( c.adjustments.size(), 2 ); BOOST_REQUIRE_EQUAL( c.switches.size(), 2 );
    BOOST_CHECK( c.adjustments[0].visual == d.interactions[0].visual );
    BOOST_CHECK_NE( c.adjustments[0].source, c.adjustments[1].source );
    BOOST_CHECK_EQUAL( c.adjustments[0].label, "Received light" );
    c.config.signals = { "v("+c.pinNodes[0].at("1")+")", "v("+c.pinNodes[0].at("2")+")",
                         "v("+c.pinNodes[1].at("1")+")", "v("+c.pinNodes[1].at("2")+")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config ); session.AdvanceTo( .001 );
    auto sample = session.Snapshot().latest;
    BOOST_CHECK_SMALL( sample.values[0] - .25, 1e-8 ); BOOST_CHECK_SMALL( sample.values[1], 1e-8 );
    session.SetSourceValue( c.adjustments[0].source, .8 ); session.SetSourceValue( c.switches[0].controlSource, 5 );
    session.AdvanceTo( .002 ); sample = session.Snapshot().latest;
    BOOST_CHECK_SMALL( sample.values[0] - .8, 1e-8 ); BOOST_CHECK_SMALL( sample.values[1] - 5, 1e-8 );
    BOOST_CHECK_SMALL( sample.values[2] - .25, 1e-8 ); BOOST_CHECK_SMALL( sample.values[3], 1e-8 );
    // Per-instance initial values must affect the electrical source as well as the UI state.
    u2.modelParameters["live_b_Light_value"] = ".6";
    auto overridden = BuildCircuitSnapshot( {u2} );
    overridden.config.signals = { "v("+overridden.pinNodes[0].at("1")+")" };
    INTERACTIVE_SIM_SESSION initialSession( CIRCUITALIVE_TEST_WORKER );
    initialSession.Configure( overridden.netlist, overridden.config ); initialSession.AdvanceTo( .001 );
    BOOST_CHECK_SMALL( initialSession.Snapshot().latest.values[0] - .6, 1e-8 );
    u2.modelParameters.clear();
    // A button's edited ON/initial values are applied before the first timestep.
    auto buttonJson = nlohmann::json::parse( restored.blocks[1].modelJson );
    buttonJson["control"]["on"] = 3.3; buttonJson["control"]["initially_closed"] = true;
    restored.blocks[1].modelJson = buttonJson.dump(); u.modelFile = Write( restored );
    c = BuildCircuitSnapshot( {u,u2} );
    c.config.signals = { "v("+c.pinNodes[0].at("1")+")", "v("+c.pinNodes[0].at("2")+")",
                         "v("+c.pinNodes[1].at("1")+")", "v("+c.pinNodes[1].at("2")+")" };
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER ); runner.Start( c );
    auto wait = [&]( auto predicate )
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        for(;;)
        {
            auto view = runner.View(); BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
            if( predicate( view ) ) return view;
            BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
    };
    auto view = wait( []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING; } );
    BOOST_CHECK_SMALL( view.sample.latest.values[1] - 3.3, 1e-8 );
    runner.ToggleSwitch( 0 ); runner.SetAdjustment( 0, .9 );
    view = wait( []( const auto& v ) { return !v.switches[0] && v.adjustments[0] == .9; } );
    view = wait( []( const auto& v ) { return std::abs( v.sample.latest.values[0] - .9 ) < 1e-8; } );
    BOOST_CHECK_SMALL( view.sample.latest.values[1], 1e-8 );
    BOOST_CHECK_SMALL( view.sample.latest.values[2] - .25, 1e-8 );
    // A composed interactive model can itself be embedded and exposed again.
    MODEL_DESIGN outer; outer.id = "outer_sensor"; outer.name = "Outer sensor"; outer.pins = d.pins;
    outer.blocks = { { "Sensor", text, {{"light","@light"},{"button","@button"},{"gnd","@gnd"}}, {} } };
    outer.ConfigureInteractions(); outer.interactions = outer.AvailableInteractions();
    auto nested = MODEL_REPOSITORY::LoadText( outer.Export() );
    BOOST_CHECK_EQUAL( nested.adjustments.size(), 1 ); BOOST_CHECK_EQUAL( nested.controls.size(), 1 );
}

BOOST_FIXTURE_TEST_CASE( InteractionEditorAdoptsPotentiometerAndRejectsDanglingOrInvalidControls, DESIGN_FILES )
{
    auto d = MODEL_DESIGN::Open( templates.at( "potentiometer" ) ); d.MakeCoreVisible(); d.ConfigureInteractions();
    BOOST_REQUIRE_EQUAL( d.interactions.size(), 1 );
    d.interactions[0].visual = {true,-.6,.2,.4,.4,false};
    const auto compiled = MODEL_REPOSITORY::LoadText( d.Export() );
    BOOST_REQUIRE_EQUAL( compiled.adjustments.size(), 1 );
    BOOST_CHECK( compiled.adjustments[0].source.find( "b_Core_" ) != std::string::npos );
    BOOST_CHECK_EQUAL( compiled.adjustments[0].parameter, "position" );
    BOOST_CHECK( !compiled.adjustments[0].visual.visible );
    BOOST_CHECK( !MODEL_DESIGN::Open( d.Export() ).interactions[0].visual.visible );
    auto invalid = d; invalid.interactions[0].block = "missing"; BOOST_CHECK_THROW( invalid.Export(), std::runtime_error );
    invalid = d; invalid.interactions[0].control = "adjust:missing"; BOOST_CHECK_THROW( invalid.Export(), std::runtime_error );
    invalid = d; invalid.interactions.push_back( invalid.interactions[0] ); BOOST_CHECK_THROW( invalid.Export(), std::runtime_error );
    invalid = d; invalid.interactions[0].visual.width = 0; BOOST_CHECK_THROW( invalid.Export(), std::runtime_error );
    invalid = d; invalid.interactions[0].visual.x = 1e20; BOOST_CHECK_THROW( invalid.Export(), std::runtime_error );
    auto json = nlohmann::json::parse( d.Export() );
    json["adjustments"][0]["visual"]["height"] = -1;
    BOOST_CHECK_THROW( MODEL_REPOSITORY::LoadText( json.dump() ), std::runtime_error );
    d.RemoveBlock( "Core" ); BOOST_CHECK( d.interactions.empty() );
}

BOOST_FIXTURE_TEST_CASE( DeletingRoutedWireSplitsNetAndPreservesOtherBranchesAfterReopen, DESIGN_FILES )
{
    auto d = Divider();
    d.blocks[0].connections["p"] = "@in";
    d.blocks[1].connections["p"] = "@in";
    d.blocks.push_back( { "R3", templates.at("resistor"), {{"p","@in"},{"n","@gnd"}}, {}, 800,200 } );
    const DESIGN_TERMINAL pin{"","in","@in",{100,100}};
    const DESIGN_TERMINAL r1{"R1","p","@in",{300,100}};
    const DESIGN_TERMINAL r2{"R2","p","@in",{600,100}};
    const DESIGN_TERMINAL r3{"R3","p","@in",{800,100}};
    const std::vector<DESIGN_WIRE> wires{{pin,r1,{}},{r1,r2,{}},{r2,r3,{}}};
    d.wireRoutes = {{"","in","R1","p",{}},{"R1","p","R2","p",{}},{"R2","p","R3","p",{}}};
    BOOST_REQUIRE( DisconnectDesignWire(d,wires,1) );
    BOOST_CHECK_EQUAL( d.blocks[0].connections.at("p"), "@in" );
    const auto newNet = d.blocks[1].connections.at("p");
    BOOST_CHECK( !newNet.empty() && newNet != "@in" );
    BOOST_CHECK_EQUAL( d.blocks[2].connections.at("p"), newNet );
    BOOST_REQUIRE_EQUAL( d.wireRoutes.size(), 2 );
    auto restored = MODEL_DESIGN::Open(d.Export());
    BOOST_CHECK_EQUAL( restored.blocks[1].connections.at("p"), newNet );
    BOOST_CHECK_EQUAL( restored.blocks[2].connections.at("p"), newNet );
    // Cutting a leaf clears just that terminal, in either wire orientation.
    auto leaf = Divider();
    const std::vector<DESIGN_WIRE> reversed{{r1,pin,{}}};
    BOOST_REQUIRE( DisconnectDesignWire(leaf,reversed,0) );
    BOOST_CHECK( leaf.blocks[0].connections.at("p").empty() );
    BOOST_CHECK_EQUAL( leaf.blocks[0].connections.at("n"), "@out" );
}

BOOST_FIXTURE_TEST_CASE( ControlReferencesAdjustResistorsAndButtonValuesWithoutResettingCapacitors, DESIGN_FILES )
{
    auto d=Divider();
    auto slider=nlohmann::json::parse(templates.at("interactive_value"));
    slider["adjustments"][0]["min"]=1000;slider["adjustments"][0]["max"]=3000;slider["adjustments"][0]["step"]=100;
    slider["parameters"][0]["default"]="1000";
    d.blocks.push_back({"Slider",slider.dump(),{},{{"value","1000"}}});
    d.blocks.push_back({"Button",templates.at("interactive_switch"),{}, {}});
    d.blocks.push_back({"C1",templates.at("capacitor"),{{"p","@out"},{"n","@gnd"}},{{"capacitance","1u"}}});
    d.ConfigureInteractions();
    d.interactions={{"Slider","adjust:value","Resistance",{true,1.2,0,.5,.3},"slider1"},
                    {"Button","switch","Button",{true,1.2,.5,.5,.3},"button1","1k","3k"}};
    d.blocks[0].values["resistance"]="#slider1";d.blocks[1].values["resistance"]="#button1";
    const auto saved=d.Export();BOOST_CHECK_EQUAL(MODEL_DESIGN::Open(saved).Export(),saved);
    CIRCUIT_COMPONENT u{"U1","Custom:Divider","Divider",{{"10","in"},{"20","out"},{"30","0"}}};u.modelFile=Write(d);
    auto u2=u;u2.reference="U2";u2.pins["20"]="out2";
    auto c=BuildCircuitSnapshot({Source("V1","in","5"),u,u2});
    BOOST_REQUIRE_EQUAL(c.adjustments.size(),2);BOOST_REQUIRE_EQUAL(c.switches.size(),2);BOOST_REQUIRE_EQUAL(c.liveUpdates.size(),4);
    c.config.signals={"v("+c.pinNodes[1].at("20")+")","v("+c.pinNodes[2].at("20")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.02);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-3.75,1e-5);
    std::map<std::string,double> values{{c.adjustments[0].source,1000},{c.switches[0].controlSource,5}};
    session.SetSourceValue(c.switches[0].controlSource,5,LiveParameterChanges(c,values,c.switches[0].controlSource));
    session.AdvanceTo(.020001);BOOST_CHECK_GT(session.Snapshot().latest.values[0],3.7);
    session.AdvanceTo(.04);BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-2.5,1e-5);
    values[c.adjustments[0].source]=3000;
    session.SetSourceValue(c.adjustments[0].source,3000,LiveParameterChanges(c,values,c.adjustments[0].source));
    session.AdvanceTo(.06);BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-1.25,1e-5);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[1]-3.75,1e-5);
    auto wrong=d;wrong.blocks[0].values["resistance"]="#missing";BOOST_CHECK_THROW(wrong.Export(),std::runtime_error);
    wrong=d;wrong.interactions[1].onValue="OPEN";BOOST_CHECK_THROW(wrong.Export(),std::runtime_error);
    wrong=d;wrong.interactions[0].reference="button1";BOOST_CHECK_THROW(wrong.Export(),std::runtime_error);
    wrong=d;wrong.blocks[0].values["resistance"]="#Resistance";BOOST_CHECK_NO_THROW(wrong.Export());
}

BOOST_FIXTURE_TEST_CASE( InteractivePassThroughButtonClosesAndOpensTheElectricalPath, DESIGN_FILES )
{
    auto d=Divider();d.blocks[0]={"Button",templates.at("interactive_switch"),{{"p","@in"},{"n","@out"}}, {}};
    d.ConfigureInteractions();d.interactions={{"Button","switch","Contact",{},"button1"}};
    CIRCUIT_COMPONENT u{"U1","Custom:Divider","Divider",{{"10","in"},{"20","out"},{"30","0"}}};u.modelFile=Write(d);
    auto c=BuildCircuitSnapshot({Source("V1","in","5"),u});c.config.signals={"v("+c.pinNodes[1].at("20")+")"};
    BOOST_REQUIRE_EQUAL(c.switches.size(),1);INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);
    s.Configure(c.netlist,c.config);s.AdvanceTo(.01);BOOST_CHECK_SMALL(s.Snapshot().latest.values[0],1e-7);
    s.SetSourceValue(c.switches[0].controlSource,c.switches[0].on);s.AdvanceTo(.02);
    BOOST_CHECK_SMALL(s.Snapshot().latest.values[0]-5,1e-4);
    s.SetSourceValue(c.switches[0].controlSource,c.switches[0].off);s.AdvanceTo(.03);
    BOOST_CHECK_SMALL(s.Snapshot().latest.values[0],1e-7);
}

BOOST_FIXTURE_TEST_CASE( ControlReferencesChangeNativeModelAndBehavioralParameters, DESIGN_FILES )
{
    for(const bool behavioral:{false,true})
    {
        auto d=Divider();auto slider=nlohmann::json::parse(templates.at("interactive_value"));
        slider["adjustments"][0]["min"]=behavioral?1:1000;slider["adjustments"][0]["max"]=behavioral?5:3000;
        slider["adjustments"][0]["step"]=behavioral?.1:100;slider["parameters"][0]["default"]=behavioral?"2":"1000";
        if(behavioral)
            d.blocks={{"Core",templates.at("ideal_condition"),{{"a","@in"},{"b","@gnd"},{"y","@out"}},{{"high","#slider1"}}}};
        else
        {
            auto sw=nlohmann::json::parse(templates.at("interactive_switch"));sw["control"]["initially_closed"]=true;
            d.blocks[0]={"Path",sw.dump(),{{"p","@in"},{"n","@out"}},{{"on_resistance","#slider1"}}};
        }
        d.blocks.push_back({"Slider",slider.dump(),{},{{"value",behavioral?"2":"1000"}}});
        d.ConfigureInteractions();d.interactions={{"Slider","adjust:value","Level",{},"slider1"}};
        CIRCUIT_COMPONENT u{"U1","Custom:Divider","Divider",{{"10","in"},{"20","out"},{"30","0"}}};u.modelFile=Write(d);
        auto c=BuildCircuitSnapshot({Source("V1","in","5"),u});c.config.signals={"v("+c.pinNodes[1].at("20")+")"};
        BOOST_REQUIRE_EQUAL(c.adjustments.size(),1);BOOST_REQUIRE(!c.liveUpdates.empty());
        INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);s.Configure(c.netlist,c.config);s.AdvanceTo(.01);
        BOOST_CHECK_SMALL(s.Snapshot().latest.values[0]-(behavioral?2:2.5),1e-5);
        const double next=behavioral?4:3000;std::map<std::string,double> values{{c.adjustments[0].source,next}};
        s.SetSourceValue(c.adjustments[0].source,next,LiveParameterChanges(c,values,c.adjustments[0].source));s.AdvanceTo(.02);
        BOOST_CHECK_SMALL(s.Snapshot().latest.values[0]-(behavioral?4:1.25),1e-5);
    }
}

BOOST_FIXTURE_TEST_CASE( ButtonReferencesAcceptTextOnlyInTextFields, DESIGN_FILES )
{
    auto d=Divider();
    d.blocks.push_back({"Slider",templates.at("interactive_value"),{},{{"value",".5"}}});
    d.blocks.push_back({"Button",templates.at("interactive_button"),{}, {}});
    d.ConfigureInteractions();
    d.interactions={{"Slider","adjust:value","#button1",{},"slider1"},
                    {"Button","switch","State",{},"button1","LIGADO","DESLIGADO"}};
    const auto text=d.Export();BOOST_CHECK_EQUAL(MODEL_DESIGN::Open(text).Export(),text);
    CIRCUIT_COMPONENT u{"U1","Custom:Divider","Divider",{{"10","in"},{"20","out"},{"30","0"}}};u.modelFile=Write(d);
    auto c=BuildCircuitSnapshot({Source("V1","in","5"),u});
    BOOST_REQUIRE_EQUAL(c.liveTexts.size(),1);
    BOOST_CHECK_EQUAL(LiveControlText(c,{false},{.5},1,"b_Slider_value","label","fallback"),"DESLIGADO");
    BOOST_CHECK_EQUAL(LiveControlText(c,{true},{.5},1,"b_Slider_value","label","fallback"),"LIGADO");
    d.blocks[0].values["resistance"]="#button1";BOOST_CHECK_THROW(d.Export(),std::runtime_error);
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
        if( p.pin == "3" )
        {
            BOOST_CHECK( PinDriveLevel( p, sample ) == LOGIC_LEVEL::UNKNOWN );
            // The model's X stage is weak; the 10k pull-up resolves the actual wire.
            BOOST_CHECK( PinLogicLevel( p, sample ) == LOGIC_LEVEL::HIGH );
        }
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
    BOOST_CHECK_LE( c.config.maxStep, 100e-6 );
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
BOOST_FIXTURE_TEST_CASE( VisibleCoresKeepAllBundledElectricalModelsAndTheirMetadata, DESIGN_FILES )
{
    for( const auto& [id, text] : templates )
    {
        BOOST_TEST_CONTEXT( id )
        {
            if( MODEL_REPOSITORY::LoadText( text ).runtime )
            {
                auto design=MODEL_DESIGN::Open(text);design.MakeCoreVisible();
                const auto edited=MODEL_REPOSITORY::LoadText(design.Export());
                const auto original=MODEL_REPOSITORY::LoadText(text);
                BOOST_REQUIRE(edited.runtime);BOOST_CHECK(edited.runtime->modules==original.runtime->modules);
                BOOST_CHECK_EQUAL(edited.pins.size(),original.pins.size());
                BOOST_CHECK_EQUAL(edited.libraryIds.size(),original.libraryIds.size());
                continue;
            }
            auto design = MODEL_DESIGN::Open( text ); design.MakeCoreVisible();
            const auto count = design.blocks.size();
            BOOST_REQUIRE_GE( count, 1 );
            const auto original = MODEL_REPOSITORY::LoadText(text);
            const auto compiled = MODEL_REPOSITORY::LoadText(design.Export());
            BOOST_CHECK_GE( compiled.instance.size(), original.instance.size() );
            BOOST_CHECK_EQUAL( original.definitions.size(), compiled.definitions.size() );
            BOOST_CHECK_EQUAL( original.parameters.size(), compiled.parameters.size() );
            BOOST_CHECK_EQUAL( original.memories.size(), compiled.memories.size() );
            BOOST_CHECK_EQUAL( original.logicVcc, compiled.logicVcc );
            BOOST_CHECK_EQUAL( original.logicThresholdReference, compiled.logicThresholdReference );
            BOOST_CHECK_EQUAL( original.currentWarnings.size(), compiled.currentWarnings.size() );
            BOOST_CHECK( original.legacyParameterMap == compiled.legacyParameterMap );
            if( !original.indicatorSignal.empty() ) BOOST_CHECK( compiled.indicatorSignal.find("b_Core_") != std::string::npos );
            if(!original.controlSource.empty())
                BOOST_CHECK(compiled.controlSource.find("b_Core_")!=std::string::npos
                    ||std::any_of(compiled.controls.begin(),compiled.controls.end(),[](const auto& control){return control.source.find("b_Core_")!=std::string::npos;}));
            auto restored = MODEL_DESIGN::Open(design.Export());
            restored.MakeCoreVisible(); BOOST_CHECK_EQUAL( restored.blocks.size(), count );
            BOOST_CHECK_EQUAL( restored.Export(), design.Export() );
        }
    }
}
BOOST_FIXTURE_TEST_CASE( VisibleDiodeCoreRetainsExponentialConductionAndEditableParameters, DESIGN_FILES )
{
    auto d = MODEL_DESIGN::Open( templates.at("diode") ); d.MakeCoreVisible(); d.id="editable_diode";
    CIRCUIT_COMPONENT original{ "D1", "Device:D", "D", { {"1","0"}, {"2","anode"} } };
    auto edited = original; edited.reference="D2"; edited.modelFile=Write(d); edited.pins["2"]="anode2";
    auto c=BuildCircuitSnapshot( { Source("V1","supply","+5v"), original, edited,
        {"R1","Device:R","1k",{{"1","supply"},{"2","anode"}}},
        {"R2","Device:R","1k",{{"1","supply"},{"2","anode2"}}} } );
    c.config.signals={"v("+c.pinNodes[1].at("2")+")","v("+c.pinNodes[2].at("2")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);session.AdvanceTo(.01);
    const auto sample=session.Snapshot().latest;
    BOOST_CHECK_CLOSE(sample.values[0],sample.values[1],1e-6);
    BOOST_CHECK_GT(sample.values[1],.5);BOOST_CHECK_LT(sample.values[1],.8);
    double time=.01;
    for( double voltage : {0.,-10.,-110.,1.} )
    {
        session.SetSourceValue("Vca1",voltage);time+=.01;session.AdvanceTo(time);
        const auto sweep=session.Snapshot().latest;
        BOOST_CHECK_SMALL(sweep.values[0]-sweep.values[1],1e-6);
    }
    BOOST_REQUIRE_EQUAL(d.blocks.size(),2);
    d.parameters[2].defaultValue="1000"; edited.modelFile=Write(d);
    c=BuildCircuitSnapshot( { Source("V1","supply","5"), edited,
        {"R1","Device:R","1k",{{"1","supply"},{"2","anode2"}}} } );
    c.config.signals={"v("+c.pinNodes[1].at("2")+")"};
    INTERACTIVE_SIM_SESSION changed(CIRCUITALIVE_TEST_WORKER);changed.Configure(c.netlist,c.config);changed.AdvanceTo(.01);
    BOOST_CHECK_GT(changed.Snapshot().latest.values[0],2.5);
}
BOOST_FIXTURE_TEST_CASE( IdealInverterAndConditionWorkWithoutSupplyPins, DESIGN_FILES )
{
    for( const auto* id : { "ideal_inverter", "ideal_condition" } )
    {
        auto d=MODEL_DESIGN::Open(templates.at(id)); d.MakeCoreVisible(); d.id="test_"+std::string(id);
        CIRCUIT_COMPONENT u{"U1","Custom:Logic","logic",{{"1","input"}}};u.modelFile=Write(d);
        const bool inverter=std::string(id)=="ideal_inverter";
        if(inverter)u.pins["2"]="out";else {u.pins["2"]="reference";u.pins["3"]="out";}
        auto c=BuildCircuitSnapshot({Source("V1","input","0v"),Source("V2","reference","+2.5V"),u,
            {"R1","Device:R","1Meg",{{"1","out"},{"2","0"}}}});
        c.config.signals={"v("+c.pinNodes[2].at(inverter?"2":"3")+")"};
        INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);s.Configure(c.netlist,c.config);s.AdvanceTo(.01);
        BOOST_CHECK_CLOSE(s.Snapshot().latest.values[0],inverter?5.:0.,.001);
        s.SetSourceValue("Vca1",5);s.AdvanceTo(.02);
        BOOST_CHECK_CLOSE(s.Snapshot().latest.values[0],inverter?0.:5.,.001);
    }
}

BOOST_FIXTURE_TEST_CASE( IdealAnalogContactsAreBidirectionalAndOpenAtTheirConfiguredCondition, DESIGN_FILES )
{
    for( const auto* id : {"ideal_analog_switch","ideal_analog_switch_nc"} )
    for( const bool reverse : {false,true} ) BOOST_TEST_CONTEXT(id << " reverse=" << reverse)
    {
        const bool nc = std::string(id).ends_with("_nc");
        const auto output = reverse ? "a" : "b";
        auto circuit = IdealBlockCircuit(*this,id,{{"a",reverse?"out":"signal"},
            {"b",reverse?"signal":"out"},{"enable","control"}},
            {{"control","0"},{"signal","-2.75"},{"bias","1"}},{},output,"bias");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(circuit.netlist,circuit.config); session.AdvanceTo(.001);
        double time = .001;
        for( double control : {0.,2.5,5.,0.} )
        {
            session.SetSourceValue("Vca1",control);session.AdvanceTo(time+=.001);
            const bool closed = nc ? control<2.5 : control>=2.5;
            const double expected = closed ? (-2.75*1000+.01)/(1000+.01) : 1.;
            BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-expected,1e-7);
        }
    }
}

BOOST_FIXTURE_TEST_CASE( IdealAnalogSelectorsAndIfPreserveIntermediateAndNegativeVoltages, DESIGN_FILES )
{
    for( const auto* id : {"ideal_analog_selector","ideal_if"} ) BOOST_TEST_CONTEXT(id)
    {
        const bool contact = std::string(id)=="ideal_analog_selector";
        auto circuit = IdealBlockCircuit(*this,id,{{"a","value_a"},{"b","value_b"},{"y","out"},
            {contact?"select":"condition","control"}},{{"control","0"},{"value_a","2.75"},{"value_b","-1.25"}},{},"y");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);
        double time = .001;
        for( double control : {0.,2.5,5.,0.} )
        {
            session.SetSourceValue("Vca1",control);session.AdvanceTo(time+=.001);
            const double selected = control>=2.5 ? 2.75 : -1.25;
            BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-selected*1000/(1000+(contact?.01:1)),1e-6);
        }
    }
}

BOOST_FIXTURE_TEST_CASE( IdealEnabledBufferDisconnectsInsteadOfForcingZero, DESIGN_FILES )
{
    auto circuit = IdealBlockCircuit(*this,"ideal_enabled_buffer",{{"a","signal"},{"enable","control"},{"y","out"}},
        {{"control","0"},{"signal","2.75"},{"bias","1"}},{},"y","bias");
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
    session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-1,1e-7); // External bias wins when disabled.
    session.SetSourceValue("Vca1",5);session.AdvanceTo(.002);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-(2.75*1000+.01)/(1000+.01),1e-7);
    session.SetSourceValue("Vca1",0);session.AdvanceTo(.003);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-1,1e-7);
}

BOOST_FIXTURE_TEST_CASE( IdealComparisonBlocksRespectEqualityAndTolerance, DESIGN_FILES )
{
    for( const std::string suffix : {"ge","lt","le","equal","not_equal"} ) BOOST_TEST_CONTEXT(suffix)
    {
        auto circuit = IdealBlockCircuit(*this,"ideal_compare_"+suffix,{{"a","input_a"},{"b","input_b"},{"y","out"}},
            {{"input_a","0"},{"input_b","2"}},{},"y");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);double time = .001;
        for( double a : {1.,2.,3.,2.0005,2.002} )
        {
            session.SetSourceValue("Vca1",a);session.AdvanceTo(time+=.001);
            const bool result = suffix=="ge" ? a>=2 : suffix=="lt" ? a<2 : suffix=="le" ? a<=2
                : suffix=="equal" ? std::abs(a-2)<=.001 : std::abs(a-2)>.001;
            BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-(result?5.*1000/1001:0),1e-6);
        }
    }
}

BOOST_FIXTURE_TEST_CASE( IdealLogicGateExtensionsImplementAllTruthTableRows, DESIGN_FILES )
{
    for( const std::string gate : {"nand","nor","xor","xnor"} ) BOOST_TEST_CONTEXT(gate)
    {
        auto circuit = IdealBlockCircuit(*this,"ideal_"+gate,{{"a","input_a"},{"b","input_b"},{"y","out"}},
            {{"input_a","0"},{"input_b","0"}},{},"y");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);double time = .001;
        for( int row = 0; row < 4; ++row )
        {
            const bool a = row&1,b = row&2;
            session.SetSourceValue("Vca1",a?2.5:0);session.SetSourceValue("Vca2",b?5:0);
            session.AdvanceTo(time+=.001);
            const bool result = gate=="nand" ? !(a&&b) : gate=="nor" ? !(a||b) : gate=="xor" ? a!=b : a==b;
            BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-(result?5.*1000/1001:0),1e-6);
        }
    }
}

BOOST_FIXTURE_TEST_CASE( IdealWindowMeasuresDifferentialVoltageAndIncludesItsLimits, DESIGN_FILES )
{
    auto circuit = IdealBlockCircuit(*this,"ideal_window",{{"plus","measured"},{"minus","reference"},{"y","out"}},
        {{"measured","17"},{"reference","12"}},{},"y");
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
    session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);double time = .001;
    for( double voltage : {16.4,16.5,17.,17.5,17.6,12.} )
    {
        session.SetSourceValue("Vca1",voltage);session.AdvanceTo(time+=.001);
        const bool valid = voltage>=16.5 && voltage<=17.5;
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-(valid?5.*1000/1001:0),1e-6);
    }
}

BOOST_FIXTURE_TEST_CASE( IdealWindowAndSwitchGateAnalogSensorOutputOnValidSupply, DESIGN_FILES )
{
    MODEL_DESIGN d;d.id="gated_sensor";d.name="Gated sensor";
    d.pins={{"vcc","VCC","power","1"},{"gnd","GND","reference","2"},
        {"input","Analog value","input","3"},{"out","OUT","output","4"}};
    d.blocks={{"Valid",templates.at("ideal_window"),{{"plus","@vcc"},{"minus","@gnd"},{"y","enabled"}},{}},
        {"Gate",templates.at("ideal_analog_switch"),{{"a","@input"},{"b","@out"},{"enable","enabled"}},{}}};
    auto restored = MODEL_DESIGN::Open(d.Export());
    CIRCUIT_COMPONENT u{"U1","Custom:Sensor","Sensor",{{"1","supply"},{"2","0"},{"3","signal"},{"4","out"}}};
    u.modelFile = Write(restored);
    const CIRCUIT_COMPONENT load{"R1","Device:R","100k",{{"1","out"},{"2","0"}}};
    auto circuit = BuildCircuitSnapshot({Source("V1","supply","5"),Source("V2","signal","2.75"),u,load});
    circuit.config.signals={"v("+circuit.pinNodes[2].at("4")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
    session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);double time=.001;
    for( double supply : {5.,0.,4.,5.,6.} )
    {
        session.SetSourceValue("Vca1",supply);session.AdvanceTo(time+=.001);
        const bool valid = supply>=4.5 && supply<=5.5;
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-(valid?2.75:0),1e-6);
    }
    // Either supply terminal left open also disables this composed circuit.
    for( const auto* pin : {"1","2"} )
    {
        auto disconnected=u;disconnected.pins[pin]="";
        auto openCircuit=BuildCircuitSnapshot({Source("V1","supply","5"),Source("V2","signal","2.75"),disconnected,load});
        openCircuit.config.signals={"v("+openCircuit.pinNodes[2].at("4")+")"};
        INTERACTIVE_SIM_SESSION openSession(CIRCUITALIVE_TEST_WORKER);
        openSession.Configure(openCircuit.netlist,openCircuit.config);openSession.AdvanceTo(.001);
        BOOST_CHECK_SMALL(openSession.Snapshot().latest.values[0],1e-6);
    }
}
BOOST_FIXTURE_TEST_CASE( ManualWireRoutesRoundTripWithoutChangingTheCircuit, DESIGN_FILES )
{
    auto d=Divider();auto before=MODEL_REPOSITORY::LoadText(d.Export());
    d.wireRoutes.push_back({"","out","R1","n",{{240,200},{240,480},{500,480}}});
    auto restored=MODEL_DESIGN::Open(d.Export());BOOST_REQUIRE_EQUAL(restored.wireRoutes.size(),1);
    BOOST_CHECK(restored.wireRoutes[0].bends==d.wireRoutes[0].bends);
    const auto after=MODEL_REPOSITORY::LoadText(restored.Export());
    BOOST_CHECK(before.instance==after.instance);BOOST_CHECK(before.definitions==after.definitions);
}
BOOST_FIXTURE_TEST_CASE( EditableLedCorePreservesAppearanceBindingsAndBlockOverrides, DESIGN_FILES )
{
    auto d=MODEL_DESIGN::Open(templates.at("led"));d.MakeCoreVisible();d.id="custom_green_led";
    d.blocks[0].values["color_red"]="0";d.blocks[0].values["color_green"]="255";d.blocks[0].values["color_blue"]="0";
    d.blocks[0].values["display_intensity"]="35";d.blocks[0].values["circle_diameter"]="6";
    d.blocks[0].values["halo_diameter"]="9";d.blocks[0].values["display_opacity"]="70";
    auto restored=MODEL_DESIGN::Open(d.Export());
    BOOST_CHECK_EQUAL(restored.Export(),d.Export());
    CIRCUIT_COMPONENT led{"D1","Device:LED","LED",{{"1","0"},{"2","anode"}}};led.modelFile=Write(restored);
    const auto resolved=MODEL_REPOSITORY().Resolve(led);const auto appearance=ResolveLedAppearance(resolved.model.ledAppearance,resolved.parameters);
    BOOST_CHECK_EQUAL(appearance.red,0);BOOST_CHECK_EQUAL(appearance.green,1);BOOST_CHECK_EQUAL(appearance.blue,0);
    BOOST_CHECK_EQUAL(appearance.intensity,.35);BOOST_CHECK_EQUAL(appearance.diameterMm,6);
    BOOST_CHECK_EQUAL(appearance.haloDiameterMm,9);BOOST_CHECK_EQUAL(appearance.opacity,.7);
}
BOOST_AUTO_TEST_CASE( ModelWireRouterAvoidsBlocksAndPreservesManuallyMovedSegments )
{
    std::vector<DESIGN_TERMINAL> terminals={ {"A","out","net",{100,100},1}, {"B","in","net",{600,100},-1} };
    std::vector<DESIGN_OBSTACLE> obstacles={ {0,60,100,100},{250,20,180,200},{600,60,100,100} };
    auto wires=RouteDesignWires(terminals,obstacles,{});BOOST_REQUIRE_EQUAL(wires.size(),1);
    BOOST_CHECK(wires[0].points.front()==terminals[0].position);BOOST_CHECK(wires[0].points.back()==terminals[1].position);
    for(std::size_t i=1;i<wires[0].points.size();++i)
    {
        const auto a=wires[0].points[i-1],b=wires[0].points[i];BOOST_CHECK(a.x==b.x||a.y==b.y);
        if(a.x==b.x)BOOST_CHECK(!(a.x>250&&a.x<430&&std::max(a.y,b.y)>20&&std::min(a.y,b.y)<220));
        else BOOST_CHECK(!(a.y>20&&a.y<220&&std::max(a.x,b.x)>250&&std::min(a.x,b.x)<430));
    }
    DESIGN_WIRE_ROUTE manual{"A","out","B","in",{{180,100},{180,350},{540,350},{540,100}}};
    wires=RouteDesignWires(terminals,obstacles,{manual});BOOST_CHECK(wires[0].manual);
    BOOST_CHECK(std::find(wires[0].points.begin(),wires[0].points.end(),DESIGN_POINT{180,350})!=wires[0].points.end());
    terminals[0].position.y=140;wires=RouteDesignWires(terminals,obstacles,{manual});
    BOOST_CHECK(wires[0].points.front()==terminals[0].position);
    for(std::size_t i=1;i<wires[0].points.size();++i)BOOST_CHECK(wires[0].points[i-1].x==wires[0].points[i].x||wires[0].points[i-1].y==wires[0].points[i].y);
}
BOOST_AUTO_TEST_CASE( DraggedWireSegmentsStayOutsideBlocksAndFollowBothTerminals )
{
    const std::vector<DESIGN_TERMINAL> initial={ {"A","out","net",{340,170},1}, {"B","in","net",{700,170},-1} };
    const std::vector<DESIGN_OBSTACLE> boxes={ {100,80,240,220}, {700,80,240,220} };
    const auto original=RouteDesignWires(initial,boxes,{}).at(0);
    const auto dragged=MoveDesignWireSegment(original,0,70);
    BOOST_CHECK(dragged.points.front()==initial[0].position);
    BOOST_CHECK(dragged.points.back()==initial[1].position);
    const DESIGN_WIRE_ROUTE manual{ "A","out","B","in", {dragged.points.begin()+1,dragged.points.end()-1} };
    for( int moving=0; moving<2; ++moving ) for( const auto& offset:std::vector<DESIGN_POINT>{{0,0},{100,90},{-40,180},{600,500}} )
    {
        auto pins=initial; auto obstacles=boxes;
        pins[moving].position.x+=offset.x; pins[moving].position.y+=offset.y;
        obstacles[moving].x+=offset.x; obstacles[moving].y+=offset.y;
        const auto wire=RouteDesignWires(pins,obstacles,{manual}).at(0);
        BOOST_REQUIRE_GE(wire.points.size(),2);
        BOOST_CHECK(wire.points.front()==pins[0].position); BOOST_CHECK(wire.points.back()==pins[1].position);
        BOOST_CHECK_EQUAL(wire.points[1].y,pins[0].position.y);
        BOOST_CHECK_GT((wire.points[1].x-pins[0].position.x)*pins[0].direction,0);
        const auto beforeEnd=wire.points[wire.points.size()-2];
        BOOST_CHECK_EQUAL(beforeEnd.y,pins[1].position.y);
        BOOST_CHECK_GT((beforeEnd.x-pins[1].position.x)*pins[1].direction,0);
        for(std::size_t i=1;i<wire.points.size();++i)
        {
            const auto a=wire.points[i-1],b=wire.points[i];BOOST_CHECK(a.x==b.x||a.y==b.y);
            for(const auto& box:obstacles)
                if(a.x==b.x)BOOST_CHECK(!(a.x>box.x&&a.x<box.x+box.width&&std::max(a.y,b.y)>box.y&&std::min(a.y,b.y)<box.y+box.height));
                else BOOST_CHECK(!(a.y>box.y&&a.y<box.y+box.height&&std::max(a.x,b.x)>box.x&&std::min(a.x,b.x)<box.x+box.width));
        }
    }
    BOOST_CHECK_THROW(MoveDesignWireSegment(original,original.points.size(),10),std::out_of_range);
}
BOOST_AUTO_TEST_CASE( EveryEditableSegmentKeepsItsPinLeadsWhenMoved )
{
    std::vector<DESIGN_TERMINAL> pins={ {"A","p","net",{340,150},1}, {"B","p","net",{700,250},-1} };
    std::vector<DESIGN_OBSTACLE> boxes={ {100,60,240,220}, {700,160,240,220} };
    auto original=RouteDesignWires(pins,boxes,{}).at(0);
    for(std::size_t i=0;i+1<original.points.size();++i)for(int offset:{-80,80})
    {
        const auto moved=MoveDesignWireSegment(original,i,offset);
        DESIGN_WIRE_ROUTE route{"A","p","B","p",{moved.points.begin()+1,moved.points.end()-1}};
        const auto wire=RouteDesignWires(pins,boxes,{route}).at(0);
        BOOST_CHECK(wire.points.front()==pins[0].position);BOOST_CHECK(wire.points.back()==pins[1].position);
        BOOST_CHECK_EQUAL(wire.points[1].y,150);BOOST_CHECK_GT(wire.points[1].x,340);
        BOOST_CHECK_EQUAL(wire.points[wire.points.size()-2].y,250);BOOST_CHECK_LT(wire.points[wire.points.size()-2].x,700);
    }
}
BOOST_AUTO_TEST_CASE( ConnectedWireJunctionsIncludeBranchesAndCrossesButExcludeOtherNetsAndCorners )
{
    auto wire=[](std::string net,std::vector<DESIGN_POINT> points)
        { return DESIGN_WIRE{{"A","p",net,points.front(),1},{"B","p",net,points.back(),-1},points,true}; };
    const auto horizontal=wire("shared",{{10,100},{210,100}});
    const auto vertical=wire("shared",{{100,10},{100,210}});
    auto junctions=DesignWireJunctions({horizontal,vertical});BOOST_REQUIRE_EQUAL(junctions.size(),1);
    BOOST_CHECK(junctions[0].position==DESIGN_POINT({100,100}));BOOST_CHECK_EQUAL(junctions[0].net,"shared");
    auto branch=wire("shared",{{100,100},{100,200}});
    junctions=DesignWireJunctions({horizontal,branch});BOOST_REQUIRE_EQUAL(junctions.size(),1);
    BOOST_CHECK(junctions[0].position==DESIGN_POINT({100,100}));
    auto separate=vertical;separate.from.net=separate.to.net="other";
    BOOST_CHECK(DesignWireJunctions({horizontal,separate}).empty());
    auto elbow=wire("shared",{{10,100},{100,100},{100,200}});
    BOOST_CHECK(DesignWireJunctions({elbow}).empty());
    auto overlap=wire("shared",{{70,100},{170,100},{170,200}});
    junctions=DesignWireJunctions({horizontal,overlap});BOOST_REQUIRE_EQUAL(junctions.size(),1);
    BOOST_CHECK(junctions[0].position==DESIGN_POINT({170,100}));
}
BOOST_FIXTURE_TEST_CASE( RemovingAnImportedCoreRemovesItsObsoleteIndicatorsAndRoutes, DESIGN_FILES )
{
    auto d=MODEL_DESIGN::Open(templates.at("led"));d.MakeCoreVisible();
    d.wireRoutes.push_back({"","a","Core","a",{{250,200}}});
    d.RemoveBlock("Core");BOOST_CHECK(d.blocks.empty());BOOST_CHECK(d.wireRoutes.empty());
    d.blocks.push_back({"Replacement",templates.at("ideal_diode"),{{"a","@a"},{"k","@k"}}, {}});
    auto replacement=MODEL_REPOSITORY::LoadText(d.Export());BOOST_CHECK(replacement.indicatorSignal.empty());
    BOOST_CHECK(replacement.controlSource.empty());BOOST_CHECK(replacement.logicVcc.empty());
}
BOOST_AUTO_TEST_CASE( FanoutWiresUseShortBranchesAndKeepManualRoutesWhenBlocksMove )
{
    std::vector<DESIGN_TERMINAL> pins={ {"A","p","net",{100,100},1}, {"B","p","net",{400,100},1}, {"C","p","net",{800,100},-1} };
    auto wires=RouteDesignWires(pins,{},{});BOOST_REQUIRE_EQUAL(wires.size(),2);
    BOOST_CHECK(wires[0].from.block=="A"&&wires[0].to.block=="B");
    BOOST_CHECK(wires[1].from.block=="B"&&wires[1].to.block=="C");
    DESIGN_WIRE_ROUTE manual{"A","p","C","p",{{150,100},{150,300},{750,300},{750,100}}};
    pins[1].position={450,200};wires=RouteDesignWires(pins,{}, {manual});BOOST_REQUIRE_EQUAL(wires.size(),2);
    BOOST_CHECK(wires[0].manual);BOOST_CHECK_EQUAL(wires[0].to.block,"C");
}
BOOST_FIXTURE_TEST_CASE( IdealDiodeConductsForwardAndBlocksReverseVoltage, DESIGN_FILES )
{
    auto d=MODEL_DESIGN::Open(templates.at("ideal_diode"));d.MakeCoreVisible();d.id="condition_diode";
    CIRCUIT_COMPONENT u{"D1","Custom:Diode","diode",{{"1","anode"},{"2","0"}}};u.modelFile=Write(d);
    auto c=BuildCircuitSnapshot({Source("V1","supply","+5V"),u,
        {"R1","Device:R","1k",{{"1","supply"},{"2","anode"}}}});
    c.config.signals={"v("+c.pinNodes[1].at("1")+")"};
    INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);s.Configure(c.netlist,c.config);s.AdvanceTo(.01);
    BOOST_CHECK_CLOSE(s.Snapshot().latest.values[0],.702149, .001);
    s.SetSourceValue("Vca1",-5);s.AdvanceTo(.02);BOOST_CHECK_CLOSE(s.Snapshot().latest.values[0],-5,.001);
}

BOOST_FIXTURE_TEST_CASE(SymbolVariantsKeepSeparatePinMapsControlsAndLuminousGeometry,DESIGN_FILES)
{
    auto design=MODEL_DESIGN::Open(templates.at("display_7seg_ca"));design.MakeCoreVisible();
    const auto originalSymbol=design.libraryId;BOOST_REQUIRE(!originalSymbol.empty());
    const auto originalMapping=design.SymbolPinMap();const auto blockConnections=design.blocks[0].connections;
    auto physical=design.pins;
    for(std::size_t i=0;i<physical.size();++i)physical[i].defaultPin=std::to_string(100+i);
    design.AddSymbol("Custom:OtherDisplay",physical);
    const auto otherMapping=design.SymbolPinMap();BOOST_CHECK(otherMapping!=originalMapping);
    for(std::size_t i=0;i<physical.size();++i)BOOST_CHECK_EQUAL(otherMapping.at(physical[i].id),physical[i].defaultPin);
    auto json=nlohmann::json::parse(design.baseJson);const auto originalVisual=json.at("segments")[0].at("visual");
    const auto segmentId=json.at("segments")[0].at("id").get<std::string>();
    json["segments"][0]["visual"]["x"]=.72;json["segments"][0]["red"]=.3;design.baseJson=json.dump();
    design.SelectSymbol(originalSymbol);BOOST_CHECK(design.SymbolPinMap()==originalMapping);
    BOOST_CHECK(nlohmann::json::parse(design.baseJson).at("segments")[0].at("visual")==originalVisual);
    BOOST_CHECK(design.blocks[0].connections==blockConnections);
    auto text=design.Export();auto model=MODEL_REPOSITORY::LoadText(text);
    const auto other=model.ForSymbol("Custom:OtherDisplay");
    BOOST_CHECK_CLOSE(other.segments[0].visual.x,.72,1e-10);BOOST_CHECK_CLOSE(other.segments[0].red,.3,1e-10);
    BOOST_CHECK_EQUAL(other.segments[0].signal,model.segments[0].signal);
    BOOST_CHECK_EQUAL(other.pins[0].defaultPin,physical[0].defaultPin);
    auto restored=MODEL_DESIGN::Open(text);restored.MakeCoreVisible();restored.SelectSymbol("Custom:OtherDisplay");
    BOOST_CHECK_CLOSE(nlohmann::json::parse(restored.baseJson).at("segments")[0].at("visual").at("x").get<double>(),.72,1e-10);
    restored.RemoveSymbol(originalSymbol);const auto removed=MODEL_REPOSITORY::LoadText(restored.Export());
    BOOST_CHECK(std::find(removed.libraryIds.begin(),removed.libraryIds.end(),originalSymbol)==removed.libraryIds.end());
    BOOST_CHECK(std::find(removed.excludedLibraryIds.begin(),removed.excludedLibraryIds.end(),originalSymbol)!=removed.excludedLibraryIds.end());
    BOOST_CHECK(restored.blocks[0].connections==blockConnections);
    auto invalid=nlohmann::json::parse(text);invalid["symbol_variants"]["Custom:OtherDisplay"]["pin_map"]["missing_port"]="999";
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(invalid.dump()),std::runtime_error);
    invalid=nlohmann::json::parse(text);auto& mapping=invalid["symbol_variants"]["Custom:OtherDisplay"]["pin_map"];
    for(auto& number:mapping)number="1";
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(invalid.dump()),std::runtime_error);
}
BOOST_FIXTURE_TEST_CASE(SymbolVariantsSwitchButtonsAndSlidersWithoutRewiringBlocks,DESIGN_FILES)
{
    for(const auto& id:{"switch","potentiometer"})
    {
        auto design=MODEL_DESIGN::Open(templates.at(id));design.MakeCoreVisible();design.ConfigureInteractions();
        BOOST_REQUIRE(!design.interactions.empty());const auto original=design.libraryId;
        if(original.empty()){design.AddSymbol("Custom:First",design.pins);}
        const auto first=design.libraryId;const auto x=design.interactions[0].visual.x;
        design.AddSymbol("Custom:Second",design.pins);design.interactions[0].visual.x=.63;design.interactions[0].visual.custom=true;
        design.SelectSymbol(first);BOOST_CHECK_CLOSE(design.interactions[0].visual.x,x,1e-10);
        const auto text=design.Export();auto model=MODEL_REPOSITORY::LoadText(text).ForSymbol("Custom:Second");
        if(!model.controls.empty())BOOST_CHECK_CLOSE(model.controls[0].visual.x,.63,1e-10);
        else{BOOST_REQUIRE(!model.adjustments.empty());BOOST_CHECK_CLOSE(model.adjustments[0].visual.x,.63,1e-10);}
        auto restored=MODEL_DESIGN::Open(text);restored.MakeCoreVisible();restored.SelectSymbol("Custom:Second");
        BOOST_CHECK_CLOSE(restored.interactions[0].visual.x,.63,1e-10);
    }
}
BOOST_FIXTURE_TEST_CASE(EditorProfileUpdatesKeepDefaultsAndCopiesDoNotStealOriginalLinks,DESIGN_FILES)
{
    MODEL_PROFILES profiles(directory/"profiles.json");const auto original=directory/"original.casim",copy=directory/"copy.casim";
    CIRCUIT_COMPONENT settings;settings.libraryId="Custom:One";settings.modelFile=original.string();
    settings.modelPins={{"a","1"},{"b","2"}};settings.modelParameters={{"resistance","17k"}};settings.measurements.voltage=true;
    profiles.Save(settings);auto second=settings;second.libraryId="Custom:Two";second.modelFile=copy.string();profiles.Save(second);
    std::map<std::string,std::map<std::string,std::string>> mappings={{"Custom:One",{{"a","2"},{"b","1"}}}};
    profiles.UpdateLinks(original,"test",mappings,{"Custom:Two"},true);
    CIRCUIT_COMPONENT fresh;fresh.libraryId="Custom:One";BOOST_REQUIRE(profiles.Apply(fresh));
    BOOST_CHECK_EQUAL(fresh.modelParameters.at("resistance"),"17k");BOOST_CHECK(fresh.measurements.voltage);BOOST_CHECK_EQUAL(fresh.modelPins.at("a"),"2");
    profiles.UpdateLinks(copy,"test",mappings,{},false);fresh={};fresh.libraryId="Custom:One";BOOST_REQUIRE(profiles.Apply(fresh));
    BOOST_CHECK_EQUAL(fresh.modelFile,original.string());
    profiles.UpdateLinks(original,"test",{}, {"Custom:One","Custom:Two"},true);
    fresh={};fresh.libraryId="Custom:One";BOOST_CHECK(!profiles.Apply(fresh));fresh.libraryId="Custom:Two";BOOST_REQUIRE(profiles.Apply(fresh));
    BOOST_CHECK_EQUAL(fresh.modelFile,copy.string());
}

BOOST_FIXTURE_TEST_CASE(SymbolLinksPreserveMatchingRulesAndRespectRemovalAndCopyIsolation,DESIGN_FILES)
{
    auto design=MODEL_DESIGN::Open(templates.at("resistor"));design.MakeCoreVisible();
    const auto original=nlohmann::json::parse(templates.at("resistor"));
    const auto symbol=design.libraryId;BOOST_REQUIRE(!symbol.empty());
    auto exported=nlohmann::json::parse(design.Export());
    for(const auto& [key,value]:original.at("matches").items())
        if(key!="library_ids")BOOST_CHECK(exported.at("matches").at(key)==value);
    design.RemoveSymbol(symbol);Write(design);MODEL_REPOSITORY repository({directory});
    CIRCUIT_COMPONENT component{"R1",symbol,"1k",{{"1","in"},{"2","0"}}};
    BOOST_CHECK_THROW(repository.Resolve(component),std::runtime_error);
    component.modelFile=(directory/(design.id+".casim")).string();BOOST_CHECK_NO_THROW(repository.Resolve(component));
    auto copy=design;copy.id="copy";copy.automaticAssociation=false;copy.AddSymbol("Custom:Copy",copy.pins);Write(copy);
    MODEL_REPOSITORY copies({directory});component.libraryId="Custom:Copy";component.modelFile.clear();
    BOOST_CHECK_THROW(copies.Resolve(component),std::runtime_error);
    component.modelFile=(directory/"copy.casim").string();BOOST_CHECK_NO_THROW(copies.Resolve(component));
}

BOOST_FIXTURE_TEST_CASE(EditedSwitchKeepsEachSymbolsNormallyOpenAndNormallyClosedState,DESIGN_FILES)
{
    auto design=MODEL_DESIGN::Open(templates.at("switch"));design.MakeCoreVisible();const auto file=Write(design);
    for(bool push:{true,false})
    {
        CIRCUIT_COMPONENT button{"S1",push?"Switch:SW_Push":"Switch:SW_SPST","Switch",{{"1","in"},{"2","out"}}};button.modelFile=file;
        auto circuit=BuildCircuitSnapshot({Source("V1","in","5"),button,{"R1","Device:R","1k",{{"1","out"},{"2","0"}}}});
        BOOST_REQUIRE_EQUAL(circuit.switches.size(),1);BOOST_CHECK_EQUAL(circuit.switches[0].initiallyClosed,!push);
        circuit.config.signals={"v("+circuit.pinNodes[1].at("2")+")"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(circuit.netlist,circuit.config);session.AdvanceTo(.001);
        const auto voltage=session.Snapshot().latest.values[0];if(push)BOOST_CHECK_SMALL(voltage,1e-4);else BOOST_CHECK_GT(voltage,4.99);
    }
}

namespace
{
struct DEFAULT_PROFILE_ENV
{
    std::string old=std::getenv("CIRCUITALIVE_PROFILE_FILE")?std::getenv("CIRCUITALIVE_PROFILE_FILE"):"";
    explicit DEFAULT_PROFILE_ENV(const fs::path& file){SetRuntimeEnvironment("CIRCUITALIVE_PROFILE_FILE",PathUtf8(file));}
    ~DEFAULT_PROFILE_ENV(){if(old.empty())UnsetRuntimeEnvironment("CIRCUITALIVE_PROFILE_FILE");else SetRuntimeEnvironment("CIRCUITALIVE_PROFILE_FILE",old);}
};
}
BOOST_FIXTURE_TEST_CASE(MakeDefaultWritesTheLiveModelAndAdvancedEditorSeesIt,DESIGN_FILES)
{
    const auto profileFile=directory/"default-bindings.json";DEFAULT_PROFILE_ENV environment(profileFile);MODEL_PROFILES profiles(profileFile);
    auto design=Divider();const auto connections=design.blocks[0].connections;const auto file=Write(design);
    auto model=MODEL_REPOSITORY::Load(file);
    CIRCUIT_COMPONENT component{"U1","Custom:LinkedVariant","1k",{{"10","in"},{"20","0"},{"30","out"}}};
    component.modelPins={{"in","30"},{"out","10"},{"gnd","20"}};component.modelParameters={{"upper","2k"}};
    component.measurements={true,true,true,"in","gnd"};
    profiles.MakeDefault(component,model,true);
    BOOST_REQUIRE(profiles.BoundModel(component.libraryId));BOOST_CHECK_EQUAL(*profiles.BoundModel(component.libraryId),file);
    BOOST_CHECK(profiles.DefaultExcluded(component.libraryId));
    auto fresh=component;fresh.modelFile.clear();fresh.modelParameters.clear();fresh.modelPins.clear();fresh.measurements={};
    BOOST_REQUIRE(profiles.Apply(fresh));BOOST_CHECK(fresh.modelParameters.empty());BOOST_CHECK(fresh.modelPins.empty());BOOST_CHECK(fresh.measurements.power);
    auto resolved=MODEL_REPOSITORY().Resolve(fresh);BOOST_CHECK_EQUAL(resolved.parameterText.at("upper"),"2k");BOOST_CHECK_EQUAL(resolved.pins.at("in"),"30");
    std::ifstream input(file);const std::string text(std::istreambuf_iterator<char>(input),{});auto edited=MODEL_DESIGN::Open(text);
    BOOST_CHECK_EQUAL(edited.parameters[0].defaultValue,"1k");BOOST_CHECK(edited.blocks[0].connections==connections);
    edited.SelectSymbol(component.libraryId);BOOST_CHECK_EQUAL(edited.SymbolPinMap().at("in"),"30");
    auto parameters=edited.SymbolParameters();BOOST_CHECK_EQUAL(parameters[0].defaultValue,"2k");
    parameters[0].defaultValue="3k";edited.SetSymbolParameters(parameters);std::ofstream(file)<<edited.Export();
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(fresh).parameterText.at("upper"),"3k");
    auto automatic=fresh;automatic.modelFile.clear();BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(automatic).model.id,design.id);
    auto second=design;second.id="other_divider";const auto otherFile=Write(second);auto instance=automatic;instance.modelFile=otherFile;
    instance.modelParameters={{"upper","4k"}};BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(instance).parameterText.at("upper"),"4k");
    BOOST_CHECK_EQUAL(*profiles.BoundModel(component.libraryId),file); // Choosing an instance model does not persist defaults.
    profiles.MakeDefault(instance,MODEL_REPOSITORY::Load(otherFile));BOOST_CHECK_EQUAL(*profiles.BoundModel(component.libraryId),otherFile);
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(automatic).model.id,second.id);
}
BOOST_FIXTURE_TEST_CASE(MakeDefaultKeepsDifferentLedsInOneModel,DESIGN_FILES)
{
    const auto profileFile=directory/"led-defaults.json";DEFAULT_PROFILE_ENV environment(profileFile);MODEL_PROFILES profiles(profileFile);
    auto design=MODEL_DESIGN::Open(templates.at("led"));design.MakeCoreVisible();const auto file=Write(design);
    const auto original=MODEL_REPOSITORY::Load(file);
    BOOST_REQUIRE(!original.brightnessParameter.empty());
    const auto parameter=original.brightnessParameter;
    CIRCUIT_COMPONENT diffuse{"D1","Custom:LED_Diffuse","LED",{{"1","0"},{"2","led"}}};
    for(const auto& pin:original.pins)diffuse.modelPins[pin.id]=pin.defaultPin;
    diffuse.modelParameters={{parameter,"10m"},{"forward_voltage","2"},{"color_red","255"},{"color_green","0"}};
    auto bright=diffuse;bright.reference="D2";bright.libraryId="Custom:LED_Bright";bright.modelParameters={{parameter,"20m"},{"forward_voltage","3"},{"color_red","0"},{"color_green","255"}};
    profiles.MakeDefault(diffuse,original);profiles.MakeDefault(bright,MODEL_REPOSITORY::Load(file));
    auto freshDiffuse=diffuse,freshBright=bright;
    for(auto* c:{&freshDiffuse,&freshBright}){c->modelParameters.clear();c->modelPins.clear();BOOST_REQUIRE(profiles.Apply(*c));}
    const auto diffuseModel=MODEL_REPOSITORY().Resolve(freshDiffuse),brightModel=MODEL_REPOSITORY().Resolve(freshBright);
    BOOST_CHECK_EQUAL(diffuseModel.model.file,brightModel.model.file);BOOST_CHECK_EQUAL(diffuseModel.model.id,brightModel.model.id);
    BOOST_CHECK_EQUAL(diffuseModel.parameterText.at(parameter),"10m");BOOST_CHECK_EQUAL(brightModel.parameterText.at(parameter),"20m");
    const auto diffuseAppearance=ResolveLedAppearance(diffuseModel.model.ledAppearance,diffuseModel.parameters);
    const auto brightAppearance=ResolveLedAppearance(brightModel.model.ledAppearance,brightModel.parameters);
    BOOST_CHECK_EQUAL(diffuseAppearance.red,1);BOOST_CHECK_EQUAL(diffuseAppearance.green,0);
    BOOST_CHECK_EQUAL(brightAppearance.red,0);BOOST_CHECK_EQUAL(brightAppearance.green,1);
    auto poweredBright=freshBright;poweredBright.pins["2"]="bright";
    const auto circuit=BuildCircuitSnapshot({Source("V1","supply","5"),
        {"R1","Device:R","330",{{"1","supply"},{"2","led"}}},freshDiffuse,
        {"R2","Device:R","330",{{"1","supply"},{"2","bright"}}},poweredBright});
    auto config=circuit.config;config.signals={"v("+circuit.pinNodes[2].at("2")+")","v("+circuit.pinNodes[4].at("2")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(circuit.netlist,config);session.AdvanceTo(.001);
    const auto sample=session.Snapshot().latest;
    BOOST_CHECK_SMALL(sample.values[0]-2,.1);BOOST_CHECK_SMALL(sample.values[1]-3,.1);
    const auto saved=MODEL_REPOSITORY::Load(file);
    BOOST_CHECK(saved.instance==original.instance);BOOST_CHECK(saved.definitions==original.definitions);
    for(std::size_t i=0;i<original.parameters.size();++i)BOOST_CHECK_EQUAL(saved.parameters[i].defaultValue,original.parameters[i].defaultValue);
    auto automatic=bright;automatic.modelParameters.clear();automatic.modelPins.clear();automatic.modelFile.clear();
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(automatic).parameterText.at(parameter),"20m");
    freshDiffuse.modelParameters[parameter]="15m";BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(freshDiffuse).parameterText.at(parameter),"15m");
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(freshBright).parameterText.at(parameter),"20m");
    // Saving only another field must keep this symbol's existing defaults.
    diffuse.modelParameters.clear();profiles.MakeDefault(diffuse,saved);
    freshDiffuse.modelParameters.clear();BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(freshDiffuse).parameterText.at(parameter),"10m");
    std::ifstream input(file);auto editor=MODEL_DESIGN::Open(std::string(std::istreambuf_iterator<char>(input),{}));
    const auto blocks=editor.blocks.size();
    editor.SelectSymbol(diffuse.libraryId);auto parameters=editor.SymbolParameters();
    auto current=std::find_if(parameters.begin(),parameters.end(),[&](const auto& p){return p.id==parameter;});
    BOOST_REQUIRE(current!=parameters.end());BOOST_CHECK_EQUAL(current->defaultValue,"10m");
    current->defaultValue="12m";editor.SetSymbolParameters(parameters);
    editor.SelectSymbol(bright.libraryId);
    for(const auto& p:editor.SymbolParameters())if(p.id==parameter)BOOST_CHECK_EQUAL(p.defaultValue,"20m");
    editor.SetSymbolParameters(editor.SymbolParameters()); // A UI collect with no edits must not change other symbols.
    editor.AddSymbol("Custom:LED_New",editor.pins);
    const auto baseline=original.ForSymbol("Custom:LED_New");
    for(std::size_t i=0;i<baseline.parameters.size();++i)BOOST_CHECK_EQUAL(editor.SymbolParameters()[i].defaultValue,baseline.parameters[i].defaultValue);
    BOOST_CHECK_EQUAL(editor.blocks.size(),blocks);std::ofstream(file)<<editor.Export();
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(freshDiffuse).parameterText.at(parameter),"12m");
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(freshBright).parameterText.at(parameter),"20m");
}
BOOST_FIXTURE_TEST_CASE(SymbolParameterDefaultsRejectInvalidValuesAndFollowValue,DESIGN_FILES)
{
    auto json=nlohmann::json::parse(Divider().Export());
    auto& variant=json["symbol_variants"]["Custom:Divider"];
    variant["parameter_defaults"]={{"upper","$value"}};
    const auto file=directory/"value-default.casim";
    std::ofstream(file)<<json.dump();
    CIRCUIT_COMPONENT component{"U1","Custom:Divider","3k",{{"10","in"},{"20","out"},{"30","0"}}};component.modelFile=file.string();
    BOOST_CHECK_EQUAL(MODEL_REPOSITORY().Resolve(component).parameters.at("upper"),3000);
    for(const auto& value:{"0","2Meg","oops",""})
    {
        variant["parameter_defaults"]["upper"]=value;
        BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(json.dump()),std::runtime_error);
    }
    variant["parameter_defaults"]={{"missing","1k"}};
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(json.dump()),std::runtime_error);
    variant["parameter_defaults"]={{"upper",1000}};
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(json.dump()),std::runtime_error);
    auto editor=Divider();auto parameters=editor.SymbolParameters();parameters[0].defaultValue="2k";editor.SetSymbolParameters(parameters);
    editor.blocks[0].values["resistance"]="2k";editor.SetSymbolParameters({});
    BOOST_CHECK(MODEL_REPOSITORY::LoadText(editor.Export()).parameters.empty());
}
BOOST_FIXTURE_TEST_CASE(InvalidDefaultSettingsLeaveModelAndBindingUntouched,DESIGN_FILES)
{
    MODEL_PROFILES profiles(directory/"invalid-defaults.json");auto design=Divider();const auto file=Write(design);const auto model=MODEL_REPOSITORY::Load(file);
    CIRCUIT_COMPONENT component{"U1","Custom:Invalid","1k",{{"10","in"},{"20","out"},{"30","0"}}};
    std::ifstream input(file);const std::string before(std::istreambuf_iterator<char>(input),{});
    for(bool pins:{false,true})
    {
        component.modelParameters=pins?std::map<std::string,std::string>{}:std::map<std::string,std::string>{{"upper","100Meg"}};
        component.modelPins=pins?std::map<std::string,std::string>{{"in","10"},{"out","10"},{"gnd","30"}}:std::map<std::string,std::string>{};
        BOOST_CHECK_THROW(profiles.MakeDefault(component,model),std::runtime_error);
        std::ifstream verify(file);BOOST_CHECK_EQUAL(std::string(std::istreambuf_iterator<char>(verify),{}),before);
        BOOST_CHECK(!profiles.BoundModel(component.libraryId));
    }
}
