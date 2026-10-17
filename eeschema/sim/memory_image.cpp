/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "memory_image.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
std::string MemoryHex( const std::vector<std::uint8_t>& bytes )
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve( bytes.size() * 2 );
    for( const auto b : bytes )
    {
        result += digits[b >> 4];
        result += digits[b & 15];
    }
    return result;
}
std::vector<std::uint8_t> MemoryUnhex( const std::string& text )
{
    if( text.size() % 2 || text.size() > 2 * MAX_MEMORY_BYTES )
        throw std::runtime_error( "Invalid memory hex size" );
    auto nibble = []( unsigned char c ) -> unsigned
    {
        if( c >= '0' && c <= '9' )
            return c - '0';
        c = std::toupper( c );
        if( c >= 'A' && c <= 'F' )
            return c - 'A' + 10;
        throw std::runtime_error( "Invalid memory hex digit" );
    };
    std::vector<std::uint8_t> bytes;
    bytes.reserve( text.size() / 2 );
    for( std::size_t i = 0; i < text.size(); i += 2 )
        bytes.push_back( ( nibble( text[i] ) << 4 ) | nibble( text[i + 1] ) );
    return bytes;
}
MEMORY_IMAGE EmptyMemory( std::size_t size, int fill )
{
    if( !size || size > MAX_MEMORY_BYTES || fill < -1 || fill > 255 )
        throw std::runtime_error( "Invalid memory capacity/fill" );
    return { std::vector<std::uint8_t>( size, fill < 0 ? 0 : fill ),
             std::vector<std::uint8_t>( size, fill < 0 ? 0 : 255 ) };
}
static void Check( const MEMORY_IMAGE& image )
{
    if( image.data.empty() || image.data.size() > MAX_MEMORY_BYTES || image.data.size() != image.known.size() )
        throw std::runtime_error( "Invalid memory image" );
}
void LoadMemoryImage( MEMORY_IMAGE& image, const std::filesystem::path& path, const std::string& requested,
                      std::uint64_t address )
{
    Check( image );
    std::error_code error;
    const auto size = std::filesystem::file_size( path, error );
    if( error || size > 8 * MAX_MEMORY_BYTES )
        throw std::runtime_error( "Missing or oversized memory image: " + path.string() );
    std::ifstream input( path, std::ios::binary );
    if( !input )
        throw std::runtime_error( "Cannot read memory image: " + path.string() );
    const std::string text( std::istreambuf_iterator<char>( input ), {} );
    auto format = requested;
    if( format == "auto" )
    {
        auto ext = path.extension().string();
        for( auto& c : ext )
            c = std::tolower( static_cast<unsigned char>( c ) );
        format = ext == ".hex" || ext == ".ihex" ? "ihex" : ext == ".camem" ? "camem" : ext == ".bin" ? "bin" : "";
    }
    auto result = image;
    auto put = [&]( std::uint64_t offset, std::uint8_t data, std::uint8_t known )
    {
        if( address > result.data.size() || offset >= result.data.size() - address )
            throw std::runtime_error( "Memory image address exceeds component capacity" );
        result.data[address + offset] = data;
        result.known[address + offset] = known;
    };
    if( format == "bin" )
        for( std::size_t i = 0; i < text.size(); ++i )
            put( i, static_cast<std::uint8_t>( text[i] ), 255 );
    else if( format == "camem" )
    {
        const auto j = nlohmann::json::parse( text );
        if( j.at( "schema_version" ) != 1 || j.at( "format" ) != "circuitalive-memory" )
            throw std::runtime_error( "Unsupported memory snapshot" );
        const auto data = MemoryUnhex( j.at( "data" ) ), known = MemoryUnhex( j.at( "known" ) );
        if( data.size() != known.size() )
            throw std::runtime_error( "Invalid memory snapshot masks" );
        for( std::size_t i = 0; i < data.size(); ++i )
            put( i, data[i], known[i] );
    }
    else if( format == "ihex" )
    {
        std::istringstream lines( text );
        std::string line;
        std::uint64_t base = 0;
        bool ended = false;
        std::vector<bool> written( image.data.size(), false );
        while( std::getline( lines, line ) )
        {
            if( !line.empty() && line.back() == '\r' )
                line.pop_back();
            if( line.empty() )
                continue;
            if( ended || line.front() != ':' )
                throw std::runtime_error( "Invalid Intel HEX record" );
            const auto record = MemoryUnhex( line.substr( 1 ) );
            if( record.size() < 5 || record.size() != std::size_t( record[0] ) + 5 )
                throw std::runtime_error( "Invalid Intel HEX length" );
            unsigned checksum = 0;
            for( auto b : record )
                checksum += b;
            if( checksum & 255 )
                throw std::runtime_error( "Intel HEX checksum mismatch" );
            const auto count = record[0], type = record[3];
            const unsigned low = ( record[1] << 8 ) | record[2];
            if( type == 0 )
                for( unsigned i = 0; i < count; ++i )
                {
                    const auto offset = base + low + i;
                    if( offset >= written.size() || address > written.size() - 1 - offset )
                        throw std::runtime_error( "Intel HEX address exceeds component capacity" );
                    const auto index = address + offset;
                    if( written[index] && result.data[index] != record[4 + i] )
                        throw std::runtime_error( "Conflicting Intel HEX records" );
                    put( offset, record[4 + i], 255 );
                    written[index] = true;
                }
            else if( type == 1 && count == 0 && low == 0 )
                ended = true;
            else if( ( type == 2 || type == 4 ) && count == 2 && low == 0 )
                base = std::uint64_t( ( record[4] << 8 ) | record[5] ) << ( type == 2 ? 4 : 16 );
            else if( ( type == 3 || type == 5 ) && count == 4 && low == 0 )
            {
            } // Entry point belongs to a future CPU engine.
            else
                throw std::runtime_error( "Unsupported or malformed Intel HEX record" );
        }
        if( !ended )
            throw std::runtime_error( "Intel HEX end record is missing" );
    }
    else
        throw std::runtime_error( "Choose BIN, Intel HEX or CircuitAlive memory snapshot format" );
    image = std::move( result );
}
void SaveMemoryImage( const MEMORY_IMAGE& image, const std::filesystem::path& path, const std::string& format )
{
    Check( image );
    std::string output;
    if( format == "bin" )
    {
        if( std::any_of( image.known.begin(), image.known.end(), []( auto b ) { return b != 255; } ) )
            throw std::runtime_error(
                    "BIN cannot represent unknown bits (X). Export a .camem snapshot to preserve them." );
        output.assign( reinterpret_cast<const char*>( image.data.data() ), image.data.size() );
    }
    else if( format == "camem" )
        output = nlohmann::json( { { "schema_version", 1 },
                                   { "format", "circuitalive-memory" },
                                   { "data", MemoryHex( image.data ) },
                                   { "known", MemoryHex( image.known ) } } )
                         .dump( 2 ) +
                 "\n";
    else if( format == "ihex" )
    {
        if( std::any_of( image.known.begin(), image.known.end(), []( auto b ) { return b != 0 && b != 255; } ) )
            throw std::runtime_error( "Intel HEX cannot represent partially unknown bytes. Export .camem instead." );
        auto record = [&]( std::uint16_t address, unsigned type, std::vector<std::uint8_t> data )
        {
            std::vector<std::uint8_t> line{ static_cast<std::uint8_t>( data.size() ),
                                            static_cast<std::uint8_t>( address >> 8 ),
                                            static_cast<std::uint8_t>( address ), static_cast<std::uint8_t>( type ) };
            line.insert( line.end(), data.begin(), data.end() );
            unsigned sum = 0;
            for( auto b : line )
                sum += b;
            line.push_back( static_cast<std::uint8_t>( -sum ) );
            output += ':' + MemoryHex( line ) + '\n';
        };
        unsigned upper = 0;
        for( std::size_t i = 0; i < image.data.size(); )
        {
            if( !image.known[i] )
            {
                ++i;
                continue;
            }
            if( ( i >> 16 ) != upper )
            {
                upper = i >> 16;
                record( 0, 4, { static_cast<std::uint8_t>( upper >> 8 ), static_cast<std::uint8_t>( upper ) } );
            }
            const auto start = i;
            std::vector<std::uint8_t> bytes;
            while( i < image.data.size() && image.known[i] == 255 && bytes.size() < 16 && ( i >> 16 ) == upper )
                bytes.push_back( image.data[i++] );
            record( start & 65535, 0, std::move( bytes ) );
        }
        record( 0, 1, {} );
    }
    else
        throw std::runtime_error( "Unsupported memory export format" );
    // Atomic replace also permits exporting over the input image after explicit selection.
    const auto temporary = path.string() + ".circuitalive.tmp";
    try
    {
        std::ofstream out( temporary, std::ios::binary | std::ios::trunc );
        out.exceptions( std::ios::failbit | std::ios::badbit );
        out.write( output.data(), output.size() );
        out.close();
        std::filesystem::rename( temporary, path );
    }
    catch( ... )
    {
        std::error_code error;
        std::filesystem::remove( temporary, error );
        throw;
    }
}
} // namespace CIRCUITALIVE
