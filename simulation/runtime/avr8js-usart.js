/* Electrical AVR USART: async 5-9 bits, synchronous USART and MSPIM.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
(function(global) {
  'use strict';
  global.createAVRUSART=function(cpu,clock,cf,io) {
    const config=cf(global.AVR8js.usart0Config);
    const irq=(address,flag,enable,constant=false)=>({address,flagRegister:0xc0,flagMask:flag,
      enableRegister:0xc1,enableMask:enable,constant});
    const RXC=irq(config.rxCompleteInterrupt,0x80,0x80,true),
      UDRE=irq(config.dataRegisterEmptyInterrupt,0x20,0x20,true),TXC=irq(config.txCompleteInterrupt,0x40,0x40);
    let buffer=null,tx=null,rx=null,fifo=[],txHigh=true,xck=false,previousRX=true,previousClock=false;
    let syncNext=Infinity,syncTx=null,syncRx=null,paused=null,spi=null,txDrain=false;
    let syncStarted=false,configuredMode=0,configuredPolarity=false;
    const mode=()=>cpu.data[0xc2]>>6;
    const bits=()=>mode()===3?8:([5,6,7,8,0,0,0,9][((cpu.data[0xc2]>>1)&3)|(cpu.data[0xc1]&4)]||8);
    const parity=()=>!!(cpu.data[0xc2]&0x20),odd=()=>!!(cpu.data[0xc2]&0x10);
    const stops=()=>cpu.data[0xc2]&8?2:1;
    const divider=()=>1+((cpu.data[0xc5]&15)<<8|cpu.data[0xc4]);
    const bitTime=()=>divider()*(mode()===0?(cpu.data[0xc0]&2?8:16):2)/clock.frequency;
    const enabledTX=()=>!!(cpu.data[0xc1]&8),enabledRX=()=>!!(cpu.data[0xc1]&0x10);
    const master=()=>!!(cpu.data[0x2a]&0x10);
    cpu.data[0xc0]=0x20;cpu.data[0xc2]=6;
    function refreshRX() {
      cpu.clearInterrupt(RXC);
      cpu.data[0xc0]&=~0x1c;cpu.data[0xc1]&=~2;
      if(fifo.length) {
        const head=fifo[0];cpu.data[0xc0]|=head.status;
        cpu.data[0xc1]|=(head.value>>7)&2;cpu.setInterruptFlag(RXC);
      }
    }
    function receive(value,status=0,ninth=false) {
      if(!enabledRX())return;
      if((cpu.data[0xc0]&1)&&ninth&&!(value&0x100))return;
      if(fifo.length===2)fifo[0].status|=8;
      else fifo.push({value,status});
      refreshRX();
    }
    function frame(value) {
      const length=bits(),out=[0];let p=odd()?1:0;
      for(let bit=0;bit<length;bit++){const high=(value>>bit)&1;p^=high;out.push(high);}
      if(parity())out.push(p);
      for(let bit=0;bit<stops();bit++)out.push(1);
      return out;
    }
    function setTX(value,time) {if(txHigh!==value){txHigh=value;io.publish(time);}}
    function completed(time) {
      if(buffer!==null&&(enabledTX()||txDrain))start(time);
      else {txDrain=false;cpu.setInterruptFlag(TXC);io.publish(time);}
    }
    function start(time) {
      if(buffer===null||!(enabledTX()||txDrain)||!io.running('usart'))return;
      const value=buffer;buffer=null;cpu.setInterruptFlag(UDRE);cpu.clearInterrupt(TXC);
      if(mode()===0) {
        const output=frame(value),period=bitTime();
        tx={output,index:1,next:time+period,period};setTX(false,time);
      } else if(mode()===1) {
        syncTx={output:frame(value),index:0};
        if(master()&&syncNext===Infinity)syncNext=time+bitTime()/2;
      } else if(mode()===3) {
        spi=new global.CircuitAliveSerial.SPI({read:pin=>io.read(pin),drive:(pin,value,when)=>{
          if(pin==='PD1')setTX(value===2,when);else {xck=value===2;io.publish(when);}
        },complete:(received,when)=>{
          spi=null;receive(received);completed(when);
        }},{sck:'PD4',mosi:'PD1',miso:'PD0'});
        spi.configure({master:true,cpol:!!(cpu.data[0xc2]&1),cpha:!!(cpu.data[0xc2]&2),
          lsbFirst:!!(cpu.data[0xc2]&4),halfPeriod:bitTime()/2},time);spi.start(value,time);
      }
    }
    function configuring() {
      if(mode()===2)throw new Error('Reserved AVR USART mode');
      if(!enabledRX()){rx=null;syncRx=null;fifo=[];refreshRX();}
      if(mode()!==1)syncNext=Infinity;
      else {
        const polarity=!!(cpu.data[0xc2]&1);
        if(configuredMode!==1||configuredPolarity!==polarity){xck=polarity;syncStarted=false;}
        if(master()&&syncNext===Infinity&&(enabledTX()||enabledRX()))syncNext=io.now()+bitTime()/2;
      }
      configuredMode=mode();configuredPolarity=!!(cpu.data[0xc2]&1);
      if(!tx&&!syncTx&&!spi)start(io.now());io.publish(io.now());
    }
    cpu.writeHooks[0xc0]=value=>{
      if(value&0x40)cpu.clearInterrupt(TXC);
      cpu.data[0xc0]=(cpu.data[0xc0]&0xfc)|(value&3);return true;
    };
    cpu.writeHooks[0xc1]=value=>{
      if(enabledTX()&&!(value&8)&&(tx||syncTx||spi||buffer!==null))txDrain=true;
      cpu.data[0xc1]=(value&0xfd)|(cpu.data[0xc1]&2);
      for(const interrupt of [RXC,UDRE,TXC])cpu.updateInterruptEnable(interrupt,value);
      configuring();return true;
    };
    cpu.writeHooks[0xc2]=value=>{cpu.data[0xc2]=value;configuring();return true;};
    for(const address of [0xc4,0xc5])cpu.writeHooks[address]=value=>{
      cpu.data[address]=address===0xc5?value&15:value;return true;
    };
    cpu.readHooks[0xc6]=()=>{
      const value=fifo.length?fifo.shift().value&255:0;refreshRX();return value;
    };
    cpu.writeHooks[0xc6]=value=>{
      if(!enabledTX())return true;
      if(buffer!==null)return true;
      buffer=value|((cpu.data[0xc1]&1)<<8);cpu.clearInterrupt(UDRE);cpu.clearInterrupt(TXC);
      if(!tx&&!syncTx&&!spi)start(io.now());return true;
    };
    function finishRX(state,level) {
      if((cpu.data[0xc0]&1)&&state.length!==9&&!level)return;
      let status=level?0:0x10;
      if(state.parityError)status|=4;
      receive(state.value,status,state.length===9);
    }
    function sampleBit(state,level) {
      if(state.bit<state.length) {
        if(level)state.value|=1<<state.bit;
        state.sum^=level?1:0;state.bit++;return false;
      }
      if(state.parity) {
        state.parityError=level!==!!state.sum;state.parity=false;return false;
      }
      finishRX(state,level);return true;
    }
    const newRX=()=>({value:0,bit:0,length:bits(),parity:parity(),sum:odd()?1:0});
    function syncEdge(level,time) {
      xck=level;
      const leading=level!==!!(cpu.data[0xc2]&1);
      if(leading) {
        syncStarted=true;
        if(!syncTx)start(time);
        setTX(syncTx?!!syncTx.output[syncTx.index]:true,time);
      } else {
        if(!syncStarted)return;
        if(enabledRX()) {
          const input=!!io.read('PD0');
          if(!syncRx&&!input)syncRx=newRX();
          else if(syncRx&&sampleBit(syncRx,input))syncRx=null;
        }
        if(syncTx&&++syncTx.index===syncTx.output.length) {syncTx=null;completed(time);}
      }
      io.publish(time);
    }
    function sample(time) {
      if(!io.running('usart'))return;
      const input=!!io.read('PD0');
      if(mode()===0&&enabledRX()&&!rx&&previousRX&&!input)
        rx={...newRX(),checkingStart:true,next:time+bitTime()/2,period:bitTime()};
      previousRX=input;
      if(mode()===1&&!master()) {
        const level=!!io.read('PD4');if(level!==previousClock)syncEdge(level,time);previousClock=level;
      }
    }
    function advance(time) {
      if(!io.running('usart'))return;
      while(tx&&tx.next<=time+1e-15) {
        const when=tx.next;
        if(tx.index===tx.output.length){tx=null;completed(when);}
        else {setTX(!!tx.output[tx.index++],when);tx.next+=tx.period;}
      }
      while(rx&&rx.next<=time+1e-15) {
        const level=!!io.read('PD0');
        if(rx.checkingStart) {if(level){rx=null;break;}rx.checkingStart=false;}
        else if(sampleBit(rx,level)){rx=null;break;}
        if(rx)rx.next+=rx.period;
      }
      if(mode()===1&&master()&&(enabledTX()||enabledRX())) {
        if(syncNext===Infinity)syncNext=time+bitTime()/2;
        let edges=0;
        while(syncNext<=time+1e-15) {
          if(++edges>100000)throw new Error('USART synchronous window too large');
          const when=syncNext;syncNext+=bitTime()/2;syncEdge(!xck,when);
        }
      }
      if(spi)spi.advance(time);
    }
    function clocksChanged() {
      if(!io.running('usart')){if(paused===null)paused=io.now();return;}
      if(paused!==null) {
        const delta=io.now()-paused;paused=null;
        if(tx)tx.next+=delta;if(rx)rx.next+=delta;if(spi)spi.next+=delta;
        if(syncNext!==Infinity)syncNext+=delta;
      }
      if(!tx&&!syncTx&&!spi)start(io.now());
    }
    function frequencyChanged(oldHz,newHz,time) {
      const scale=oldHz/newHz;
      time=paused??time;
      if(tx){tx.next=time+(tx.next-time)*scale;tx.period*=scale;}
      if(rx){rx.next=time+(rx.next-time)*scale;rx.period*=scale;}
      if(syncNext!==Infinity)syncNext=time+(syncNext-time)*scale;
      if(spi){spi.next=time+(spi.next-time)*scale;spi.config.halfPeriod*=scale;}
    }
    return {sample,advance,clocksChanged,frequencyChanged,
      busy:()=>!!(tx||rx||spi||syncNext!==Infinity),
      nextTime:()=>!io.running('usart')?Infinity:Math.min(tx?tx.next:Infinity,rx?rx.next:Infinity,spi?spi.next:Infinity,syncNext),
      pin:pin=>pin==='PD1'&&(enabledTX()||tx||syncTx||spi)?(txHigh?2:1):
        pin==='PD4'&&mode()!==0&&master()&&(enabledTX()||enabledRX())?(xck?2:1):undefined,
      get txEnable(){return enabledTX();},get rxEnable(){return enabledRX();},
      get bitsPerChar(){return bits();},get baudRate(){return 1/bitTime();}};
  };
})(globalThis);
