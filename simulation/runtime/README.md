# Programmable runtime protocol, version 1

The worker embeds QuickJS-NG. It evaluates the package's frozen JavaScript source
in a separate context for every component. The host protocol is independent of
CPU architecture; another interpreter, FPGA logic model or CPU emulator can
implement the same factory. Merely declaring a different device ID does not
implement that device's instruction set or peripherals.

```javascript
globalThis.createDevice = function(config) {
  // config: id, device, clockHz, firmware byte array, eeprom byte array,
  // options object, pins array, quantumSeconds.
  return {
    reset: function() {},
    nextEventTime: function() { return /* absolute simulated seconds */; },
    advanceTo: function(deadline, inputs) {
      // inputs: [{pin: "IO0", voltage: 3.3}, ...]
      // They apply at the previously confirmed deadline and remain constant
      // until this call's deadline. Arrays and input records are immutable and
      // may be reused when unchanged. Return all transitions chronologically.
      return [{pin: "IO1", drive: 2, time: deadline}];
    }
  };
};
```

Drive values are `0` = high impedance, `1` = drive LOW, `2` = drive HIGH,
`3` = weak pull-up. The electrical bridge determines voltages and resistances;
the firmware backend does not force an input pin to zero volts. Every returned
pin must be in the descriptor's `pins` array. Timestamps must be finite,
monotonic and inside the confirmed window. `nextEventTime` must be strictly
after the confirmed time; infinity is allowed for an inactive backend. The host
also limits the next boundary to `quantumSeconds` so inputs are sampled regularly.

An optional `prepareWindow(deadline, inputs)` returns `{time, events, needsInputs}` and may
yield before the requested deadline to observe an electrical input. The host
uses this only for one component with fixed power/reset, in windows of at most
1 ms. Ordinary backends retain their configured input quantum. The AVR backend
yields before GPIO reads and input-dependent configuration; enabled external
interrupts, ADC, RX and external clocks retain the input quantum. Output edges
always keep instruction-cycle timestamps and electrical breakpoints.
When `needsInputs` is explicitly false, the host may compute one further window
on a dedicated thread while the analog solver processes the returned events.
The backend must yield before observing a new input, including an observation
exactly at a deadline. Omitted `needsInputs` conservatively disables this overlap.
At most one window is queued, and it never passes the current editor batch;
Pause, Stop and interactive changes therefore wait for a confirmed circuit time.

Only synchronous methods are supported. There are no operating-system modules,
filesystem/network callbacks, timers, wall-clock, random source, shared memory
or native imports. Script execution has deterministic interrupt-count limits,
64 MiB per-device memory limits and a 512 KiB stack limit. The maximums are 32
instances, 1024 pins per instance, 4 MiB source per instance and 100000 pin
transitions per call. Exceptions fail the simulation with the device ID; they
are never silently ignored. Reset restores the original frozen images.

## Included AVR backend

`avr8js.js` bundles upstream AVR8js 0.21.1 and the editable
`avr8js-adapter.js`. Run `python3 simulation/runtime/build-avr8js.py` after editing
the adapter or updating the vendored dependency. Python is needed for development
only; the installed application uses the resulting JavaScript directly.
The generator resolves opcode masks into a lookup table, retaining upstream
instruction bodies and their priority. `node qa/tests/cli/test_avr_decoder.cjs`
compares all 65536 encodings against upstream for two register/flag states;
`node qa/tests/cli/test_avr_timing.cjs` also compares CPU state and GPIO events
across sampling boundaries, clock-prescaler changes and power cycles.
Node.js is only used for these developer checks.
The generator also emits portable C++ instruction bodies for the bundled native
accelerator. It stops before peripheral hooks and clock/interrupt boundaries;
the JavaScript instruction path remains the reference and fallback. Timer events
are scheduled at compares, overflow and PWM turns instead of every counter tick.
`qa_interactive_sim` differentially checks native instructions and timer output
timestamps against the vendored reference. No extra installed dependency is used.

Supported exact targets are `atmega48p`, `atmega88p`, `atmega168p`, `atmega328p`.
The adapter selects each device's Flash, SRAM, EEPROM and interrupt-vector
spacing. Package/board pin mappings live in `.casim` descriptors. CPU instructions,
GPIO direction, pull-ups, pin interrupts, timers 0/1/2, timer PWM, ADC conversions,
EEPROM and asynchronous UART TX/RX are emulated through registers and firmware.
UART electrical framing supports 5–8 data bits and parity/stop configuration;
RX is sampled through the electrical window, so choose a quantum comfortably
shorter than the bit period. SPI, TWI/I2C, watchdog, sleep modes, self-programming
and 9-bit UART are explicitly rejected when requested. ADC auto-triggering and
analog comparator are not currently part of the validated capabilities.

Pin IDs are `PB0`–`PB7`, `PC0`–`PC6`, `PD0`–`PD7`, `ADC6`, `ADC7`, `VCC`,
`AVCC`, `GND`, `AREF`, `RESET`. PC6 remains reserved for reset (no fuse override).
The configured clock is an ideal oscillator; crystal startup, fuses, bootloader
selection, oscillator-pin multiplexing, brown-out hysteresis, supply current,
protection diodes and thermal/electrical damage are outside this initial backend.
EEPROM survives electrical reset but returns to the initial image on session reset.

The ADC uses `AVCC` (falling back to `VCC`), external `AREF` and the upstream
internal references; channel inputs are voltages relative to `GND`. GPIO inputs
use 0.3/0.6 of VCC with hysteresis. `options.supplyMin` and `supplyMax` set the
allowed supply window (defaults 1.8–5.5 V). Invalid power or active-low reset
releases all GPIOs and stops CPU execution. Restoring power/reset restarts the CPU.

The host holds inputs throughout each configured electrical window (default
1 µs), and outputs preserve firmware instruction/timer timestamps. AVR instructions
are atomic; a multi-cycle instruction can straddle a window by up to its cycle
duration (normally up to four cycles; EEPROM instructions can stall longer).
Output events beyond a deadline are retained until the following window. This
is a sampled co-simulation with finite input latency. Choose a shorter sampling interval
for fast external edges; GUI update frequency never sets CPU timing.
The editor targets wall-clock time at 1× and schedules against an absolute clock,
including computation and publication in each frame's budget. Supported immutable
behavioral sources use a compiled expression graph and analytic derivatives;
unsupported, mutable and branch-referenced sources retain ngspice evaluation.
The adaptive integrator still enforces its electrical error tolerances and pin
transitions retain their own breakpoints. Throughput depends on the clock,
circuit and host computer; the displayed actual speed exposes any shortfall.
The editor processes Pause/Stop between bounded batches.

## Included PIC16 backend

`pic16.js` independently interprets the PIC16F84A's 14-bit instructions, banked
registers, indirect addressing, eight-level return stack and interrupt entry.
It provides GPIO/TRIS, PORTB pull-ups, RA4 open-drain, Timer0 with prescaler,
RB0 external interrupt, RB4–RB7 change interrupts and EEPROM reads. ClockHz is
oscillator frequency; an instruction cycle takes four clocks. Reset/power loss
releases pins and restarting preserves the initialized EEPROM.

Firmware is little-endian pairs of bytes per program word. HEX regions include
program Flash (1024 words), ID words at 2000h–2003h, configuration at 2007h and
EEPROM at 2100h–213Fh, using twice those addresses as Intel HEX byte addresses.
Other non-erased regions are rejected. WDT must be disabled if a configuration
word is supplied; absent configuration defaults to disabled. SLEEP and EEPROM
write/unlock timing are explicitly unsupported. Supply defaults to 4–5.5 V.
Oscillator modes, power-up timing, protection fuses, supply consumption and pin
protection are outside this backend. This does not implement other PIC families.

PIC PORT read-modify-write instructions sample the physical pin, including
outputs. Electrical feedback follows the configured sampling window, so software
must allow the pad voltage to propagate before reading it, as with actual loading.

## Distribution

CMake links the interpreter statically into `circuitalive-sim-worker` on Linux and
Windows. The installed `share/circuitalive/runtime` directory supplies all JS
modules and documentation; models refer to it by relative paths. No Node.js,
Python, compiler, package manager or external emulator is needed by the runtime.
Release packaging checks these files and licenses, then runs AVR and PIC firmware
against the relocated worker. Developer scripts and release validation may use
Python, which is separate from a user's simulation requirements.
