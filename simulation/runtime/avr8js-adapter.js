/* CircuitAliveEDA ATmega48P/88P/168P/328P adapter. SPDX-License-Identifier: GPL-3.0-or-later
 * Host protocol is architecture-independent; this is only one device backend.
 * Instructions are atomic; input voltages are held for the host sampling window.
 */
(function(global) {
  'use strict';
  const AVR = global.AVR8js;
  const devices = {
    atmega48p: {flash:4096,sram:512,eeprom:256,vectorDivisor:2},
    atmega88p: {flash:8192,sram:1024,eeprom:512,vectorDivisor:2},
    atmega168p: {flash:16384,sram:1024,eeprom:512,vectorDivisor:1},
    atmega328p: {flash:32768,sram:2048,eeprom:1024,vectorDivisor:1}
  };
  // The smaller devices use one-word interrupt vectors, unlike the 168/328.
  function interrupts(config, divisor) {
    if(Array.isArray(config))return config.map(x=>interrupts(x,divisor));
    if(config && typeof config==='object') {
      const result={};
      for(const key of Object.keys(config)) {
        const value=config[key];
        result[key]=/interrupt$/i.test(key) && typeof value==='number'
          ? value/divisor : interrupts(value,divisor);
      }
      return result;
    }
    return config;
  }
  global.createDevice = function(config) {
    const device=devices[config.device];
    if(!device)throw new Error('Unsupported AVR device: '+config.device);
    const options=config.options || {};
    if(options.sramBytes!==undefined && Number(options.sramBytes)!==device.sram)
      throw new Error('SRAM option does not match the selected AVR device');
    if(config.firmware.length>device.flash)throw new Error('Firmware exceeds device flash');
    if(config.eeprom.length>device.eeprom)throw new Error('EEPROM image exceeds device memory');
    const gpioPins=[];
    for(const id of config.pins) {
      if(/^P[BCD][0-7]$/.test(id)) {
        if(id==='PC7')throw new Error('PC7 is not an ATmega48/88/168/328 GPIO');
        gpioPins.push({id,port:id[1],bit:Number(id[2])});
      } else if(!['VCC','AVCC','GND','AREF','RESET','ADC6','ADC7'].includes(id)) {
        throw new Error('Unsupported AVR pin: '+id);
      }
    }
    const memory=new Uint8Array(device.flash);memory.fill(255);memory.set(config.firmware);
    const program=new Uint16Array(device.flash/2);
    for(let i=0;i<program.length;i++)program[i]=memory[2*i] | (memory[2*i+1]<<8);
    const eeprom=new AVR.EEPROMMemoryBackend(device.eeprom);
    eeprom.memory.set(config.eeprom);
    eeprom.readMemory=address=>eeprom.memory[address&(device.eeprom-1)];
    eeprom.writeMemory=(address,value)=>{eeprom.memory[address&(device.eeprom-1)]&=value;};
    eeprom.eraseMemory=address=>{eeprom.memory[address&(device.eeprom-1)]=255;};
    let cpu,clock,ports,adc,uart,timer0,clockRevision=0;
    let confirmed=0,origin=0,active=false,events=[],last={};
    let voltages=Object.create(null),txQueue=[],txHigh=true;
    let lastInputs=null;
    let rx=null,previousRx=true,previousPud=0,inputBits=Object.create(null);
    const supplyMin=options.supplyMin===undefined ? 1.8 : Number(options.supplyMin);
    const supplyMax=options.supplyMax===undefined ? 5.5 : Number(options.supplyMax);
    if(!Number.isFinite(supplyMin)||!Number.isFinite(supplyMax)||supplyMin<=0||supplyMax<supplyMin)
      throw new Error('Invalid supply range');
    function at(){return origin+(clock ? clock.timeNanos*1e-9 : 0);}
    function emit(pin,drive,time) {
      if(last[pin]===drive)return;
      last[pin]=drive;
      events.push({pin,drive,time:Math.max(confirmed,time)});
      if(events.length>100000)throw new Error('Too many GPIO transitions in one window');
    }
    function portState(pin) {
      if(!active)return 0;
      if(pin.id==='PC6')return 0; // Reset fuse stays enabled in this implementation.
      if(pin.id==='PD1' && uart && uart.txEnable)return txHigh ? 2 : 1;
      const state=ports[pin.port].pinState(pin.bit);
      if(state===AVR.PinState.Low)return 1;
      if(state===AVR.PinState.High)return 2;
      if(state===AVR.PinState.InputPullUp && !(cpu.data[0x55]&0x10))return 3;
      return 0;
    }
    function publish(time) {
      for(const pin of gpioPins)emit(pin.id,portState(pin),time);
    }
    function setup(time) {
      origin=time;txQueue=[];txHigh=true;rx=null;previousPud=0;inputBits=Object.create(null);
      cpu=new AVR.CPU(program,device.sram);
      clock=new AVR.AVRClock(cpu,config.clockHz);
      const cf=x=>interrupts(x,device.vectorDivisor);
      ports={B:new AVR.AVRIOPort(cpu,cf(AVR.portBConfig)),
             C:new AVR.AVRIOPort(cpu,cf(AVR.portCConfig)),
             D:new AVR.AVRIOPort(cpu,cf(AVR.portDConfig))};
      for(const name of Object.keys(ports)) {
        const port=ports[name],update=port.updatePinRegister.bind(port);
        const mapped=gpioPins.filter(pin=>pin.port===name);
        port.updatePinRegister=ddr=>{
          // PINx observes pad voltage, including outputs under load. PORTx and
          // DDRx still command the electrical driver independently of this readback.
          let physical=0;
          for(const pin of mapped)if(voltages[pin.id]!==undefined)physical|=1<<pin.bit;
          update(ddr&~physical);
        };
      }
      timer0=new AVR.AVRTimer(cpu,cf(AVR.timer0Config));
      new AVR.AVRTimer(cpu,cf(AVR.timer1Config));
      new AVR.AVRTimer(cpu,cf(AVR.timer2Config));
      adc=new AVR.AVRADC(cpu,cf(AVR.adcConfig));
      adc.channelValues.fill(0);
      const adcWrite=cpu.writeHooks[AVR.adcConfig.ADCSRA];
      cpu.writeHooks[AVR.adcConfig.ADCSRA]=(value,oldValue)=>{
        if(value&0x20)throw new Error('ADC auto-trigger mode is not implemented in this backend');
        return adcWrite(value,oldValue);
      };
      new AVR.AVREEPROM(cpu,eeprom,cf(AVR.eepromConfig));
      uart=new AVR.AVRUSART(cpu,cf(AVR.usart0Config),config.clockHz);
      uart.reset();
      const clockWrite=cpu.writeHooks[0x61];
      cpu.writeHooks[0x61]=value=>{
        const result=clockWrite(value);++clockRevision;uart.freqHz=clock.frequency;return result;
      };
      for(const port of Object.values(ports))port.addListener(()=>publish(at()));
      uart.onConfigurationChange=()=>{
        if(!uart.txEnable){txQueue=[];txHigh=true;}
        publish(at());
      };
      uart.onByteTransmit=value=>{
        if(!uart.txEnable)return;
        if(uart.bitsPerChar===9)throw new Error('9-bit USART electrical framing is not supported');
        const bitTime=1/uart.baudRate;
        const start=Math.max(at(),txQueue.length ? txQueue[txQueue.length-1].time : at());
        let index=0,parity=0;
        txQueue.push({time:start,high:false});
        for(let bit=0;bit<uart.bitsPerChar;bit++){
          const high=!!(value&(1<<bit));parity^=high ? 1:0;
          txQueue.push({time:start+(++index)*bitTime,high});
        }
        if(uart.parityEnabled)txQueue.push({time:start+(++index)*bitTime,high:!!(parity^(uart.parityOdd?1:0))});
        txQueue.push({time:start+(++index)*bitTime,high:true});
        txQueue.push({time:start+(index+uart.stopBits)*bitTime,high:true});
      };
      // Fail explicitly rather than silently pretending byte-only peripherals drive pins.
      cpu.writeHooks[0x4c]=(value)=>{
        if(value&0x40)throw new Error('SPI electrical bridge is not implemented in this backend');
      };
      cpu.writeHooks[0xbc]=(value)=>{
        if(value&0x04)throw new Error('TWI/I2C electrical bridge is not implemented in this backend');
      };
      cpu.writeHooks[0x60]=(value)=>{
        if(value&0x48)throw new Error('Watchdog is not implemented in this backend');
      };
      cpu.writeHooks[0x57]=(value)=>{
        if(value&1)throw new Error('Self-programming flash is not implemented in this backend');
      };
      cpu.readHooks[0x50]=()=>{
        throw new Error('Analog comparator is not implemented in this backend');
      };
      cpu.writeHooks[0x50]=(value)=>{
        if(value&0x0c)throw new Error('Analog comparator is not implemented in this backend');
      };
      cpu.writeHooks[0xb6]=(value)=>{
        if(value&0x20)throw new Error('Asynchronous Timer2 oscillator is not implemented in this backend');
      };
    }
    function apply(inputs,time) {
      // The host reuses immutable snapshots when pad voltages are unchanged.
      // DDR/register writes still refresh PINx through the installed hooks.
      if(inputs===lastInputs)return;
      lastInputs=inputs;
      for(const input of inputs)voltages[input.pin]=input.voltage;
      const ground=voltages.GND===undefined ? 0:voltages.GND;
      const supply=(voltages.VCC===undefined ? 0:voltages.VCC)-ground;
      const reset=voltages.RESET===undefined ? supply : voltages.RESET-ground;
      const enabled=supply>=supplyMin && supply<=supplyMax && reset>=supply*0.6;
      if(enabled && !active){events=[];setup(time);active=true;}
      else if(!enabled && active){events=[];active=false;txQueue=[];rx=null;publish(time);}
      if(!active)return;
      adc.avcc=(voltages.AVCC===undefined ? supply:voltages.AVCC-ground);
      adc.aref=(voltages.AREF===undefined ? supply:voltages.AREF-ground);
      for(const pin of gpioPins) {
        if(voltages[pin.id]!==undefined) {
          const voltage=voltages[pin.id]-ground;
          const previous=inputBits[pin.id];
          if(voltage>=supply*0.6)inputBits[pin.id]=true;
          else if(voltage<=supply*0.3)inputBits[pin.id]=false;
          const level=!!inputBits[pin.id];
          inputBits[pin.id]=level;
          if(previous===undefined||previous!==level)ports[pin.port].setPin(pin.bit,level);
        }
        if(pin.port==='C' && pin.bit<=5)adc.channelValues[pin.bit]=(voltages[pin.id]||ground)-ground;
      }
      for(let bit=6;bit<8;bit++)adc.channelValues[bit]=(voltages['ADC'+bit]||ground)-ground;
      const level=voltages.PD0===undefined ? true : voltages.PD0-ground>=supply*0.6;
      if(uart.rxEnable && !rx && previousRx && !level) {
        if(uart.bitsPerChar===9)throw new Error('9-bit USART electrical framing is not supported');
        rx={next:time+1.5/uart.baudRate,bitTime:1/uart.baudRate,bit:0,value:0,
            bits:uart.bitsPerChar,parity:uart.parityEnabled};
      }
      previousRx=level;
    }
    function peripherals() {
      const pud=cpu.data[0x55]&0x10;
      if(!txQueue.length&&!rx&&previousPud===pud)return;
      const time=at();
      while(txQueue.length && txQueue[0].time<=time) {
        const edge=txQueue.shift();txHigh=edge.high;
        const pin=gpioPins.find(p=>p.id==='PD1');
        if(pin)emit(pin.id,portState(pin),edge.time);
      }
      if(rx && time>=rx.next) {
        if(rx.bit<rx.bits){if(previousRx)rx.value|=1<<rx.bit;rx.bit++;rx.next+=rx.bitTime;}
        else if(rx.parity){rx.parity=false;rx.next+=rx.bitTime;}
        else {if(previousRx)uart.writeByte(rx.value,true);rx=null;}
      }
      if(previousPud!==pud){previousPud=pud;publish(time);}
    }
    function inputSensitive() {
      return (cpu.data[0x3d]&3) || (cpu.data[0x68]&7) || (cpu.data[0x7a]&0x40)
        || (cpu.data[0x6f]&0x20) || (cpu.data[0x45]&7)>=6 || (cpu.data[0x81]&7)>=6
        || uart.rxEnable;
    }
    function observesInput(opcode) {
      let address=-1;
      if((opcode&0xf000)===0xb000)address=(opcode&15)+((opcode&0x600)>>5)+32;
      else if((opcode&0xfc00)===0x9800)address=((opcode&0xf8)>>3)+32;
      else if((opcode&0xfc0f)===0x9000)address=cpu.progMem[cpu.pc+1];
      else if((opcode&0xfc0c)===0x900c && (opcode&3)!==3)
        address=cpu.dataView.getUint16(26,true)-((opcode&3)===2?1:0);
      else if((opcode&0xfc0e)===0x9008 && (opcode&1))address=cpu.dataView.getUint16(28,true);
      else if((opcode&0xfc0f)===0x900a)address=cpu.dataView.getUint16(28,true)-1;
      else if((opcode&0xfc0e)===0x9000 && (opcode&1))address=cpu.dataView.getUint16(30,true);
      else if((opcode&0xfc0f)===0x9002)address=cpu.dataView.getUint16(30,true)-1;
      else if((opcode&0xd000)===0x8000)
        address=cpu.dataView.getUint16(opcode&8?28:30,true)
          +((opcode&7)|((opcode&0xc00)>>7)|((opcode&0x2000)>>8));
      else if((opcode&0xfe0c)===0x9204)address=cpu.dataView.getUint16(30,true);
      // GPIO observations, starting ADC/RX/interrupts or selecting an external
      // timer clock need a fresh electrical snapshot at this instruction.
      return [0x23,0x26,0x29,0x7a,0x3d,0x68,0xc1,0x45,0x81,0x6f].includes(address);
    }
    function advance(time,inputs,adaptive) {
        if(!Number.isFinite(time)||time<confirmed)throw new Error('Invalid AVR simulated time');
        apply(inputs,confirmed);
        if(adaptive && active && inputSensitive())time=Math.min(time,confirmed+config.quantumSeconds);
        if(active) {
          let instructions=0;
          let revision=clockRevision;
          let scaledTime=(time-origin)*clock.frequency;
          let limit=scaledTime-clock.cyclesDelta;
          let rounding=Math.max(1e-6,(Math.abs(scaledTime)+Math.abs(clock.cyclesDelta)
            +(Math.abs(time)+Math.abs(origin))*clock.frequency)*Number.EPSILON*8);
          while(cpu.cycles<limit-rounding || at()<time) {
            if(typeof global.__circuitaliveAvrRun==='function' && cpu.nextInterrupt<0
                && !txQueue.length && !rx && cpu.cycles+8<limit-rounding) {
              const deadline=Math.min(limit-rounding, cpu.cycles+100000,
                cpu.nextClockEvent ? cpu.nextClockEvent.cycles : Infinity);
              if(deadline>cpu.cycles+8) {
                const counter=timer0.divider && !timer0.tcntUpdated && !timer0.updateDivider
                  && timer0.TOP===255 && (timer0.timerMode===0 || timer0.timerMode===3);
                const block=global.__circuitaliveAvrRun(cpu.data,cpu.progBytes,cpu.pc,cpu.cycles,deadline,
                  counter ? timer0.tcnt : -1,timer0.lastCycle,timer0.divider);
                cpu.pc=block[0];cpu.cycles=block[1];
              }
            }
            if(++instructions>1000000)throw new Error('AVR instruction budget exceeded; use smaller windows');
            const opcode=cpu.progMem[cpu.pc];
            if(adaptive && observesInput(opcode) && at()>confirmed+1e-15) { time=Math.min(time,at());break; }
            if(opcode===0xffff)throw new Error('Execution reached erased AVR flash at byte '+(cpu.pc*2));
            if(opcode===0x9588)throw new Error('SLEEP mode is not implemented in this backend');
            if(opcode===0x95e8 || opcode===0x95f8)throw new Error('Self-programming flash is not implemented in this backend');
            AVR.avrInstruction(cpu);
            if(cpu.nextInterrupt>=0 || (cpu.nextClockEvent && cpu.nextClockEvent.cycles<=cpu.cycles))cpu.tick();
            if(txQueue.length || rx || (cpu.data[0x55]&0x10)!==previousPud)peripherals();
            if(revision!==clockRevision) {
              revision=clockRevision;
              scaledTime=(time-origin)*clock.frequency;
              limit=scaledTime-clock.cyclesDelta;
              rounding=Math.max(1e-6,(Math.abs(scaledTime)+Math.abs(clock.cyclesDelta)
                +(Math.abs(time)+Math.abs(origin))*clock.frequency)*Number.EPSILON*8);
            }
          }
        }
        events.sort((a,b)=>a.time-b.time);
        const ready=[],future=[];
        for(const event of events)(event.time<=time+1e-15 ? ready:future).push(event);
        events=future;confirmed=time;
        return adaptive ? {time,events:ready,
          needsInputs:!!(active && (inputSensitive() || observesInput(cpu.progMem[cpu.pc])))}:ready;
    }
    return {
      reset:function(){
        active=false;confirmed=0;origin=0;events=[];last={};voltages=Object.create(null);
        lastInputs=null;
        txQueue=[];rx=null;previousRx=true;
      },
      nextEventTime:function(){return confirmed+config.quantumSeconds;},
      advanceTo:function(time,inputs){return advance(time,inputs,false);},
      prepareWindow:function(time,inputs){return advance(time,inputs,true);}

    };
  };
})(globalThis);
