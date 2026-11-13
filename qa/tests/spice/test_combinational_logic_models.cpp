/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Source( const std::string& ref, const std::string& net, double value )
{
    return { ref, "Simulation_SPICE:VDC", std::to_string( value ), {{"1",net},{"2","0"}} };
}

// Wiring and expected results below use physical datasheet pins and arithmetic,
// independently of the model's port order and internal Boolean implementation.
struct BENCH
{
    CIRCUIT_SNAPSHOT circuit;
    std::unique_ptr<INTERACTIVE_SIM_SESSION> session;
    std::map<int,std::string> sources;
    std::map<int,double> values;
    std::vector<int> outputs;
    bool ls;
    double time = 0;

    BENCH( const std::string& library, int vp, int ground,
           const std::map<int,double>& inputs, const std::vector<int>& watch,
           bool pullOutputs = true ) :
            outputs( watch ), ls( library.find( ":74LS" ) != std::string::npos )
    {
        CIRCUIT_COMPONENT u{ "U1", library, library.substr( library.find( ':' ) + 1 ), {} };
        for( const auto& p : MODEL_REPOSITORY().SelectModel( u ).pins ) u.pins[p.defaultPin] = "";
        u.pins[std::to_string( vp )] = "vcc";
        u.pins[std::to_string( ground )] = "0";
        std::vector<CIRCUIT_COMPONENT> parts{Source("VS","vcc",5),Source("VP","pull",3.3)};
        sources[vp] = "Vca1";
        values[vp] = 5;
        for( const auto& [pin,value] : inputs )
        {
            const auto net = "input" + std::to_string( pin );
            u.pins[std::to_string( pin )] = net;
            sources[pin] = "Vca" + std::to_string( parts.size() + 1 );
            values[pin] = value;
            parts.push_back( Source( "VI" + std::to_string( pin ), net, value ) );
        }
        for( int pin : watch ) u.pins[std::to_string( pin )] = "out" + std::to_string( pin );
        const auto device = parts.size();
        parts.push_back( u );
        for( int pin : watch )
            if( pullOutputs )
                parts.push_back( {"RP"+std::to_string(pin),"Device:R","100k",
                    {{"1","out"+std::to_string(pin)},{"2","pull"}}} );
        circuit = BuildCircuitSnapshot( parts );
        circuit.config.step = circuit.config.maxStep = 50e-9;
        circuit.config.horizon = .1;
        circuit.config.signals.clear();
        for( int pin : watch )
            circuit.config.signals.push_back( "v(" + circuit.pinNodes[device].at( std::to_string(pin) ) + ")" );
        session = std::make_unique<INTERACTIVE_SIM_SESSION>( CIRCUITALIVE_TEST_WORKER );
        session->Configure( circuit.netlist, circuit.config );
        Settle();
    }

    void Set( int pin, double value )
    {
        if( values.at( pin ) != value )
        {
            session->SetSourceValue( sources.at( pin ), value );
            values[pin] = value;
        }
    }
    void Word( const std::vector<int>& pins, unsigned value, double high = 5 )
    {
        for( unsigned i=0; i<pins.size(); ++i ) Set( pins[i], value & (1u<<i) ? high : 0 );
    }
    void Settle( double duration = .5e-6 ) { session->AdvanceTo( time += duration ); }
    void CheckPin( unsigned bit, bool high ) const
    {
        const auto sample = session->Snapshot().latest;
        BOOST_TEST_CONTEXT( "physical pin " << outputs.at( bit ) )
            BOOST_CHECK_SMALL( sample.values.at( bit ) - (high ? (ls ? 3.4 : 5) : (ls ? .2 : 0)), .025 );
    }
    void Check( unsigned word ) const
    {
        for( unsigned i=0; i<outputs.size(); ++i ) CheckPin( i, word & (1u<<i) );
    }
    void CheckZ( unsigned bit ) const
    {
        BOOST_TEST_CONTEXT( "released physical pin " << outputs.at( bit ) )
            BOOST_CHECK_SMALL( session->Snapshot().latest.values.at( bit ) - 3.3, .002 );
    }
    void CheckZ() const { for( unsigned i=0; i<outputs.size(); ++i ) CheckZ( i ); }
    void PowerOff( int vp )
    {
        for( double voltage : {0.,1.,7.} )
        {
            Set( vp, voltage ); Settle(); CheckZ();
        }
    }
};

std::map<int,double> Inputs( const std::vector<int>& pins, std::map<int,double> controls = {} )
{
    for( int pin : pins ) controls[pin] = 0;
    return controls;
}
const std::vector<int> mux8{4,3,2,1,15,14,13,12};
const std::vector<int> bank0{6,5,4,3}, bank1{10,11,12,13};
const std::vector<int> quad0{2,5,11,14}, quad1{3,6,10,13}, quadOut{4,7,9,12};
const std::vector<int> a4{10,12,13,15}, b4{9,11,14,1};
const std::vector<int> a8{2,4,6,8,11,13,15,17}, b8{3,5,7,9,12,14,16,18};
const std::vector<int> octalIn{2,3,4,5,6,7,8,9}, octalOut{18,17,16,15,14,13,12,11};

std::string ScalarPinBridges( const std::string& deck )
{
    const std::regex bridge( R"(^(A\S+)\s+\[([^\]]+)\]\s+\[([^\]]+)\]\s+(bh_(?:adc|dac)_\S+)\s*$)" );
    std::istringstream lines( deck ); std::string line, result;
    for( ; std::getline( lines, line ); )
    {
        std::smatch match;
        if( std::regex_match( line, match, bridge ) )
        {
            std::istringstream inputs( match[2] ), outputs( match[3] );
            std::string input, output; unsigned bit = 0;
            while( inputs >> input && outputs >> output )
                result += match[1].str() + "_bit" + std::to_string( bit++ ) + " [" + input
                          + "] [" + output + "] " + match[4].str() + '\n';
        }
        else
            result += line + '\n';
    }
    return result;
}
}

BOOST_AUTO_TEST_CASE( GroupedPinBridgesMatchScalarVoltagesCurrentsAndTransitionTimes )
{
    BENCH bench( "74xx:74LS541", 20, 10, Inputs( octalIn, {{1,0},{19,0}} ), octalOut );
    auto config = bench.circuit.config;
    config.signals.push_back( "i(Vca1)" );
    config.step = config.maxStep = .1e-9;
    config.historyCapacity = 1;
    config.horizon = .001;
    const auto scalar = ScalarPinBridges( bench.circuit.netlist );
    BOOST_REQUIRE( scalar != bench.circuit.netlist );
    INTERACTIVE_SIM_SESSION original( CIRCUITALIVE_TEST_WORKER ), grouped( CIRCUITALIVE_TEST_WORKER );
    original.Configure( scalar, config ); grouped.Configure( bench.circuit.netlist, config );
    double time = 0;
    for( const auto [pin, voltage] : std::vector<std::pair<int,double>>{
            {20,5},{2,5},{3,5},{4,5},{1,5},{1,0},{4,1.3},{4,0},{20,0},{20,5},{19,5},{19,0}} )
    {
        // Include independent data, enables, X and power cycling. Compare the
        // actual transition samples and supply current, not just settled bits.
        if( time > 0 ) // First interval establishes the initial operating state.
        {
            original.SetSourceValue( bench.sources.at( pin ), voltage );
            grouped.SetSourceValue( bench.sources.at( pin ), voltage );
        }
        for( int sample = 0; sample < 120; ++sample )
        {
            time += 1e-9;
            original.AdvanceTo( time ); grouped.AdvanceTo( time );
            const auto a = original.Snapshot( true ).latest, b = grouped.Snapshot( true ).latest;
            BOOST_REQUIRE_SMALL( a.time - b.time, .11e-9 );
            for( std::size_t i = 0; i < a.values.size(); ++i )
                BOOST_REQUIRE_SMALL( a.values[i] - b.values[i], 2e-5 );
        }
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogEightWayMuxChecksEveryDataPinAndComplement )
{
    for( const std::string chip : {"74LS151","74LS251"} ) BOOST_TEST_CONTEXT( chip )
    {
        BENCH b( "74xx:"+chip, 16, 8, Inputs(mux8,{{7,0},{9,0},{10,0},{11,0}}), {5,6} );
        for( unsigned data : {0u,255u,1u,2u,4u,8u,16u,32u,64u,128u,0x55u,0xaau} )
        {
            b.Word( mux8, data );
            for( unsigned address=0; address<8; ++address )
            {
                b.Word( {11,10,9}, address ); b.Settle();
                b.Check( data & (1u<<address) ? 1 : 2 );
            }
        }
        b.Set( 7, 5 ); b.Settle();
        if( chip == "74LS251" ) b.CheckZ(); else b.Check( 2 );
        b.Word( mux8, 255 ); b.Settle();
        if( chip == "74LS251" ) b.CheckZ(); else b.Check( 2 );
        b.Set( 7, 0 ); b.Settle(); b.Check( 1 );
        b.PowerOff( 16 );
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogDualMuxHasSharedSelectAndIndependentEnables )
{
    for( const std::string chip : {"74LS153","74LS253"} ) BOOST_TEST_CONTEXT( chip )
    {
        BENCH b( "74xx:"+chip,16,8,Inputs(bank0,Inputs(bank1,{{1,0},{2,0},{14,0},{15,0}})),{7,9} );
        for( unsigned pattern=0; pattern<16; ++pattern )
        {
            b.Word( bank0, pattern ); b.Word( bank1, pattern^15 );
            for( unsigned address=0; address<4; ++address )
                for( unsigned disabled=0; disabled<4; ++disabled )
            {
                b.Word( {14,2}, address ); b.Word( {1,15}, disabled ); b.Settle();
                for( unsigned bank=0; bank<2; ++bank )
                {
                    if( (disabled & (1u<<bank)) && chip == "74LS253" ) b.CheckZ( bank );
                    else b.CheckPin( bank, !(disabled & (1u<<bank)) &&
                        (((bank ? pattern^15 : pattern) >> address) & 1) );
                }
            }
        }
        b.PowerOff( 16 );
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogQuadMuxDistinguishesInversionAndTriState )
{
    for( const std::string chip : {"74LS157","74LS158","74LS257","74LS258"} ) BOOST_TEST_CONTEXT( chip )
    {
        const bool invert = chip.ends_with( "158" ) || chip.ends_with( "258" );
        const bool tri = chip.ends_with( "257" ) || chip.ends_with( "258" );
        BENCH b("74xx:"+chip,16,8,Inputs(quad0,Inputs(quad1,{{1,0},{15,0}})),quadOut);
        for( unsigned a=0; a<16; ++a ) for( unsigned other=0; other<16; ++other )
        {
            b.Word(quad0,a); b.Word(quad1,other);
            for( unsigned select=0; select<2; ++select )
            {
                b.Set(1,select ? 5 : 0); b.Settle();
                b.Check((select ? other : a) ^ (invert ? 15 : 0));
            }
        }
        b.Set(15,5); b.Settle();
        if(tri) b.CheckZ(); else b.Check(invert ? 15 : 0);
        b.Word(quad0,5); b.Word(quad1,10); b.Settle();
        if(tri) b.CheckZ(); else b.Check(invert ? 15 : 0);
        b.Set(15,0); b.Settle(); b.Check(10 ^ (invert ? 15 : 0));
        b.PowerOff(16);
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogMagnitudeChecksEveryWordPairAndAllCascadeCases )
{
    for( const std::string family : {"74LS","74HC","74HCT"} ) BOOST_TEST_CONTEXT(family)
    {
        BENCH b("74xx:"+family+"85",16,8,Inputs(a4,Inputs(b4,{{2,0},{3,5},{4,0}})),{5,6,7});
        for( unsigned a=0; a<16; ++a ) for( unsigned other=0; other<16; ++other )
        {
            b.Word(a4,a); b.Word(b4,other); b.Settle();
            b.Check(a>other ? 1 : a==other ? 2 : 4);
        }
        // Datasheet expansion truth table, including the non-one-hot inputs:
        // IEQ overrides IGT/ILT; with IEQ=0, each result is inhibited by its opposite input.
        for( unsigned cascade=0; cascade<8; ++cascade )
        {
            b.Word({2,3,4},cascade);
            for( const auto& pair : std::vector<std::pair<unsigned,unsigned>>{{0,0},{5,5},{15,15},{8,7},{7,8},{1,0},{0,1}} )
            {
                b.Word(a4,pair.first); b.Word(b4,pair.second); b.Settle();
                const unsigned equal = cascade & 2 ? 2 :
                    ((cascade & 1 ? 0 : 1) | (cascade & 4 ? 0 : 4));
                b.Check(pair.first>pair.second ? 1 : pair.first<pair.second ? 4 : equal);
            }
        }
        b.PowerOff(16);
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogEqualityChecksAllWordsAndSingleBitMismatches )
{
    for( const std::string family : {"74LS","74HC","74HCT"} ) BOOST_TEST_CONTEXT(family)
    {
        BENCH b("74xx:"+family+"688",20,10,Inputs(a8,Inputs(b8,{{1,0}})),{19});
        for( unsigned word=0; word<256; ++word )
        {
            b.Word(a8,word); b.Word(b8,word); b.Settle(); b.Check(0);
            for( unsigned bit=0; bit<8; ++bit )
            {
                b.Word(b8,word^(1u<<bit)); b.Settle(); b.Check(1);
            }
        }
        b.Word(a8,0xa5); b.Word(b8,0xa5); b.Set(1,5); b.Settle(); b.Check(1);
        b.Word(b8,0); b.Settle(); b.Check(1);
        b.Set(1,0); b.Settle(); b.Check(1);
        b.Word(b8,0xa5); b.Settle(); b.Check(0);
        b.PowerOff(20);
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalog238ChecksEveryAddressAndEnableCombination )
{
    for( const std::string family : {"74HC","74HCT"} ) BOOST_TEST_CONTEXT(family)
    {
        BENCH b("74xx:"+family+"238",16,8,{{1,0},{2,0},{3,0},{4,0},{5,0},{6,5}},
                {15,14,13,12,11,10,9,7});
        for( unsigned enables=0; enables<8; ++enables )
            for( unsigned address=0; address<8; ++address )
        {
            b.Word({4,5,6},enables); b.Word({1,2,3},address); b.Settle();
            b.Check(enables==4 ? 1u<<address : 0);
        }
        b.PowerOff(16);
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalog541ChecksAllBitsBothEnablesAndLiveData )
{
    for( const std::string family : {"74LS","74HCT","74AHC","74AHCT"} ) BOOST_TEST_CONTEXT(family)
    {
        BENCH b("74xx:"+family+"541",20,10,Inputs(octalIn,{{1,0},{19,0}}),octalOut);
        for( unsigned word=0; word<256; ++word )
        {
            b.Word(octalIn,word); b.Settle(); b.Check(word);
        }
        for( unsigned enables=1; enables<4; ++enables )
        {
            b.Word({1,19},enables); b.Settle(); b.CheckZ();
            b.Word(octalIn,0xa5); b.Settle(); b.CheckZ();
            b.Word(octalIn,0x5a); b.Settle(); b.CheckZ();
            b.Word({1,19},0); b.Settle(); b.Check(0x5a);
        }
        b.PowerOff(20);
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogDisabledControlsDominateFloatingData )
{
    BENCH mux("74xx:74LS151",16,8,{{7,5},{9,0},{10,0},{11,0}},{5,6});
    mux.Check(2);
    BENCH muxTri("74xx:74LS251",16,8,{{7,5}},{5,6}); muxTri.CheckZ();
    BENCH dual("74xx:74LS153",16,8,{{1,5},{15,5}},{7,9}); dual.Check(0);
    BENCH dualTri("74xx:74LS253",16,8,{{1,5},{15,5}},{7,9}); dualTri.CheckZ();
    BENCH equality("74xx:74HC688",20,10,{{1,5}},{19}); equality.Check(1);
    BENCH decoder("74xx:74HC238",16,8,{{4,5}},{15,14,13,12,11,10,9,7}); decoder.Check(0);
    BENCH octal("74xx:74AHC541",20,10,{{1,5}},{18,17,16,15,14,13,12,11}); octal.CheckZ();
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogFloatingSelectedDataRemainsUnknownAndUnknownEnableReleases )
{
    BENCH mux("74xx:74LS251",16,8,{{7,0},{9,0},{10,0},{11,0}},{5,6},false);
    for( double value : mux.session->Snapshot().latest.values )
    {
        BOOST_CHECK_GT(value,.8); BOOST_CHECK_LT(value,3.2);
    }
    BENCH octal("74xx:74AHC541",20,10,Inputs(octalIn,{{19,0}}),octalOut); octal.CheckZ();
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogIeeeBindingsRetainElectricalBehavior )
{
    for( const std::string chip : {"74LS257","74LS258"} ) BOOST_TEST_CONTEXT(chip)
    {
        BENCH b("74xx_IEEE:"+chip,16,8,Inputs(quad0,Inputs(quad1,{{1,0},{15,0}})),quadOut);
        b.Word(quad0,5); b.Word(quad1,10); b.Settle(); b.Check(chip=="74LS257" ? 5 : 10);
        b.Set(1,5); b.Settle(); b.Check(chip=="74LS257" ? 10 : 5);
        b.Set(15,5); b.Settle(); b.CheckZ();
    }
    BENCH octal("74xx_IEEE:74LS541",20,10,Inputs(octalIn,{{1,0},{19,0}}),octalOut);
    octal.Word(octalIn,0x81); octal.Settle(); octal.Check(0x81);
    BENCH equality("74xx_IEEE:74LS688",20,10,Inputs(a8,Inputs(b8,{{1,0}})),{19});
    equality.Check(0); equality.Set(18,5); equality.Settle(); equality.Check(1);
    BENCH decoder("74xx_IEEE:74HC238",16,8,{{1,5},{2,0},{3,5},{4,0},{5,0},{6,5}},
                  {15,14,13,12,11,10,9,7}); decoder.Check(1u<<5);
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogTtlThresholdsStayAbsoluteAcrossSupplyRange )
{
    for( const std::string family : {"74LS","74HCT","74AHCT"} ) BOOST_TEST_CONTEXT(family)
    {
        const bool ls = family == "74LS";
        BENCH b("74xx:"+family+"541",20,10,Inputs(octalIn,{{1,0},{19,0}}),octalOut);
        const double supply = ls ? 5.2 : 5.4;
        b.Set(20,supply); b.Word(octalIn,255,2.05); b.Settle();
        for( double value : b.session->Snapshot().latest.values )
            BOOST_CHECK_SMALL(value-(supply-(ls ? 1.6 : 0)),.025);
        b.Set(20,ls ? 4.8 : 4.6); b.Word(octalIn,255,.79); b.Settle(); b.Check(0);
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogBuffersHaveFiniteDataAndEnableDelays )
{
    for( const std::string family : {"74LS","74HCT","74AHC","74AHCT"} ) BOOST_TEST_CONTEXT(family)
    {
        BENCH b("74xx:"+family+"541",20,10,Inputs(octalIn,{{1,0},{19,0}}),octalOut);
        b.Check(0);
        b.Word(octalIn,255); b.Settle(1e-9); b.Check(0);
        b.Settle(); b.Check(255);
        b.Set(1,5); b.Settle(1e-9); b.Check(255);
        b.Settle(); b.CheckZ();
    }
}

BOOST_AUTO_TEST_CASE( CombinationalCatalogSupplyAndInputsUseTheLocalGround )
{
    CIRCUIT_COMPONENT u{"U1","74xx:74AHCT541","74AHCT541",{}};
    u.pins={{"20","vcc"},{"10","reference"},{"1","reference"},{"19","reference"}};
    for(unsigned i=0;i<8;++i)
    {
        u.pins[std::to_string(octalIn[i])] = i%2 ? "reference" : "vcc";
        u.pins[std::to_string(octalOut[i])] = "out"+std::to_string(i);
    }
    auto c=BuildCircuitSnapshot({Source("VG","reference",3.3),Source("VS","vcc",8.3),u});
    c.config.step=c.config.maxStep=20e-9; c.config.horizon=.001; c.config.signals.clear();
    for(int pin : octalOut) c.config.signals.push_back("v("+c.pinNodes[2].at(std::to_string(pin))+")");
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER); session.Configure(c.netlist,c.config);
    session.AdvanceTo(1e-6);
    for(unsigned i=0;i<8;++i)
        BOOST_CHECK_SMALL(session.Snapshot().latest.values.at(i)-(i%2 ? 3.3 : 8.3),.001);
}
