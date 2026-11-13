/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "behavioral_model.h"
#include "simulation_model.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace CIRCUITALIVE
{
using JSON = nlohmann::json;

namespace
{
// Equal-width adc_bridge/dac_bridge vectors are independent bit channels.
// One instance per model avoids repeating XSPICE's generic instance setup for
// every pin at every Newton iteration. Multi-bit converters are left intact.
void GroupPinBridges( std::vector<std::string>& lines )
{
    struct GROUP { std::size_t index, width = 0; std::string name, inputs, outputs; };
    std::map<std::string, std::vector<GROUP>> groups;
    std::vector<std::string> result;
    for( auto& line : lines )
    {
        std::istringstream words( line );
        std::string name, inputs, outputs, model, extra;
        words >> name >> std::ws;
        bool bridge = !name.empty() && name.front() == 'A' && words.get() == '[';
        if( bridge )
        {
            std::getline( words, inputs, ']' ); words >> std::ws;
            bridge = words.get() == '[';
            if( bridge )
            {
                std::getline( words, outputs, ']' ); words >> model;
                bridge = ( model == "bh_adc_${id}" || model == "bh_dac_${id}" )
                         && !( words >> extra );
            }
        }
        auto width = []( const std::string& pins )
        {
            std::istringstream tokens( pins ); std::string pin; std::size_t n = 0;
            while( tokens >> pin ) ++n;
            return n;
        };
        const auto count = bridge ? width( inputs ) : 0;
        if( count && count == width( outputs ) )
        {
            auto& batches = groups[model];
            // The generic DAC loader clears a matrix of port partials. Bound
            // its width so very wide packages do not incur quadratic work.
            const auto limit = model == "bh_dac_${id}" ? 16u : 64u;
            if( batches.empty() || batches.back().width + count > limit )
            {
                batches.push_back( { result.size(), 0, name, {}, {} } );
                result.emplace_back();
            }
            auto& group = batches.back();
            group.inputs += " " + inputs; group.outputs += " " + outputs;
            group.width += count;
        }
        else
            result.push_back( std::move( line ) );
    }
    for( const auto& [model, batches] : groups )
        for( const auto& group : batches )
            result[group.index] = group.name + " [" + group.inputs + "] [" + group.outputs + "] " + model;
    lines = std::move( result );
}
}

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
    const auto electrical = b.value( "electrical", JSON::object() );
    if( !electrical.is_object() ) throw std::runtime_error( "Behavioral electrical settings must be an object" );
    const std::set<std::string> electricalKeys{ "threshold_reference", "input_low_current", "input_high_current",
        "input_low_reference", "input_high_reference", "output_high_resistance", "output_low_resistance",
        "quiescent_low", "switching_capacitance", "source_rating", "sink_rating", "current_rating_window" };
    for( const auto& [key, value] : electrical.items() )
    {
        if( !electricalKeys.contains( key ) ) throw std::runtime_error( "Unknown electrical setting: " + key );
        par( value.get<std::string>() );
    }
    auto ep = [&]( const std::string& key, const std::string& fallback )
    {
        return electrical.contains( key ) ? par( electrical.at( key ).get<std::string>() ) : fallback;
    };
    if( electrical.contains( "threshold_reference" ) )
    {
        m.logicThresholdReference = electrical.at( "threshold_reference" );
        par( m.logicThresholdReference );
    }
    const auto thresholdSupply = ep( "threshold_reference", "max(v(" + vcc + "," + gnd + "),1u)" );
    const auto highResistance = ep( "output_high_resistance", "${param:rout}" );
    const auto lowResistance = ep( "output_low_resistance", "${param:rout}" );
    add( "Bbh_power_${id} bh_power_${id} 0 V={(v(" + vcc + "," + gnd + ") >= ${param:vmin} && v(" + vcc + "," + gnd + ") <= ${param:vmax})}" );
    std::string quiescent = "${param:iq}";
    const auto& outputs = b.at( "outputs" );
    if( electrical.contains( "quiescent_low" ) )
    {
        if( outputs.empty() ) throw std::runtime_error( "State-dependent supply current needs outputs" );
        std::string lowStates;
        for( const auto& [id, value] : outputs.items() )
            lowStates += ( lowStates.empty() ? "" : "+" ) + std::string( "(1-v(bh_code_" ) + id + "_${id}))";
        quiescent += "+(" + ep( "quiescent_low", "0" ) + "-${param:iq})*(" + lowStates + ")/" + std::to_string( outputs.size() );
    }
    add( "Bbh_iq_${id} " + vcc + " " + gnd + " I={v(bh_power_${id}) > 0.5 ? " + quiescent + " : 0}" );
    def( ".model bh_adc_${id} adc_bridge(in_low=${param:vil} in_high=${param:vih})" );
    const auto& blocks = b.at( "blocks" );
    if( !blocks.is_array() || blocks.empty() || blocks.size() > 1024 ) throw std::runtime_error( "Invalid behavioral block count" );
    std::set<std::string> clockInputs;
    const bool inputHysteresis = b.value( "input_hysteresis", false );
    if( inputHysteresis )
        for( const auto& p : m.pins )
            if( p.role == "input" || p.role == "bidirectional" ) clockInputs.insert( p.id );
    if( b.value( "clock_hysteresis", false ) || inputHysteresis )
    {
        for( const auto& block : blocks )
            if( block.at( "type" ) == "dff" )
            {
                auto name = block.at( "clock" ).get<std::string>();
                if( name.starts_with( '!' ) ) name.erase( 0, 1 );
                clockInputs.insert( std::move( name ) );
            }
        // ngspice 46 does not support reversed ADC thresholds as hysteresis.
        // Convert the two threshold comparisons to digital set/reset signals
        // and retain the clock phase in a latch while neither is asserted.
        // Data pins keep the ordinary 0/X/1 thresholds.
        def( ".model bh_clock_adc_${id} adc_bridge(in_low=0.25 in_high=0.75 rise_delay=1p fall_delay=1p)" );
        def( ".model bh_clock_hold_${id} d_srlatch(sr_delay=1p enable_delay=1p set_delay=1p reset_delay=1p rise_delay=1p fall_delay=1p ic=2)" );
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
        const auto validInput = "v(bh_power_${id}) > 0.5 && ${drive:" + p.id + "} > 0.5";
        add( "Bbh_in_" + p.id + "_${id} " + n + " 0 V={(" + validInput + ") ? v(" + physical + "," + gnd + ")/" + thresholdSupply + " : (${param:vil}+${param:vih})/2}" );
        add( "Cbh_in_" + p.id + "_${id} " + physical + " " + gnd + " ${param:cin}" );
        if( electrical.contains( "input_low_current" ) || electrical.contains( "input_high_current" ) )
        {
            // A continuous signed I/V load, anchored at the datasheet IIL/IIH test voltages.
            // Negative current at LOW is essential for TTL fan-out; a capacitor alone is insufficient.
            const auto lowRef = ep( "input_low_reference", "0" );
            const auto highRef = ep( "input_high_reference", "max(v(" + vcc + "," + gnd + "),1u)" );
            const auto fraction = "min(1,max(0,(v(" + physical + "," + gnd + ")-" + lowRef + ")/max(" + highRef + "-" + lowRef + ",1u)))";
            add( "Bbh_load_" + p.id + "_${id} " + physical + " " + gnd + " I={v(bh_power_${id}) > 0.5 ? "
                + ep( "input_high_current", "0" ) + "*" + fraction + "-" + ep( "input_low_current", "0" ) + "*(1-" + fraction + ") : 0}" );
        }
        if( clockInputs.contains( p.id ) )
        {
            const auto stem = "bh_clock_" + p.id + "_${id}";
            const auto guard = inputHysteresis ? "(" + validInput + ") ? " : "";
            const auto unknown = inputHysteresis ? " : 0.5" : "";
            add( "B" + stem + "_high " + stem + "_high 0 V={" + guard + "(v(" + n + ") >= ${param:vih} ? 1 : 0)" + unknown + "}" );
            add( "B" + stem + "_low " + stem + "_low 0 V={" + guard + "(v(" + n + ") <= ${param:vil} ? 1 : 0)" + unknown + "}" );
            add( "A" + stem + "_adc [" + stem + "_high " + stem + "_low] [" + stem + "_set " + stem + "_reset] bh_clock_adc_${id}" );
            add( "A" + stem + "_hold " + stem + "_set " + stem + "_reset bh_d_1_${id} bh_d_0_${id} bh_d_0_${id} " + d + " NULL bh_clock_hold_${id}" );
        }
        else
            add( "Abh_adc_" + p.id + "_${id} [" + n + "] [" + d + "] bh_adc_${id}" );
        if( electrical.contains( "switching_capacitance" ) )
        {
            // Two unit-code edges per input cycle give ICC = CPD*VCC*f.
            // External load charging already flows through the high-side driver.
            const auto stem = "bh_cpd_" + p.id + "_${id}";
            const auto capacitance = ep( "switching_capacitance", "0" );
            add( "A" + stem + "_dac [" + d + "] [" + stem + "_code] bh_dac_${id}" );
            // A finite RC avoids trapezoidal current ringing from an ideal
            // voltage-source/capacitor pair; integrating |I| must count edges,
            // not amplify numerical derivative oscillations.
            add( "R" + stem + " " + stem + "_code " + stem + " {${param:transition}/max(" + capacitance + ",1e-24)}" );
            add( "C" + stem + " " + stem + " 0 " + capacitance );
            add( "B" + stem + "_supply " + vcc + " " + gnd
                + " I={v(bh_power_${id}) > 0.5 ? max(v(" + vcc + "," + gnd + "),0)*" + capacitance
                + "*abs(v(" + stem + "_code," + stem + "))/(2*${param:transition}) : 0}" );
        }
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
        if( electrical.contains( "output_high_resistance" ) || electrical.contains( "output_low_resistance" ) )
        {
            // Interpolate one target voltage and resistance, then route sourcing
            // current through VCC and sinking current through GND. Two parallel
            // partly-on drivers would add shoot-through on top of calibrated CPD.
            const auto target = "(${param:low_drop}+(v(" + vcc + "," + gnd + ")-${param:high_drop}-${param:low_drop})*v(" + code + "))";
            const auto resistance = "(" + highResistance + "*v(" + code + ")+" + lowResistance + "*(1-v(" + code + ")))";
            const auto current = known + "*(v(" + physical + "," + gnd + ")-" + target + ")/" + resistance;
            add( "Bbh_hi_" + id + "_${id} " + physical + " " + vcc + " I={v(" + enable + ") > 0.5 ? min(0," + current + ") : 0}" );
            add( "Bbh_lo_" + id + "_${id} " + physical + " " + gnd + " I={v(" + enable + ") > 0.5 ? max(0," + current + ") : 0}" );
        }
        else
        {
            add( "Bbh_hi_" + id + "_${id} " + physical + " " + vcc + " I={v(" + enable + ") > 0.5 ? " + known + "*v(" + code + ")*(v(" + physical + "," + vcc + ")+${param:high_drop})/" + highResistance + " : 0}" );
            add( "Bbh_lo_" + id + "_${id} " + physical + " " + gnd + " I={v(" + enable + ") > 0.5 ? " + known + "*(1-v(" + code + "))*(v(" + physical + "," + gnd + ")-${param:low_drop})/" + lowResistance + " : 0}" );
        }
        for( const auto& direction : { std::string( "source" ), std::string( "sink" ) } )
            if( electrical.contains( direction + "_rating" ) )
            {
                const std::string parameter = electrical.at( direction + "_rating" );
                par( parameter );
                const auto branch = direction == "source" ? "hi_" : "lo_";
                const auto node = "bh_rating_" + id + "_" + direction + "_${id}";
                // IOH/IOL are DC operating ratings, not instantaneous charging
                // limits. Average briefly so a normal capacitive edge does not
                // latch a permanent overload warning in the editor.
                add( "B" + node + " " + node + "_raw 0 V={abs(i(Bbh_" + std::string( branch ) + id + "_${id}))}" );
                add( "R" + node + " " + node + "_raw " + node + " {" + ep( "current_rating_window", "1u" ) + "/1n}" );
                add( "C" + node + " " + node + " 0 1n" );
                m.currentWarnings.push_back( { id + " " + direction, "v(" + node + ")", parameter } );
            }
        m.driverEnable[id] = "v(" + enable + ")"; m.driverValue[id] = "v(" + code + ")";
    }
    for( const auto& p : m.pins )
        if( ( p.role == "output" || p.role == "bidirectional" ) && !outputs.contains( p.id ) )
            throw std::runtime_error( "Missing behavioral output: " + p.id );
    for( const auto& name : references )
        if( !defined.contains( name ) ) throw std::runtime_error( "Undriven behavioral signal: " + name );
    if( m.measurePositive.empty() ) { m.measurePositive = m.logicVcc; m.measureNegative = m.logicGround; }
    GroupPinBridges( m.instance );
}
}
