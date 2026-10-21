/* Developer differential check. Installed simulation does not require Node.js. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '../../..');
const original = fs.readFileSync(path.join(root, 'thirdparty/avr8js/cjs/cpu/instruction.js'), 'utf8');
const marker = '    cpu.pc = (cpu.pc + 1) % cpu.progMem.length;';
assert.equal(original.split(marker).length, 2);
const referenceModule = {exports: {}};
vm.runInNewContext(original.replace(marker,
    "    else { throw new Error('Unsupported AVR opcode'); }\n" + marker),
    {exports: referenceModule.exports});
const bundled = {};
vm.runInNewContext(fs.readFileSync(path.join(root, 'simulation/runtime/avr8js.js'), 'utf8'), bundled);
const {CPU, avrInstruction} = bundled.AVR8js;
const program = new Uint16Array(16384);
const reference = new CPU(program, 2048), indexed = new CPU(program, 2048);
let valid = 0, rejected = 0;
for (const seed of [0x00, 0xa5]) {
    for (let opcode = 0; opcode < 65536; ++opcode) {
        program[0] = opcode; program[1] = 0x0100;
        for (const cpu of [reference, indexed]) {
            cpu.reset(); cpu.pc = 0; cpu.cycles = 123;
            for (let i = 0; i < cpu.data.length; ++i) cpu.data[i] = (i * 17 + seed) & 255;
            cpu.data[95] = seed; cpu.SP = 0x600;
            for (const reg of [26, 28, 30]) cpu.dataView.setUint16(reg, 0x100, true);
        }
        let expectedError, actualError;
        try { referenceModule.exports.avrInstruction(reference); } catch (error) { expectedError = error; }
        try { avrInstruction(indexed); } catch (error) { actualError = error; }
        const context = `opcode 0x${opcode.toString(16)}, seed 0x${seed.toString(16)}`;
        assert.equal(!!actualError, !!expectedError, context);
        if (expectedError) {
            assert.match(actualError.message, /Unsupported AVR opcode/, context); ++rejected;
        } else {
            assert.equal(indexed.pc, reference.pc, context);
            assert.equal(indexed.cycles, reference.cycles, context);
            assert.deepEqual(indexed.data, reference.data, context); ++valid;
        }
    }
}
assert.ok(valid > 0 && rejected > 0);
console.log(`All 65536 opcodes match upstream for two register/flag states: ${valid} valid, ${rejected} rejected.`);
