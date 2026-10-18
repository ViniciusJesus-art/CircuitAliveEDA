/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
// Private implementation, included inside CIRCUITALIVE by interactive_sim_session.cpp.
struct INTERACTIVE_SIM_SESSION::IMPL
{
    mutable std::mutex mutex;
    int pid=-1;
    HANDLE process=nullptr,job=nullptr,pipe=INVALID_HANDLE_VALUE;
    std::chrono::milliseconds timeout;
    std::string input,workerPath;
    JSON configuration;
    std::uint64_t generationOffset=0;
    explicit IMPL(std::chrono::milliseconds value):timeout(value){}
    ~IMPL(){Terminate();}
    void Terminate() noexcept
    {
        if(pipe!=INVALID_HANDLE_VALUE){CancelIoEx(pipe,nullptr);CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;}
        if(process)
        {
            if(WaitForSingleObject(process,500)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,5000);}
            CloseHandle(process);process=nullptr;
        }
        if(job){CloseHandle(job);job=nullptr;}pid=-1;
    }
    void Spawn()
    {
        input.clear();
        static std::atomic<unsigned> serial{0};
        const auto name=L"\\\\.\\pipe\\CircuitAliveEDA-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
        pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
                             1,65536,65536,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create simulation socket");
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        HANDLE child=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,&security,OPEN_EXISTING,0,nullptr);
        if(child==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot prepare simulation worker spawn");
        HANDLE errors=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
        struct CHILD_HANDLES{HANDLE first,second;~CHILD_HANDLES(){CloseHandle(first);if(second!=INVALID_HANDLE_VALUE)CloseHandle(second);}} handles{child,errors};
        if(errors==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot prepare simulation worker spawn");
        // Restrict inheritance to protocol and stderr handles, even in a multithreaded GUI.
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes))throw std::runtime_error("Cannot prepare simulation worker spawn");
        struct ATTRIBUTE_CLEANUP{LPPROC_THREAD_ATTRIBUTE_LIST value;~ATTRIBUTE_CLEANUP(){DeleteProcThreadAttributeList(value);}} cleanup{attributes};
        HANDLE inherited[]={child,errors};
        if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))
            throw std::runtime_error("Cannot prepare simulation worker spawn");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
        startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=startup.StartupInfo.hStdOutput=child;startup.StartupInfo.hStdError=errors;
        startup.lpAttributeList=attributes;
        const auto executable=std::filesystem::path(std::u8string(workerPath.begin(),workerPath.end())).wstring();
        std::wstring command=L"\""+executable+L"\"";
        PROCESS_INFORMATION info{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
                           CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,
                           nullptr,nullptr,&startup.StartupInfo,&info))
            throw std::runtime_error("Cannot start simulation worker: "+std::to_string(GetLastError()));
        process=info.hProcess;pid=static_cast<int>(info.dwProcessId);
        job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        const bool assigned=job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&AssignProcessToJobObject(job,process);
        if(!assigned){CloseHandle(info.hThread);Terminate();throw std::runtime_error("Cannot prepare simulation worker spawn");}
        ResumeThread(info.hThread);CloseHandle(info.hThread);
        const auto hello=Request({{"op","hello"}});
        if(hello.at("version").get<int>()!=VERSION)throw std::runtime_error("Incompatible simulation worker");
    }
    DWORD Transfer(bool write,void* buffer,DWORD size,CLOCK::time_point deadline)
    {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-CLOCK::now()).count();
        if(remaining<=0)throw std::runtime_error("Simulation worker timed out");
        OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!operation.hEvent)throw std::runtime_error("Simulation worker is unavailable");
        DWORD transferred=0;
        const BOOL done=write?WriteFile(pipe,buffer,size,&transferred,&operation):ReadFile(pipe,buffer,size,&transferred,&operation);
        DWORD error=done?ERROR_SUCCESS:GetLastError();
        if(!done&&error==ERROR_IO_PENDING)
        {
            const auto waited=WaitForSingleObject(operation.hEvent,static_cast<DWORD>(remaining));
            if(waited!=WAIT_OBJECT_0)
            {
                CancelIoEx(pipe,&operation);GetOverlappedResult(pipe,&operation,&transferred,TRUE);
                CloseHandle(operation.hEvent);throw std::runtime_error("Simulation worker timed out");
            }
            if(!GetOverlappedResult(pipe,&operation,&transferred,FALSE))error=GetLastError();else error=ERROR_SUCCESS;
        }
        CloseHandle(operation.hEvent);
        if(error!=ERROR_SUCCESS||!transferred)throw std::runtime_error("Simulation worker exited without a reply");
        return transferred;
    }
    JSON Request(JSON request)
    {
        if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("Simulation worker is unavailable");
        JSON response;
        try
        {
            const auto deadline=CLOCK::now()+timeout;std::string message=request.dump()+'\n';
            if(message.size()>MAX_MESSAGE)throw std::runtime_error("Simulation request exceeds protocol limit");
            for(std::size_t sent=0;sent<message.size();)
                sent+=Transfer(true,message.data()+sent,static_cast<DWORD>(message.size()-sent),deadline);
            while(input.find('\n')==std::string::npos)
            {
                char data[8192];input.append(data,Transfer(false,data,sizeof(data),deadline));
                if(input.size()>MAX_MESSAGE)throw std::runtime_error("Simulation reply exceeds protocol limit");
            }
            const auto end=input.find('\n');response=JSON::parse(input.substr(0,end));input.erase(0,end+1);
            response.at("ok").get<bool>();
        }
        catch(...){Terminate();throw;}
        if(!response.at("ok").get<bool>())throw std::runtime_error(response.at("error").get<std::string>());
        return response;
    }
};
