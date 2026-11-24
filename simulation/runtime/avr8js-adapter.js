/* CircuitAliveEDA ATmega48P/88P/168P/328P adapter. SPDX-License-Identifier: GPL-3.0-or-later
 * Host protocol is architecture-independent; this is only one device backend.
 * Instructions are atomic; input voltages are held for the host sampling window.
 */
(function(global) {
  'use strict';
  const AVR = global.AVR8js;
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
    const device=global.CircuitAliveAVR.device(config.device);
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
      } else if(id==='RESET')gpioPins.push({id,port:'C',bit:6});
      else if(!['VCC','AVCC','GND','AREF','ADC6','ADC7'].includes(id)) {
        throw new Error('Unsupported AVR pin: '+id);
      }
    }
    const constantInputs=new Set(options.constantInputs||[]);
    const unconnectedPins=new Set(options.unconnectedPins||[]);
    const pinById=Object.fromEntries(gpioPins.map(pin=>[pin.id,pin]));
    const pinsByPort={B:gpioPins.filter(pin=>pin.port==='B'),C:gpioPins.filter(pin=>pin.port==='C'),D:gpioPins.filter(pin=>pin.port==='D')};
    const changedPinsByPort={};
    for(const name of Object.keys(pinsByPort))
      changedPinsByPort[name]=Array.from({length:256},(_,mask)=>pinsByPort[name].filter(pin=>mask&(1<<pin.bit)));
    const gpioRegisters=[0x24,0x25,0x27,0x28,0x2a,0x2b];
    let gpioWriteHooks;
    const timerControlRegisters=[0x44,0x80,0xb0];
    let timerControlHooks;
    const memory=new Uint8Array(device.flash);memory.fill(255);memory.set(config.firmware);
    const program=new Uint16Array(device.flash/2);
    for(let i=0;i<program.length;i++)program[i]=memory[2*i] | (memory[2*i+1]<<8);
    const eeprom=new AVR.EEPROMMemoryBackend(device.eeprom);
    eeprom.memory.set(config.eeprom);
    eeprom.readMemory=address=>eeprom.memory[address&(device.eeprom-1)];
    eeprom.writeMemory=(address,value)=>{eeprom.memory[address&(device.eeprom-1)]&=value;};
    eeprom.eraseMemory=address=>{eeprom.memory[address&(device.eeprom-1)]=255;};
    let cpu,clock,ports,adc,uart,timer0,timer1,timer2,serial,nvm,system,analog,timerBridge,clockRevision=0;
    let resetRequested=0,suppressInterrupt=false,persistentLock=options.lockBits===undefined?255:options.lockBits;
    let restartReason=1,brownout=false;
    const initialProgram=program.slice(),initialEeprom=eeprom.memory.slice();
    let confirmed=0,origin=0,active=false,events=[],last={},eventsOrdered=true;
    let voltages=Object.create(null);
    let lastInputs=null;
    let previousPud=0,inputBits=Object.create(null),physicalMasks={B:0,C:0,D:0};
    const supplyMin=options.supplyMin===undefined ? 1.8 : Number(options.supplyMin);
    const supplyMax=options.supplyMax===undefined ? 5.5 : Number(options.supplyMax);
    if(!Number.isFinite(supplyMin)||!Number.isFinite(supplyMax)||supplyMin<=0||supplyMax<supplyMin)
      throw new Error('Invalid supply range');
    function at(){return origin+(clock ? clock.timeNanos*1e-9 : 0);}
    function emit(pin,drive,time) {
      if(last[pin]===drive)return;
      last[pin]=drive;
      const stamp=Math.max(confirmed,time);
      if(events.length&&events[events.length-1].time>stamp)eventsOrdered=false;
      events.push({pin,drive,time:stamp});
      if(events.length>100000)throw new Error('Too many GPIO transitions in one window');
    }
    function portState(pin) {
      if(!active)return 0;
      if((pin.id==='PC6'||pin.id==='RESET')&&(!nvm||(nvm.fuses.high&0x80)))return 0;
      if((pin.id==='PB6'||pin.id==='PB7')&&(cpu.data[0xb6]&0x20))return 0;
      if(nvm&&(pin.id==='PB6'||pin.id==='PB7')) {
        const source=nvm.fuses.low&15;if(source>=4||(source===0&&pin.id==='PB6'))return 0;
      }
      if(pin.id==='PB0'&&system&&system.clockOutput!==undefined)return system.clockOutput?2:1;
      if(uart&&(pin.id==='PD1'||pin.id==='PD4')) {const state=uart.pin(pin.id);if(state!==undefined)return state;}
      if(serial) {
        const state=serial.pin(pin.id);
        if(state!==undefined)return state;
      }
      const state=ports[pin.port].pinState(pin.bit);
      if(state===AVR.PinState.Low)return 1;
      if(state===AVR.PinState.High)return 2;
      if(state===AVR.PinState.InputPullUp && !(cpu.data[0x55]&0x10))return 3;
      return 0;
    }
    function publish(time,pins=gpioPins) {
      for(const pin of pins)emit(pin.id,portState(pin),time);
    }
    function beforeClocksChange() {
      if(timerBridge)for(const timer of [timer0,timer1,timer2])timer.count(false);
      if(analog)analog.poll();
    }
    function clocksChanged() {
      lastInputs=null;
      if(timerBridge)timerBridge.clocksChanged();
      if(analog)analog.clocksChanged();
      if(serial&&serial.clocksChanged)serial.clocksChanged();
      if(uart)uart.clocksChanged();
    }
    function setup(time,reason=1) {
      const retained=cpu&&reason!==1?cpu.data.slice(256):null;
      const resetFlags=reason|(cpu&&reason!==1?cpu.data[0x54]:0);
      const retainedNVM=nvm&&reason!==1?nvm.retain():null;
      if(nvm)persistentLock=nvm.lock;
      serial=null;uart=null;analog=null;timerBridge=null;system=null;
      origin=time;previousPud=0;inputBits=Object.create(null);
      cpu=new AVR.CPU(program,device.sram);
      if(retained)cpu.data.set(retained,256);
      clock=new AVR.AVRClock(cpu,config.clockHz);
      const cf=x=>interrupts(x,device.vectorDivisor);
      ports={B:new AVR.AVRIOPort(cpu,cf(AVR.portBConfig)),
             C:new AVR.AVRIOPort(cpu,cf(AVR.portCConfig)),
             D:new AVR.AVRIOPort(cpu,cf(AVR.portDConfig))};
      gpioWriteHooks=gpioRegisters.map(address=>cpu.writeHooks[address]);
      for(const name of Object.keys(ports)) {
        const port=ports[name],update=port.updatePinRegister.bind(port);
        port.updatePinRegister=ddr=>{
          // PINx observes pad voltage, including outputs under load. PORTx and
          // DDRx still command the electrical driver independently of this readback.
          update(ddr&~physicalMasks[name]);
          const disabled=name==='C'?(cpu.data[0x7e]&0x3f)|((nvm&&nvm.fuses.high&0x80)?0x40:0)
            :name==='D'?(cpu.data[0x7f]&3)<<6:0;
          cpu.data[port.portConfig.PIN]&=~disabled;
        };
      }
      cpu.writeHooks[0x7e]=value=>{cpu.data[0x7e]=value&0x3f;ports.C.updatePinRegister(cpu.data[0x27]);return true;};
      cpu.writeHooks[0x7f]=value=>{cpu.data[0x7f]=value&3;ports.D.updatePinRegister(cpu.data[0x2a]);return true;};
      timer0=new AVR.AVRTimer(cpu,cf(AVR.timer0Config));
      timer1=new AVR.AVRTimer(cpu,cf(AVR.timer1Config));
      timer2=new AVR.AVRTimer(cpu,cf(AVR.timer2Config));
      adc=new AVR.AVRADC(cpu,cf(AVR.adcConfig));
      adc.channelValues.fill(0);
      nvm=global.createAVRNVM(cpu,device,{...options,lockBits:persistentLock},cf,{now:at},eeprom);
      if(retainedNVM)nvm.restore(retainedNVM);
      cpu.pc=nvm.resetPC;
      if(!(nvm.fuses.low&0x80)){clock.prescalerValue=8;cpu.data[0x61]=3;}
      system=global.createAVRSystem(cpu,nvm,device,{
        now:at,beforeClocksChange,clocksChanged,publish,
        timeForCycle:cycle=>origin+(cycle+clock.cyclesDelta)/clock.frequency,
        reset:reason=>{resetRequested=reason;},
        onSleep:mode=>{if(mode===1&&analog)analog.start();}
      },resetFlags);
      const read=pin=>!!inputBits[pin],running=domain=>system.running(domain);
      timerBridge=global.createAVRTimers(cpu,[timer0,timer1,timer2],cf,
        {now:at,read,running,beforeClocksChange},options);
      analog=global.createAVRAnalog(cpu,adc,cf,{running,vectorDivisor:device.vectorDivisor,
        voltage:pin=>(voltages[pin]||0)-(voltages.GND||0),captureSource:timerBridge.captureSource});
      uart=global.createAVRUSART(cpu,clock,cf,{now:at,read,running,publish});
      serial=global.createAVRSerial(cpu,clock,cf,{now:at,publish,read,running});
      const clockWrite=cpu.writeHooks[0x61];
      cpu.writeHooks[0x61]=value=>{
        system.poll();
        const old=clock.frequency,result=clockWrite(value);
        uart.frequencyChanged(old,clock.frequency,at());serial.frequencyChanged(old,clock.frequency,at());
        ++clockRevision;return result;
      };
      const calibrated=config.clockHz,calibration=Number(options.calibrationByte??128);
      cpu.writeHooks[0x66]=value=>{
        system.poll();const old=clock.frequency;
        if((nvm.fuses.low&15)===2)clock.baseFreqHz=calibrated*Math.max(.1,1+(value-calibration)/256);
        clock.cyclesDelta=(cpu.cycles+clock.cyclesDelta)*(clock.frequency/old)-cpu.cycles;
        uart.frequencyChanged(old,clock.frequency,at());serial.frequencyChanged(old,clock.frequency,at());
        cpu.data[0x66]=value;++clockRevision;return true;
      };
      const tick=cpu.tick.bind(cpu);
      cpu.tick=()=>{
        const pending=cpu.pendingInterrupts[cpu.nextInterrupt],enabled=cpu.interruptsEnabled;
        const blocked=system.sleeping||suppressInterrupt||!nvm.interruptsAllowed();
        if(blocked)cpu.data[95]&=~0x80;
        tick();
        if(enabled&&!blocked&&pending&&!(cpu.data[95]&0x80))
          system.onInterrupt(pending.address);
        if(enabled&&blocked)cpu.data[95]|=0x80;
      };
      for(const name of Object.keys(ports)) {
        const port=ports[name],changedPins=changedPinsByPort[name];let previousDdr=cpu.data[port.portConfig.DDR];
        port.addListener((value,previous)=>{
          const ddr=cpu.data[port.portConfig.DDR],changed=(value^previous)|(ddr^previousDdr);
          previousDdr=ddr;const time=at();
          // A port write often changes one pad. Other pads keep their drive,
          // including UART/SPI overrides, without reevaluating every function.
          const ordinary=name==='D'?0xed:name==='B'?(serial.active()?2:0x3e):(serial.active()?0x0f:0x3f);
          const low=ddr&~value,high=ddr&value&~port.openCollector;
          const pullups=(cpu.data[0x55]&0x10)?0:cpu.data[port.portConfig.PORT];
          for(const pin of changedPins[changed]) {
            const mask=1<<pin.bit;
            // Timer overrides already contribute to value. Dedicated serial,
            // reset, oscillator and clock-output pads use their full resolver.
            const drive=!active?0:(ordinary&mask)?((low&mask)?1:(high&mask)?2:(pullups&mask)?3:0):portState(pin);
            emit(pin.id,drive,time);
          }
        });
      }
      timerControlHooks=timerControlRegisters.map(address=>cpu.writeHooks[address]);
      clocksChanged();++clockRevision;
    }
    function apply(inputs,time) {
      // The host reuses immutable snapshots when pad voltages are unchanged.
      // DDR/register writes still refresh PINx through the installed hooks.
      if(inputs===lastInputs)return;
      lastInputs=inputs;
      for(const input of inputs) {
        voltages[input.pin]=input.voltage;
        const pin=pinById[input.pin];
        if(pin) {
          if(input.voltage===undefined)physicalMasks[pin.port]&=~(1<<pin.bit);
          else physicalMasks[pin.port]|=1<<pin.bit;
        }
      }
      const ground=voltages.GND===undefined ? 0:voltages.GND;
      const supply=(voltages.VCC===undefined ? 0:voltages.VCC)-ground;
      const reset=voltages.RESET===undefined ? supply : voltages.RESET-ground;
      const resetDisabled=!(Number(options.fuseHigh===undefined?0xdf:options.fuseHigh)&0x80);
      const bodFuse=device.fuseLayout==='328'?Number(options.fuseExtended??255):Number(options.fuseHigh??223);
      const bodVoltage=({4:4.3,5:2.7,6:1.8})[bodFuse&7]||0;
      if(bodVoltage)brownout=!(system&&system.bodDisabled)&&supply<(bodVoltage+(brownout?.05:0));
      const enabled=supply>=supplyMin && supply<=supplyMax && !brownout && (resetDisabled||reset>=supply*0.6);
      if(enabled && !active){events=[];setup(time,restartReason);active=true;}
      else if(!enabled && active){
        restartReason=brownout?4:(supply<supplyMin||supply>supplyMax)?1:2;
        events=[];active=false;publish(time);
      }
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
          if(pin.id==='PD2'&&previous!==undefined&&!(cpu.data[0x3d]&1)) {
            const sense=cpu.data[0x69]&3;
            if((sense===1&&previous!==level)||(sense===2&&previous&&!level)||(sense===3&&!previous&&level))
              cpu.setInterruptFlag({address:2/device.vectorDivisor,flagRegister:0x3c,flagMask:1,
                enableRegister:0x3d,enableMask:1});
          }
        }
        if(pin.port==='C' && pin.bit<=5)adc.channelValues[pin.bit]=(voltages[pin.id]??ground)-ground;
      }
      for(let bit=6;bit<8;bit++)adc.channelValues[bit]=(voltages['ADC'+bit]??ground)-ground;
      analog.updateComparator();timerBridge.sample();uart.sample(time);
      serial.sample(time);
    }
    function peripherals() {
      const pud=cpu.data[0x55]&0x10;
      if(previousPud===pud)return;
      const time=at();
      if(previousPud!==pud){previousPud=pud;publish(time);}
    }
    function inputSensitive() {
      // Timer1's internal counter does not require an electrical poll. Only
      // its capture input does; an undriven open pad cannot receive an edge.
      // Unknown/custom topologies retain conservative sampling. Output/pull-up
      // configuration restores sampling even on a physically open pad.
      const capturePin=pinById.PB0;
      const captureInput=!constantInputs.has('PB0') && (!unconnectedPins.has('PB0')
        || (capturePin && portState(capturePin)!==0));
      const capture=(cpu.data[0x81]&7) && ![8,10,12,14].includes(timer1.WGM) && captureInput;
      return (cpu.data[0x3d]&3) || (cpu.data[0x68]&7) || (cpu.data[0x7a]&0x40)
        || (cpu.data[0x6f]&0x20) || (cpu.data[0x45]&7)>=6 || (cpu.data[0x81]&7)>=6
        || uart.rxEnable || serial.active() || uart.busy()
        || (!(cpu.data[0x50]&0x80)&&((cpu.data[0x50]&12)||((cpu.data[0x7a]&0xa0)===0xa0&&(cpu.data[0x7b]&7)===1)))
        || capture || (cpu.data[0x7a]&0x20) || (cpu.data[0xb6]&0x40);
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
      return [0x23,0x26,0x29,0x7a,0x3d,0x68,0xc1,0x45,0x81,0x6f,
        0x4c,0x4d,0x4e,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0x50,0x53,0x55,0x60,0x64,0x7b,0x7c,
        0xb6,0xc2,0xc6,0x81].includes(address) || opcode===0x9588;
    }
    function pollHardware() {
      nvm.sync();timerBridge.poll();analog.poll();system.poll();
      if(uart.busy())uart.advance(at());
      if(serial.busy())serial.advance(at());
    }
    function hardwareDeadline() {
      return Math.min(system.nextTime(),nvm.nextTime(),timerBridge.nextTime(),uart.nextTime(),
        origin+(Math.min(analog.nextCycle,timerBridge.nextCycle)+clock.cyclesDelta)/clock.frequency);
    }
    function advance(time,inputs,adaptive) {
        if(!Number.isFinite(time)||time<confirmed)throw new Error('Invalid AVR simulated time');
        apply(inputs,confirmed);
        let sampling=!!(adaptive && active && inputSensitive());
        if(sampling)time=Math.min(time,confirmed+config.quantumSeconds);
        if(active) {
          let instructions=0;
          let revision=clockRevision;
          let scaledTime=(time-origin)*clock.frequency;
          let limit=scaledTime-clock.cyclesDelta;
          let rounding=Math.max(1e-6,(Math.abs(scaledTime)+Math.abs(clock.cyclesDelta)
            +(Math.abs(time)+Math.abs(origin))*clock.frequency)*Number.EPSILON*8);
          pollHardware();
          while(cpu.cycles<limit-rounding || at()<time) {
            if(lastInputs===null){apply(inputs,at());pollHardware();}
            if(!active)break;
            if(resetRequested) {
              const reason=resetRequested;resetRequested=0;const now=at();setup(now,reason);
              lastInputs=null;apply(inputs,now);publish(now);
              pollHardware();
            }
            if(system.sleeping||nvm.stalled()) {
              if(++instructions>1000000)throw new Error('AVR event budget exceeded');
              const next=Math.min(time,hardwareDeadline(),cpu.nextClockEvent
                ? origin+(cpu.nextClockEvent.cycles+clock.cyclesDelta)/clock.frequency:Infinity);
              const cycles=(next-origin)*clock.frequency-clock.cyclesDelta;
              if(cycles>cpu.cycles)cpu.cycles=cycles;
              cpu.tick();pollHardware();
              if(at()>=time-rounding/clock.frequency)break;
              if(system.sleeping||nvm.stalled())continue;
            }
            if(typeof global.__circuitaliveAvrRun==='function' && cpu.nextInterrupt<0
                && !serial.busy() && !uart.busy() && nvm.nativeSafe() && cpu.cycles+8<limit-rounding) {
              const deadline=Math.min(limit-rounding, cpu.cycles+100000,
                cpu.nextClockEvent ? cpu.nextClockEvent.cycles : Infinity,
                (hardwareDeadline()-origin)*clock.frequency-clock.cyclesDelta);
              if(deadline>cpu.cycles+8) {
                const counter=timerBridge.running(0) && timer0.divider && !timer0.tcntUpdated && !timer0.updateDivider
                  && !cpu.readHooks[0x35]
                  && timer0.TOP===255 && (timer0.timerMode===0 || timer0.timerMode===3);
                const block=global.__circuitaliveAvrRun(cpu.data,cpu.progBytes,cpu.pc,cpu.cycles,deadline,
                  counter ? timer0.tcnt : -1,timer0.lastCycle,timer0.divider,
                  !cpu.readHooks[0x24]&&!cpu.readHooks[0x25]&&!cpu.readHooks[0x27]&&!cpu.readHooks[0x28]
                    &&!cpu.readHooks[0x2a]&&!cpu.readHooks[0x2b]&&!cpu.readHooks[0x44]
                    &&!cpu.readHooks[0x80]&&!cpu.readHooks[0xb0] ? 1:0,
                  4 + (adaptive&&!sampling ?
                    (gpioRegisters.every((address,i)=>cpu.writeHooks[address]===gpioWriteHooks[i]) ? 1:0)
                    + (!(cpu.data[0xb6]&0x20)&&timerControlRegisters.every((address,i)=>cpu.writeHooks[address]===timerControlHooks[i]) ? 2:0) : 0));
                if(block[2])for(let i=0;i<block[2].length;i+=3) {
                  cpu.cycles=block[2][i+2];cpu.writeData(block[2][i],block[2][i+1]);
                }
                cpu.pc=block[0];cpu.cycles=block[1];
                if(cpu.cycles>=deadline) {
                  // Native execution reached this exact boundary without
                  // crossing it. Deliver its hardware events before another
                  // instruction; SEI/RETI are handled below in the adapter.
                  suppressInterrupt=false;
                  if(cpu.nextInterrupt>=0 || (cpu.nextClockEvent && cpu.nextClockEvent.cycles<=cpu.cycles))cpu.tick();
                  pollHardware();
                  continue;
                }
              }
            }
            if(++instructions>1000000)throw new Error('AVR instruction budget exceeded; use smaller windows');
            const opcode=cpu.progMem[cpu.pc];
            const observes=adaptive && observesInput(opcode);
            if(observes && at()>confirmed+1e-15) { time=Math.min(time,at());break; }
            if(opcode===0xffff)throw new Error('Execution reached erased AVR flash at byte '+(cpu.pc*2));
            suppressInterrupt=opcode===0x9478||opcode===0x9518;
            if(opcode===0x9588) {
              cpu.pc=(cpu.pc+1)%cpu.progMem.length;++cpu.cycles;system.sleep();
            } else if(opcode===0x9598) {cpu.pc=(cpu.pc+1)%cpu.progMem.length;++cpu.cycles;}
            else if(!nvm.instruction(opcode))AVR.avrInstruction(cpu);
            if(cpu.nextInterrupt>=0 || (cpu.nextClockEvent && cpu.nextClockEvent.cycles<=cpu.cycles))cpu.tick();
            if((cpu.data[0x55]&0x10)!==previousPud)peripherals();
            pollHardware();
            // Enabling RX, ADC or an external interrupt can happen at the very
            // first instruction after an input barrier. Bound this window now,
            // rather than keeping its previously autonomous deadline.
            const startedSampling=!!(observes && !sampling && inputSensitive());
            if(startedSampling){sampling=true;time=Math.min(time,confirmed+config.quantumSeconds);}
            if(revision!==clockRevision || startedSampling) {
              revision=clockRevision;
              scaledTime=(time-origin)*clock.frequency;
              limit=scaledTime-clock.cyclesDelta;
              rounding=Math.max(1e-6,(Math.abs(scaledTime)+Math.abs(clock.cyclesDelta)
                +(Math.abs(time)+Math.abs(origin))*clock.frequency)*Number.EPSILON*8);
            }
          }
        }
        if(!eventsOrdered){events.sort((a,b)=>a.time-b.time);eventsOrdered=true;}
        const ready=[],future=[];
        for(const event of events)(event.time<=time+1e-15 ? ready:future).push(event);
        events=future;confirmed=time;
        return adaptive ? {time,events:ready,
          needsInputs:!!(active && (inputSensitive() || observesInput(cpu.progMem[cpu.pc])))}:ready;
    }
    return {
      reset:function(){
        program.set(initialProgram);eeprom.memory.set(initialEeprom);nvm=null;cpu=null;
        persistentLock=options.lockBits===undefined?255:options.lockBits;resetRequested=0;
        restartReason=1;brownout=false;
        active=false;confirmed=0;origin=0;events=[];last={};eventsOrdered=true;voltages=Object.create(null);physicalMasks={B:0,C:0,D:0};
        lastInputs=null;
      },
      nextEventTime:function(){return confirmed+config.quantumSeconds;},
      advanceTo:function(time,inputs){return advance(time,inputs,false);},
      prepareWindow:function(time,inputs){return advance(time,inputs,true);}

    };
  };
})(globalThis);
