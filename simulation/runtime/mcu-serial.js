/* Electrical serial engines, independent of CPU ISA/register layout.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * env: read(pin), drive(pin, 0=release/1=LOW/2=HIGH, time), complete(value).
 * All times are simulated seconds; reads observe the host's electrical snapshot.
 */
(function(global) {
  'use strict';
  class SPI {
    constructor(env,pins) { this.env=env;this.pins=pins;this.reset(); }
    reset() { this.busy=false;this.selected=false;this.tx=0;this.rx=0;this.bits=0;this.next=Infinity; }
    configure(config,time) {
      this.config=config;this.reset();
      this.clock=!!config.cpol;
      if(config.master)this.env.drive(this.pins.sck,this.clock?2:1,time);
    }
    bit(index) { return (this.tx>>(this.config.lsbFirst?index:7-index))&1; }
    output(index,time,pin) { this.env.drive(pin,this.bit(index)?2:1,time); }
    receive(pin,index) { if(this.env.read(pin))this.rx|=1<<(this.config.lsbFirst?index:7-index); }
    start(value,time) {
      this.tx=value;this.rx=0;this.bits=0;
      if(!this.config.master) {
        if(this.selected&&!this.config.cpha)this.output(0,time,this.pins.miso);
        return;
      }
      this.busy=true;this.edge=0;this.next=time+this.config.halfPeriod;
      if(!this.config.cpha)this.output(0,time,this.pins.mosi);
    }
    advance(time) {
      if(!this.config || !this.config.master)return;
      while(this.busy && this.next<=time+1e-15) {
        const when=this.next,leading=(this.edge%2)===0,index=this.edge>>1;
        this.clock=leading?!this.config.cpol:!!this.config.cpol;
        this.env.drive(this.pins.sck,this.clock?2:1,when);
        if(leading===!this.config.cpha)this.receive(this.pins.miso,index);
        else if(this.config.cpha)this.output(index,when,this.pins.mosi);
        else if(index<7)this.output(index+1,when,this.pins.mosi);
        this.edge++;this.next+=this.config.halfPeriod;
        if(this.edge===16) {
          this.busy=false;this.next=Infinity;this.env.complete(this.rx,when);
        }
      }
    }
    sample(time) {
      if(!this.config || this.config.master)return;
      const selected=!this.env.read(this.pins.ss),clock=!!this.env.read(this.pins.sck);
      if(!selected) {
        this.selected=false;this.busy=false;this.bits=0;this.rx=0;this.clock=clock;
        this.env.drive(this.pins.miso,0,time);return;
      }
      if(!this.selected) {
        this.selected=true;this.clock=clock;this.bits=0;this.rx=0;
        if(!this.config.cpha)this.output(0,time,this.pins.miso);
        return;
      }
      if(clock===this.clock)return;
      this.clock=clock;
      const leading=clock!==!!this.config.cpol;
      if(leading||this.bits)this.busy=true;
      if(leading===!this.config.cpha) {
        this.receive(this.pins.mosi,this.bits++);
        if(this.bits===8) {
          const value=this.rx;this.bits=0;this.rx=0;this.busy=false;
          this.env.complete(value,time);
        }
      } else this.output(this.bits,time,this.pins.miso);
    }
  }

  // Open-drain controller. HIGH is always a release: pull-ups, RC rise times
  // and a peer holding SCL LOW are resolved by the electrical solver.
  class I2CController {
    constructor(env,pins) { this.env=env;this.pins=pins;this.reset(); }
    reset() {
      this.busy=false;this.owned=false;this.steps=[];this.index=0;this.next=Infinity;
      this.lastClock=true;this.highData=true;this.clockFell=false;
      this.lastData=true;this.startFree=false;this.startJoined=false;
    }
    sample(time) {
      const clock=!!this.env.read(this.pins.scl);
      const data=!!this.env.read(this.pins.sda);
      if(this.busy&&this.startFree&&clock&&this.lastData&&!data) {
        const start=this.steps.findIndex(step=>step[0]==='start');
        if(start>=this.index){this.index=start;this.next=time;this.startJoined=true;}
      }
      if(clock)this.highData=!!this.env.read(this.pins.sda);
      const step=this.steps[this.index];
      // Another controller can finish the HIGH phase first. Synchronize to its
      // falling edge using the data observed while SCL was actually HIGH.
      if(this.busy&&this.lastClock&&!clock&&step&&['read','arbitrate','ack','sentAck'].includes(step[0])) {
        this.clockFell=true;this.next=Math.min(this.next,time);
      }
      this.lastClock=clock;
      this.lastData=data;
    }
    line(pin,low,time) { this.env.drive(this.pins[pin],low?1:0,time); }
    run(steps,halfPeriod,time,done) {
      if(this.busy)throw new Error('I2C operation already active');
      this.busy=true;this.steps=steps;this.index=0;this.half=halfPeriod;
      this.next=time;this.done=done;this.received=0;this.ack=false;
    }
    start(repeated,half,time,done) {
      this.startFree=!repeated&&!!this.env.read(this.pins.sda)&&!!this.env.read(this.pins.scl);
      this.startJoined=false;
      this.run([...(repeated?[['scl',true]]:[]),['sda',false],['raise'],['high'],['start'],['scl',true]],half,time,()=>{
        this.owned=true;this.startFree=false;done('start');
      });
    }
    stop(half,time,done) {
      this.run([['scl',true],['sda',true],['raise'],['high'],['sda',false],['idle']],half,time,()=>{
        this.owned=false;done('stop');
      });
    }
    transfer(value,read,ack,half,time,done) {
      const steps=[];
      for(let bit=7;bit>=0;--bit)steps.push(['sda',read?false:!(value&(1<<bit))],
        ['raise'],['high'],[read?'read':'arbitrate',bit,!!(value&(1<<bit))],['scl',true]);
      steps.push(['sda',read&&ack],['raise'],['high'],[read?'sentAck':'ack'],['scl',true],['sda',false]);
      this.run(steps,half,time,event=>event==='arbitration'?done(event):done('byte',read?this.received:this.ack));
    }
    advance(time) {
      while(this.busy && this.next<=time+1e-15) {
        const step=this.steps[this.index],when=this.next,op=step[0];
        if(op==='raise') {
          // The following HIGH barrier waits for the solver to resolve SCL.
          this.line('scl',false,when);
        } else if(op==='sda'||op==='scl')this.line(op,step[1],when);
        else {
          // Clock stretching and pull-up failure hold the operation here.
          const synchronized=this.clockFell&&['read','arbitrate','ack','sentAck'].includes(op);
          if(!this.env.read(this.pins.scl)&&!synchronized&&!(op==='start'&&this.startJoined)) {this.next=time+this.half/8;return;}
          const data=synchronized?this.highData:!!this.env.read(this.pins.sda);
          this.clockFell=false;
          if(op==='start') {
            if(!data&&!this.startJoined) {this.next=time+this.half;return;}
            this.line('sda',true,when);
          } else if(op==='read') {if(data)this.received|=1<<step[1];}
          else if(op==='ack')this.ack=!data;
          else if(op==='arbitrate' && step[2] && !data) {
            this.busy=false;this.owned=false;this.next=Infinity;
            this.line('sda',false,when);this.line('scl',false,when);
            this.done('arbitration');return;
          } else if(op==='idle'&&!data) {this.next=time+this.half;return;}
        }
        this.index++;
        // SDA setup uses the LOW phase; sampling occurs halfway through HIGH.
        // Each ordinary byte bit therefore has two half-periods, not four.
        const delay=op==='sda'||op==='high'||op==='start'?this.half:0;
        this.next=when+delay;
        if(this.index===this.steps.length) {
          const done=this.done;this.busy=false;this.next=Infinity;done();
        }
      }
    }
  }
  class I2CTarget {
    constructor(env,pins) {this.env=env;this.pins=pins;this.reset();}
    reset() {
      this.state='idle';this.bits=0;this.byte=0;this.selected=false;this.pending=false;
      this.lastSDA=true;this.lastSCL=true;this.ackStage=0;this.general=false;
      this.startCount=0;
    }
    line(pin,low,time) {this.env.drive(this.pins[pin],low?1:0,time);}
    resume(config,time,clear=true) {
      this.config=config;
      if(!this.pending||!clear)return;
      this.pending=false;
      if(this.state==='send') {
        this.byte=config.byte;this.bits=0;
        this.line('sda',!(this.byte&0x80),time);
      }
      this.line('scl',false,time);
    }
    notify(status,time,byte,stretch=true) {
      this.pending=true;
      if(stretch)this.line('scl',true,time);
      this.env.complete(status,byte,time);
    }
    sample(time) {
      if(!this.config)return;
      const sda=!!this.env.read(this.pins.sda),scl=!!this.env.read(this.pins.scl);
      const rising=scl&&!this.lastSCL,falling=!scl&&this.lastSCL;
      const start=scl&&this.lastSCL&&this.lastSDA&&!sda;
      const stop=scl&&this.lastSCL&&!this.lastSDA&&sda;
      this.lastSDA=sda;this.lastSCL=scl;
      if(start||stop) {
        if(start)this.startCount++;
        const wasSelected=this.selected;
        const illegal=wasSelected&&(this.bits!==0||this.ackStage!==0);
        this.state=start?'address':'idle';this.bits=0;this.byte=0;this.selected=false;this.ackStage=0;
        this.line('sda',false,time);
        if(wasSelected)this.notify(illegal?0:0xa0,time,undefined,false);
        return;
      }
      if(this.pending) {
        if(falling&&this.state==='address')this.line('scl',true,time);
        return;
      }
      if(this.state==='idle')return;
      if(this.ackStage) {
        if(falling && this.ackStage===1) {
          this.line('sda',this.ack,time);this.ackStage=2;
        } else if(rising && this.ackStage===2)this.ackStage=3;
        else if(falling && this.ackStage===3) {
          this.line('sda',false,time);this.ackStage=0;
          const state=this.state;this.state=this.nextState;this.bits=0;this.byte=0;
          if(this.selected) {
            if(state==='address')this.notify(this.general?0x70:this.reading?0xa8:0x60,time);
            else this.notify(this.general?(this.ack?0x90:0x98):(this.ack?0x80:0x88),time,this.received);
          }
        }
        return;
      }
      if(this.state==='address'||this.state==='receive') {
        if(!rising)return;
        this.byte=(this.byte<<1)|(sda?1:0);
        if(++this.bits!==8)return;
        this.received=this.byte;
        if(this.state==='address') {
          const address=this.byte>>1;
          this.general=address===0&&!(this.byte&1)&&this.config.generalCall;
          this.selected=!!(this.config.ack&&(this.general
            || (address!==0 && ((address^this.config.address)&~this.config.mask)===0)));
          this.reading=!!(this.byte&1);
          this.ack=this.selected;
          this.nextState=this.selected?(this.reading?'send':'receive'):'idle';
        } else {this.ack=!!this.config.ack;this.nextState=this.ack?'receive':'idle';}
        this.ackStage=1;
      } else if(this.state==='send') {
        if(rising) {
          if(this.bits<8)this.bits++;
          else {
            this.ack=!sda;this.state='sent';
          }
        } else if(falling) {
          if(this.bits<8)this.line('sda',!(this.byte&(0x80>>this.bits)),time);
          else this.line('sda',false,time);
        }
      } else if(this.state==='sent'&&falling) {
        this.state=this.ack&&this.config.ack?'send':'idle';
        this.notify(this.ack?(this.config.ack?0xb8:0xc8):0xc0,time);
      }
    }
  }
  global.CircuitAliveSerial=Object.freeze({SPI,I2CController,I2CTarget});
})(globalThis);
