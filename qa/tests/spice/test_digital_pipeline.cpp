/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <string>
#include <vector>
using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( const std::string& ref, const std::string& net, const std::string& value )
{ return {ref,"Simulation_SPICE:VDC",value,{{"1",net},{"2","0"}}}; }
CIRCUIT_COMPONENT FlipFlops()
{
    return {"U1","74xx:74HC74","74HC74",{{"14","vcc"},{"7","0"},
        {"2","data"},{"3","clock"},{"4","vcc"},{"1","reset"},{"5","q1"},{"6","nq1"},
        {"12","q1"},{"11","clock"},{"10","vcc"},{"13","reset"},{"9","q2"},{"8","nq2"}}};
}
void Watch( CIRCUIT_SNAPSHOT& c, std::size_t ff )
{
    c.config.signals.clear();
    for(const std::string pin:{"3","5","9"})c.config.signals.push_back("v("+c.pinNodes[ff].at(pin)+")");
}
}
BOOST_AUTO_TEST_CASE( PipelineCommonClockCapturesPreviousStageOnFollowingEdge )
{
    for(const double step:{1e-4,1e-5,1e-8}) BOOST_TEST_CONTEXT("maxStep="<<step)
    {
        auto c=BuildCircuitSnapshot({Source("VS","vcc","5"),Source("VD","data","5"),
            Source("VC","clock","0"),Source("VR","reset","0"),FlipFlops()});Watch(c,4);
        c.config.step=c.config.maxStep=step;c.config.horizon=.001;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(1e-6);session.SetSourceValue("Vca4",5);session.AdvanceTo(2e-6);
        session.SetSourceValue("Vca3",5);session.AdvanceTo(3e-6);
        BOOST_CHECK_GT(session.Snapshot().latest.values[1],4.9);
        BOOST_CHECK_LT(session.Snapshot().latest.values[2],.1);
        session.SetSourceValue("Vca3",0);session.AdvanceTo(4e-6);
        BOOST_CHECK_LT(session.Snapshot().latest.values[2],.1);
        session.SetSourceValue("Vca3",5);session.AdvanceTo(5e-6);
        BOOST_CHECK_GT(session.Snapshot().latest.values[2],4.9);
    }
}
BOOST_AUTO_TEST_CASE( PipelineNe555ClockDoesNotRaceThroughCascadedFlipFlops )
{
    CIRCUIT_COMPONENT timer{"T1","Timer:NE555P","NE555P",{{"1","0"},{"2","timing"},{"3","clock"},
        {"4","vcc"},{"5","ctrl"},{"6","timing"},{"7","dis"},{"8","vcc"}}};
    auto c=BuildCircuitSnapshot({Source("VS","vcc","5"),Source("VD","data","5"),Source("VR","reset","0"),
        timer,FlipFlops(),{"RA","Device:R","10k",{{"1","vcc"},{"2","dis"}}},
        {"RB","Device:R","10k",{{"1","dis"},{"2","timing"}}},
        {"CT","Device:C","1u",{{"1","timing"},{"2","0"}}}});Watch(c,4);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    session.AdvanceTo(.005);session.SetSourceValue("Vca3",5);
    bool previous=session.Snapshot().latest.values[0]>2.5;unsigned edges=0;
    for(unsigned i=11;i<150;++i)
    {
        session.AdvanceTo(i*.0005);const auto s=session.Snapshot().latest;
        const bool high=s.values[0]>2.5;
        if(high&&!previous)
        {
            ++edges;
            BOOST_TEST_CONTEXT("rising edge "<<edges<<" at "<<s.time)
            {
                BOOST_CHECK_GT(s.values[1],4.9);
                if(edges==1)BOOST_CHECK_LT(s.values[2],.1);
                else {BOOST_CHECK_GT(s.values[2],4.9);break;}
            }
        }
        previous=high;
    }
    BOOST_CHECK_EQUAL(edges,2u);
}
BOOST_AUTO_TEST_CASE( PipelineFirstClockWithoutResetDoesNotCaptureNewFirstStageValue )
{
    for(const bool use555:{false,true}) BOOST_TEST_CONTEXT("555="<<use555)
    {
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","vcc","5"),Source("VD","data","5"),
            Source("VR","reset","5"),FlipFlops()};
        if(use555)
        {
            parts.push_back({"T1","Timer:NE555P","NE555P",{{"1","0"},{"2","timing"},{"3","clock"},
                {"4","vcc"},{"5","ctrl"},{"6","timing"},{"7","dis"},{"8","vcc"}}});
            parts.push_back({"RA","Device:R","10k",{{"1","vcc"},{"2","dis"}}});
            parts.push_back({"RB","Device:R","10k",{{"1","dis"},{"2","timing"}}});
            parts.push_back({"CT","Device:C","1u",{{"1","timing"},{"2","0"}}});
            parts.push_back({"CC","Device:C","10n",{{"1","ctrl"},{"2","0"}}});
            parts.push_back({"RL","Device:R","220",{{"1","clock"},{"2","led"}}});
            parts.push_back({"D1","Device:LED","LED",{{"1","0"},{"2","led"}}});
        }
        else parts.push_back(Source("VC","clock","0"));
        auto c=BuildCircuitSnapshot(parts);Watch(c,3);
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        if(!use555){session.AdvanceTo(1e-6);session.SetSourceValue("Vca5",5);}
        session.AdvanceTo(2e-6);const auto sample=session.Snapshot().latest;
        BOOST_TEST_MESSAGE("initial clock="<<sample.values[0]<<" q1="<<sample.values[1]<<" q2="<<sample.values[2]);
        BOOST_CHECK_GT(sample.values[1],4.5);BOOST_CHECK_LT(sample.values[2],3.5);
    }
}

BOOST_AUTO_TEST_CASE( PipelineSlowClockAndFeedbackCannotRippleAcrossOneHighPhase )
{
    for(const std::string rise:{"1n","1u","100u"}) BOOST_TEST_CONTEXT(rise)
    {
        auto c=BuildCircuitSnapshot({Source("VS","vcc","5"),Source("VD","data","5"),
            Source("VC","clock","0"),Source("VR","reset","0"),FlipFlops()});Watch(c,4);
        const auto start=c.netlist.find("\nVca3 ")+1;const auto end=c.netlist.find('\n',start);
        c.netlist.replace(start,end-start,"Vca3 "+c.pinNodes[2].at("1")+" 0 PULSE(0 5 .001 "+rise+" "+rise+" .001 .003)");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(.0005);session.SetSourceValue("Vca4",5);
        session.AdvanceTo(.0015);const auto sample=session.Snapshot().latest;
        BOOST_TEST_MESSAGE("rise="<<rise<<" clock="<<sample.values[0]<<" q1="<<sample.values[1]<<" q2="<<sample.values[2]);
        BOOST_CHECK_GT(sample.values[1],4.9);BOOST_CHECK_LT(sample.values[2],.1);
        session.AdvanceTo(.0045);BOOST_CHECK_GT(session.Snapshot().latest.values[2],4.9);
    }
}
BOOST_AUTO_TEST_CASE( PipelineThresholdRippleIsOneClockEdgeUntilClockReturnsLow )
{
    for( const bool hct : { false, true } ) BOOST_TEST_CONTEXT( "HCT=" << hct )
    {
    auto ff = FlipFlops();
    if( hct ) { ff.libraryId = "74xx:74HCT74"; ff.value = "74HCT74"; }
    auto c=BuildCircuitSnapshot({Source("VS","vcc","5"),Source("VD","data","5"),
        Source("VC","clock","0"),Source("VR","reset","0"),ff});Watch(c,4);
    const double high = hct ? 2.1 : 3.6, dip = hct ? 1.9 : 3.4;
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    session.AdvanceTo(1e-6);session.SetSourceValue("Vca4",5);session.AdvanceTo(2e-6);
    session.SetSourceValue("Vca3",high);session.AdvanceTo(3e-6);
    BOOST_CHECK_GT(session.Snapshot().latest.values[1],4.9);BOOST_CHECK_LT(session.Snapshot().latest.values[2],.1);
    // A small dip into the threshold band is still the same high clock phase.
    session.SetSourceValue("Vca3",dip);session.AdvanceTo(4e-6);
    session.SetSourceValue("Vca3",high);session.AdvanceTo(5e-6);
    BOOST_CHECK_LT(session.Snapshot().latest.values[2],.1);
    session.SetSourceValue("Vca3",0);session.AdvanceTo(6e-6);
    session.SetSourceValue("Vca3",5);session.AdvanceTo(7e-6);
    BOOST_CHECK_GT(session.Snapshot().latest.values[2],4.9);
    // Low data also takes two real rising edges to traverse the pipeline.
    session.SetSourceValue("Vca2",0);session.SetSourceValue("Vca3",0);session.AdvanceTo(8e-6);
    session.SetSourceValue("Vca3",5);session.AdvanceTo(9e-6);
    BOOST_CHECK_LT(session.Snapshot().latest.values[1],.1);
    BOOST_CHECK_GT(session.Snapshot().latest.values[2],4.9);
    session.SetSourceValue("Vca3",0);session.AdvanceTo(10e-6);
    session.SetSourceValue("Vca3",5);session.AdvanceTo(11e-6);
    BOOST_CHECK_LT(session.Snapshot().latest.values[2],.1);
    }
}
