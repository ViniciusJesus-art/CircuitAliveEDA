/* AVR watchdog, sleep and clock/power control. SPDX-License-Identifier: GPL-3.0-or-later */
(function(global) {
  'use strict';
  global.createAVRSystem=function(cpu,nvm,device,io,reason) {
    const divisor=device.vectorDivisor,alwaysWatchdog=!(nvm.fuses.high&0x10);
    const interrupt={address:0x0c/divisor,flagRegister:0x60,flagMask:0x80,enableRegister:0x60,enableMask:0x40};
    let sleeping=false,mode=0,wdDeadline=Infinity,wdUnlock=-Infinity,ivUnlock=-Infinity;
    let bodUnlock=-Infinity,bodReady=-Infinity,bodSleepOff=false;
    let clockHalf=0,clockLevel=false;
    const clockOutput=!(nvm.fuses.low&0x40);
    cpu.data[0x54]=reason;cpu.data[0x66]=128;
    if(alwaysWatchdog||reason&8)cpu.data[0x60]=8;
    const wdEnabled=()=>!!(cpu.data[0x60]&0x48);
    const wdPeriod=()=>.016*(2**Math.min(9,((cpu.data[0x60]>>2)&8)|(cpu.data[0x60]&7)));
    function kick() {wdDeadline=wdEnabled()?io.now()+wdPeriod():Infinity;}
    kick();cpu.onWatchdogReset=kick;
    cpu.readHooks[0x60]=()=>{
      if(cpu.cycles>wdUnlock)cpu.data[0x60]&=~0x10;
      return cpu.data[0x60];
    };
    cpu.writeHooks[0x60]=value=>{
      const old=cpu.data[0x60],open=cpu.cycles<=wdUnlock;
      let next=(old&0x80)|(value&0x40);
      if(value&0x80)cpu.clearInterrupt(interrupt);
      next=(next&~0x80)|(cpu.data[0x60]&0x80);
      if((value&0x18)===0x18) {wdUnlock=cpu.cycles+4;next|=(old&0x2f)|0x18;}
      else next|=open?(value&0x2f):((old&0x27)|(value&8)|(old&8));
      if(alwaysWatchdog||cpu.data[0x54]&8)next|=8;
      cpu.data[0x60]=next;cpu.updateInterruptEnable(interrupt,next);
      if(!wdEnabled())wdDeadline=Infinity;
      else if(!(old&0x48)||open||(value&0x18)===0x18)kick();
      return true;
    };
    cpu.writeHooks[0x54]=value=>{cpu.data[0x54]&=value;return true;};
    cpu.writeHooks[0x55]=value=>{
      const old=cpu.data[0x55];
      if(value&1)ivUnlock=cpu.cycles+4;
      if((value&0xc0)===0xc0)bodUnlock=cpu.cycles+4;
      if((value&0x80)&&cpu.cycles<=bodUnlock)bodReady=cpu.cycles+3;
      cpu.data[0x55]=(value&0x10)|(cpu.cycles<=bodUnlock?value&0xc0:0)
        |(cpu.cycles<=ivUnlock?value&3:old&2);
      io.clocksChanged();return true;
    };
    cpu.readHooks[0x55]=()=>{
      if(cpu.cycles>ivUnlock)cpu.data[0x55]&=~1;
      if(cpu.cycles>bodUnlock)cpu.data[0x55]&=~0x40;
      if(!sleeping&&cpu.cycles>bodReady)cpu.data[0x55]&=~0x80;
      return cpu.data[0x55];
    };
    cpu.writeHooks[0x64]=value=>{
      io.beforeClocksChange();cpu.data[0x64]=value&0xef;io.clocksChanged();return true;
    };
    const powerBits={adc:0,usart:1,spi:2,timer1:3,timer0:5,timer2:6,twi:7,'twi-controller':7};
    function running(domain) {
      if(cpu.data[0x64]&(1<<powerBits[domain]))return false;
      if(!sleeping||mode===0)return true;
      if(domain==='twi')return true; // Address recognition is asynchronous.
      if(domain==='adc')return mode===1;
      return domain==='timer2'&&!!(cpu.data[0xb6]&0x20)&&[1,3,7].includes(mode);
    }
    function sleep() {
      if(!(cpu.data[0x53]&1))return;
      io.beforeClocksChange();mode=(cpu.data[0x53]>>1)&7;
      if(mode===4||mode===5)throw new Error('Reserved AVR sleep mode');
      bodSleepOff=cpu.cycles<=bodReady&&!!(cpu.data[0x55]&0x80)&&[2,3,6,7].includes(mode);
      sleeping=true;io.clocksChanged();io.onSleep(mode);
    }
    function canWake(address) {
      const vector=address*divisor;
      if(mode===0)return true;
      if(vector===0x02||vector===0x04) {
        const shift=vector===2?0:2;return !(cpu.data[0x69]&(3<<shift));
      }
      if([6,8,10,12,0x30].includes(vector))return true;
      if(mode===1&&[0x2a,0x2c,0x32].includes(vector))return true;
      return [1,3,7].includes(mode)&&!!(cpu.data[0xb6]&0x20)&&[0x0e,0x10,0x12].includes(vector);
    }
    function wake() {
      if(!sleeping)return false;
      if(!cpu.pendingInterrupts.some((interrupt,address)=>interrupt&&canWake(address)))return false;
      sleeping=false;bodSleepOff=false;io.clocksChanged();cpu.cycles+=4;return true;
    }
    function poll() {
      if(clockOutput) {
        if(!sleeping||mode===0) {
          while((clockHalf+1)/2<=cpu.cycles) {
            clockHalf++;clockLevel=!!(clockHalf&1);io.publish(io.timeForCycle(clockHalf/2));
          }
        } else {clockHalf=Math.floor(cpu.cycles*2);clockLevel=false;}
      }
      if(io.now()+1e-15>=wdDeadline) {
        const value=cpu.data[0x60];
        if(value&0x40)cpu.setInterruptFlag(interrupt);
        else if(value&8){io.reset(8);return;}
        kick();
      }
      wake();
    }
    function onInterrupt(address) {
      if(address===interrupt.address&&(cpu.data[0x60]&8)) {
        cpu.data[0x60]&=~0x40;cpu.updateInterruptEnable(interrupt,cpu.data[0x60]);
      }
      if(cpu.data[0x55]&2)cpu.pc+=nvm.bootStart/2;
    }
    return {running,sleep,poll,wake,onInterrupt,nextTime:()=>wdDeadline,
      get sleeping(){return sleeping;},get mode(){return sleeping?mode:-1;},
      get bodDisabled(){return bodSleepOff;},get clockOutput(){return clockOutput?clockLevel:undefined;}};
  };
})(globalThis);
