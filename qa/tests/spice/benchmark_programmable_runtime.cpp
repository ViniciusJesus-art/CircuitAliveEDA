/* SPDX-License-Identifier: GPL-3.0-or-later */
// Opt-in diagnostic, not an application execution mode or a pass/fail speed test.
#include <sim/memory_image.h>
#include <sim/programmable_runtime.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

using namespace CIRCUITALIVE;

namespace
{
using EVENTS = std::vector<PROGRAMMABLE_PIN_STATE>;
using TRACES = std::vector<EVENTS>;

PROGRAMMABLE_DEVICE_CONFIG Configuration( const char* firmware )
{
    PROGRAMMABLE_DEVICE_CONFIG config;
    config.id = "U1";
    config.device = "atmega328p";
    config.pins = { "RESET", "PD0", "PD1", "PD2", "PD3", "PD4", "VCC", "GND",
                    "PB6", "PB7", "PD5", "PD6", "PD7", "PB0", "PB1", "PB2",
                    "PB3", "PB4", "PB5", "AVCC", "AREF", "PC0", "PC1", "PC2",
                    "PC3", "PC4", "PC5" };
    std::ifstream source( CIRCUITALIVE_BENCH_AVR_RUNTIME );
    if( !source ) throw std::runtime_error( "Cannot open the AVR runtime" );
    config.source.assign( std::istreambuf_iterator<char>( source ), {} );
    auto image = EmptyMemory( 32768, 255 );
    if( firmware && std::string( firmware ) != "-" )
        LoadMemoryImage( image, std::filesystem::path( reinterpret_cast<const char8_t*>( firmware ) ) );
    else
    {
        // Real AVR instructions: DDRB.PB5 output, toggle with ~192 us delays.
        const std::uint16_t words[] = { 0xe200, 0xb904, 0x9a2d, 0xe014, 0xef2f,
                0x952a, 0xf7f1, 0x951a, 0xf7d9, 0x982d, 0xe014, 0xef2f,
                0x952a, 0xf7f1, 0x951a, 0xf7d9, 0xcff1 };
        for( std::size_t i = 0; i < std::size( words ); ++i )
        {
            image.data[2*i] = words[i] & 255;
            image.data[2*i+1] = words[i] >> 8;
        }
    }
    config.firmware = std::move( image.data );
    // Constant electrical inputs isolate runtime cost from the analog solver.
    // Run() enables cooperative windows; prepareWindow() can still yield before
    // the requested deadline. The input quantum is never enlarged to force it.
    config.autonomousWindowSeconds = 0;
    return config;
}

struct RESULT
{
    double wall = 0, cpu = 0;
    TRACES traces;
    PROGRAMMABLE_RUNTIME::STATISTICS statistics;
};

RESULT Run( PROGRAMMABLE_DEVICE_CONFIG config, int count, bool parallel,
            double quantum, double duration, bool coupled = false )
{
    std::vector<PROGRAMMABLE_DEVICE_CONFIG> devices;
    std::vector<PROGRAMMABLE_PIN_INPUT> inputs;
    for( int i = 0; i < count; ++i )
    {
        config.id = "U" + std::to_string( i+1 );
        config.autonomousWindowSeconds = quantum > config.quantumSeconds ? quantum : 0;
        devices.push_back( config );
        for( const auto& pin : config.pins )
        {
            const bool supply = pin == "VCC" || pin == "AVCC" || pin == "AREF" || pin == "RESET";
            inputs.push_back( { config.id, pin, supply ? 5.0 : 0.0 } );
        }
    }
    PROGRAMMABLE_RUNTIME runtime;
    runtime.Configure( devices, parallel ? 0 : 1 );
    RESULT result;
    result.traces.resize( count );
    // Exercise the production coordinator, including private windows and merge.
    // Exclude context construction and thread startup from both timings.
    const auto start = std::chrono::steady_clock::now();
    const auto cpu = std::clock();
    const auto steps = std::llround( duration / quantum );
    for( long long step = 1; step <= steps; ++step )
    {
        if( coupled )
        {
            std::vector<double> voltages( count );
            voltages[0] = ( ( step-1 ) / 23 ) % 2 ? 5.0 : 0.0;
            for( const auto& state : runtime.States() )
            {
                const auto index = std::stoul( state.device.substr( 1 ) );
                if( state.pin == "PB5" && index < voltages.size() )
                    voltages[index] = state.drive == PROGRAMMABLE_DRIVE::HIGH ? 5.0 : 0.0;
            }
            for( auto& input : inputs )
                if( input.pin == "PD2" )
                    input.voltage = voltages[std::stoul( input.device.substr( 1 ) )-1];
        }
        const double target = step * quantum;
        do
        {
            const double before = runtime.Time();
            for( const auto& event : runtime.AdvanceTo( target, inputs ) )
                result.traces[std::stoul( event.device.substr( 1 ) )-1].push_back( event );
            if( runtime.Time() <= before ) throw std::runtime_error( "Coordinator did not advance" );
        } while( runtime.Time() < target - 1e-15 );
    }
    result.wall = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
    result.cpu = double( std::clock() - cpu ) / CLOCKS_PER_SEC;
    result.statistics = runtime.Statistics();
    return result;
}

void Compare( const EVENTS& actual, const EVENTS& expected )
{
    if( actual.size() != expected.size() ) throw std::runtime_error( "Trace size mismatch" );
    for( std::size_t i = 0; i < actual.size(); ++i )
        if( actual[i].pin != expected[i].pin || actual[i].drive != expected[i].drive
            || std::abs( actual[i].time - expected[i].time ) > 1e-12 )
            throw std::runtime_error( "Pin/timestamp mismatch at event " + std::to_string( i ) );
}

void Audit( PROGRAMMABLE_DEVICE_CONFIG config )
{
    // PD2 pull-up input is continuously mirrored onto PB5. The four devices
    // communicate only at 1 us barriers, with identical event ordering.
    const std::uint16_t words[] = { 0xe200, 0xb904, 0xe004, 0xb90b, 0x994a,
                                   0xc002, 0x982d, 0xcffc, 0x9a2d, 0xcffa };
    std::fill( config.firmware.begin(), config.firmware.end(), 255 );
    for( std::size_t i = 0; i < std::size( words ); ++i )
    {
        config.firmware[2*i] = words[i] & 255;
        config.firmware[2*i+1] = words[i] >> 8;
    }
    const auto serial = Run( config, 4, false, 1e-6, .001, true );
    const auto parallel = Run( config, 4, true, 1e-6, .001, true );
    for( std::size_t i = 0; i < serial.traces.size(); ++i )
    {
        Compare( parallel.traces[i], serial.traces[i] );
        const auto toggles = std::count_if( serial.traces[i].begin(), serial.traces[i].end(),
                                          []( const auto& event ) { return event.pin == "PB5"; } );
        if( toggles < 40 ) throw std::runtime_error( "Input audit did not exercise GPIO transitions" );
    }
    std::cout << nlohmann::json( { { "audit", "four_coupled_mcus_at_1us" },
                                   { "equivalent", true } } ).dump() << std::endl;
}
} // namespace

int main( int argc, char** argv )
{
    try
    {
        if( argc > 4 ) throw std::runtime_error( "Usage: benchmark [firmware.hex/bin|-] [seconds] [repeats]" );
        const auto config = Configuration( argc > 1 ? argv[1] : nullptr );
        const double duration = argc > 2 ? std::stod( argv[2] ) : .06;
        const int repeats = argc > 3 ? std::stoi( argv[3] ) : 3;
        if( !std::isfinite( duration ) || duration < .001 || duration > 10
            || std::abs( duration * 1000 - std::round( duration * 1000 ) ) > 1e-6
            || repeats < 1 || repeats > 10 )
            throw std::runtime_error( "Use 1..10000 whole milliseconds and 1..10 repetitions" );
        Audit( config );
        const auto reference = Run( config, 1, false, 1e-6, duration ).traces.front();
        for( int repeat = 0; repeat < repeats; ++repeat )
            for( int count : { 1, 2, 4, 8 } )
                for( double quantum : { 1e-6, 1e-4, 1e-3 } )
                    for( bool parallel : { false, true } )
                    {
                        const auto result = Run( config, count, parallel, quantum, duration );
                        for( const auto& trace : result.traces ) Compare( trace, reference );
                        const nlohmann::json report = {
                            { "repeat", repeat }, { "devices", count }, { "parallel", parallel },
                            { "quantum", quantum }, { "simulated_seconds", duration },
                            { "wall_seconds", result.wall }, { "cpu_seconds", result.cpu },
                            { "events_per_device", reference.size() }, { "equivalent", true },
                            { "worker_threads", result.statistics.workerThreads },
                            { "windows", result.statistics.windows },
                            { "parallel_batches", result.statistics.parallelBatches } };
                        std::cout << report.dump() << std::endl;
                    }
        return 0;
    }
    catch( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
