/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "editor_layout.h"
#include <eeschema_settings.h>
#include <sch_edit_frame.h>
#include <settings/settings_manager.h>
#include <tool/tool_manager.h>
#include <tools/sch_tool_base.h>
#include <wx/aui/aui.h>
#include <wx/timer.h>

namespace CIRCUITALIVE
{
class EDITOR_LAYOUT_TOOL : public wxEvtHandler, public SCH_TOOL_BASE<SCH_EDIT_FRAME>
{
public:
    EDITOR_LAYOUT_TOOL() : SCH_TOOL_BASE<SCH_EDIT_FRAME>( "circuitalive.EditorLayout" ), m_timer( this ) {}

    ~EDITOR_LAYOUT_TOOL() override
    {
        m_timer.Stop();
        if( m_frame )
            m_frame->Unbind( wxEVT_IDLE, &EDITOR_LAYOUT_TOOL::idle, this );
    }

    bool Init() override
    {
        if( !SCH_TOOL_BASE<SCH_EDIT_FRAME>::Init() )
            return false;
        m_frame->Bind( wxEVT_IDLE, &EDITOR_LAYOUT_TOOL::idle, this );
        Bind( wxEVT_TIMER, &EDITOR_LAYOUT_TOOL::save, this, m_timer.GetId() );
        return true;
    }

private:
    void setTransitions() override {}

    void idle( wxIdleEvent& event )
    {
        event.Skip();
        if( m_saving || m_frame->IsClosing() || !m_frame->IsShown() || m_frame->IsIconized() )
            return;
        // SavePerspective is also available with wx 3.3. Use it only as a change signature;
        // SaveWindowSettings chooses the appropriate native serialization on each platform.
        const wxPoint position = m_frame->GetPosition();
        const wxSize size = m_frame->GetSize();
        const wxString state = wxAuiManager::GetManager( m_frame )->SavePerspective()
                + wxString::Format( "|window=%d,%d,%d,%d,%d", position.x, position.y,
                                    size.x, size.y, m_frame->IsMaximized() );
        if( state != m_state )
        {
            m_state = state;
            m_timer.StartOnce( 300 );
        }
    }

    void save( wxTimerEvent& )
    {
        if( m_frame->IsClosing() || m_frame->IsIconized() )
            return;
        m_saving = true;
        EESCHEMA_SETTINGS* cfg = m_frame->eeconfig();
        // Do not update project file/session state when the user only moves a panel.
        m_frame->SaveEditorLayoutSettings();
        m_frame->GetSettingsManager()->Save( cfg );
        m_saving = false;
    }

    wxTimer m_timer;
    wxString m_state;
    bool m_saving = false;
};

void RegisterEditorLayout( TOOL_MANAGER* manager )
{
    manager->RegisterTool( new EDITOR_LAYOUT_TOOL );
}
}
