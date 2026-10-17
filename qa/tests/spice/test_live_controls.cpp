/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/model_catalog_index.h>
#include <circuitalive/simulation_model.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <fstream>
#include <thread>
using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Pot( const std::string& name = "R_Potentiometer", bool dual = false, bool mount = false )
{
    CIRCUIT_COMPONENT p{ "RV1", "Device:" + name, "10k", {{"1","top"},{"2","wiper"},{"3","0"}} };
    if( dual ) { p.pins["4"]="top2";p.pins["5"]="wiper2";p.pins["6"]="0"; }
    if( mount ) p.pins["MP"]="";
    p.modelParameters["position"]=".25";return p;
}
CIRCUIT_SNAPSHOT Divider( CIRCUIT_COMPONENT pot, bool dual = false )
{
    std::vector<CIRCUIT_COMPONENT> parts{
        {"V1","Simulation_SPICE:VDC","10",{{"1","top"},{"2","0"}}},pot};
    if( dual ) parts.push_back({"V2","Simulation_SPICE:VDC","4",{{"1","top2"},{"2","0"}}});
    auto c=BuildCircuitSnapshot(parts);c.config.signals={"v("+c.pinNodes[1].at("2")+")"};
    if( dual ) c.config.signals.push_back("v("+c.pinNodes[1].at("5")+")");
    // This bench retains only its requested voltages, so discard automatic warning channels.
    c.currentWarnings.clear(); c.logicPins.clear();
    return c;
}
template<typename P> EDITOR_SESSION_VIEW Await( SESSION_RUNNER& runner, P predicate )
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;)
    {
        const auto view=runner.View();BOOST_REQUIRE_MESSAGE(view.state!=EDITOR_SESSION_STATE::ERROR,view.error);
        if(predicate(view))return view;
        BOOST_REQUIRE(std::chrono::steady_clock::now()<deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}

BOOST_AUTO_TEST_CASE( LivePotentiometerModelsAdjustEveryTrackWithoutRebuilding )
{
    struct VARIANT{std::string name;bool dual,mount;};
    for(const auto& spec:std::vector<VARIANT>{{"R_Potentiometer",false,false},{"R_Potentiometer_Dual",true,false},
        {"R_Potentiometer_MountingPin",false,true},{"R_Potentiometer_Dual_MountingPin",true,true}})
    BOOST_TEST_CONTEXT(spec.name)
    {
        auto c=Divider(Pot(spec.name,spec.dual,spec.mount),spec.dual);
        BOOST_REQUIRE_EQUAL(c.adjustments.size(),1u);BOOST_CHECK(c.switches.empty());
        BOOST_CHECK_EQUAL(c.adjustments[0].initial,.25);
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-2.5,1e-5);
        for(double value:{.8,0.,1.,.3})
        {
            const double before=session.Snapshot().latest.time;
            session.SetSourceValue(c.adjustments[0].source,value);session.AdvanceTo(before+.001);
            const auto sample=session.Snapshot();
            BOOST_CHECK_GT(sample.latest.time,before);BOOST_CHECK_SMALL(sample.latest.values[0]-10*value,1e-5);
            if(spec.dual)BOOST_CHECK_SMALL(sample.latest.values[1]-4*value,1e-5);
        }
    }
}

BOOST_AUTO_TEST_CASE( LivePotChangePreservesCapacitorStateAndLoading )
{
    auto c=BuildCircuitSnapshot({
        {"V1","Simulation_SPICE:VDC","5",{{"1","top"},{"2","0"}}},Pot(),
        {"R1","Device:R","10k",{{"1","wiper"},{"2","0"}}},
        {"C1","Device:C","1u",{{"1","wiper"},{"2","0"}}}});
    c.config.signals={"v("+c.pinNodes[1].at("2")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.03);
    const double before=session.Snapshot().latest.values[0];BOOST_CHECK_SMALL(before-5.0*2000/9500,.001);
    session.SetSourceValue(c.adjustments[0].source,.75);session.AdvanceTo(.03001);
    const auto immediate=session.Snapshot();BOOST_CHECK_GT(immediate.latest.time,.03);
    BOOST_CHECK_GT(immediate.latest.values[0],before);BOOST_CHECK_LT(immediate.latest.values[0]-before,.05);
    session.AdvanceTo(.1);BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-5*(7500.*10000/17500)/(2500+7500.*10000/17500),.001);
}

BOOST_AUTO_TEST_CASE( LiveRunnerCoalescesSliderChangesAndKeepsPauseResumeState )
{
    auto c=Divider(Pot());SESSION_RUNNER runner(CIRCUITALIVE_TEST_WORKER);runner.Start(c);
    Await(runner,[](const auto& v){return v.state==EDITOR_SESSION_STATE::RUNNING;});
    runner.Pause();auto paused=Await(runner,[](const auto& v){return v.state==EDITOR_SESSION_STATE::PAUSED;});
    for(unsigned i=0;i<400;++i)runner.SetAdjustment(0,double(i)/500);
    runner.SetAdjustment(0,.8);
    auto changed=Await(runner,[](const auto& v){return !v.adjustments.empty()&&v.adjustments[0]==.8;});
    BOOST_CHECK(changed.state==EDITOR_SESSION_STATE::PAUSED);
    BOOST_CHECK_EQUAL(changed.sample.latest.time,paused.sample.latest.time);
    runner.Resume();auto resumed=Await(runner,[&](const auto& v){return v.sample.latest.time>paused.sample.latest.time;});
    BOOST_CHECK_SMALL(resumed.sample.latest.values[0]-8,1e-5);
    runner.SetAdjustment(0,2);Await(runner,[](const auto& v){return v.adjustments[0]==1;});
    runner.SetAdjustment(0,-1);Await(runner,[](const auto& v){return v.adjustments[0]==0;});
    runner.SetAdjustment(0,std::numeric_limits<double>::quiet_NaN());runner.SetAdjustment(999,.5);
    runner.Stop();runner.SetAdjustment(0,.9);runner.Start(c);
    auto restarted=Await(runner,[](const auto& v){return v.state==EDITOR_SESSION_STATE::RUNNING;});
    BOOST_CHECK_EQUAL(restarted.adjustments[0],.25);
}

BOOST_AUTO_TEST_CASE( LiveRunnerSlowPlaybackAndPausedStepPreserveCircuitState )
{
    auto c = Divider( Pot() ); c.config.maxStep = c.config.step = 1e-6; c.config.horizon = 10;
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER ); runner.SetSpeed( .001 ); runner.Start( c );
    auto started = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING; } );
    BOOST_CHECK_EQUAL( started.speed, .001 );
    runner.Pause(); const auto paused = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } );
    runner.SetAdjustment( 0, .8 );
    Await( runner, []( const auto& v ) { return v.adjustments[0] == .8; } );
    runner.Step(); const auto stepped = Await( runner, [&]( const auto& v ) { return v.sample.latest.time > paused.sample.latest.time; } );
    BOOST_CHECK( stepped.state == EDITOR_SESSION_STATE::PAUSED );
    BOOST_CHECK_SMALL( stepped.sample.latest.time - paused.sample.latest.time - .00002, 2e-6 );
    BOOST_CHECK_SMALL( stepped.sample.latest.values[0] - 8, 1e-5 );
    runner.SetSpeed( .01 ); Await( runner, []( const auto& v ) { return v.speed == .01; } );
    runner.Step(); const auto faster = Await( runner, [&]( const auto& v ) { return v.sample.latest.time > stepped.sample.latest.time; } );
    BOOST_CHECK_SMALL( faster.sample.latest.time - stepped.sample.latest.time - .0002, 2e-6 );
    runner.SetSpeed( std::numeric_limits<double>::quiet_NaN() ); runner.SetSpeed( -1 );
    runner.Stop(); runner.Start( c );
    const auto restarted = Await( runner, []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::RUNNING; } );
    BOOST_CHECK_EQUAL( restarted.speed, .01 ); BOOST_CHECK_EQUAL( restarted.adjustments[0], .25 );
}

BOOST_AUTO_TEST_CASE( LiveAdjustmentSchemaRejectsInvalidRangesAndMissingParameters )
{
    const auto path=MODEL_REPOSITORY().SelectModel(Pot()).file;
    std::ifstream in(path);const std::string original(std::istreambuf_iterator<char>(in),{});
    auto replace=[&](const std::string& from,const std::string& to)
    {auto json=original;const auto at=json.find(from);BOOST_REQUIRE(at!=std::string::npos);json.replace(at,from.size(),to);return json;};
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(replace("\"parameter\": \"position\"","\"parameter\": \"missing\"")),std::runtime_error);
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(replace("\"step\": 0.01","\"step\": 0")),std::runtime_error);
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(replace("\"min\": 0,\n      \"max\": 1,\n      \"step\"","\"min\": 2,\n      \"max\": 1,\n      \"step\"")),std::runtime_error);
}

BOOST_AUTO_TEST_CASE( ModelCatalogSearchScalesToLargeLibrariesAndKeepsDistinctFamilies )
{
    std::vector<SIMULATION_MODEL> models;
    for(unsigned i=0;i<20000;++i)
    {
        SIMULATION_MODEL m;m.id="part_"+std::to_string(i);m.name="Part "+std::to_string(i);
        m.category=i%2?"Analog/Comparators":"Digital/Logic";m.family=i%2?"LM393":"74HC";
        m.libraryIds={"Test:"+m.id};models.push_back(std::move(m));
    }
    MODEL_CATALOG_INDEX index(models);
    BOOST_CHECK_EQUAL(index.Find("","Analog").size(),10000u);
    BOOST_CHECK(index.Find("","Analog/Comparator").empty());
    auto found=index.Find("TEST:part_13457 lm393");BOOST_REQUIRE_EQUAL(found.size(),1u);
    BOOST_CHECK_EQUAL(index.Records()[found[0]].model,13457u);
    BOOST_CHECK(index.Find("part_13457","Digital").empty());
    models[0].category.clear();models[0].family.clear();models[0].libraryIds={"User_Library:Special"};
    MODEL_CATALOG_INDEX fallback(models);BOOST_REQUIRE_EQUAL(fallback.Find("Special","User Library").size(),1u);
}
