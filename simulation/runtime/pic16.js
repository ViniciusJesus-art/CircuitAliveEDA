/* CircuitAliveEDA PIC16F84A interpreter and electrical adapter.
 * Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later
 * Instruction/register definitions: Microchip PIC16F84A datasheet DS35007C.
 * No host I/O, wall clock or architecture-specific native plugin is required.
 */
(function(global) {
  'use strict';
  global.createDevice = function(config) {
    if(config.device!=='pic16f84a')throw new Error('Unsupported PIC device: '+config.device);
    const options=config.options||{}, image=config.firmware;
    if(image.length>0x4280)throw new Error('PIC firmware image exceeds supported address space');
    if(config.eeprom.length>64)throw new Error('PIC16F84A EEPROM capacity is 64 bytes');
    const gpios=[];
    for(const id of config.pins) {
      if(/^RA[0-4]$/.test(id)||/^RB[0-7]$/.test(id))gpios.push({id,port:id[1],bit:Number(id[2])});
      else if(!['VCC','GND','RESET'].includes(id))throw new Error('Unsupported PIC16F84A pin: '+id);
    }
    function byte(address){return address<image.length ? image[address]:255;}
    // Intel HEX uses byte addresses: config word 2007h -> 400Eh,
    // data EEPROM words 2100h..213Fh -> 4200h..427Fh.
    for(let address=2048;address<image.length;address++) {
      if(byte(address)===255)continue;
      if((address>=0x4000&&address<0x4008)||(address>=0x400e&&address<0x4010)
          ||(address>=0x4200&&address<0x4280))continue;
      throw new Error('Unsupported PIC firmware region at byte 0x'+address.toString(16));
    }
    const configPresent=byte(0x400e)!==255||byte(0x400f)!==255;
    const configWord=(byte(0x400e)|(byte(0x400f)<<8))&0x3fff;
    if(configPresent&&(configWord&4))throw new Error('PIC16F84A watchdog must be disabled in the configuration word; WDT is not implemented');
    const originalEeprom=new Uint8Array(64);originalEeprom.fill(255);
    for(let i=0;i<64;i++)originalEeprom[i]=byte(0x4200+i*2);
    if(options.hasEepromImage!==false)originalEeprom.set(config.eeprom);
    const eeprom=new Uint8Array(originalEeprom);
    const period=4/config.clockHz;
    if(!Number.isFinite(period)||period<=0)throw new Error('Invalid PIC oscillator frequency');
    const supplyMin=options.supplyMin===undefined?4:Number(options.supplyMin);
    const supplyMax=options.supplyMax===undefined?5.5:Number(options.supplyMax);
    if(!Number.isFinite(supplyMin)||!Number.isFinite(supplyMax)||supplyMin<=0||supplyMax<supplyMin)
      throw new Error('Invalid PIC supply range');
    let ram,stack,sp,w,pc,cycles,origin,active=false,confirmed=0,events=[],last={};
    let voltages=Object.create(null),physicalA=0,physicalB=0,lastReadB=0;
    let prescaler=0,timerInhibit=0,pcWritten=false,externalA4=false,externalB0=false;
    function at(){return origin+cycles*period;}
    function emit(pin,drive,time) {
      if(last[pin]===drive)return;
      last[pin]=drive;events.push({pin,drive,time:Math.max(confirmed,time)});
      if(events.length>100000)throw new Error('Too many PIC GPIO transitions in one window');
    }
    function publish(time) {
      for(const pin of gpios) {
        let drive=0;
        if(active) {
          const tris=ram[pin.port==='A'?0x85:0x86];
          const latch=ram[pin.port==='A'?5:6];
          if(!(tris&(1<<pin.bit))) {
            const high=!!(latch&(1<<pin.bit));
            drive=pin.id==='RA4'&&high ? 0:(high?2:1); // RA4 is open-drain.
          } else if(pin.port==='B'&&!(ram[0x81]&0x80))drive=3;
        }
        emit(pin.id,drive,time);
      }
    }
    function setup(time) {
      ram=new Uint8Array(256);stack=new Uint16Array(8);sp=0;w=0;pc=0;cycles=0;origin=time;
      ram[3]=0x18;ram[0x81]=255;ram[0x85]=31;ram[0x86]=255;
      prescaler=0;timerInhibit=0;lastReadB=physicalB;pcWritten=false;
    }
    function address(raw) {
      const low=raw&127;
      if([0,2,3,4,10,11].includes(low)||low>=12)return low;
      return raw&255;
    }
    function readAddress(raw,indirect) {
      let a=address(raw);
      if(a===0) {
        if(indirect||(ram[4]&127)===0)return 0;
        return readAddress(ram[4],true);
      }
      if(a===2)return pc&255;
      if(a===5)return physicalA;
      if(a===6){lastReadB=physicalB;return physicalB;}
      if(a===0x89)return 0;
      if(a>=0x50&&a<0x80)return 0;
      return ram[a];
    }
    function writeAddress(raw,value,indirect) {
      const a=address(raw);value&=255;
      if(a===0) {
        if(!indirect&&(ram[4]&127)!==0)writeAddress(ram[4],value,true);
        return;
      }
      if(a===2){pc=((ram[10]&31)<<8)|value;pcWritten=true;return;}
      if(a===3){ram[3]=(ram[3]&0x18)|(value&0x27);return;}
      if(a===1){ram[1]=value;timerInhibit=3;if(!(ram[0x81]&8))prescaler=0;return;}
      if(a===5){ram[5]=value&31;return;}
      if(a===6){ram[6]=value;lastReadB=physicalB;return;}
      if(a===0x85){ram[a]=value&31;return;}
      if(a===10){ram[a]=value&31;return;}
      if(a===0x81){ram[a]=value;prescaler=0;return;}
      if(a===9){ram[a]=value&63;return;}
      if(a===0x88) {
        if(value&2)throw new Error('PIC16F84A EEPROM write timing/unlock is not implemented');
        ram[a]=value&0x1c;
        if(value&1)ram[8]=eeprom[ram[9]&63];
        return;
      }
      if(a===0x89||(a>=0x50&&a<0x80)||a===7||a===0x87)return;
      ram[a]=value;
    }
    function read(f){return readAddress(f|((ram[3]&0x20)?128:0),false);}
    function write(f,value){writeAddress(f|((ram[3]&0x20)?128:0),value,false);}
    function flag(mask,set){ram[3]=set ? ram[3]|mask:ram[3]&~mask;}
    function zero(value){flag(4,(value&255)===0);}
    function arithmetic(a,b,subtract) {
      const result=subtract ? a-b:a+b;
      flag(1,subtract ? a>=b:result>255);
      flag(2,subtract ? (a&15)>=(b&15):(a&15)+(b&15)>15);
      zero(result);return result&255;
    }
    function timerPulse() {
      if(timerInhibit)return;
      const divider=(ram[0x81]&8)?1:(2<<(ram[0x81]&7));
      if(++prescaler<divider)return;
      prescaler=0;ram[1]=(ram[1]+1)&255;
      if(ram[1]===0)ram[11]|=4;
    }
    function tick(count) {
      for(let i=0;i<count;i++) {
        if(timerInhibit)timerInhibit--;
        else if(!(ram[0x81]&0x20))timerPulse();
        cycles++;
      }
    }
    function pending() {
      return ((ram[11]&0x20)&&(ram[11]&4))||((ram[11]&0x10)&&(ram[11]&2))
        ||((ram[11]&8)&&(ram[11]&1))||((ram[11]&0x40)&&(ram[0x88]&0x10));
    }
    function interrupt() {
      if(!(ram[11]&0x80)||!pending())return false;
      stack[sp]=pc;sp=(sp+1)&7;pc=4;ram[11]&=0x7f;tick(2);publish(at());return true;
    }
    function step() {
      const location=pc&1023;
      const raw=byte(location*2)|(byte(location*2+1)<<8);
      if(raw===65535)throw new Error('Execution reached erased PIC flash at word 0x'+location.toString(16));
      const op=raw&0x3fff,f=op&127,d=!!(op&128),literal=op&255;
      pc=(pc+1)&0x1fff;pcWritten=false;let cost=1;
      const destination=value=>{if(d)write(f,value);else w=value&255;};
      const skip=()=>{pc=(pc+1)&0x1fff;cost=2;};
      if((op&0x3f9f)===0){} // NOP, including documented don't-care encodings.
      else if(op===8||op===9){sp=(sp+7)&7;pc=stack[sp];cost=2;if(op===9)ram[11]|=128;}
      else if(op===0x64){ram[3]|=0x18;if(ram[0x81]&8)prescaler=0;} // CLRWDT; WDT disabled.
      else if(op===0x63)throw new Error('PIC16F84A SLEEP is not implemented in this backend');
      else if(op===0x62)writeAddress(0x81,w,false); // Legacy OPTION.
      else if(op===0x65||op===0x66)writeAddress(op===0x65?0x85:0x86,w,false);
      else if((op&0x3f80)===0x80)write(f,w);
      else if((op&0x3f80)===0x100){w=0;zero(w);}
      else if((op&0x3f80)===0x180){write(f,0);zero(0);}
      else if((op&0x3800)===0x2000){stack[sp]=pc;sp=(sp+1)&7;pc=((ram[10]&0x18)<<8)|(op&0x7ff);cost=2;}
      else if((op&0x3800)===0x2800){pc=((ram[10]&0x18)<<8)|(op&0x7ff);cost=2;}
      else if((op&0x3c00)===0x3000)w=literal;
      else if((op&0x3c00)===0x3400){w=literal;sp=(sp+7)&7;pc=stack[sp];cost=2;}
      else if((op&0x3e00)===0x3e00){const old=w;w=(old+literal)&255;arithmetic(old,literal,false);}
      else if((op&0x3e00)===0x3c00){const old=w;w=(literal-old)&255;arithmetic(literal,old,true);}
      else if((op&0x3f00)===0x3800){w|=literal;zero(w);}
      else if((op&0x3f00)===0x3900){w&=literal;zero(w);}
      else if((op&0x3f00)===0x3a00){w^=literal;zero(w);}
      else if((op&0x3000)===0x1000) {
        const bit=1<<((op>>7)&7),value=read(f);
        switch(op&0x3c00) {
          case 0x1000:write(f,value&~bit);break;
          case 0x1400:write(f,value|bit);break;
          case 0x1800:if(!(value&bit))skip();break;
          case 0x1c00:if(value&bit)skip();break;
        }
      } else {
        const value=read(f),oldW=w;let result;
        switch(op&0x3f00) {
          case 0x200:result=(value-oldW)&255;destination(result);arithmetic(value,oldW,true);break;
          case 0x300:result=(value-1)&255;destination(result);zero(result);break;
          case 0x400:result=value|oldW;destination(result);zero(result);break;
          case 0x500:result=value&oldW;destination(result);zero(result);break;
          case 0x600:result=value^oldW;destination(result);zero(result);break;
          case 0x700:result=(value+oldW)&255;destination(result);arithmetic(value,oldW,false);break;
          case 0x800:destination(value);zero(value);break;
          case 0x900:result=value^255;destination(result);zero(result);break;
          case 0xa00:result=(value+1)&255;destination(result);zero(result);break;
          case 0xb00:result=(value-1)&255;destination(result);if(result===0)skip();break;
          case 0xc00:result=(value>>1)|((ram[3]&1)<<7);destination(result);flag(1,!!(value&1));break;
          case 0xd00:result=((value<<1)|(ram[3]&1))&255;destination(result);flag(1,!!(value&128));break;
          case 0xe00:destination(((value<<4)|(value>>4))&255);break;
          case 0xf00:result=(value+1)&255;destination(result);if(result===0)skip();break;
          default:throw new Error('Unsupported PIC opcode 0x'+op.toString(16)+' at word 0x'+location.toString(16));
        }
      }
      if(pcWritten)cost=2;
      tick(cost);publish(at());
    }
    function apply(inputs,time) {
      for(const input of inputs)voltages[input.pin]=input.voltage;
      const ground=voltages.GND||0,supply=(voltages.VCC||0)-ground;
      const reset=voltages.RESET===undefined?supply:voltages.RESET-ground;
      const enabled=supply>=supplyMin&&supply<=supplyMax&&reset>=supply*0.8;
      physicalA=0;physicalB=0;
      for(const pin of gpios)if((voltages[pin.id]||ground)-ground>=supply*0.6) {
        if(pin.port==='A')physicalA|=1<<pin.bit;else physicalB|=1<<pin.bit;
      }
      if(enabled&&!active){setup(time);active=true;publish(time);}
      else if(!enabled&&active){active=false;events=[];publish(time);}
      if(!active)return;
      const nextA4=!!(physicalA&16),nextB0=!!(physicalB&1);
      if((ram[0x81]&0x20)&&externalA4!==nextA4
          &&((ram[0x81]&0x10)?!nextA4:nextA4))timerPulse();
      if(externalB0!==nextB0&&((ram[0x81]&0x40)?nextB0:!nextB0))ram[11]|=2;
      if((physicalB^lastReadB)&0xf0&ram[0x86])ram[11]|=1;
      externalA4=nextA4;externalB0=nextB0;
    }
    return {
      reset:function(){
        confirmed=0;origin=0;cycles=0;active=false;events=[];last={};voltages=Object.create(null);
        physicalA=0;physicalB=0;lastReadB=0;externalA4=false;externalB0=false;eeprom.set(originalEeprom);
      },
      nextEventTime:function(){return confirmed+config.quantumSeconds;},
      advanceTo:function(time,inputs){
        if(!Number.isFinite(time)||time<confirmed)throw new Error('Invalid PIC simulated time');
        apply(inputs,confirmed);
        if(active){
          let instructions=0;
          while(at()<time){
            if(++instructions>1000000)throw new Error('PIC instruction budget exceeded; use smaller windows');
            if(!interrupt())step();
          }
        }
        events.sort((a,b)=>a.time-b.time);
        const ready=[],future=[];
        for(const event of events)(event.time<=time+1e-15?ready:future).push(event);
        events=future;confirmed=time;return ready;
      }
    };
  };
})(globalThis);
