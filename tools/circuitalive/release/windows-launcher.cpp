/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static std::string utf8(const fs::path& p){const auto s=p.generic_u8string();return {s.begin(),s.end()};}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
    try
    {
        std::wstring path(32768,L'\0');path.resize(GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size())));
        const auto bin=fs::path(path).parent_path(),root=bin.parent_path();
        const auto data=root/L"share/kicad";
        SetEnvironmentVariableW(L"KICAD10_SYMBOL_DIR",(data/L"symbols").c_str());
        SetEnvironmentVariableW(L"KICAD10_FOOTPRINT_DIR",(data/L"footprints").c_str());
        SetEnvironmentVariableW(L"KICAD10_3DMODEL_DIR",(data/L"3dmodels").c_str());
        SetEnvironmentVariableW(L"KICAD10_TEMPLATE_DIR",(data/L"template").c_str());
        SetEnvironmentVariableW(L"CIRCUITALIVE_MODEL_DIR",(root/L"share/circuitalive/models").c_str());
        SetEnvironmentVariableW(L"CIRCUITALIVE_BLOCK_DIR",(root/L"share/circuitalive/blocks").c_str());
        wchar_t appdata[32768];if(!GetEnvironmentVariableW(L"LOCALAPPDATA",appdata,32768))return 2;
        const auto scripts=fs::path(appdata)/L"CircuitAliveEDA/ngspice";
        fs::create_directories(scripts);std::ofstream init(scripts/L"spinit");
        init << "* CircuitAliveEDA bundled runtime\nunset osdi_enabled\nset num_threads=2\n";
        for(const auto& entry:fs::directory_iterator(bin/L"ngspice"))
            if(entry.path().extension()==L".cm")init<<"codemodel ngspice/"<<utf8(entry.path().filename())<<"\n";
        init.close();if(!init)return 2;
        SetEnvironmentVariableW(L"SPICE_SCRIPTS",scripts.c_str());
        SetEnvironmentVariableW(L"PYTHONHOME",bin.c_str());
        const auto executable=bin/L"kicad.exe";
        std::wstring command=L"\""+executable.wstring()+L"\"";
        const auto* tail=GetCommandLineW();
        if(*tail==L'\"'){++tail;while(*tail&&*tail!=L'\"')++tail;if(*tail)++tail;}
        else while(*tail&&*tail!=L' ')++tail;
        command+=tail;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,bin.c_str(),&startup,&process))return 3;
        CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
    }
    catch(...){MessageBoxW(nullptr,L"Could not start the bundled runtime. Reinstall CircuitAliveEDA.",L"CircuitAliveEDA",MB_OK|MB_ICONERROR);return 1;}
}
