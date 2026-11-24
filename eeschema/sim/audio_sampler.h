/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "interactive_sim_session.h"
#include <algorithm>
#include <cmath>
#include <deque>

namespace CIRCUITALIVE
{
// Integrate every accepted solver interval into 48 kHz bins. Pulse area is
// preserved even for PWM above the audio sample rate; scope history is irrelevant.
class AUDIO_SAMPLER
{
public:
    void Reset() { *this = {}; }
    void Observe( double time, const std::vector<double>& values, const std::vector<SIM_AUDIO_CHANNEL>& channels )
    {
        if(channels.empty())return;
        double value=0;
        for(const auto& c:channels)value+=values.at(c.signal)/c.fullScale*c.volume;
        if(!m_started){m_started=true;m_time=time;m_value=value;m_end=time+1.0/48000;return;}
        if(time<=m_time){m_value=value;return;}
        const double begin=m_time, previous=m_value, duration=time-begin;
        while(m_time<time)
        {
            const double end=std::min(time,m_end);
            const double a=previous+(value-previous)*(m_time-begin)/duration;
            const double b=previous+(value-previous)*(end-begin)/duration;
            m_area+=(a+b)*.5*(end-m_time);m_time=end;
            if(m_time>=m_end-1e-14)
            {
                const double input=m_area*48000;
                // DC block at approximately 20 Hz; PWM duty becomes audible AC.
                m_dc=.997385*m_dc+input-m_previous;m_previous=input;
                if(m_pcm.size()==16384){m_pcm.pop_front();++m_overwritten;}
                m_pcm.push_back(static_cast<std::int16_t>(std::lround(std::clamp(m_dc,-1.0,1.0)*32767)));
                m_end+=1.0/48000;m_area=0;
            }
        }
        m_value=value;
    }
    std::vector<std::int16_t> Samples() const { return {m_pcm.begin(),m_pcm.end()}; }
    void Drain() { m_pcm.clear(); }
    std::uint64_t Overwritten() const { return m_overwritten; }
private:
    bool m_started=false;
    double m_time=0,m_value=0,m_end=0,m_area=0,m_previous=0,m_dc=0;
    std::deque<std::int16_t> m_pcm;
    std::uint64_t m_overwritten=0;
};
}
