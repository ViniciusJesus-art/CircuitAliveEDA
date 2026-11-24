/* CircuitAliveEDA AVR device profiles. SPDX-License-Identifier: GPL-3.0-or-later */
(function(global) {
  'use strict';
  const profiles=Object.create(null);
  function registerDevice(id,profile) {
    if(!/^[a-z][a-z0-9_]*$/.test(id) || profiles[id])throw new Error('Invalid or duplicate AVR device: '+id);
    // A profile may reuse this register/peripheral layout, not invent a new ISA.
    if(profile.family!=='atmega-x8')throw new Error('Unsupported AVR peripheral layout: '+profile.family);
    for(const key of ['flash','sram','eeprom'])
      if(!Number.isInteger(profile[key]) || profile[key]<=0 || profile[key]>32768)
        throw new Error('Invalid AVR memory capacity: '+key);
    if(profile.flash%2 || (profile.eeprom&(profile.eeprom-1))
        || ![1,2].includes(profile.vectorDivisor))throw new Error('Invalid AVR device profile');
    const pageBytes=profile.pageBytes || (profile.flash<=8192?64:128);
    const bootMinBytes=profile.bootMinBytes===undefined?(profile.flash===4096?0:profile.flash>=32768?512:256):profile.bootMinBytes;
    if(![64,128].includes(pageBytes)||![0,256,512].includes(bootMinBytes))throw new Error('Invalid AVR Flash layout');
    const signature=profile.signature || [0x1e,profile.flash===4096?0x92:profile.flash===8192?0x93:profile.flash===16384?0x94:0x95,
      profile.flash===4096?0x0a:profile.flash===8192?0x0f:profile.flash===16384?0x0b:0x0f];
    profiles[id]=Object.freeze({family:profile.family,flash:profile.flash,sram:profile.sram,
      eeprom:profile.eeprom,vectorDivisor:profile.vectorDivisor,pageBytes,bootMinBytes,
      signature:Object.freeze([...signature]),fuseLayout:profile.flash>=32768?'328':'48-168'});
  }
  for(const [id,flash,sram,eeprom,vectorDivisor] of [
    ['atmega48p',4096,512,256,2],['atmega88p',8192,1024,512,2],
    ['atmega168p',16384,1024,512,1],['atmega328p',32768,2048,1024,1]])
    registerDevice(id,{family:'atmega-x8',flash,sram,eeprom,vectorDivisor});
  const capabilities=Object.freeze({
    spi:Object.freeze(['controller','target','modes-0-3','msb-first','lsb-first']),
    i2c:Object.freeze(['controller','target','7-bit','address-mask','general-call','clock-stretching',
      'ack-nack','repeated-start','arbitration-loss']),
    system:Object.freeze(['watchdog-reset-interrupt','six-sleep-modes','prr','clkpr','ivsel','reset-causes','bod','clock-output']),
    adc:Object.freeze(['single','free-running','auto-triggers','result-lock','references']),
    timers:Object.freeze(['timer1-capture','noise-canceller','asynchronous-timer2','external-clocks','tsm']),
    usart:Object.freeze(['async-5-9-bit','parity-frame-overrun','mpcm','synchronous','mspim','rx-fifo','tx-buffer']),
    flash:Object.freeze(['page-buffer','erase-write','spm','boot-reset','rww','fuses-lock-bits']),
    notModeled:Object.freeze(['debugwire','external-isp-high-voltage-programmers','transistor-effects',
      'oscillator-startup-jitter','precise-power-temperature-curves'])
  });
  global.CircuitAliveAVR=Object.freeze({registerDevice,capabilities,
    device:id=>profiles[id],devices:()=>Object.keys(profiles)});
})(globalThis);
