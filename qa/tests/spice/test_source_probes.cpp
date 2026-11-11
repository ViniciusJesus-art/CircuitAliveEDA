/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/source_probe.h>
#include <circuitalive/control_references.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/simulation_model.h>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
using namespace CIRCUITALIVE;

namespace
{
CIRCUIT_SNAPSHOT Bench( PROBE_KIND kind, const SOURCE_PROBE_SETTINGS& settings = {} )
{
    auto c = BuildCircuitSnapshot( { SourceProbeComponent( kind, settings, "#P1", "out" ),
            { "R1", "Device:R", "1k", {{"1","out"},{"2","0"}} } } );
    c.config.signals = { "v(" + c.pinNodes[0].at("1") + ")" };
    c.config.historyCapacity = 4096;
    c.config.historySignals.reset(); c.currentWarnings.clear(); c.logicPins.clear();
    return c;
}
double At( INTERACTIVE_SIM_SESSION& session, double time )
{
    session.AdvanceTo( time ); return session.Snapshot().latest.values.at(0);
}
template<typename P> EDITOR_SESSION_VIEW Await( SESSION_RUNNER& runner, P predicate )
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for(;;)
    {
        auto view = runner.View();
        BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
        if( predicate(view) ) return view;
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( std::chrono::milliseconds(5) );
    }
}
}

BOOST_AUTO_TEST_CASE( SourceProbeSettingsRoundTripAndRejectInvalidElectricalValues )
{
    for( auto kind : { PROBE_KIND::LOGIC, PROBE_KIND::PWM, PROBE_KIND::SINE, PROBE_KIND::SQUARE } )
    {
        SOURCE_PROBE_SETTINGS s; s.frequency = 4321; s.high = 3.3; s.initialHigh = true; s.duty = .123;
        const auto text = SerializeSourceProbe( kind, s );
        BOOST_CHECK_EQUAL( SerializeSourceProbe( kind, ParseSourceProbe( kind, text ) ), text );
        BOOST_CHECK( ParseProbeKind( ProbeKindName(kind) ) == kind );
        s.resistance = 0; BOOST_CHECK_THROW( ValidateSourceProbe(kind,s), std::runtime_error );
    }
    SOURCE_PROBE_SETTINGS s;
    s.frequency = 0; BOOST_CHECK_THROW( ValidateSourceProbe(PROBE_KIND::PWM,s), std::runtime_error );
    s.frequency = 1e6; s.rise = s.fall = 1e-6;
    BOOST_CHECK_THROW( ValidateSourceProbe(PROBE_KIND::PWM,s), std::runtime_error );
    s = {}; s.duty = 1.01; BOOST_CHECK_THROW( ValidateSourceProbe(PROBE_KIND::PWM,s), std::runtime_error );
    s = {}; s.high = s.low; BOOST_CHECK_THROW( ValidateSourceProbe(PROBE_KIND::LOGIC,s), std::runtime_error );
    s = {}; s.amplitude = std::numeric_limits<double>::quiet_NaN();
    BOOST_CHECK_THROW( ValidateSourceProbe(PROBE_KIND::SINE,s), std::runtime_error );
    BOOST_CHECK_THROW( ParseSourceProbe(PROBE_KIND::LOGIC,"{}"), std::exception );
}

BOOST_AUTO_TEST_CASE( LogicSourceProbeDrivesBothLevelsAndIncludesLoadedVoltageDrop )
{
    SOURCE_PROBE_SETTINGS s; s.low = .2; s.high = 3.3; s.resistance = 100; s.initialHigh = true;
    const auto c = Bench(PROBE_KIND::LOGIC,s);
    BOOST_REQUIRE_EQUAL(c.switches.size(),1u); BOOST_CHECK(c.switches[0].initiallyClosed);
    BOOST_CHECK_EQUAL(c.pinNodes[0].at("2"),"0");
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    BOOST_CHECK_CLOSE( At(session,.001), 3., .001 );
    session.SetSourceValue(c.switches[0].controlSource,c.switches[0].off);
    BOOST_CHECK_CLOSE( At(session,.002), .2/1.1, .001 );
    session.SetSourceValue(c.switches[0].controlSource,c.switches[0].on);
    BOOST_CHECK_CLOSE( At(session,.003), 3., .001 );
    BOOST_CHECK_EQUAL(session.Snapshot().generation,1u);
}

BOOST_AUTO_TEST_CASE( LogicSourceProbeRunnerTogglesWhilePausedAndRestartsAtConfiguredState )
{
    const auto c = Bench(PROBE_KIND::LOGIC);
    SESSION_RUNNER runner(CIRCUITALIVE_TEST_WORKER);runner.Start(c);
    Await(runner,[](const auto& v){return v.state==EDITOR_SESSION_STATE::RUNNING;});
    runner.Pause();const auto paused=Await(runner,[](const auto& v){return v.state==EDITOR_SESSION_STATE::PAUSED;});
    runner.ToggleSwitch(0);
    auto changed=Await(runner,[](const auto& v){return v.switches.at(0);});
    BOOST_CHECK_EQUAL(changed.sample.latest.time,paused.sample.latest.time);
    runner.Step();auto stepped=Await(runner,[&](const auto& v){return v.sample.latest.time>paused.sample.latest.time;});
    BOOST_CHECK_GT(stepped.sample.latest.values[0],4.9);
    runner.Stop();runner.Start(c);
    auto restarted=Await(runner,[](const auto& v){return v.state==EDITOR_SESSION_STATE::RUNNING;});
    BOOST_CHECK(!restarted.switches.at(0));
}

BOOST_AUTO_TEST_CASE( PulseSourceProbeRespectsFrequencyDutyDelayAndLiveDutyChanges )
{
    SOURCE_PROBE_SETTINGS s; s.duty=.25; s.delay=.001; s.rise=s.fall=1e-6;
    const auto c=Bench(PROBE_KIND::PWM,s);
    BOOST_REQUIRE_EQUAL(c.adjustments.size(),1u);BOOST_REQUIRE_EQUAL(c.liveUpdates.size(),1u);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    BOOST_CHECK_SMALL(At(session,.0005),1e-6);
    BOOST_CHECK_GT(At(session,.0011),4.9);BOOST_CHECK_SMALL(At(session,.0014),1e-6);
    const auto& control=c.adjustments[0];
    for(double duty:{.75,0.,1.,.5})
    BOOST_TEST_CONTEXT("duty=" << duty)
    {
        session.SetSourceValue(control.source,duty,LiveParameterChanges(c,{{control.source,duty}},control.source));
        const double start=std::ceil(session.Snapshot().latest.time*1000+.1)/1000;
        const double high=At(session,start+.0002),late=At(session,start+.0006),end=At(session,start+.0009);
        if(duty>0)BOOST_CHECK_GT(high,4.9);else BOOST_CHECK_SMALL(high,1e-6);
        if(duty>.6)BOOST_CHECK_GT(late,4.9);else BOOST_CHECK_SMALL(late,1e-6);
        if(duty>.9)BOOST_CHECK_GT(end,4.9);else BOOST_CHECK_SMALL(end,1e-6);
    }
    BOOST_CHECK_EQUAL(session.Snapshot().generation,1u);
    BOOST_CHECK_EQUAL(session.Snapshot().eventsApplied,4u);
}

BOOST_AUTO_TEST_CASE( SourceProbePulsePreservesConfiguredEdgesWhenTheyFitTheDutyCycle )
{
    SOURCE_PROBE_SETTINGS s;s.rise=.0006;s.fall=.00001;
    const auto c=Bench(PROBE_KIND::PWM,s);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    BOOST_CHECK_CLOSE(At(session,.0001),5/1.001/6,.01);
    BOOST_CHECK_CLOSE(At(session,.0003),2.5/1.001,.01);
    BOOST_CHECK_GT(At(session,.0007),4.9);
    BOOST_CHECK_SMALL(At(session,.0009),1e-6);
    const auto& control=c.adjustments[0];
    session.SetSourceValue(control.source,.9,LiveParameterChanges(c,{{control.source,.9}},control.source));
    BOOST_CHECK_CLOSE(At(session,.00105),5/1.001*(.00005/(.0006*.0002/.00061)),.01);
    BOOST_CHECK_GT(At(session,.0015),4.9);
}

BOOST_AUTO_TEST_CASE( SquareAndSineSourceProbesKeepTheirConfiguredLevelsAndPhase )
{
    SOURCE_PROBE_SETTINGS s; s.frequency=2000;s.low=-2;s.high=3;
    auto square=Bench(PROBE_KIND::SQUARE,s);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(square.netlist,square.config);
    BOOST_CHECK_CLOSE(At(session,.0001),3/1.001,.001);
    BOOST_CHECK_CLOSE(At(session,.0004),-2/1.001,.001);
    BOOST_CHECK_CLOSE(At(session,.0006),3/1.001,.001);
    session.Stop();
    s.amplitude=2;s.offset=1;s.phase=90;
    auto sine=Bench(PROBE_KIND::SINE,s);session.Configure(sine.netlist,sine.config);
    BOOST_CHECK_CLOSE(At(session,.0005),3/1.001,.01);
    BOOST_CHECK_SMALL(At(session,.00075)+1/1.001,.001);
    BOOST_CHECK_CLOSE(At(session,.001),3/1.001,.01);
    BOOST_CHECK_LE(sine.config.maxStep,1/(s.frequency*24));
}

BOOST_AUTO_TEST_CASE( SourceProbesHaveIndependentControlsAndFiniteCurrentIntoGround )
{
    SOURCE_PROBE_SETTINGS s;s.initialHigh=true;
    auto c=BuildCircuitSnapshot({SourceProbeComponent(PROBE_KIND::LOGIC,s,"#P1","a"),
        SourceProbeComponent(PROBE_KIND::LOGIC,{},"#P2","b"),
        SourceProbeComponent(PROBE_KIND::LOGIC,s,"#P3","0")});
    BOOST_REQUIRE_EQUAL(c.switches.size(),3u);
    c.config.signals={"v("+c.pinNodes[0].at("1")+")","v("+c.pinNodes[1].at("1")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
    BOOST_CHECK_CLOSE(session.Snapshot().latest.values[0],5,.001);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[1],1e-6);
    session.SetSourceValue(c.switches[1].controlSource,1);session.AdvanceTo(.002);
    BOOST_CHECK_CLOSE(session.Snapshot().latest.values[0],5,.001);
    BOOST_CHECK_CLOSE(session.Snapshot().latest.values[1],5,.001);
}

BOOST_AUTO_TEST_CASE( SourceProbeControlsStandardLogicInputsWithoutFloatingDriverState )
{
    CIRCUIT_COMPONENT gate{"U1","74xx:74HC04","74HC04",{}};
    auto model=MODEL_REPOSITORY().SelectModel(gate);
    for(const auto& pin:model.pins)gate.pins[pin.defaultPin]=pin.role=="input"?"0":"";
    gate.pins["1"]="input";gate.pins["2"]="output";gate.pins["14"]="vcc";gate.pins["7"]="0";
    auto c=BuildCircuitSnapshot({SourceProbeComponent(PROBE_KIND::LOGIC,{},"#P1","input"),
        {"VCC","Simulation_SPICE:VDC","5",{{"1","vcc"},{"2","0"}}},gate});
    c.config.signals={"v("+c.pinNodes[2].at("2")+")"};
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    BOOST_CHECK_GT(At(session,1e-6),4.8);
    session.SetSourceValue(c.switches[0].controlSource,1);
    BOOST_CHECK_LT(At(session,2e-6),.2);
    session.SetSourceValue(c.switches[0].controlSource,0);
    BOOST_CHECK_GT(At(session,3e-6),4.8);
}
