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
uses this for components with fixed power/reset, in windows of at most
1 ms. Ordinary backends retain their configured input quantum. The AVR backend
yields before GPIO reads and input-dependent configuration; enabled external
interrupts, ADC, RX and external clocks retain the input quantum. Output edges
always keep instruction-cycle timestamps and electrical breakpoints.
When `needsInputs` is explicitly false, the host may compute one further window
while the analog solver processes the returned events. Independent contexts
can execute concurrently in a persistent CPU-bounded pool. Small sampling jobs
stay sequential; context access is always exclusive.
The backend must yield before observing a new input, including an observation
exactly at a deadline. Omitted `needsInputs` conservatively disables this overlap.
At most one private window per instance is queued. Contexts may reach different
input barriers; only events through the common confirmed time are published.
Future pin states remain private, and a context receives fresh electrical inputs
when the solver reaches its own barrier. Events at equal times are ordered by
device configuration and backend order, independently of worker completion order.
No window passes the current editor batch;
Pause, Stop and interactive changes therefore wait for a confirmed circuit time.
After a live source/parameter change, one normal sampling interval resolves the
new voltages before lookahead or prefetch is allowed again.

The editor can also run compiler-certified independent analog domains in separate
persistent solver processes. Shared grounded ideal DC sources are replicated and
their observed currents are added; coupled components stay together. Every domain
must reach the editor's common barrier before a snapshot is published. This does
not change the JavaScript backend contract. The process IPC protocol is version 4
and requires the application and solver worker to be updated together. See
`docs/circuitalive/SIMULATION_PARALLELISM.md` for decomposition and history semantics.

Only synchronous methods are supported. There are no operating-system modules,
filesystem/network callbacks, timers, wall-clock, random source, shared memory
or native imports. Script execution has deterministic interrupt-count limits,
64 MiB per-device memory limits and a 512 KiB stack limit. The maximums are 32
instances, 1024 pins per instance, 4 MiB source per instance and 100000 pin
transitions per call. Exceptions fail the simulation with the device ID; they
are never silently ignored. Reset restores the original frozen images.

## Included AVR backend

`avr8js.js` bundles upstream AVR8js 0.21.1 and the editable
`avr8js-adapter.js`, `avr8js-devices.js`, `avr8js-serial.js`, `avr8js-usart.js`,
`avr8js-nvm.js`, `avr8js-system.js`, `avr8js-analog.js`, `avr8js-timers.js` and
CPU-independent `mcu-serial.js`. Run `python3 simulation/runtime/build-avr8js.py` after editing
these modules or updating the vendored dependency. Python is needed for development
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
Read-only observations of Timer0 flags may also run natively while its simple
counter snapshot is valid and no custom TIFR0 read hook is installed. Writes still
return to JavaScript. Native blocks retain the original clock/interrupt deadline;
polling optimizations do not advance past an overflow or skip GPIO timestamps.

Supported exact targets are `atmega48p`, `atmega88p`, `atmega168p`, `atmega328p`.
The adapter selects each device's Flash, SRAM, EEPROM and interrupt-vector
spacing. Package/board pin mappings live in `.casim` descriptors. CPU instructions,
GPIO direction, pull-ups, pin interrupts, timers 0/1/2, timer PWM, ADC conversions,
EEPROM, asynchronous UART TX/RX, SPI and TWI/I2C are emulated through registers and firmware.
UART electrical framing supports 5–9 data bits and parity/stop configuration;
RX is sampled through the electrical window, so choose a quantum comfortably
shorter than the bit period. USART includes RXB8/TXB8, parity/framing/overrun
flags, a two-entry receive FIFO, transmit buffering, MPCM, synchronous controller/
target operation through PD4/XCK and master SPI mode (MSPIM).

The ADC implements single conversions, free-running and all eight ADTS selections
(comparator, INT0, Timer0 compare/overflow, Timer1 compare/overflow/capture),
ADCL/ADCH locking, references and left adjustment. Analog comparator inputs are
PD6/AIN0, PD7/AIN1, bandgap or the ADC mux with ACME and ADEN cleared. Comparator
interrupt modes and ACIC route to Timer1 capture. ICP1/PB0 supports edge selection,
the four-clock noise filter, ICF1/ICIE1 and the shared 16-bit TEMP read latch.
Timer2 supports an ideal 32768 Hz asynchronous oscillator (`timer2Hz`), external
TOSC1/PB6 edges with EXCLK, ASSR update-busy flags and operation during applicable
sleep modes. GTCCR synchronizes/holds prescalers; PRR gates peripheral clocks.

The watchdog uses independent simulated time for 16 ms–8 s periods, WDR, timed
configuration, interrupt/reset/combined modes and WDTON. All six defined sleep
modes stop the CPU and gate the appropriate clocks; enabled wake sources resume
execution even with global interrupts disabled. Reset rebuilds peripheral state,
retains SRAM for electrical/watchdog resets and sets sticky MCUSR cause flags.
IVCE/IVSEL relocate interrupt vectors; fuses configure CKDIV8, CKOUT, RSTDISBL,
WDTON, BOD, BOOTSZ/BOOTRST. BOD has a functional threshold/hysteresis and the timed
sleep disable sequence. OSCCAL adjusts nominal frequency using an approximate
linear calibration law; oscillator startup and analog calibration are idealized.

SPM implements page buffering, erase/write, busy timing, lock bits, fuse reads and
RWW/NRWW execution rules. ATmega48P has no boot section; SELFPRGEN controls SPM.
The other devices restrict SPM to their fuse-selected boot section. Flash page
size, boot geometry, signature and vector spacing are device profiles. ATmega328P
also supports SIGRD signature/calibration reads. EEPROM implements protected
erase/write modes, ready interrupt and independent write timing. Flash/EEPROM
survive electrical reset; session reset restores the original firmware/images.
SPM never writes the user's HEX/BIN file. Flash uses 4.5 ms operations and EEPROM
3.4 ms erase+write / 1.8 ms single operations, as functional timing constants.

SPI uses PB2/SS, PB3/MOSI, PB4/MISO and PB5/SCK. Controller and target roles,
all four CPOL/CPHA modes, both bit orders, SPIF/SPIE and write collision are
implemented. DDR still controls controller MOSI/SCK and target MISO outputs.
Target deselection releases MISO. A controller's input SS going LOW switches
it to target mode and raises SPIF. Flags clear on ISR entry or the documented
SPSR-then-SPDR access sequence, not by writing a 1 to SPSR.

TWI uses PC4/SDA and PC5/SCL, always open-drain; external pull-ups or enabled
PORT pull-ups must bring the bus HIGH. Both controller and target support 7-bit
addressing, ACK/NACK, repeated START/STOP, TWINT/TWIE and clock stretching.
Targets support TWAR/TWAMR address matching and general call. Controller reads
and acknowledgements observe the actual SDA voltage. A controller that releases
SDA but observes LOW during a transmitted 1 reports arbitration lost (38h).
Controllers synchronize SCL LOW/HIGH phases and arbitrate on the resolved SDA
line. Address recognition can continue after arbitration loss. Hardware target
matching is 7-bit, as on these AVRs; 10-bit protocol sequences require firmware.
A target holds SCL LOW while TWINT awaits firmware.
No peer/pull-up is invented: unmatched addresses NACK, and a bus held LOW waits.
Choose an input quantum much shorter than a half-bit period, including for SPI
targets; clock stretching and electrical rise times can extend an I2C transfer.
SPI, USART and I2C rates use the current CPU prescaler; CLKPR/OSCCAL changes
rescale the remaining phase of an in-flight bit.

Device capacities/vector spacing are registered in `avr8js-devices.js`.
`CircuitAliveAVR.registerDevice(id, profile)` accepts the implemented `atmega-x8`
layout only. A later descriptor module may register a verified compatible device
without editing the adapter. Other register layouts need an adapter, not a renamed
profile. `CircuitAliveAVR.capabilities` records functional blocks and physical scope.

Pin IDs are `PB0`–`PB7`, `PC0`–`PC6`, `PD0`–`PD7`, `ADC6`, `ADC7`, `VCC`,
`AVCC`, `GND`, `AREF`, `RESET`. Programming RSTDISBL enables PC6 through the RESET
binding. Bundled descriptors conditionally create that driver from `fuse_high`,
so ordinary shared reset nets remain passive and do not couple analog partitions.
The configured clock is an ideal nominal oscillator. External ISP/high-voltage
programmers and debugWIRE are separate programming/debug interfaces and are not
included. Protection diodes, startup jitter and thermal/electrical damage are
outside the functional circuit model.
Bundled descriptors now include a configurable approximate supply-current load;
it is not a prediction of consumption versus firmware, temperature or clock.
EEPROM survives electrical reset but returns to the initial image on session reset.

The ADC uses `AVCC` (falling back to `VCC`), external `AREF` and the upstream
internal references; channel inputs are voltages relative to `GND`. GPIO inputs
use 0.3/0.6 of VCC with hysteresis. `options.supplyMin` and `supplyMax` set the
allowed supply window (defaults 1.8–5.5 V). Invalid power or active-low reset
releases all GPIOs and stops CPU execution. Restoring power/reset restarts the CPU.
The optional `option_parameters` descriptor map feeds resolved numeric symbol
parameters into runtime options. AVR models expose decimal `fuse_low`,
`fuse_high`, `fuse_extended`, `lock_bits` and `timer2_hz` this way.

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
Oscillator modes, power-up timing, protection fuses, precise supply consumption and pin
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
