/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( const std::string& ref, const std::string& net, double value )
{
    return { ref, "Simulation_SPICE:VDC", std::to_string(value), {{"1",net},{"2","0"}} };
}
CIRCUIT_COMPONENT Device( const std::string& library )
{
    CIRCUIT_COMPONENT u{ "U1", library, library.substr(library.find(':')+1), {} };
    for( const auto& p : MODEL_REPOSITORY().SelectModel(u).pins ) u.pins[p.defaultPin]="0";
    return u;
}
const std::vector<std::string> families{"74HC","74HCT","74AHC","74AHCT","74LS"};
const std::vector<int> data8{3,4,7,8,13,14,17,18}, outputs8{2,5,6,9,12,15,16,19};
const std::vector<int> shiftOutputs{15,1,2,3,4,5,6,7};
const std::vector<int> parallel165{11,12,13,14,3,4,5,6};

// Bench wiring and expected words use physical pins from the datasheets; neither
// the .casim port order nor its logic expressions supply the expected results.
struct BENCH
{
    CIRCUIT_SNAPSHOT circuit;
    std::unique_ptr<INTERACTIVE_SIM_SESSION> session;
    std::map<int,std::string> sources;
    std::vector<int> outputs;
    std::string library;
    double time=0;

    BENCH( const std::string& lib, int vp, int gnd,
           const std::map<int,double>& inputs, const std::vector<int>& watch,
           const std::map<int,std::string>& ties={} ) : outputs(watch), library(lib)
    {
        auto u=Device(lib); u.pins[std::to_string(vp)]="vcc"; u.pins[std::to_string(gnd)]="0";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","vcc",5),Source("VP","pull",3.3)};
        sources[vp]="Vca1";
        for( const auto& [pin,value] : inputs )
        {
            const auto net="input"+std::to_string(pin); u.pins[std::to_string(pin)]=net;
            sources[pin]="Vca"+std::to_string(parts.size()+1);
            parts.push_back(Source("VI"+std::to_string(pin),net,value));
        }
        for( const auto& [pin,net] : ties ) u.pins[std::to_string(pin)]=net;
        for( int pin : watch ) u.pins[std::to_string(pin)]="out"+std::to_string(pin);
        const auto device=parts.size(); parts.push_back(u);
        for( int pin : watch ) parts.push_back({"RP"+std::to_string(pin),"Device:R","100k",
            {{"1","out"+std::to_string(pin)},{"2","pull"}}});
        circuit=BuildCircuitSnapshot(parts); circuit.config.signals.clear();
        circuit.config.step=circuit.config.maxStep=20e-9; circuit.config.horizon=.1;
        for( int pin : watch ) circuit.config.signals.push_back("v("+circuit.pinNodes[device].at(std::to_string(pin))+")");
        session=std::make_unique<INTERACTIVE_SIM_SESSION>(CIRCUITALIVE_TEST_WORKER);
        session->Configure(circuit.netlist,circuit.config);
        Settle(); // Source edits require an initialized, confirmed pause.
    }
    void Set( int pin, double value ) { session->SetSourceValue(sources.at(pin),value); }
    void SetWord( const std::vector<int>& pins, unsigned word )
    {
        for( unsigned i=0; i<pins.size(); ++i ) Set(pins[i],word&(1u<<i)?5:0);
    }
    void Settle( double duration=2e-6 ) { session->AdvanceTo(time+=duration); }
    void Pulse( int pin ) { Set(pin,0); Settle(); Set(pin,5); Settle(); }
    void Check( unsigned word ) const
    {
        const auto sample=session->Snapshot().latest;
        const bool ls=library.starts_with("74xx:74LS");
        for( unsigned bit=0; bit<outputs.size(); ++bit ) BOOST_TEST_CONTEXT("pin "<<outputs[bit]<<" word="<<word)
            BOOST_CHECK_SMALL(sample.values.at(bit)-(word&(1u<<bit)?(ls?3.4:5):(ls?.2:0)),.03);
    }
    void CheckZ() const
    {
        for( double value : session->Snapshot().latest.values ) BOOST_CHECK_SMALL(value-3.3,.002);
    }
};
std::map<int,double> Inputs( const std::vector<int>& pins, std::map<int,double> controls )
{
    for( int pin : pins ) controls[pin]=0;
    return controls;
}
std::vector<std::string> NewLibraries()
{
    std::vector<std::string> libs;
    for( const auto& f : families ) for( const std::string suffix : {"273","373","374","595"} )
        libs.push_back("74xx:"+f+suffix);
    for( const std::string chip : {"74HC164","74HCT164","74HC165","74LS165","74HC138",
            "74HCT138","74LS138","74LS139","74LS161","74LS163","74LS174","74LS175","74LS393"} )
        libs.push_back("74xx:"+chip);
    libs.push_back("4xxx:4020"); libs.push_back("4xxx:4040");
    return libs;
}
}

BOOST_AUTO_TEST_CASE( RegisterCatalogParallelRegistersCaptureAllPhysicalBitsAndClearAsynchronously )
{
    struct SPEC { std::string name; int vp,gnd,clock; std::vector<int> data,out,inverted; };
    std::vector<SPEC> specs;
    for( const auto& f : families ) specs.push_back({f+"273",20,10,11,data8,outputs8,{}});
    specs.push_back({"74LS174",16,8,9,{3,4,6,11,13,14},{2,5,7,10,12,15},{}});
    specs.push_back({"74LS175",16,8,9,{4,5,12,13},{2,7,10,15},{3,6,11,14}});
    for( const auto& s : specs ) BOOST_TEST_CONTEXT(s.name)
    {
        auto outs=s.out; outs.insert(outs.end(),s.inverted.begin(),s.inverted.end());
        BENCH b("74xx:"+s.name,s.vp,s.gnd,Inputs(s.data,{{1,0},{s.clock,0}}),outs);
        const unsigned mask=(1u<<s.data.size())-1;
        auto check=[&](unsigned word){b.Check(word|(s.inverted.empty()?0:((word^mask)<<s.data.size())));};
        b.Settle(); check(0); b.Set(1,5); b.Settle();
        for( unsigned bit=0; bit<s.data.size(); ++bit )
        {
            const unsigned word=1u<<bit; b.SetWord(s.data,word); b.Settle();
            b.Pulse(s.clock); check(word);
            b.SetWord(s.data,word^mask); b.Settle(); check(word);
            b.Set(s.clock,0); b.Settle(); check(word); // falling edge must not capture
        }
        b.Set(1,0); b.Settle(); check(0);
        b.Set(s.vp,0); b.Settle(); b.CheckZ();
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog373IsTransparentAndHoldsWhileItsOutputsAreDisabled )
{
    for( const auto& f : families ) BOOST_TEST_CONTEXT(f)
    {
        BENCH b("74xx:"+f+"373",20,10,Inputs(data8,{{1,0},{11,5}}),outputs8);
        b.Settle(); b.Check(0);
        for( unsigned word : {0x55u,0xaau,0x81u,0x18u} )
        { b.SetWord(data8,word); b.Settle(); b.Check(word); }
        b.Set(11,0); b.Settle(); b.SetWord(data8,0xe7); b.Settle(); b.Check(0x18);
        b.Set(1,5); b.Settle(); b.CheckZ();
        b.Set(11,5); b.Settle(); b.SetWord(data8,0xa5); b.Settle(); b.CheckZ();
        b.Set(11,0); b.Settle(); b.SetWord(data8,0); b.Set(1,0); b.Settle(); b.Check(0xa5);
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog374UsesRisingEdgesAndOutputEnableDoesNotClearStorage )
{
    for( const auto& f : families ) BOOST_TEST_CONTEXT(f)
    {
        BENCH b("74xx:"+f+"374",20,10,Inputs(data8,{{1,0},{11,0}}),outputs8);
        b.Settle(); b.Pulse(11); b.Check(0);
        for( unsigned bit=0; bit<8; ++bit )
        {
            b.SetWord(data8,1u<<bit); b.Settle(); b.Pulse(11); b.Check(1u<<bit);
            b.SetWord(data8,0xff^(1u<<bit)); b.Settle(); b.Check(1u<<bit);
            b.Set(11,0); b.Settle(); b.Check(1u<<bit);
        }
        b.Set(1,5); b.Settle(); b.CheckZ();
        b.SetWord(data8,0xa5); b.Settle(); b.Pulse(11); b.CheckZ();
        b.Set(1,0); b.Settle(); b.Check(0xa5);
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog164ShiftsOneStagePerEdgeAndAndsBothSerialInputs )
{
    for( const std::string chip : {"74HC164","74HCT164"} ) BOOST_TEST_CONTEXT(chip)
    {
        BENCH b("74xx:"+chip,14,7,{{1,0},{2,5},{8,0},{9,0}},{3,4,5,6,10,11,12,13});
        b.Settle(); b.Check(0); b.Set(9,5); b.Settle(); unsigned word=0;
        for( unsigned i=0; i<20; ++i )
        {
            const bool a=(0xa5u>>(i%8))&1, other=(i%5)!=0;
            b.Set(1,a?5:0); b.Set(2,other?5:0); b.Settle();
            b.Pulse(8); word=((word<<1)|(a&&other))&0xff; b.Check(word);
            b.Set(8,0); b.Settle(); b.Check(word);
        }
        b.Set(9,0); b.Settle(); b.Check(0);
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog595SeparatesShiftStorageResetAndOutputEnable )
{
    for( const auto& f : families ) BOOST_TEST_CONTEXT(f)
    {
        auto watch=shiftOutputs; watch.push_back(9);
        BENCH b("74xx:"+f+"595",16,8,{{10,0},{11,0},{12,0},{13,0},{14,0}},watch);
        b.Settle(); b.Pulse(12); b.Check(0); b.Set(10,5); b.Settle();
        unsigned shifted=0;
        for( unsigned bit=0; bit<8; ++bit )
        {
            const bool data=(0xa5u>>(7-bit))&1; b.Set(14,data?5:0); b.Settle(); b.Pulse(11);
            shifted=((shifted<<1)|data)&0xff;
            b.Check((shifted&0x80)?0x100:0); // parallel bank still zero; serial remains live
        }
        b.Pulse(12); b.Check(0x1a5);
        b.Set(10,0); b.Settle(); b.Check(0xa5); // shift reset must not clear output storage
        b.Set(13,5); b.Settle();
        for( unsigned i=0; i<8; ++i ) BOOST_CHECK_SMALL(b.session->Snapshot().latest.values[i]-3.3,.002);
        BOOST_CHECK_LT(b.session->Snapshot().latest.values[8],.3); // serial output is not tri-state
        b.Pulse(12); b.Set(13,0); b.Settle(); b.Check(0);
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog595TiedClocksCaptureThePreviousShiftWord )
{
    for( const auto& f : families ) BOOST_TEST_CONTEXT(f)
    {
        BENCH b("74xx:"+f+"595",16,8,{{10,0},{11,0},{13,0},{14,5}},shiftOutputs,{{12,"input11"}});
        b.Settle(); b.Pulse(11); b.Check(0); b.Set(10,5); b.Settle(); unsigned shifted=0;
        for( unsigned i=0; i<10; ++i )
        {
            const bool data=(i%3)!=1; b.Set(14,data?5:0); b.Settle();
            b.Pulse(11); b.Check(shifted); shifted=((shifted<<1)|data)&0xff;
        }
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog165LoadsWithoutClockAndShiftsComplementaryOutputs )
{
    for( const std::string chip : {"74HC165","74LS165"} ) BOOST_TEST_CONTEXT(chip)
    {
        BENCH b("74xx:"+chip,16,8,Inputs(parallel165,{{1,0},{2,0},{10,0},{15,0}}),{9,7});
        auto check=[&](unsigned word){b.Check(word&0x80?1:2);};
        b.SetWord(parallel165,0xa5); b.Settle(); check(0xa5);
        b.Set(6,0); b.Settle(); check(0x25); // D7 changes while PL remains LOW
        b.Set(6,5); b.Settle(); check(0xa5); b.Set(1,5); b.Settle();
        unsigned word=0xa5;
        for( unsigned i=0; i<16; ++i )
        {
            const bool data=i%3==0; b.Set(10,data?5:0); b.Settle();
            b.Pulse(2); word=((word<<1)|data)&0xff; check(word);
        }
        // CE changes while CP is HIGH, avoiding a clock-gate transition.
        b.Set(15,5); b.Settle();
        for( unsigned i=0; i<3; ++i ) { b.Pulse(2); check(word); }
        b.Set(15,0); b.Settle(); check(word);
        b.Set(10,5); b.Settle(); b.Pulse(2); check(((word<<1)|1)&0xff);
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog138ExhaustsAddressesAndAllEnableCombinations )
{
    for( const std::string chip : {"74HC138","74HCT138","74LS138"} ) BOOST_TEST_CONTEXT(chip)
    {
        BENCH b("74xx:"+chip,16,8,{{1,0},{2,0},{3,0},{4,0},{5,0},{6,0}},{15,14,13,12,11,10,9,7});
        for( unsigned enables=0; enables<8; ++enables ) for( unsigned address=0; address<8; ++address )
        {
            b.SetWord({4,5,6},enables); b.SetWord({1,2,3},address); b.Settle();
            b.Check(enables==4?(0xff^(1u<<address)):0xff);
        }
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog139HasIndependentEnablesAndAddressBanks )
{
    BENCH b("74xx:74LS139",16,8,{{1,0},{2,0},{3,0},{13,0},{14,0},{15,0}},{4,5,6,7,12,11,10,9});
    for( unsigned enables=0; enables<4; ++enables ) for( unsigned addresses=0; addresses<16; ++addresses )
    {
        b.SetWord({1,15},enables); b.SetWord({2,3,14,13},addresses); b.Settle();
        const auto low=enables&1?15:(15^(1u<<(addresses&3)));
        const auto high=enables&2?15:(15^(1u<<(addresses>>2)));
        b.Check(low|(high<<4));
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog161And163LoadCountHoldAndUseCorrectResetPriority )
{
    for( const std::string chip : {"74LS161","74LS163"} ) BOOST_TEST_CONTEXT(chip)
    {
        BENCH b("74xx:"+chip,16,8,Inputs({3,4,5,6},{{1,0},{2,0},{7,5},{9,5},{10,5}}),{14,13,12,11,15});
        b.Settle(); b.Pulse(2); b.Check(0); b.Set(1,5); b.Settle();
        // Loading takes priority over all four enable combinations.
        for( unsigned enables=0; enables<4; ++enables ) for( unsigned word=0; word<16; ++word )
        {
            b.SetWord({7,10},enables); b.Set(9,0); b.SetWord({3,4,5,6},word); b.Settle();
            b.Pulse(2); b.Check(word|((word==15 && (enables&2))?16:0));
        }
        b.Set(9,5); b.Set(7,5); b.Set(10,5); b.Settle(); unsigned count=15;
        for( unsigned i=0; i<33; ++i )
        { b.Pulse(2); count=(count+1)&15; b.Check(count|(count==15?16:0)); }
        b.Set(7,0); b.Settle(); b.Pulse(2); b.Check(count|(count==15?16:0));
        b.Set(7,5); b.Set(10,0); b.Settle(); b.Pulse(2); b.Check(count);
        b.Set(9,0); b.SetWord({3,4,5,6},10); b.Settle(); b.Pulse(2); b.Check(10);
        b.Set(1,0); b.Settle(); b.Check(chip=="74LS161"?0:10);
        b.Pulse(2); b.Check(0); // reset overrides pending parallel load and disabled CET
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalog393CountsFallingEdgesAndResetsEachBankIndependently )
{
    BENCH b("74xx:74LS393",14,7,{{1,5},{2,5},{12,5},{13,5}},{3,4,5,6,11,10,9,8});
    b.Settle(); b.Check(0); b.Set(2,0); b.Set(12,0); b.Settle(); unsigned first=0,second=0;
    for( unsigned i=0; i<20; ++i )
    {
        b.Set(1,0); b.Settle(); first=(first+1)&15; b.Check(first|(second<<4));
        b.Set(1,5); b.Settle(); b.Check(first|(second<<4));
        for( unsigned j=0; j<2; ++j )
        {
            b.Set(13,0); b.Settle(); second=(second+1)&15; b.Check(first|(second<<4));
            b.Set(13,5); b.Settle(); b.Check(first|(second<<4));
        }
    }
    b.Set(2,5); b.Settle(); b.Check(second<<4);
    b.Set(12,5); b.Settle(); b.Check(0);
}

BOOST_AUTO_TEST_CASE( RegisterCatalogCdRippleCountersContainEveryInternalStageAndWrap )
{
    struct SPEC { std::string library; unsigned width; std::vector<int> bits,pins; };
    for( const SPEC s : std::vector<SPEC>{
        {"4xxx:4020",14,{0,3,4,5,6,7,8,9,10,11,12,13},{9,7,5,4,6,13,12,14,15,1,2,3}},
        {"4xxx_IEEE:4040",12,{0,1,2,3,4,5,6,7,8,9,10,11},{9,7,6,5,3,2,4,13,12,14,15,1}}} )
        BOOST_TEST_CONTEXT(s.library)
    {
        auto u=Device(s.library); u.pins["16"]="vcc"; u.pins["8"]="0";
        u.pins["10"]="clock"; u.pins["11"]="reset";
        for( int pin : s.pins ) u.pins[std::to_string(pin)]="out"+std::to_string(pin);
        auto c=BuildCircuitSnapshot({Source("VS","vcc",5),Source("VC","clock",5),Source("VR","reset",5),u});
        const auto start=c.netlist.find("\nVca2 ")+1, end=c.netlist.find('\n',start);
        c.netlist.replace(start,end-start,"Vca2 "+c.pinNodes[1].at("1")+" 0 PULSE(5 0 10u 1n 1n 5u 10u)");
        c.config.signals.clear(); c.config.step=c.config.maxStep=100e-6; c.config.horizon=1;
        for( int pin : s.pins ) c.config.signals.push_back("v("+c.pinNodes[3].at(std::to_string(pin))+")");
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
        auto check=[&](unsigned count)
        {
            const auto sample=session.Snapshot().latest;
            for( unsigned i=0; i<s.bits.size(); ++i ) BOOST_TEST_CONTEXT("bit "<<s.bits[i]<<" count="<<count)
                BOOST_CHECK_SMALL(sample.values[i]-(count&(1u<<s.bits[i])?5:0),.02);
        };
        session.AdvanceTo(2e-6); check(0); session.SetSourceValue("Vca3",0);
        auto advance=[&](double target)
        {
            // Bound each request as in the editor; a full 14-bit period can
            // otherwise exceed the worker's stop-acknowledgement deadline.
            double confirmed=session.Snapshot().latest.time;
            while( target-confirmed>1e-3 )
            {
                session.AdvanceTo(confirmed+1e-3);
                confirmed=session.Snapshot().latest.time;
            }
            session.AdvanceTo(target);
        };
        for( unsigned bit=0; bit<s.width; ++bit )
        {
            const unsigned count=1u<<bit; advance(count*10e-6+3e-6); check(count);
        }
        advance(((1u<<s.width)-1)*10e-6+3e-6); check((1u<<s.width)-1);
        advance((1u<<s.width)*10e-6+3e-6); check(0);
    }
}

BOOST_AUTO_TEST_CASE( RegisterCatalogEveryNewPackageReleasesAllOutputsWhenUnpowered )
{
    const auto libs=NewLibraries(); BOOST_REQUIRE_EQUAL(libs.size(),35);
    for( const auto& library : libs ) BOOST_TEST_CONTEXT(library)
    {
        auto u=Device(library); const auto model=MODEL_REPOSITORY().SelectModel(u);
        int vp=0,gnd=0; std::vector<int> outputs;
        for( const auto& pin : model.pins )
        {
            if( pin.role=="power" ) vp=std::stoi(pin.defaultPin);
            if( pin.role=="reference" ) gnd=std::stoi(pin.defaultPin);
            if( pin.role=="output" ) outputs.push_back(std::stoi(pin.defaultPin));
        }
        BOOST_REQUIRE(vp>0 && gnd>0 && !outputs.empty());
        BENCH b(library,vp,gnd,{},outputs); b.Set(vp,0); b.Settle(); b.CheckZ();
    }
}
