/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform_runtime.h"
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif
namespace CIRCUITALIVE
{
std::filesystem::path ExecutablePath()
{
#ifdef _WIN32
    std::wstring buffer(32768,L'\0');
    const auto size=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!size || size>=buffer.size()) throw std::runtime_error("Cannot locate application executable");
    buffer.resize(size); return std::filesystem::path(buffer);
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}
std::string PathUtf8(const std::filesystem::path& path)
{
    const auto value=path.generic_u8string();return {value.begin(),value.end()};
}
std::uint64_t ProcessId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}
std::uint64_t ResidentMemoryBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
    return GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))?memory.WorkingSetSize:0;
#else
    std::ifstream statm("/proc/self/statm");std::uint64_t pages=0,resident=0;statm>>pages>>resident;
    return resident*static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}
void SetRuntimeEnvironment(const char* name,const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name,value.c_str());
#else
    setenv(name,value.c_str(),1);
#endif
}
void ReplaceSavedFile(const std::filesystem::path& source,const std::filesystem::path& destination)
{
#ifdef _WIN32
    if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace saved file: "+PathUtf8(destination));
#else
    std::filesystem::rename(source,destination);
#endif
}
void UnsetRuntimeEnvironment(const char* name)
{
#ifdef _WIN32
    _putenv_s(name,"");
#else
    unsetenv(name);
#endif
}
}
