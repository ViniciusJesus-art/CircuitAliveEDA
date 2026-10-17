/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <sim/runtime_memory.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace CIRCUITALIVE;
namespace
{
struct TEMP
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / ( "ca-memory-" + std::to_string( getpid() ) );
    TEMP()
    {
        std::filesystem::create_directories( path );
    }
    ~TEMP()
    {
        std::filesystem::remove_all( path );
    }
};
CIRCUIT_COMPONENT Source( std::string ref, std::string node, std::string value )
{
    return { ref, "Simulation_SPICE:VDC", value, { { "1", node }, { "2", "0" } } };
}
} // namespace
BOOST_AUTO_TEST_CASE( MemoryFilesPreserveKnownAndUnknownBits )
{
    TEMP tmp;
    auto image = EmptyMemory( 65536, -1 );
    image.data[0] = 0xa5;
    image.known[0] = 255;
    image.data[0x8000] = 0x3c;
    image.known[0x8000] = 255;
    SaveMemoryImage( image, tmp.path / "sparse.hex", "ihex" );
    auto restored = EmptyMemory( 65536, -1 );
    LoadMemoryImage( restored, tmp.path / "sparse.hex" );
    BOOST_CHECK( restored.data == image.data );
    BOOST_CHECK( restored.known == image.known );
    image.known[2] = 0x0f;
    image.data[2] = 7;
    SaveMemoryImage( image, tmp.path / "snapshot.camem", "camem" );
    LoadMemoryImage( restored, tmp.path / "snapshot.camem" );
    BOOST_CHECK( restored.known == image.known );
    BOOST_CHECK( restored.data == image.data );
    BOOST_CHECK_THROW( SaveMemoryImage( image, tmp.path / "bad.bin", "bin" ), std::exception );
    BOOST_CHECK_THROW( SaveMemoryImage( image, tmp.path / "bad.hex", "ihex" ), std::exception );
    auto known = EmptyMemory( 128, 255 );
    SaveMemoryImage( known, tmp.path / "image.bin", "bin" );
    auto larger = EmptyMemory( 256, -1 );
    LoadMemoryImage( larger, tmp.path / "image.bin", "auto", 32 );
    BOOST_CHECK_EQUAL( larger.known[31], 0 );
    BOOST_CHECK_EQUAL( larger.data[32], 255 );
    BOOST_CHECK_EQUAL( larger.known[159], 255 );
    BOOST_CHECK_EQUAL( larger.known[160], 0 );
    BOOST_CHECK_THROW( LoadMemoryImage( known, tmp.path / "image.bin", "bin", 1 ), std::exception );
}
BOOST_AUTO_TEST_CASE( MemoryHexRejectsCorruptionWithoutChangingImage )
{
    TEMP tmp;
    auto image = EmptyMemory( 64, 255 );
    auto before = image;
    std::ofstream( tmp.path / "bad.hex" ) << ":01000000A500\n:00000001FF\n";
    BOOST_CHECK_THROW( LoadMemoryImage( image, tmp.path / "bad.hex" ), std::exception );
    BOOST_CHECK( image.data == before.data );
    std::ofstream( tmp.path / "overlap.hex" ) << ":01000000A55A\n:010000003CC3\n:00000001FF\n";
    BOOST_CHECK_THROW( LoadMemoryImage( image, tmp.path / "overlap.hex" ), std::exception );
    std::ofstream( tmp.path / "bounds.hex" ) << ":01004000A51A\n:00000001FF\n";
    BOOST_CHECK_THROW( LoadMemoryImage( image, tmp.path / "bounds.hex" ), std::exception );
}
BOOST_AUTO_TEST_CASE( RuntimeMemorySupportsRollbackAndBoundedStorage )
{
    RUNTIME_MEMORY runtime;
    runtime.Reset( { { "ram", "RAM", 8, true, EmptyMemory( 32768, -1 ) } } );
    auto b = runtime.Backend();
    auto t1 = b.write( b.user, 0, 0, 1, 7, 0xa5, 255, 0 );
    b.mark( b.user, 0, t1, 1 );
    auto t2 = b.write( b.user, 0, t1, 2, 7, 0x3c, 255, 0 );
    b.mark( b.user, 0, t2, 2 );
    // Solver rejects the event at 2 and retries from the previous event state.
    auto t3 = b.write( b.user, 0, t1, 1.5, 8, 0x55, 255, 0 );
    b.mark( b.user, 0, t3, 1.5 );
    runtime.Commit( 1.5 );
    auto data = runtime.Read( "ram", 7, 2 );
    BOOST_CHECK_EQUAL( data.data[0], 0xa5 );
    BOOST_CHECK_EQUAL( data.data[1], 0x55 );
    auto token = t3;
    for( int i = 0; i < 2000; ++i )
    {
        token = b.write( b.user, 0, token, i + 3, i % 32768, i & 255, 255, 0 );
        b.mark( b.user, 0, token, i + 3 );
        runtime.Commit( i + 3 );
    }
    BOOST_CHECK_LE( runtime.Versions(), 3 );
    BOOST_CHECK( runtime.Error().empty() );
}
BOOST_AUTO_TEST_CASE( EpromImagesArePerInstanceAndHaveSixtyFourKAddresses )
{
    TEMP tmp;
    auto first = EmptyMemory( 65536, 255 );
    first.data[0] = 0xa5;
    first.data[0x8000] = 0x3c;
    SaveMemoryImage( first, tmp.path / "eprom.hex", "ihex" );
    CIRCUIT_COMPONENT u{ "U1", "Memory_EPROM:27C512", "27C512-55", {} };
    auto model = MODEL_REPOSITORY().SelectModel( u );
    BOOST_CHECK_EQUAL( model.id, "eprom_27c512_55" );
    for( const auto& p : model.pins )
        u.pins[p.defaultPin] = p.id == "vcc"        ? "vcc"
                               : p.id == "a15"      ? "address"
                               : p.id == "oe"       ? "oe"
                               : p.role == "output" ? p.id
                                                    : "0";
    u.resources["image"] = { ( tmp.path / "eprom.hex" ).string(), "auto", 0 };
    auto circuit = BuildCircuitSnapshot(
            { Source( "V1", "vcc", "5" ), Source( "VA", "address", "0" ), Source( "VO", "oe", "0" ), u } );
    BOOST_REQUIRE_EQUAL( circuit.config.memories.size(), 1 );
    BOOST_CHECK_EQUAL( circuit.config.memories[0].initial.data[0x8000], 0x3c );
    circuit.config.signals.clear();
    for( const auto& p : model.pins )
        if( p.role == "output" )
            circuit.config.signals.push_back( "v(" + circuit.pinNodes.back().at( p.defaultPin ) + ")" );
    circuit.config.maxStep = 2e-9;
    circuit.config.step = 1e-9;
    circuit.config.horizon = 1e-4;
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( circuit.netlist, circuit.config );
    session.AdvanceTo( 1e-6 );
    auto s = session.ReadMemory( circuit.config.memories[0].id, 0x8000, 16 );
    BOOST_CHECK_EQUAL( s.image.data[0], 0x3c );
    const auto time = session.Snapshot().latest.time;
    BOOST_CHECK_EQUAL( s.time, time );
    for( unsigned bit = 0; bit < 8; ++bit )
        BOOST_CHECK_EQUAL( session.Snapshot().latest.values[bit] > 2.5, bool( ( 0xa5 >> bit ) & 1 ) );
    session.SetSourceValue( "Vca2", 5 );
    session.AdvanceTo( time + 20e-9 ); // address access has not elapsed
    for( unsigned bit = 0; bit < 8; ++bit )
        BOOST_CHECK_EQUAL( session.Snapshot().latest.values[bit] > 2.5, bool( ( 0xa5 >> bit ) & 1 ) );
    session.AdvanceTo( time + 150e-9 );
    for( unsigned bit = 0; bit < 8; ++bit )
        BOOST_CHECK_EQUAL( session.Snapshot().latest.values[bit] > 2.5, bool( ( 0x3c >> bit ) & 1 ) );
    BOOST_CHECK_EQUAL( session.ReadMemory( s.id, 0, 1 ).image.data[0], 0xa5 );
    BOOST_CHECK_EQUAL( session.Snapshot().latest.time, session.ReadMemory( s.id, 0, 1 ).time );
    auto other = u;
    other.reference = "U2";
    other.resources.clear();
    auto two = BuildCircuitSnapshot(
            { Source( "V1", "vcc", "5" ), Source( "VA", "address", "0" ), Source( "VO", "oe", "0" ), u, other } );
    BOOST_CHECK_EQUAL( two.config.memories.size(), 2 );
    BOOST_CHECK_EQUAL( two.config.memories[1].initial.data[0], 255 );
    BOOST_CHECK_NE( two.config.memories[0].id, two.config.memories[1].id );
}
BOOST_AUTO_TEST_CASE( SramLoadsInitialDataAndExportsActualPausedWrites )
{
    TEMP tmp;
    auto initial = EmptyMemory( 32768, -1 );
    initial.data[0] = 0x5a;
    initial.known[0] = 255;
    SaveMemoryImage( initial, tmp.path / "ram.camem", "camem" );
    CIRCUIT_COMPONENT u{ "U1", "Memory_RAM:CY62256-70PC", "CY62256-70PC", {} };
    auto model = MODEL_REPOSITORY().SelectModel( u );
    for( const auto& p : model.pins )
        u.pins[p.defaultPin] = p.id == "vcc"               ? "vcc"
                               : p.id == "we"              ? "we"
                               : p.id == "cs"              ? "cs"
                               : p.id == "oe"              ? "oe"
                               : p.id == "a0"              ? "addr"
                               : p.role == "bidirectional" ? p.id
                                                           : "0";
    u.resources["image"] = { ( tmp.path / "ram.camem" ).string(), "auto", 0 };
    std::vector<CIRCUIT_COMPONENT> parts = { Source( "V1", "vcc", "5" ), Source( "VW", "we", "5" ),
                                             Source( "VC", "cs", "0" ), Source( "VO", "oe", "5" ),
                                             Source( "VA", "addr", "5" ) };
    for( unsigned i = 0; i < 8; ++i )
        parts.push_back(
                Source( "VD" + std::to_string( i ), "d" + std::to_string( i ), ( 0xa5 >> i ) & 1 ? "5" : "0" ) );
    parts.push_back( u );
    auto c = BuildCircuitSnapshot( parts );
    c.config.signals = { "v(" + c.pinNodes[0].at( "1" ) + ")" };
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config );
    session.AdvanceTo( 1e-6 );
    const auto id = c.config.memories[0].id;
    auto data = session.ReadMemory( id, 0, 16 );
    BOOST_CHECK_EQUAL( data.image.data[0], 0x5a );
    BOOST_CHECK_EQUAL( data.image.known[1], 0 );
    session.SetSourceValue( "Vca2", 0 );
    session.AdvanceTo( 2e-6 );
    session.SetSourceValue( "Vca2", 5 );
    session.AdvanceTo( 3e-6 );
    data = session.ReadMemory( id, 0, 16 );
    BOOST_CHECK_EQUAL( data.image.data[0], 0x5a );
    BOOST_CHECK_EQUAL( data.image.data[1], 0xa5 );
    BOOST_CHECK_EQUAL( data.image.known[1], 255 );
    BOOST_CHECK_EQUAL( data.image.known[2], 0 );
    auto now = session.Snapshot().latest.time;
    SaveMemoryImage( data.image, tmp.path / "captured.camem", "camem" );
    BOOST_CHECK_EQUAL( session.Snapshot().latest.time, now );
    session.AdvanceTo( 4e-6 );
    BOOST_CHECK_EQUAL( session.ReadMemory( id, 1, 1 ).image.data[0], 0xa5 );
    session.Stop();
    session.AdvanceTo( 1e-6 );
    BOOST_CHECK_EQUAL( session.ReadMemory( id, 1, 1 ).image.known[0], 0 );
}
