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
        ignored = 0;
        return index >= 0 && static_cast<std::size_t>( index ) < length ? bytes[index] : ignored;
    }
};
struct WORDS
{
    BYTES& bytes;
    int operator[]( int index ) { return bytes[index * 2] | ( bytes[index * 2 + 1] << 8 ); }
};
struct DATA_VIEW
{
    BYTES& data;
    int getUint16( int index, bool ) { return data[index] | ( data[index + 1] << 8 ); }
    int getInt8( int index ) { return data[index] < 128 ? data[index] : data[index] - 256; }
    void setUint16( int index, int value, bool ) { data[index] = value; data[index + 1] = value >> 8; }
    void setInt16( int index, int value, bool little ) { setUint16( index, value, little ); }
};
struct CPU
{
    BYTES data, progBytes;
    WORDS progMem{ progBytes };
    DATA_VIEW dataView{ data };
    int pc;
    double cycles;
    bool pc22Bits = false;
    int counterBase = -1;
    double counterCycle = 0;
    int counterDivider = 0;
    CPU( std::uint8_t* memory, std::size_t memorySize, std::uint8_t* flash, std::size_t flashSize,
         int programCounter, double cycle ) : data{memory,memorySize}, progBytes{flash,flashSize},
         pc(programCounter), cycles(cycle) {}
    int readData( int address )
    {
        if( address == 0x46 && counterBase >= 0 )
            data[address] = ( counterBase + static_cast<int>( ( cycles - counterCycle ) / counterDivider ) ) & 255;
        return data[address];
    }
    void writeData( int address, int value, int = 255 ) { data[address] = value; }
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
    // SP and SREG have no hooks in the ATmega adapter. Everything else is
    // dispatched by the reference engine, including counter reads.
    const bool counterRead = address == 0x46 && cpu.counterBase >= 0
        && ( ( opcode & 0xf800 ) == 0xb000 || ( opcode & 0xd200 ) == 0x8000 );
    return address >= 32 && address < 256 && !( address >= 93 && address <= 95 ) && !counterRead;
}

bool Instruction( CPU& cpu, int opcode )
{
#include "avr_instruction.inc"
    cpu.pc = ( cpu.pc + 1 ) % static_cast<int>( cpu.progBytes.length / 2 );
    ++cpu.cycles;
    return true;
}

JSValue Execute( JSContext* context, JSValueConst, int argc, JSValueConst* args )
{
    if( argc != 8 ) return JS_ThrowTypeError( context, "Expected AVR memory, flash, CPU deadline and counter state" );
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
    JSValue result;
    if( !valid ) result = JS_ThrowTypeError( context, "Invalid AVR execution buffer or deadline" );
    else
    {
        CPU cpu(data+dataOffset,dataLength,flash+flashOffset,flashLength,pc,cycles);
        cpu.counterBase = counterBase; cpu.counterCycle = counterCycle; cpu.counterDivider = counterDivider;
        // Never cross an event boundary: AVR instructions take at most 5 cycles.
        int count = 0;
        for( ; count < 32768 && cpu.cycles + 5 < limit; ++count )
        {
            if( cpu.pc < 0 || cpu.pc + 1 >= static_cast<int>( flashLength / 2 ) ) break;
            int opcode = cpu.progMem[cpu.pc];
            if( opcode == 0xffff || opcode == 0x9588 || opcode == 0x95e8
                || opcode == 0x95f8 || opcode == 0x95a8 || PeripheralAccess( cpu, opcode ) ) break;
            // Leave invalid/extended flash operations to the reference engine.
            if( opcode == 0x9519 || opcode == 0x9419 || opcode == 0x95d8
                || ( opcode & 0xfe0e ) == 0x9006 ) break;
            if( !Instruction( cpu, opcode ) ) break;
        }
        result = JS_NewArray( context );
        JS_SetPropertyUint32( context, result, 0, JS_NewInt32( context, cpu.pc ) );
        JS_SetPropertyUint32( context, result, 1, JS_NewFloat64( context, cpu.cycles ) );
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
