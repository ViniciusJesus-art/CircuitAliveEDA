/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <fstream>
#include <sim/platform_runtime.h>
using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Supply( const std::string& ref, const std::string& net, double value )
{
    return { ref, "Simulation_SPICE:VDC", std::to_string( value ), { { "1", net }, { "2", "0" } } };
}
CIRCUIT_COMPONENT Part( const std::string& library )
{
    CIRCUIT_COMPONENT part{ "U1", library, "1", {} };
    for( const auto& pin : MODEL_REPOSITORY().SelectModel( part ).pins ) part.pins[pin.defaultPin] = "";
    return part;
}
void Insert( CIRCUIT_SNAPSHOT& circuit, const std::string& line )
{
    circuit.netlist.insert( circuit.netlist.rfind( ".end" ), line + '\n' );
}
struct GATE
{
    std::string library, kind;
    std::vector<std::vector<int>> inputs;
    std::vector<int> outputs;
};
const std::vector<std::vector<int>> lsQuad{ { 1,2 },{ 4,5 },{ 9,10 },{ 12,13 } };
const std::vector<std::vector<int>> cdQuad{ { 1,2 },{ 5,6 },{ 8,9 },{ 12,13 } };
const std::vector<std::vector<int>> norQuad{ { 2,3 },{ 5,6 },{ 8,9 },{ 11,12 } };
const std::vector<std::vector<int>> hex{ { 1 },{ 3 },{ 5 },{ 9 },{ 11 },{ 13 } };
const std::vector<std::vector<int>> lsTriple{ { 1,2,13 },{ 3,4,5 },{ 9,10,11 } };
const std::vector<std::vector<int>> cdTriple{ { 1,2,8 },{ 3,4,5 },{ 11,12,13 } };
const std::vector<std::vector<int>> lsFour{ { 1,2,4,5 },{ 9,10,12,13 } };
const std::vector<std::vector<int>> cdFour{ { 2,3,4,5 },{ 9,10,11,12 } };
std::vector<GATE> Gates()
{
    return {
        {"74xx:74LS00","nand",lsQuad,{3,6,8,11}},
        {"74xx:74LS02","nor",norQuad,{1,4,10,13}},
        {"74xx:74LS04","inverter",hex,{2,4,6,8,10,12}},
        {"74xx:74LS10","nand",lsTriple,{12,6,8}},
        {"74xx:74LS11","and",lsTriple,{12,6,8}},
        {"74xx:74LS20","nand",lsFour,{6,8}},
        {"74xx:74LS21","and",lsFour,{6,8}},
        {"74xx:74LS27","nor",lsTriple,{12,6,8}},
        {"74xx:74LS30","nand",{{1,2,3,4,5,6,11,12}},{8}},
        {"74xx:74LS86","xor",lsQuad,{3,6,8,11}},
        {"74xx:74HCT00","nand",lsQuad,{3,6,8,11}},
        {"74xx:74HCT02","nor",norQuad,{1,4,10,13}},
        {"74xx:74HCT04","inverter",hex,{2,4,6,8,10,12}},
        {"4xxx:4001","nor",cdQuad,{3,4,10,11}},
        {"4xxx:4002","nor",cdFour,{1,13}},
        {"4xxx:4011","nand",cdQuad,{3,4,10,11}},
        {"4xxx:4012","nand",cdFour,{1,13}},
        {"4xxx:4023","nand",cdTriple,{9,6,10}},
        {"4xxx:4025","nor",cdTriple,{9,6,10}},
        {"4xxx:4069","inverter",hex,{2,4,6,8,10,12}},
        {"4xxx:4070","xor",cdQuad,{3,4,10,11}},
        {"4xxx:4071","or",cdQuad,{3,4,10,11}},
        {"4xxx:4072","or",cdFour,{1,13}},
        {"4xxx:4073","and",cdTriple,{9,6,10}},
        {"4xxx:4075","or",cdTriple,{9,6,10}},
        {"4xxx:4077","xnor",cdQuad,{3,4,10,11}},
        {"4xxx:4081","and",cdQuad,{3,4,10,11}}
    };
}
bool Truth( const std::string& kind, unsigned bits, unsigned width )
{
    if( kind == "inverter" ) return !( bits & 1 );
    if( kind == "and" || kind == "nand" ) return ( bits == ( 1u << width ) - 1 ) != ( kind == "nand" );
    if( kind == "or" || kind == "nor" ) return ( bits != 0 ) != ( kind == "nor" );
    unsigned ones = 0;
    for( unsigned i = 0; i < width; ++i ) ones += ( bits >> i ) & 1;
    return ( ones % 2 != 0 ) != ( kind == "xnor" );
}
}

BOOST_AUTO_TEST_CASE( BundledCatalogResolvesAllDeclaredAssociationsWithoutAmbiguity )
{
    struct FIRMWARE_FILE
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ( "ca-catalog-" + std::to_string( ProcessId() ) + ".bin" );
        FIRMWARE_FILE() { std::ofstream( path, std::ios::binary ).put( 0 ); }
        ~FIRMWARE_FILE() { std::error_code error; std::filesystem::remove( path, error ); }
    } firmware;
    MODEL_REPOSITORY repository;
    BOOST_REQUIRE( !repository.List().empty() );
    for( const auto& model : repository.List() ) BOOST_TEST_CONTEXT( model.id )
    {
        CIRCUIT_COMPONENT c{ "U1", "Custom:Explicit", "1", {} };
        for( const auto& pin : model.pins ) c.pins[pin.defaultPin] = "";
        c.modelFile = "builtin:" + model.id;
        if( model.runtime )
        {
            BOOST_CHECK_THROW( repository.Resolve( c ), std::runtime_error );
            c.resources[model.runtime->firmwareResource] = { PathUtf8( firmware.path ), "bin", 0 };
        }
        BOOST_CHECK_EQUAL( repository.Resolve( c ).model.id, model.id );
        c.modelFile.clear();
        for( const auto& identity : model.libraryIds )
        {
            c.libraryId = identity;
            BOOST_CHECK_EQUAL( repository.Resolve( c ).model.id, model.id );
        }
    }
}

BOOST_AUTO_TEST_CASE( CatalogLogicGatesExhaustPhysicalTruthTablesOnEveryUnit )
{
    for( const auto& gate : Gates() ) BOOST_TEST_CONTEXT( gate.library )
    {
        auto u = Part( gate.library );
        u.pins["14"] = "vcc"; u.pins["7"] = "0";
        const unsigned width = gate.inputs.front().size(), mask = ( 1u << width ) - 1;
        std::vector<CIRCUIT_COMPONENT> parts{ Supply( "VS", "vcc", 5 ) };
        // Separate complementary stimulus banks expose unintended coupling between units.
        for( unsigned bank = 0; bank < 2; ++bank )
            for( unsigned i = 0; i < width; ++i )
                parts.push_back( Supply( "VD" + std::to_string( bank*width+i ),
                    "d" + std::to_string( bank*width+i ), bank ? 5 : 0 ) );
        for( std::size_t unit = 0; unit < gate.inputs.size(); ++unit )
            for( unsigned i = 0; i < width; ++i )
                u.pins[std::to_string( gate.inputs[unit][i] )] = "d" + std::to_string( ( unit % 2 ) * width + i );
        const auto device = parts.size(); parts.push_back( u );
        auto c = BuildCircuitSnapshot( parts ); c.config.signals.clear();
        for( const auto pin : gate.outputs ) c.config.signals.push_back( "v(" + c.pinNodes[device].at( std::to_string( pin ) ) + ")" );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER ); session.Configure( c.netlist, c.config );
        double time = 0;
        for( unsigned pattern = 0; pattern <= mask; ++pattern )
        {
            const unsigned bits = pattern ^ ( pattern >> 1 ); // Gray order reduces simultaneous discontinuities.
            if( pattern ) for( unsigned bank = 0; bank < 2; ++bank ) for( unsigned i = 0; i < width; ++i )
                session.SetSourceValue( "Vca" + std::to_string( 2 + bank*width+i ),
                    ( ( ( bits >> i ) & 1 ) != bank ) ? 5 : 0 );
            session.AdvanceTo( time += 4e-6 );
            const auto sample = session.Snapshot().latest;
            for( std::size_t unit = 0; unit < gate.outputs.size(); ++unit )
            {
                const bool high = Truth( gate.kind, unit % 2 ? bits ^ mask : bits, width );
                const bool ttl = gate.library.starts_with( "74xx:74LS" );
                BOOST_CHECK_SMALL( sample.values.at( unit ) - ( high ? ( ttl ? 3.4 : 5 ) : ( ttl ? .2 : 0 ) ), .02 );
            }
        }
    }
}

BOOST_AUTO_TEST_CASE( CatalogTriStateBuffersRespectEachEnableBankAndSupply )
{
    for( const std::string chip : { "74LS125", "74LS126", "74HC240", "74HC244", "74HCT240", "74HCT244", "74LS240", "74LS244" } ) BOOST_TEST_CONTEXT( chip )
    {
        const bool octal = chip.ends_with( "240" ) || chip.ends_with( "244" ), inverted = chip.ends_with( "240" );
        const bool activeHigh = chip == "74LS126", ttl = chip.starts_with( "74LS" );
        const std::vector<int> data = octal ? std::vector<int>{2,4,6,8,11,13,15,17} : std::vector<int>{2,5,9,12};
        const std::vector<int> out = octal ? std::vector<int>{18,16,14,12,9,7,5,3} : std::vector<int>{3,6,8,11};
        const std::vector<int> enables = octal ? std::vector<int>{1,19} : std::vector<int>{1,4,10,13};
        auto u = Part( "74xx:" + chip ); u.pins[octal ? "20" : "14"] = "vcc"; u.pins[octal ? "10" : "7"] = "0";
        for( std::size_t i = 0; i < data.size(); ++i ) u.pins[std::to_string( data[i] )] = i % 2 ? "low" : "high";
        std::vector<CIRCUIT_COMPONENT> parts{ Supply( "VS", "vcc", 5 ), Supply( "VH", "high", 5 ),
            Supply( "VL", "low", 0 ), Supply( "VP", "pull", 3.3 ) };
        for( std::size_t i = 0; i < enables.size(); ++i )
        {
            u.pins[std::to_string( enables[i] )] = "en" + std::to_string( i );
            parts.push_back( Supply( "VE" + std::to_string( i ), "en" + std::to_string( i ), activeHigh ? 5 : 0 ) );
        }
        const auto device = parts.size(); parts.push_back( u );
        for( std::size_t i = 0; i < out.size(); ++i )
        {
            parts[device].pins[std::to_string( out[i] )] = "out" + std::to_string( i );
            parts.push_back( { "RP" + std::to_string( i ), "Device:R", "100k", {{"1","out"+std::to_string(i)},{"2","pull"}} } );
        }
        auto c = BuildCircuitSnapshot( parts ); c.config.signals.clear();
        for( auto pin : out ) c.config.signals.push_back( "v(" + c.pinNodes[device].at( std::to_string( pin ) ) + ")" );
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );session.Configure( c.netlist,c.config );
        session.AdvanceTo( 2e-6 );
        for( std::size_t i = 0; i < out.size(); ++i )
        {
            const bool high = ( i % 2 == 0 ) != inverted;
            BOOST_CHECK_SMALL( session.Snapshot().latest.values.at(i) - ( high ? ( ttl ? 3.4 : 5 ) : ( ttl ? .2 : 0 ) ), .01 );
        }
        // Disable the first bank only; other banks must continue to drive their outputs.
        session.SetSourceValue( "Vca5", activeHigh ? 0 : 5 );session.AdvanceTo( 4e-6 );
        for( std::size_t i = 0; i < ( octal ? 4u : 1u ); ++i ) BOOST_CHECK_SMALL( session.Snapshot().latest.values.at(i)-3.3,.01 );
        if( octal ) for( std::size_t i = 4; i < out.size(); ++i )
        {
            const bool high = ( i % 2 == 0 ) != inverted;
            BOOST_CHECK_SMALL( session.Snapshot().latest.values.at(i) - ( high ? ( ttl ? 3.4 : 5 ) : ( ttl ? .2 : 0 ) ), .01 );
        }
        session.SetSourceValue( "Vca1", 0 );session.AdvanceTo( 6e-6 );
        for( double value : session.Snapshot().latest.values ) BOOST_CHECK_SMALL( value-3.3,.01 );
    }
}

BOOST_AUTO_TEST_CASE( CatalogFlipFlopsCaptureRetainAndUseCorrectAsyncPolarity )
{
    for( const std::string library : { "74xx:74LS74", "74xx:74HCT74", "4xxx:4013" } ) BOOST_TEST_CONTEXT( library )
    {
        const bool cd = library == "4xxx:4013", ttl = library == "74xx:74LS74";
        auto u = Part( library ); u.pins["14"]="vcc";u.pins["7"]="0";
        const std::vector<int> data=cd?std::vector<int>{5,9}:std::vector<int>{2,12};
        const std::vector<int> clocks{3,11};
        const std::vector<int> sets=cd?std::vector<int>{6,8}:std::vector<int>{4,10};
        const std::vector<int> resets=cd?std::vector<int>{4,10}:std::vector<int>{1,13};
        const std::vector<int> q=cd?std::vector<int>{1,13}:std::vector<int>{5,9};
        const std::vector<int> nq=cd?std::vector<int>{2,12}:std::vector<int>{6,8};
        for(int p:data)u.pins[std::to_string(p)]="data";
        for(int p:clocks)u.pins[std::to_string(p)]="clock";
        for(int p:sets)u.pins[std::to_string(p)]="set";
        for(int p:resets)u.pins[std::to_string(p)]="reset";
        auto c=BuildCircuitSnapshot({Supply("VS","vcc",5),Supply("VD","data",5),Supply("VC","clock",0),
            Supply("VP","set",cd?0:5),Supply("VR","reset",cd?5:0),u});
        c.config.signals.clear();
        for(unsigned i=0;i<2;++i){c.config.signals.push_back("v("+c.pinNodes[5].at(std::to_string(q[i]))+")");c.config.signals.push_back("v("+c.pinNodes[5].at(std::to_string(nq[i]))+")");}
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);
        auto check=[&](bool high){const auto sample=session.Snapshot().latest;for(unsigned i=0;i<4;++i){const bool level=high!=(i%2!=0);BOOST_CHECK_SMALL(sample.values.at(i)-(level?(ttl?3.4:5):(ttl?.2:0)),.02);}};
        session.AdvanceTo(2e-6);check(false);
        session.SetSourceValue("Vca5",cd?0:5);session.AdvanceTo(4e-6);check(false);
        session.SetSourceValue("Vca3",5);session.AdvanceTo(6e-6);check(true);
        session.SetSourceValue("Vca2",0);session.AdvanceTo(8e-6);check(true);
        session.SetSourceValue("Vca3",0);session.AdvanceTo(10e-6);
        session.SetSourceValue("Vca3",5);session.AdvanceTo(12e-6);check(false);
        session.SetSourceValue("Vca4",cd?5:0);session.AdvanceTo(14e-6);check(true);
    }
}

BOOST_AUTO_TEST_CASE( CatalogLogicUsesNominalTtlThresholdsAndPropagationDelay )
{
    struct CHIP { const char* library; double delay, transition, high, low, other; };
    for( const auto spec : std::vector<CHIP>{
        { "74xx:74LS00", 10e-9, 2e-9, 3.4, .2, 5 },
        { "74xx:74HCT00", 15e-9, 3e-9, 5, 0, 5 },
        { "4xxx:4001", 60e-9, 20e-9, 5, 0, 0 } } ) BOOST_TEST_CONTEXT( spec.library )
    {
        auto u = Part( spec.library );
        u.pins["14"] = "vcc"; u.pins["7"] = "0";
        u.pins["1"] = "a"; u.pins["2"] = "b";
        auto c = BuildCircuitSnapshot( { Supply( "VS", "vcc", 5 ), Supply( "VA", "a", 0 ),
                                       Supply( "VB", "b", spec.other ), u } );
        c.config.signals = { "v(" + c.pinNodes[3].at( "3" ) + ")" };
        c.config.step = c.config.maxStep = 1e-9;
        c.config.horizon = 2e-6;
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( c.netlist, c.config ); session.AdvanceTo( 500e-9 );
        BOOST_CHECK_SMALL( session.Snapshot().latest.values.at( 0 ) - spec.high, .02 );
        session.SetSourceValue( "Vca2", 5 ); session.AdvanceTo( 502e-9 );
        BOOST_CHECK_SMALL( session.Snapshot().latest.values.at( 0 ) - spec.high, .02 );
        session.AdvanceTo( 500e-9 + spec.delay + spec.transition + 3e-9 );
        BOOST_CHECK_SMALL( session.Snapshot().latest.values.at( 0 ) - spec.low, .02 );
        if( std::string( spec.library ).starts_with( "74xx:" ) )
        {
            // At nominal 5V these families recognize TTL levels, rather than 0.7*VCC.
            session.SetSourceValue( "Vca2", .7 ); session.AdvanceTo( 800e-9 );
            BOOST_CHECK_SMALL( session.Snapshot().latest.values.at( 0 ) - spec.high, .02 );
            session.SetSourceValue( "Vca2", 2.1 ); session.AdvanceTo( 1e-6 );
            BOOST_CHECK_SMALL( session.Snapshot().latest.values.at( 0 ) - spec.low, .02 );
        }
    }
}

BOOST_AUTO_TEST_CASE( CatalogNAndPChannelBodyDiodesAndLogicLevelGateDrive )
{
    for( const std::string chip : { "2N7002", "BSS138", "BSS84", "IRF3205", "IRLZ44N", "IRF9540N" } ) BOOST_TEST_CONTEXT( chip )
    {
        const bool pChannel = chip == "BSS84" || chip == "IRF9540N";
        auto q = Part( "Transistor_FET:" + chip );
        const auto model = MODEL_REPOSITORY().SelectModel( q );
        std::string drain;
        for( const auto& p : model.pins )
        {
            q.pins[p.defaultPin] = p.id == "d" ? "drain" : "0";
            if( p.id == "d" ) drain = p.defaultPin;
        }
        auto c = BuildCircuitSnapshot( { q } );
        const auto node = c.pinNodes[0].at( drain );
        Insert( c, "Ibody 0 " + node + ( pChannel ? " .05" : " -.05" ) );
        c.config.signals = { "v(" + node + ")" };
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( c.netlist, c.config ); session.AdvanceTo( .001 );
        const double forward = session.Snapshot().latest.values.at( 0 ) * ( pChannel ? 1 : -1 );
        BOOST_CHECK_GT( forward, .3 ); BOOST_CHECK_LT( forward, 1.2 );
    }

    auto q = Part( "Transistor_FET:IRLZ44N" );
    q.pins = { { "1", "gate" }, { "2", "drain" }, { "3", "0" } };
    auto c = BuildCircuitSnapshot( { Supply( "VG", "gate", 5 ), q } );
    const auto node = c.pinNodes[1].at( "2" );
    Insert( c, "Iload 0 " + node + " 16" );
    c.config.signals = { "v(" + node + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config ); session.AdvanceTo( .001 );
    BOOST_CHECK_LT( session.Snapshot().latest.values.at( 0 ) / 16, .025 );
    session.SetSourceValue( "Vca1", 4 ); session.AdvanceTo( .002 );
    BOOST_CHECK_LT( session.Snapshot().latest.values.at( 0 ) / 16, .035 );
}

BOOST_AUTO_TEST_CASE( CatalogRectifiersHavePlausibleForwardDropAndBlockInReverse )
{
    std::vector<std::string> chips;
    for(unsigned i=0;i<9;++i)chips.push_back("1N540"+std::to_string(i));
    for(const auto* n:{"1N5817","1N5818","1N5820","1N5821","1N5822"})chips.push_back(n);
    for(const auto& chip:chips)BOOST_TEST_CONTEXT(chip)
    {
        auto d=Part("Diode:"+chip);d.pins={{"1","0"},{"2","anode"}};
        auto c=BuildCircuitSnapshot({d});
        const bool schottky=chip.starts_with("1N58");const double current=(chip=="1N5817"||chip=="1N5818")?1:3;
        const std::string node=c.pinNodes[0].at("2");
        Insert(c,"Itest 0 "+node+" DC "+std::to_string(current));c.config.signals={"v("+node+")"};
        INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.001);
        const double forward=session.Snapshot().latest.values.at(0);
        BOOST_CHECK_GT(forward,schottky?.25:.75);BOOST_CHECK_LT(forward,schottky?.55:1.2);
        // A separate reverse-bias bench checks current, not just an unloaded voltage.
        auto reverse=BuildCircuitSnapshot({Supply("VS","reverse",-10),{"RL","Device:R","1k",{{"1","reverse"},{"2","a"}}},
            {"D1","Diode:"+chip,chip,{{"1","0"},{"2","a"}}}});
        reverse.config.signals={"v("+reverse.pinNodes[2].at("2")+")"};
        INTERACTIVE_SIM_SESSION off(CIRCUITALIVE_TEST_WORKER);off.Configure(reverse.netlist,reverse.config);off.AdvanceTo(.001);
        BOOST_CHECK_LT(std::abs(off.Snapshot().latest.values.at(0)+10)/1000,schottky?1e-4:1e-6);
    }
}

BOOST_AUTO_TEST_CASE( CatalogMosfetsSwitchForTheirPolarityAndRespectOnResistanceBands )
{
    struct MOS{const char* name;bool p;double current,maxResistance;};
    for(const MOS spec:std::vector<MOS>{{"2N7002",false,.075,5},{"BSS138",false,.11,3.5},{"BSS84",true,.13,10},
        {"IRF3205",false,16,.008},{"IRLZ44N",false,16,.022},{"IRF9540N",true,11,.117}})BOOST_TEST_CONTEXT(spec.name)
    {
        auto q=Part(std::string("Transistor_FET:")+spec.name);auto model=MODEL_REPOSITORY().SelectModel(q);
        std::map<std::string,std::string> pads;
        for(const auto& p:model.pins)pads[p.id]=p.defaultPin;
        q.pins[pads["s"]]="0";q.pins[pads["g"]]="gate";q.pins[pads["d"]]="drain";
        auto c=BuildCircuitSnapshot({Supply("VG","gate",spec.p?-10:10),q});
        const auto node=c.pinNodes[1].at(pads["d"]);
        Insert(c,"Itest 0 "+node+" DC "+std::to_string(spec.p?-spec.current:spec.current));c.config.signals={"v("+node+")"};
        INTERACTIVE_SIM_SESSION on(CIRCUITALIVE_TEST_WORKER);on.Configure(c.netlist,c.config);on.AdvanceTo(.001);
        const double resistance=std::abs(on.Snapshot().latest.values.at(0))/spec.current;
        BOOST_CHECK_GT(resistance,spec.maxResistance*.2);BOOST_CHECK_LT(resistance,spec.maxResistance);
        // Under a resistive load, removing gate drive must stop conduction for N and P devices.
        auto loaded=BuildCircuitSnapshot({Supply("VS","supply",spec.p?-12:12),Supply("VG","gate",spec.p?-10:10),
            {"RL","Device:R","1k",{{"1","supply"},{"2","drain"}}},q});
        loaded.config.signals={"v("+loaded.pinNodes[3].at(pads["d"])+")"};
        INTERACTIVE_SIM_SESSION switched(CIRCUITALIVE_TEST_WORKER);switched.Configure(loaded.netlist,loaded.config);switched.AdvanceTo(.001);
        BOOST_CHECK_LT(std::abs(switched.Snapshot().latest.values.at(0)),.2);
        switched.SetSourceValue("Vca2",0);switched.AdvanceTo(.002);
        BOOST_CHECK_SMALL(switched.Snapshot().latest.values.at(0)-(spec.p?-12:12),.01);
    }
}
