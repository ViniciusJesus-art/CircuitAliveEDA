/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace CIRCUITALIVE
{
// Functional HD44780 controller. Only accepted electrical edges mutate the bus.
class NATIVE_HD44780
{
public:
    void Reset(double time=0)
    {
        ram.fill(32);cgram.fill(0);address=shift=0;cg=false;increment=true;entryShift=false;
        display=cursor=blink=false;eight=true;twoLines=false;enable=half=readHalf=false;
        high=readByte=0;savedRs=false;busyUntil=time+.015;
    }
    void AdvanceAddress()
    {
        if(cg)address=(address+(increment?1:63))&63;
        else if(increment)address=address==0x27?0x40:address==0x67?0:(address+1)&127;
        else address=address==0?0x67:address==0x40?0x27:(address-1)&127;
    }
    void Execute(bool rs,unsigned byte,double time)
    {
        if(time+1e-12<busyUntil)return;
        busyUntil=time+37e-6;
        if(rs)
        {
            if(cg)cgram[address&63]=byte&31;else ram[address&127]=byte;
            AdvanceAddress();if(entryShift&&!cg)shift=(shift+(increment?1:39))%40;
        }
        else if(byte&128){cg=false;address=byte&127;}
        else if(byte&64){cg=true;address=byte&63;}
        else if(byte&32){eight=byte&16;twoLines=byte&8;half=readHalf=false;}
        else if(byte&16)
        {
            if(byte&8)shift=(shift+(byte&4?39:1))%40;
            else{const bool previous=increment;increment=byte&4;AdvanceAddress();increment=previous;}
        }
        else if(byte&8){display=byte&4;cursor=byte&2;blink=byte&1;}
        else if(byte&4){increment=byte&2;entryShift=byte&1;}
        else if(byte&2){address=0;cg=false;shift=0;busyUntil=time+1.52e-3;}
        else if(byte&1){ram.fill(32);address=0;cg=false;shift=0;busyUntil=time+1.52e-3;}
    }
    int Bus(bool rs,bool rw,bool e,unsigned data,double time)
    {
        if(e&&!enable&&rw&&(!readHalf||eight))readByte=rs?(cg?cgram[address&63]:ram[address&127]):((time<busyUntil?128:0)|address);
        if(!e&&enable)
        {
            if(rw)
            {
                if(eight||readHalf){if(rs)AdvanceAddress();readHalf=false;}else readHalf=true;
                half=false;
            }
            else
            {
                readHalf=false;
                if(eight)Execute(rs,data,time);
                else if(!half){high=data&240;savedRs=rs;half=true;}
                else{half=false;Execute(savedRs,high|(data>>4),time);}
            }
        }
        enable=e;
        return e&&rw?(eight?readByte:(readHalf?(readByte<<4)&240:readByte&240)):-1;
    }
    std::string Presentation(bool powered,bool backlight,double time) const
    {
        auto cells=nlohmann::json::array();
        for(unsigned row=0;row<2;++row)for(unsigned col=0;col<16;++col)cells.push_back(ram[(row?64:0)+(col+shift)%40]);
        return nlohmann::json{{"type","hd44780"},{"powered",powered},{"backlight",backlight},{"display",display},
            {"cells",cells},{"cgram",cgram},{"cursor",cursor},{"blink",blink&&(static_cast<std::uint64_t>(time/.4096)&1)!=0},
            {"address",cg?-1:static_cast<int>(address)},{"shift",shift}}.dump();
    }
    bool EightBit() const{return eight;}
private:
    std::array<unsigned,128> ram{};std::array<unsigned,64> cgram{};
    unsigned address=0,shift=0,high=0,readByte=0;
    bool cg=false,increment=true,entryShift=false,display=false,cursor=false,blink=false,eight=true,twoLines=false;
    bool enable=false,half=false,readHalf=false,savedRs=false;
    double busyUntil=.015;
};
}
