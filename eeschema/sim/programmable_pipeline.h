/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "programmable_runtime.h"
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace CIRCUITALIVE
{
/** One bounded CPU job overlaps the analog solver. Inputs are refreshed at
 * explicit firmware barriers; speculative execution never passes a user-visible
 * batch boundary. Stop/reset discard the job before touching its context.
 */
class PROGRAMMABLE_PIPELINE
{
public:
    PROGRAMMABLE_PIPELINE();
    ~PROGRAMMABLE_PIPELINE();
    void Configure( const std::vector<PROGRAMMABLE_DEVICE_CONFIG>& aDevices );
    void Reset();
    void Cancel();
    std::vector<PROGRAMMABLE_PIN_STATE> AdvanceTo( double aTime,
        const std::vector<PROGRAMMABLE_PIN_INPUT>& aInputs, double aBatchTarget = 0 );
    double Time() const { return m_time; }
    bool HasPrefetch() const { return m_future.valid(); }
private:
    struct RESULT
    {
        std::vector<PROGRAMMABLE_PIN_STATE> events;
        double time;
        bool needsInputs;
    };
    PROGRAMMABLE_RUNTIME m_runtime;
    double m_time = 0, m_window = 0, m_tolerance = 1e-12, m_requested = 0;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_quit = false;
    std::function<void()> m_job;
    std::future<RESULT> m_future;
    std::thread m_thread;
};
}
