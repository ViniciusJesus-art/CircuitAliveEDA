/* CircuitAliveEDA AVR8js native instruction accelerator.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Instruction semantics are generated from the vendored MIT AVR8js source.
 */
#include "avr_accelerator.h"
#include <quickjs.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <limits>

namespace CIRCUITALIVE
{
namespace
{
struct BYTES
{
    std::uint8_t* bytes;
    std::size_t length;
    std::uint8_t ignored = 0;
    std::uint8_t& operator[]( int index )
    {
        if( index >= 0 && static_cast<std::size_t>( index ) < length ) return bytes[index];
        ignored = 0;
        return ignored;
    }
};
// Execute validates the complete 256-byte register/I/O window up front.
// Register operands are masked into that window by the decoder, so their
// frequent accesses need no repeated comparison with the SRAM allocation.
struct DATA_BYTES : BYTES
{
    std::uint8_t& operator[]( int index )
    {
        if( static_cast<unsigned>( index ) < 256 ) return bytes[index];
        return BYTES::operator[]( index );
    }
};
struct WORDS
{
    BYTES& bytes;
    int operator[]( int index ) { return bytes[index * 2] | ( bytes[index * 2 + 1] << 8 ); }
};
struct DATA_VIEW
{
    DATA_BYTES& data;
    int getUint16( int index, bool ) { return data[index] | ( data[index + 1] << 8 ); }
    int getInt8( int index ) { return data[index] < 128 ? data[index] : data[index] - 256; }
    void setUint16( int index, int value, bool ) { data[index] = value; data[index + 1] = value >> 8; }
    void setInt16( int index, int value, bool little ) { setUint16( index, value, little ); }
};
struct CPU
{
    struct GPIO_WRITE { int address,value;double cycles; };
    DATA_BYTES data;
    BYTES progBytes;
    WORDS progMem{ progBytes };
    DATA_VIEW dataView{ data };
    int pc;
    double cycles;
    bool pc22Bits = false;
    int counterBase = -1;
    double counterCycle = 0;
    int counterDivider = 0;
    bool plainIOReads = false;
    bool batchGPIO = false;
    bool unchangedTimerControls = false;
    std::vector<GPIO_WRITE> gpioWrites;
    std::array<std::uint8_t,6> gpioBefore{};
    CPU( std::uint8_t* memory, std::size_t memorySize, std::uint8_t* flash, std::size_t flashSize,
         int programCounter, double cycle ) : data{memory,memorySize}, progBytes{flash,flashSize},
         pc(programCounter), cycles(cycle) {}
    int readData( int address )
    {
        if( address == 0x46 && counterBase >= 0 )
            data[address] = ( counterBase + static_cast<int>( ( cycles - counterCycle ) / counterDivider ) ) & 255;
        return data[address];
    }
    void writeData( int address, int value, int = 255 )
    {
        if(batchGPIO&&value!=data[address]&&(address==0x24||address==0x25||address==0x27||address==0x28||address==0x2a||address==0x2b))
            gpioWrites.push_back({address,value,cycles});
        data[address] = value;
    }
    void onWatchdogReset() {}
};
bool isTwoWordInstruction( int opcode )
{
    return ( opcode & 0xfe0f ) == 0x9000 || ( opcode & 0xfe0f ) == 0x9200
        || ( opcode & 0xfe0e ) == 0x940e || ( opcode & 0xfe0e ) == 0x940c;
}

// Return before any peripheral access, including indirect LDS/STS and pointer
// pre-decrements. Thus every hook runs in JS at its original instruction time.
bool PeripheralAccess( CPU& cpu, int opcode )
{
    // All indirect/direct data and I/O opcodes occupy 0x8000..0xbfff.
    // The arithmetic/control group 0x9400..0x97ff has no peripheral operand.
    // Keep the detailed decoder off the common register/branch path.
    if( ( opcode & 0xc000 ) != 0x8000 || ( opcode & 0xfc00 ) == 0x9400 ) return false;
    int address = -1;
    if( ( opcode & 0xf000 ) == 0xb000 )
        address = ( opcode & 15 ) + ( ( opcode & 0x600 ) >> 5 ) + 32;
    else if( ( opcode & 0xfc00 ) == 0x9800 )
        address = ( ( opcode & 0xf8 ) >> 3 ) + 32;
    else if( ( opcode & 0xfc0f ) == 0x9000 )
        address = cpu.progMem[cpu.pc + 1];
    else if( ( opcode & 0xfc0c ) == 0x900c && ( opcode & 3 ) != 3 )
        address = cpu.dataView.getUint16( 26, true ) - ( ( opcode & 3 ) == 2 ? 1 : 0 );
    else if( ( opcode & 0xfc0e ) == 0x9008 && ( opcode & 1 ) )
        address = cpu.dataView.getUint16( 28, true );
    else if( ( opcode & 0xfc0f ) == 0x900a )
        address = cpu.dataView.getUint16( 28, true ) - 1;
    else if( ( opcode & 0xfc0e ) == 0x9000 && ( opcode & 1 ) )
        address = cpu.dataView.getUint16( 30, true );
    else if( ( opcode & 0xfc0f ) == 0x9002 )
        address = cpu.dataView.getUint16( 30, true ) - 1;
    else if( ( opcode & 0xd000 ) == 0x8000 )
        address = cpu.dataView.getUint16( opcode & 8 ? 28 : 30, true )
            + ( ( opcode & 7 ) | ( ( opcode & 0xc00 ) >> 7 ) | ( ( opcode & 0x2000 ) >> 8 ) );
    else if( ( opcode & 0xfe0c ) == 0x9204 )
        address = cpu.dataView.getUint16( 30, true );
    // Timer0 flags are plain read-only observations until the next scheduled
    // clock event. Arduino micros()/delay() polls them with SBIS; handing every
    // poll back to JS defeats native execution of an otherwise quiet loop.
    // The adapter only supplies counterBase when this register has no read hook.
    const bool read = ( opcode & 0xf800 ) == 0xb000 || ( opcode & 0xfe00 ) == 0x9000
                      || ( opcode & 0xd200 ) == 0x8000 || ( opcode & 0xfd00 ) == 0x9900;
    const bool counterRead = address == 0x46 && cpu.counterBase >= 0
        && ( ( opcode & 0xf800 ) == 0xb000 || ( opcode & 0xd200 ) == 0x8000 );
    const bool timerFlagRead = address == 0x35 && cpu.counterBase >= 0 && read;
    const bool plainRead=cpu.plainIOReads && read && (address==0x24||address==0x25
        ||address==0x27||address==0x28||address==0x2a||address==0x2b
        ||address==0x44||address==0x80||address==0xb0);
    const bool gpioWrite=cpu.batchGPIO && !read && (address==0x24||address==0x25
        ||address==0x27||address==0x28||address==0x2a||address==0x2b);
    // PB0 also feeds Timer1 capture. Return to the full peripheral backend
    // before enabling its output/pull-up, even if it was an open pad so far.
    bool captureChange=false;
    if(gpioWrite&&(address==0x24||address==0x25))
    {
        int value=cpu.data[(opcode&0x1f0)>>4];
        if((opcode&0xff00)==0x9800)value=cpu.data[address]&~(1<<((opcode>>0)&7));
        if((opcode&0xff00)==0x9a00)value=cpu.data[address]|(1<<((opcode>>0)&7));
        captureChange=(value^cpu.data[address])&1;
        if((opcode&0xfe0c)==0x9204)captureChange=true;
    }
    // TCCRxA only stores compare-output and waveform-mode configuration.
    // Rewriting the same bits has no hardware effect. Other control/flag
    // registers, asynchronous Timer2 and custom hooks stay in the adapter.
    const bool store=(opcode&0xf800)==0xb800||(opcode&0xfe0f)==0x9200;
    const bool unchangedControl=cpu.unchangedTimerControls&&store
        &&(address==0x44||address==0x80||address==0xb0)
        &&cpu.data[(opcode&0x1f0)>>4]==cpu.data[address];
    return address >= 32 && address < 256 && !( address >= 93 && address <= 95 )
           && !counterRead && !timerFlagRead && !plainRead && !(gpioWrite&&!captureChange)&&!unchangedControl;
}

bool Instruction( CPU& cpu, int opcode, double deadline = std::numeric_limits<double>::infinity() )
{
#include "avr_instruction.inc"
    ++cpu.pc;
    const int words=static_cast<int>(cpu.progBytes.length/2);
    if(cpu.pc<0||cpu.pc>=words)cpu.pc%=words;
    ++cpu.cycles;
    return true;
}

// Count pure busy-wait iterations arithmetically, stopping before the next
// hardware deadline. Execute the final iteration normally so every SREG bit,
// PC and cycle matches the instruction decoder, including signed overflow.
int DelayLoop( CPU& cpu, int opcode, double limit, int budget )
{
    if(opcode==0xcfff) // RJMP to this same instruction; no register/flag changes.
    {
        const int count=std::min(budget,static_cast<int>((limit-cpu.cycles-6)/2));
        if(count>1){cpu.cycles+=count*2;return count;}
        return 0;
    }
    const bool word=(opcode&0xffcf)==0x9701; // SBIW r24/26/28/30, 1.
    const bool byte=(opcode&0xfe0f)==0x940a; // DEC r0..r31.
    if(!word&&!byte)return 0;
    if(cpu.progMem[cpu.pc+1]!=0xf7f1)return 0; // BRNE back to the previous instruction.
    const int reg=word?24+((opcode>>3)&6):(opcode>>4)&31;
    const int value=cpu.data[reg]+(word?(cpu.data[reg+1]<<8):0),cycles=word?4:3;
    const int iterations=std::min({budget/2,value-1,static_cast<int>((limit-cpu.cycles-6)/cycles)});
    if(iterations<=1)return 0;
    const int beforeLast=value-iterations+1;
    cpu.data[reg]=beforeLast;if(word)cpu.data[reg+1]=beforeLast>>8;
    cpu.cycles+=(iterations-1)*cycles;
    Instruction(cpu,opcode);Instruction(cpu,0xf7f1);
    return iterations*2;
}

JSValue Execute( JSContext* context, JSValueConst, int argc, JSValueConst* args )
{
    if( argc < 8 || argc > 10 ) return JS_ThrowTypeError( context, "Expected AVR memory, flash, CPU deadline and counter state" );
    for( int i = 2; i < argc; ++i )
        if( !JS_IsNumber( args[i] ) ) return JS_ThrowTypeError( context, "AVR execution arguments must be numbers" );
    std::size_t dataOffset, dataLength, dataElement, flashOffset, flashLength, flashElement;
    JSValue dataBuffer = JS_GetTypedArrayBuffer( context, args[0], &dataOffset, &dataLength, &dataElement );
    if( JS_IsException( dataBuffer ) ) return dataBuffer;
    JSValue flashBuffer = JS_GetTypedArrayBuffer( context, args[1], &flashOffset, &flashLength, &flashElement );
    if( JS_IsException( flashBuffer ) ) { JS_FreeValue( context, dataBuffer ); return flashBuffer; }
    std::size_t dataSize = 0, flashSize = 0;
    auto* data = JS_GetArrayBuffer( context, &dataSize, dataBuffer );
    auto* flash = JS_GetArrayBuffer( context, &flashSize, flashBuffer );
    int pc = 0;
    double cycles = 0, limit = 0;
    int counterBase = -1, counterDivider = 0;
    int plainIOReads = 0,batchGPIO=0;
    double counterCycle = 0;
    bool valid = data && flash && dataElement == 1 && flashElement == 1
        && dataOffset <= dataSize && dataLength <= dataSize - dataOffset
        && flashOffset <= flashSize && flashLength <= flashSize - flashOffset
        && dataLength >= 256 && dataLength <= 65536 && flashLength >= 2 && flashLength <= 131072
        && flashLength % 2 == 0 && JS_ToInt32( context, &pc, args[2] ) == 0
        && JS_ToFloat64( context, &cycles, args[3] ) == 0 && JS_ToFloat64( context, &limit, args[4] ) == 0
        && pc >= 0 && pc < static_cast<int>( flashLength / 2 )
        && std::isfinite( cycles ) && cycles >= 0 && cycles <= 9007199253740991.0
        && std::isfinite( limit ) && limit >= cycles && limit - cycles <= 1000000;
    valid = valid && JS_ToInt32( context, &counterBase, args[5] ) == 0
        && JS_ToFloat64( context, &counterCycle, args[6] ) == 0
        && JS_ToInt32( context, &counterDivider, args[7] ) == 0
        && counterBase >= -1 && counterBase <= 255 && std::isfinite( counterCycle )
        && counterCycle >= 0 && counterCycle <= cycles
        && ( counterBase == -1 || ( counterDivider > 0 && counterDivider <= 1024 ) );
    if(argc>=9)valid=valid&&JS_ToInt32(context,&plainIOReads,args[8])==0&&(plainIOReads==0||plainIOReads==1);
    if(argc==10)valid=valid&&JS_ToInt32(context,&batchGPIO,args[9])==0&&batchGPIO>=0&&batchGPIO<=7;
    JSValue result;
    if( !valid ) result = JS_ThrowTypeError( context, "Invalid AVR execution buffer or deadline" );
    else
    {
        CPU cpu(data+dataOffset,dataLength,flash+flashOffset,flashLength,pc,cycles);
        cpu.counterBase = counterBase; cpu.counterCycle = counterCycle; cpu.counterDivider = counterDivider;
        cpu.plainIOReads=plainIOReads;
        cpu.batchGPIO=batchGPIO&1;cpu.unchangedTimerControls=batchGPIO&2;
        const bool fillDeadline=batchGPIO&4;
        constexpr std::array<int,6> gpioAddresses{0x24,0x25,0x27,0x28,0x2a,0x2b};
        if(batchGPIO)for(unsigned i=0;i<6;++i)cpu.gpioBefore[i]=cpu.data[gpioAddresses[i]];
        // Legacy callers keep a five-cycle guard. The adapter can fill that
        // guard using conservative cycle bounds generated with the decoder.
        int count = 0;
        for( ; count < 32768 && cpu.cycles + (fillDeadline?0:5) < limit; ++count )
        {
            if( cpu.pc < 0 || cpu.pc + 1 >= static_cast<int>( flashLength / 2 ) ) break;
            int opcode = cpu.progMem[cpu.pc];
            if( opcode == 0xffff || opcode == 0x9588 || opcode == 0x95e8
                || opcode == 0x95f8 || opcode == 0x95a8 || PeripheralAccess( cpu, opcode ) ) break;
            // SEI/RETI suppress interrupt entry for their following tick. Keep
            // that boundary in the adapter when filling right up to an event.
            if(fillDeadline&&(opcode==0x9478||opcode==0x9518))break;
            // Leave invalid/extended flash operations to the reference engine.
            if( opcode == 0x9519 || opcode == 0x9419 || opcode == 0x95d8
                || ( opcode & 0xfe0e ) == 0x9006 ) break;
            if(const int skipped=DelayLoop(cpu,opcode,limit,32768-count))
            {count+=skipped-1;continue;}
            if( !Instruction( cpu, opcode, fillDeadline?limit:std::numeric_limits<double>::infinity() ) ) break;
        }
        result = JS_NewArray( context );
        JS_SetPropertyUint32( context, result, 0, JS_NewInt32( context, cpu.pc ) );
        JS_SetPropertyUint32( context, result, 1, JS_NewFloat64( context, cpu.cycles ) );
        if(!cpu.gpioWrites.empty())
        {
            // Replay only peripheral hooks in JS at their original CPU cycle.
            // Restore register inputs first; native instructions already saw
            // every write, but no hook has observed the final register state.
            for(unsigned i=0;i<6;++i)cpu.data[gpioAddresses[i]]=cpu.gpioBefore[i];
            std::vector<double> packed(cpu.gpioWrites.size()*3);unsigned index=0;
            for(const auto& write:cpu.gpioWrites)
            {
                packed[index++]=write.address;packed[index++]=write.value;packed[index++]=write.cycles;
            }
            JSValue buffer=JS_NewArrayBufferCopy(context,reinterpret_cast<const std::uint8_t*>(packed.data()),packed.size()*sizeof(double));
            JSValue writes=buffer;
            if(!JS_IsException(buffer))
            {
                writes=JS_NewTypedArray(context,1,&buffer,JS_TYPED_ARRAY_FLOAT64);
                JS_FreeValue(context,buffer);
            }
            if(JS_IsException(writes)){JS_FreeValue(context,result);result=writes;}
            else JS_SetPropertyUint32(context,result,2,writes);
        }
        // Native work shares the per-call script budget; a custom module cannot
        // bypass the interpreter watchdog by repeatedly invoking this helper.
        auto* budget = static_cast<int*>( JS_GetContextOpaque( context ) );
        if( budget && ( *budget += ( count + 63 ) / 64 ) > 5000 )
        {
            JS_FreeValue( context, result );
            result = JS_ThrowInternalError( context, "AVR execution budget exceeded" );
        }
    }
    JS_FreeValue( context, flashBuffer ); JS_FreeValue( context, dataBuffer );
    return result;
}
}

void InstallAvrAccelerator( JSContext* context, int* executionBudget )
{
    JS_SetContextOpaque( context, executionBudget );
    JSValue global = JS_GetGlobalObject( context );
    JS_DefinePropertyValueStr( context, global, "__circuitaliveAvrRun",
        JS_NewCFunction( context, Execute, "__circuitaliveAvrRun", 8 ), 0 );
    JS_FreeValue( context, global );
}
}
