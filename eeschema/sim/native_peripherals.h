/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "programmable_device.h"
#include "native_hd44780.h"
#include <map>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace CIRCUITALIVE
{
/** Functional peripherals observe accepted electrical points, without executing
 * firmware or polling the script interpreter at a fixed clock. Deadlines bound
 * the solver's next step; rejected trial points never mutate peripheral state.
 */
class NATIVE_PERIPHERALS
{
    static constexpr double NEVER = std::numeric_limits<double>::infinity();
    struct IR_RECEIVER
    {
        std::string id;
        std::array<std::size_t, 2> inputs; // VCC, GND
        std::vector<std::size_t> opticalInputs;
        double minimum, maximum, threshold, period, tolerance, maximumBurst;
        unsigned minimumCycles;
        double rising = -NEVER, first = 0, expires = NEVER;
        unsigned cycles = 0;
        bool optical = false, active = false, blocked = false;
        int drive = -1;
    };
    struct LCD
    {
        std::string id;NATIVE_HD44780 controller;
        std::array<std::size_t,15> inputs; // VCC,GND,RS,RW,E,D0..D7,LEDA,LEDK
        std::array<int,8> drives;
        double minimum=2.7,maximum=5.5,time=0;
        bool powered=false,backlight=false;
    };
public:
    void Configure( const std::vector<PROGRAMMABLE_DEVICE_CONFIG>& devices,
                    const std::vector<std::string>& signals )
    {
        m_receivers.clear();m_lcds.clear();
        for( const auto& device : devices )
        {
            if( device.engine != "native" ) continue;
            if(device.device=="hd44780")
            {
                LCD lcd{};lcd.id=device.id;const auto options=nlohmann::json::parse(device.optionsJson);
                lcd.minimum=options.value("supplyMin",2.7);lcd.maximum=options.value("supplyMax",5.5);
                if(!std::isfinite(lcd.minimum)||!std::isfinite(lcd.maximum)||lcd.minimum<=0||lcd.maximum<lcd.minimum)
                    throw std::runtime_error("Invalid native LCD supply range");
                std::size_t i=0;
                for(const char* name:{"VCC","GND","RS","RW","E","D0","D1","D2","D3","D4","D5","D6","D7","LEDA","LEDK"})
                {
                    const auto binding=std::find_if(device.bindings.begin(),device.bindings.end(),[&](const auto& b){return b.id==name;});
                    if(binding==device.bindings.end())throw std::runtime_error("Missing native LCD input: "+std::string(name));
                    const auto signal=std::find(signals.begin(),signals.end(),binding->signal);
                    if(!binding->signal.empty()&&signal==signals.end())throw std::runtime_error("Native LCD input signal is missing");
                    lcd.inputs[i++]=binding->signal.empty()?signals.size():signal-signals.begin();
                }
                m_lcds.push_back(std::move(lcd));continue;
            }
            if( device.device != "ir_demodulator" )
                throw std::runtime_error( "Unknown native peripheral: " + device.device );
            const auto options = nlohmann::json::parse( device.optionsJson );
            IR_RECEIVER r{};
            r.id = device.id;
            auto positive = [&]( const char* name, double fallback )
            {
                const double value = options.value( name, fallback );
                if( !std::isfinite( value ) || value <= 0 )
                    throw std::runtime_error( "Invalid native peripheral option: " + std::string( name ) );
                return value;
            };
            r.minimum = positive( "supplyMin", 2.5 );
            r.maximum = positive( "supplyMax", 5.5 );
            r.period = 1 / positive( "carrierHz", 38000 );
            r.threshold = positive( "thresholdCurrent", .001 );
            r.tolerance = positive( "tolerance", .2 );
            r.maximumBurst = positive( "maximumBurst", .02 );
            const double cycles = positive( "minimumCycles", 6 );
            if( r.maximum < r.minimum || r.tolerance >= 1 || cycles > 1000000
                || std::floor( cycles ) != cycles )
                throw std::runtime_error( "Invalid native IR receiver options" );
            r.minimumCycles = static_cast<unsigned>( cycles );
            std::size_t i = 0;
            for( const auto* name : { "VCC", "GND" } )
            {
                const auto binding = std::find_if( device.bindings.begin(), device.bindings.end(),
                        [&]( const auto& b ) { return b.id == name; } );
                if( binding == device.bindings.end() )
                    throw std::runtime_error( "Missing native IR input: " + std::string( name ) );
                const auto signal = std::find( signals.begin(), signals.end(), binding->signal );
                if( !binding->signal.empty() && signal == signals.end() )
                    throw std::runtime_error( "Native IR input signal is missing" );
                r.inputs[i++] = binding->signal.empty() ? signals.size() : signal - signals.begin();
            }
            bool opticalBinding = false;
            for( const auto& binding : device.bindings )
            {
                if( binding.id != "IR" && !binding.id.starts_with( "IR_" ) ) continue;
                opticalBinding = true;
                if( binding.signal.empty() ) continue;
                const auto signal = std::find( signals.begin(), signals.end(), binding.signal );
                if( signal == signals.end() ) throw std::runtime_error( "Native IR input signal is missing" );
                r.opticalInputs.push_back( signal - signals.begin() );
            }
            if( !opticalBinding ) throw std::runtime_error( "Missing native IR optical input" );
            const auto output = std::find_if( device.bindings.begin(), device.bindings.end(),
                    []( const auto& b ) { return b.id == "OUT"; } );
            if( output == device.bindings.end() || ( output->driverSlot < 0 && output->highSource.empty() ) )
                throw std::runtime_error( "Missing native IR output driver" );
            m_receivers.push_back( std::move( r ) );
        }
        Reset();
    }

    void Reset()
    {
        m_events.clear();
        for(auto& lcd:m_lcds){lcd.controller.Reset();lcd.drives.fill(-1);lcd.time=0;lcd.powered=lcd.backlight=false;}
        for( auto& r : m_receivers )
        {
            r.rising = -NEVER; r.expires = NEVER; r.first = 0; r.cycles = 0;
            r.optical = r.active = r.blocked = false; r.drive = -1;
        }
    }

    const std::vector<PROGRAMMABLE_PIN_STATE>& Observe( double time, const std::vector<double>& values )
    {
        m_events.clear();
        for(auto& lcd:m_lcds)
        {
            auto read=[&](std::size_t pin){return lcd.inputs[pin]<values.size()?values[lcd.inputs[pin]]:0.;};
            const double ground=read(1),supply=read(0)-ground;const bool on=supply>=lcd.minimum&&supply<=lcd.maximum;
            if(on&&!lcd.powered)lcd.controller.Reset(time);
            lcd.time=time;lcd.powered=on;lcd.backlight=on&&read(13)-read(14)>1.5;
            int output=-1;
            if(on)
            {
                auto high=[&](std::size_t pin){return read(pin)-ground>supply*.5;};unsigned data=0;
                for(unsigned bit=0;bit<8;++bit)if(high(5+bit))data|=1u<<bit;
                output=lcd.controller.Bus(high(2),high(3),high(4),data,time);
            }
            for(unsigned bit=0;bit<8;++bit)
            {
                const int drive=output<0||(!lcd.controller.EightBit()&&bit<4)?0:(output&(1u<<bit)?2:1);
                if(drive!=lcd.drives[bit]){lcd.drives[bit]=drive;m_events.push_back({lcd.id,"D"+std::to_string(bit),static_cast<PROGRAMMABLE_DRIVE>(drive),time});}
            }
        }
        for( auto& r : m_receivers )
        {
            auto read = [&]( std::size_t i ) { return r.inputs[i] < values.size() ? values[r.inputs[i]] : 0.; };
            const double supply = read( 0 ) - read( 1 );
            const bool powered = supply >= r.minimum && supply <= r.maximum;
            double current = 0;
            for( const auto signal : r.opticalInputs ) current += std::max( 0., values[signal] );
            const bool light = powered && current >= r.threshold;
            if( !powered || time + 1e-12 >= r.expires )
            {
                r.cycles = 0; r.active = r.blocked = false; r.expires = NEVER; r.rising = -NEVER;
            }
            if( light && !r.optical )
            {
                if( std::abs( time - r.rising - r.period ) <= r.period * r.tolerance )
                    r.cycles = std::min( r.cycles + 1, r.minimumCycles );
                else
                {
                    r.cycles = 1; r.first = time; r.blocked = false; r.active = false;
                }
                r.rising = time;
                if( r.cycles >= r.minimumCycles && !r.blocked ) r.active = true;
                r.expires = time + r.period * ( 1.5 + r.tolerance );
            }
            if( r.active && time + 1e-12 >= r.first + r.maximumBurst )
            {
                r.active = false; r.blocked = true;
            }
            r.optical = light;
            const int drive = powered ? ( r.active ? 1 : 2 ) : 0;
            if( drive != r.drive )
            {
                r.drive = drive;
                m_events.push_back( { r.id, "OUT", static_cast<PROGRAMMABLE_DRIVE>( drive ), time } );
            }
        }
        return m_events;
    }

    std::map<std::string,std::string> Presentation() const
    {
        std::map<std::string,std::string> result;
        for(const auto& lcd:m_lcds)result[lcd.id]=lcd.controller.Presentation(lcd.powered,lcd.backlight,lcd.time);
        return result;
    }

    double Deadline() const
    {
        double time = NEVER;
        for( const auto& r : m_receivers )
        {
            time = std::min( time, r.expires );
            if( r.active ) time = std::min( time, r.first + r.maximumBurst );
        }
        return time;
    }
private:
    std::vector<IR_RECEIVER> m_receivers;
    std::vector<LCD> m_lcds;
    std::vector<PROGRAMMABLE_PIN_STATE> m_events;
};
}
