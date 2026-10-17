/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <tool/tool_event.h>
#include <wx/settings.h>
#include <wx/timer.h>
#include <cstdint>
#include <functional>

namespace CIRCUITALIVE
{
// Keep the native menu path, but allow the second right click to arrive before opening it.
class RIGHT_CLICK_GESTURE : public wxEvtHandler
{
public:
    explicit RIGHT_CLICK_GESTURE( std::function<void( const TOOL_EVENT& )> aPost ) :
            m_timer( this ), m_post( std::move( aPost ) )
    {
        Bind( wxEVT_TIMER, [this]( wxTimerEvent& )
        {
            auto event = ContextEvent();
            event.SetMousePosition( m_position );
            event.SetParameter( m_generation );
            m_post( event );
        } );
    }

    void Defer( const TOOL_EVENT& aClick )
    {
        Cancel();
        m_position = aClick.Position();
        m_pending = true;
        const int delay = wxSystemSettings::GetMetric( wxSYS_DCLICK_MSEC );
        m_timer.StartOnce( delay > 0 ? delay + 20 : 420 );
    }

    void Cancel()
    {
        m_timer.Stop();
        m_pending = false;
        ++m_generation;
    }

    bool Consume( const TOOL_EVENT& aEvent )
    {
        if( !m_pending || aEvent.Parameter<std::uint64_t>() != m_generation )
            return false;

        Cancel();
        return true;
    }

    static TOOL_EVENT ContextEvent()
    {
        return TOOL_EVENT( TC_COMMAND, TA_ACTION, std::string( "circuitalive.Schematic.contextMenu" ) );
    }

private:
    wxTimer m_timer;
    std::function<void( const TOOL_EVENT& )> m_post;
    VECTOR2D m_position;
    std::uint64_t m_generation = 0;
    bool m_pending = false;
};
}
