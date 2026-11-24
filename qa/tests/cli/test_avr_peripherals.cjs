/* Functional AVR peripheral regressions. Developer Node only. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'../../..');
const marker='    return {\n      reset:function(){';
let source=fs.readFileSync(path.join(root,'simulation/runtime/avr8js.js'),'utf8');
assert.equal(source.split(marker).length,2);
source=source.replace(marker,`    return {
      inspect:()=>({cpu,clock,system,nvm,adc,timer0,timer1,timer2,analog,uart,timerBridge}),
      reset:function(){`);
function fixture(device='atmega328p',options={},firmware=[0xff,0xcf]) {
  const context={};vm.runInNewContext(source,context);
  const pins=['VCC','AVCC','AREF','GND','RESET',...['B','C','D'].flatMap(p=>Array.from({length:p==='C'?7:8},(_,i)=>'P'+p+i))];
  let inputs=pins.map(pin=>({pin,voltage:['VCC','AVCC','AREF','RESET','PD0'].includes(pin)?5:pin==='PC0'?2.5:0})),time=0;
  const runtime=context.createDevice({device,options,firmware,eeprom:[],pins,clockHz:1e6,quantumSeconds:1e-6});
  runtime.advanceTo(0,inputs);
  return {runtime,context,get state(){return runtime.inspect();},get time(){return time;},
    advance:duration=>{time+=duration;return runtime.advanceTo(time,inputs);},
    input:(pin,voltage)=>{inputs=inputs.map(input=>input.pin===pin?{pin,voltage}:input);},
    write:(address,value)=>runtime.inspect().cpu.writeData(address,value)};
}
for(const name of ['atmega48p','atmega88p','atmega168p','atmega328p']) {
  const adc=fixture(name);adc.write(0x7c,0x40);adc.write(0x7a,0xe1);adc.advance(.00008);
  assert.ok(adc.state.cpu.data[0x7a]&0x10,`${name}: free-running ADIF`);
  assert.equal(adc.state.cpu.data[0x78]|adc.state.cpu.data[0x79]<<8,512);
  adc.state.cpu.readData(0x78);adc.input('PC0',5);adc.advance(.0001);
  assert.equal(adc.state.cpu.data[0x79],2,'ADCL read locks both result bytes');
  adc.state.cpu.readData(0x79);adc.advance(.0001);
  assert.equal(adc.state.cpu.data[0x78]|adc.state.cpu.data[0x79]<<8,1023);
  adc.write(0x64,1);const before=adc.state.cpu.data[0x78];adc.input('PC0',0);adc.advance(.0001);
  assert.equal(adc.state.cpu.data[0x78],before,'PRADC freezes conversion');
  adc.write(0x64,0);adc.advance(.0001);assert.equal(adc.state.cpu.data[0x79],0);

  const timer=fixture(name);timer.write(0x7c,0x40);timer.write(0x7b,3);timer.write(0x7a,0xa1);
  timer.write(0x47,19);timer.write(0x44,2);timer.write(0x45,1);timer.advance(.0002);
  assert.equal(timer.state.cpu.data[0x78]|timer.state.cpu.data[0x79]<<8,512,'Timer0 compare triggers ADC');
  const capture=fixture(name);capture.write(0x81,0xc1);capture.advance(.00002);capture.input('PB0',5);
  capture.advance(.000001);assert.equal(capture.state.cpu.data[0x36]&0x20,0,'Capture noise filter needs four clocks');
  capture.advance(.00001);assert.ok(capture.state.cpu.data[0x36]&0x20,'ICP1 captures filtered edge');
  assert.ok(capture.state.timer1.icr>=20);
  capture.write(0x36,0x20);capture.write(0x81,0x41);capture.write(0x50,0x0f);
  capture.input('PD6',4);capture.input('PD7',1);capture.advance(.00001);
  assert.ok(capture.state.cpu.data[0x50]&0x30,'Comparator ACO and ACI');
  assert.ok(capture.state.cpu.data[0x36]&0x20,'Comparator routes to Timer1 capture');

  const rtc=fixture(name);rtc.write(0xb6,0x20);rtc.write(0xb1,1);rtc.advance(.001);
  assert.ok(rtc.state.timer2.tcnt>=32&&rtc.state.timer2.tcnt<=33,'Asynchronous 32768 Hz counter');
  rtc.write(0x64,0x40);const count=rtc.state.timer2.tcnt;rtc.advance(.001);
  assert.equal(rtc.state.timer2.tcnt,count,'PRTIM2 freezes asynchronous timer');
  rtc.write(0x64,0);rtc.write(0x53,7);rtc.state.system.sleep();rtc.advance(.001);
  assert.notEqual(rtc.state.timer2.tcnt,count,'Timer2 runs in Power-save');
  const asleep=rtc.state.timer2.tcnt;rtc.write(0x53,5);rtc.state.system.sleep();rtc.advance(.001);
  assert.equal(rtc.state.timer2.tcnt,asleep,'Timer2 stops in Power-down');

  const watchdog=fixture(name);watchdog.state.cpu.data[256]=0x55;
  watchdog.write(0x60,0x18);watchdog.write(0x60,8);watchdog.advance(.020);
  assert.ok(watchdog.state.cpu.data[0x54]&8,'Watchdog resets with WDRF');
  assert.equal(watchdog.state.cpu.data[256],0x55,'Watchdog preserves SRAM');
  const sleep=fixture(name,{},[0x88,0x95,0xff,0xcf]);
  sleep.write(0x53,5);sleep.write(0x60,0x40);sleep.advance(.010);
  assert.equal(sleep.state.cpu.pc,1,'SLEEP stops instruction execution');
  sleep.advance(.010);assert.equal(sleep.state.system.sleeping,false,'Watchdog wakes Power-down');
  assert.ok(sleep.state.cpu.data[0x60]&0x80);
}

function exchange(mode,ninth=false,polarity=false,parityOdd=false) {
  const a=fixture(),b=fixture();
  if(mode===1)a.write(0x2a,0x10);
  for(const item of [a,b]) {
    item.write(0xc4,9);item.write(0xc2,(mode===1?0x46:0x26)|(polarity?1:0)|(parityOdd?0x10:0));
    item.write(0xc1,ninth?0x1d:0x18);
  }
  a.write(0xc6,0xa5);b.write(0xc6,0x3c);
  let aLine=1,bLine=1,xck=0;
  for(let step=0;step<2500;step++) {
    a.input('PD0',bLine?5:0);
    for(const event of a.advance(1e-6)) {
      if(event.pin==='PD1')aLine=event.drive===2?1:0;
      if(event.pin==='PD4')xck=event.drive===2?1:0;
    }
    b.input('PD0',aLine?5:0);b.input('PD4',xck?5:0);
    for(const event of b.advance(1e-6))if(event.pin==='PD1')bLine=event.drive===2?1:0;
  }
  assert.ok(a.state.cpu.data[0xc0]&0x80,'Controller RXC, mode '+mode);
  assert.ok(b.state.cpu.data[0xc0]&0x80,'Peer RXC');
  if(ninth){assert.ok(a.state.cpu.data[0xc1]&2);assert.ok(b.state.cpu.data[0xc1]&2);}
  assert.equal(a.state.cpu.readData(0xc6),0x3c,'Peer -> controller byte');
  assert.equal(b.state.cpu.readData(0xc6),0xa5,'Controller -> peer byte');
  assert.equal((a.state.cpu.data[0xc0]|b.state.cpu.data[0xc0])&0x1c,0,'No frame/parity/overrun error');
}
exchange(0,true);exchange(0,false,false,true);exchange(1);exchange(1,true,true);

const errors=fixture();errors.write(0xc4,9);errors.write(0xc2,0x26);errors.write(0xc1,0x10);
function injectFrame(value,length=8,parityBit=0,stop=true) {
  const frame=[0,...Array.from({length},(_,bit)=>(value>>bit)&1),parityBit,+stop,1];
  for(const level of frame){errors.input('PD0',level?5:0);errors.advance(.00016);}
}
injectFrame(0x55,8,1,false);
assert.equal(errors.state.cpu.data[0xc0]&0x94,0x94,'Receive flags report parity and frame errors');
errors.state.cpu.readData(0xc6);
for(const value of [3,5,6])injectFrame(value);
assert.ok(errors.state.cpu.data[0xc0]&8,'Unread receive FIFO reports overrun');
errors.write(0xc1,0);errors.write(0xc1,0x14);errors.write(0xc0,1);
injectFrame(0x55,9,0,true);assert.equal(errors.state.cpu.data[0xc0]&0x80,0,'MPCM ignores a data frame');
injectFrame(0x155,9,1,true);assert.ok(errors.state.cpu.data[0xc0]&0x80,'MPCM accepts an address frame');

for(const mode of [0,1,2,3,6,7]) {
  const power=fixture();power.write(0x45,1);power.write(0xb6,0x20);power.write(0xb1,1);
  power.write(0x68,1);power.write(0x6b,2); // PCINT1 wakes every sleep mode.
  power.write(0x53,(mode<<1)|1);power.state.system.sleep();power.advance(.001);
  assert.equal(power.state.system.sleeping,true,`Sleep mode ${mode} stays asleep without a wake source`);
  assert.equal(power.state.cpu.readData(0x46)!==0,mode===0,`Sleep mode ${mode}: synchronous timer clock`);
  assert.equal(power.state.timer2.tcnt!==0,[0,1,3,7].includes(mode),`Sleep mode ${mode}: asynchronous timer clock`);
  power.input('PB1',5);power.advance(.00001);
  assert.equal(power.state.system.sleeping,false,`PCINT wake from sleep mode ${mode}`);
}
const extClock=fixture();extClock.write(0xb6,0x60);extClock.write(0xb1,1);
for(let edge=0;edge<8;edge++) {
  extClock.input('PB6',5);extClock.advance(.000002);extClock.input('PB6',0);extClock.advance(.000002);
}
assert.equal(extClock.state.timer2.tcnt,8,'Timer2 external TOSC clock counts physical rising edges');
const ee=fixture();ee.write(0x41,7);ee.write(0x40,0x5a);ee.write(0x3f,4);ee.write(0x3f,2);
ee.advance(.001);assert.ok(ee.state.cpu.readData(0x3f)&2,'EEPROM stays busy for physical write time');
ee.write(0x61,0x80);ee.write(0x61,3);ee.advance(.003);
assert.equal(ee.state.cpu.readData(0x3f)&2,0,'EEPROM completion is independent of CLKPR');
ee.write(0x3f,1);assert.equal(ee.state.cpu.data[0x40],0x5a);
ee.input('RESET',0);ee.advance(.0001);ee.input('RESET',5);ee.advance(.0001);
ee.write(0x41,7);ee.write(0x3f,1);assert.equal(ee.state.cpu.data[0x40],0x5a,'EEPROM survives electrical reset');

const maskedTrigger=fixture();maskedTrigger.write(0x69,3); // INT0 rising, interrupt mask remains disabled.
maskedTrigger.write(0x7c,0x40);maskedTrigger.write(0x7b,2);maskedTrigger.write(0x7a,0xa1);
maskedTrigger.input('PD2',5);maskedTrigger.advance(.0001);
assert.equal(maskedTrigger.state.cpu.data[0x79],2,'INT0 triggers ADC without enabling its ISR');
const mspim=fixture();mspim.write(0x2a,0x10);mspim.write(0xc2,0xc0);mspim.write(0xc4,9);mspim.write(0xc1,0x18);
mspim.write(0xc6,0x96);let loopback=1;
for(let step=0;step<300;step++) {
  mspim.input('PD0',loopback?5:0);
  for(const event of mspim.advance(1e-6))if(event.pin==='PD1')loopback=event.drive===2;
}
assert.ok(mspim.state.cpu.data[0xc0]&0xc0,'MSPIM TXC and RXC');
assert.equal(mspim.state.cpu.readData(0xc6),0x96,'MSPIM electrical loopback');

for(const name of ['atmega48p','atmega88p','atmega168p','atmega328p']) {
  const relocated=fixture(name),state=relocated.state,vector=12/(name==='atmega48p'||name==='atmega88p'?2:1);
  const address=state.nvm.bootStart/2+vector;
  state.cpu.progMem[address]=0xcfff;relocated.write(0x55,1);relocated.write(0x55,2);
  state.cpu.data[95]=0x80;relocated.write(0x60,0x40);relocated.advance(.020);
  assert.equal(relocated.state.cpu.pc,address,`${name}: IVSEL relocates watchdog vector`);
}
const brown=fixture('atmega328p',{fuseExtended:0xfd}); // 2.7 V BOD.
brown.input('VCC',2.5);brown.advance(.0001);brown.input('VCC',5);brown.advance(.0001);
assert.ok(brown.state.cpu.data[0x54]&4,'Brown-out reset flag');
const resetPin=fixture('atmega328p',{fuseHigh:0x5f});
resetPin.write(0x27,0x40);resetPin.write(0x28,0x40);
assert.ok(resetPin.advance(.00001).some(event=>event.pin==='RESET'&&event.drive===2),'RSTDISBL enables PC6 driver');
const disabledInput=fixture();disabledInput.input('PC0',5);disabledInput.advance(.00001);
disabledInput.write(0x7e,1);assert.equal(disabledInput.state.cpu.readData(0x26)&1,0,'DIDR0 disables the digital input buffer');
const outputClock=fixture('atmega328p',{fuseLow:0xa2});
assert.ok(outputClock.advance(.00001).filter(event=>event.pin==='PB0').length>=19,'CKOUT drives the CPU clock on PB0');
const divided=fixture('atmega328p',{fuseLow:0x62});assert.equal(divided.state.clock.prescaler,8,'CKDIV8 fuse');
const bodOff=fixture('atmega328p',{fuseExtended:0xfd});
bodOff.write(0x55,0xc0);bodOff.write(0x55,0x80);bodOff.write(0x53,5);bodOff.state.system.sleep();
bodOff.input('VCC',2.5);bodOff.advance(.001);assert.ok(bodOff.state.cpu.cycles>0,'Timed BOD disable permits sleep below the BOD threshold');
const combined=fixture();combined.state.cpu.progMem[12]=0x9518;combined.state.cpu.data[95]=0x80;
combined.write(0x60,0x18);combined.write(0x60,0x48);combined.advance(.020);
assert.equal(combined.state.cpu.data[0x60]&0x40,0,'Combined watchdog mode clears WDIE on ISR entry');
combined.advance(.020);assert.ok(combined.state.cpu.data[0x54]&8,'Next combined-mode timeout resets');
const kicking=fixture('atmega328p',{},[0xa8,0x95,0xfe,0xcf]);
kicking.write(0x60,0x18);kicking.write(0x60,8);kicking.advance(.050);
assert.equal(kicking.state.cpu.data[0x54]&8,0,'WDR instructions keep the watchdog alive');
const bootReset=fixture('atmega328p',{fuseHigh:0xde});
assert.equal(bootReset.state.cpu.pc,bootReset.state.nvm.bootStart/2,'BOOTRST selects boot reset vector');
const baudChange=fixture();baudChange.write(0xc4,9);baudChange.write(0xc1,8);baudChange.write(0xc6,0xff);
baudChange.advance(.000080);baudChange.write(0x61,0x80);baudChange.write(0x61,3);
const baudEdges=baudChange.advance(.000800).filter(event=>event.pin==='PD1'&&event.drive===2);
assert.ok(baudEdges.length);assert.ok(Math.abs(baudEdges[0].time-.000720)<1e-9,'CLKPR stretches an in-flight USART bit');

for(const name of ['atmega48p','atmega88p','atmega168p','atmega328p']) {
  const memory={atmega48p:4096,atmega88p:8192,atmega168p:16384,atmega328p:32768}[name];
  const firmware=new Array(memory).fill(255);firmware[0]=255;firmware[1]=207;firmware[256]=0;firmware[257]=0;
  const flash=fixture(name,{},firmware);
  function spm(command,value=0) {
    const state=flash.state,pc=state.nvm.bootStart/2;
    state.cpu.progMem[pc]=0x95e8;state.cpu.progMem[pc+1]=0xcfff;state.cpu.pc=pc;
    state.cpu.dataView.setUint16(30,256,true);state.cpu.data[0]=value&255;state.cpu.data[1]=value>>8;
    flash.write(0x57,command);flash.advance(.00001);
  }
  spm(3);flash.advance(.005);assert.equal(flash.state.cpu.progMem[128],0xffff,'Page erase');
  spm(1,0x1234);spm(5);flash.advance(.005);assert.equal(flash.state.cpu.progMem[128],0x1234,'Page write');
  if(name!=='atmega48p') {
    spm(9,0xfb);assert.equal(flash.state.nvm.lock&4,0,'SPM programs BLB01');
    spm(3);flash.advance(.005);assert.equal(flash.state.cpu.progMem[128],0x1234,'BLB01 prevents application Flash erase');
  }
  function lpm(command,address) {
    const state=flash.state,pc=state.nvm.bootStart/2;
    state.cpu.progMem[pc]=0x9104;state.cpu.progMem[pc+1]=0xcfff;state.cpu.pc=pc;
    state.cpu.dataView.setUint16(30,address,true);flash.write(0x57,command);flash.advance(.00001);
    return state.cpu.data[16];
  }
  assert.equal(lpm(9,0),0xe2,'LPM reads the low fuse byte');
  if(name==='atmega328p')assert.equal(lpm(0x21,0),0x1e,'SIGRD reads the manufacturer signature');
  flash.runtime.reset();flash.runtime.advanceTo(0,[]);
  // Re-power after session reset; original frozen Flash must be restored.
  flash.input('VCC',5);flash.advance(.000001);assert.equal(flash.state.cpu.progMem[128],0,'Session reset restores Flash');
}
// Compare the adapter's ordinary GPIO publication with the emulator's own pad
// resolver, including direction changes, PUD and open-collector overrides.
{
  const gpio=fixture(),last=new Map(['B','C','D'].flatMap(name=>Array.from({length:8},(_,bit)=>['P'+name+bit,0]))),AVR=gpio.context.AVR8js;
  // A multi-cycle AVR instruction can finish after the requested frontier.
  // Flush its timestamped writes before comparing with current CPU registers.
  const collect=()=>{for(const event of gpio.advance(4e-6))last.set(event.pin,event.drive);};
  collect();
  for(const [name,ddr,portAddress,ordinary] of [['B',0x24,0x25,0x3e],['C',0x27,0x28,0x3f],['D',0x2a,0x2b,0xed]]) {
    const port=gpio.state.cpu.gpioByPort[portAddress];
    for(const collector of [0,0x55,0xff])for(const pud of [0,0x10]) {
      gpio.write(ddr,0);gpio.write(portAddress,0);collect();port.openCollector=collector;
      gpio.write(0x55,pud);collect();
      for(const direction of [0,0x0f,0xf0,0x55,0xaa,0xff])for(let value=0;value<256;value++) {
        gpio.write(ddr,direction);gpio.write(portAddress,value);collect();
        for(let bit=0;bit<8;bit++)if(ordinary&(1<<bit)) {
          const state=port.pinState(bit);
          const expected=state===AVR.PinState.Low?1:state===AVR.PinState.High?2:
            state===AVR.PinState.InputPullUp&&!pud?3:0;
          assert.equal(last.get('P'+name+bit),expected,`GPIO ${name}${bit}: DDR=${direction} PORT=${value} PUD=${pud} OC=${collector}`);
        }
      }
    }
  }
}
console.log('AVR functional peripherals passed: GPIO directions/PUD/open-collector, ADC/triggers, comparator/capture, Timer2/EXCLK, PRR, six sleep modes, watchdog, USART async/9-bit/sync/MSPIM, EEPROM, fuses/locks/reset/clock, IVSEL and Flash SPM.');
