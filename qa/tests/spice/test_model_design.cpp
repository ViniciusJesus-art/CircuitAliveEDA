/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/model_design.h>
#include <circuitalive/model_design_presets.h>
#include <circuitalive/model_routing.h>
#include <circuitalive/circuit_snapshot.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
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
BOOST_FIXTURE_TEST_CASE( VisibleCoresKeepAllBundledElectricalModelsAndTheirMetadata, DESIGN_FILES )
{
    for( const auto& [id, text] : templates )
    {
        BOOST_TEST_CONTEXT( id )
        {
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
            if( !original.indicatorSignal.empty() ) BOOST_CHECK( compiled.indicatorSignal.find("b_Core_") != std::string::npos );
            if( !original.controlSource.empty() ) BOOST_CHECK( compiled.controlSource.find("b_Core_") != std::string::npos );
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
