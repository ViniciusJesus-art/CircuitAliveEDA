/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CIRCUITALIVE
{
constexpr std::size_t MAX_MEMORY_BYTES = 1024 * 1024;
struct RESOURCE_SELECTION
{
    std::string path, format = "auto";
    std::uint64_t address = 0;
    bool operator==( const RESOURCE_SELECTION& ) const = default;
};
struct MEMORY_IMAGE
{
    std::vector<std::uint8_t> data, known; // A mask: an unset bit represents X.
};
struct MEMORY_REGION_CONFIG
{
    std::string id, label;
    unsigned wordBits = 8;
    bool writable = false;
    MEMORY_IMAGE initial;
};
struct MEMORY_SNAPSHOT
{
    std::string id;
    double time = 0;
    MEMORY_IMAGE image;
};
MEMORY_IMAGE EmptyMemory( std::size_t size, int fill ); // -1 = unknown, otherwise 0..255.
void LoadMemoryImage( MEMORY_IMAGE& image, const std::filesystem::path& path, const std::string& format = "auto",
                      std::uint64_t address = 0 );
void SaveMemoryImage( const MEMORY_IMAGE& image, const std::filesystem::path& path, const std::string& format );
std::string MemoryHex( const std::vector<std::uint8_t>& bytes );
std::vector<std::uint8_t> MemoryUnhex( const std::string& text );
} // namespace CIRCUITALIVE
