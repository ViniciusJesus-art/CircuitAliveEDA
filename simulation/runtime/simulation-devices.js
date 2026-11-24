/* Functional simulation peripherals. Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * HD44780 instruction/DDR/CGRAM state and electrical parallel/PCF8574 adapters;
 * carrier-qualified IR demodulator. Times are simulation seconds.
 */
(function(global) {
  'use strict';
  class HD44780 {
    constructor(time=0) { this.reset(time); }
    reset(time) {
      this.ddram=new Uint8Array(128);this.ddram.fill(32);this.cgram=new Uint8Array(64);
      this.address=0;this.cg=false;this.increment=true;this.entryShift=false;this.shift=0;
      this.display=false;this.cursor=false;this.blink=false;this.eight=true;this.twoLines=false;
      this.enable=false;this.half=false;this.readHalf=false;this.busyUntil=time+.015;
    }
    advanceAddress() {
      if(this.cg)this.address=(this.address+(this.increment?1:63))&63;
      else if(this.increment)this.address=this.address===0x27?0x40:this.address===0x67?0:(this.address+1)&127;
      else this.address=this.address===0?0x67:this.address===0x40?0x27:(this.address-1)&127;
    }
    execute(rs,byte,time) {
      if(time+1e-12<this.busyUntil)return;
      this.busyUntil=time+37e-6;
      if(rs) {
        if(this.cg)this.cgram[this.address&63]=byte&31;else this.ddram[this.address&127]=byte;
        this.advanceAddress();if(this.entryShift&&!this.cg)this.shift=(this.shift+(this.increment?1:39))%40;
      } else if(byte&128) {this.cg=false;this.address=byte&127;}
      else if(byte&64) {this.cg=true;this.address=byte&63;}
      else if(byte&32) {this.eight=!!(byte&16);this.twoLines=!!(byte&8);this.half=false;this.readHalf=false;}
      else if(byte&16) {
        if(byte&8)this.shift=(this.shift+(byte&4?39:1))%40;
        else {const inc=this.increment;this.increment=!!(byte&4);this.advanceAddress();this.increment=inc;}
      } else if(byte&8) {this.display=!!(byte&4);this.cursor=!!(byte&2);this.blink=!!(byte&1);}
      else if(byte&4) {this.increment=!!(byte&2);this.entryShift=!!(byte&1);}
      else if(byte&2) {this.address=0;this.cg=false;this.shift=0;this.busyUntil=time+1.52e-3;}
      else if(byte&1) {this.ddram.fill(32);this.address=0;this.cg=false;this.shift=0;this.busyUntil=time+1.52e-3;}
    }
    bus(rs,rw,e,data,time) {
      if(e&&!this.enable&&rw) {
        if(!this.readHalf||this.eight)this.readByte=rs?(this.cg?this.cgram[this.address&63]:this.ddram[this.address&127]):((time<this.busyUntil?128:0)|this.address);
      }
      if(!e&&this.enable) {
        if(rw) {
          if(this.eight||this.readHalf) {if(rs)this.advanceAddress();this.readHalf=false;}
          else this.readHalf=true;
          this.half=false;
        } else {
          this.readHalf=false;
          if(this.eight)this.execute(rs,data,time);
          else if(!this.half){this.high=data&240;this.savedRS=rs;this.half=true;}
          else {this.half=false;this.execute(this.savedRS,this.high|(data>>4),time);}
        }
      }
      this.enable=e;
      return e&&rw ? (this.eight?this.readByte:(this.readHalf?(this.readByte<<4)&240:this.readByte&240)) : null;
    }
    presentation(powered,backlight,time) {
      const cells=[];
      for(let row=0;row<2;row++)for(let col=0;col<16;col++)cells.push(this.ddram[(row?64:0)+(col+this.shift)%40]);
      return {type:'hd44780',powered,backlight,display:this.display,cells,cgram:Array.from(this.cgram),
        cursor:this.cursor,blink:this.blink&&!!(Math.floor(time/.4096)&1),address:this.cg?-1:this.address,shift:this.shift};
    }
  }
  global.CircuitAliveHD44780=HD44780;
  global.createDevice=function(config) {
    const o=config.options||{}, quantum=config.quantumSeconds||1e-6;
    let now=0,volts=Object.create(null),events=[],last=Object.create(null),powered=false;
    const read=pin=>(volts[pin]||0)-(volts.GND||0);
    const logic=pin=>read(pin)>read('VCC')*.5;
    function emit(pin,drive,time=now) {
      if(last[pin]===drive)return;last[pin]=drive;events.push({pin,drive,time});
    }
    function inputs(list) { for(const p of list)volts[p.pin]=p.voltage; }
    if(config.device==='ir_demodulator') {
      const frequency=o.carrierHz||38000, period=1/frequency, tolerance=o.tolerance||.2;
      const threshold=o.thresholdCurrent||.001, minimum=o.minimumCycles||6, maximum=o.maximumBurst||.02;
      let optical=false,rising=-Infinity,first=0,cycles=0,active=false,expired=Infinity;
      return {
        nextEventTime:()=>Math.min(now+quantum,expired>now?expired:Infinity),
        advanceTo(to,list) {
          events=[];inputs(list);
          const supply=read('VCC');powered=supply>=o.supplyMin&&supply<=o.supplyMax;
          const light=powered&&(volts.IR||0)>=threshold;
          if(!powered){cycles=0;rising=-Infinity;active=false;expired=Infinity;}
          else if(light&&!optical) {
            const dt=now-rising;
            if(Math.abs(dt-period)<=period*tolerance){cycles++;}else{cycles=1;first=now;}
            rising=now;
            if(cycles>=minimum&&now-first<maximum)active=true;
            else if(now-first>=maximum)active=false;
            expired=now+period*(1.5+tolerance);
          }
          if(now>=expired){cycles=0;active=false;expired=Infinity;}
          optical=light;
          emit('OUT',powered?(active?1:2):0);
          if(expired<=to){active=false;cycles=0;emit('OUT',powered?2:0,expired);expired=Infinity;}
          now=to;return events;
        }
      };
    }
    const lcd=new HD44780();let backlight=false,latch=255,target;
    const i2c=config.device==='hd44780_pcf8574';
    const mapping=o.mapping||{rs:0,rw:1,e:2,backlight:3,d4:4,d5:5,d6:6,d7:7};
    let pcfRead=255;
    function port(value) {
      latch=value;let data=0;for(let bit=4;bit<8;bit++)if(value&(1<<mapping['d'+bit]))data|=1<<bit;
      const rs=!!(value&(1<<mapping.rs)),rw=!!(value&(1<<mapping.rw)),e=!!(value&(1<<mapping.e));
      const output=lcd.bus(rs,rw,e,data,now);pcfRead=latch;
      if(output!==null)for(let bit=4;bit<8;bit++)if(!(output&(1<<bit)))pcfRead&=~(1<<mapping['d'+bit]);
      backlight=!!(value&(1<<mapping.backlight));
    }
    if(i2c) {
      if(o.address!==undefined&&(!Number.isInteger(o.address)||o.address<0||o.address>127))throw new Error('I2C address must be an integer from 0 to 127');
      if(!global.CircuitAliveSerial)throw new Error('PCF8574 requires mcu-serial.js');
      target=new global.CircuitAliveSerial.I2CTarget({read:logic,drive:emit,complete:(status,byte)=>{
        if(status===0x80)port(byte);
        target.resume({ack:true,address:o.address===undefined?0x27:o.address,mask:0,byte:pcfRead},now);
      }},{sda:'SDA',scl:'SCL'});
      target.resume({ack:true,address:o.address===undefined?0x27:o.address,mask:0,byte:255},0,false);
    }
    return {
      nextEventTime:()=>now+quantum,
      presentation:()=>lcd.presentation(powered,backlight,now),
      advanceTo(to,list) {
        events=[];inputs(list);const supply=read('VCC');const on=supply>=o.supplyMin&&supply<=o.supplyMax;
        if(on&&!powered) {lcd.reset(now);latch=255;if(target){target.reset();target.resume({ack:true,address:o.address===undefined?0x27:o.address,mask:0,byte:255},now,false);}}
        powered=on;
        if(!on) {
          for(const pin of config.pins)if(/^D[0-7]$/.test(pin)||pin==='SDA'||pin==='SCL')emit(pin,0);
          backlight=false;
        } else if(i2c)target.sample(now);
        else {
          let data=0;for(let bit=0;bit<8;bit++)if(logic('D'+bit))data|=1<<bit;
          const value=lcd.bus(logic('RS'),logic('RW'),logic('E'),data,now);
          for(let bit=0;bit<8;bit++)emit('D'+bit,value===null||(!lcd.eight&&bit<4)?0:(value&(1<<bit)?2:1));
          backlight=read('LEDA')-read('LEDK')>1.5;
        }
        now=to;return events;
      }
    };
  };
})(globalThis);
