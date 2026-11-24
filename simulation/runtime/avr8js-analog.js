/* AVR ADC/comparator/capture triggers. SPDX-License-Identifier: GPL-3.0-or-later */
(function(global) {
  'use strict';
  global.createAVRAnalog=function(cpu,adc,cf,io) {
    const adcInterrupt=adc.ADC,comparator={address:0x2e/io.vectorDivisor,flagRegister:0x50,flagMask:0x10,
      enableRegister:0x50,enableMask:8};
    let completeAt=Infinity,result=0,latchedAdjust=false,locked=false,generation=0;
    let remaining=0,lastCycle=cpu.cycles,wasRunning=true,compare=false,initialized=false;
    const triggers=[null,[0x50,0x10],[0x3c,1],[0x35,2],[0x35,1],[0x36,4],[0x36,1],[0x36,0x20]];
    function start() {
      if(adc.converting||!(cpu.data[0x7a]&0x80)||!io.running('adc'))return;
      const channel=cpu.data[0x7c]&15;
      const voltage=channel<8?adc.channelValues[channel]:channel===8?.378125:channel===14?1.1:0;
      const reference=adc.referenceVoltage;
      result=reference>0?Math.min(1023,Math.max(0,Math.floor(1024*voltage/reference))):0;
      latchedAdjust=!!(cpu.data[0x7c]&0x20);adc.converting=true;cpu.data[0x7a]|=0x40;
      remaining=adc.sampleCycles;lastCycle=cpu.cycles;wasRunning=true;++generation;
      completeAt=cpu.cycles+remaining;
    }
    function trigger(register,mask) {
      if(!(cpu.data[0x7a]&0x20))return;
      const source=triggers[cpu.data[0x7b]&7];
      if(source&&source[0]===register&&(source[1]&mask))start();
    }
    const setFlag=cpu.setInterruptFlag.bind(cpu);
    cpu.setInterruptFlag=interrupt=>{
      const rising=!(cpu.data[interrupt.flagRegister]&interrupt.flagMask);
      setFlag(interrupt);
      if(rising)trigger(interrupt.flagRegister,interrupt.flagMask);
    };
    cpu.writeHooks[0x7a]=value=>{
      const old=cpu.data[0x7a];
      cpu.data[0x7a]=(value&0xaf)|(old&0x50);
      if(value&0x10)cpu.clearInterrupt(adcInterrupt);
      cpu.updateInterruptEnable(adcInterrupt,value);
      if(!(value&0x80)) {adc.converting=false;remaining=0;completeAt=Infinity;cpu.data[0x7a]&=~0x40;}
      else {
        if(!(old&0x80))adc.conversionCycles=25;
        if(value&0x40)start();
      }
      updateComparator();return true;
    };
    cpu.writeHooks[0x7b]=value=>{
      const old=cpu.data[0x7b];cpu.data[0x7b]=value&0x47;
      if((old&7)!==(value&7)) {
        const source=triggers[value&7];if(source&&(cpu.data[source[0]]&source[1]))trigger(...source);
      }
      updateComparator();return true;
    };
    cpu.readHooks[0x78]=()=>{locked=true;return cpu.data[0x78];};
    cpu.readHooks[0x79]=()=>{locked=false;return cpu.data[0x79];};
    cpu.writeHooks[0x50]=value=>{
      const old=cpu.data[0x50];cpu.data[0x50]=(old&0x30)|(value&0xcf);
      if(value&0x10)cpu.clearInterrupt(comparator);
      cpu.updateInterruptEnable(comparator,value);updateComparator();return true;
    };
    function updateComparator() {
      if(cpu.data[0x50]&0x80)return;
      const positive=cpu.data[0x50]&0x40?1.1:io.voltage('PD6');
      const mux=(cpu.data[0x7b]&0x40)&&!(cpu.data[0x7a]&0x80);
      const negative=mux?(adc.channelValues[cpu.data[0x7c]&7]||0):io.voltage('PD7');
      const next=positive>negative;
      cpu.data[0x50]=(cpu.data[0x50]&~0x20)|(next?0x20:0);
      if(initialized&&next!==compare) {
        const mode=cpu.data[0x50]&3;
        if(mode===0||(mode===2&&!next)||(mode===3&&next))cpu.setInterruptFlag(comparator);
      }
      compare=next;initialized=true;
      io.captureSource(compare,!!(cpu.data[0x50]&4));
    }
    cpu.writeHooks[0x7c]=value=>{cpu.data[0x7c]=value;updateComparator();return true;};
    function clocksChanged() {
      const running=io.running('adc');
      if(adc.converting) {
        if(wasRunning)remaining=Math.max(0,remaining-(cpu.cycles-lastCycle));
        lastCycle=cpu.cycles;wasRunning=running;completeAt=running?cpu.cycles+remaining:Infinity;
      }
    }
    function poll() {
      if(adc.converting&&cpu.cycles>=completeAt) {
        adc.converting=false;remaining=0;completeAt=Infinity;adc.conversionCycles=13;
        if(!locked) {
          cpu.data[0x78]=latchedAdjust?(result<<6)&255:result&255;
          cpu.data[0x79]=latchedAdjust?result>>2:result>>8;
        }
        cpu.data[0x7a]&=~0x40;cpu.setInterruptFlag(adcInterrupt);
        if((cpu.data[0x7a]&0xa0)===0xa0&&!(cpu.data[0x7b]&7))start();
      }
    }
    return {start,trigger,updateComparator,poll,clocksChanged,
      get converting(){return adc.converting;},get nextCycle(){return completeAt;}};
  };
})(globalThis);
