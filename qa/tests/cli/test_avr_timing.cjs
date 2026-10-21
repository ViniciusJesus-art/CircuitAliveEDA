/* Developer differential check. Installed simulation does not require Node.js. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '../../..');
const source = fs.readFileSync(path.join(root, 'simulation/runtime/avr8js.js'), 'utf8');

// Retain the original adapter's per-instruction time comparison and peripheral
// calls as an independent reference for the optimized execution loop.
let referenceSource = source;
for (const [optimized, original] of [
    ['while(cpu.cycles<limit-rounding || at()<time)', 'while(at()<time)'],
    ['if(cpu.nextInterrupt>=0 || (cpu.nextClockEvent && cpu.nextClockEvent.cycles<=cpu.cycles))cpu.tick();',
     'cpu.tick();'],
    ['if(txQueue.length || rx || (cpu.data[0x55]&0x10)!==previousPud)peripherals();',
     'peripherals();'],
]) {
    assert.equal(referenceSource.split(optimized).length, 2, 'Revalidate the adapter comparison');
    referenceSource = referenceSource.replace(optimized, original);
}

const supply = [{pin: 'VCC', voltage: 5}, {pin: 'GND', voltage: 0}, {pin: 'RESET', voltage: 5}];
const unpowered = [{pin: 'VCC', voltage: 0}, {pin: 'GND', voltage: 0}, {pin: 'RESET', voltage: 5}];
function device(text, firmware) {
    const context = {};
    const marker = '    return {\n      reset:function(){';
    assert.equal(text.split(marker).length, 2);
    // Test-only observation and state injection avoid executing hours of idle
    // firmware to exercise rounding after an electrical reset at a large time.
    text = text.replace(marker, `    return {
      inspect:()=>({pc:cpu.pc,cycles:cpu.cycles,data:cpu.data,
                    delta:clock.cyclesDelta,prescaler:clock.prescaler}),
      poke:(cycles,delta,base)=>{
        cpu.cycles=cycles;clock.cyclesDelta=delta;origin=base;confirmed=at();return confirmed;
      },
      reset:function(){`);
    vm.runInNewContext(text, context);
    return context.createDevice({device: 'atmega328p', firmware, eeprom: [],
        pins: ['PB5', 'VCC', 'GND', 'RESET'], clockHz: 16e6, quantumSeconds: 1e-6});
}

let comparisons = 0;
function compare(reference, optimized, deadline, inputs, message) {
    assert.equal(JSON.stringify(optimized.advanceTo(deadline, inputs)),
                 JSON.stringify(reference.advanceTo(deadline, inputs)), message);
    const actual = optimized.inspect(), expected = reference.inspect();
    for (const key of ['pc', 'cycles', 'delta', 'prescaler'])
        assert.equal(actual[key], expected[key], `${message}: ${key}`);
    assert.deepEqual(Buffer.from(actual.data), Buffer.from(expected.data), `${message}: SRAM/registers`);
    ++comparisons;
}

const ldi = (reg, value) => 0xe000 | ((value & 0xf0) << 4) | ((reg - 16) << 4) | (value & 15);
const out = (io, reg) => 0xb800 | ((io & 0x30) << 5) | (reg << 4) | (io & 15);
const rjmp = displacement => 0xc000 | (displacement & 0xfff);
const words = [ldi(16, 0x20), out(4, 16), 0x9a2d,
    ldi(16, 0x80), 0x9300, 0x0061, ldi(16, 1), 0x9300, 0x0061,
    ...new Array(20).fill(0), 0x982d,
    ldi(16, 0x80), 0x9300, 0x0061, ldi(16, 0), 0x9300, 0x0061, 0];
words[words.length - 1] = rjmp(2 - words.length);
const firmware = words.flatMap(word => [word & 255, word >> 8]);
for (const window of [10e-9, 100e-9, 1e-6, 1.234567e-6, 10e-6]) {
    const reference = device(referenceSource, firmware), optimized = device(source, firmware);
    for (let sample = 1; sample <= 10000; ++sample)
        compare(reference, optimized, sample * window,
                sample > 4000 && sample < 4500 ? unpowered : supply,
                `window ${window}, sample ${sample}`);
}

const reference = device(referenceSource, [0xff, 0xcf]); // RJMP to itself.
const optimized = device(source, [0xff, 0xcf]);
compare(reference, optimized, 1e-6, supply, 'idle setup');
for (const origin of [0, 1, 30, 60, 89, 90, 300, 500, 1000, 1800, 3599, 3600, 1e9]) {
    for (let sample = 0; sample < 10000; ++sample) {
        const cycles = sample * 2, deadline = origin + (cycles + 2) / 16e6;
        reference.poke(cycles, 0, origin); optimized.poke(cycles, 0, origin);
        compare(reference, optimized, deadline, [], `reset origin ${origin}, sample ${sample}`);
    }
}
for (let sample = 0; sample < 10000; ++sample) {
    const cycles = 1e9 + sample, delta = cycles * 255;
    const deadline = reference.poke(cycles, delta, 0);
    optimized.poke(cycles, delta, 0);
    compare(reference, optimized, deadline, [], `long clock /256 to /1, sample ${sample}`);
}
console.log(`${comparisons} timing boundaries match the original adapter, including prescaler changes and power cycles.`);
