/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <circuitalive/visual_signals.h>
#include <algorithm>
#include <cmath>
#include <sstream>
using namespace CIRCUITALIVE;
namespace
{
const int hexInputs[]{1,3,5,9,11,13}, hexOutputs[]{2,4,6,8,10,12};
struct SCHMITT { std::string lib; double supply, low, high, outputHigh, outputLow; };
const std::vector<SCHMITT> schmitts{
    {"74xx:74HC14",4.5,1.4,2.38,4.5,0}, {"74xx:74LS14",5,.8,1.6,3.4,.2},
    {"4xxx:40106",5,1.9,2.9,5,0}, {"4xxx_IEEE:40106",5,1.9,2.9,5,0}};
CIRCUIT_COMPONENT Source( std::string ref, std::string net, double volts )
{ return {ref,"Simulation_SPICE:VDC",std::to_string(volts),{{"1",net},{"2","0"}}}; }
CIRCUIT_COMPONENT Device( const std::string& lib )
{
    CIRCUIT_COMPONENT u{"U1",lib,"1",{}};
    for(const auto& p:MODEL_REPOSITORY().SelectModel(u).pins)
        u.pins[p.defaultPin]=p.role=="input"?"input":"";
    u.pins["14"]="vcc";u.pins["7"]="0";return u;
}
std::string Voltage( const CIRCUIT_SNAPSHOT& c, std::size_t component, int pin )
{ return "v("+c.pinNodes.at(component).at(std::to_string(pin))+")"; }
void Configure( CIRCUIT_SNAPSHOT& c, double step=5e-9 )
{ c.config.step=c.config.maxStep=step;c.config.horizon=.01; }
void Insert( CIRCUIT_SNAPSHOT& c, const std::string& line )
{ c.netlist.insert(c.netlist.rfind(".end"),line+'\n'); }
void Pulse( CIRCUIT_SNAPSHOT& c, double period, double high=5 )
{
    const auto start=c.netlist.find("Vca2 ");BOOST_REQUIRE(start!=std::string::npos);
    const auto end=c.netlist.find('\n',start);
    std::ostringstream line;line<<"Vca2 "<<c.pinNodes[1].at("1")<<" 0 PULSE(0 "<<high
        <<" 1u 1n 1n "<<period/2-1e-9<<' '<<period<<')';
    c.netlist.replace(start,end-start,line.str());
}
const CURRENT_WARNING& Warning( const CIRCUIT_SNAPSHOT& c, const std::string& name )
{
    auto it=std::find_if(c.currentWarnings.begin(),c.currentWarnings.end(),[&](const auto& w){return w.name==name;});
    BOOST_REQUIRE(it!=c.currentWarnings.end());return *it;
}
}

BOOST_AUTO_TEST_CASE( ElectricalSchmittEveryPhysicalUnitRetainsStateInsideHysteresis )
{
    for(const auto& s:schmitts) BOOST_TEST_CONTEXT(s.lib)
    {
        auto u=Device(s.lib);std::vector<CIRCUIT_COMPONENT> parts{Source("VS","vcc",s.supply)};
        for(unsigned i=0;i<6;++i)
        { u.pins[std::to_string(hexInputs[i])]="in"+std::to_string(i);parts.push_back(Source("VD"+std::to_string(i),"in"+std::to_string(i),0)); }
        parts.push_back(u);auto c=BuildCircuitSnapshot(parts);Configure(c);c.config.signals.clear();
        for(int pin:hexOutputs)c.config.signals.push_back(Voltage(c,7,pin));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        double time=2e-6;session.AdvanceTo(time);
        for(unsigned unit=0;unit<6;++unit)
        {
            const double middle=(s.low+s.high)/2;
            for(const auto& [voltage,high]:std::vector<std::pair<double,bool>>{
                {s.high-.05,true},{s.high+.05,false},{middle,false},{s.low+.05,false},{s.low-.05,true}})
            {
                session.SetSourceValue("Vca"+std::to_string(unit+2),voltage);session.AdvanceTo(time+=2e-6);
                for(unsigned i=0;i<6;++i)
                    BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(i)-((i!=unit||high)?s.outputHigh:s.outputLow),.015);
            }
            session.SetSourceValue("Vca"+std::to_string(unit+2),0);session.AdvanceTo(time+=2e-6);
        }
    }
}

BOOST_AUTO_TEST_CASE( ElectricalSchmittInBandStartupAndPowerLossDoNotInventKnownState )
{
    for(const auto& s:schmitts) BOOST_TEST_CONTEXT(s.lib)
    {
        auto c=BuildCircuitSnapshot({Source("VS","vcc",s.supply),Source("VD","input",(s.low+s.high)/2),Device(s.lib)}, {},true);
        Configure(c);const auto pin=std::find_if(c.logicPins.begin(),c.logicPins.end(),[](const auto& p){return p.component==2&&p.pin=="2";});
        BOOST_REQUIRE(pin!=c.logicPins.end());
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(2e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::UNKNOWN);
        session.SetSourceValue("Vca2",s.supply);session.AdvanceTo(4e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::LOW);
        session.SetSourceValue("Vca1",1);session.SetSourceValue("Vca2",(s.low+s.high)/2);session.AdvanceTo(6e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::HIGH_Z);
        session.SetSourceValue("Vca1",s.supply);session.AdvanceTo(8e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::UNKNOWN);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalTtlAbsoluteThresholdsAndVisualActivityFollowSupplyRange )
{
    for(double supply:{4.75,5.0,5.25}) BOOST_TEST_CONTEXT(supply)
    {
        auto u=Device("74xx:74LS00");u.pins["2"]="vcc";
        auto c=BuildCircuitSnapshot({Source("VS","vcc",supply),Source("VD","input",.79),u},{},true);Configure(c);
        ConfigureVisualSignals(c);
        const auto pin=std::find_if(c.logicPins.begin(),c.logicPins.end(),[](const auto& p){return p.component==2&&p.pin=="1";});
        BOOST_REQUIRE(pin!=c.logicPins.end());
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(1e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::LOW);
        session.SetSourceValue("Vca2",2.01);session.AdvanceTo(2e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::HIGH);
        const auto output=std::find(c.config.signals.begin(),c.config.signals.end(),Voltage(c,2,3));
        BOOST_REQUIRE(output!=c.config.signals.end());
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(output-c.config.signals.begin())-.2,.01);
        session.SetSourceValue("Vca1",5.3);session.AdvanceTo(3e-6);
        BOOST_CHECK(PinLogicLevel(*pin,session.Snapshot().latest)==LOGIC_LEVEL::UNKNOWN);
    }
    // Activity thresholds must also use 0.8/2 V even when VCC is 5.25 V.
    SIGNAL_ACTIVITY_TRACKER tracker;SIM_ACTIVITY_MONITOR monitor{0,ACTIVITY_NO_SIGNAL,1,.16,.4,1,4.75,5.25,5};
    for(int i=0;i<6;++i)tracker.Observe(monitor,i*1e-6,{i%2?2.01:.79,5.25});
    BOOST_CHECK_CLOSE(tracker.Activity().period,2e-6,.01);
}

BOOST_AUTO_TEST_CASE( ElectricalTtlInputsDrawSignedDatasheetLoads )
{
    for(const std::string lib:{"74xx:74LS00","74xx:74LS14"}) BOOST_TEST_CONTEXT(lib)
    {
        auto u=Device(lib);for(auto& [pin,net]:u.pins)if(net=="input")net="vcc";u.pins["1"]="input";
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VD","input",.4),u});Configure(c);
        c.config.signals={"i(Vca2)"};INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(2e-6);BOOST_CHECK_CLOSE(session.Snapshot().latest.values.at(0),400e-6,.01);
        session.SetSourceValue("Vca2",2.7);session.AdvanceTo(4e-6);
        BOOST_CHECK_CLOSE(-session.Snapshot().latest.values.at(0),20e-6,.01);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalTtlFanoutLoadsOutputAndWarnsBeyondTwentyInputs )
{
    for(unsigned count:{20,21}) for(bool high:{false,true}) BOOST_TEST_CONTEXT(count<<" inputs, high="<<high)
    {
        auto driver=Device("74xx:74LS00");driver.pins["2"]="vcc";driver.pins["3"]="out";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","vcc",5),Source("VD","input",high?0:5),driver};
        unsigned remaining=count;
        for(unsigned chip=0;chip<3;++chip)
        {
            auto u=Device("74xx:74LS00");u.reference="U"+std::to_string(chip+2);
            for(auto& [pin,net]:u.pins)if(net=="input")
            {net=remaining?"out":"vcc";if(remaining)--remaining;}
            parts.push_back(u);
        }
        auto c=BuildCircuitSnapshot(parts);Configure(c);
        const auto& warning=Warning(c,std::string("U1 y1 ")+(high?"source":"sink"));
        const auto voltage=c.config.signals.size();c.config.signals.push_back(Voltage(c,2,3));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(20e-6);
        const auto values=session.Snapshot().latest.values;const double current=values.at(warning.signal);
        BOOST_CHECK_CLOSE(current,count*(high?20e-6:400e-6),.05);
        if(count==20)BOOST_CHECK_LE(current,warning.limit*1.0001);
        else BOOST_CHECK_GT(current,warning.limit*1.04);
        BOOST_CHECK_SMALL(values.at(voltage)-(high?3.4-current*150:.2+current*18.75),.002);
        BOOST_CHECK(high?values.at(voltage)>2:values.at(voltage)<.8);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalCmosOutputsMatchTypicalLoadedVoltagePoints )
{
    for(const std::string lib:{"74xx:74HC00","74xx:74HC14","4xxx:40106"}) for(bool high:{false,true}) BOOST_TEST_CONTEXT(lib<<" high="<<high)
    {
        const bool cd=lib=="4xxx:40106";const double supply=cd?5:4.5,current=cd?.001:.004;
        auto u=Device(lib);if(lib=="74xx:74HC00")u.pins["2"]="vcc";
        auto c=BuildCircuitSnapshot({Source("VS","vcc",supply),Source("VD","input",high?0:supply),u});Configure(c);
        const int output=lib=="74xx:74HC00"?3:2;const auto node=c.pinNodes[2].at(std::to_string(output));
        std::ostringstream load;load<<"Iload "<<(high?node:c.pinNodes[0].at("1"))<<' '<<(high?"0":node)<<' '<<current;Insert(c,load.str());
        // Keep default saved signals to exercise all native branch-current warning monitors.
        auto index=c.config.signals.size();c.config.signals.push_back("v("+node+")");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(3e-6);
        const double expected=cd?(high?4.6:.4):(high?4.32:.15);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(index)-expected,.01);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalPackageQuiescentCurrentDependsOnFamilyAndOutputState )
{
    struct CHIP{std::string lib;double high,low;};
    for(const auto& s:std::vector<CHIP>{{"74xx:74LS00",.0008,.0024},{"74xx:74LS14",.0086,.012},
        {"74xx:74HC00",1e-6,1e-6},{"74xx:74HC14",1e-6,1e-6},{"4xxx:40106",20e-9,20e-9}}) BOOST_TEST_CONTEXT(s.lib)
    {
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VD","input",0),Device(s.lib)});Configure(c);
        c.config.signals={"i(Vca1)"};INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(3e-6);BOOST_CHECK_SMALL(-session.Snapshot().latest.values.at(0)-s.high,1e-9);
        session.SetSourceValue("Vca2",5);session.AdvanceTo(6e-6);
        BOOST_CHECK_SMALL(-session.Snapshot().latest.values.at(0)-s.low,1e-9);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalSchmittPropagationIncludesExternalCapacitiveLoad )
{
    struct TIMING{std::string lib;double load,min,max;};
    for(const auto& s:std::vector<TIMING>{{"74xx:74HC14",15e-12,10e-9,18e-9},
        {"74xx:74LS14",15e-12,14e-9,24e-9},{"4xxx:40106",50e-12,125e-9,160e-9}}) BOOST_TEST_CONTEXT(s.lib)
    {
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VD","input",0),Device(s.lib)});Configure(c,5e-10);
        const auto output=c.pinNodes[2].at("2");std::ostringstream load;load<<"Cload "<<output<<" 0 "<<s.load;Insert(c,load.str());
        c.config.signals={Voltage(c,2,1),Voltage(c,2,2)};c.config.historyCapacity=4096;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(500e-9);
        const auto before=session.Snapshot().latest;const double middle=(before.values.at(1)+(s.lib=="74xx:74LS14"?.2:0))/2;
        session.SetSourceValue("Vca2",5);session.AdvanceTo(before.time+250e-9);
        double edge=0;for(const auto& sample:session.Snapshot().history)
            if(sample.time>before.time&&sample.values.at(1)<middle){edge=sample.time;break;}
        BOOST_REQUIRE_GT(edge,0);const double delay=edge-before.time;
        BOOST_TEST_MESSAGE(s.lib<<" loaded propagation="<<delay);
        BOOST_CHECK_GT(delay,s.min);BOOST_CHECK_LT(delay,s.max);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalCmosDynamicConsumptionTracksFrequencyEvenWithMaskedOutput )
{
    struct LOAD{bool nand;double capacitance;};
    for(const auto load:std::vector<LOAD>{{false,0},{false,20e-12},{true,0}})
      for(double period:{10e-6,1e-6}) BOOST_TEST_CONTEXT(load.nand<<" load="<<load.capacitance<<" period="<<period)
    {
        const bool nand=load.nand;
        auto u=Device(nand?"74xx:74HC00":"74xx:74HC14");
        for(auto& [pin,net]:u.pins)if(net=="input")net="0";u.pins["1"]="input";
        // NAND B=0 masks the changing input: CPD still consumes energy internally.
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VD","input",0),u});Configure(c,1e-9);Pulse(c,period);
        if(load.capacitance)
        {
            std::ostringstream line;line<<"Cexternal "<<c.pinNodes[2].at("2")<<" 0 "<<load.capacitance;Insert(c,line.str());
        }
        c.config.signals={"i(Vca1)"};c.config.historyCapacity=4096;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        std::vector<SIM_SAMPLE> history;
        for(double time=.5e-6;time<1e-6+4*period+600e-9;time+=.5e-6)
        {
            session.AdvanceTo(time);auto batch=session.Snapshot(true).history;
            history.insert(history.end(),batch.begin(),batch.end());
        }
        const double start=1e-6+period+100e-9,end=start+3*period;
        double charge=0,elapsed=0;
        for(std::size_t i=1;i<history.size();++i)
        {
            const auto& a=history[i-1];const auto& b=history[i];
            if(a.time>=start&&b.time<=end){const double dt=b.time-a.time;charge-=dt*(a.values[0]+b.values[0])/2;elapsed+=dt;}
        }
        BOOST_REQUIRE_GT(elapsed,2.9*period);
        const double dynamic=charge/elapsed-1e-6,expected=((nand?22e-12:7e-12)+load.capacitance)*5/period;
        BOOST_TEST_MESSAGE("dynamic ICC="<<dynamic<<" expected="<<expected);
        BOOST_CHECK_CLOSE(dynamic,expected,4);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalDcRatingsDoNotLatchNormalCapacitiveChargingAsOverload )
{
    for(const auto& s:schmitts) BOOST_TEST_CONTEXT(s.lib)
    {
        auto c=BuildCircuitSnapshot({Source("VS","vcc",s.supply),Source("VD","input",0),Device(s.lib)});Configure(c,1e-9);
        Pulse(c,1e-6,s.supply);Insert(c,"Cload "+c.pinNodes[2].at("2")+" 0 50p");
        const auto source=Warning(c,"U1 y1 source"),sink=Warning(c,"U1 y1 sink");
        c.config.signals={c.config.signals.at(source.signal),c.config.signals.at(sink.signal)};c.config.historyCapacity=4096;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(3e-6);
        double sourcePeak=0,sinkPeak=0;for(const auto& sample:session.Snapshot().history)
        {sourcePeak=std::max(sourcePeak,sample.values[0]);sinkPeak=std::max(sinkPeak,sample.values[1]);}
        BOOST_CHECK_LT(sourcePeak,source.limit);BOOST_CHECK_LT(sinkPeak,sink.limit);
        BOOST_CHECK_GT(sourcePeak,1e-6);BOOST_CHECK_GT(sinkPeak,1e-6);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalFloatingTtlInputStaysUnknownAndNandControllingZeroRemainsKnown )
{
    auto u=Device("74xx:74LS00");for(auto& [pin,net]:u.pins)if(net=="input")net="vcc";
    u.pins["1"]=u.pins["4"]="open";u.pins["5"]="0";
    auto c=BuildCircuitSnapshot({Source("VS","vcc",5),u},{},true);Configure(c);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(3e-6);
    for(const auto& pin:c.logicPins)if(pin.component==1)
    {
        const auto level=PinLogicLevel(pin,session.Snapshot().latest);
        if(pin.pin=="1")BOOST_CHECK(level==LOGIC_LEVEL::HIGH_Z);
        if(pin.pin=="3")BOOST_CHECK(level==LOGIC_LEVEL::UNKNOWN);
        if(pin.pin=="6")BOOST_CHECK(level==LOGIC_LEVEL::HIGH);
    }
}

BOOST_AUTO_TEST_CASE( ElectricalCmosLoadedBandwidthFallsWithIncreasingFrequency )
{
    for(double period:{1e-6,50e-9}) BOOST_TEST_CONTEXT(period)
    {
        auto u=Device("74xx:74HC00");for(auto& [pin,net]:u.pins)if(net=="input")net="vcc";u.pins["1"]="input";
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VD","input",0),u});Configure(c,1e-9);Pulse(c,period);
        Insert(c,"Cload "+c.pinNodes[2].at("3")+" 0 1n");c.config.signals={Voltage(c,2,3)};c.config.historyCapacity=4096;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(5e-6);
        double low=5,high=0;for(const auto& sample:session.Snapshot().history)if(sample.time>4e-6)
        {low=std::min(low,sample.values[0]);high=std::max(high,sample.values[0]);}
        if(period>100e-9)BOOST_CHECK_GT(high-low,4.9);
        else BOOST_CHECK_LT(high-low,2);
    }
}
