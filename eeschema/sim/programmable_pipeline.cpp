/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "programmable_pipeline.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CIRCUITALIVE
{
PROGRAMMABLE_PIPELINE::PROGRAMMABLE_PIPELINE() : m_thread( [this]
{
    for( ;; )
    {
        std::function<void()> job;
        {
            std::unique_lock lock( m_mutex );
            m_wake.wait( lock, [this] { return m_quit || bool( m_job ); } );
            if( m_quit ) return;
            job = std::move( m_job );
            m_job = {};
        }
        job();
    }
} )
{}

PROGRAMMABLE_PIPELINE::~PROGRAMMABLE_PIPELINE()
{
    Cancel();
    {
        std::lock_guard lock( m_mutex );
        m_quit = true;
    }
    m_wake.notify_one();
    m_thread.join();
}

void PROGRAMMABLE_PIPELINE::Cancel()
{
    if( m_future.valid() )
    {
        // Stop/reset intentionally discard unfinished results and their errors.
        // Normal consumption uses get() below and propagates backend failures.
        try { m_future.get(); } catch( ... ) {}
    }
}

void PROGRAMMABLE_PIPELINE::Configure( const std::vector<PROGRAMMABLE_DEVICE_CONFIG>& devices )
{
    Cancel();
    m_runtime.Configure( devices );
    m_time = m_window = m_requested = 0;
    m_tolerance = 1e-12;
    if( devices.size() == 1 )
    {
        m_window = devices.front().autonomousWindowSeconds;
        m_tolerance = std::max( 1e-12, devices.front().quantumSeconds * 1e-3 );
    }
}

void PROGRAMMABLE_PIPELINE::Reset()
{
    Cancel();
    m_runtime.Reset();
    m_time = 0;
}

std::vector<PROGRAMMABLE_PIN_STATE> PROGRAMMABLE_PIPELINE::AdvanceTo( double time,
        const std::vector<PROGRAMMABLE_PIN_INPUT>& inputs, double batchTarget )
{
    RESULT result;
    if( m_future.valid() )
    {
        if( std::abs( time - m_requested ) > m_tolerance )
            throw std::runtime_error( "Programmable prefetch exceeded its batch boundary" );
        result = m_future.get();
    }
    else
    {
        result.events = m_runtime.AdvanceTo( time, inputs );
        result.time = m_runtime.Time();
        result.needsInputs = m_runtime.NeedsInputs();
    }
    m_time = result.time;
    if( m_window > 0 && !result.needsInputs && batchTarget > m_time + m_tolerance )
    {
        m_requested = std::min( batchTarget, m_time + m_window );
        if( batchTarget - m_requested < m_tolerance ) m_requested = batchTarget;
        auto task = std::make_shared<std::packaged_task<RESULT()>>( [this, to = m_requested, inputs]
        {
            auto events = m_runtime.AdvanceTo( to, inputs );
            return RESULT{ std::move( events ), m_runtime.Time(), m_runtime.NeedsInputs() };
        } );
        m_future = task->get_future();
        {
            std::lock_guard lock( m_mutex );
            m_job = [task] { ( *task )(); };
        }
        m_wake.notify_one();
    }
    return std::move( result.events );
}
}
