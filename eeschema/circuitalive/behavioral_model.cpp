/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "behavioral_model.h"
#include "simulation_model.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
void CompileBehavioralModel( const JSON& b, SIMULATION_MODEL& m )
{
    if( b.at( "version" ) != 1 ) throw std::runtime_error( "Unsupported behavioral block version" );
    auto valid = []( const std::string& s )
    {
        return !s.empty() && s.size() <= 80 && std::all_of( s.begin(), s.end(), []( unsigned char c )
            { return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_'; } );
    };
    auto port = [&]( const std::string& id ) -> const MODEL_PIN&
    {
        auto it = std::find_if( m.pins.begin(), m.pins.end(), [&]( const auto& p ) { return p.id == id; } );
        if( it == m.pins.end() ) throw std::runtime_error( "Unknown behavioral port: " + id );
        return *it;
    };
    auto par = [&]( const std::string& id )
    {
        if( std::none_of( m.parameters.begin(), m.parameters.end(), [&]( const auto& p ) { return p.id == id; } ) )
            throw std::runtime_error( "Behavioral model needs parameter: " + id );
        return "${param:" + id + "}";
    };
    const auto supply = b.at( "supply" );
    m.logicVcc = supply.at( "vcc" ); m.logicGround = supply.at( "gnd" );
    port( m.logicVcc ); port( m.logicGround );
    if( m.logicVcc == m.logicGround ) throw std::runtime_error( "Behavioral supply ports must differ" );
    m.logicLow = "vil"; m.logicHigh = "vih"; m.logicMinSupply = "vmin"; m.logicMaxSupply = "vmax";
    for( const auto* id : { "vil", "vih", "vmin", "vmax", "rise_delay", "fall_delay", "transition", "rout", "cin", "iq", "high_drop", "low_drop" } ) par( id );
    const auto vcc = "${pin:" + m.logicVcc + "}", gnd = "${pin:" + m.logicGround + "}";
    auto add = [&]( std::string line ) { m.instance.push_back( std::move( line ) ); };
    auto def = [&]( std::string line ) { m.definitions.push_back( std::move( line ) ); };
    add( "Bbh_power_${id} bh_power_${id} 0 V={(v(" + vcc + "," + gnd + ") >= ${param:vmin} && v(" + vcc + "," + gnd + ") <= ${param:vmax})}" );
    add( "Bbh_iq_${id} " + vcc + " " + gnd + " I={v(bh_power_${id}) > 0.5 ? ${param:iq} : 0}" );
    def( ".model bh_adc_${id} adc_bridge(in_low=${param:vil} in_high=${param:vih})" );
    const auto& blocks = b.at( "blocks" );
    if( !blocks.is_array() || blocks.empty() || blocks.size() > 1024 ) throw std::runtime_error( "Invalid behavioral block count" );
    std::set<std::string> clockInputs;
    if( b.value( "clock_hysteresis", false ) )
    {
        for( const auto& block : blocks )
            if( block.at( "type" ) == "dff" )
            {
                auto name = block.at( "clock" ).get<std::string>();
                if( name.starts_with( '!' ) ) name.erase( 0, 1 );
                clockInputs.insert( std::move( name ) );
            }
        // ngspice's ADC uses hysteresis when in_low > in_high. Clock inputs
        // retain their phase in the threshold band instead of emitting X and
        // then creating another rising edge on the return to HIGH. Data pins
        // keep the ordinary 0/X/1 thresholds.
        def( ".model bh_clock_adc_${id} adc_bridge(in_low=${param:vih} in_high=${param:vil})" );
    }
    def( ".model bh_dac_${id} dac_bridge(out_low=0 out_high=1 out_undef=0.5 t_rise=${param:transition} t_fall=${param:transition})" );
    def( ".model bh_zero_${id} d_pulldown" ); def( ".model bh_one_${id} d_pullup" );
    add( "Abh_zero_${id} bh_d_0_${id} bh_zero_${id}" );
    add( "Abh_one_${id} bh_d_1_${id} bh_one_${id}" );
    std::set<std::string> defined{ "0", "1" }, references;
    auto signal = [&]( std::string name )
    {
        bool inverted = name.starts_with( "!" );
        if( inverted ) name.erase( 0, 1 );
        if( !valid( name ) ) throw std::runtime_error( "Invalid behavioral signal: " + name );
        references.insert( name );
        return ( inverted ? "~" : "" ) + std::string( "bh_d_" ) + name + "_${id}";
    };
    auto output = [&]( const std::string& name )
    {
        if( !valid( name ) || !defined.insert( name ).second )
            throw std::runtime_error( "Repeated/invalid behavioral signal driver: " + name );
        return "bh_d_" + name + "_${id}";
    };
    for( const auto& p : m.pins ) if( p.role == "input" || p.role == "bidirectional" )
    {
        const auto d = output( p.id ), n = "bh_n_" + p.id + "_${id}", physical = "${pin:" + p.id + "}";
        add( "Bbh_in_" + p.id + "_${id} " + n + " 0 V={(v(bh_power_${id}) > 0.5 && ${drive:" + p.id + "} > 0.5) ? v(" + physical + "," + gnd + ")/max(v(" + vcc + "," + gnd + "),1u) : 0.5}" );
        add( "Cbh_in_" + p.id + "_${id} " + physical + " " + gnd + " ${param:cin}" );
        add( "Abh_adc_" + p.id + "_${id} [" + n + "] [" + d + "] "
            + ( clockInputs.contains( p.id ) ? "bh_clock_adc_${id}" : "bh_adc_${id}" ) );
    }
    std::size_t index = 0, memoryBits = 0;
    for( const auto& block : blocks )
    {
        const std::string type = block.at( "type" ), stem = "bh_b" + std::to_string( ++index ) + "_${id}";
        const auto model = stem + "_model";
        std::string pins, spec;
        auto s = [&]( const char* key ) { return signal( block.at( key ).get<std::string>() ); };
        auto o = [&]( const char* key ) { return output( block.at( key ).get<std::string>() ); };
        auto vector = [&]( const char* key, bool out )
        {
            const auto names = block.at( key ).get<std::vector<std::string>>();
            if( names.empty() || names.size() > 64 ) throw std::runtime_error( "Invalid behavioral vector width" );
            std::string result = "[";
            for( const auto& n : names ) result += " " + ( out ? output( n ) : signal( n ) );
            return result + " ]";
        };
        const std::set<std::string> gates{ "and", "nand", "or", "nor", "xor", "xnor", "inverter", "buffer" };
        const std::string delays = "rise_delay=${param:rise_delay} fall_delay=${param:fall_delay}";
        if( gates.contains( type ) )
        {
            const auto inputs = block.at( "inputs" ).get<std::vector<std::string>>();
            if( ( ( type == "buffer" || type == "inverter" ) && inputs.size() != 1 )
                || ( type != "buffer" && type != "inverter" && inputs.size() < 2 ) )
                throw std::runtime_error( "Invalid gate input count" );
            pins = inputs.size() == 1 ? signal( inputs[0] ) : vector( "inputs", false );
            pins += " " + o( "out" ); spec = "d_" + type + "(" + delays + ")";
        }
        else if( type == "dff" || type == "sr_latch" )
        {
            int initial = block.value( "initial", 2 );
            if( initial < 0 || initial > 2 ) throw std::runtime_error( "Invalid initial logic state" );
            if( type == "dff" )
            {
                pins = s( "data" ) + " " + s( "clock" );
                spec = "d_dff(clk_delay=${param:rise_delay}";
            }
            else
            {
                pins = s( "s" ) + " " + s( "r" ) + " " + s( "enable" );
                spec = "d_srlatch(sr_delay=${param:rise_delay} enable_delay=${param:rise_delay}";
            }
            pins += " " + s( "set" ) + " " + s( "reset" ) + " " + o( "q" ) + " " + o( "nq" );
            spec += " set_delay=${param:rise_delay} reset_delay=${param:fall_delay} rise_delay=1p fall_delay=1p ic=" + std::to_string( initial ) + ")";
        }
        else if( type == "rom" || (type == "ram" && block.contains("memory")) )
        {
            const auto id=block.at("memory").get<std::string>();
            const auto region=std::find_if(m.memories.begin(),m.memories.end(),[&](const auto& r){return r.id==id;});
            if(region==m.memories.end() || region->addressBits!=block.at("address").size()
               || region->wordBits!=block.at("data_out").size()
               || region->writable!=(type=="ram"))
                throw std::runtime_error("Memory block shape/region mismatch: "+id);
            add("A"+stem+"_power [bh_power_${id}] ["+stem+"_pd] bh_adc_${id}");
            add("A"+stem+"_ready "+stem+"_pd "+stem+"_rd "+stem+"_ready_model");
            def(".model "+stem+"_ready_model d_buffer(rise_delay="+par("powerup_delay")+" fall_delay=1p)");
            const auto address=vector("address",false);
            std::string input;
            if(type=="ram")
            {
                if(block.at("data_in").size()!=region->wordBits)throw std::runtime_error("Memory data width mismatch");
                input=vector("data_in",false);
            }
            else { input="["; for(unsigned i=0;i<region->wordBits;++i)input+=" "+signal("0");input+=" ]"; }
            pins=address+" "+input+" "+vector("data_out",true)+" "+(type=="ram" ? s("write") : signal("0"))
                +" "+signal(block.at("select").at(0))+" "+stem+"_pd "+stem+"_rd";
            spec="ca_memory(slot=${memory:"+id+"} read_delay="+par("read_delay")+")";
        }
        else if( type == "ram" )
        {
            const auto width = block.at( "data_in" ).size(), bits = block.at( "address" ).size();
            const int select = block.value( "select_value", 1 ), initial = block.value( "initial", 2 );
            if( !bits || bits > 16 || !width || width > 32 || block.at( "data_out" ).size() != width
                || block.at( "select" ).size() != 1 || select < 0 || select > 1 || initial < 0 || initial > 2 )
                throw std::runtime_error( "RAM limits: 16 address bits, 32 data bits, one select" );
            memoryBits += ( std::size_t( 1 ) << bits ) * width;
            if( memoryBits > 2097152 ) throw std::runtime_error( "Behavioral model exceeds 2 Mibit of RAM" );
            // A power loss invalidates the volatile array: write to an unknown address while
            // selected. XSPICE d_ram then marks all words X, without expanding the array into gates.
            const auto selected = signal( block.at( "select" ).at( 0 ).get<std::string>() );
            add( "A" + stem + "_ctrl [" + s( "write" ) + " " + selected + "] [" + stem + "_wc " + stem + "_sc] bh_dac_${id}" );
            add( "B" + stem + "_wp " + stem + "_wp 0 V={v(bh_power_${id}) > .5 ? v(" + stem + "_wc) : 1}" );
            add( "B" + stem + "_sp " + stem + "_sp 0 V={v(bh_power_${id}) > .5 ? v(" + stem + "_sc) : " + std::to_string( select ) + "}" );
            add( "A" + stem + "_power [" + stem + "_wp " + stem + "_sp] [" + stem + "_w " + stem + "_s] bh_adc_${id}" );
            // Keep the core address unknown during power recovery, until its write/select
            // control paths have settled. Otherwise the restoring address could write a word.
            add( "A" + stem + "_pwr_adc [bh_power_${id}] [" + stem + "_pd] bh_adc_${id}" );
            add( "A" + stem + "_ready " + stem + "_pd " + stem + "_rd " + stem + "_ready_model" );
            def( ".model " + stem + "_ready_model d_buffer(rise_delay={max(" + par( "powerup_delay" ) + ",4*(" + par( "rise_delay" ) + "+" + par( "fall_delay" ) + "+" + par( "transition" ) + "))} fall_delay=1p)" );
            add( "A" + stem + "_ready_dac [" + stem + "_rd] [" + stem + "_ready] bh_dac_${id}" );
            std::string coreAddress = "[";
            std::size_t bit = 0;
            for( const auto& name : block.at( "address" ) )
            {
                const auto a = stem + "_addr" + std::to_string( bit++ );
                add( "A" + a + "_dac [" + signal( name ) + "] [" + a + "_code] bh_dac_${id}" );
                add( "B" + a + "_mask " + a + "_masked 0 V={(v(bh_power_${id}) > .5 && v(" + stem + "_ready) > .75) ? v(" + a + "_code) : .5}" );
                add( "A" + a + "_adc [" + a + "_masked] [" + a + "_d] bh_adc_${id}" );
                coreAddress += " " + a + "_d";
            }
            pins = vector( "data_in", false ) + " " + vector( "data_out", true ) + " " + coreAddress + " ]"
                + " " + stem + "_w [" + stem + "_s]";
            spec = "d_ram(select_value=" + std::to_string( select ) + " ic=" + std::to_string( initial )
                + " read_delay=" + par( "read_delay" ) + ")";
        }
        else throw std::runtime_error( "Unsupported behavioral block: " + type );
        add( "A" + stem + " " + pins + " " + model ); def( ".model " + model + " " + spec );
    }
    const auto& outputs = b.at( "outputs" );
    for( const auto& [id, value] : outputs.items() )
    {
        const auto& p = port( id );
        if( p.role != "output" && p.role != "bidirectional" ) throw std::runtime_error( "Behavioral output needs an output/bidirectional port" );
        const std::string code = "bh_code_" + id + "_${id}", enable = "bh_en_" + id + "_${id}", enCode = "bh_ec_" + id + "_${id}";
        add( "Abh_dac_" + id + "_${id} [" + signal( value.at( "signal" ) ) + "] [" + code + "] bh_dac_${id}" );
        add( "Abh_ena_" + id + "_${id} [" + signal( value.value( "enable", "1" ) ) + "] [" + enCode + "] bh_dac_${id}" );
        // Midpoint enable is X, not proof that a driver is enabled. Output stays electrically open.
        add( "Bbh_en_" + id + "_${id} " + enable + " 0 V={v(bh_power_${id}) > 0.5 && v(" + enCode + ") > 0.75}" );
        const auto physical = "${pin:" + id + "}";
        const std::string known = "((v(" + code + ") > 0.75 || v(" + code + ") < 0.25) ? 1 : 1u)";
        add( "Bbh_hi_" + id + "_${id} " + physical + " " + vcc + " I={v(" + enable + ") > 0.5 ? " + known + "*v(" + code + ")*(v(" + physical + "," + vcc + ")+${param:high_drop})/${param:rout} : 0}" );
        add( "Bbh_lo_" + id + "_${id} " + physical + " " + gnd + " I={v(" + enable + ") > 0.5 ? " + known + "*(1-v(" + code + "))*(v(" + physical + "," + gnd + ")-${param:low_drop})/${param:rout} : 0}" );
        m.driverEnable[id] = "v(" + enable + ")"; m.driverValue[id] = "v(" + code + ")";
    }
    for( const auto& p : m.pins )
        if( ( p.role == "output" || p.role == "bidirectional" ) && !outputs.contains( p.id ) )
            throw std::runtime_error( "Missing behavioral output: " + p.id );
    for( const auto& name : references )
        if( !defined.contains( name ) ) throw std::runtime_error( "Undriven behavioral signal: " + name );
    if( m.measurePositive.empty() ) { m.measurePositive = m.logicVcc; m.measureNegative = m.logicGround; }
}
}
