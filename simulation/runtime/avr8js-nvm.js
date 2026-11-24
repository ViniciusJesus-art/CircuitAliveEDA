/* Functional AVR Flash, fuses/locks and EEPROM. SPDX-License-Identifier: GPL-3.0-or-later */
(function(global) {
  'use strict';
  global.createAVRNVM=function(cpu,device,options,cf,io,eeprom) {
    const byteOption=(key,fallback)=>{
      const value=options[key]===undefined?fallback:Number(options[key]);
      if(!Number.isInteger(value)||value<0||value>255)throw new Error('Invalid AVR '+key);
      return value;
    };
    const fuses={low:byteOption('fuseLow',0xe2),high:byteOption('fuseHigh',0xdf),
      extended:byteOption('fuseExtended',device.bootMinBytes?0xff:0xfe)};
    let lock=byteOption('lockBits',255),buffer=new Uint8Array(device.pageBytes);buffer.fill(255);
    const bootFuse=device.fuseLayout==='328'?fuses.high:fuses.extended;
    const bootStart=device.bootMinBytes?device.flash-(device.bootMinBytes<<(3-((bootFuse>>1)&3))):0;
    const nrwwStart=device.bootMinBytes?device.flash-device.bootMinBytes*8:0;
    const ready={address:0x32/device.vectorDivisor,flagRegister:0x57,flagMask:1,
      enableRegister:0x57,enableMask:0x80,inverseFlag:true,constant:true};
    const eer=cf(global.AVR8js.eepromConfig);
    const eeReady={address:eer.eepromReadyInterrupt,flagRegister:0x3f,flagMask:2,
      enableRegister:0x3f,enableMask:8,inverseFlag:true,constant:true};
    let spmUntil=-Infinity,flashDone=Infinity,flashAction=null,flashStall=false;
    let eeUnlock=-Infinity,eeDone=Infinity,eeAction=null;
    const now=io.now;
    function sync() {
      const time=now();
      if(cpu.cycles>spmUntil&&flashDone===Infinity)cpu.data[0x57]&=0xc0;
      if(cpu.cycles>eeUnlock)cpu.data[0x3f]&=~4;
      if(time+1e-15>=eeDone) {
        if(eeAction)eeAction();eeDone=Infinity;eeAction=null;cpu.setInterruptFlag(eeReady);
      }
      if(time+1e-15>=flashDone) {
        if(flashAction)flashAction();flashDone=Infinity;flashAction=null;flashStall=false;
        cpu.data[0x57]&=0xc0;cpu.setInterruptFlag(ready);
      }
    }
    cpu.readHooks[0x57]=()=>{sync();return cpu.data[0x57];};
    cpu.writeHooks[0x57]=value=>{
      sync();
      if(flashDone!==Infinity||eeDone!==Infinity)return true;
      cpu.data[0x57]=(cpu.data[0x57]&0x40)|(value&(device.fuseLayout==='328'?0xbf:0x9f));
      spmUntil=cpu.cycles+4;cpu.updateInterruptEnable(ready,value);return true;
    };
    cpu.readHooks[0x3f]=()=>{sync();return cpu.data[0x3f];};
    cpu.writeHooks[0x3f]=value=>{
      sync();const old=cpu.data[0x3f],address=((cpu.data[0x42]<<8)|cpu.data[0x41])&(device.eeprom-1);
      cpu.data[0x3f]=(old&6)|(value&0x38);cpu.updateInterruptEnable(eeReady,value);
      if(eeDone!==Infinity)return true;
      if(value&1) {cpu.data[0x40]=eeprom.readMemory(address);cpu.cycles+=4;}
      if(value&4) {eeUnlock=cpu.cycles+4;cpu.data[0x3f]|=4;}
      // EEMPE must already have been set by a preceding instruction.
      if((value&2)&&(old&4)&&cpu.cycles<=eeUnlock&&flashDone===Infinity) {
        const mode=(value>>4)&3,data=cpu.data[0x40];
        if(mode===3)return true; // Reserved EEPM combination.
        eeDone=now()+(mode===0?.0034:.0018);
        eeAction=()=>{if(mode!==2)eeprom.eraseMemory(address);if(mode!==1)eeprom.writeMemory(address,data);};
        cpu.data[0x3f]=(cpu.data[0x3f]&~4)|2;cpu.clearInterrupt(eeReady,false);cpu.cycles+=2;
      }
      return true;
    };
    function specialRead(address) {
      const mode=cpu.data[0x57];
      if(!(mode&1)||cpu.cycles>spmUntil)return undefined;
      if(mode&0x20) {
        if(address===1)return byteOption('calibrationByte',128);
        return address%2===0&&address<=4?device.signature[address>>1]:255;
      }
      if(mode&8)return ({0:fuses.low,1:lock,2:fuses.extended,3:fuses.high})[address]??255;
      return undefined;
    }
    function instruction(opcode) {
      sync();
      const z=cpu.dataView.getUint16(30,true);
      if(opcode===0x95e8||opcode===0x95f8) {
        const mode=cpu.data[0x57],authorized=device.bootMinBytes?cpu.pc*2>=bootStart:!(fuses.extended&1);
        if(authorized&&(mode&1)&&cpu.cycles<=spmUntil&&flashDone===Infinity&&eeDone===Infinity) {
          const address=z&(device.flash-1),page=address&~(device.pageBytes-1);
          if(mode&0x10) {cpu.data[0x57]&=~0x40;buffer.fill(255);}
          else if(mode&8)lock&=cpu.data[0]|0xc0;
          else if(mode&6) {
            const allowed=!(device.bootMinBytes&&!(lock&(page>=bootStart?0x10:4)));
            if(allowed) {
              const copy=buffer.slice(),erase=!!(mode&2);
              flashDone=now()+.0045;flashStall=!device.bootMinBytes||page>=nrwwStart;
              if(!flashStall)cpu.data[0x57]|=0x40;
              flashAction=()=>{
                for(let i=0;i<device.pageBytes;i++)cpu.progBytes[page+i]=erase?255:cpu.progBytes[page+i]&copy[i];
                if(!erase)buffer.fill(255);
              };
            }
          } else {
            const offset=address&(device.pageBytes-2);buffer[offset]=cpu.data[0];buffer[offset+1]=cpu.data[1];
            cpu.data[0x57]&=~0x40;
          }
          cpu.data[0x57]&=0xc0;
          if(flashDone!==Infinity)cpu.data[0x57]|=mode&7;
          spmUntil=-Infinity;
        }
        if(opcode===0x95f8)cpu.dataView.setUint16(30,(z+2)&65535,true);
        cpu.pc=(cpu.pc+1)%cpu.progMem.length;cpu.cycles+=4;return true;
      }
      if(opcode===0x95c8||(opcode&0xfe0e)===0x9004) {
        let value=specialRead(z);
        if(value===undefined&&device.bootMinBytes) {
          const fromBoot=cpu.pc*2>=bootStart,toBoot=z>=bootStart;
          if((fromBoot&&!toBoot&&!(lock&8))||(!fromBoot&&toBoot&&!(lock&0x20)))value=0;
        }
        if(value!==undefined) {
          cpu.data[opcode===0x95c8?0:(opcode>>4)&31]=value;
          if(opcode!==0x95c8&&(opcode&1))cpu.dataView.setUint16(30,(z+1)&65535,true);
          cpu.pc=(cpu.pc+1)%cpu.progMem.length;cpu.cycles+=3;return true;
        }
      }
      return false;
    }
    return {fuses,bootStart,resetPC:device.bootMinBytes&&!(bootFuse&1)?bootStart/2:0,
      sync,instruction,nextTime:()=>Math.min(eeDone,flashDone),
      stalled:()=>flashDone!==Infinity&&flashStall||!!((cpu.data[0x57]&0x40)&&cpu.pc*2<nrwwStart),
      nativeSafe:()=>lock===255&&!(cpu.data[0x57]&0x69)&&flashDone===Infinity,
      interruptsAllowed:()=>!device.bootMinBytes||!((!(lock&8)&&cpu.pc*2<bootStart&&(cpu.data[0x55]&2))
        ||(!(lock&0x20)&&cpu.pc*2>=bootStart&&!(cpu.data[0x55]&2))),
      retain:()=>({eeDone,eeAction,flashDone,flashAction,flashStall,rww:cpu.data[0x57]&0x40}),
      restore:state=>{
        ({eeDone,eeAction,flashDone,flashAction,flashStall}=state);
        if(eeDone!==Infinity)cpu.data[0x3f]|=2;
        if(flashDone!==Infinity)cpu.data[0x57]|=1;
        cpu.data[0x57]|=state.rww;
      },
      get lock(){return lock;},set lock(value){lock=value;}};
  };
})(globalThis);
