/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( const std::string& ref, const std::string& net, double value,
                          const std::string& negative="0" )
{
    return {ref,"Simulation_SPICE:VDC",std::to_string(value),{{"1",net},{"2",negative}}};
}
CIRCUIT_COMPONENT Resistor( const std::string& ref, const std::string& a,
                            const std::string& b, const std::string& value )
{
    return {ref,"Device:R",value,{{"1",a},{"2",b}}};
}
CIRCUIT_COMPONENT Device( const std::string& lib )
{
    CIRCUIT_COMPONENT u{"U1",lib,"1",{}};
    for( const auto& p : MODEL_REPOSITORY().SelectModel(u).pins ) u.pins[p.defaultPin]="";
    return u;
}
std::string Voltage( const CIRCUIT_SNAPSHOT& c, std::size_t part, int pin )
{
    return "v("+c.pinNodes.at(part).at(std::to_string(pin))+")";
}
void Configure( CIRCUIT_SNAPSHOT& c, double step=100e-9, bool initial=false )
{
    c.config.step=c.config.maxStep=step;c.config.horizon=.1;c.config.useInitialConditions=initial;
}
struct JFET { std::string library,order; double idss,pinch; bool p=false; };
// Independent reference points and physical orders, not derived from .casim expressions.
std::vector<JFET> Jfets()
{
    std::vector<JFET> all{{"Transistor_FET:2N3819","DGS",.01,4},
        {"Transistor_FET:BF245A","GSD",.004,1.8},{"Transistor_FET:BF245B","GSD",.01,3.5},
        {"Transistor_FET:BF245C","GSD",.018,5.5},{"Transistor_FET:MMBFJ111","DSG",.1,4.5},
        {"Transistor_FET:MMBFJ112","DSG",.04,3},{"Transistor_FET:MMBFJ113","DSG",.012,1.8}};
    for( const std::string order : {"DGS","DSG","GDS","GSD","SDG","SGD"} )
        for( const std::string type : {"N","P"} )
            all.push_back({"Device:Q_"+type+"JFET_"+order,order,.005,2,type=="P"});
    return all;
}
int Pin( const JFET& j, char terminal ) { return int(j.order.find(terminal))+1; }
struct DIODES { std::string name;std::vector<std::pair<int,int>> edges;bool schottky; };
std::vector<DIODES> Diodes()
{
    std::vector<DIODES> all;
    for( const std::string suffix : {"","W"} )
    {
        all.push_back({"BAT54A"+suffix,{{3,1},{3,2}},true});
        all.push_back({"BAT54C"+suffix,{{1,3},{2,3}},true});
        all.push_back({"BAT54S"+suffix,{{1,3},{3,2}},true});
    }
    all.push_back({"BAT54ADW",{{6,1},{6,2},{3,4},{3,5}},true});
    all.push_back({"BAT54SDW",{{1,6},{6,2},{4,3},{3,5}},true});
    for( const std::string suffix : {"","M","T","W"} )
        all.push_back({"BAV70"+suffix,{{1,3},{2,3}},false});
    all.push_back({"BAV70S",{{1,6},{2,6},{4,3},{5,3}},false});
    all.push_back({"BAV99",{{1,3},{3,2}},false});
    all.push_back({"BAV99S",{{1,6},{6,2},{4,3},{3,5}},false});
    return all;
}
struct DRIVER { std::string name;int channels;double high; };
const std::vector<DRIVER> drivers{{"ULN2003",7,5},{"ULN2003A",7,5},{"ULQ2003A",7,5},
    {"ULN2004",7,12},{"ULN2004A",7,12},{"ULQ2004A",7,12},{"ULN2803A",8,5},
    {"ULN2804A",8,12},{"MC1413BD",7,5},{"MC1413BP",7,5},{"MC1413D",7,5},
    {"MC1413P",7,5},{"NCV1413B",7,5},{"SN75468",7,5},{"SN75469",7,12}};
struct DRIVER_BENCH
{
    CIRCUIT_SNAPSHOT circuit;
    std::unique_ptr<INTERACTIVE_SIM_SESSION> session;
    DRIVER spec;
    double time=0;
    double warningLimit=0;
    DRIVER_BENCH( const DRIVER& s, bool floating=false ) : spec(s)
    {
        auto u=Device("Transistor_Array:"+s.name);
        u.pins[std::to_string(s.channels+1)]="0";
        u.pins[std::to_string(s.channels+2)]=floating?"":"clamp";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","supply",12),Source("VC","clamp",12)};
        for( int i=0;i<s.channels;++i )
        {
            const auto in="input"+std::to_string(i),out="out"+std::to_string(i);
            u.pins[std::to_string(i+1)]=(floating&&i)?"":in;
            u.pins[std::to_string(2*s.channels+2-i)]=out;
            parts.push_back(Source("VI"+std::to_string(i),in,0));
        }
        const auto device=parts.size();parts.push_back(u);
        for( int i=0;i<s.channels;++i )
            parts.push_back(Resistor("RL"+std::to_string(i),"supply","out"+std::to_string(i),"120"));
        circuit=BuildCircuitSnapshot(parts);Configure(circuit);
        const auto selected=std::find_if(circuit.currentWarnings.begin(),circuit.currentWarnings.end(),
            [](const auto& warning){return warning.name=="U1";});
        BOOST_REQUIRE(selected!=circuit.currentWarnings.end());warningLimit=selected->limit;
        const auto warning=circuit.config.signals.at(selected->signal);
        circuit.config.signals.clear();
        for( int i=0;i<s.channels;++i ) circuit.config.signals.push_back(Voltage(circuit,device,2*s.channels+2-i));
        for( int i=0;i<s.channels;++i ) circuit.config.signals.push_back("i(Vca"+std::to_string(3+i)+")");
        circuit.config.signals.push_back(warning);
        session=std::make_unique<INTERACTIVE_SIM_SESSION>(CIRCUITALIVE_TEST_WORKER);
        session->Configure(circuit.netlist,circuit.config);Advance();
    }
    void Set( int channel, double voltage ) { session->SetSourceValue("Vca"+std::to_string(3+channel),voltage); }
    void Advance( double dt=10e-6 ) { session->AdvanceTo(time+=dt); }
    void Check( unsigned mask ) const
    {
        const auto values=session->Snapshot().latest.values;
        for( int i=0;i<spec.channels;++i ) BOOST_TEST_CONTEXT("channel "<<i+1)
        {
            const double output=values.at(i),input=-values.at(spec.channels+i);
            if( mask&(1u<<i) )
            {
                BOOST_CHECK_GT(output,.5);BOOST_CHECK_LT(output,1.4);
                BOOST_CHECK_GT(input,.0005);BOOST_CHECK_LT(input,.002);
            }
            else { BOOST_CHECK_SMALL(output-12,.002);BOOST_CHECK_SMALL(input,2e-6); }
        }
        BOOST_CHECK_LT(values.back(),warningLimit);
    }
};
}

BOOST_AUTO_TEST_CASE( JunctionCatalogJfetsAreNormallyOnAndReverseGateBiasPinchesOffBothPolarities )
{
    for( const auto& j : Jfets() ) BOOST_TEST_CONTEXT(j.library)
    {
        auto u=Device(j.library);const double polarity=j.p?-1:1;
        u.pins={{std::to_string(Pin(j,'D')),"drain"},{std::to_string(Pin(j,'G')),"gate"},
            {std::to_string(Pin(j,'S')),"0"}};
        auto c=BuildCircuitSnapshot({Source("VD","drain",polarity*15),Source("VG","gate",0),u});
        Configure(c);c.config.signals={"i(Vca1)","i(Vca2)"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(10e-6);double on=-polarity*session.Snapshot().latest.values.at(0);
        BOOST_CHECK_CLOSE(on,j.idss,5);BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(1),1e-8);
        session.SetSourceValue("Vca2",-polarity*j.pinch/2);session.AdvanceTo(20e-6);
        BOOST_CHECK_CLOSE(-polarity*session.Snapshot().latest.values.at(0)/on,.25,7);
        session.SetSourceValue("Vca2",-polarity*j.pinch*1.2);session.AdvanceTo(30e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(0),1e-8);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(1),1e-8);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogJfetsConductBilaterallyInTheOhmicRegion )
{
    for( const auto& j : Jfets() ) for( bool swap : {false,true} ) BOOST_TEST_CONTEXT(j.library<<" swap="<<swap)
    {
        auto u=Device(j.library);const double polarity=j.p?-1:1;
        u.pins={{std::to_string(Pin(j,swap?'S':'D')),"drain"},
            {std::to_string(Pin(j,swap?'D':'S')),"0"},{std::to_string(Pin(j,'G')),"0"}};
        auto c=BuildCircuitSnapshot({Source("VD","drain",polarity*.05),u});Configure(c);
        c.config.signals={"i(Vca1)"};INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(c.netlist,c.config);session.AdvanceTo(10e-6);
        const double resistance=.05/(-polarity*session.Snapshot().latest.values.at(0));
        BOOST_CHECK_CLOSE(resistance,j.pinch/(2*j.idss)+1,8);
        if( j.library=="Transistor_FET:MMBFJ111" ) BOOST_CHECK_LT(resistance,30);
        if( j.library=="Transistor_FET:MMBFJ112" ) BOOST_CHECK_LT(resistance,50);
        if( j.library=="Transistor_FET:MMBFJ113" ) BOOST_CHECK_LT(resistance,100);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogJfetGateIsAJunctionRatherThanAnInsulatedMosGate )
{
    for( bool p : {false,true} )
    {
        auto u=Device(p?"Device:Q_PJFET_DGS":"Device:Q_NJFET_DGS");u.pins={{"1","0"},{"2","gate"},{"3","0"}};
        auto c=BuildCircuitSnapshot({Source("VG","drive",p?-1:1),u,Resistor("RG","drive","gate","1k")});
        Configure(c);c.config.signals={"i(Vca1)",Voltage(c,1,2)};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(10e-6);const auto v=session.Snapshot().latest.values;
        BOOST_CHECK_GT(std::abs(v.at(0)),.0003);BOOST_CHECK_LT(std::abs(v.at(0)),.0007);
        BOOST_CHECK_GT((p?-1:1)*v.at(1),.3);BOOST_CHECK_LT((p?-1:1)*v.at(1),.7);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogNamedJfetsRemapAllGenericDrawingPinOrdersByTerminalName )
{
    MODEL_REPOSITORY repository;
    for( const auto& j : Jfets() ) if( j.library.starts_with("Device:Q_NJFET_") )
    {
        auto u=Device(j.library);u.value="MMBFJ113";
        for( char terminal : std::string("DGS") ) u.pinNames[std::to_string(Pin(j,terminal))]=std::string(1,terminal);
        const auto resolved=repository.Resolve(u);BOOST_CHECK_EQUAL(resolved.model.id,"jfet_mmbfj113");
        for( const auto& [port,terminal] : std::vector<std::pair<std::string,char>>{{"d",'D'},{"g",'G'},{"s",'S'}} )
            BOOST_CHECK_EQUAL(resolved.pins.at(port),std::to_string(Pin(j,terminal)));
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogEveryPhysicalArrayJunctionConductsAndBlocksInItsOwnDirection )
{
    for( const auto& d : Diodes() ) for( const auto& [anode,cathode] : d.edges )
        BOOST_TEST_CONTEXT(d.name<<" "<<anode<<"->"<<cathode)
    {
        auto u=Device("Diode:"+d.name);u.pins[std::to_string(anode)]="anode";u.pins[std::to_string(cathode)]="0";
        auto c=BuildCircuitSnapshot({Source("VS","drive",5),u,Resistor("RL","drive","anode","1k")});Configure(c);
        c.config.signals={Voltage(c,1,anode),"i(Vca1)"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(10e-6);auto v=session.Snapshot().latest.values;
        BOOST_CHECK_GT(v.at(0),d.schottky?.1:.45);BOOST_CHECK_LT(v.at(0),d.schottky?.45:.95);
        BOOST_CHECK_SMALL(-v.at(1)-(5-v.at(0))/1000,1e-9);
        session.SetSourceValue("Vca1",-10);session.AdvanceTo(20e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(1),1e-6);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogQuadDiodePairsRetainIndependentFloatingReferences )
{
    for( const std::string name : {"BAT54ADW","BAT54SDW","BAV70S","BAV99S"} ) BOOST_TEST_CONTEXT(name)
    {
        auto u=Device("Diode:"+name);
        std::vector<CIRCUIT_COMPONENT> parts{Source("V1","feed1",5,"return1"),Source("V2","feed2",5,"return2"),u};
        for( int pair=0;pair<2;++pair )
        {
            const auto index=std::to_string(pair+1),feed="feed"+index,ret="return"+index,out="out"+index;
            const int mid=pair?3:6,first=pair?4:1,last=pair?5:2;
            if( name=="BAT54ADW" )
            {
                parts[2].pins[std::to_string(mid)]=feed;
                for( int pin : {first,last} )
                {
                    const auto net="load"+std::to_string(pin);parts[2].pins[std::to_string(pin)]=net;
                    parts.push_back(Resistor("RL"+std::to_string(pin),net,ret,"1k"));
                }
            }
            else if( name=="BAV70S" )
            {
                parts[2].pins[std::to_string(mid)]=ret;
                for( int pin : {first,last} )
                {
                    const auto net="load"+std::to_string(pin);parts[2].pins[std::to_string(pin)]=net;
                    parts.push_back(Resistor("RL"+std::to_string(pin),feed,net,"1k"));
                }
            }
            else
            {
                parts[2].pins[std::to_string(first)]=feed;parts[2].pins[std::to_string(last)]=out;
                parts.push_back(Resistor("RL"+index,out,ret,"1k"));
            }
        }
        auto c=BuildCircuitSnapshot(parts);Configure(c);BOOST_CHECK_EQUAL(c.automaticReferences.size(),2u);
        c.config.signals={"i(Vca1)","i(Vca2)"};INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(c.netlist,c.config);session.AdvanceTo(10e-6);auto v=session.Snapshot().latest.values;
        BOOST_CHECK_SMALL(v.at(0)-v.at(1),1e-8);BOOST_CHECK_LT(v.at(1),-.003);
        session.SetSourceValue("Vca1",0);session.AdvanceTo(20e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(0),1e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(1)-v.at(1),1e-8);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogDarlingtonArraysSinkEveryPhysicalChannelIndependently )
{
    for( const auto& d : drivers ) BOOST_TEST_CONTEXT(d.name)
    {
        DRIVER_BENCH b(d);b.Check(0);
        for( int bit=0;bit<d.channels;++bit )
        {
            if( bit ) b.Set(bit-1,0);
            b.Set(bit,d.high);b.Advance();b.Check(1u<<bit);
        }
        for( int bit=0;bit<d.channels;++bit ) b.Set(bit,d.high);
        b.Advance();b.Check((1u<<d.channels)-1);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogDarlingtonComCanFloatAndUnconnectedInputsArePulledDown )
{
    for( const auto& d : std::vector<DRIVER>{{"ULN2003A",7,5},{"ULN2004A",7,12},
            {"ULN2803A",8,5},{"ULN2804A",8,12},{"MC1413D",7,5},{"SN75468",7,5},{"SN75469",7,12}} )
        BOOST_TEST_CONTEXT(d.name)
    {
        DRIVER_BENCH b(d,true);b.Set(0,d.high);b.Advance();b.Check(1);
        b.Set(0,0);b.Advance();b.Check(0);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogDarlingtonInputVariantsDrawDifferentAnalogCurrents )
{
    DRIVER_BENCH ttl({"ULN2003A",7,5}),cmos({"ULN2004A",7,5});
    ttl.Set(0,5);cmos.Set(0,5);ttl.Advance();cmos.Advance();
    const double lowR=-ttl.session->Snapshot().latest.values.at(7),highR=-cmos.session->Snapshot().latest.values.at(7);
    BOOST_CHECK_GT(lowR,.001);BOOST_CHECK_GT(highR,.0003);BOOST_CHECK_LT(highR,.0005);
    BOOST_CHECK_CLOSE(lowR/highR,10500.0/2700,3);
}

BOOST_AUTO_TEST_CASE( JunctionCatalogDarlingtonFlybackDiodesReturnInterruptedInductorCurrentToCom )
{
    for( const DRIVER d : {drivers.at(1),drivers.at(4),drivers.at(6),drivers.at(7),drivers.at(10),drivers.at(13),drivers.at(14)} )
        BOOST_TEST_CONTEXT(d.name)
    {
        auto u=Device("Transistor_Array:"+d.name);
        for( int i=1;i<=d.channels;++i ) u.pins[std::to_string(i)]="0";
        u.pins["1"]="input";u.pins[std::to_string(d.channels+1)]="0";
        u.pins[std::to_string(d.channels+2)]="clamp";u.pins[std::to_string(2*d.channels+2)]="out";
        auto c=BuildCircuitSnapshot({Source("VS","supply",12),Source("VC","clamp",12),Source("VI","input",d.high),u,
            Resistor("RL","supply","coil","100"),{"L1","Device:L","20m",{{"1","coil"},{"2","out"}}}});
        Configure(c,1e-6,true);c.config.signals={Voltage(c,3,2*d.channels+2),"i(Vca1)","i(Vca2)"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(.005);BOOST_CHECK_GT(-session.Snapshot().latest.values.at(1),.1);
        session.SetSourceValue("Vca3",0);session.AdvanceTo(.005002);const auto v=session.Snapshot().latest.values;
        BOOST_CHECK_GT(v.at(0),12.3);BOOST_CHECK_LT(v.at(0),14.5);
        BOOST_CHECK_GT(v.at(2),.07);BOOST_CHECK_LT(v.at(2),.13);
        BOOST_CHECK_SMALL(v.at(1)+v.at(2),.001);
        session.AdvanceTo(.008);BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(1),1e-5);
    }
}

BOOST_AUTO_TEST_CASE( JunctionCatalogCompositeWarningsRunWithAllDefaultSnapshotSignals )
{
    auto diode=Device("Diode:BAT54ADW");diode.pins={{"6","supply"},{"1","out1"},{"2","out2"},
        {"3","0"},{"4","0"},{"5","0"}};
    auto relay=Device("Relay:Relay_SPST-NO");relay.pins={{"A1","supply"},{"A2","0"},{"13","0"},{"14","0"}};
    auto bridge=Device("Device:D_Bridge_+-AA");bridge.pins={{"1","out"},{"2","0"},{"3","ac1"},{"4","ac2"}};
    auto router=Device("Analog_Switch:CD4066BE");
    for( auto& [pin,net] : router.pins ) net="0";
    for( const std::string pin : {"14","13","5","6","12"} ) router.pins[pin]="supply";
    router.pins["1"]="supply";router.pins["2"]="out";
    const std::vector<std::vector<CIRCUIT_COMPONENT>> benches{
        {Source("VS","supply",5),diode,Resistor("R1","out1","0","1k"),Resistor("R2","out2","0","2k")},
        {Source("VS","supply",12),relay},
        {Source("VS","ac1",12,"ac2"),bridge,Resistor("RL","out","0","100")},
        {Source("VS","supply",5),router,Resistor("RL","out","0","1k")}};
    const std::vector<double> expected{.0047,.03,.105,.0034};
    for( unsigned i=0;i<benches.size();++i ) BOOST_TEST_CONTEXT(benches[i][1].libraryId)
    {
        auto c=BuildCircuitSnapshot(benches[i]);Configure(c);
        auto selected=std::find_if(c.currentWarnings.begin(),c.currentWarnings.end(),
            [](const auto& warning){return warning.name=="U1";});
        BOOST_REQUIRE(selected!=c.currentWarnings.end());
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(50e-6);
        BOOST_CHECK_CLOSE(session.Snapshot().latest.values.at(selected->signal),expected[i],5);
    }
}
