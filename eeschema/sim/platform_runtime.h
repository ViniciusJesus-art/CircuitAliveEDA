/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace CIRCUITALIVE
{
std::filesystem::path ExecutablePath();
std::uint64_t ProcessId();
std::uint64_t ResidentMemoryBytes();
std::string PathUtf8(const std::filesystem::path& path);
void SetRuntimeEnvironment(const char* name,const std::string& value);
void UnsetRuntimeEnvironment(const char* name);
void ReplaceFile(const std::filesystem::path& source,const std::filesystem::path& destination);
}
