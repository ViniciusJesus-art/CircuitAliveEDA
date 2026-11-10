/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Supply( const std::string& ref, const std::string& net, double value )
{
    return { ref, "Simulation_SPICE:VDC", std::to_string( value ), {{"1", net}, {"2", "0"}} };
}
CIRCUIT_COMPONENT Load( const std::string& ref, const std::string& net )
{
    return { ref, "Device:R", "1k", {{"1", net}, {"2", "0"}} };
}
CIRCUIT_COMPONENT Device( const std::string& library )
{
    CIRCUIT_COMPONENT u{ "U1", library, library.substr( library.find( ':' ) + 1 ), {} };
    for( const auto& p : MODEL_REPOSITORY().SelectModel( u ).pins ) u.pins[p.defaultPin] = "";
    return u;
}
std::string Voltage( const CIRCUIT_SNAPSHOT& c, std::size_t device, int pin )
{
    return "v(" + c.pinNodes.at( device ).at( std::to_string( pin ) ) + ")";
}
void Extra( CIRCUIT_SNAPSHOT& c, const std::string& line )
{
    c.netlist.insert( c.netlist.rfind( ".end" ), line + '\n' );
}
struct ROUTER
{
    std::string library;
    std::vector<int> select, common;
    std::vector<std::vector<int>> channels;
    double resistance;
    bool independent = false;
    bool wide = false;
};
// Physical mappings and reference resistances from the datasheets, independent
// of the .casim port order and expressions under test. Split supplies are +/-5 V.
const std::vector<int> octal{13,14,15,12,1,5,2,4};
const std::vector<int> sixteen{9,8,7,6,5,4,3,2,23,22,21,20,19,18,17,16};
const std::vector<ROUTER> routers{
    {"4xxx:4051", {11,10,9}, {3}, {octal}, 180},
    {"Analog_Switch:CD4052B", {10,9}, {13,3}, {{12,14,15,11},{1,5,2,4}}, 180},
    {"Analog_Switch:CD4053B", {11,10,9}, {14,15,4}, {{12,13},{2,1},{5,3}}, 180, true},
    {"74xx:74HC4051", {11,10,9}, {3}, {octal}, 70},
    {"74xx:74HCT4051", {11,10,9}, {3}, {octal}, 70},
    {"74xx:CD74HC4067M", {10,11,14,13}, {1}, {sixteen}, 70-10.0/3, false, true},
    {"Analog_Switch:HEF4067BT", {10,11,14,13}, {1}, {sixteen}, 350, false, true}
};
double Input( const ROUTER& r, unsigned bank, unsigned channel )
{
    return ( r.wide ? .2 : -1.2 ) + .2*channel + .1*bank;
}
}

BOOST_AUTO_TEST_CASE( RoutingCatalogQuadSwitchesAreIndependentAndBilateral )
{
    for( const std::string library : {"4xxx:4016", "Analog_Switch:CD4066BE"} )
        for( bool reverse : {false, true} ) BOOST_TEST_CONTEXT( library << " reverse=" << reverse )
    {
        auto u = Device( library ); u.pins["14"] = "vcc"; u.pins["7"] = "vss";
        const std::array<int,4> a{1,3,8,10}, b{2,4,9,11}, en{13,5,6,12};
        std::vector<CIRCUIT_COMPONENT> parts{Supply("VS","vcc",5), Supply("VN","vss",-5)};
        for( unsigned i=0; i<4; ++i )
        {
            parts.push_back( Supply("VE"+std::to_string(i), "en"+std::to_string(i), i%2 ? -5 : 5) );
            u.pins[std::to_string(en[i])] = "en"+std::to_string(i);
            u.pins[std::to_string(reverse ? b[i] : a[i])] = "in"+std::to_string(i);
            u.pins[std::to_string(reverse ? a[i] : b[i])] = "out"+std::to_string(i);
        }
        const auto device = parts.size(); parts.push_back(u);
        for( unsigned i=0; i<4; ++i )
        {
            parts.push_back(Supply("VI"+std::to_string(i), "in"+std::to_string(i), -2.0+i));
            parts.push_back(Load("RL"+std::to_string(i), "out"+std::to_string(i)));
        }
        auto c = BuildCircuitSnapshot(parts); c.config.signals.clear();
        c.config.step=c.config.maxStep=50e-9; c.config.horizon=.001;
        for( unsigned i=0; i<4; ++i ) c.config.signals.push_back(Voltage(c,device,reverse ? a[i] : b[i]));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
        const double gain = 1000.0/(1000+(library=="4xxx:4016" ? 250 : 180));
        for( unsigned phase=0; phase<2; ++phase )
        {
            if( phase ) for( unsigned i=0; i<4; ++i )
                session.SetSourceValue("Vca"+std::to_string(3+i), i%2 ? 5 : -5);
            session.AdvanceTo((phase+1)*10e-6);
            for( unsigned i=0; i<4; ++i )
                BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(i) -
                    ((i%2==phase) ? (-2.0+i)*gain : 0), .001);
        }
        session.SetSourceValue("Vca1",-5); session.AdvanceTo(30e-6);
        for( unsigned i=0; i<4; ++i ) BOOST_CHECK_SMALL(session.Snapshot().latest.values[i],1e-6);
    }
}

BOOST_AUTO_TEST_CASE( RoutingCatalogAllMuxAddressesAndPhysicalBanksPassAnalogBothWays )
{
    for( const auto& r : routers ) for( bool reverse : {false,true} )
        BOOST_TEST_CONTEXT(r.library << " reverse=" << reverse)
    {
        auto u=Device(r.library); u.pins[r.wide?"24":"16"]="vcc";
        u.pins[r.wide?"12":"8"]="0"; u.pins[r.wide?"15":"6"]="inhibit";
        if( !r.wide ) u.pins["7"]="vee";
        std::vector<CIRCUIT_COMPONENT> parts{Supply("VS","vcc",5),Supply("VN","vee",-5)};
        for( unsigned bit=0; bit<r.select.size(); ++bit )
        {
            const auto net="s"+std::to_string(bit); u.pins[std::to_string(r.select[bit])]=net;
            parts.push_back(Supply("VA"+std::to_string(bit),net,0));
        }
        const auto inhibit=parts.size()+1;
        parts.push_back(Supply("VE","inhibit",0));
        for( unsigned bank=0; bank<r.common.size(); ++bank )
        {
            const auto net="common"+std::to_string(bank); u.pins[std::to_string(r.common[bank])]=net;
            for( unsigned ch=0; ch<r.channels[bank].size(); ++ch )
                u.pins[std::to_string(r.channels[bank][ch])]="channel"+std::to_string(bank)+"_"+std::to_string(ch);
        }
        const auto device=parts.size(); parts.push_back(u);
        for( unsigned bank=0; bank<r.common.size(); ++bank )
        {
            const auto net="common"+std::to_string(bank);
            if( reverse ) parts.push_back(Supply("VI"+std::to_string(bank),net,Input(r,bank,0)));
            else parts.push_back(Load("RL"+std::to_string(bank),net));
            for( unsigned ch=0; ch<r.channels[bank].size(); ++ch )
            {
                const auto key=std::to_string(bank)+"_"+std::to_string(ch), n="channel"+key;
                if( reverse ) parts.push_back(Load("RL"+key,n));
                else parts.push_back(Supply("VI"+key,n,Input(r,bank,ch)));
            }
        }
        auto c=BuildCircuitSnapshot(parts); c.config.signals.clear();
        c.config.step=c.config.maxStep=50e-9; c.config.horizon=.001;
        for( unsigned bank=0; bank<r.common.size(); ++bank )
            if( reverse ) for( int p : r.channels[bank] ) c.config.signals.push_back(Voltage(c,device,p));
            else c.config.signals.push_back(Voltage(c,device,r.common[bank]));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
        const double gain=1000/(1000+r.resistance); double time=0;
        for( unsigned pattern=0; pattern<(1u<<r.select.size()); ++pattern )
        {
            const unsigned address=pattern^(pattern>>1);
            if( pattern ) for( unsigned bit=0; bit<r.select.size(); ++bit )
                session.SetSourceValue("Vca"+std::to_string(3+bit),address&(1u<<bit)?5:0);
            session.AdvanceTo(time+=5e-6); unsigned signal=0;
            for( unsigned bank=0; bank<r.common.size(); ++bank )
            {
                const unsigned selected=r.independent?((address>>bank)&1):address;
                if( reverse ) for( unsigned ch=0; ch<r.channels[bank].size(); ++ch )
                    BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(signal++)-
                        (ch==selected ? Input(r,bank,0)*gain : 0),.001);
                else BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(signal++)-Input(r,bank,selected)*gain,.001);
            }
        }
        session.SetSourceValue("Vca"+std::to_string(inhibit),5); session.AdvanceTo(time+=5e-6);
        for( double value : session.Snapshot().latest.values ) BOOST_CHECK_SMALL(value,1e-6);
        session.SetSourceValue("Vca"+std::to_string(inhibit),0);
        session.SetSourceValue("Vca1",0); session.AdvanceTo(time+=5e-6);
        for( double value : session.Snapshot().latest.values ) BOOST_CHECK_SMALL(value,1e-6);
    }
}

BOOST_AUTO_TEST_CASE( RoutingCatalogMuxControlDelayIncludesAnOpenInterval )
{
    auto u=Device("4xxx:4051"); u.pins["16"]="vcc";
    for( auto& [pin,net] : u.pins ) if( pin!="16" ) net="0";
    u.pins["11"]="select"; u.pins["13"]="a"; u.pins["14"]="b"; u.pins["3"]="out";
    auto c=BuildCircuitSnapshot({Supply("VS","vcc",5),Supply("VA","select",0),
        Supply("VI0","a",1),Supply("VI1","b",2),u,Load("RL","out")});
    c.config.signals={Voltage(c,4,3)}; c.config.maxStep=c.config.step=1e-9; c.config.horizon=20e-6;
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
    session.AdvanceTo(2e-6); BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-1000./1470,.001);
    session.SetSourceValue("Vca2",5); session.AdvanceTo(2.05e-6);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-1000./1470,.001);
    session.AdvanceTo(2.3e-6); BOOST_CHECK_SMALL(session.Snapshot().latest.values[0],.001);
    session.AdvanceTo(2.6e-6); BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-2000./1470,.001);
}

BOOST_AUTO_TEST_CASE( RoutingCatalogUndefinedAndFloatingAddressesDoNotSelectAChannel )
{
    for( const std::string chip : {"4xxx:4051","74xx:74HC4051","74xx:74HCT4051"} )
        for( bool floating : {false,true} ) BOOST_TEST_CONTEXT(chip << " floating=" << floating)
    {
        auto u=Device(chip);
        for( auto& [pin,net] : u.pins ) net="0";
        u.pins["16"]="vcc"; u.pins["3"]="out"; u.pins["11"]=floating?"":"address";
        for( int pin : octal ) u.pins[std::to_string(pin)]="in";
        auto c=BuildCircuitSnapshot({Supply("VS","vcc",5),Supply("VI","in",2),
            Supply("VA","address",chip=="74xx:74HCT4051"?1.4:2.5),u,Load("RL","out")});
        c.config.signals={Voltage(c,3,3)};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
        session.AdvanceTo(10e-6); BOOST_CHECK_SMALL(session.Snapshot().latest.values[0],1e-6);
        if( !floating )
        {
            session.SetSourceValue("Vca3",0); session.AdvanceTo(20e-6);
            BOOST_CHECK_GT(session.Snapshot().latest.values[0],1.3);
            if( chip=="74xx:74HCT4051" )
            {
                session.SetSourceValue("Vca3",2.1); session.AdvanceTo(30e-6);
                BOOST_CHECK_GT(session.Snapshot().latest.values[0],1.3);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE( RoutingCatalogResistanceTracksSupplyReferencePoints )
{
    struct SPEC { std::string library; int vp,ground,a,b; std::array<double,3> supply,resistance; bool hc=false,hct=false; };
    const std::vector<SPEC> specs{
        {"4xxx:4016",14,7,1,2,{5,10,15},{580,250,200}},
        {"4xxx:4066",14,7,1,2,{5,10,15},{470,180,125}},
        {"4xxx:4051",16,8,3,13,{5,10,15},{470,180,125}},
        {"4xxx:4052",16,8,13,12,{5,10,15},{470,180,125}},
        {"4xxx:4053",16,8,14,12,{5,10,15},{470,180,125}},
        {"74xx:74HC4051",16,8,3,13,{4.5,6,9},{100,90,70},true},
        {"74xx:74HCT4051",16,8,3,13,{4.5,6,9},{100,90,70},true,true},
        {"74xx:CD74HC4067SM",24,12,1,9,{4.5,6,6},{70,60,60}},
        {"Analog_Switch:HEF4067BTT",24,12,1,9,{5,10,15},{350,80,60}}
    };
    for( const auto& s : specs ) for( unsigned point=0; point<3; ++point )
        BOOST_TEST_CONTEXT(s.library << " span=" << s.supply[point])
    {
        auto u=Device(s.library); for( auto& [pin,net] : u.pins ) net="0";
        u.pins[std::to_string(s.vp)]="vcc";
        if( s.vp==14 ) u.pins["13"]="vcc";
        if( s.vp==16 ) u.pins["7"]="vee";
        u.pins[std::to_string(s.a)]="in"; u.pins[std::to_string(s.b)]="out";
        const double logic=s.hct?std::min(5.,s.supply[point]):s.supply[point];
        auto c=BuildCircuitSnapshot({Supply("VS","vcc",logic),Supply("VN","vee",logic-s.supply[point]),
            Supply("VI","in",1),u,Load("RL","out")});
        c.config.signals={Voltage(c,3,s.b)};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
        session.AdvanceTo(10e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0]-1000/(1000+s.resistance[point]),.001);
        session.SetSourceValue("Vca1",.5); session.AdvanceTo(20e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0],1e-6);
        session.SetSourceValue("Vca1",25); session.AdvanceTo(30e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values[0],1e-6);
    }
}

BOOST_AUTO_TEST_CASE( ProtectionCatalogEveryBzv55VoltageAndToleranceGradeRegulatesAndConductsForward )
{
    struct ZENER { std::string suffix; double voltage,resistance; };
    const std::vector<ZENER> series{
        {"2V4",2.4,70},{"2V7",2.7,75},{"3V0",3,80},{"3V3",3.3,85},
        {"3V6",3.6,85},{"3V9",3.9,85},{"4V3",4.3,80},{"4V7",4.7,50},
        {"5V1",5.1,40},{"5V6",5.6,15},{"6V2",6.2,6},{"6V8",6.8,6},
        {"7V5",7.5,6},{"8V2",8.2,6},{"9V1",9.1,6},{"10",10,8},
        {"11",11,10},{"12",12,10},{"13",13,10},{"15",15,10},
        {"16",16,10},{"18",18,10},{"20",20,15},{"22",22,20},{"24",24,25},
        {"27",27,25},{"30",30,30},{"33",33,35},{"36",36,35},{"39",39,40},
        {"43",43,45},{"47",47,50},{"51",51,60},{"56",56,70},{"62",62,80},
        {"68",68,90},{"75",75,95}
    };
    for( const auto& z : series ) for( const std::string grade : {"B","C"} )
        BOOST_TEST_CONTEXT("BZV55" << grade << z.suffix)
    {
        auto u=Device("Diode:BZV55"+grade+z.suffix); u.pins["1"]="out"; u.pins["2"]="0";
        auto c=BuildCircuitSnapshot({u}); const auto node=c.pinNodes[0].at("1");
        const double current=z.voltage<=24?.005:.002;
        Extra(c,"Iinject 0 "+node+" "+std::to_string(current));
        c.config.signals={Voltage(c,0,1)};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
        session.AdvanceTo(100e-6); const double nominal=session.Snapshot().latest.values[0];
        BOOST_CHECK_SMALL(nominal-z.voltage,.005);
        session.SetSourceValue("Iinject",current*1.1); session.AdvanceTo(200e-6);
        const double slope=(session.Snapshot().latest.values[0]-nominal)/(current*.1);
        BOOST_CHECK_GT(slope,z.resistance*.85); BOOST_CHECK_LT(slope,z.resistance*1.1);
        session.SetSourceValue("Iinject",-.01); session.AdvanceTo(300e-6);
        const double forward=-session.Snapshot().latest.values[0];
        BOOST_CHECK_GT(forward,.5); BOOST_CHECK_LT(forward,.9);
    }
}
