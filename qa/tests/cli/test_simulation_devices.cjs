/* Protocol regressions for physical simulation peripherals; Node is only a QA dependency. */
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.resolve(__dirname,'../../..'),ctx={};
for(const f of ['mcu-serial.js','simulation-devices.js'])vm.runInNewContext(fs.readFileSync(path.join(root,'simulation/runtime',f),'utf8'),ctx);
const HD=ctx.CircuitAliveHD44780;
// Exercise instructions through enable edges, including power-on delay and busy rejection.
const lcd=new HD();let t=.02;
function write(rs,data,delay=50e-6){lcd.bus(rs,false,true,data,t);lcd.bus(rs,false,false,data,t+1e-6);t+=delay;}
write(false,0x38);write(false,0x0c);write(false,0x80);write(true,65);write(true,66);
assert.deepEqual(Array.from(lcd.ddram.slice(0,2)),[65,66]);
write(false,0xc0);write(true,90);assert.equal(lcd.presentation(true,true,t).cells[16],90);
write(false,1);write(true,88);assert.equal(lcd.ddram[0],32,'busy writes must be ignored');t+=.002;
write(false,0x40);for(const row of [0,10,21,17,10,4,0,0])write(true,row);
write(false,0x80);write(true,0);assert.equal(lcd.presentation(true,true,t).cgram[1],10);
write(false,0x18);assert.equal(lcd.shift,1);write(false,0x1c);assert.equal(lcd.shift,0);
write(false,0x28);assert.equal(lcd.eight,false);
function nibble(rs,value){write(rs,value<<4);}
function byte(rs,value){nibble(rs,value>>4);nibble(rs,value&15);}
byte(false,0x80);byte(true,0x4b);assert.equal(lcd.ddram[0],0x4b);
// Busy/address read uses two nibbles, data reads advance only after the low nibble.
byte(false,0x80);
let high=lcd.bus(true,true,true,0,t);assert.equal(high,0x40);lcd.bus(true,true,false,0,t+1e-6);assert.equal(lcd.address,0);
let low=lcd.bus(true,true,true,0,t+2e-6);assert.equal(low,0xb0);lcd.bus(true,true,false,0,t+3e-6);assert.equal(lcd.address,1);
byte(false,0x04);byte(false,0xc0);byte(true,88);assert.equal(lcd.address,0x27,'DDRAM decrement crosses line boundary');
// Actual PCF8574 runtime, including addressed ACK/NACK, repeated START and readback.
function backpack(address=0x27) {
  let now=0,host={SDA:0,SCL:0},device={SDA:0,SCL:0};
  const pins=['VCC','GND','SDA','SCL'];
  const runtime=ctx.createDevice({device:'hd44780_pcf8574',pins,quantumSeconds:1e-6,options:{supplyMin:2.7,supplyMax:5.5,address}});
  const voltage=pin=>(host[pin]===1||device[pin]===1)?0:5;
  const master=new ctx.CircuitAliveSerial.I2CController({read:p=>voltage(p)>2.5,drive:(p,d)=>host[p]=d},{sda:'SDA',scl:'SCL'});
  function tick() {
    now+=1e-6;master.sample(now);master.advance(now);
    const events=runtime.advanceTo(now,pins.map(pin=>({pin,voltage:pin==='GND'?0:pin==='VCC'?5:voltage(pin)})));
    for(const e of events)device[e.pin]=e.drive;
  }
  function wait(seconds){const end=now+seconds;while(now<end)tick();}
  function run(fn){let result;fn((...v)=>result=v);for(let i=0;i<20000&&!result;i++)tick();assert.ok(result,'I2C operation timed out');return result;}
  const start=repeated=>run(done=>master.start(!!repeated,5e-6,now,done));
  const send=value=>run(done=>master.transfer(value,false,false,5e-6,now,done));
  const receive=()=>run(done=>master.transfer(0,true,false,5e-6,now,done));
  const stop=()=>run(done=>master.stop(5e-6,now,done));
  function port(value){start();assert.equal(send(address*2)[1],true);assert.equal(send(value)[1],true);stop();}
  function nibble(value,rs=false){const p=(value<<4)|8|(rs?1:0);port(p|4);port(p);}
  function byte(value,rs=false){nibble(value>>4,rs);nibble(value&15,rs);}
  wait(.02);return {runtime,wait,start,send,receive,stop,port,nibble,byte};
}
const b=backpack();b.start();assert.equal(b.send(0x40)[1],false,'wrong address NACK');b.stop();
b.nibble(3);b.wait(.005);b.nibble(3);b.nibble(3);b.nibble(2);b.byte(0x28);b.byte(0x0c);b.byte(0x06);b.byte(0x80);b.byte(72,true);b.byte(105,true);
assert.equal(b.runtime.presentation().cells[0],72);assert.equal(b.runtime.presentation().cells[1],105);assert.equal(b.runtime.presentation().display,true);
b.start();assert.equal(b.send(0x4e)[1],true);b.send(0x08);b.start(true);assert.equal(b.send(0x4f)[1],true);assert.equal(b.receive()[1],8);b.stop();
// IR rejects constant illumination and an incorrect carrier, then demodulates bursts.
function infrared(freq) {
 const r=ctx.createDevice({device:'ir_demodulator',pins:['OUT','GND','VCC','IR'],quantumSeconds:1e-6,options:{supplyMin:2.5,supplyMax:5.5,carrierHz:38000}});
 let now=0,out=0,active=false;
 function step(light,supply=3.3){now+=1e-6;for(const e of r.advanceTo(now,[{pin:'VCC',voltage:supply},{pin:'GND',voltage:0},{pin:'IR',voltage:light?.01:0}])){out=e.drive;if(out===1)active=true;}}
 for(let i=0;i<1000;i++)step(freq===Infinity||((i*1e-6*freq)%1<.33));
 const detected=active;for(let i=0;i<200;i++)step(false);assert.equal(out,2,'envelope ends after carrier timeout');step(false,1);assert.equal(out,0,'undervoltage releases output');return detected;
}
assert.equal(infrared(Infinity),false);assert.equal(infrared(20000),false);assert.equal(infrared(38000),true);
console.log('HD44780 parallel 4/8 bit, memory/busy/readback, I2C backpack and IR demodulation passed');
