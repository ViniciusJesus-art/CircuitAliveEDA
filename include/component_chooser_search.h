/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>

namespace CIRCUITALIVE
{
struct COMPONENT_QUERY
{
    std::string symbols;
    std::string footprint;
    std::string package;
    std::string mounting;
    int pins = 0;
    double width = 0, height = 0;
};

inline std::string NormalizePhysicalText( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(),
                    []( unsigned char c ) { return std::tolower( c ); } );
    std::replace( text.begin(), text.end(), ',', '.' );
    static const std::regex quarter( R"(1\s*/\s*4\s*w|250\s*mw)" );
    static const std::regex eighth( R"(1\s*/\s*8\s*w|125\s*mw)" );
    static const std::regex half( R"(1\s*/\s*2\s*w|500\s*mw)" );
    static const std::regex unitSpaces( R"(([0-9])\s+(mm|w)\b)" );
    static const std::regex trailingZeros( R"(([0-9]+)\.0+(mm|w)\b)" );
    text = std::regex_replace( text, quarter, "0.25w" );
    text = std::regex_replace( text, eighth, "0.125w" );
    text = std::regex_replace( text, half, "0.5w" );
    text = std::regex_replace( text, unitSpaces, "$1$2" );
    return std::regex_replace( text, trailingZeros, "$1$2" );
}

inline COMPONENT_QUERY ParseComponentQuery( const std::string& input )
{
    COMPONENT_QUERY result;
    std::string text = NormalizePhysicalText( input );
    std::smatch match;
    static const std::regex package( R"(\b(dip|pdip|soic|ssop|tssop|sot|qfn|qfp|tqfp|lqfp|sop)[ -]*([0-9]+)\b)" );
    if( std::regex_search( text, match, package ) )
    {
        result.package = match[1];
        try { result.pins = std::stoi( match[2] ); } catch( const std::exception& ) {}
        text = std::regex_replace( text, package, " " );
    }
    static const std::regex dimensions( R"(\b([0-9]+(?:\.[0-9]+)?)\s*x\s*([0-9]+(?:\.[0-9]+)?)mm\b)" );
    if( std::regex_search( text, match, dimensions ) )
    {
        try { result.width = std::stod( match[1] ); result.height = std::stod( match[2] ); }
        catch( const std::exception& ) {}
        text = std::regex_replace( text, dimensions, " " );
    }
    std::istringstream words( text );
    for( std::string word; words >> word; )
    {
        if( word == "smd" || word == "pth" || word == "tht" )
            result.mounting = word == "smd" ? "smd" : "pth";
        else if( std::regex_match( word, std::regex( R"([0-9]+(?:\.[0-9]+)?(?:mm|w))" ) ) )
            result.footprint += ( result.footprint.empty() ? "" : " " ) + word;
        else
            result.symbols += ( result.symbols.empty() ? "" : " " ) + word;
    }
    return result;
}

inline bool MatchesPhysicalText( const std::string& text, const std::string& query )
{
    const auto normalized = NormalizePhysicalText( text );
    std::istringstream words( NormalizePhysicalText( query ) );
    for( std::string word; words >> word; )
    {
        bool found = false;
        for( auto at = normalized.find( word ); at != std::string::npos; at = normalized.find( word, at + 1 ) )
        {
            if( !std::isdigit( static_cast<unsigned char>( word.front() ) ) || at == 0
                || ( !std::isdigit( static_cast<unsigned char>( normalized[at - 1] ) )
                     && normalized[at - 1] != '.' ) )
            { found = true; break; }
        }
        if( !found ) return false;
    }
    return true;
}

inline std::string AlternatePackagePattern( std::string pattern )
{
    std::transform( pattern.begin(), pattern.end(), pattern.begin(),
                    []( unsigned char c ) { return std::tolower( c ); } );
    // Older symbol libraries use SO14* or TSSOP14*, while current footprints use
    // SOIC-14* and TSSOP-14*. Keep the original wildcard as well as this alias.
    static const std::regex legacy( R"(\b(so|soic|sop|ssop|tssop|dip|qfn|qfp|tqfp|lqfp)([0-9]+)(\*)?)" );
    std::string result;
    size_t end = 0;
    for( std::sregex_iterator it( pattern.begin(), pattern.end(), legacy ), stop; it != stop; ++it )
    {
        const auto& match = *it;
        result += pattern.substr( end, match.position() - end );
        result += ( match[1] == "so" ? "soic" : match[1].str() ) + "-" + match[2].str();
        // The package separator excludes SOIC-14-16, which has 14 pads but
        // uses 16-pin numbering with two pins removed; it cannot replace SOIC-14.
        if( match[3].matched ) result += "_*";
        end = match.position() + match.length();
    }
    return result + pattern.substr( end );
}

inline std::string PreferredComponentFootprint( const std::string& library,
                                               const std::string& preferred, bool overrideLibrary )
{
    return overrideLibrary || library.empty() ? preferred : library;
}

inline bool RememberFirstComponentFootprint( const std::string& library, bool hasPreference,
                                             const std::string& selected )
{
    return library.empty() && !hasPreference && !selected.empty();
}
}
