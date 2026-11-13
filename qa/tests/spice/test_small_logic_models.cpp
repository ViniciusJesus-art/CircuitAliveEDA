/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/simulation_model.h>
#include <circuitalive/visual_signals.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>

using namespace CIRCUITALIVE;

namespace
{
struct CHANNEL
{
    std::vector<int> inputs;
    int output;
    int enable = 0;
    bool enableHigh = false;
};

struct SMALL_GATE
{
    std::string name, function;
    int vcc, ground;
    double supply, minimum, maximum;
    bool schmitt = false, drain = false;
    std::vector<CHANNEL> channels;
    std::string library = "74xGxx";
};

std::vector<SMALL_GATE> SmallGates()
{
    struct FAMILY { std::string name; double supply, minimum, maximum; };
    const std::vector<FAMILY> families{
        { "AHC",5,2,5.5 }, { "AHCT",5,4.5,5.5 }, { "AUC",1.8,.8,2.7 },
        { "AUP",3.3,.8,3.6 }, { "LVC",3.3,1.65,5.5 }
    };
    const std::map<std::string,std::string> truth{
        {"00","nand"},{"02","nor"},{"04","not"},{"06","not"},{"07","buf"},
        {"08","and"},{"10","nand"},{"11","and"},{"125","buf"},{"126","buf"},
        {"14","not"},{"17","buf"},{"240","not"},{"241","buf"},{"27","nor"},
        {"32","or"},{"34","buf"},{"38","nand"},{"86","xor"}
    };
    const std::map<std::string,std::vector<std::vector<std::string>>> functions{
        {"AHC",{{"00","02","04","08","125","126","14","32","86"},{"00"}}},
        {"AHCT",{{"00","02","04","08","125","126","14","32","86"},{"00"}}},
        {"AUC",{{"00","02","04","06","07","08","125","126","14","17","240","32","86"},
                {"00","02","04","06","07","08","125","126","240","241","32","34","86"}}},
        {"AUP",{{"00","02","04","06","07","08","125","126","14","17","240","32","34"}}},
        {"LVC",{{"00","02","04","06","07","08","10","11","125","126","14","17","240","27","32","34","38","86"},
                {"00","02","04","06","07","08","125","126","14","17","240","241","32","34","38","86"},
                {"04","06","07","14","17","34"}}}
    };
    std::vector<SMALL_GATE> result;
    for( const auto& family : families )
    {
        const auto& banks = functions.at( family.name );
        for( std::size_t bank = 0; bank < banks.size(); ++bank )
            for( const auto& code : banks[bank] )
            {
                SMALL_GATE gate;
                gate.name = "74" + family.name + std::to_string( bank+1 ) + "G" + code;
                gate.function = truth.at( code );
                gate.supply = family.supply; gate.minimum = family.minimum; gate.maximum = family.maximum;
                gate.schmitt = code == "14" || code == "17";
                gate.drain = code == "06" || code == "07" || code == "38";
                const bool tri = code == "125" || code == "126" || code == "240" || code == "241";
                const bool unary = gate.function == "not" || gate.function == "buf";
                // Datasheet package pinouts, including the reversed second/third
                // unary channels and independent OE pins of the dual drivers.
                if( bank == 0 )
                {
                    gate.vcc = 5; gate.ground = 3;
                    if( tri ) gate.channels = { {{2},4,1,code=="126"} };
                    else if( unary ) gate.channels = { {{2},4} };
                    else if( code == "10" || code == "11" || code == "27" )
                    {
                        gate.ground = 2;
                        gate.channels = { {{1,3,6},4} };
                    }
                    else gate.channels = { {{1,2},4} };
                }
                else if( bank == 1 )
                {
                    if( tri )
                    {
                        gate.vcc = 8; gate.ground = 4;
                        gate.channels = { {{2},6,1,code=="126"}, {{5},3,7,code=="126" || code=="241"} };
                    }
                    else if( unary )
                    {
                        gate.vcc = 5; gate.ground = 2;
                        gate.channels = { {{1},6}, {{3},4} };
                    }
                    else
                    {
                        gate.vcc = 8; gate.ground = 4;
                        gate.channels = { {{1,2},7}, {{5,6},3} };
                    }
                }
                else
                {
                    gate.vcc = 8; gate.ground = 4;
                    gate.channels = { {{1},7}, {{6},2}, {{3},5} };
                }
                result.push_back( std::move( gate ) );
            }
    }
    return result;
}

bool Truth( const std::string& function, const std::vector<bool>& inputs )
{
    if( function == "not" ) return !inputs.front();
    if( function == "buf" ) return inputs.front();
    const auto ones = std::count( inputs.begin(), inputs.end(), true );
    if( function == "and" || function == "nand" )
        return ( ones == static_cast<int>( inputs.size() ) ) != ( function == "nand" );
    if( function == "or" || function == "nor" ) return ( ones != 0 ) != ( function == "nor" );
    return ones % 2 != 0;
}

struct BENCH
{
    SMALL_GATE gate;
    CIRCUIT_SNAPSHOT circuit;
    std::unique_ptr<INTERACTIVE_SIM_SESSION> session;
    std::map<int,std::string> sources;
    std::map<int,double> values;
    std::vector<int> stimulus;
    double time = 0;

    explicit BENCH( const SMALL_GATE& spec, const std::string& load = "100k" ) : gate( spec )
    {
        auto source = []( const std::string& reference, const std::string& net, double value )
        { return CIRCUIT_COMPONENT{reference,"Simulation_SPICE:VDC",std::to_string(value),{{"1",net},{"2","0"}}}; };
        CIRCUIT_COMPONENT u{ "U1", gate.library+":"+gate.name, gate.name, {} };
        for( const auto& pin : MODEL_REPOSITORY().SelectModel( u ).pins ) u.pins[pin.defaultPin] = "";
        u.pins[std::to_string(gate.vcc)] = "vcc";
        u.pins[std::to_string(gate.ground)] = "0";
        std::vector<CIRCUIT_COMPONENT> parts{source("VS","vcc",gate.supply),source("VP","pull",gate.supply*.8)};
        sources[gate.vcc] = "Vca1"; values[gate.vcc] = gate.supply;
        std::set<int> inputs;
        for( const auto& channel : gate.channels )
        {
            inputs.insert( channel.inputs.begin(), channel.inputs.end() );
            if( channel.enable ) inputs.insert( channel.enable );
        }
        for( int pin : inputs )
        {
            const auto net = "input"+std::to_string(pin);
            u.pins[std::to_string(pin)] = net;
            sources[pin] = "Vca"+std::to_string(parts.size()+1); values[pin] = 0;
            parts.push_back( source("VI"+std::to_string(pin),net,0) );
            stimulus.push_back( pin );
        }
        for( const auto& channel : gate.channels ) u.pins[std::to_string(channel.output)] = "out"+std::to_string(channel.output);
        parts.push_back( u );
        for( const auto& channel : gate.channels )
            parts.push_back( {"RP"+std::to_string(channel.output),"Device:R",load,
                             {{"1",u.pins.at(std::to_string(channel.output))},{"2","pull"}}} );
        circuit = BuildCircuitSnapshot( parts, {}, true );
        circuit.config.step = circuit.config.maxStep = 25e-9;
        circuit.config.horizon = .01;
        session = std::make_unique<INTERACTIVE_SIM_SESSION>( CIRCUITALIVE_TEST_WORKER );
        session->Configure( circuit.netlist, circuit.config );
        Settle();
    }

    void Set( int pin, double value )
    {
        if( values.at(pin) != value )
        {
            session->SetSourceValue( sources.at(pin), value );
            values[pin] = value;
        }
    }
    void Settle() { session->AdvanceTo( time += .5e-6 ); }
    const LOGIC_PIN& Pin( int physical ) const
    {
        const auto it = std::find_if( circuit.logicPins.begin(), circuit.logicPins.end(),
            [&]( const auto& pin ) { return pin.reference=="U1" && pin.pin==std::to_string(physical); } );
        if( it == circuit.logicPins.end() ) throw std::runtime_error("Missing small-logic pin");
        return *it;
    }
    void Check( const CHANNEL& channel, bool high, bool released = false ) const
    {
        const auto sample = session->Snapshot().latest;
        const auto& pin = Pin( channel.output );
        BOOST_TEST_CONTEXT( gate.name << " physical output " << channel.output )
        {
            const auto drive = released ? LOGIC_LEVEL::HIGH_Z : high ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::LOW;
            BOOST_CHECK( PinDriveLevel( pin, sample ) == drive );
            const double expected = released ? gate.supply*.8 : high ? gate.supply : 0;
            BOOST_CHECK_SMALL( sample.values.at(pin.voltage)-expected, .015 );
            if( values.at(gate.vcc)==gate.supply )
                BOOST_CHECK( PinLogicLevel( pin, sample ) == (released || high ? LOGIC_LEVEL::HIGH : LOGIC_LEVEL::LOW) );
        }
    }
};
}

BOOST_AUTO_TEST_CASE( SmallLogicCatalogAllPhysicalTruthTablesAndIndependentEnables )
{
    const auto gates = SmallGates();
    BOOST_REQUIRE_EQUAL( gates.size(), 99u );
    for( const auto& gate : gates ) BOOST_TEST_CONTEXT( gate.name )
    {
        BENCH b( gate );
        for( unsigned pattern = 0; pattern < (1u << b.stimulus.size()); ++pattern )
        {
            const unsigned gray = pattern ^ (pattern >> 1);
            for( std::size_t bit=0; bit<b.stimulus.size(); ++bit )
                b.Set( b.stimulus[bit], (gray & (1u<<bit)) ? gate.supply : 0 );
            b.Settle();
            for( const auto& channel : gate.channels )
            {
                std::vector<bool> inputs;
                for( int pin : channel.inputs ) inputs.push_back( b.values.at(pin) > gate.supply*.5 );
                const bool high = Truth( gate.function, inputs );
                const bool enabled = !channel.enable || ((b.values.at(channel.enable)>gate.supply*.5)==channel.enableHigh);
                b.Check( channel, high, !enabled || (gate.drain && high) );
            }
        }
        for( double voltage : {0.,gate.minimum*.5,gate.maximum+.5} )
        {
            b.Set( gate.vcc, voltage ); b.Settle();
            for( const auto& channel : gate.channels ) b.Check( channel, false, true );
        }
    }
}

BOOST_AUTO_TEST_CASE( SmallLogicSchmittRetainsEachPhysicalChannelAndReleasesOnPowerLoss )
{
    for( const auto& gate : SmallGates() )
    {
        if( !gate.schmitt ) continue;
        BOOST_TEST_CONTEXT( gate.name )
        {
            BENCH b( gate );
            const double band = gate.name.starts_with("74AHCT") ? 1.2 : .45*gate.supply;
            for( int pin : b.stimulus ) b.Set( pin, band );
            b.Settle();
            for( const auto& channel : gate.channels ) b.Check( channel, gate.function=="not" );
            for( int pin : b.stimulus ) b.Set( pin, gate.supply );
            b.Settle();
            for( int pin : b.stimulus ) b.Set( pin, band );
            b.Settle();
            for( const auto& channel : gate.channels ) b.Check( channel, gate.function!="not" );
            b.Set( gate.vcc, 0 ); b.Settle();
            for( const auto& channel : gate.channels ) b.Check( channel, false, true );
        }
    }
}

BOOST_AUTO_TEST_CASE( SmallLogicFamilySupplyEndpointsAndTtlAbsoluteThresholds )
{
    for( const auto& gate : SmallGates() )
    {
        if( gate.channels.size()!=1 || gate.function!="and" ) continue;
        BOOST_TEST_CONTEXT( gate.name )
        {
            BENCH b( gate );
            for( double supply : {gate.minimum,gate.maximum} )
            {
                b.Set( gate.vcc, supply );
                for( int pin : b.stimulus ) b.Set( pin, supply );
                b.Settle();
                const auto& pin = b.Pin(gate.channels.front().output);
                BOOST_CHECK( PinDriveLevel(pin,b.session->Snapshot().latest)==LOGIC_LEVEL::HIGH );
                BOOST_CHECK_SMALL( b.session->Snapshot().latest.values.at(pin.voltage)-supply,.02 );
                if( gate.name.starts_with("74AHCT") )
                {
                    for( int input : b.stimulus ) b.Set(input,2.1);
                    b.Settle();
                    BOOST_CHECK( PinDriveLevel(pin,b.session->Snapshot().latest)==LOGIC_LEVEL::HIGH );
                    b.Set(b.stimulus.front(),.7); b.Settle();
                    BOOST_CHECK( PinDriveLevel(pin,b.session->Snapshot().latest)==LOGIC_LEVEL::LOW );
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE( SmallLogicNcPinsAndExplicitPackageAliasesRemainElectricalMatches )
{
    MODEL_REPOSITORY repository;
    const std::map<std::string,std::string> aliases{
        {"SN74LVC1G00DBV","74lvc1g00"},{"SN74LVC1G00DCK","74lvc1g00"},{"SN74LVC1G00DRL","74lvc1g00"},
        {"SN74LVC1G125DBV","74lvc1g125"},{"SN74LVC1G125DCK","74lvc1g125"},{"SN74LVC1G125DRL","74lvc1g125"},
        {"SN74LVC1G14DBV","74lvc1g14"},{"SN74LVC1G14DRL","74lvc1g14"},{"SN74LVC2G14DBV","74lvc2g14"}
    };
    for( const auto& [name,model] : aliases )
    {
        CIRCUIT_COMPONENT part{"U1","74xGxx:"+name,name,{}};
        BOOST_CHECK_EQUAL( repository.SelectModel(part).id, model );
    }
    for( const auto& gate : SmallGates() )
    {
        CIRCUIT_COMPONENT part{"U1","74xGxx:"+gate.name,gate.name,{}};
        const auto model = repository.SelectModel(part);
        for( const auto& pin : model.pins )
            if( pin.label=="NC" )
            {
                BOOST_CHECK_EQUAL( pin.role,"passive" );
                BOOST_CHECK( model.driverEnable.find(pin.id)==model.driverEnable.end() );
                BOOST_CHECK( std::none_of(model.instance.begin(),model.instance.end(),[&](const auto& line)
                    {return line.starts_with("Bbh_in_"+pin.id) || line.starts_with("Cbh_in_"+pin.id);} ) );
            }
    }
}

BOOST_AUTO_TEST_CASE( SmallLogicInterfaceBuffersPreserveEveryBitAndBothBanks )
{
    for( const std::string family : {"AHC","AHCT"} )
        for( const std::string code : {"04","240","244"} )
        {
            SMALL_GATE gate;
            gate.name="74"+family+code; gate.library="74xx";
            gate.function=code=="244" ? "buf" : "not";
            gate.supply=5; gate.minimum=family=="AHCT" ? 4.5 : 2; gate.maximum=5.5;
            if( code=="04" )
            {
                gate.vcc=14; gate.ground=7;
                gate.channels={{{1},2},{{3},4},{{5},6},{{9},8},{{11},10},{{13},12}};
            }
            else
            {
                gate.vcc=20; gate.ground=10;
                gate.channels={{{2},18,1},{{4},16,1},{{6},14,1},{{8},12,1},
                               {{17},3,19},{{15},5,19},{{13},7,19},{{11},9,19}};
            }
            BOOST_TEST_CONTEXT( gate.name )
            {
                BENCH b(gate);
                auto word=[&](unsigned value)
                {
                    for( std::size_t bit=0; bit<gate.channels.size(); ++bit )
                        b.Set(gate.channels[bit].inputs.front(), value&(1u<<bit) ? 5 : 0);
                };
                auto check=[&](unsigned value)
                {
                    b.Settle();
                    for( std::size_t bit=0; bit<gate.channels.size(); ++bit )
                    {
                        const auto& channel=gate.channels[bit];
                        const bool disabled=channel.enable && b.values.at(channel.enable)>2.5;
                        b.Check(channel, ((value&(1u<<bit))!=0)!=(code!="244"), disabled);
                    }
                };
                for( unsigned value=0; value<(1u<<gate.channels.size()); ++value )
                {
                    const unsigned gray=value^(value>>1); word(gray); check(gray);
                }
                if( code!="04" )
                    for( unsigned enables=0; enables<4; ++enables )
                    {
                        b.Set(1,enables&1 ? 5 : 0); b.Set(19,enables&2 ? 5 : 0);
                        for( unsigned value : {0u,255u,0xa5u,0x5au} ) { word(value); check(value); }
                    }
                for( double voltage : {0.,gate.minimum*.5,6.} )
                {
                    b.Set(gate.vcc,voltage); b.Settle();
                    for( const auto& channel : gate.channels ) b.Check(channel,false,true);
                }
            }
        }
}

BOOST_AUTO_TEST_CASE( SmallLogicCompoundGatesDecodersAndDemultiplexersUseTheirSpecificTruthTables )
{
    std::vector<SMALL_GATE> gates;
    auto add=[&](const std::string& name,int vcc,int ground,std::vector<CHANNEL> channels)
    {
        SMALL_GATE gate;
        gate.name=name; gate.vcc=vcc; gate.ground=ground;
        const bool auc=name.starts_with("74AUC");
        gate.supply=auc ? 1.8 : 3.3; gate.minimum=auc ? .8 : 1.65; gate.maximum=auc ? 2.7 : 5.5;
        gate.channels=std::move(channels); gates.push_back(std::move(gate));
    };
    for( const std::string suffix : {"0832","3208","332","386"} )
        add("74LVC1G"+suffix,5,2,{{{1,3,6},4}});
    for( const std::string family : {"AUC","LVC"} )
    {
        if( family=="LVC" ) add("74LVC1G18",5,2,{{{3},6,1,false},{{3},4,1,true}});
        add("74"+family+"1G19",5,2,{{{1,3},6},{{1,3},4}});
    }
    add("74LVC1G139",8,4,{{{1,2},7},{{1,2},6},{{1,2},5},{{1,2},3}});
    add("74LVC2G157",8,4,{{{1,2,6,7},5},{{1,2,6,7},3}});
    for( const auto& gate : gates ) BOOST_TEST_CONTEXT( gate.name )
    {
        BENCH b(gate);
        auto high=[&](int pin){return b.values.at(pin)>gate.supply*.5;};
        for( unsigned pattern=0; pattern<(1u<<b.stimulus.size()); ++pattern )
        {
            const unsigned gray=pattern^(pattern>>1);
            for( std::size_t bit=0; bit<b.stimulus.size(); ++bit )
                b.Set(b.stimulus[bit],gray&(1u<<bit) ? gate.supply : 0);
            b.Settle();
            if( gate.name.ends_with("0832") ) b.Check(gate.channels[0],(high(1)&&high(3))||high(6));
            else if( gate.name.ends_with("3208") ) b.Check(gate.channels[0],(high(1)||high(3))&&high(6));
            else if( gate.name.ends_with("332") ) b.Check(gate.channels[0],high(1)||high(3)||high(6));
            else if( gate.name.ends_with("386") ) b.Check(gate.channels[0],high(1)^high(3)^high(6));
            else if( gate.name.ends_with("G18") )
            {
                b.Check(gate.channels[0],high(3),high(1));
                b.Check(gate.channels[1],high(3),!high(1));
            }
            else if( gate.name.ends_with("G19") )
            {
                b.Check(gate.channels[0],high(3)||high(1));
                b.Check(gate.channels[1],high(3)||!high(1));
            }
            else if( gate.name.ends_with("139") )
            {
                const unsigned address=(high(1)?1:0)+(high(2)?2:0);
                for( unsigned output=0; output<4; ++output ) b.Check(gate.channels[output],output!=address);
            }
            else
            {
                const bool selected=!high(7) && (high(6)?high(2):high(1));
                b.Check(gate.channels[0],selected); b.Check(gate.channels[1],!selected);
            }
        }
        for( double voltage : {0.,gate.minimum*.5,gate.maximum+.5} )
        {
            b.Set(gate.vcc,voltage); b.Settle();
            for( const auto& channel : gate.channels ) b.Check(channel,false,true);
        }
    }
}
