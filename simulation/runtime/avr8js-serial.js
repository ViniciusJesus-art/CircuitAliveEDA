/* AVR register adapters for reusable electrical serial engines.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
(function(global) {
  'use strict';
  global.createAVRSerial=function(cpu,clock,cf,io) {
    const AVR=global.AVR8js,Serial=global.CircuitAliveSerial;
    const drive=Object.create(null);
    const set=(pin,value,time)=>{drive[pin]=value;io.publish(time);};
    const read=pin=>io.read(pin);
    const spiConfig=cf(AVR.spiConfig),twiConfig=cf(AVR.twiConfig);
    const spiInterrupt={address:spiConfig.spiInterrupt,flagRegister:spiConfig.SPSR,flagMask:0x80,
      enableRegister:spiConfig.SPCR,enableMask:0x80};
    const twiInterrupt={address:twiConfig.twiInterrupt,flagRegister:twiConfig.TWCR,flagMask:0x80,
      enableRegister:twiConfig.TWCR,enableMask:1,constant:true};
    const {SPCR,SPSR,SPDR}=spiConfig,{TWBR,TWSR,TWAR,TWDR,TWCR,TWAMR}=twiConfig;
    let spiEnabled=false,twiEnabled=false,spiFlagsRead=0,spiLatch=0;
    let spiPaused=null,twiPaused=null;
    let lostStart=-1;
    const running=domain=>!io.running||io.running(domain);
    const spi=new Serial.SPI({read,drive:set,complete:value=>{
      cpu.data[SPDR]=value;cpu.setInterruptFlag(spiInterrupt);
    }},{ss:'PB2',mosi:'PB3',miso:'PB4',sck:'PB5'});
    const twi=new Serial.I2CController({read,drive:set},{sda:'PC4',scl:'PC5'});
    const target=new Serial.I2CTarget({read,drive:(pin,value,time)=>{
      if(!twi.busy&&!twi.owned)set(pin,value,time);
    },complete:(value,byte)=>{
      if(twi.busy||twi.owned)return;
      if(lostStart===target.startCount)value=({96:0x68,112:0x78,168:0xb0})[value]??value;
      if(byte!==undefined)cpu.data[TWDR]=byte;
      status(value);
    }},{sda:'PC4',scl:'PC5'});
    const targetConfig=()=>({address:cpu.data[TWAR]>>1,mask:cpu.data[TWAMR]>>1,
      generalCall:!!(cpu.data[TWAR]&1),ack:!!(cpu.data[TWCR]&0x40),byte:cpu.data[TWDR]});
    function spiSetup(value) {
      spiEnabled=!!(value&0x40);
      for(const pin of ['PB3','PB4','PB5'])delete drive[pin];
      spi.reset();
      if(spiEnabled) {
        const base=(cpu.data[SPSR]&1)?2:4,rate=[1,4,16,32][value&3];
        spi.configure({master:!!(value&0x10),cpol:!!(value&8),cpha:!!(value&4),
          lsbFirst:!!(value&0x20),halfPeriod:base*rate/(2*clock.frequency)},io.now());
        spi.tx=spiLatch;
      }
      io.publish(io.now());
    }
    function clearSPIFlags() {
      if(spiFlagsRead&0x80)cpu.clearInterrupt(spiInterrupt);
      cpu.data[SPSR]&=~spiFlagsRead;spiFlagsRead=0;
    }
    cpu.writeHooks[SPCR]=value=>{
      const old=cpu.data[SPCR];cpu.data[SPCR]=value;
      cpu.updateInterruptEnable(spiInterrupt,value);
      if((old^value)&0x7f)spiSetup(value);
      if(spiEnabled)sample(io.now());
      return true;
    };
    cpu.writeHooks[SPSR]=value=>{cpu.data[SPSR]=(cpu.data[SPSR]&0xc0)|(value&1);return true;};
    cpu.readHooks[SPSR]=()=>{spiFlagsRead=cpu.data[SPSR]&0xc0;return cpu.data[SPSR];};
    cpu.readHooks[SPDR]=()=>{const value=cpu.data[SPDR];clearSPIFlags();return value;};
    cpu.writeHooks[SPDR]=value=>{
      clearSPIFlags();
      if(!spiEnabled){cpu.data[SPDR]=value;return true;}
      if(spi.busy){cpu.data[SPSR]|=0x40;return true;}
      spiLatch=value;
      // Rate changes (including CLKPR) apply when starting the next byte.
      if(spi.config.master) {
        const value=cpu.data[SPCR],base=(cpu.data[SPSR]&1)?2:4;
        spi.config.halfPeriod=base*[1,4,16,32][value&3]/(2*clock.frequency);
      }
      spi.start(value,io.now());return true;
    };

    function status(value) {
      cpu.data[TWSR]=(cpu.data[TWSR]&3)|value;cpu.setInterruptFlag(twiInterrupt);
    }
    cpu.data[TWSR]=0xf8;cpu.data[TWDR]=0xff;
    cpu.writeHooks[TWSR]=value=>{cpu.data[TWSR]=(cpu.data[TWSR]&0xf8)|(value&3);return true;};
    cpu.writeHooks[TWAR]=value=>{cpu.data[TWAR]=value;return true;};
    cpu.writeHooks[TWAMR]=value=>{cpu.data[TWAMR]=value&0xfe;return true;};
    cpu.writeHooks[TWDR]=value=>{
      if(twi.busy || (twiEnabled && !(cpu.data[TWCR]&0x80) && target.selected))cpu.data[TWCR]|=8;
      else {cpu.data[TWDR]=value;cpu.data[TWCR]&=~8;}
      return true;
    };
    cpu.writeHooks[TWCR]=value=>{
      const old=cpu.data[TWCR];
      cpu.data[TWCR]=(value&0x75)|(old&0x88);
      if(value&0x80)cpu.clearInterrupt(twiInterrupt);
      cpu.updateInterruptEnable(twiInterrupt,cpu.data[TWCR]);
      const enabled=!!(value&4);
      if(enabled!==twiEnabled) {
        twiEnabled=enabled;twi.reset();target.reset();
        if(enabled){drive.PC4=0;drive.PC5=0;}
        else {delete drive.PC4;delete drive.PC5;}
        io.publish(io.now());
      }
      if(enabled)target.resume(targetConfig(),io.now(),!!(value&0x80));
      if(!enabled || !(value&0x80) || twi.busy)return true;
      const half=(16+2*cpu.data[TWBR]*[1,4,16,64][cpu.data[TWSR]&3])/(2*clock.frequency);
      const previous=cpu.data[TWSR]&0xf8;
      if(value&0x20) {
        const repeated=twi.owned;
        twi.start(repeated,half,io.now(),()=>{
          cpu.data[TWCR]&=~0x20;status(repeated?0x10:8);
        });
      } else if(value&0x10) {
        const stopped=()=>{cpu.data[TWCR]&=~0x10;cpu.data[TWSR]=(cpu.data[TWSR]&3)|0xf8;};
        if(twi.owned)twi.stop(half,io.now(),stopped);
        else {target.reset();set('PC4',0,io.now());set('PC5',0,io.now());stopped();}
      } else if(twi.owned) {
        const address=previous===8||previous===0x10,reading=!address&&(previous===0x40||previous===0x50);
        const byte=cpu.data[TWDR],ack=!!(value&0x40);
        twi.transfer(byte,reading,ack,half,io.now(),(event,result)=>{
          if(event==='arbitration'){lostStart=target.startCount;status(0x38);return;}
          if(reading){cpu.data[TWDR]=result;status(ack?0x50:0x58);}
          else if(address)status(byte&1?(result?0x40:0x48):(result?0x18:0x20));
          else status(result?0x28:0x30);
        });
      }
      return true;
    };
    function sample(time) {
      if(twiEnabled&&running('twi'))twi.sample(time);
      if(spiEnabled&&running('spi')) {
        if(spi.config.master && !(cpu.data[0x24]&4) && !read('PB2')) {
          cpu.data[SPCR]&=~0x10;spiSetup(cpu.data[SPCR]);cpu.setInterruptFlag(spiInterrupt);
        }
        spi.sample(time);
      }
      if(twiEnabled&&running('twi')) {
        const config=targetConfig();
        target.config={...config,ack:config.ack&&!twi.busy&&!twi.owned};
        target.sample(time);
      }
    }
    function clocksChanged() {
      const time=io.now();
      if(!running('spi')){if(spiPaused===null)spiPaused=time;}
      else if(spiPaused!==null){spi.next+=time-spiPaused;spiPaused=null;}
      if(!running('twi-controller')){if(twiPaused===null)twiPaused=time;}
      else if(twiPaused!==null){twi.next+=time-twiPaused;twiPaused=null;}
    }
    return {
      sample,clocksChanged,frequencyChanged:(oldHz,newHz,time)=>{
        const scale=oldHz/newHz;
        const spiTime=spiPaused??time,twiTime=twiPaused??time;
        if(spi.config&&spi.config.master){spi.config.halfPeriod*=scale;spi.next=spiTime+(spi.next-spiTime)*scale;}
        if(twi.busy){twi.half*=scale;twi.next=twiTime+(twi.next-twiTime)*scale;}
      },
      active:()=>(spiEnabled&&running('spi'))||(twiEnabled&&running('twi')),
      busy:()=>(spi.busy&&running('spi'))||(twi.busy&&running('twi-controller')),
      advance:time=>{if(running('spi'))spi.advance(time);if(running('twi-controller'))twi.advance(time);},
      pin:pin=>{
        if(twiEnabled && (pin==='PC4'||pin==='PC5'))
          return drive[pin] || (!(cpu.data[0x55]&0x10)&&(cpu.data[0x28]&(1<<(pin==='PC4'?4:5)))?3:0);
        if(spiEnabled) {
          const master=spi.config.master;
          if(pin==='PB4') {
            if(!master&&spi.selected&&!read('PB2')&&(cpu.data[0x24]&0x10))return drive[pin]||1;
            return !(cpu.data[0x55]&0x10)&&(cpu.data[0x25]&0x10)&&!(cpu.data[0x24]&0x10)?3:0;
          }
          if(!master&&pin==='PB2')return !(cpu.data[0x55]&0x10)&&(cpu.data[0x25]&4)?3:0;
          if(pin==='PB3'||pin==='PB5') {
            const bit=pin==='PB3'?3:5;
            if(master && (cpu.data[0x24]&(1<<bit)))return drive[pin]===undefined?1:drive[pin];
            if(!master)return !(cpu.data[0x55]&0x10)&&(cpu.data[0x25]&(1<<bit))?3:0;
          }
        }
        return undefined;
      }
    };
  };
})(globalThis);
