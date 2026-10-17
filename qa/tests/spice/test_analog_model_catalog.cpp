/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( const std::string& ref, const std::string& net, double value )
{
    return { ref, "Simulation_SPICE:VDC", std::to_string( value ), {{"1",net},{"2","0"}} };
}
CIRCUIT_COMPONENT Resistor( const std::string& ref, const std::string& a,
                            const std::string& b, const std::string& value )
{
    return { ref, "Device:R", value, {{"1",a},{"2",b}} };
}
CIRCUIT_COMPONENT Device( const std::string& library )
{
    CIRCUIT_COMPONENT u{ "U1", library, "10k", {} };
    for( const auto& pin : MODEL_REPOSITORY().SelectModel( u ).pins ) u.pins[pin.defaultPin] = "";
    return u;
}
void Extra( CIRCUIT_SNAPSHOT& c, const std::string& line )
{
    c.netlist.insert( c.netlist.rfind( ".end" ), line + '\n' );
}
std::string Voltage( const CIRCUIT_SNAPSHOT& c, std::size_t part, int pin )
{
    return "v(" + c.pinNodes.at(part).at(std::to_string(pin)) + ")";
}
struct AMP
{
    std::string library;
    int vp, vn;
    std::vector<int> plus, minus, out;
    bool bipolar = false;
};
const std::vector<int> dualP{3,5}, dualN{2,6}, dualO{1,7};
const std::vector<int> quadP{3,5,10,12}, quadN{2,6,9,13}, quadO{1,7,8,14};
const std::vector<AMP> amplifiers{
    {"LM358",8,4,dualP,dualN,dualO}, {"LM358_DFN",8,4,dualP,dualN,dualO},
    {"LM2904",8,4,dualP,dualN,dualO}, {"LM324",4,11,quadP,quadN,quadO},
    {"LM2902",4,11,quadP,quadN,quadO},
    {"TL071",7,4,{3},{2},{6},true}, {"TL072",8,4,dualP,dualN,dualO,true},
    {"TL074",4,11,quadP,quadN,quadO,true}, {"LM741",7,4,{3},{2},{6},true},
    {"MCP6001-OT",5,2,{3},{4},{1}}, {"MCP6001R",2,5,{3},{4},{1}},
    {"MCP6001U",5,2,{1},{3},{4}}, {"MCP6002-xP",8,4,dualP,dualN,dualO},
    {"MCP6002-xMC",8,4,dualP,dualN,dualO}, {"MCP6004",4,11,quadP,quadN,quadO}
};
std::vector<CIRCUIT_COMPONENT> Followers( const AMP& spec )
{
    auto u=Device("Amplifier_Operational:"+spec.library);
    u.pins[std::to_string(spec.vp)]="vp";u.pins[std::to_string(spec.vn)]="vn";
    std::vector<CIRCUIT_COMPONENT> parts{Source("VP","vp",spec.bipolar?12:5),
                                       Source("VN","vn",spec.bipolar?-12:0)};
    for( std::size_t i=0;i<spec.out.size();++i )
    {
        // Use distinct values, including negative outputs with bipolar supplies.
        const double value=spec.bipolar ? -2.0+i : 1.0+.4*i;
        const auto input="in"+std::to_string(i), output="out"+std::to_string(i);
        u.pins[std::to_string(spec.plus[i])]=input;
        u.pins[std::to_string(spec.minus[i])]=output;
        u.pins[std::to_string(spec.out[i])]=output;
        parts.push_back(Source("VI"+std::to_string(i),input,value));
    }
    parts.push_back(u);
    for( std::size_t i=0;i<spec.out.size();++i )
        parts.push_back(Resistor("RL"+std::to_string(i),"out"+std::to_string(i),"0","10k"));
    return parts;
}
}

BOOST_AUTO_TEST_CASE( AnalogCatalogEveryAmplifierChannelFollowsItsOwnInput )
{
    for( const auto& spec:amplifiers ) BOOST_TEST_CONTEXT(spec.library)
    {
        auto parts=Followers(spec);const auto device=2+spec.out.size();
        auto c=BuildCircuitSnapshot(parts);c.config.signals.clear();
        for( auto p:spec.out ) c.config.signals.push_back(Voltage(c,device,p));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
        auto sample=session.Snapshot().latest;
        for( std::size_t i=0;i<spec.out.size();++i )
            BOOST_CHECK_SMALL(sample.values.at(i)-(spec.bipolar?-2.0+i:1.0+.4*i),.012);
        // The second channel remains independent after the first input changes.
        session.SetSourceValue("Vca3",spec.bipolar?3:2.5);session.AdvanceTo(.002);
        sample=session.Snapshot().latest;
        BOOST_CHECK_SMALL(sample.values.at(0)-(spec.bipolar?3:2.5),.012);
        for( std::size_t i=1;i<spec.out.size();++i )
            BOOST_CHECK_SMALL(sample.values.at(i)-(spec.bipolar?-2.0+i:1.0+.4*i),.012);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogAmplifiersLimitSourceAndSinkCurrentAndReleaseWhenUnpowered )
{
    for( const std::string chip:{"LM358","TL072","LM741","MCP6002-xP"} ) BOOST_TEST_CONTEXT(chip)
    {
        const auto spec=*std::find_if(amplifiers.begin(),amplifiers.end(),[&](const AMP& a){return a.library==chip;});
        auto parts=Followers(spec);const auto device=2+spec.out.size();
        parts.resize(device+1);
        parts[2]=Source("VI0","in0",3);
        parts[device].pins[std::to_string(spec.minus[0])]="0"; // open-loop high command
        // A 1-ohm load forces current limiting, well below the desired voltage.
        auto c=BuildCircuitSnapshot(parts);const auto out=c.pinNodes[device].at(std::to_string(spec.out[0]));
        Extra(c,"Roverload "+out+" 0 1");c.config.signals={"v("+out+")"};
        const auto resolved=MODEL_REPOSITORY().Resolve(parts[device]);
        const double scale=chip=="MCP6002-xP" ? (6.+(5.-1.8)*17./3.7)/23. : 1.;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(.001);
        BOOST_CHECK_GT(session.Snapshot().latest.values[0],.95*scale*resolved.parameters.at("source_limit"));
        BOOST_CHECK_LE(session.Snapshot().latest.values[0],1.0001*scale*resolved.parameters.at("source_limit"));
        // Separate bench for sink current avoids loading the disabled output with the source resistor.
        auto sink=parts;sink[device].pins[std::to_string(spec.plus[0])]="0";
        sink[device].pins[std::to_string(spec.minus[0])]="in0";
        sink[2]=Source("VI0","in0",3);
        auto d=BuildCircuitSnapshot(sink);const auto node=d.pinNodes[device].at(std::to_string(spec.out[0]));
        Extra(d,"Vpull pull 0 5\nRload pull "+node+" 1");d.config.signals={"v("+node+")"};
        INTERACTIVE_SIM_SESSION other(CIRCUITALIVE_TEST_WORKER);other.Configure(d.netlist,d.config);other.AdvanceTo(.001);
        BOOST_CHECK_SMALL(5-other.Snapshot().latest.values[0]-scale*resolved.parameters.at("sink_limit"),.0002);
        // With supplies removed a 100k pull-up must prevail; no phantom powered output.
        auto off=parts;off[0]=Source("VP","vp",0);off[1]=Source("VN","vn",0);
        off.resize(device+1);auto e=BuildCircuitSnapshot(off);const auto n=e.pinNodes[device].at(std::to_string(spec.out[0]));
        Extra(e,"Vpull pull 0 3.3\nRload pull "+n+" 100k");e.config.signals={"v("+n+")"};
        INTERACTIVE_SIM_SESSION unpowered(CIRCUITALIVE_TEST_WORKER);unpowered.Configure(e.netlist,e.config);unpowered.AdvanceTo(.001);
        BOOST_CHECK_SMALL(unpowered.Snapshot().latest.values[0]-3.3,.002);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogClosedLoopGainAndSlewAreFinite )
{
    auto u=Device("Amplifier_Operational:LM358");
    u.pins["8"]="vcc";u.pins["4"]="0";u.pins["3"]="in";u.pins["2"]="feedback";u.pins["1"]="out";
    // Terminate the unused amplifier as a follower.
    u.pins["5"]="in";u.pins["6"]=u.pins["7"]="unused";
    auto c=BuildCircuitSnapshot({Source("VS","vcc",12),Source("VI","in",1),u,
        Resistor("RF","out","feedback","10k"),Resistor("RG","feedback","0","10k"),
        Resistor("RL","out","0","10k")});
    c.config.signals={Voltage(c,2,1)};c.config.horizon=.002;c.config.step=c.config.maxStep=50e-9;
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(100e-6);
    const double start=session.Snapshot().latest.values[0];BOOST_CHECK_SMALL(start-2,.01);
    session.SetSourceValue("Vca2",3);session.AdvanceTo(102e-6);
    const double after=session.Snapshot().latest.values[0];
    BOOST_CHECK_GT(after-start,.3);BOOST_CHECK_LT(after-start,.7); // nominal 0.3 V/us
    session.AdvanceTo(150e-6);BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-6,.015);
}

BOOST_AUTO_TEST_CASE( AnalogCatalogComparatorsUseEveryPhysicalChannelAndOpenCollector )
{
    for( const std::string chip:{"LM393","LM339","LM2903","LM2901","LMV393"} ) BOOST_TEST_CONTEXT(chip)
    {
        const bool quad=chip=="LM339"||chip=="LM2901";
        const std::vector<int> plus=quad?std::vector<int>{5,7,11,9}:dualP;
        const std::vector<int> minus=quad?std::vector<int>{4,6,10,8}:dualN;
        const std::vector<int> outputs=quad?std::vector<int>{2,1,13,14}:dualO;
        auto u=Device("Comparator:"+chip);u.pins[quad?"3":"8"]="vcc";u.pins[quad?"12":"4"]="0";
        for(std::size_t i=0;i<outputs.size();++i)
        {
            u.pins[std::to_string(plus[i])]=i%2?"low":"high";u.pins[std::to_string(minus[i])]="ref";
            u.pins[std::to_string(outputs[i])]="out"+std::to_string(i);
        }
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","vcc",5),Source("VH","high",3),
            Source("VL","low",1),Source("VR","ref",2),Source("VP","pull",chip=="LMV393"?5:12),u};
        for(std::size_t i=0;i<outputs.size();++i)parts.push_back(Resistor("RP"+std::to_string(i),"pull","out"+std::to_string(i),"10k"));
        auto c=BuildCircuitSnapshot(parts);c.config.signals.clear();
        for(int p:outputs)c.config.signals.push_back(Voltage(c,5,p));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.0001);
        for(std::size_t i=0;i<outputs.size();++i)
            if(i%2)BOOST_CHECK_LT(session.Snapshot().latest.values[i],.2);
            else BOOST_CHECK_SMALL(session.Snapshot().latest.values[i]-(chip=="LMV393"?5:12),.002);
        session.SetSourceValue("Vca2",1);session.SetSourceValue("Vca3",3);session.AdvanceTo(.0002);
        for(std::size_t i=0;i<outputs.size();++i)
            if(i%2==0)BOOST_CHECK_LT(session.Snapshot().latest.values[i],.2);
            else BOOST_CHECK_SMALL(session.Snapshot().latest.values[i]-(chip=="LMV393"?5:12),.002);
        session.SetSourceValue("Vca1",0);session.AdvanceTo(.0003);
        for(double value:session.Snapshot().latest.values)BOOST_CHECK_SMALL(value-(chip=="LMV393"?5:12),.002);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogComparatorDelayIsNotInstantaneous )
{
    for(const std::string chip:{"LM393","LMV393"}) BOOST_TEST_CONTEXT(chip)
    {
        auto u=Device("Comparator:"+chip);u.pins["8"]="vcc";u.pins["4"]="0";
        u.pins["3"]=u.pins["5"]="in";u.pins["2"]=u.pins["6"]="ref";u.pins["1"]="out";
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VI","in",2.1),Source("VR","ref",2),u,
            Resistor("RP","vcc","out","1k")});
        c.config.signals={Voltage(c,3,1)};c.config.horizon=30e-6;c.config.step=c.config.maxStep=1e-9;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(5e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-5,.002);
        session.SetSourceValue("Vca2",1.9);session.AdvanceTo(5.05e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-5,.002);
        session.AdvanceTo(chip=="LM393"?6.6e-6:5.4e-6);
        BOOST_CHECK_LT(session.Snapshot().latest.values[0],.4);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogAllFixedRegulatorsRegulateDropOutAndLimitCurrent )
{
    struct REG{std::string name;double vout,limit,drop;bool negative=false;bool so8=false;};
    std::vector<REG> regs;
    for(const auto value:{"1.5","1.8","2.5","2.85","3.3","5.0"})regs.push_back({"AMS1117-"+std::string(value),std::stod(value),1,1.3});
    for(const auto value:{"1.8","2.5","3.3","5.0"})regs.push_back({"LM1117DT-"+std::string(value),std::stod(value),.8,1.2});
    for(const auto value:{5,12,15})regs.push_back({"L79"+std::string(value==5?"05":std::to_string(value)),double(value),1.5,2,true});
    for(const auto value:{5,9,12})for(bool so8:{false,true})regs.push_back({"L78L"+std::string(value<10?"0":"")+std::to_string(value)+(so8?"_SO8":"_TO92"),double(value),.1,1.7,false,so8});
    for(const auto& spec:regs) BOOST_TEST_CONTEXT(spec.name)
    {
        auto u=Device("Regulator_Linear:"+spec.name);
        const int in=spec.negative?2:spec.so8?8:3,out=spec.so8?1:spec.name.starts_with("L78L")?1:spec.negative?3:2;
        const int ground=spec.negative?1:spec.name.starts_with("L78L")?2:1;
        u.pins[std::to_string(in)]="in";u.pins[std::to_string(out)]="out";u.pins[std::to_string(ground)]="0";
        const double sign=spec.negative?-1:1;
        auto c=BuildCircuitSnapshot({Source("VS","in",sign*(spec.vout+3)),u,Resistor("RL","out","0","1k")});
        c.config.signals={Voltage(c,1,out)};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
        BOOST_CHECK_SMALL(sign*session.Snapshot().latest.values[0]-spec.vout,.02);
        session.SetSourceValue("Vca1",sign*(spec.vout+spec.drop-.5));session.AdvanceTo(.002);
        BOOST_CHECK_SMALL(sign*session.Snapshot().latest.values[0]-(spec.vout-.5),.025);
        // Overload is measured through a known 1-ohm load, rather than an internal implementation node.
        auto d=BuildCircuitSnapshot({Source("VS","in",sign*(spec.vout+3)),u,Resistor("RL","out","0","1")});d.config.signals={Voltage(d,1,out)};
        INTERACTIVE_SIM_SESSION load(CIRCUITALIVE_TEST_WORKER);load.Configure(d.netlist,d.config);load.AdvanceTo(.001);
        BOOST_CHECK_SMALL(sign*load.Snapshot().latest.values[0]-spec.limit,.002);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogAdjustableRegulatorsUseTheExternalDivider )
{
    for(const std::string name:{"AMS1117","LM1117DT-ADJ"}) BOOST_TEST_CONTEXT(name)
    {
        auto u=Device("Regulator_Linear:"+name);u.pins={{"1","adj"},{"2","out"},{"3","in"}};
        auto c=BuildCircuitSnapshot({Source("VS","in",9),u,Resistor("R1","out","adj","120"),
            Resistor("R2","adj","0","360"),Resistor("RL","out","0","1k")});c.config.signals={Voltage(c,1,2),Voltage(c,1,1)};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.002);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-5,.04);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-session.Snapshot().latest.values[1]-1.25,.005);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogOptocouplersHaveCtrResponseAndIsolatedDomains )
{
    for(const std::string name:{"PC817","EL817","LTV-817","TLP291"}) BOOST_TEST_CONTEXT(name)
    {
        auto u=Device("Isolator:"+name);u.pins={{"1","anode"},{"2","ledground"},{"3","0"},{"4","collector"}};
        const auto model=MODEL_REPOSITORY().SelectModel(u);BOOST_CHECK_EQUAL(model.electricalGroups.size(),2u);
        // Input ground is deliberately 20V above the phototransistor ground.
        auto c=BuildCircuitSnapshot({Source("VI","drive",20),Source("VG","ledground",20),Source("VS","vcc",5),u,
            Resistor("RI","drive","anode","750"),Resistor("RC","vcc","collector","10k")});
        c.config.signals={Voltage(c,3,4),Voltage(c,3,1),Voltage(c,3,2)};
        c.config.horizon=.01;c.config.step=c.config.maxStep=100e-9;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(100e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-5,.002);
        session.SetSourceValue("Vca1",25);session.AdvanceTo(100.1e-6);
        BOOST_CHECK_GT(session.Snapshot().latest.values[0],2); // finite optical response, not instantaneous
        session.AdvanceTo(150e-6);BOOST_CHECK_LT(session.Snapshot().latest.values[0],.1);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[2]-20,1e-8);
        // Force VCE=5V and measure transfer ratio away from saturation.
        auto d=BuildCircuitSnapshot({Source("VI","drive",5),Source("VS","collector",5),u,
            Source("VG","ledground",0),Resistor("RI","drive","anode","750")});
        d.config.signals={Voltage(d,2,1),"i(Vca2)"};
        INTERACTIVE_SIM_SESSION ctr(CIRCUITALIVE_TEST_WORKER);ctr.Configure(d.netlist,d.config);ctr.AdvanceTo(.001);
        auto sample=ctr.Snapshot().latest;const double led=(5-sample.values[0])/750;
        BOOST_CHECK_GT(led,.004);BOOST_CHECK_LT(led,.006);
        BOOST_CHECK_SMALL(-sample.values[1]/led-1,.015);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogPotentiometersRespectPositionAndIndependentTracks )
{
    for(const std::string name:{"R_Potentiometer","R_Potentiometer_Dual","R_Potentiometer_MountingPin","R_Potentiometer_Dual_MountingPin"})
    for(const double position:{0.,.25,.75,1.}) BOOST_TEST_CONTEXT(name<<" at "<<position)
    {
        const bool dual=name.find("Dual")!=std::string::npos;
        auto u=Device("Device:"+name);u.modelParameters["position"]=std::to_string(position);
        u.pins["1"]="vcc";u.pins["2"]="wiper";u.pins["3"]="0";
        if(dual){u.pins["4"]="other";u.pins["5"]="wiper2";u.pins["6"]="0";}
        auto c=BuildCircuitSnapshot({Source("VS","vcc",10),Source("VT","other",4),u});c.config.signals={Voltage(c,2,2)};
        if(dual)c.config.signals.push_back(Voltage(c,2,5));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-10*position,1e-5);
        if(dual)BOOST_CHECK_SMALL(session.Snapshot().latest.values[1]-4*position,1e-5);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogAmplifierBandwidthRollsOffWithoutChangingTheGlobalTimestep )
{
    for(const double frequency:{1000.,700000.}) BOOST_TEST_CONTEXT(frequency)
    {
        auto u=Device("Amplifier_Operational:LM358");u.modelParameters["offset"]="0";
        u.pins["8"]="vcc";u.pins["4"]="0";u.pins["3"]=u.pins["5"]="input";
        u.pins["1"]=u.pins["2"]="output";u.pins["6"]=u.pins["7"]="unused";
        auto c=BuildCircuitSnapshot({Source("VS","vcc",12),u,Resistor("RL","output","0","10k")});
        Extra(c,"Vstim "+c.pinNodes[1].at("3")+" 0 SIN(2 .01 "+std::to_string(frequency)+")");
        c.config.signals={Voltage(c,1,1)};c.config.step=c.config.maxStep=1/(frequency*80);
        c.config.horizon=.1;c.config.historyCapacity=80;
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(30/frequency);
        const auto snapshot=session.Snapshot();BOOST_REQUIRE_GE(snapshot.history.size(),79u);
        double low=1e10,high=-1e10;
        for(const auto& s:snapshot.history){low=std::min(low,s.values[0]);high=std::max(high,s.values[0]);}
        const double gain=(high-low)/.02;
        const double expected=1/std::sqrt(1+std::pow(frequency/700000,2));
        BOOST_CHECK_SMALL(gain-expected,.035);
    }
}

BOOST_AUTO_TEST_CASE( AnalogCatalogMcpOutputCurrentTracksNominalSupplyVoltage )
{
    for(const double supply:{1.8,5.5}) BOOST_TEST_CONTEXT(supply)
    {
        auto u=Device("Amplifier_Operational:MCP6001-OT");
        u.pins={{"1","output"},{"2","0"},{"3","input"},{"4","0"},{"5","vcc"}};
        auto c=BuildCircuitSnapshot({Source("VS","vcc",supply),Source("VI","input",1),u,
            Resistor("RL","output","0","1")});c.config.signals={Voltage(c,2,1),"i(Vca1)"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
        const auto sample=session.Snapshot().latest;const double limit=supply==1.8?.006:.023;
        BOOST_CHECK_SMALL(sample.values[0]-limit,1e-5);
        BOOST_CHECK_SMALL(-sample.values[1]-sample.values[0]-.0001,1e-6);
    }
}
