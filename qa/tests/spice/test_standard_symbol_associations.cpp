/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
using namespace CIRCUITALIVE;
namespace
{
MODEL_REPOSITORY BundledOnly()
{
    const auto installed=std::getenv("CA_STANDARD_ASSOCIATION_MODELS");
    return MODEL_REPOSITORY({installed?std::filesystem::path(installed):MODEL_REPOSITORY::DefaultDirectories().back()});
}
CIRCUIT_COMPONENT Source(std::string ref,std::string net,double voltage)
{ return {ref,"Simulation_SPICE:VDC",std::to_string(voltage),{{"1",net},{"2","0"}}}; }
CIRCUIT_COMPONENT R(std::string ref,std::string a,std::string b,std::string value="1k")
{ return {ref,"Device:R",value,{{"1",a},{"2",b}}}; }
CIRCUIT_COMPONENT Part(const std::string& lib)
{
    CIRCUIT_COMPONENT u{"U1",lib,"1k",{}};
    for(const auto& p:BundledOnly().SelectModel(u).pins)u.pins[p.defaultPin]="";
    return u;
}
std::string V(const CIRCUIT_SNAPSHOT& c,std::size_t part,const std::string& pin)
{ return "v("+c.pinNodes.at(part).at(pin)+")"; }
void Configure(CIRCUIT_SNAPSHOT& c)
{ c.config.step=c.config.maxStep=1e-6;c.config.horizon=.1;c.config.useInitialConditions=false; }
struct TWO_PIN{std::string name;bool closed;};
const std::vector<TWO_PIN> switches{{"SW_SPST",true},{"SW_Push",false},{"SW_Push_45deg",false},
 {"SW_Push_Open",true},{"SW_DIP_x01",false},{"SW_Omron_B3FS",false},{"SW_Reed",false},
 {"SW_Reed_Opener",true},{"SW_SPST_Temperature_NO",false},{"SW_SPST_Temperature_NC",true}};
}

BOOST_AUTO_TEST_CASE(StandardBindingsUseBundledDefaultsWithoutProfilesOrStoredFields)
{
    const auto repository=BundledOnly();
    const std::vector<std::pair<std::string,std::vector<std::string>>> families{
      {"resistor",{"R","R_45deg","R_Small","R_Small_US","R_US","R_Trim","R_Variable","R_Variable_US",
          "Thermistor","Thermistor_US","Thermistor_NTC","Thermistor_NTC_US","Thermistor_PTC","Thermistor_PTC_US"}},
      {"capacitor",{"C","C_45deg","C_Polarized","C_Polarized_Small","C_Polarized_Small_US","C_Polarized_US",
          "C_Small","C_Small_US","C_US","C_Trim","C_Trim_Small","C_Variable"}},
      {"inductor",{"L","L_Small","L_45deg","L_Ferrite","L_Ferrite_Small","L_Iron","L_Iron_Small","L_Polarized","L_Trim"}},
      {"diode",{"D","D_Small","D_45deg","D_45deg_Filled","D_Filled","D_Small_Filled"}},
      {"led",{"LED","LED_Small","LED_45deg","LED_45deg_Filled","LED_Filled","LED_Small_Filled"}}};
    for(const auto& [model,names]:families)for(const auto& name:names)BOOST_TEST_CONTEXT(name)
    {
        CIRCUIT_COMPONENT u{"U1","Device:"+name,model=="capacitor"?"1u":"1k",{{"1","a"},{"2","b"}}};
        BOOST_CHECK_EQUAL(repository.Resolve(u).model.id,model);
        BOOST_CHECK(u.modelFile.empty());BOOST_CHECK(u.modelPins.empty());BOOST_CHECK(u.modelParameters.empty());
    }
    for(const auto& s:switches)BOOST_TEST_CONTEXT(s.name)
    {
        CIRCUIT_COMPONENT u{"SW1","Switch:"+s.name,s.name,{{"1","a"},{"2","b"}}};
        const auto resolved=repository.Resolve(u);
        BOOST_CHECK_EQUAL(resolved.model.id,"switch");BOOST_CHECK_EQUAL(resolved.controlInitiallyClosed,s.closed);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsNeverChooseByPinCountOrInheritedDrawingAlone)
{
    const auto repository=BundledOnly();
    for(const std::string name:{"Device:Fuse","Device:Crystal","Device:R_Photo","Device:D_Tunnel",
        "Device:DIAC","Switch:SW_SPDT_MSM","Custom:UnknownTwoTerminalPart"})BOOST_TEST_CONTEXT(name)
    {
        CIRCUIT_COMPONENT u{"U1",name,"1k",{{"1","a"},{"2","b"}}};
        BOOST_CHECK_THROW(repository.SelectModel(u),std::runtime_error);
    }
    // These share drawings with implemented parts, but have different functions/ratings.
    for(const std::string name:{"74xx:74LS160","74xx:74LS22","Regulator_Linear:AZ1117CH2-3.3",
        "Amplifier_Operational:AD8620"})BOOST_TEST_CONTEXT(name)
    {
        CIRCUIT_COMPONENT u{"U1",name,"1",{}};BOOST_CHECK_THROW(repository.SelectModel(u),std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsSpiceDiodeRetainsLiteralLibraryParametersAndNamedPartPriority)
{
    CIRCUIT_COMPONENT u{"D1","Simulation_SPICE:D","D",{{"1","0"},{"2","anode"}}};
    u.pinNames={{"1","K"},{"2","A"}};u.explicitSpiceModel=true;
    u.legacySpiceFields={{"Sim.Device","D"},{"Sim.Pins","1=K 2=A"},{"Sim.Params","rs=50m cjo=10p"}};
    const auto repository=BundledOnly();const auto generic=repository.Resolve(u);
    BOOST_CHECK_EQUAL(generic.model.id,"diode");
    BOOST_CHECK_SMALL(generic.parameters.at("series_resistance")-.05,1e-12);
    BOOST_CHECK_SMALL(generic.parameters.at("junction_capacitance")-10e-12,1e-18);
    u.modelParameters["series_resistance"]=".2";
    BOOST_CHECK_SMALL(repository.Resolve(u).parameters.at("series_resistance")-.2,1e-12);
    u.modelParameters.clear();u.value="1N4148";
    BOOST_CHECK_EQUAL(repository.Resolve(u).model.id,"diode_1n4148");
}

BOOST_AUTO_TEST_CASE(StandardBindingsAllTwoTerminalSwitchesStartCorrectlyAndToggle)
{
    std::vector<TWO_PIN> all=switches;
    for(const std::string name:{"SW_Push_Shielded","CK_KMS2xxG","CK_KMS2xxGP"})all.push_back({name,false});
    for(const auto& s:all)BOOST_TEST_CONTEXT(s.name)
    {
        auto u=Part("Switch:"+s.name);u.pins["1"]="feed";u.pins["2"]="out";
        const bool shield=u.pins.contains("SH");if(shield)u.pins["SH"]="shield";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","feed",5),u,R("RL","out","0")};
        if(shield)parts.push_back(Source("VSH","shield",-12));
        auto c=BuildCircuitSnapshot(parts);Configure(c);c.config.signals={V(c,1,"2"),"i(Vca1)"};
        if(shield)c.config.signals.push_back("i(Vca4)");
        BOOST_REQUIRE_EQUAL(c.switches.size(),1u);BOOST_CHECK_EQUAL(c.switches[0].initiallyClosed,s.closed);
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
        auto check=[&](bool closed)
        {
            const auto values=session.Snapshot().latest.values;
            BOOST_CHECK_SMALL(values[0]-(closed?5*1000/1000.01:0),1e-6);
            BOOST_CHECK_SMALL(-values[1]-(closed?5/1000.01:0),1e-8);
            if(shield)BOOST_CHECK_SMALL(values[2],1e-9);
        };
        check(s.closed);const auto& control=c.switches[0];
        session.SetSourceValue(control.controlSource,s.closed?control.off:control.on);session.AdvanceTo(2e-5);check(!s.closed);
        session.SetSourceValue(control.controlSource,s.closed?control.on:control.off);session.AdvanceTo(3e-5);check(s.closed);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsDpstKeepsPolesIsolatedAndGanged)
{
    for(const auto& s:std::vector<TWO_PIN>{{"SW_DPST",false},{"SW_DPST_x2",false},{"SW_DPST_Temperature",false},
      {"SW_Push_Dual",false},{"SW_Push_Dual_x2",false},{"SW_Push_Open_Dual",true},{"SW_Push_Open_Dual_x2",true}})BOOST_TEST_CONTEXT(s.name)
    {
        auto u=Part("Switch:"+s.name);u.pins={{"1","feed1"},{"2","out1"},{"3","feed2"},{"4","out2"}};
        auto c=BuildCircuitSnapshot({Source("V1","feed1",7),Source("V2","feed2",-3),u,R("R1","out1","0"),R("R2","out2","0")});Configure(c);
        c.config.signals={V(c,2,"2"),V(c,2,"4")};BOOST_REQUIRE_EQUAL(c.switches.size(),1u);
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
        auto check=[&](bool closed){const auto v=session.Snapshot().latest.values;BOOST_CHECK_SMALL(v[0]-(closed?7*1000/1000.01:0),1e-6);BOOST_CHECK_SMALL(v[1]-(closed?-3*1000/1000.01:0),1e-6);};
        check(s.closed);session.SetSourceValue(c.switches[0].controlSource,s.closed?0:5);session.AdvanceTo(2e-5);check(!s.closed);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsSpdtVariantsUseTheActualCommonPinAndDpdtStaysIsolated)
{
    struct SPEC{std::string name;std::vector<std::array<int,3>> poles;};
    const std::vector<SPEC> specs{{"SW_SPDT",{{2,1,3}}},{"SW_Push_SPDT",{{2,1,3}}},{"SW_Reed_SPDT",{{2,1,3}}},
      {"SW_SPDT_201",{{0,2,1}}},{"SW_SPDT_312",{{1,3,2}}},{"SW_Wuerth_450301014042",{{1,3,2}}},
      {"SW_SPDT_XKB_DMx-xxxx-1",{{1,3,2}}},{"SW_SPDT_321",{{2,3,1}}},{"SW_Nidec_CAS-120A1",{{2,3,1}}},
      {"SW_DPDT_x2",{{2,1,3},{5,4,6}}},{"SW_Push_DPDT",{{2,1,3},{5,4,6}}},{"SW_Slide_DPDT",{{2,1,3},{5,4,6}}}};
    for(const auto& spec:specs)BOOST_TEST_CONTEXT(spec.name)
    {
        auto u=Part("Switch:"+spec.name);std::vector<CIRCUIT_COMPONENT> parts{u};
        for(unsigned p=0;p<spec.poles.size();++p)
        {
            const auto feed="feed"+std::to_string(p);parts[0].pins[std::to_string(spec.poles[p][0])]=feed;
            parts.push_back(Source("VS"+std::to_string(p),feed,p?-3:7));
            for(unsigned t=1;t<=2;++t)
            {
                const auto net="throw"+std::to_string(p)+std::to_string(t);
                parts[0].pins[std::to_string(spec.poles[p][t])]=net;parts.push_back(R("RL"+net,net,"0"));
            }
        }
        auto c=BuildCircuitSnapshot(parts);Configure(c);c.config.signals.clear();
        for(const auto& pole:spec.poles)for(unsigned t=1;t<=2;++t)c.config.signals.push_back(V(c,0,std::to_string(pole[t])));
        BOOST_REQUIRE_EQUAL(c.switches.size(),1u);INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
        auto check=[&](bool alternate){const auto v=session.Snapshot().latest.values;for(unsigned p=0;p<spec.poles.size();++p)for(unsigned t=0;t<2;++t)
          BOOST_CHECK_SMALL(v[2*p+t]-((bool(t)==alternate)?(p?-3:7)*1000/1000.01:0),1e-6);};
        check(false);session.SetSourceValue(c.switches[0].controlSource,5);session.AdvanceTo(2e-5);check(true);
        session.SetSourceValue(c.switches[0].controlSource,0);session.AdvanceTo(3e-5);check(false);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsDipBanksHaveIndependentControlsAndOppositeRowPairs)
{
    for(unsigned count=2;count<=12;++count)BOOST_TEST_CONTEXT(count)
    {
        const auto suffix=(count<10?"0":"")+std::to_string(count);auto u=Part("Switch:SW_DIP_x"+suffix);
        std::vector<CIRCUIT_COMPONENT> parts;
        for(unsigned i=1;i<=count;++i)
        {
            const auto feed="feed"+std::to_string(i),out="out"+std::to_string(i);
            u.pins[std::to_string(i)]=feed;u.pins[std::to_string(2*count+1-i)]=out;
            parts.push_back(Source("VS"+std::to_string(i),feed,i%2?double(i):-double(i)));
        }
        parts.push_back(u);for(unsigned i=1;i<=count;++i)parts.push_back(R("RL"+std::to_string(i),"out"+std::to_string(i),"0"));
        auto c=BuildCircuitSnapshot(parts);Configure(c);c.config.signals.clear();
        for(unsigned i=1;i<=count;++i)c.config.signals.push_back(V(c,count,std::to_string(2*count+1-i)));
        BOOST_REQUIRE_EQUAL(c.switches.size(),count);
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);double time=1e-5;session.AdvanceTo(time);
        for(unsigned selected=0;selected<count;++selected)
        {
            if(selected)session.SetSourceValue(c.switches[selected-1].controlSource,0);
            session.SetSourceValue(c.switches[selected].controlSource,5);session.AdvanceTo(time+=1e-5);
            const auto v=session.Snapshot().latest.values;
            for(unsigned i=0;i<count;++i)BOOST_CHECK_SMALL(v[i]-(i==selected?((i%2?-1:1)*double(i+1))*1000/1000.01:0),1e-6);
        }
        for(const auto& control:c.switches)session.SetSourceValue(control.controlSource,5);session.AdvanceTo(time+=1e-5);
        for(unsigned i=0;i<count;++i)BOOST_CHECK_SMALL(session.Snapshot().latest.values[i]-((i%2?-1:1)*double(i+1))*1000/1000.01,1e-6);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsFourPinTactileRetainsPermanentCopperPairs)
{
  for(bool stacked:{false,true})BOOST_TEST_CONTEXT("stacked="<<stacked)
  {
    const auto copy=stacked?"feed":"copy",outcopy=stacked?"out":"outcopy";
    auto u=Part("Switch:SW_MEC_5G");u.pins={{"1","feed"},{"2",copy},{"3","out"},{"4",outcopy}};
    auto c=BuildCircuitSnapshot({Source("VS","feed",5),u,R("RP",copy,"0","20k"),R("R1","out","0"),R("R2",outcopy,"0","2k")});Configure(c);
    c.config.signals={V(c,1,"2"),V(c,1,"3")};
    if(!stacked)c.config.signals.push_back(V(c,1,"4"));
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
    auto v=session.Snapshot().latest.values;BOOST_CHECK_SMALL(v[0]-5,1e-6);BOOST_CHECK_SMALL(v[1],1e-6);
    if(!stacked)BOOST_CHECK_SMALL(v[2],1e-6);
    session.SetSourceValue(c.switches[0].controlSource,5);session.AdvanceTo(2e-5);v=session.Snapshot().latest.values;
    BOOST_CHECK_SMALL(v[0]-5,1e-6);BOOST_CHECK_GT(v[1],4.999);
    if(!stacked)BOOST_CHECK_SMALL(v[1]-v[2],1e-8);
    else BOOST_CHECK_EQUAL(c.pinNodes[1].at("3"),c.pinNodes[1].at("4"));
  }
}

BOOST_AUTO_TEST_CASE(StandardBindingsIlluminatedSwitchHasSeparateContactAndLedCircuit)
{
    for(const std::string name:{"SW_Push_LED","SW_SPST_LED","SW_MEC_5E","SW_MEC_5G_LED"})BOOST_TEST_CONTEXT(name)
    {
        auto u=Part("Switch:"+name);u.pins={{"1","feed"},{"2","out"},{"3","0"},{"4","anode"}};
        auto c=BuildCircuitSnapshot({Source("VS","feed",3),Source("VL","ledfeed",5),u,R("RL","out","0"),R("RLED","ledfeed","anode")});Configure(c);
        BOOST_REQUIRE_EQUAL(c.leds.size(),1u);BOOST_REQUIRE_EQUAL(c.switches.size(),1u);
        const auto led=c.leds[0].signal,out=c.config.signals.size();c.config.signals.push_back(V(c,2,"2"));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
        const auto initial=session.Snapshot().latest.values;BOOST_CHECK_GT(initial[led],.001);BOOST_CHECK_LT(initial[led],.004);
        const bool closed=name=="SW_SPST_LED";BOOST_CHECK_SMALL(initial[out]-(closed?3*1000/1000.01:0),1e-6);
        session.SetSourceValue(c.switches[0].controlSource,closed?0:5);session.AdvanceTo(2e-5);
        auto v=session.Snapshot().latest.values;BOOST_CHECK_SMALL(v[led]-initial[led],1e-9);BOOST_CHECK_SMALL(v[out]-(closed?0:3*1000/1000.01),1e-6);
        session.SetSourceValue("Vca2",0);session.AdvanceTo(3e-5);v=session.Snapshot().latest.values;
        BOOST_CHECK_SMALL(v[led],1e-8);BOOST_CHECK_SMALL(v[out]-(closed?0:3*1000/1000.01),1e-6);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsResistorPacksPreserveEveryIsolatedElementAndValue)
{
    for(unsigned count=2;count<=11;++count)for(bool sip:{false,true})for(bool split:{false,true})
    {
        if(sip&&count>7)continue;
        const auto name="R_Pack"+(count<10?std::string("0"):"")+std::to_string(count)+(sip?"_SIP":"")+(split?"_Split":"");
        BOOST_TEST_CONTEXT(name)
        {
            auto u=Part("Device:"+name);u.value="2k";std::vector<CIRCUIT_COMPONENT> parts;
            std::vector<unsigned> ends;
            for(unsigned i=1;i<=count;++i)
            {
                const unsigned a=sip?2*i-1:i,b=sip?2*i:2*count+1-i;ends.push_back(b);
                const auto feed="feed"+std::to_string(i),out="out"+std::to_string(i);
                u.pins[std::to_string(a)]=feed;u.pins[std::to_string(b)]=out;
                parts.push_back(Source("VS"+std::to_string(i),feed,i%2?double(i):-double(i)));
            }
            parts.push_back(u);for(unsigned i=1;i<=count;++i)parts.push_back(R("RL"+std::to_string(i),"out"+std::to_string(i),"0"));
            auto c=BuildCircuitSnapshot(parts);Configure(c);c.config.signals.clear();for(auto b:ends)c.config.signals.push_back(V(c,count,std::to_string(b)));
            INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
            for(unsigned i=0;i<count;++i)BOOST_CHECK_SMALL(session.Snapshot().latest.values[i]-((i%2?-1:1)*double(i+1))/3,1e-6);
            session.SetSourceValue("Vca1",6);session.AdvanceTo(2e-5);
            for(unsigned i=0;i<count;++i)BOOST_CHECK_SMALL(session.Snapshot().latest.values[i]-(i?((i%2?-1:1)*double(i+1))/3:2),1e-6);
        }
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsKelvinShuntsExposeTheActualSenseEnds)
{
    for(const std::string name:{"R_Shunt","R_Shunt_US"})for(bool joined:{false,true})BOOST_TEST_CONTEXT(name<<" joined="<<joined)
    {
        auto u=Part("Device:"+name);u.value="100m";u.pins={{"1","force"},{"2",joined?"force":"sensep"},{"3",joined?"0":"sensen"},{"4","0"}};
        auto c=BuildCircuitSnapshot({Source("VS","feed",2),R("RF","feed","force","1"),u});Configure(c);
        c.config.signals=joined?std::vector<std::string>{V(c,2,"1"),"i(Vca1)"}
            :std::vector<std::string>{V(c,2,"1"),V(c,2,"2"),V(c,2,"3"),"i(Vca1)"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
        const auto v=session.Snapshot().latest.values;BOOST_CHECK_SMALL(v[0]-2*.1/1.1,1e-7);
        if(joined)
        {
            BOOST_CHECK_EQUAL(c.pinNodes[2].at("1"),c.pinNodes[2].at("2"));
            BOOST_CHECK_EQUAL(c.pinNodes[2].at("3"),c.pinNodes[2].at("4"));
        }
        else {BOOST_CHECK_SMALL(v[1]-v[0],1e-9);BOOST_CHECK_SMALL(v[2],1e-9);}
        BOOST_CHECK_SMALL(-v[joined?1:3]-2/1.1,1e-7);
    }
}

BOOST_AUTO_TEST_CASE(StandardBindingsFixedDividersRespectTapNumbersAndExternalLoading)
{
    struct SPEC{std::string name;int top,tap,bottom;};
    for(const auto& s:std::vector<SPEC>{{"VoltageDivider",1,2,3},{"VoltageDivider_CenterPin1",2,1,3},{"VoltageDivider_CenterPin3",1,3,2}})BOOST_TEST_CONTEXT(s.name)
    {
        auto u=Part("Device:"+s.name);u.pins[std::to_string(s.top)]="feed";u.pins[std::to_string(s.tap)]="out";u.pins[std::to_string(s.bottom)]="0";
        u.modelParameters={{"upper_resistance","3k"},{"lower_resistance","1k"}};
        auto c=BuildCircuitSnapshot({Source("VS","feed",5),u,R("RL","out","0")});Configure(c);c.config.signals={V(c,1,std::to_string(s.tap))};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-5);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-5*.5/3.5,1e-6);
    }
}
