/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( const std::string& ref, const std::string& net, double value,
                          const std::string& negative = "0" )
{
    return {ref,"Simulation_SPICE:VDC",std::to_string(value),{{"1",net},{"2",negative}}};
}
CIRCUIT_COMPONENT Resistor( const std::string& ref, const std::string& p,
                            const std::string& n, const std::string& value )
{
    return {ref,"Device:R",value,{{"1",p},{"2",n}}};
}
CIRCUIT_COMPONENT Device( const std::string& lib )
{
    CIRCUIT_COMPONENT u{"U1",lib,"10k",{}};
    for( const auto& p : MODEL_REPOSITORY().SelectModel(u).pins ) u.pins[p.defaultPin]="";
    return u;
}
std::string Voltage( const CIRCUIT_SNAPSHOT& c, std::size_t part, const std::string& pin )
{
    return "v("+c.pinNodes.at(part).at(pin)+")";
}
void Configure( CIRCUIT_SNAPSHOT& c, double step, bool initial = false )
{
    c.config.step=c.config.maxStep=step; c.config.horizon=.5;
    c.config.useInitialConditions=initial;
}
struct BRIDGE { std::string library; int positive,negative,ac1,ac2; };
// Physical mappings are independent of the order of ports in the model files.
std::vector<BRIDGE> Bridges()
{
    std::vector<BRIDGE> result{
        {"Device:D_Bridge_+-AA",1,2,3,4},{"Device:D_Bridge_+A-A",1,3,2,4},
        {"Device:D_Bridge_+AA-",1,4,2,3},{"Device:D_Bridge_-A+A",3,1,2,4},
        {"Device:D_Bridge_-AA+",4,1,2,3},{"Diode_Bridge:MB2S",3,4,1,2},
        {"Diode_Bridge:MB4S",3,4,1,2},{"Diode_Bridge:MB6S",3,4,1,2},
        {"Diode_Bridge:MB8S",1,2,3,4}};
    for( int code : {2,4,6,8,10} ) result.push_back({"Diode_Bridge:ABS"+std::to_string(code),1,2,3,4});
    for( const std::string name : {"W005G","W01G","W02G","W04G","W06G","W08G","W10G"} )
        result.push_back({"Diode_Bridge:"+name,1,3,2,4});
    return result;
}
CIRCUIT_SNAPSHOT BridgeCircuit( const BRIDGE& spec, bool capacitor = false )
{
    auto u=Device(spec.library);
    u.pins[std::to_string(spec.positive)]="out"; u.pins[std::to_string(spec.negative)]="0";
    u.pins[std::to_string(spec.ac1)]="ac1"; u.pins[std::to_string(spec.ac2)]="ac2";
    std::vector<CIRCUIT_COMPONENT> parts{Source("VAC","ac1",12,"ac2"),u,
        Resistor("RL","out","0",capacitor?"1k":"100")};
    if( capacitor ) parts.push_back({"CF","Device:C","100u",{{"1","out"},{"2","0"}}});
    auto c=BuildCircuitSnapshot(parts); Configure(c,10e-6,capacitor);
    c.config.signals={Voltage(c,1,std::to_string(spec.positive)),"i(Vca1)"};
    return c;
}
struct RELAY { std::string name; std::vector<std::array<int,3>> poles; };
const std::vector<RELAY> relays{
    {"SPST-NO",{{13,0,14}}},{"SPST-NC",{{11,12,0}}},{"SPDT",{{11,12,14}}},
    {"DPST-NO",{{13,0,14},{23,0,24}}},{"DPST-NC",{{11,12,0},{21,22,0}}},
    {"DPDT",{{11,12,14},{21,22,24}}}};
struct RELAY_BENCH
{
    CIRCUIT_SNAPSHOT circuit;
    std::unique_ptr<INTERACTIVE_SIM_SESSION> session;
    std::vector<bool> normallyClosed;
    std::vector<double> feeds;
    double time=0;
    RELAY_BENCH( const RELAY& spec, double resistance = 400, double inductance = .8,
                 double rated = 12 )
    {
        auto u=Device("Relay:Relay_"+spec.name); u.pins["A1"]="coil";u.pins["A2"]="0";
        u.modelParameters={{"coil_resistance",std::to_string(resistance)},
            {"coil_inductance",std::to_string(inductance)},{"rated_voltage",std::to_string(rated)}};
        std::vector<CIRCUIT_COMPONENT> parts{Source("VC","coil",0),u};
        for( unsigned i=0; i<spec.poles.size(); ++i )
        {
            const auto index=std::to_string(i), feed="feed"+index, ret="return"+index;
            // Each pole has its own floating battery; the coil has an explicit ground.
            parts.push_back(Source("VP"+index,feed,i?-4:7,ret));
            parts[1].pins[std::to_string(spec.poles[i][0])]=feed;
            for( unsigned throwIndex=1; throwIndex<=2; ++throwIndex )
            {
                const int pin=spec.poles[i][throwIndex]; if( !pin ) continue;
                const auto net="contact"+std::to_string(pin);
                parts[1].pins[std::to_string(pin)]=net;
                parts.push_back(Resistor("RL"+std::to_string(pin),net,ret,"50"));
                normallyClosed.push_back(throwIndex==1); feeds.push_back(i?-4:7);
            }
        }
        circuit=BuildCircuitSnapshot(parts);Configure(circuit,10e-6,true);
        BOOST_CHECK_EQUAL(circuit.automaticReferences.size(),spec.poles.size());
        circuit.config.signals={"i(Vca1)"};
        for( const auto& pole : spec.poles ) for( unsigned i=1; i<=2; ++i )
            if( pole[i] ) circuit.config.signals.push_back(Voltage(circuit,1,std::to_string(pole[i])));
        session=std::make_unique<INTERACTIVE_SIM_SESSION>(CIRCUITALIVE_TEST_WORKER);
        session->Configure(circuit.netlist,circuit.config); Advance(.001); Check(false);
    }
    void Set( double volts ) { session->SetSourceValue("Vca1",volts); }
    void Advance( double dt ) { session->AdvanceTo(time+=dt); }
    double CoilCurrent() const { return -session->Snapshot().latest.values.at(0); }
    void Check( bool energized ) const
    {
        const auto sample=session->Snapshot().latest;
        for( unsigned i=0; i<normallyClosed.size(); ++i )
            BOOST_CHECK_SMALL(sample.values.at(i+1)-
                (normallyClosed[i]!=energized ? feeds[i]*50/50.05 : 0),1e-5);
    }
};
}

BOOST_AUTO_TEST_CASE( PassiveCatalogBussedNetworksLoadEveryPhysicalLegAndAllDrawingVariants )
{
    for( int n=3; n<=13; ++n ) for( const std::string suffix : {"","_Split","_US"} )
        BOOST_TEST_CONTEXT("N="<<n<<" drawing="<<suffix)
    {
        auto u=Device("Device:R_Network"+(n<10?std::string("0"):"")+std::to_string(n)+suffix);
        u.value="2k2"; u.pins["1"]="bus";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","bus",5),u};
        for( int i=1; i<=n; ++i )
        {
            auto net="leg"+std::to_string(i); parts[1].pins[std::to_string(i+1)]=net;
            parts.push_back(Resistor("RL"+std::to_string(i),net,"0",std::to_string(i*1000)));
        }
        auto c=BuildCircuitSnapshot(parts);Configure(c,1e-6);c.config.signals={"i(Vca1)"};
        for( int pin=2; pin<=n+1; ++pin ) c.config.signals.push_back(Voltage(c,1,std::to_string(pin)));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        for( int phase=0; phase<2; ++phase )
        {
            if( phase ) session.SetSourceValue("Vca1",-3);
            session.AdvanceTo((phase+1)*10e-6); const auto values=session.Snapshot().latest.values;
            const double v=phase?-3:5; double current=0;
            for( int i=1; i<=n; ++i )
            {
                const double expected=v*(i*1000)/(2200+i*1000);
                BOOST_CHECK_SMALL(values.at(i)-expected,1e-7); current+=v/(2200+i*1000);
            }
            BOOST_CHECK_SMALL(values.at(0)+current,1e-9);
        }
    }
}

BOOST_AUTO_TEST_CASE( PassiveCatalogDualTerminatorsHaveIndependentLoadedTapsAndTwoSharedBuses )
{
    for( int n=2; n<=11; ++n ) BOOST_TEST_CONTEXT("N="<<n)
    {
        auto u=Device("Device:R_Network_Dividers_x"+(n<10?std::string("0"):"")+std::to_string(n)+"_SIP");
        u.modelParameters={{"upper_resistance","2k"},{"lower_resistance","3k"}};
        u.pins["1"]="upper";u.pins[std::to_string(n+2)]="lower";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VU","upper",8),Source("VL","lower",-2),u};
        for( int i=1; i<=n; ++i )
        {
            const auto net="tap"+std::to_string(i);parts[2].pins[std::to_string(i+1)]=net;
            if( i%2 ) parts.push_back(Resistor("RL"+std::to_string(i),net,"0","6k"));
        }
        auto c=BuildCircuitSnapshot(parts);Configure(c,1e-6);c.config.signals={"i(Vca1)","i(Vca2)"};
        for( int pin=2; pin<=n+1; ++pin ) c.config.signals.push_back(Voltage(c,2,std::to_string(pin)));
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        session.AdvanceTo(10e-6);auto values=session.Snapshot().latest.values;
        double upper=0,lower=0;
        for( int i=1; i<=n; ++i )
        {
            const double expected=(8/2000.0-2/3000.0)/(1/2000.0+1/3000.0+(i%2?1/6000.0:0));
            BOOST_CHECK_SMALL(values.at(i+1)-expected,1e-7);
            upper+=(8-expected)/2000;lower+=(-2-expected)/3000;
        }
        BOOST_CHECK_SMALL(values.at(0)+upper,1e-9);BOOST_CHECK_SMALL(values.at(1)+lower,1e-9);
    }
}

BOOST_AUTO_TEST_CASE( PassiveCatalogBridgePinoutsRectifyBothInputPolaritiesWithTwoDiodeDrops )
{
    for( const auto& spec : Bridges() ) BOOST_TEST_CONTEXT(spec.library)
    {
        auto c=BridgeCircuit(spec);INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(c.netlist,c.config);
        for( int phase=0; phase<2; ++phase )
        {
            if( phase ) session.SetSourceValue("Vca1",-12);
            session.AdvanceTo((phase+1)*.001);const auto values=session.Snapshot().latest.values;
            BOOST_CHECK_GT(values.at(0),9.7);BOOST_CHECK_LT(values.at(0),11);
            BOOST_CHECK_SMALL(std::abs(values.at(1))-values.at(0)/100,1e-6);
            BOOST_CHECK(phase ? values.at(1)>0 : values.at(1)<0);
        }
    }
}

BOOST_AUTO_TEST_CASE( PassiveCatalogBridgeFullWaveOutputHasTwiceTheAcFrequency )
{
    auto c=BridgeCircuit({"Diode_Bridge:W04G",1,3,2,4});
    const auto pos=c.netlist.find("Vca1 "), end=c.netlist.find('\n',pos);
    c.netlist.replace(pos,end-pos,"Vca1 "+c.pinNodes[0].at("1")+" "+c.pinNodes[0].at("2")+" SIN(0 12 100)");
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    std::vector<double> peaks;
    for( int half=0; half<8; ++half )
    {
        session.AdvanceTo(.0025+half*.005);const double peak=session.Snapshot().latest.values.at(0);
        BOOST_CHECK_GT(peak,9.7);BOOST_CHECK_LT(peak,11);peaks.push_back(peak);
        session.AdvanceTo(.005+half*.005);BOOST_CHECK_LT(session.Snapshot().latest.values.at(0),.15);
    }
    for( double peak : peaks ) BOOST_CHECK_SMALL(peak-peaks.front(),.005);
}

BOOST_AUTO_TEST_CASE( PassiveCatalogBridgeChargesReservoirAndBlocksDischargeIntoAcSource )
{
    auto c=BridgeCircuit({"Diode_Bridge:ABS4",1,2,3,4},true);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    session.AdvanceTo(.005);const double charged=session.Snapshot().latest.values.at(0);
    BOOST_CHECK_GT(charged,10);session.SetSourceValue("Vca1",0);session.AdvanceTo(.015);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(0)-charged*std::exp(-.01/.1),.01);
    BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(1),1e-6);
}

BOOST_AUTO_TEST_CASE( PassiveCatalogBridgesExposeIndividualDiodeBlockingAndForwardPaths )
{
    // Isolate each junction: the unused opposite terminal is left open.
    for( const auto& spec : Bridges() ) for( bool upper : {false,true} )
        for( int ac : {spec.ac1,spec.ac2} ) BOOST_TEST_CONTEXT(spec.library<<" upper="<<upper<<" AC="<<ac)
    {
        auto u=Device(spec.library);const int anode=upper?ac:spec.negative,cathode=upper?spec.positive:ac;
        u.pins[std::to_string(anode)]="anode";u.pins[std::to_string(cathode)]="0";
        auto c=BuildCircuitSnapshot({Source("VS","anode",-20),u});Configure(c,1e-6);
        c.config.signals={"i(Vca1)"};INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);
        session.Configure(c.netlist,c.config);session.AdvanceTo(10e-6);
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(0),1e-6);
        session.SetSourceValue("Vca1",.8);session.AdvanceTo(20e-6);
        BOOST_CHECK_LT(session.Snapshot().latest.values.at(0),-.01);
        BOOST_CHECK_GT(session.Snapshot().latest.values.at(0),-2);
    }
}

BOOST_AUTO_TEST_CASE( PassiveCatalogRelayCoilsAreInductiveAndContactsAreIsolatedBilateralPaths )
{
    for( const auto& spec : relays ) for( double polarity : {-1,1} )
        BOOST_TEST_CONTEXT(spec.name<<" coil polarity="<<polarity)
    {
        RELAY_BENCH b(spec);b.Set(polarity*12);b.Advance(.002);
        BOOST_CHECK_SMALL(b.CoilCurrent()-polarity*.03*(1-std::exp(-1)),1e-5);b.Check(false);
        b.Advance(.010);b.Check(true);BOOST_CHECK_SMALL(b.CoilCurrent()-polarity*.03,1e-4);
        b.Set(0);b.Advance(.002);
        BOOST_CHECK_SMALL(b.CoilCurrent()-polarity*.03*(1-std::exp(-6))*std::exp(-1),1e-5);b.Check(true);
        b.Advance(.010);b.Check(false);
    }
}

BOOST_AUTO_TEST_CASE( PassiveCatalogRelayPickupDropoutHysteresisRetainsStateBetweenThresholds )
{
    RELAY_BENCH b(relays.back());
    b.Set(6);b.Advance(.025);b.Check(false);
    b.Set(12);b.Advance(.025);b.Check(true);
    b.Set(6);b.Advance(.025);b.Check(true);
    b.Set(.5);b.Advance(.025);b.Check(false);
    b.Set(6);b.Advance(.025);b.Check(false);
}

BOOST_AUTO_TEST_CASE( PassiveCatalogRelayDelaysIncludeElectricalRiseAndDecay )
{
    RELAY_BENCH b(relays.at(2));b.Set(12);
    // tau=2 ms; 75% pickup at 2.773 ms, then 5 ms operate delay.
    b.Advance(.007);b.Check(false);b.Advance(.0012);b.Check(true);
    b.Set(0);
    // 10% dropout after about 4.57 ms, then 3 ms release delay.
    b.Advance(.007);b.Check(true);b.Advance(.0012);b.Check(false);
}

BOOST_AUTO_TEST_CASE( PassiveCatalogRelayTransferOpensBothThrowsDuringTravel )
{
    RELAY_BENCH b(relays.at(2));b.Set(12);bool gap=false;
    b.Advance(.0075);
    for( int i=0; i<70; ++i )
    {
        b.Advance(10e-6);const auto values=b.session->Snapshot().latest.values;
        const bool nc=std::abs(values.at(1))>1,no=std::abs(values.at(2))>1;
        BOOST_CHECK(!(nc&&no));gap|=!nc&&!no;
    }
    BOOST_CHECK(gap);b.Check(true);
}

BOOST_AUTO_TEST_CASE( PassiveCatalogRelayCoilParametersScaleCurrentAndOperatingVoltage )
{
    RELAY_BENCH b(relays.front(),100,.2,5);b.Set(5);b.Advance(.015);
    b.Check(true);BOOST_CHECK_SMALL(b.CoilCurrent()-.05,5e-5);
    b.Set(0);b.Advance(.015);b.Check(false);
}

BOOST_AUTO_TEST_CASE( PassiveCatalogRelayExternalFlybackDiodeCarriesTheInterruptedCoilCurrent )
{
    auto u=Device("Relay:Relay_SPST-NO");u.pins={{"A1","coil"},{"A2","return"},
        {"13","supply"},{"14","out"}};
    auto c=BuildCircuitSnapshot({Source("VS","supply",12),Source("VM","supply",0,"coil"),u,
        {"SW1","Switch:SW_SPST","SW_SPST",{{"1","return"},{"2","0"}}},
        {"DF","Diode:1N4007","1N4007",{{"1","supply"},{"2","return"}}},
        Resistor("RL","out","0","1k")});
    Configure(c,5e-6,true);
    c.config.signals={Voltage(c,2,"A1"),Voltage(c,2,"A2"),"i(Vca2)",Voltage(c,2,"14")};
    BOOST_REQUIRE_EQUAL(c.switches.size(),1u);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
    session.AdvanceTo(.025);auto values=session.Snapshot().latest.values;
    BOOST_CHECK_SMALL(values.at(2)-.03,2e-5);BOOST_CHECK_GT(values.at(3),11.9);
    session.SetSourceValue(c.switches[0].controlSource,c.switches[0].off);
    session.AdvanceTo(.02505);values=session.Snapshot().latest.values;
    BOOST_CHECK_GT(values.at(0)-values.at(1),-1.3);BOOST_CHECK_LT(values.at(0)-values.at(1),-.3);
    BOOST_CHECK_GT(values.at(2),.025);BOOST_CHECK_GT(values.at(3),11.9);
    session.AdvanceTo(.045);values=session.Snapshot().latest.values;
    BOOST_CHECK_SMALL(values.at(2),1e-6);BOOST_CHECK_SMALL(values.at(3),1e-5);
}
