/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <circuitalive/session_runner.h>
#include <sim/platform_runtime.h>
#include <sim/memory_image.h>
#include <sim/programmable_runtime.h>
#include <sim/programmable_pipeline.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace CIRCUITALIVE;
namespace fs = std::filesystem;

namespace
{
struct PROGRAM_FILES
{
    fs::path directory = fs::temp_directory_path()
                         / ( fs::path( u8"ca programmable-ç" ) / std::to_string( ProcessId() ) );
    PROGRAM_FILES() { fs::create_directories( directory ); }
    ~PROGRAM_FILES() { std::error_code error; fs::remove_all( directory, error ); }

    fs::path Firmware( const std::string& name, const std::vector<std::uint16_t>& words,
                       bool hex = true )
    {
        std::vector<std::uint8_t> bytes;
        for( auto word : words )
        {
            bytes.push_back( word & 255 );
            bytes.push_back( word >> 8 );
        }
        auto path = directory / ( name + ( hex ? ".hex" : ".bin" ) );
        if( !hex )
        {
            std::ofstream output( path, std::ios::binary );
            output.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
        }
        else
        {
            // Generate independent Intel HEX records, including their checksums.
            std::ofstream output( path );
            output << std::hex << std::uppercase << std::setfill( '0' );
            for( std::size_t address = 0; address < bytes.size(); address += 16 )
            {
                const auto count = std::min<std::size_t>( 16, bytes.size() - address );
                unsigned sum = count + ( address >> 8 ) + ( address & 255 );
                output << ':' << std::setw( 2 ) << count << std::setw( 4 ) << address << "00";
                for( std::size_t i = 0; i < count; ++i )
                {
                    output << std::setw( 2 ) << unsigned( bytes[address + i] );
                    sum += bytes[address + i];
                }
                output << std::setw( 2 ) << ( ( -sum ) & 255 ) << '\n';
            }
            output << ":00000001FF\n";
        }
        return path;
    }
};

// These are real AVR instructions, avoiding an AVR toolchain test dependency.
std::uint16_t Ldi( unsigned reg, unsigned value )
{
    return 0xe000 | ( ( value & 0xf0 ) << 4 ) | ( ( reg - 16 ) << 4 ) | ( value & 15 );
}
std::uint16_t Out( unsigned io, unsigned reg )
{
    return 0xb800 | ( ( io & 0x30 ) << 5 ) | ( reg << 4 ) | ( io & 15 );
}
std::uint16_t Rjmp( int displacement ) { return 0xc000 | ( displacement & 0xfff ); }
std::uint16_t Sbi( unsigned io, unsigned bit ) { return 0x9a00 | ( io << 3 ) | bit; }
std::uint16_t Cbi( unsigned io, unsigned bit ) { return 0x9800 | ( io << 3 ) | bit; }
std::vector<std::uint16_t> DigitalOutput( bool high )
{
    // DDRB.PB5 = output; PORTB.PB5 = selected level; loop forever.
    return { Ldi( 16, 0x20 ), Out( 0x04, 16 ), Ldi( 16, high ? 0x20 : 0 ),
             Out( 0x05, 16 ), Rjmp( -1 ) };
}
std::vector<std::uint16_t> BlinkOutput()
{
    // Toggle PB5 every ~192 us, with real software delay loops.
    return { Ldi( 16, 0x20 ), Out( 0x04, 16 ), Sbi( 0x05, 5 ),
             Ldi( 17, 4 ), Ldi( 18, 255 ), 0x952a, 0xf7f1, 0x951a, 0xf7d9,
             Cbi( 0x05, 5 ), Ldi( 17, 4 ), Ldi( 18, 255 ), 0x952a, 0xf7f1,
             0x951a, 0xf7d9, Rjmp( -15 ) };
}
std::vector<std::uint16_t> InputToOutput()
{
    // Enable PD2 input pull-up; mirror PIND.PD2 onto PB5.
    return { Ldi( 16, 0x20 ), Out( 0x04, 16 ), Ldi( 16, 0x04 ), Out( 0x0b, 16 ),
             std::uint16_t( 0x9900 | ( 0x09 << 3 ) | 2 ), Rjmp( 2 ),
             Cbi( 0x05, 5 ), Rjmp( -4 ), Sbi( 0x05, 5 ), Rjmp( -6 ) };
}
std::vector<std::uint16_t> PwmOutput()
{
    // Fast PWM Timer0, OC0A/PD6, 50%, clock /64 (976.5625 Hz at 16 MHz).
    return { Ldi( 16, 0x40 ), Out( 0x0a, 16 ), Ldi( 16, 0x83 ), Out( 0x24, 16 ),
             Ldi( 16, 128 ), Out( 0x27, 16 ), Ldi( 16, 3 ), Out( 0x25, 16 ), Rjmp( -1 ) };
}

std::vector<std::uint16_t> TimerInterruptOutput()
{
    // ATmega328P vector 16 at word 32: Timer0 overflow. Toggle PINB.PB5
    // from the ISR, like a timed Blink; main remains in an interruptible loop.
    std::vector<std::uint16_t> program( 34, 0 );
    program[0] = Rjmp( 33 ); program[32] = Rjmp( 16 );
    const std::vector<std::uint16_t> code = {
        Ldi( 16, 255 ), Out( 0x3d, 16 ), Ldi( 16, 8 ), Out( 0x3e, 16 ),
        Ldi( 16, 0x20 ), Out( 0x04, 16 ), Ldi( 16, 0 ), Out( 0x24, 16 ),
        Ldi( 16, 1 ), 0x9300, 0x006e, Ldi( 16, 3 ), Out( 0x25, 16 ),
        0x9478, Rjmp( -1 ), Ldi( 16, 0x20 ), Out( 0x03, 16 ), 0x9518 };
    program.insert( program.end(), code.begin(), code.end() );
    return program;
}
std::vector<std::uint16_t> AdcToOutput()
{
    // ADC0 with AVCC reference, /128 ADC clock; PB5 = ADC0 >= half-scale.
    return { Ldi( 16, 0x20 ), Out( 0x04, 16 ), Ldi( 16, 0x40 ), 0x9300, 0x007c,
             Ldi( 16, 0xc7 ), 0x9300, 0x007a, 0x9100, 0x007a,
             0xfd06, Rjmp( -4 ), 0x9110, 0x0078, 0x9100, 0x0079,
             0xff01, Rjmp( 2 ), Sbi( 0x05, 5 ), Rjmp( -15 ),
             Cbi( 0x05, 5 ), Rjmp( -17 ) };
}

std::vector<std::uint16_t> PicDigitalOutput( bool high )
{
    // Select bank 1, configure RB0 output, then set its bank-0 output latch.
    return { 0x1683, 0x1006, 0x1283, std::uint16_t( high ? 0x1406 : 0x1006 ), 0x2804 };
}

std::vector<std::uint16_t> PicInputToOutput()
{
    // TRISB = 2; OPTION.RBPU = 0; continuously mirror RB1 input onto RB0.
    return { 0x1683, 0x3002, 0x0086, 0x1381, 0x1283,
             0x1886, 0x2809, 0x1006, 0x2805, 0x1406, 0x2805 };
}

std::vector<std::uint16_t> PicBlinkOutput()
{
    // DECFSZ/GOTO delay; XORWF toggles RB0 every approximately 197 instruction cycles.
    return { 0x1683, 0x0186, 0x1283, 0x1006, 0x3040, 0x00a0,
             0x0ba0, 0x2806, 0x3001, 0x0686, 0x2804 };
}

std::vector<std::uint16_t> PicTimerOutput()
{
    // Timer0 internal Fosc/4, no prescaler. Poll T0IF; toggle RB0 using CALL/RETURN.
    return { 0x1683, 0x0186, 0x3088, 0x0081, 0x1283, 0x0186, 0x0181,
             0x1d0b, 0x2807, 0x110b, 0x200e, 0x2807, 0x0000, 0x0000,
             0x3001, 0x0686, 0x0008 };
}

std::vector<std::uint16_t> PicArithmeticOutput()
{
    // A subroutine computes ((7-3)+2)^3 = 5, driving RB0/RB2 high and RB1 low.
    return { 0x1683, 0x0186, 0x1283, 0x2006, 0x0086, 0x2805,
             0x3007, 0x00a0, 0x3003, 0x0220, 0x3e02, 0x3a03, 0x0008 };
}

CIRCUIT_COMPONENT Supply( std::string reference, std::string node, const std::string& value )
{
    return { std::move( reference ), "Simulation_SPICE:VDC", value,
             { { "1", std::move( node ) }, { "2", "0" } } };
}

std::string Text( const fs::path& file )
{
    std::ifstream input( file );
    if( !input ) throw std::runtime_error( "Missing test runtime: " + file.string() );
    return std::string( std::istreambuf_iterator<char>( input ), {} );
}

PROGRAMMABLE_DEVICE_CONFIG AvrConfig( const fs::path& firmware,
                                     const std::string& device = "atmega328p" )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id = "U1";
    config.device = device;
    config.source = Text( CIRCUITALIVE_TEST_AVR_RUNTIME );
    config.pins = { "PB5", "PC0", "PD2", "PD6", "VCC", "AVCC", "GND", "AREF", "RESET" };
    auto image = EmptyMemory( device == "atmega48p" ? 4096 : device == "atmega88p" ? 8192
                                    : device == "atmega168p" ? 16384 : 32768, 255 );
    LoadMemoryImage( image, firmware );
    config.firmware = image.data;
    return config;
}

PROGRAMMABLE_DEVICE_CONFIG PicConfig( const fs::path& firmware )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id = "U1"; config.device = "pic16f84a"; config.clockHz = 4e6;
    config.source = Text( CIRCUITALIVE_TEST_PIC_RUNTIME );
    config.pins = { "RA0", "RA4", "RB0", "RB1", "RB2", "VCC", "GND", "RESET" };
    auto image = EmptyMemory( 0x4280, 255 ); LoadMemoryImage( image, firmware );
    config.firmware = std::move( image.data );
    return config;
}

std::vector<PROGRAMMABLE_PIN_INPUT> PicInputs( double input = 5 )
{
    return { { "U1", "VCC", 5 }, { "U1", "GND", 0 }, { "U1", "RESET", 5 },
             { "U1", "RB1", input } };
}

std::vector<PROGRAMMABLE_PIN_INPUT> AvrInputs( const std::string& id = "U1", double input = 5 )
{
    return { { id, "VCC", 5 }, { id, "AVCC", 5 }, { id, "GND", 0 }, { id, "AREF", 5 },
             { id, "RESET", 5 }, { id, "PD2", input }, { id, "PC0", input } };
}

PROGRAMMABLE_DRIVE PinDrive( const PROGRAMMABLE_RUNTIME& runtime, const std::string& pin,
                              const std::string& device = "U1" )
{
    const auto states = runtime.States();
    auto it = std::find_if( states.begin(), states.end(), [&]( const auto& state )
                           { return state.device == device && state.pin == pin; } );
    BOOST_REQUIRE_MESSAGE( it != states.end(), "Missing runtime pin state: " + device + ":" + pin );
    return it->drive;
}

CIRCUIT_COMPONENT WithOpenPins( CIRCUIT_COMPONENT component )
{
    for( const auto& pin : MODEL_REPOSITORY().SelectModel( component ).pins )
        component.pins.try_emplace( pin.defaultPin, "" );
    return component;
}

CIRCUIT_COMPONENT AvrSymbol( const fs::path& firmware, const std::string& reference = "U1" )
{
    CIRCUIT_COMPONENT u{ reference, "MCU_Microchip_ATmega:ATmega328P-P", "ATmega328P-P",
                        { { "7", "vcc" }, { "20", "vcc" }, { "8", "0" }, { "22", "0" },
                          { "1", "reset" }, { "21", "vcc" }, { "19", "out" },
                          { "4", "input" }, { "23", "analog" }, { "12", "pwm" } } };
    u.modelFile = "builtin:mcu_atmega328p_p";
    if( !firmware.empty() ) u.resources["firmware"] = { PathUtf8( firmware ), "auto", 0 };
    return WithOpenPins( std::move( u ) );
}

void SelectSignals( CIRCUIT_SNAPSHOT& circuit, std::vector<std::string> signals )
{
    for( const auto& signal : circuit.config.signals )
        if( std::find( signals.begin(), signals.end(), signal ) == signals.end() )
            signals.push_back( signal );
    circuit.config.signals = std::move( signals );
    circuit.config.historyCapacity = std::min<std::size_t>( 256, 65536 / circuit.config.signals.size() );
}

CIRCUIT_SNAPSHOT AvrCircuit( const CIRCUIT_COMPONENT& u, const std::string& input = "5",
                             const std::string& analog = "1" )
{
    auto circuit = BuildCircuitSnapshot( { Supply( "VCC", "vcc", "5" ),
            Supply( "VRESET", "reset", "5" ), Supply( "VIN", "input", input ),
            Supply( "VADC", "analog", analog ), u,
            { "Rload", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } } } );
    SelectSignals( circuit, { "v(" + circuit.pinNodes[4].at( "19" ) + ")",
                               "v(" + circuit.pinNodes[4].at( "4" ) + ")",
                               "v(" + circuit.pinNodes[4].at( "12" ) + ")" } );
    circuit.config.step = circuit.config.maxStep = 1e-6;
    circuit.config.horizon = .01;
    circuit.config.historyCapacity = std::min<std::size_t>( 4096, 65536 / circuit.config.signals.size() );
    return circuit;
}
} // namespace

BOOST_FIXTURE_TEST_CASE( ProgrammableAvrExecutesHexAndBinOnEveryDeclaredCpuVariant, PROGRAM_FILES )
{
    const auto hex = Firmware( "high", DigitalOutput( true ) );
    const auto bin = Firmware( "low", DigitalOutput( false ), false );
    for( const std::string device : { "atmega48p", "atmega88p", "atmega168p", "atmega328p" } )
    {
        BOOST_TEST_CONTEXT( device )
        {
            auto high = AvrConfig( hex, device );
            auto low = AvrConfig( bin, device ); low.id = "U2";
            PROGRAMMABLE_RUNTIME runtime;
            runtime.Configure( { high, low } );
            auto inputs = AvrInputs(); const auto second = AvrInputs( "U2" );
            inputs.insert( inputs.end(), second.begin(), second.end() );
            runtime.AdvanceTo( 10e-6, inputs );
            BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH );
            BOOST_CHECK( PinDrive( runtime, "PB5", "U2" ) == PROGRAMMABLE_DRIVE::LOW );
            BOOST_CHECK( PinDrive( runtime, "PC0" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );
            BOOST_CHECK_CLOSE( runtime.Time(), 10e-6, 1e-8 );
        }
    }
}

BOOST_AUTO_TEST_CASE( ProgrammableNativeAvrInstructionsMatchReferenceDecoder )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id="CHECK"; config.device="decoder-audit"; config.pins={"TEST"};
    config.source=Text(CIRCUITALIVE_TEST_AVR_RUNTIME)+R"JS(
      globalThis.createDevice=function() {
        const program=new Uint16Array(16384), a=new AVR8js.CPU(program,2048), b=new AVR8js.CPU(program,2048);
        const templates=[0,165].map(seed=>Uint8Array.from({length:a.data.length},(_,i)=>(i*17+seed)&255));
        const wordsA=new Uint32Array(a.data.buffer),wordsB=new Uint32Array(b.data.buffer);
        let cursor=0,confirmed=0,executed=0;
        return {nextEventTime:()=>confirmed+1e-6,advanceTo:time=>{
          for(let end=Math.min(131072,cursor+256);cursor<end;cursor++) {
            const opcode=cursor&65535, seed=cursor<65536?0:165;
            program[0]=opcode;program[1]=0x100;
            for(const cpu of [a,b]) {
              cpu.data.set(templates[cursor<65536?0:1]);cpu.pc=0;cpu.cycles=123;cpu.SP=0x600;cpu.data[95]=seed;
              for(const reg of [26,28,30])cpu.dataView.setUint16(reg,0x100,true);
            }
            const result=__circuitaliveAvrRun(b.data,b.progBytes,b.pc,b.cycles,129,-1,0,0);
            if(result[1]===123) {
              for(let i=0;i<wordsA.length;i++)if(wordsA[i]!==wordsB[i])throw Error('Bailout mutated memory '+opcode);
              continue;
            }
            AVR8js.avrInstruction(a);executed++;
            if(result[0]!==a.pc || result[1]!==a.cycles)throw Error('Native PC/cycles mismatch '+opcode);
            for(let i=0;i<wordsA.length;i++)if(wordsA[i]!==wordsB[i])throw Error('Native memory mismatch '+opcode+' at word '+i);
          }
          confirmed=time;
          if(cursor===131072 && executed<50000)throw Error('Too few native instructions checked');
          return [];
        }};
      };
    )JS";
    PROGRAMMABLE_RUNTIME runtime; runtime.Configure({config});
    for(int i=1;i<=512;i++)BOOST_CHECK_NO_THROW(runtime.AdvanceTo(i*1e-6));
}

BOOST_AUTO_TEST_CASE( ProgrammableNativeHelperKeepsScriptLimitsAndRejectsCoercion )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id="BOUNDS";config.device="native-budget-audit";config.pins={"TEST"};
    config.source=R"JS(
      globalThis.createDevice=()=>({nextEventTime:()=>Infinity,advanceTo:t=>{
        const data=new Uint8Array(256),flash=new Uint8Array([255,207,0,0]);
        let coerced=false, rejected=false;
        try { __circuitaliveAvrRun(data,flash,{valueOf:()=>{coerced=true;return 0;}},0,100,-1,0,0); }
        catch(error){rejected=true;}
        if(coerced || !rejected)throw Error('Unsafe native argument coercion');
        let cycles=0;
        for(let i=0;i<100;i++)cycles=__circuitaliveAvrRun(data,flash,0,cycles,cycles+100000,-1,0,0)[1];
        throw Error('Native helper bypassed the execution budget');
      }});
    )JS";
    PROGRAMMABLE_RUNTIME runtime;runtime.Configure({config});
    BOOST_CHECK_EXCEPTION(runtime.AdvanceTo(1e-6),std::runtime_error,[](const auto& error) {
        return std::string(error.what()).find("script execution budget exceeded")!=std::string::npos;
    });
}

BOOST_FIXTURE_TEST_CASE( ProgrammableCooperativeAvrYieldsBeforeReadingInputs, PROGRAM_FILES )
{
    auto config=AvrConfig(Firmware("cooperative-input",InputToOutput()));
    config.autonomousWindowSeconds=.001;
    PROGRAMMABLE_RUNTIME runtime;runtime.Configure({config});
    auto inputs=AvrInputs();
    runtime.AdvanceTo(.001,inputs);
    BOOST_CHECK_GT(runtime.Time(),0);
    BOOST_CHECK_LT(runtime.Time(),10e-6);
    const auto first=runtime.Time();
    inputs=AvrInputs("U1",0);
    runtime.AdvanceTo(.001,inputs);
    BOOST_CHECK_GT(runtime.Time(),first);
    BOOST_CHECK_LT(runtime.Time()-first,2e-6);
    BOOST_CHECK(PinDrive(runtime,"PB5")==PROGRAMMABLE_DRIVE::LOW);
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePipelinePreservesEventsAndRefreshesInputsAtBarriers, PROGRAM_FILES )
{
    for( const auto& firmware : { BlinkOutput(), TimerInterruptOutput(), PwmOutput(),
                                 InputToOutput(), AdcToOutput() } )
    {
        auto config = AvrConfig( Firmware( "pipeline", firmware ) );
        config.autonomousWindowSeconds = .001;
        PROGRAMMABLE_RUNTIME serial;
        PROGRAMMABLE_PIPELINE pipeline;
        serial.Configure( { config } ); pipeline.Configure( { config } );
        double time = 0;
        bool prefetched = false;
        while( time < .003 - 1e-12 )
        {
            const auto inputs = AvrInputs( "U1", time < .0015 ? 5 : 0 );
            const double target = std::min( .003, time + .001 );
            const auto expected = serial.AdvanceTo( target, inputs );
            const auto actual = pipeline.AdvanceTo( target, inputs, .003 );
            BOOST_REQUIRE_SMALL( serial.Time() - pipeline.Time(), 1e-14 );
            BOOST_REQUIRE_EQUAL( expected.size(), actual.size() );
            for( std::size_t i = 0; i < actual.size(); ++i )
            {
                BOOST_CHECK_EQUAL( expected[i].pin, actual[i].pin );
                BOOST_CHECK( expected[i].drive == actual[i].drive );
                BOOST_CHECK_SMALL( expected[i].time - actual[i].time, 1e-14 );
            }
            prefetched |= pipeline.HasPrefetch();
            time = pipeline.Time();
        }
        BOOST_CHECK( !pipeline.HasPrefetch() );
        if( firmware == BlinkOutput() ) BOOST_CHECK( prefetched );
        pipeline.Reset();
        BOOST_CHECK_EQUAL( pipeline.Time(), 0 );
        BOOST_CHECK( !pipeline.HasPrefetch() );
    }
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePipelineResetDiscardsPendingWorkAndReconfigures, PROGRAM_FILES )
{
    auto config = AvrConfig( Firmware( "pipeline-reset", BlinkOutput() ) );
    config.autonomousWindowSeconds = .001;
    PROGRAMMABLE_PIPELINE pipeline;
    pipeline.Configure( { config } );
    pipeline.AdvanceTo( .001, AvrInputs(), .01 );
    BOOST_REQUIRE( pipeline.HasPrefetch() );
    pipeline.Reset();
    BOOST_CHECK_EQUAL( pipeline.Time(), 0 );
    BOOST_CHECK( !pipeline.HasPrefetch() );
    pipeline.AdvanceTo( .001, AvrInputs(), .01 );
    BOOST_REQUIRE( pipeline.HasPrefetch() );
    config.id = "U2";
    pipeline.Configure( { config } );
    BOOST_CHECK_EQUAL( pipeline.Time(), 0 );
    const auto events = pipeline.AdvanceTo( .001, AvrInputs( "U2" ) );
    BOOST_REQUIRE( !events.empty() );
    for( const auto& event : events ) BOOST_CHECK_EQUAL( event.device, "U2" );
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePipelineYieldsForFreshInputsAfterAutonomousWork, PROGRAM_FILES )
{
    // About 1.2 ms of computation precedes the first GPIO observation. The
    // second window starts in the background with the old HIGH input, then
    // must stop before reading it and resume with the circuit's new LOW.
    std::vector<std::uint16_t> firmware = { Ldi( 17, 25 ), Ldi( 18, 255 ),
                                           0x952a, 0xf7f1, 0x951a, 0xf7d9 };
    const auto input = InputToOutput();
    firmware.insert( firmware.end(), input.begin(), input.end() );
    auto config = AvrConfig( Firmware( "pipeline-late-input", firmware ) );
    config.autonomousWindowSeconds = .001;
    PROGRAMMABLE_PIPELINE pipeline;
    pipeline.Configure( { config } );
    pipeline.AdvanceTo( .001, AvrInputs( "U1", 5 ), .003 );
    BOOST_REQUIRE( pipeline.HasPrefetch() );
    const auto pending = pipeline.AdvanceTo( .002, AvrInputs( "U1", 0 ), .003 );
    BOOST_REQUIRE_GT( pipeline.Time(), .001 );
    BOOST_REQUIRE_LT( pipeline.Time(), .002 );
    BOOST_CHECK( !pipeline.HasPrefetch() );
    bool sawLow = false, sawHigh = false;
    for( const auto& event : pending ) if( event.pin == "PB5" )
    {
        sawLow |= event.drive == PROGRAMMABLE_DRIVE::LOW;
        sawHigh |= event.drive == PROGRAMMABLE_DRIVE::HIGH;
    }
    for( int sample = 0; sample < 10; ++sample )
        for( const auto& event : pipeline.AdvanceTo( pipeline.Time() + .001,
                                                   AvrInputs( "U1", 0 ) ) )
            if( event.pin == "PB5" )
            {
                sawLow |= event.drive == PROGRAMMABLE_DRIVE::LOW;
                sawHigh |= event.drive == PROGRAMMABLE_DRIVE::HIGH;
            }
    BOOST_CHECK( sawLow );
    BOOST_CHECK( !sawHigh );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableAcceleratedTimersKeepUpstreamGpioEventTimes, PROGRAM_FILES )
{
    auto phase=PwmOutput(); phase[2]=Ldi(16,0x81);
    const auto timerSource=Text(fs::path(CIRCUITALIVE_TEST_AVR_RUNTIME).parent_path().parent_path().parent_path()
        / "thirdparty/avr8js/cjs/peripherals/timer.js");
    for(const auto& firmware : {BlinkOutput(),PwmOutput(),phase,TimerInterruptOutput(),AdcToOutput()})
    {
        auto config=AvrConfig(Firmware("timer-reference",firmware));
        auto reference=config;
        const std::string native="typeof global.__circuitaliveAvrRun==='function'";
        reference.source.replace(reference.source.find(native),native.size(),"false");
        const std::string start="modules['peripherals/timer']=function(require,module,exports){\n";
        const auto first=reference.source.find(start)+start.size();
        const auto last=reference.source.find("\n};\nmodules[",first);
        reference.source.replace(first,last-first,timerSource);
        PROGRAMMABLE_RUNTIME expected,actual;expected.Configure({reference});actual.Configure({config});
        for(int step=1;step<=100;step++)
        {
            const auto inputs=AvrInputs("U1",step>50?1:4);
            const auto a=expected.AdvanceTo(step*100e-6,inputs);
            const auto b=actual.AdvanceTo(step*100e-6,inputs);
            BOOST_REQUIRE_EQUAL(a.size(),b.size());
            for(std::size_t i=0;i<a.size();i++) {
                BOOST_CHECK_EQUAL(a[i].pin,b[i].pin);
                BOOST_CHECK(a[i].drive==b[i].drive);
                BOOST_CHECK_SMALL(a[i].time-b[i].time,1e-12);
            }
        }
    }
}

BOOST_FIXTURE_TEST_CASE( ProgrammableAvrBlinkEventsKeepFirmwareCycleTimestamps, PROGRAM_FILES )
{
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( { AvrConfig( Firmware( "blink", BlinkOutput() ) ) } );
    const auto events = runtime.AdvanceTo( .001, AvrInputs() );
    std::vector<PROGRAMMABLE_PIN_STATE> edges;
    for( const auto& event : events )
        if( event.pin == "PB5" && ( event.drive == PROGRAMMABLE_DRIVE::HIGH
                                    || event.drive == PROGRAMMABLE_DRIVE::LOW ) )
            edges.push_back( event );
    BOOST_REQUIRE_GE( edges.size(), 5 );
    for( std::size_t i = 1; i < edges.size(); ++i )
    {
        BOOST_CHECK( edges[i].drive != edges[i-1].drive );
        const auto interval = edges[i].time - edges[i-1].time;
        // Ignore the initial LOW-to-HIGH direction/PORT setup transition.
        if( interval > 1e-6 )
        {
            BOOST_CHECK_GT( interval, 190e-6 );
            BOOST_CHECK_LT( interval, 195e-6 );
        }
    }
}

BOOST_FIXTURE_TEST_CASE( ProgrammableFirmwareTimingScalesWithTheConfiguredHardwareClock, PROGRAM_FILES )
{
    const auto firmware = Firmware( "timer-clock", TimerInterruptOutput() );
    for( double clock : { 8e6, 16e6, 20e6 } )
    {
        auto config = AvrConfig( firmware );
        config.clockHz = clock;
        PROGRAMMABLE_RUNTIME runtime;
        runtime.Configure( { config } );
        std::vector<PROGRAMMABLE_PIN_STATE> edges;
        for( const auto& event : runtime.AdvanceTo( .005, AvrInputs() ) )
            if( event.pin == "PB5" && event.time > 1e-5 ) edges.push_back( event );
        BOOST_REQUIRE_GE( edges.size(), 2 );
        for( std::size_t i = 1; i < edges.size(); ++i )
        {
            BOOST_CHECK( edges[i].drive != edges[i-1].drive );
            BOOST_CHECK_CLOSE( edges[i].time - edges[i-1].time, 256 * 64 / clock, .02 );
        }
    }
}

BOOST_AUTO_TEST_CASE( ProgrammableMeasurementDefaultsFollowTheDeclaredSupplyAndKeepExplicitPorts )
{
    const MODEL_REPOSITORY repository;
    std::size_t tested = 0;
    for( const auto& model : repository.List() )
    {
        if( !model.runtime ) continue;
        ++tested;
        auto json = nlohmann::json::parse( Text( model.file ) );
        json.erase( "measurement_ports" );
        const auto defaults = MODEL_REPOSITORY::LoadText( json.dump() );
        BOOST_CHECK_EQUAL( defaults.measurePositive, model.runtime->vcc );
        BOOST_CHECK_EQUAL( defaults.measureNegative, model.runtime->ground );
        const auto& first = model.pins.front().id;
        const auto& second = model.pins[1].id;
        json["measurement_ports"] = { { "positive", first }, { "negative", second } };
        const auto explicitPorts = MODEL_REPOSITORY::LoadText( json.dump() );
        BOOST_CHECK_EQUAL( explicitPorts.measurePositive, first );
        BOOST_CHECK_EQUAL( explicitPorts.measureNegative, second );
    }
    BOOST_CHECK_GE( tested, 17 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableSupplyMeasurementUsesVccAndGroundWithoutAnExplicitPair, PROGRAM_FILES )
{
    auto u = AvrSymbol( Firmware( "supply-measurement", DigitalOutput( true ) ) );
    u.measurements.voltage = true;
    auto circuit = BuildCircuitSnapshot( { Supply( "VCC", "vcc", "5" ),
                                           Supply( "VRESET", "reset", "5" ), u } );
    circuit.config.step = circuit.config.maxStep = 1e-6;
    circuit.config.horizon = .001;
    BOOST_REQUIRE_EQUAL( circuit.measurements.size(), 1 );
    const auto& measurement = circuit.measurements.front();
    BOOST_CHECK_EQUAL( circuit.config.signals.at( measurement.positive ),
                       "v(" + circuit.pinNodes[2].at( "7" ) + ")" );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( 20e-6 );
    BOOST_CHECK_CLOSE( MeasurementValue( measurement, session.Snapshot().latest ), 5, .001 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableAvrInputPullupResetAndPowerDoNotForceInputPinsLow, PROGRAM_FILES )
{
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( { AvrConfig( Firmware( "input", InputToOutput() ) ) } );
    auto inputs = AvrInputs();
    runtime.AdvanceTo( 10e-6, inputs );
    BOOST_CHECK( PinDrive( runtime, "PD2" ) == PROGRAMMABLE_DRIVE::PULL_UP );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH );
    inputs = AvrInputs( "U1", 0 );
    runtime.AdvanceTo( 20e-6, inputs );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::LOW );
    for( auto& input : inputs ) if( input.pin == "RESET" ) input.voltage = 0;
    runtime.AdvanceTo( 30e-6, inputs );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );
    runtime.AdvanceTo( 40e-6, AvrInputs() );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH );
    inputs = AvrInputs();
    for( auto& input : inputs ) if( input.pin == "VCC" ) input.voltage = 0;
    runtime.AdvanceTo( 50e-6, inputs );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );
    runtime.Reset();
    BOOST_CHECK_SMALL( runtime.Time(), 1e-20 );
    runtime.AdvanceTo( 10e-6, AvrInputs() );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableAvrTimerPwmAndAdcFollowRegisterFirmware, PROGRAM_FILES )
{
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( { AvrConfig( Firmware( "pwm", PwmOutput() ) ) } );
    const auto events = runtime.AdvanceTo( .004, AvrInputs() );
    std::vector<PROGRAMMABLE_PIN_STATE> edges;
    for( const auto& event : events )
        if( event.pin == "PD6" && event.time > .001
            && ( event.drive == PROGRAMMABLE_DRIVE::HIGH || event.drive == PROGRAMMABLE_DRIVE::LOW ) )
            edges.push_back( event );
    BOOST_REQUIRE_GE( edges.size(), 4 );
    for( std::size_t i = 1; i < edges.size(); ++i )
    {
        BOOST_CHECK( edges[i].drive != edges[i-1].drive );
        BOOST_CHECK_CLOSE( edges[i].time - edges[i-1].time, .000512, 2 );
    }
    runtime.Configure( { AvrConfig( Firmware( "adc", AdcToOutput() ) ) } );
    runtime.AdvanceTo( .0005, AvrInputs( "U1", 1 ) );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::LOW );
    runtime.AdvanceTo( .001, AvrInputs( "U1", 4 ) );
    BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH );
}

BOOST_AUTO_TEST_CASE( ProgrammableContractAcceptsNonAvrJavascriptModulesWithoutApplicationRebuild )
{
    // This is a synthetic backend contract test, not an emulated PIC/ARM/FPGA.
    PROGRAMMABLE_DEVICE_CONFIG a;
    a.id = "LOGIC1"; a.device = "synthetic-contract-test"; a.clockHz = 3000000;
    a.firmware = { 1 }; a.pins = { "IO0" }; a.optionsJson = "{\"delay\":0.000004}";
    a.quantumSeconds = 10e-6;
    a.source = R"JS(
        globalThis.createDevice = function(config) {
            let sent = false;
            return {
                reset: function() { sent = false; },
                nextEventTime: function() { return sent ? 1e30 : config.options.delay; },
                advanceTo: function(time, inputs) {
                    if (sent || time < config.options.delay) return [];
                    sent = true;
                    return [{pin:"IO0", drive:config.firmware[0] ? 2 : 1,
                             time:config.options.delay}];
                }
            };
        };
    )JS";
    auto b = a; b.id = "LOGIC2"; b.firmware = { 0 };
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( { a, b } );
    BOOST_CHECK_CLOSE( runtime.NextEventTime(), 4e-6, 1e-8 );
    runtime.AdvanceTo( 2e-6 );
    BOOST_CHECK( PinDrive( runtime, "IO0", "LOGIC1" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );
    auto events = runtime.AdvanceTo( 6e-6 );
    BOOST_REQUIRE_EQUAL( events.size(), 2 );
    for( const auto& event : events ) BOOST_CHECK_CLOSE( event.time, 4e-6, 1e-8 );
    BOOST_CHECK( PinDrive( runtime, "IO0", "LOGIC1" ) == PROGRAMMABLE_DRIVE::HIGH );
    BOOST_CHECK( PinDrive( runtime, "IO0", "LOGIC2" ) == PROGRAMMABLE_DRIVE::LOW );
    runtime.Reset();
    BOOST_CHECK_SMALL( runtime.Time(), 1e-20 );
    BOOST_CHECK_CLOSE( runtime.NextEventTime(), 4e-6, 1e-8 );
}

BOOST_AUTO_TEST_CASE( ProgrammableScriptErrorsAndInvalidEventsAreReported )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id = "Faulty"; config.device = "synthetic-contract-test"; config.pins = { "IO0" };
    config.source = "globalThis.createDevice = function() { throw new Error('load failed'); };";
    PROGRAMMABLE_RUNTIME runtime;
    BOOST_CHECK_THROW( runtime.Configure( { config } ), std::runtime_error );
    config.source = R"JS(
        globalThis.createDevice = function() {
            return {reset:function(){}, nextEventTime:function(){return 1e-6;},
                    advanceTo:function(t){throw new Error("execution failed");}};
        };
    )JS";
    runtime.Configure( { config } );
    BOOST_CHECK_THROW( runtime.AdvanceTo( 2e-6 ), std::runtime_error );
    config.source = R"JS(
        globalThis.createDevice = function() {
            return {reset:function(){}, nextEventTime:function(){return 1e-6;},
                    advanceTo:function(t){return [{pin:"UNKNOWN",drive:2,time:t}];}};
        };
    )JS";
    runtime.Configure( { config } );
    BOOST_CHECK_THROW( runtime.AdvanceTo( 2e-6 ), std::runtime_error );
    config.engine = "unavailable-backend";
    BOOST_CHECK_THROW( runtime.Configure( { config } ), std::runtime_error );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableModelRequiresValidFirmwareWithinItsDeclaredFlash, PROGRAM_FILES )
{
    auto u = AvrSymbol( {} );
    BOOST_CHECK_THROW( AvrCircuit( u ), std::runtime_error );
    u.resources["firmware"] = { ( directory / "missing.hex" ).string(), "auto", 0 };
    BOOST_CHECK_THROW( AvrCircuit( u ), std::runtime_error );
    auto valid = Firmware( "valid", DigitalOutput( true ) );
    std::string corrupted = Text( valid );
    corrupted[corrupted.find( '\n' ) - 1] = corrupted[corrupted.find( '\n' ) - 1] == '0' ? '1' : '0';
    std::ofstream( directory / "bad.hex" ) << corrupted;
    u.resources["firmware"] = { PathUtf8( directory / "bad.hex" ), "auto", 0 };
    BOOST_CHECK_THROW( AvrCircuit( u ), std::runtime_error );
    std::vector<char> oversized( 32769, 0 );
    std::ofstream( directory / "large.bin", std::ios::binary ).write( oversized.data(), oversized.size() );
    u.resources["firmware"] = { PathUtf8( directory / "large.bin" ), "auto", 0 };
    BOOST_CHECK_THROW( AvrCircuit( u ), std::runtime_error );
    u.resources["firmware"] = { PathUtf8( valid ), "auto", 32768 };
    BOOST_CHECK_THROW( AvrCircuit( u ), std::runtime_error );
    // Freeze file contents before starting: removing the source image is harmless.
    u.resources["firmware"] = { "valid.hex", "auto", 0 }; u.projectDirectory = PathUtf8( directory );
    auto circuit = AvrCircuit( u );
    BOOST_REQUIRE_EQUAL( circuit.config.programmableDevices.size(), 1 );
    BOOST_REQUIRE_EQUAL( circuit.config.programmableDevices[0].firmware.size(), 32768 );
    BOOST_CHECK_EQUAL( circuit.config.programmableDevices[0].firmware[0], 0 );
    BOOST_CHECK_EQUAL( circuit.config.programmableDevices[0].firmware[1], 0xe2 );
    fs::remove( valid );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( 20e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableWorkerReadsDigitalInputsAndKeepsTwoSymbolsIndependent, PROGRAM_FILES )
{
    const auto image = Firmware( "mirror", InputToOutput() );
    auto first = AvrSymbol( image );
    auto second = first; second.reference = "U2";
    second.resources["firmware"] = { PathUtf8( Firmware( "low", DigitalOutput( false ), false ) ), "bin", 0 };
    second.pins["19"] = "out2";
    std::vector<CIRCUIT_COMPONENT> parts = { Supply( "VCC", "vcc", "5" ),
        Supply( "VRESET", "reset", "5" ), Supply( "VIN", "input", "5" ),
        Supply( "VADC", "analog", "1" ), first, second,
        { "Rload", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } },
        { "Rload2", "Device:R", "10k", { { "1", "out2" }, { "2", "0" } } } };
    auto circuit = BuildCircuitSnapshot( parts );
    SelectSignals( circuit, { "v(" + circuit.pinNodes[4].at( "19" ) + ")",
                               "v(" + circuit.pinNodes[5].at( "19" ) + ")" } );
    circuit.config.step = circuit.config.maxStep = 1e-6;
    circuit.config.horizon = .001;
    BOOST_REQUIRE_EQUAL( circuit.config.programmableDevices.size(), 2 );
    BOOST_CHECK_NE( circuit.config.programmableDevices[0].id, circuit.config.programmableDevices[1].id );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( 20e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[1], .1 );
    session.SetSourceValue( "Vca3", 0 ); session.AdvanceTo( 40e-6 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[0], .1 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[1], .1 );
    session.SetSourceValue( "Vca3", 5 ); session.AdvanceTo( 60e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[1], .1 );
    session.SetSourceValue( "Vca2", 0 ); session.AdvanceTo( 80e-6 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[0], .1 );
    session.SetSourceValue( "Vca2", 5 ); session.AdvanceTo( 100e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
    session.Stop(); session.AdvanceTo( 20e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[1], .1 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableWorkerAdcConvertsTheActualElectricalInput, PROGRAM_FILES )
{
    auto circuit = AvrCircuit( AvrSymbol( Firmware( "adc-worker", AdcToOutput() ) ) );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( .0005 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[0], .1 );
    session.SetSourceValue( "Vca4", 4 ); session.AdvanceTo( .001 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableElectricalPullupRespondsToButtonAndHighImpedanceAcceptsExternalVoltage,
                         PROGRAM_FILES )
{
    auto u = AvrSymbol( Firmware( "electrical-input", InputToOutput() ) );
    auto circuit = BuildCircuitSnapshot( { Supply( "VCC", "vcc", "5" ),
        Supply( "VRESET", "reset", "5" ), Supply( "VBIAS", "bias", "2.5" ), u,
        { "SW1", "Switch:SW_Push", "Button", { { "1", "input" }, { "2", "0" } } },
        { "Rinput", "Device:R", "1Meg", { { "1", "input" }, { "2", "0" } } },
        { "Rload", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } },
        { "Rbias", "Device:R", "10k", { { "1", "bias" }, { "2", "analog" } } } } );
    SelectSignals( circuit, { "v(" + circuit.pinNodes[3].at( "19" ) + ")",
                               "v(" + circuit.pinNodes[3].at( "4" ) + ")",
                               "v(" + circuit.pinNodes[3].at( "23" ) + ")" } );
    circuit.config.step = circuit.config.maxStep = 1e-6;
    circuit.config.horizon = .001;
    BOOST_REQUIRE_EQUAL( circuit.switches.size(), 1 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( 20e-6 );
    auto values = session.Snapshot().latest.values;
    BOOST_CHECK_GT( values[0], 4.8 ); BOOST_CHECK_GT( values[1], 4 );
    BOOST_CHECK_CLOSE( values[2], 2.5, .001 );
    session.SetSourceValue( circuit.switches[0].controlSource, circuit.switches[0].on );
    session.AdvanceTo( 40e-6 ); values = session.Snapshot().latest.values;
    BOOST_CHECK_LT( values[0], .1 ); BOOST_CHECK_LT( values[1], .1 );
    BOOST_CHECK_CLOSE( values[2], 2.5, .001 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableWorkerPreservesPwmTransitionsThroughTheAnalogSolver, PROGRAM_FILES )
{
    auto circuit = AvrCircuit( AvrSymbol( Firmware( "pwm-worker", PwmOutput() ) ) );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    std::size_t high = 0, low = 0;
    for( int window = 1; window <= 10; ++window )
    {
        session.AdvanceTo( window * .0003 );
        for( const auto& sample : session.Snapshot().history )
        {
            if( sample.time < .001 ) continue;
            if( sample.values[2] > 4.8 ) ++high;
            if( sample.values[2] < .1 ) ++low;
        }
    }
    BOOST_CHECK_GT( high, 10 ); BOOST_CHECK_GT( low, 10 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableUnoSymbolUsesTheSameFirmwareWithItsOwnConnectorMap, PROGRAM_FILES )
{
    CIRCUIT_COMPONENT uno{ "A1", "MCU_Module:Arduino_UNO_R3", "Arduino_UNO_R3",
        { { "5", "vcc" }, { "6", "0" }, { "7", "0" }, { "29", "0" },
          { "3", "reset" }, { "30", "vcc" }, { "28", "led" } } };
    uno.resources["firmware"] = { PathUtf8( Firmware( "uno", DigitalOutput( true ) ) ), "auto", 0 };
    uno = WithOpenPins( std::move( uno ) );
    auto circuit = BuildCircuitSnapshot( { Supply( "VCC", "vcc", "5" ),
        Supply( "VRESET", "reset", "5" ), uno,
        { "Rled", "Device:R", "1k", { { "1", "led" }, { "2", "0" } } } } );
    BOOST_REQUIRE_EQUAL( circuit.config.programmableDevices.size(), 1 );
    BOOST_CHECK_EQUAL( circuit.config.programmableDevices[0].device, "atmega328p" );
    SelectSignals( circuit, { "v(" + circuit.pinNodes[2].at( "28" ) + ")" } );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( 20e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableExternalScriptIsFrozenPerSnapshotAndIndependentOfCpuArchitecture,
                         PROGRAM_FILES )
{
    auto u = AvrSymbol( Firmware( "contract", DigitalOutput( true ), false ) );
    const auto bundled = MODEL_REPOSITORY().SelectModel( u );
    auto model = nlohmann::json::parse( Text( bundled.file ) );
    model["id"] = "synthetic_contract"; model["name"] = "Synthetic runtime contract";
    model["matches"] = nlohmann::json::object();
    model["runtime"]["device"] = "synthetic_contract";
    model["runtime"]["modules"] = { "logic.js" };
    model["runtime"]["firmware_bytes"] = 64;
    for( auto& pin : model["runtime"]["pins"] )
        if( pin["id"] == "PB5" ) pin["id"] = "IO0";
    const auto modelPath = directory / "contract.casim";
    const auto scriptPath = directory / "logic.js";
    std::ofstream( modelPath ) << model.dump( 2 );
    auto script = [&]( bool high )
    {
        std::ofstream( scriptPath ) << R"JS(
            globalThis.createDevice = function(config) {
                let sent=false;
                return {reset:function(){sent=false;},
                        nextEventTime:function(){return 1e30;},
                        advanceTo:function(time,inputs){
                            if(sent)return [];
                            sent=true;
                            return [{pin:"IO0",time:time,drive:)JS" << ( high ? 2 : 1 ) << R"JS(}];
                        }};
            };
        )JS";
    };
    script( true );
    u.modelFile = PathUtf8( modelPath );
    auto high = AvrCircuit( u );
    BOOST_REQUIRE_EQUAL( high.config.programmableDevices.size(), 1 );
    BOOST_CHECK_EQUAL( high.config.programmableDevices[0].device, "synthetic_contract" );
    script( false );
    auto low = AvrCircuit( u );
    BOOST_CHECK_NE( high.config.programmableDevices[0].source,
                    low.config.programmableDevices[0].source );
    fs::remove( scriptPath );
    for( const auto* circuit : { &high, &low } )
    {
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit->netlist, circuit->config ); session.AdvanceTo( 20e-6 );
        const auto output = session.Snapshot().latest.values[0];
        if( circuit == &high ) BOOST_CHECK_GT( output, 4.8 );
        else BOOST_CHECK_LT( output, .1 );
    }
    // Old readers must not silently treat a programmable model as a passive device.
    model["schema_version"] = 1;
    BOOST_CHECK_THROW( MODEL_REPOSITORY::LoadText( model.dump() ), std::runtime_error );
    model["schema_version"] = 2; model["backend"] = "ngspice";
    BOOST_CHECK_THROW( MODEL_REPOSITORY::LoadText( model.dump() ), std::runtime_error );
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePicExecutesRealHexAndBinaryWithIndependentImages, PROGRAM_FILES )
{
    auto high = PicConfig( Firmware( "pic-high", PicDigitalOutput( true ) ) );
    auto low = PicConfig( Firmware( "pic-low", PicDigitalOutput( false ), false ) ); low.id = "U2";
    PROGRAMMABLE_RUNTIME runtime; runtime.Configure( { high, low } );
    auto inputs = PicInputs();
    auto second = PicInputs(); for( auto& input : second ) input.device = "U2";
    inputs.insert( inputs.end(), second.begin(), second.end() );
    runtime.AdvanceTo( 20e-6, inputs );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH );
    BOOST_CHECK( PinDrive( runtime, "RB0", "U2" ) == PROGRAMMABLE_DRIVE::LOW );
    BOOST_CHECK( PinDrive( runtime, "RA0" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );

    runtime.Configure( { PicConfig( Firmware( "pic-arithmetic", PicArithmeticOutput() ) ) } );
    runtime.AdvanceTo( 30e-6, PicInputs() );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH );
    BOOST_CHECK( PinDrive( runtime, "RB1" ) == PROGRAMMABLE_DRIVE::LOW );
    BOOST_CHECK( PinDrive( runtime, "RB2" ) == PROGRAMMABLE_DRIVE::HIGH );
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePicInputsPullupResetAndTimerUsePicRegisters, PROGRAM_FILES )
{
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( { PicConfig( Firmware( "pic-input", PicInputToOutput() ) ) } );
    runtime.AdvanceTo( 30e-6, PicInputs() );
    BOOST_CHECK( PinDrive( runtime, "RB1" ) == PROGRAMMABLE_DRIVE::PULL_UP );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH );
    runtime.AdvanceTo( 60e-6, PicInputs( 0 ) );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::LOW );
    auto inputs = PicInputs();
    for( auto& input : inputs ) if( input.pin == "RESET" ) input.voltage = 0;
    runtime.AdvanceTo( 70e-6, inputs );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );
    runtime.AdvanceTo( 100e-6, PicInputs() );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH );

    for( bool timer : { false, true } )
    {
        runtime.Configure( { PicConfig( Firmware( timer ? "pic-timer" : "pic-blink",
                                    timer ? PicTimerOutput() : PicBlinkOutput() ) ) } );
        std::vector<PROGRAMMABLE_PIN_STATE> edges;
        auto inputs = PicInputs();
        inputs.push_back( { "U1", "RB0", 0 } );
        // PIC read-modify-write instructions read the electrical PORT, so feed
        // its output back through accepted input windows, as the solver does.
        for( int window = 1; window <= 1200; ++window )
        {
            const auto events = runtime.AdvanceTo( window * 1e-6, inputs );
            for( const auto& event : events )
                if( event.pin == "RB0" && event.time > 10e-6 ) edges.push_back( event );
            inputs.back().voltage = PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH ? 5 : 0;
        }
        BOOST_REQUIRE_GE( edges.size(), 4 );
        for( std::size_t i = 1; i < edges.size(); ++i )
        {
            BOOST_CHECK( edges[i].drive != edges[i-1].drive );
            BOOST_CHECK_CLOSE( edges[i].time - edges[i-1].time, timer ? 256e-6 : 197e-6, 4 );
        }
    }
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePicConfigurationAndEepromHexRegionsAreHandledExplicitly, PROGRAM_FILES )
{
    const auto file = Firmware( "pic-config", PicDigitalOutput( true ) );
    std::string hex = Text( file ); hex.erase( hex.find( ":00000001FF" ) );
    hex += ":02400E00FB3F76\n:0242000042007A\n:00000001FF\n";
    std::ofstream( file ) << hex;
    PROGRAMMABLE_RUNTIME runtime;
    auto config = PicConfig( file );
    BOOST_CHECK_EQUAL( config.firmware[0x400e], 0xfb );
    BOOST_CHECK_EQUAL( config.firmware[0x4200], 0x42 );
    runtime.Configure( { config } ); runtime.AdvanceTo( 20e-6, PicInputs() );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::HIGH );
    // Watchdog-enabled firmware is rejected until that peripheral is implemented.
    config.firmware[0x400e] = 0xff;
    BOOST_CHECK_THROW( runtime.Configure( { config } ), std::runtime_error );

    const std::vector<std::uint16_t> eepromRead = { 0x1683, 0x0186, 0x1283, 0x3000,
        0x0089, 0x1683, 0x1408, 0x1283, 0x0808, 0x0086, 0x280a };
    config = PicConfig( Firmware( "pic-eeprom", eepromRead ) );
    config.firmware[0x4200] = 0x42; config.firmware[0x4201] = 0;
    runtime.Configure( { config } ); runtime.AdvanceTo( 30e-6, PicInputs() );
    BOOST_CHECK( PinDrive( runtime, "RB0" ) == PROGRAMMABLE_DRIVE::LOW );
    BOOST_CHECK( PinDrive( runtime, "RB1" ) == PROGRAMMABLE_DRIVE::HIGH );
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePicWorkerUsesItsPhysicalPinsAndFirmwareControlledDirection, PROGRAM_FILES )
{
    CIRCUIT_COMPONENT pic{ "U1", "MCU_Microchip_PIC16:PIC16F84A-04P", "PIC16F84A",
        { { "14", "vcc" }, { "5", "0" }, { "4", "reset" }, { "6", "out" }, { "7", "input" } } };
    pic.modelFile = "builtin:mcu_pic16f84a_p";
    pic.resources["firmware"] = { PathUtf8( Firmware( "pic-worker", PicInputToOutput() ) ), "auto", 0 };
    pic = WithOpenPins( std::move( pic ) );
    auto circuit = BuildCircuitSnapshot( { Supply( "VCC", "vcc", "5" ),
        Supply( "VRESET", "reset", "5" ), Supply( "VIN", "input", "5" ), pic,
        { "Rload", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } } } );
    BOOST_REQUIRE_EQUAL( circuit.config.programmableDevices.size(), 1 );
    BOOST_CHECK_EQUAL( circuit.config.programmableDevices[0].device, "pic16f84a" );
    SelectSignals( circuit, { "v(" + circuit.pinNodes[3].at( "6" ) + ")" } );
    circuit.config.step = circuit.config.maxStep = 1e-6; circuit.config.horizon = .001;
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config ); session.AdvanceTo( 30e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
    session.SetSourceValue( "Vca3", 0 ); session.AdvanceTo( 60e-6 );
    BOOST_CHECK_LT( session.Snapshot().latest.values[0], .1 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammablePlayPauseAndStopKeepFirmwareLifecycleConsistent, PROGRAM_FILES )
{
    auto circuit = AvrCircuit( AvrSymbol( Firmware( "lifecycle", DigitalOutput( true ) ) ) );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.Play();
    auto playing = session.Snapshot();
    BOOST_CHECK( playing.state == SIM_SESSION_STATE::RUNNING );
    BOOST_CHECK_GT( playing.latest.time, 0 );
    BOOST_CHECK_GT( playing.latest.values[0], 4.8 );
    session.Pause();
    auto paused = session.Snapshot();
    BOOST_CHECK( paused.state == SIM_SESSION_STATE::PAUSED );
    BOOST_CHECK_EQUAL( paused.latest.time, session.Snapshot().latest.time );
    session.Play();
    BOOST_CHECK_GT( session.Snapshot().latest.time, paused.latest.time );
    session.Pause(); session.Stop();
    session.AdvanceTo( 20e-6 );
    BOOST_CHECK_GT( session.Snapshot().latest.values[0], 4.8 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableEditorRunnerRemainsResponsiveToPauseAndStop, PROGRAM_FILES )
{
    auto circuit = AvrCircuit( AvrSymbol( Firmware( "runner", BlinkOutput() ) ) );
    SESSION_RUNNER runner( CIRCUITALIVE_TEST_WORKER );
    auto wait = [&]( auto condition )
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 3 );
        while( std::chrono::steady_clock::now() < deadline )
        {
            const auto view = runner.View();
            BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
            if( condition( view ) ) return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        return false;
    };
    runner.Start( std::move( circuit ) );
    BOOST_REQUIRE( wait( []( const auto& v ) { return v.sample.latest.time > 20e-6; } ) );
    runner.Pause();
    BOOST_REQUIRE( wait( []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::PAUSED; } ) );
    const auto paused = runner.View().sample.latest.time;
    runner.Resume();
    BOOST_REQUIRE( wait( [&]( const auto& v ) { return v.sample.latest.time > paused; } ) );
    runner.Stop();
    BOOST_REQUIRE( wait( []( const auto& v ) { return v.state == EDITOR_SESSION_STATE::STOPPED; } ) );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableAvrTimerOverflowInterruptControlsTheBlinkPinAtFirmwareTime, PROGRAM_FILES )
{
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( { AvrConfig( Firmware( "timer-blink", TimerInterruptOutput() ) ) } );
    std::vector<PROGRAMMABLE_PIN_STATE> edges;
    for( int window = 1; window <= 450; ++window )
        for( const auto& event : runtime.AdvanceTo( window * 10e-6, AvrInputs() ) )
            if( event.pin == "PB5" && event.time > 1e-5 ) edges.push_back( event );
    BOOST_REQUIRE_EQUAL( edges.size(), 4 );
    for( std::size_t i = 1; i < edges.size(); ++i )
    {
        BOOST_CHECK( edges[i].drive != edges[i-1].drive );
        BOOST_CHECK_CLOSE( edges[i].time - edges[i-1].time, 1024e-6, .02 );
    }
    BOOST_CHECK_CLOSE( edges.front().time, 1024e-6, 1 );
}

BOOST_FIXTURE_TEST_CASE( ProgrammableAvrClockPrescalerKeepsInterruptTimeAcrossWindowsAndPowerCycles, PROGRAM_FILES )
{
    auto program = TimerInterruptOutput();
    // Unlock CLKPR, then select CPU clock /2 within the four-cycle write window.
    // The ISR moves by six words; its Timer0 period must become 2048 us.
    program.insert( program.begin() + 34,
                    { Ldi( 16, 0x80 ), 0x9300, 0x0061, Ldi( 16, 1 ), 0x9300, 0x0061 } );
    program[32] = Rjmp( 22 );
    const auto firmware = Firmware( "prescaled-timer-blink", program );
    std::vector<PROGRAMMABLE_PIN_STATE> reference;
    for( const double window : { 10e-9, 100e-9, 1e-6, 1.234567e-6, 10e-6 } )
    {
        BOOST_TEST_CONTEXT( "initial window=" << window )
        {
            PROGRAMMABLE_RUNTIME runtime;
            runtime.Configure( { AvrConfig( firmware ) } );
            std::vector<PROGRAMMABLE_PIN_STATE> edges;
            auto advance = [&]( double time, const auto& inputs, double after )
            {
                for( const auto& event : runtime.AdvanceTo( time, inputs ) )
                    if( event.pin == "PB5" && event.time > after ) edges.push_back( event );
            };
            // Small and non-integral windows exercise instruction boundaries
            // while CLKPR changes the conversion between cycles and seconds.
            for( int sample = 1; sample <= 200; ++sample )
                advance( sample * window, AvrInputs(), 1e-5 );
            advance( .0045, AvrInputs(), 1e-5 );
            BOOST_REQUIRE_EQUAL( edges.size(), 2 );
            BOOST_CHECK_CLOSE( edges[1].time - edges[0].time, .002048, .0001 );

            auto unpowered = AvrInputs();
            for( auto& input : unpowered ) if( input.pin == "VCC" ) input.voltage = 0;
            runtime.AdvanceTo( .004501, unpowered );
            BOOST_CHECK( PinDrive( runtime, "PB5" ) == PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE );
            advance( .0091, AvrInputs(), .004501 + 1e-5 );
            BOOST_REQUIRE_EQUAL( edges.size(), 4 );
            BOOST_CHECK_SMALL( edges[2].time - .004501 - edges[0].time, 1e-14 );
            BOOST_CHECK_CLOSE( edges[3].time - edges[2].time, .002048, .0001 );
            if( reference.empty() ) reference = edges;
            else for( std::size_t i = 0; i < edges.size(); ++i )
            {
                BOOST_CHECK( edges[i].drive == reference[i].drive );
                BOOST_CHECK_SMALL( edges[i].time - reference[i].time, 1e-14 );
            }
        }
    }
}

BOOST_AUTO_TEST_CASE( ProgrammableInputSnapshotsRemainImmutableWhenReusedOrReplaced )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id = "U1"; config.device = "input_snapshot"; config.pins = { "IN", "OUT" };
    config.source = R"JS(
        globalThis.createDevice = function() {
            let previous, previousValue;
            return {nextEventTime:()=>Infinity, advanceTo:function(time,inputs) {
                if(!Object.isFrozen(inputs)||!inputs.every(Object.isFrozen))
                    throw new Error('Electrical input snapshot must be immutable');
                if(previous&&previous[0].voltage!==previousValue)
                    throw new Error('A retained electrical snapshot changed');
                const value=inputs[0].voltage;
                previous=inputs; previousValue=value;
                return [{pin:'OUT',drive:value>2.5?2:1,time:time}];
            }};
        };
    )JS";
    PROGRAMMABLE_RUNTIME runtime; runtime.Configure( { config } );
    runtime.AdvanceTo( 1e-6, { { "U1", "IN", 1 } } );
    BOOST_CHECK( PinDrive( runtime, "OUT" ) == PROGRAMMABLE_DRIVE::LOW );
    runtime.AdvanceTo( 2e-6, { { "U1", "IN", 1 } } );
    BOOST_CHECK( PinDrive( runtime, "OUT" ) == PROGRAMMABLE_DRIVE::LOW );
    runtime.AdvanceTo( 3e-6, { { "U1", "IN", 5 } } );
    BOOST_CHECK( PinDrive( runtime, "OUT" ) == PROGRAMMABLE_DRIVE::HIGH );
    runtime.AdvanceTo( 4e-6, { { "U1", "IN", 0 } } );
    BOOST_CHECK( PinDrive( runtime, "OUT" ) == PROGRAMMABLE_DRIVE::LOW );
}

BOOST_AUTO_TEST_CASE( ProgrammableStopConditionsKeepPrecisionAcrossLongTimeRanges )
{
    // A small deterministic backend isolates the solver/stop-parser handoff
    // from firmware execution cost. It emits exact half-second output edges.
    PROGRAMMABLE_DEVICE_CONFIG device;
    device.id = "CLOCK"; device.device = "synthetic-clock";
    device.pins = { "OUT" };
    device.quantumSeconds = 1e-6;
    device.autonomousWindowSeconds = .001;
    device.bindings = { { "OUT", "v(out)", "", "", "", 0 } };
    device.source = R"JS(
        globalThis.createDevice = function() {
            let next = 0;
            function prepare(time) {
                const events = [];
                while(next <= time) {
                    events.push({pin:'OUT',drive:(Math.round(next*2)%2)?1:2,time:next});
                    next += .5;
                }
                return {time,events,needsInputs:false};
            }
            return {nextEventTime:()=>Infinity,advanceTo:t=>prepare(t).events,prepareWindow:prepare};
        };
    )JS";
    SIM_SESSION_CONFIG config;
    config.step = config.maxStep = 100e-6;
    config.horizon = 130;
    config.signals = { "v(out)" };
    config.programmableDevices = { device };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( "Long-time stop precision\nVcc vcc 0 5\n"
        "Aout %gd out 0 %gd out vcc gpio\n.model gpio ca_programmable_pin(slot=0)\n"
        "Rload out 0 10k\n.end\n", config );
    for( const double origin : { 32., 64., 128. } )
    {
        const double target = origin + .008041666662212;
        session.AdvanceTo( target );
        const auto high = session.Snapshot( true );
        BOOST_REQUIRE_SMALL( high.latest.time - target, 1e-10 );
        BOOST_REQUIRE_GT( high.latest.values[0], 4.9 );
        session.AdvanceTo( origin + .504041666662212 );
        const auto low = session.Snapshot( true );
        BOOST_REQUIRE_SMALL( low.latest.time - origin - .504041666662212, 1e-10 );
        BOOST_REQUIRE_LT( low.latest.values[0], .1 );
    }
}

BOOST_FIXTURE_TEST_CASE( ProgrammableRepeatedEditorBatchesKeepAnalogSourcesConvergent, PROGRAM_FILES )
{
    // A sine source AC-coupled into an amplifier exposed a tiny saved timestep
    // when pausing slightly before an MCU boundary. The next editor batch
    // failed near 0.125 s with "Timestep too small" on the source's branch.
    // Exercise both the default MCU period and a slower period with finer
    // electrical steps, as required by other models in a mixed circuit.
    for( const double quantum : { 1e-6, 100e-6 } ) BOOST_TEST_CONTEXT( "sampling=" << quantum )
    {
        CIRCUIT_COMPONENT uno{ "A1", "MCU_Module:Arduino_UNO_R3", "Arduino_UNO_R3",
            { { "5", "vcc" }, { "6", "0" }, { "7", "0" }, { "29", "0" }, { "28", "out" } } };
        uno.resources["firmware"] = { PathUtf8( Firmware( "batch-output", DigitalOutput( true ) ) ), "ihex", 0 };
        uno.modelParameters["sample_period"] = quantum == 1e-6 ? "1u" : "100u";
        CIRCUIT_COMPONENT source{ "VIN", "Simulation_SPICE:VSIN", "VSIN",
            { { "1", "signal" }, { "2", "0" } } };
        source.modelParameters = { { "amplitude", "0.1" }, { "frequency", "1000" } };
        auto circuit = BuildCircuitSnapshot( { Supply( "VCC", "vcc", "5" ), WithOpenPins( uno ),
            { "Rout", "Device:R", "10k", { { "1", "out" }, { "2", "0" } } }, source,
            { "Q1", "Transistor_BJT:BC548", "BC548",
                { { "1", "collector" }, { "2", "base" }, { "3", "emitter" } } },
            { "Cin", "Device:C", "10u", { { "1", "signal" }, { "2", "base" } } },
            { "Rbias", "Device:R", "47k", { { "1", "vcc" }, { "2", "base" } } },
            { "Cout", "Device:C", "10u", { { "1", "collector" }, { "2", "audio" } } },
            { "Rc", "Device:R", "2.2k", { { "1", "vcc" }, { "2", "collector" } } },
            { "Rl", "Device:R", "100k", { { "1", "audio" }, { "2", "0" } } },
            { "Re", "Device:R", "470", { { "1", "emitter" }, { "2", "0" } } },
            { "Rb", "Device:R", "15k", { { "1", "base" }, { "2", "0" } } } } );
        SelectSignals( circuit, { "v(" + circuit.pinNodes[1].at( "28" ) + ")",
                                 "v(" + circuit.pinNodes[3].at( "1" ) + ")",
                                 "i(Vca4)" } );
        // Deterministic initialization edges isolate the electrical scheduler
        // from CPU execution speed and external Arduino firmware/toolchains.
        circuit.config.programmableDevices[0].source = R"JS(
            globalThis.createDevice = function() {
                let next = 0;
                const edges = [{pin:'PB5',drive:1,time:11.25e-6},
                               {pin:'PB5',drive:2,time:14.4375e-6}];
                return {nextEventTime:()=>Infinity,advanceTo:function(t) {
                    const out=[];
                    while(next<edges.length && edges[next].time<=t)out.push(edges[next++]);
                    return out;
                }};
            };
        )JS";
        circuit.config.step = circuit.config.maxStep = 1e-6;
        circuit.config.horizon = 1;
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( circuit.netlist, circuit.config );
        const double until = .15;
        for( double target = circuit.config.step; ; )
        {
            session.AdvanceTo( target );
            const auto snapshot = session.Snapshot( true );
            const auto& sample = snapshot.latest;
            BOOST_REQUIRE( snapshot.state == SIM_SESSION_STATE::PAUSED );
            BOOST_REQUIRE_SMALL( sample.time - target, 1e-10 );
            if( sample.time > 20e-6 ) BOOST_REQUIRE_GT( sample.values[0], 4.8 );
            const double expected = .1 * std::sin( 2 * std::acos( -1. ) * 1000 * sample.time );
            BOOST_REQUIRE_SMALL( sample.values[1] - expected, 1e-8 );
            for( const double value : sample.values ) BOOST_REQUIRE( std::isfinite( value ) );
            if( target >= until ) break;
            // Use the returned time, exactly like SESSION_RUNNER, including
            // the stop-condition rounding and 512-window batch limit.
            target = std::min( until, sample.time + std::min( .008, quantum * 512 ) );
        }
        if( quantum == 100e-6 )
        {
            // A requested pause breakpoint can differ from the rounded stop
            // expression by a few ULPs. It must not leave a femtosecond step
            // for the next request, including with the sine source's branch.
            session.AdvanceTo( .5 );
            session.AdvanceTo( .500017 );
            const auto sample = session.Snapshot( true ).latest;
            BOOST_CHECK_SMALL( sample.time - .500017, 1e-10 );
            for( const double value : sample.values ) BOOST_CHECK( std::isfinite( value ) );
        }
    }
}
