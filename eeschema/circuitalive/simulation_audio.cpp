/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_audio.h"
#include <wx/dynlib.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif
namespace CIRCUITALIVE
{
struct SIMULATION_AUDIO::IMPL
{
    mutable std::mutex mutex;std::condition_variable wake;std::deque<std::int16_t> queue;
    bool quit=false,flush=false,enabled=false,failed=false;std::string error;
    std::thread thread;
    IMPL():thread([this]{run();}){}
    ~IMPL(){ {std::lock_guard lock(mutex);quit=true;}wake.notify_one();thread.join(); }
    void run()
    {
#ifdef __linux__
        // Resolve the platform ABI dynamically: audio is optional and neither the
        // worker nor application startup depends on a running sound server.
        wxDynamicLibrary library;
        struct SAMPLE_SPEC { int format;std::uint32_t rate;std::uint8_t channels; };
        struct BUFFER_ATTR { std::uint32_t maxlength,tlength,prebuf,minreq,fragsize; };
        using OPEN=void*(*)(const char*,const char*,int,const char*,const char*,const SAMPLE_SPEC*,const void*,const BUFFER_ATTR*,int*);
        using WRITE=int(*)(void*,const void*,std::size_t,int*);using FREE=void(*)(void*);using FLUSH=int(*)(void*,int*);
        OPEN open=nullptr;WRITE write=nullptr;FREE free=nullptr;FLUSH discard=nullptr;void* stream=nullptr;
#elif defined(_WIN32)
        HWAVEOUT stream=nullptr;
        HANDLE completion=CreateEventW(nullptr,FALSE,FALSE,nullptr);
#endif
        for(;;)
        {
            std::vector<std::int16_t> block;bool clear=false;
            {
                std::unique_lock lock(mutex);wake.wait(lock,[&]{return quit||flush||(!queue.empty()&&enabled&&!failed);});
                if(quit)break;
                clear=flush;flush=false;
                const auto count=std::min<std::size_t>(960,queue.size());
                if(enabled&&!failed){block.assign(queue.begin(),queue.begin()+count);queue.erase(queue.begin(),queue.begin()+count);}
            }
            bool ok=true;
#ifdef __linux__
            if(clear&&stream)discard(stream,nullptr);
            if(!block.empty()&&!stream)
            {
                if(!library.IsLoaded()&&library.Load("libpulse-simple.so.0",wxDL_NOW|wxDL_QUIET))
                {
                    open=reinterpret_cast<OPEN>(library.GetSymbol("pa_simple_new"));write=reinterpret_cast<WRITE>(library.GetSymbol("pa_simple_write"));
                    free=reinterpret_cast<FREE>(library.GetSymbol("pa_simple_free"));discard=reinterpret_cast<FLUSH>(library.GetSymbol("pa_simple_flush"));
                }
                const SAMPLE_SPEC spec{3,48000,1};
                // Rebuffer after an underrun. With prebuf=0 Pulse keeps its
                // read cursor running through silence; a temporarily slower
                // simulation then writes behind that cursor indefinitely.
                // Keep 20 ms ready before starting/restarting playback.
                const BUFFER_ATTR attributes{9600,4800,1920,960,0};
                if(open&&write&&free&&discard)stream=open(nullptr,"CircuitAliveEDA",1,nullptr,"Simulation audio",&spec,nullptr,&attributes,nullptr);
                ok=stream!=nullptr;
            }
            if(!block.empty()&&stream)ok=write(stream,block.data(),block.size()*sizeof(std::int16_t),nullptr)>=0;
#elif defined(_WIN32)
            if(clear&&stream)waveOutReset(stream);
            if(!block.empty()&&!stream)
            {
                WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=48000;
                format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=96000;
                ok=completion&&waveOutOpen(&stream,WAVE_MAPPER,&format,reinterpret_cast<DWORD_PTR>(completion),0,CALLBACK_EVENT)==MMSYSERR_NOERROR;
            }
            if(!block.empty()&&stream)
            {
                WAVEHDR header{};header.lpData=reinterpret_cast<char*>(block.data());header.dwBufferLength=block.size()*2;
                ok=waveOutPrepareHeader(stream,&header,sizeof(header))==MMSYSERR_NOERROR;
                if(ok)
                {
                    ResetEvent(completion);
                    ok=waveOutWrite(stream,&header,sizeof(header))==MMSYSERR_NOERROR;
                    while(ok&&!(header.dwFlags&WHDR_DONE))
                    {
                        {
                            std::lock_guard lock(mutex);
                            if(quit||flush){waveOutReset(stream);break;}
                        }
                        // Wake at buffer completion instead of adding a polling
                        // interval of silence between consecutive audio blocks.
                        WaitForSingleObject(completion,10);
                    }
                    waveOutUnprepareHeader(stream,&header,sizeof(header));
                }
            }
#else
            ok=block.empty();
#endif
            if(!ok)
            {
#ifdef __linux__
                if(stream){free(stream);stream=nullptr;}
#elif defined(_WIN32)
                if(stream){waveOutReset(stream);waveOutClose(stream);stream=nullptr;}
#endif
                std::lock_guard lock(mutex);failed=true;queue.clear();error="Simulation audio: no playback device available; toggle audio to retry";
            }
        }
#ifdef __linux__
        if(stream){discard(stream,nullptr);free(stream);}
#elif defined(_WIN32)
        if(stream){waveOutReset(stream);waveOutClose(stream);}
        if(completion)CloseHandle(completion);
#endif
    }
};
SIMULATION_AUDIO::SIMULATION_AUDIO():m_impl(std::make_unique<IMPL>()){}
SIMULATION_AUDIO::~SIMULATION_AUDIO()=default;
void SIMULATION_AUDIO::Submit(const std::vector<std::int16_t>& samples,bool enabled)
{
    auto& s=*m_impl;
    {std::lock_guard lock(s.mutex);
        if(enabled!=s.enabled){s.flush=true;s.failed=false;s.error.clear();}s.enabled=enabled;
        if(!enabled){s.queue.clear();}
        else if(!s.failed)
        {
            s.queue.insert(s.queue.end(),samples.begin(),samples.end());
            // Bound latency at 200 ms even when playback falls behind.
            if(s.queue.size()>9600)s.queue.erase(s.queue.begin(),s.queue.end()-9600);
        }
    }s.wake.notify_one();
}
void SIMULATION_AUDIO::Flush()
{
    auto& s=*m_impl;{std::lock_guard lock(s.mutex);s.queue.clear();s.flush=true;}s.wake.notify_one();
}
std::string SIMULATION_AUDIO::Error() const{std::lock_guard lock(m_impl->mutex);return m_impl->error;}
}
