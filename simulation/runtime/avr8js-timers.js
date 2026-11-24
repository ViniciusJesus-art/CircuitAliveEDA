/* AVR timer clock domains, asynchronous Timer2 and Timer1 capture.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
(function(global) {
  'use strict';
  global.createAVRTimers=function(cpu,timers,cf,io,options) {
    let cmp=false,useComparator=false,previousCapture=false,candidate=null;
    let asyncNext=Infinity,asyncPhase=0,lastAsync=false,lastExternal=false;
    let syncEpoch=cpu.cycles,timer2Epoch=cpu.cycles;
    const asyncHz=options.timer2Hz===undefined?32768:Number(options.timer2Hz);
    if(!Number.isFinite(asyncHz)||asyncHz<=0||asyncHz>20000000)throw new Error('Invalid Timer2 oscillator frequency');
    const states=timers.map((timer,index)=>({timer,index,running:true,paused:cpu.cycles,
      base:timer.count,domain:'timer'+index}));
    function permitted(index) {
      if(!io.running('timer'+index))return false;
      const hold=cpu.data[0x43];
      return !((hold&0x80)&&(hold&(index===2?2:1)));
    }
    for(const state of states) {
      const {timer,index,base}=state;
      const eventTicks=()=>typeof timer.nextEventTicks==='function'?timer.nextEventTicks():1;
      timer.count=(reschedule=true,external=false)=>{
        if(!permitted(index)||(index===2&&(cpu.data[0xb6]&0x20))) {
          if(timer.tcntUpdated){timer.tcnt=timer.tcntNext;timer.tcntUpdated=false;}
          return;
        }
        // An atomic CPU instruction can cross compare AND wrap (notably tiny
        // CTC TOP values). Deliver both boundaries before counting the tail.
        const actual=cpu.cycles;
        if(timer.updateDivider) {
          const divider=timer.config.dividers[timer.CS],epoch=index===2?timer2Epoch:syncEpoch;
          // Timer0/1 share their continuously running prescaler. Align its
          // phase without removing/reusing the clock event currently executing.
          if(divider)cpu.cycles=epoch+Math.floor((actual-epoch)/divider)*divider;
          base(reschedule,external);cpu.cycles=actual;return;
        }
        if(!external&&!timer.updateDivider&&timer.divider&&!timer.tcntUpdated) {
          let budget=0;
          while(timer.lastCycle+eventTicks()*timer.divider<=actual) {
            if(++budget>10000)throw new Error('AVR timer event window too large');
            cpu.cycles=timer.lastCycle+eventTicks()*timer.divider;
            base(false);
            if(timer.TOP===0)timer.timerUpdated(0,1);
          }
          cpu.cycles=actual;
        }
        base(reschedule,external);
      };
      cpu.writeHooks[timer.config.TIFR]=value=>{
        const flags=cpu.data[timer.config.TIFR];
        for(const interrupt of [timer.OVF,timer.OCFA,timer.OCFB,...(index===1?[capture]:[])])
          if(value&interrupt.flagMask)cpu.clearInterrupt(interrupt);
        cpu.data[timer.config.TIFR]=flags&~value;return true;
      };
    }
    const timer1=timers[1],capture={address:cf(global.AVR8js.timer1Config).captureInterrupt,
      flagRegister:0x36,flagMask:0x20,enableRegister:0x6f,enableMask:0x20};
    const maskHook=cpu.writeHooks[0x6f];
    cpu.writeHooks[0x6f]=(value,...args)=>{cpu.updateInterruptEnable(capture,value);return maskHook(value,...args);};
    cpu.readHooks[0x86]=()=>{timer1.highByteTemp=timer1.icr>>8;return timer1.icr&255;};
    cpu.readHooks[0x87]=()=>timer1.highByteTemp;
    // TCNT low reads latch the shared TEMP register used for atomic 16-bit reads.
    const countLow=cpu.readHooks[0x84];
    cpu.readHooks[0x84]=address=>{const low=countLow(address);timer1.highByteTemp=timer1.tcnt>>8;return low;};
    cpu.readHooks[0x85]=()=>timer1.highByteTemp;
    function clocksChanged() {
      for(const state of states) {
        const {timer,index}=state,next=permitted(index)&&!(index===2&&(cpu.data[0xb6]&0x20));
        if(next===state.running)continue;
        if(!next) {state.base(false);cpu.clearClockEvent(timer.count);state.paused=cpu.cycles;}
        else {
          timer.lastCycle+=cpu.cycles-state.paused;
          if(timer.updateDivider||timer.divider)cpu.addClockEvent(timer.count,1);
        }
        state.running=next;
      }
      const asynchronous=!!(cpu.data[0xb6]&0x20)&&permitted(2);
      if(asynchronous!==lastAsync) {asyncNext=asynchronous?io.now()+1/asyncHz:Infinity;lastAsync=asynchronous;}
    }
    cpu.writeHooks[0x43]=value=>{
      io.beforeClocksChange();
      const released=(cpu.data[0x43]&0x80)&&!(value&0x80)?cpu.data[0x43]&3:0;
      if((value|released)&1){syncEpoch=cpu.cycles;for(const index of [0,1])timers[index].lastCycle=cpu.cycles;}
      if((value|released)&2){timer2Epoch=cpu.cycles;timers[2].lastCycle=cpu.cycles;asyncPhase=0;}
      cpu.data[0x43]=value&0x80?value&0x83:0;clocksChanged();return true;
    };
    cpu.writeHooks[0xb6]=value=>{
      io.beforeClocksChange();cpu.data[0xb6]=value&0x60;
      asyncPhase=0;clocksChanged();return true;
    };
    // ASSR update-busy bits live for two oscillator cycles. The new values are
    // latched by the functional model immediately, and writes are serialized.
    let updateDone=Infinity;
    for(const [address,bit] of [[0xb2,4],[0xb3,3],[0xb4,2],[0xb0,1],[0xb1,0]]) {
      const hook=cpu.writeHooks[address];
      cpu.writeHooks[address]=(...args)=>{
        const result=hook(...args);
        if(cpu.data[0xb6]&0x20){cpu.data[0xb6]|=1<<bit;updateDone=io.now()+2/asyncHz;}
        return result;
      };
    }
    function tickAsync() {
      const timer=timers[2],divider=[0,1,8,32,64,128,256,1024][cpu.data[0xb1]&7];
      if(timer.tcntUpdated){timer.tcnt=timer.tcntNext;timer.tcntUpdated=false;}
      timer.updateDivider=false;timer.divider=0;cpu.clearClockEvent(timer.count);
      if(divider&&++asyncPhase>=divider) {
        asyncPhase=0;states[2].base(false,true);if(timer.TOP===0)timer.timerUpdated(0,1);
      }
    }
    function sample() {
      if((cpu.data[0xb6]&0x60)===0x60&&permitted(2)) {
        const level=io.read('PB6');if(level&&!lastExternal)tickAsync();lastExternal=level;
      }
      const level=useComparator?cmp:io.read('PB0');
      if(level!==previousCapture) {
        previousCapture=level;
        candidate={level,cycle:cpu.cycles+((cpu.data[0x81]&0x80)?4:0)};
      }
      pollCapture();
    }
    function pollCapture() {
      if(!candidate||cpu.cycles<candidate.cycle)return;
      const level=candidate.level;candidate=null;
      if(!permitted(1)||level!==!!(cpu.data[0x81]&0x40)||[8,10,12,14].includes(timer1.WGM))return;
      timer1.count(false);timer1.icr=timer1.tcnt;
      cpu.data[0x86]=timer1.icr&255;cpu.data[0x87]=timer1.icr>>8;cpu.setInterruptFlag(capture);
    }
    function poll() {
      if(io.now()>=updateDone){cpu.data[0xb6]&=0x60;updateDone=Infinity;}
      while(lastAsync&&!(cpu.data[0xb6]&0x40)&&asyncNext<=io.now()+1e-15) {
        tickAsync();asyncNext+=1/asyncHz;
      }
      pollCapture();
    }
    return {clocksChanged,sample,poll,running:permitted,
      captureSource:(value,enabled)=>{cmp=value;useComparator=enabled;sample();},
      nextTime:()=>Math.min(updateDone,cpu.data[0xb6]&0x40?Infinity:asyncNext),
      get nextCycle(){return candidate?candidate.cycle:Infinity;}};
  };
})(globalThis);
