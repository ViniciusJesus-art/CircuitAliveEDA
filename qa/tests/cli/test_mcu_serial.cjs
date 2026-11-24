/* Developer electrical-bus regression checks; no Node dependency at runtime. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const root=path.resolve(__dirname,'../../..'),context={};
vm.runInNewContext(fs.readFileSync(path.join(root,'simulation/runtime/mcu-serial.js'),'utf8'),context);
const {SPI,I2CController,I2CTarget}=context.CircuitAliveSerial;

for(const cpol of [false,true])for(const cpha of [false,true])for(const lsbFirst of [false,true]) {
  const wires={ss:false,sck:cpol,mosi:false,miso:false},received=[];
  const pins={ss:'ss',sck:'sck',mosi:'mosi',miso:'miso'};
  const env=id=>({read:pin=>wires[pin],drive:(pin,value)=>{wires[pin]=value===2;},
    complete:value=>received.push([id,value])});
  const controller=new SPI(env('controller'),pins),target=new SPI(env('target'),pins);
  controller.configure({master:true,cpol,cpha,lsbFirst,halfPeriod:1},0);
  target.configure({master:false,cpol,cpha,lsbFirst},0);
  target.tx=0xa6;target.sample(0);controller.start(0x39,0);
  for(let time=0;time<=17;time+=0.125){controller.advance(time);target.sample(time);}
  assert.deepEqual(received.sort(),[['controller',0xa6],['target',0x39]],`SPI mode ${+cpol}/${+cpha}/${+lsbFirst}`);
  assert.equal(target.busy,false,'Last trailing edge must allow preloading the next byte');
  received.length=0;target.start(0x5a,17);controller.start(0xc3,17);
  for(let time=17;time<=34;time+=0.125){controller.advance(time);target.sample(time);}
  assert.deepEqual(received.sort(),[['controller',0x5a],['target',0xc3]],'Consecutive bytes with SS LOW');
  wires.ss=true;target.sample(35);
  assert.equal(target.selected,false);
}

// A wired-AND bus. Every participant reads actual shared line levels; a
// released pin is HIGH only when a pull-up is present and nobody pulls LOW.
function i2cBus(pullups=true,options={}) {
  const drives=[{},{}],events=[],statuses=[];
  let now=0,stretchUntil=0,byteToSend=0x96;
  const read=pin=>pullups&&drives.every(d=>d[pin]!==1);
  const env=id=>({read,drive:(pin,value,time)=>{
    assert.notEqual(value,2,'I2C must never drive HIGH');
    drives[id][pin]=value;events.push({id,pin,value,time});
  }});
  const controller=new I2CController(env(0),{sda:'sda',scl:'scl'});
  const target=new I2CTarget({...env(1),complete:(status,byte)=>{
    statuses.push({status,byte});stretchUntil=now+3;
  }},{sda:'sda',scl:'scl'});
  const config=()=>({address:0x42,mask:0,generalCall:true,ack:true,byte:byteToSend,...options});
  target.resume(config(),0);
  function tick(duration=0.125) {
    now+=duration;
    if(target.pending&&now>=stretchUntil)target.resume(config(),now);
    controller.advance(now);target.sample(now);
  }
  function run(start) {
    let result;
    start((...value)=>{result=value;});
    for(let n=0;n<20000&&result===undefined;n++)tick();
    assert.ok(result,'I2C operation did not finish');
    return result;
  }
  return {controller,target,drives,statuses,events,read,tick,run,
    time:()=>now,setByte:value=>{byteToSend=value;}};
}
const bus=i2cBus();
bus.run(done=>bus.controller.start(false,1,bus.time(),done));
assert.equal(bus.run(done=>bus.controller.transfer(0x84,false,false,1,bus.time(),done))[1],true);
assert.equal(bus.run(done=>bus.controller.transfer(0x53,false,false,1,bus.time(),done))[1],true);
assert.ok(bus.statuses.some(s=>s.status===0x60));
assert.ok(bus.statuses.some(s=>s.status===0x80&&s.byte===0x53));
bus.run(done=>bus.controller.start(true,1,bus.time(),done));
assert.equal(bus.run(done=>bus.controller.transfer(0x85,false,false,1,bus.time(),done))[1],true);
assert.equal(bus.run(done=>bus.controller.transfer(0xff,true,true,1,bus.time(),done))[1],0x96);
bus.setByte(0x3c);
assert.equal(bus.run(done=>bus.controller.transfer(0xff,true,false,1,bus.time(),done))[1],0x3c);
bus.run(done=>bus.controller.stop(1,bus.time(),done));
assert.ok(bus.statuses.some(s=>s.status===0xa8));
assert.ok(bus.statuses.some(s=>s.status===0xb8));
assert.ok(bus.statuses.some(s=>s.status===0xc0));
assert.equal(bus.read('sda'),true);assert.equal(bus.read('scl'),true);

const missing=i2cBus();
missing.run(done=>missing.controller.start(false,1,missing.time(),done));
assert.equal(missing.run(done=>missing.controller.transfer(0xa0,false,false,1,missing.time(),done))[1],false);
missing.run(done=>missing.controller.stop(1,missing.time(),done));
const floating=i2cBus(false);let finished=false;
floating.controller.start(false,1,0,()=>{finished=true;});
for(let n=0;n<100;n++)floating.tick();
assert.equal(finished,false,'Missing pull-ups must prevent a successful START');
assert.equal(floating.controller.busy,true);
let arbitration,heldLow=false;
const competing=new I2CController({read:pin=>pin!=='sda'||!heldLow,drive:()=>{}},{sda:'sda',scl:'scl'});
competing.start(false,1,0,()=>{});competing.advance(10);heldLow=true;
competing.transfer(0xff,false,false,1,10,event=>{arbitration=event;});competing.advance(100);
assert.equal(arbitration,'arbitration','Released SDA observed LOW must report arbitration loss');
assert.equal(competing.owned,false);
const general=i2cBus();
general.run(done=>general.controller.start(false,1,general.time(),done));
assert.equal(general.run(done=>general.controller.transfer(0,false,false,1,general.time(),done))[1],true);
assert.ok(general.statuses.some(s=>s.status===0x70));
general.run(done=>general.controller.transfer(0x27,false,false,1,general.time(),done));
assert.ok(general.statuses.some(s=>s.status===0x90&&s.byte===0x27));
const masked=i2cBus(true,{mask:1});
masked.run(done=>masked.controller.start(false,1,masked.time(),done));
assert.equal(masked.run(done=>masked.controller.transfer(0x86,false,false,1,masked.time(),done))[1],true);
const nackOptions={ack:true},nack=i2cBus(true,nackOptions);
nack.run(done=>nack.controller.start(false,1,nack.time(),done));
nack.run(done=>nack.controller.transfer(0x84,false,false,1,nack.time(),done));
nackOptions.ack=false;
assert.equal(nack.run(done=>nack.controller.transfer(0x12,false,false,1,nack.time(),done))[1],false);
assert.ok(nack.statuses.some(s=>s.status===0x88&&s.byte===0x12));
// Two controllers with different clock periods compete for a shared target.
const shared=[{},{},{}],results=[];
const sharedRead=pin=>shared.every(drive=>drive[pin]!==1);
const sharedEnv=id=>({read:sharedRead,drive:(pin,value)=>{shared[id][pin]=value;}});
const left=new I2CController(sharedEnv(0),{sda:'sda',scl:'scl'}),
  right=new I2CController(sharedEnv(1),{sda:'sda',scl:'scl'});
const peer=new I2CTarget({...sharedEnv(2),complete:()=>{}},{sda:'sda',scl:'scl'});
const peerConfig={address:0x42,mask:0,generalCall:false,ack:true,byte:0};
peer.resume(peerConfig,0);
let started=0,transferred=false;
left.start(false,1,0,()=>{started++;});right.start(false,1.5,0,()=>{started++;});
for(let time=0;time<200;time+=.0625) {
  left.sample(time);right.sample(time);left.advance(time);right.advance(time);peer.sample(time);
  if(peer.pending)peer.resume(peerConfig,time);
  if(started===2&&!transferred) {
    transferred=true;
    left.transfer(0x84,false,false,1,time,(event,value)=>results.push(['left',event,value]));
    right.transfer(0x86,false,false,1.5,time,(event,value)=>results.push(['right',event,value]));
  }
}
assert.ok(results.some(r=>r[0]==='right'&&r[1]==='arbitration'),'Higher address loses arbitration');
assert.ok(results.some(r=>r[0]==='left'&&r[1]==='byte'&&r[2]===true),'Winning controller receives the target ACK');
console.log('SPI: all four modes, both bit orders, full duplex and deselection. I2C: address, ACK/NACK, write, read, repeated START, STOP, stretching and missing pull-ups.');
