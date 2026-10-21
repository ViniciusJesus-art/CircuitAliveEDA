/*
 * CircuitAliveEDA programmable device runtime.
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "programmable_runtime.h"
#include "avr_accelerator.h"

#include <quickjs.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace CIRCUITALIVE
{
namespace
{
using JSON = nlohmann::json;
constexpr std::size_t MAX_SOURCE = 4 * 1024 * 1024;
constexpr std::size_t MAX_EVENTS = 100000;
constexpr int MAX_INTERRUPTS = 5000;

struct VALUE
{
    JSContext* context;
    JSValue value;
    VALUE( JSContext* c, JSValue v ) : context( c ), value( v ) {}
    ~VALUE() { JS_FreeValue( context, value ); }
    VALUE( const VALUE& ) = delete;
};

std::string String( JSContext* context, JSValueConst value )
{
    const char* text = JS_ToCString( context, value );
    if( !text )
        return "JavaScript conversion failed";
    std::string result = text;
    JS_FreeCString( context, text );
    return result;
}

struct DEVICE
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    JSValue object = JS_UNDEFINED;
    JSValue inputCache = JS_UNDEFINED;
    std::vector<PROGRAMMABLE_PIN_INPUT> cachedInputs;
    int interrupts = 0;
    double next = 0.0;
    std::set<std::string> pins;
    std::map<std::string, PROGRAMMABLE_PIN_STATE> states;
    bool needsInputs = true;

    explicit DEVICE( const PROGRAMMABLE_DEVICE_CONFIG& c ) : config( c ) {}
    ~DEVICE()
    {
        if( context )
        {
            JS_UpdateStackTop( runtime );
            JS_FreeValue( context, object );
            JS_FreeValue( context, inputCache );
            JS_FreeContext( context );
        }
        if( runtime )
            JS_FreeRuntime( runtime );
    }

    [[noreturn]] void Error( const std::string& message ) const
    {
        throw std::runtime_error( "Programmable device " + config.id + ": " + message );
    }

    void Check( JSValueConst result )
    {
        if( JS_IsException( result ) )
        {
            VALUE error( context, JS_GetException( context ) );
            std::string message = String( context, error.value );
            VALUE stack( context, JS_GetPropertyStr( context, error.value, "stack" ) );
            if( !JS_IsUndefined( stack.value ) )
                message += "\n" + String( context, stack.value );
            if( interrupts > MAX_INTERRUPTS )
                message = "script execution budget exceeded";
            Error( message );
        }
    }

    JSValue Parse( const JSON& json )
    {
        std::string text = json.dump();
        JSValue result = JS_ParseJSON( context, text.c_str(), text.size(), "device.json" );
        Check( result );
        return result;
    }

    JSValue Call( const char* method, int argc = 0, JSValueConst* argv = nullptr )
    {
        // Configure runs on the control thread; accepted solver callbacks can
        // subsequently advance this context on the ngspice thread, sequentially.
        JS_UpdateStackTop( runtime );
        VALUE fn( context, JS_GetPropertyStr( context, object, method ) );
        if( !JS_IsFunction( context, fn.value ) )
            Error( std::string( "missing synchronous runtime method " ) + method );
        interrupts = 0;
        JSValue result = JS_Call( context, fn.value, object, argc, argv );
        Check( result );
        return result;
    }

    JSValue Inputs( const std::vector<PROGRAMMABLE_PIN_INPUT>& inputs )
    {
        std::size_t count = 0;
        bool same = !JS_IsUndefined( inputCache );
        for( const auto& input : inputs )
        {
            if( input.device != config.id ) continue;
            if( !pins.count( input.pin ) || !std::isfinite( input.voltage ) )
                Error( "unknown pin or non-finite electrical input" );
            same = same && count < cachedInputs.size()
                && input.pin == cachedInputs[count].pin && input.voltage == cachedInputs[count].voltage;
            ++count;
        }
        if( same && count == cachedInputs.size() ) return JS_DupValue( context, inputCache );

        // Inputs are immutable electrical snapshots. Reuse equal snapshots and
        // construct changed ones directly, avoiding JSON encode/parse per us.
        VALUE array( context, JS_NewArray( context ) ); Check( array.value );
        cachedInputs.clear();
        for( const auto& input : inputs )
        {
            if( input.device != config.id ) continue;
            VALUE item( context, JS_NewObject( context ) ); Check( item.value );
            if( JS_SetPropertyStr( context, item.value, "pin",
                    JS_NewStringLen( context, input.pin.data(), input.pin.size() ) ) < 0
                || JS_SetPropertyStr( context, item.value, "voltage", JS_NewFloat64( context, input.voltage ) ) < 0
                || JS_FreezeObject( context, item.value ) < 0
                || JS_SetPropertyUint32( context, array.value, static_cast<std::uint32_t>(cachedInputs.size()),
                    JS_DupValue( context, item.value ) ) < 0 )
                Check( JS_EXCEPTION );
            cachedInputs.push_back( input );
        }
        if( JS_FreezeObject( context, array.value ) < 0 ) Check( JS_EXCEPTION );
        JS_FreeValue( context, inputCache );
        inputCache = JS_DupValue( context, array.value );
        return JS_DupValue( context, array.value );
    }

    void Initialize()
    {
        if( config.id.empty() || config.id.size() > 256 || config.device.empty()
            || config.device.size() > 256 || config.engine != "javascript" )
            Error( "invalid device identity or unsupported execution engine" );
        if( config.source.empty() || config.source.size() > MAX_SOURCE
            || config.firmware.size() > 16 * 1024 * 1024 || config.eeprom.size() > 1024 * 1024 )
            Error( "empty or oversized program/module" );
        if( !std::isfinite( config.clockHz ) || config.clockHz <= 0 || config.clockHz > 1e10
            || !std::isfinite( config.quantumSeconds ) || config.quantumSeconds < 1e-9
            || config.quantumSeconds > 0.01 || config.optionsJson.size() > 1024 * 1024 )
            Error( "invalid clock, sampling interval or options" );
        if( !std::isfinite( config.autonomousWindowSeconds ) || config.autonomousWindowSeconds < 0
            || config.autonomousWindowSeconds > .001 ) Error( "invalid autonomous execution window" );
        if( config.pins.size() > 1024 )
            Error( "too many device pins" );
        for( const std::string& pin : config.pins )
        {
            if( pin.empty() || pin.size() > 256 || !pins.insert( pin ).second )
                Error( "invalid or duplicate pin ID" );
            states[pin] = { config.id, pin, PROGRAMMABLE_DRIVE::HIGH_IMPEDANCE, 0.0 };
        }
        JSON options;
        try
        {
            options = JSON::parse( config.optionsJson );
        }
        catch( const JSON::exception& )
        {
            Error( "invalid runtime options JSON" );
        }
        if( !options.is_object() )
            Error( "runtime options must be an object" );
        runtime = JS_NewRuntime();
        if( !runtime )
            Error( "cannot allocate JavaScript interpreter" );
        JS_SetMemoryLimit( runtime, 64 * 1024 * 1024 );
        JS_SetMaxStackSize( runtime, 512 * 1024 );
        JS_SetCanBlock( runtime, false );
        JS_SetInterruptHandler( runtime, []( JSRuntime*, void* opaque ) -> int {
            return ++static_cast<DEVICE*>( opaque )->interrupts > MAX_INTERRUPTS;
        }, this );
        context = JS_NewContext( runtime );
        if( !context )
            Error( "cannot allocate JavaScript context" );
        InstallAvrAccelerator( context, &interrupts );
        // No libc, std/os modules, importer or event loop is installed. The
        // bounded native AVR helper only operates on validated memory buffers.
        // Remove wall-clock/random/blocking globals to keep execution deterministic.
        static const char sandbox[] =
            "delete globalThis.Date; delete globalThis.Atomics;"
            "delete globalThis.SharedArrayBuffer; delete globalThis.performance; delete Math.random;";
        VALUE restricted( context, JS_Eval( context, sandbox, sizeof( sandbox ) - 1,
                                           "sandbox.js", JS_EVAL_TYPE_GLOBAL ) );
        Check( restricted.value );
        interrupts = 0;
        VALUE script( context, JS_Eval( context, config.source.c_str(), config.source.size(),
                                       "device-module.js", JS_EVAL_TYPE_GLOBAL ) );
        Check( script.value );
        VALUE global( context, JS_GetGlobalObject( context ) );
        VALUE factory( context, JS_GetPropertyStr( context, global.value, "createDevice" ) );
        if( !JS_IsFunction( context, factory.value ) )
            Error( "module must define globalThis.createDevice(config)" );
        JSON description = { { "id", config.id }, { "device", config.device },
                             { "clockHz", config.clockHz }, { "firmware", config.firmware },
                             { "eeprom", config.eeprom }, { "options", options },
                             { "pins", config.pins }, { "quantumSeconds", config.quantumSeconds } };
        VALUE arg( context, Parse( description ) );
        interrupts = 0;
        object = JS_Call( context, factory.value, global.value, 1, &arg.value );
        Check( object );
        if( !JS_IsObject( object ) )
            Error( "factory must return a synchronous runtime object" );
        UpdateNext( 0.0 );
    }

    void UpdateNext( double time )
    {
        VALUE result( context, Call( "nextEventTime" ) );
        if( !JS_IsNumber( result.value ) || JS_ToFloat64( context, &next, result.value ) < 0
            || std::isnan( next ) || next <= time )
            Error( "nextEventTime must be after the confirmed simulated time" );
        // Even a quiet backend must sample electrical inputs on its configured quantum.
        next = std::min( next, time + config.quantumSeconds );
    }

    std::vector<PROGRAMMABLE_PIN_STATE> Advance(
            double from, double& to, const std::vector<PROGRAMMABLE_PIN_INPUT>& inputs )
    {
        JS_UpdateStackTop( runtime );
        VALUE cooperative( context, JS_GetPropertyStr( context, object, "prepareWindow" ) );
        const bool adaptive = config.autonomousWindowSeconds > 0 && JS_IsFunction( context, cooperative.value );
        if( adaptive ) to = std::min( to, from + config.autonomousWindowSeconds );
        if( config.autonomousWindowSeconds > 0 && !adaptive ) to = std::min( to, from + config.quantumSeconds );
        VALUE time( context, JS_NewFloat64( context, to ) );
        VALUE arg( context, Inputs( inputs ) );
        JSValueConst args[] = { time.value, arg.value };
        VALUE result( context, Call( adaptive ? "prepareWindow" : "advanceTo", 2, args ) );
        if( adaptive )
        {
            VALUE barrier( context, JS_GetPropertyStr( context, result.value, "needsInputs" ) );
            needsInputs = !JS_IsBool( barrier.value ) || JS_ToBool( context, barrier.value );
            VALUE reached( context, JS_GetPropertyStr( context, result.value, "time" ) );
            double at = 0;
            if( !JS_IsNumber( reached.value ) || JS_ToFloat64( context, &at, reached.value ) < 0
                || !std::isfinite( at ) || at <= from || at > to )
                Error( "invalid cooperative input boundary" );
            to = at;
        }
        VALUE transitions( context, adaptive ? JS_GetPropertyStr( context, result.value, "events" )
                                            : JS_DupValue( context, result.value ) );
        if( !JS_IsArray( transitions.value ) )
            Error( "advanceTo must return an array of pin transitions" );
        VALUE lengthValue( context, JS_GetPropertyStr( context, transitions.value, "length" ) );
        std::uint32_t count = 0;
        if( JS_ToUint32( context, &count, lengthValue.value ) < 0 || count > MAX_EVENTS )
            Error( "too many pin transitions in one step" );
        std::vector<PROGRAMMABLE_PIN_STATE> events;
        double previous = from;
        for( std::uint32_t i = 0; i < count; ++i )
        {
            VALUE event( context, JS_GetPropertyUint32( context, transitions.value, i ) );
            VALUE pin( context, JS_GetPropertyStr( context, event.value, "pin" ) );
            VALUE drive( context, JS_GetPropertyStr( context, event.value, "drive" ) );
            VALUE stamp( context, JS_GetPropertyStr( context, event.value, "time" ) );
            std::int32_t mode = -1;
            double at = 0.0;
            std::string name = String( context, pin.value );
            if( !pins.count( name ) || !JS_IsNumber( drive.value )
                || JS_ToInt32( context, &mode, drive.value ) < 0 || mode < 0 || mode > 3
                || !JS_IsNumber( stamp.value ) || JS_ToFloat64( context, &at, stamp.value ) < 0
                || !std::isfinite( at ) || at < previous - 1e-15 || at > to + 1e-15 )
                Error( "invalid, unordered or out-of-window pin transition" );
            at = std::clamp( at, from, to );
            previous = at;
            PROGRAMMABLE_PIN_STATE state{ config.id, name, static_cast<PROGRAMMABLE_DRIVE>( mode ), at };
            states[name] = state;
            events.push_back( std::move( state ) );
        }
        UpdateNext( to );
        return events;
    }
};
} // namespace

struct PROGRAMMABLE_RUNTIME::IMPL
{
    std::vector<std::unique_ptr<DEVICE>> devices;
    double time = 0.0;
};

PROGRAMMABLE_RUNTIME::PROGRAMMABLE_RUNTIME() : m_impl( std::make_unique<IMPL>() ) {}
PROGRAMMABLE_RUNTIME::~PROGRAMMABLE_RUNTIME() = default;

void PROGRAMMABLE_RUNTIME::Configure( const std::vector<PROGRAMMABLE_DEVICE_CONFIG>& configs )
{
    if( configs.size() > 32 )
        throw std::runtime_error( "Too many programmable devices (maximum 32)" );
    auto impl = std::make_unique<IMPL>();
    std::set<std::string> names;
    for( const auto& config : configs )
    {
        if( !names.insert( config.id ).second )
            throw std::runtime_error( "Duplicate programmable device ID: " + config.id );
        auto device = std::make_unique<DEVICE>( config );
        if( configs.size() != 1 ) device->config.autonomousWindowSeconds = 0;
        device->Initialize();
        impl->devices.push_back( std::move( device ) );
    }
    m_impl = std::move( impl );
}

void PROGRAMMABLE_RUNTIME::Reset()
{
    std::vector<PROGRAMMABLE_DEVICE_CONFIG> configs;
    for( const auto& device : m_impl->devices )
        configs.push_back( device->config );
    Configure( configs );
}

std::vector<PROGRAMMABLE_PIN_STATE> PROGRAMMABLE_RUNTIME::AdvanceTo(
        double time, const std::vector<PROGRAMMABLE_PIN_INPUT>& inputs )
{
    if( !std::isfinite( time ) || time < m_impl->time )
        throw std::runtime_error( "Programmable runtime time must be finite and monotonic" );
    for( const auto& input : inputs )
        if( std::none_of( m_impl->devices.begin(), m_impl->devices.end(), [&]( const auto& d ) {
            return d->config.id == input.device;
        } ) )
            throw std::runtime_error( "Unknown programmable input device: " + input.device );
    std::vector<PROGRAMMABLE_PIN_STATE> events;
    for( const auto& device : m_impl->devices )
    {
        auto changes = device->Advance( m_impl->time, time, inputs );
        events.insert( events.end(), changes.begin(), changes.end() );
    }
    std::stable_sort( events.begin(), events.end(), []( const auto& a, const auto& b ) {
        return a.time < b.time;
    } );
    m_impl->time = time;
    return events;
}

std::vector<PROGRAMMABLE_PIN_STATE> PROGRAMMABLE_RUNTIME::States() const
{
    std::vector<PROGRAMMABLE_PIN_STATE> result;
    for( const auto& device : m_impl->devices )
        for( const auto& entry : device->states )
            result.push_back( entry.second );
    return result;
}

double PROGRAMMABLE_RUNTIME::NextEventTime() const
{
    double next = std::numeric_limits<double>::infinity();
    for( const auto& device : m_impl->devices )
        next = std::min( next, device->next );
    return next;
}

double PROGRAMMABLE_RUNTIME::Time() const { return m_impl->time; }
bool PROGRAMMABLE_RUNTIME::NeedsInputs() const
{
    return m_impl->devices.size()!=1 || m_impl->devices.front()->needsInputs;
}

} // namespace CIRCUITALIVE
