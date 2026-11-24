/* Register-level serial tests, running the bundled AVR CPU/adapter. */
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'../../..');
let source=fs.readFileSync(path.join(root,'simulation/runtime/avr8js.js'),'utf8');
const marker='    return {\n      reset:function(){';
assert.equal(source.split(marker).length,2);
source=source.replace(marker,'    return {\n      inspect:()=>cpu,\n      reset:function(){');
const context={};vm.runInNewContext(source,context);
const config={device:'atmega328p',firmware:[0xff,0xcf],eeprom:[],clockHz:16e6,quantumSeconds:1e-7,
  pins:['PB2','PB3','PB4','PB5','PC4','PC5','VCC','GND','RESET']};
const inputs=config.pins.map(pin=>({pin,voltage:pin==='GND'?0:5}));
const device=context.createDevice(config);
device.advanceTo(1e-6,inputs);
const cpu=device.inspect();
cpu.writeData(0x24,0x2c); // SS/MOSI/SCK outputs, MISO input.
cpu.writeData(0x4c,0xd2); // SPIE/SPE/MSTR, clock /64.
cpu.writeData(0x4e,0x39);cpu.writeData(0x4e,0x55); // Second write collides.
assert.equal(cpu.readData(0x4d)&0x40,0x40);
cpu.readData(0x4e);assert.equal(cpu.readData(0x4d)&0x40,0);
const edges=device.advanceTo(50e-6,inputs).filter(e=>e.pin==='PB5');
assert.ok(edges.length>=16);
assert.equal(cpu.readData(0x4d)&0x80,0x80,'SPIF after byte completion');
cpu.writeData(0x4d,0x80);
assert.equal(cpu.data[0x4d]&0x80,0x80,'SPSR flag is not write-one-to-clear');
assert.equal(cpu.readData(0x4e),0xff,'Receive HIGH on all MISO bits');
assert.equal(cpu.data[0x4d]&0x80,0,'SPSR then SPDR clears SPIF');
assert.equal(cpu.nextInterrupt,-1,'Clearing SPIF removes the pending interrupt');

// A software write to SPCR must observe input SS even when the host snapshot
// has not changed since the preceding instruction.
cpu.writeData(0x24,0);cpu.writeData(0x4c,0);
const lowSS=inputs.map(input=>input.pin==='PB2'?{pin:'PB2',voltage:0}:input);
device.advanceTo(51e-6,lowSS);cpu.writeData(0x4c,0x50);
assert.equal(cpu.data[0x4c]&0x10,0,'LOW input SS clears MSTR');
assert.equal(cpu.data[0x4d]&0x80,0x80);

cpu.writeData(0xbc,4); // TWI enabled without pending interrupt at reset.
assert.equal(cpu.data[0xbc]&0x80,0);
cpu.writeData(0xb8,72);cpu.writeData(0xbc,0xa4);
device.advanceTo(80e-6,inputs);
assert.equal(cpu.data[0xb9]&0xf8,8,'START status');
assert.equal(cpu.data[0xbc]&0x80,0x80,'TWINT set on completion');
cpu.writeData(0xbb,0xa0);cpu.writeData(0xbc,0x84);
assert.equal(cpu.data[0xbc]&0x80,0,'TWINT write-one-to-clear');
cpu.writeData(0xbb,0x55);
assert.equal(cpu.data[0xbc]&8,8,'TWWC for writes during a transfer');
device.advanceTo(200e-6,inputs);
assert.equal(cpu.data[0xb9]&0xf8,0x20,'No electrical peer -> SLA+W NACK');
cpu.writeData(0xbc,0x94);device.advanceTo(220e-6,inputs);
assert.equal(cpu.data[0xbc]&0x10,0,'STOP bit clears after bus release');
cpu.writeData(0xbc,0xa4);device.advanceTo(240e-6,inputs);
cpu.writeData(0xbb,0xff);cpu.writeData(0xbc,0x84);
device.advanceTo(260e-6,inputs.map(input=>input.pin==='PC4'?{pin:'PC4',voltage:0}:input));
assert.equal(cpu.data[0xb9]&0xf8,0x38,'Electrical arbitration loss must reach TWSR');

const registry=context.CircuitAliveAVR;
registry.registerDevice('test_x8',{family:'atmega-x8',flash:4096,sram:512,eeprom:256,vectorDivisor:2});
assert.ok(Object.isFrozen(registry.device('test_x8')));
assert.ok(context.createDevice({...config,device:'test_x8'}));
assert.throws(()=>registry.registerDevice('test_x8',{}),/duplicate/);
assert.throws(()=>registry.registerDevice('other_layout',{family:'stm32'}),/peripheral layout/);
assert.throws(()=>context.createDevice({...config,device:'not_implemented'}),/Unsupported AVR device/);
for(const name of registry.devices()) {
  const other=context.createDevice({...config,device:name});other.advanceTo(1e-6,inputs);
  const core=other.inspect(),divisor=registry.device(name).vectorDivisor;
  core.writeData(0x24,0x2c);core.writeData(0x4c,0xd2);core.writeData(0x4e,0x55);
  other.advanceTo(50e-6,inputs);
  assert.equal(core.nextInterrupt,0x22/divisor,`${name}: SPI interrupt vector`);
  core.readData(0x4d);core.readData(0x4e);
  core.writeData(0xbc,0xa5);other.advanceTo(80e-6,inputs);
  assert.equal(core.nextInterrupt,0x30/divisor,`${name}: TWI interrupt vector`);
}
console.log('AVR serial register flags, collisions, input SS, interrupts, NACK/STOP and compatible device registration passed.');
