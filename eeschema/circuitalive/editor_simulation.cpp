/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "editor_simulation.h"
#include "schematic_adapter.h"
#include "session_runner.h"
#include "simulation_overlay.h"
#include <algorithm>

#include <tools/sch_tool_base.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <schematic.h>
#include <tool/tool_action.h>
#include <wx/aui/framemanager.h>
#include <wx/button.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>

namespace CIRCUITALIVE
{
// Independent action namespace: no additions to upstream SCH_ACTIONS or its simulator commands.
static TOOL_ACTION play( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.play" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Interactive Play" ) ) );
static TOOL_ACTION pause( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.pause" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Interactive Pause" ) ) );
static TOOL_ACTION stop( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.stop" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Interactive Stop" ) ) );

class EDITOR_SIMULATION_TOOL : public wxEvtHandler, public SCH_TOOL_BASE<SCH_EDIT_FRAME>,
                               public SCHEMATIC_LISTENER
{
public:
    EDITOR_SIMULATION_TOOL() : SCH_TOOL_BASE<SCH_EDIT_FRAME>( "circuitalive.Interactive" ),
                               m_timer( this ) {}
    ~EDITOR_SIMULATION_TOOL() override { Shutdown(); }
    void Install();
    void Shutdown();
    void Invalidate();
    void Rebind()
    {
        Invalidate();
        if( m_panel )
            m_frame->Schematic().AddListener( this );
    }
    void Reset( RESET_REASON aReason ) override
    {
        if( aReason == SUPERMODEL_RELOAD || aReason == SHUTDOWN )
            Invalidate();
        SCH_TOOL_BASE<SCH_EDIT_FRAME>::Reset( aReason );
    }
    void OnSchSheetChanged( SCHEMATIC& ) override { redraw(); }
    void OnSchCurrentVariantChanged( SCHEMATIC& ) override { Invalidate(); }

private:
    void setTransitions() override
    {
        Go( &EDITOR_SIMULATION_TOOL::Play, play.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Pause, pause.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Stop, stop.MakeEvent() );
    }
    int Play( const TOOL_EVENT& );
    int Pause( const TOOL_EVENT& );
    int Stop( const TOOL_EVENT& );
    void tick( wxTimerEvent& );
    void click( wxMouseEvent& );
    void redraw();
    void setStatus( const wxString& aText );

    wxTimer m_timer;
    wxPanel* m_panel = nullptr; // owned by the frame/AUI
    wxButton* m_play = nullptr;
    wxButton* m_pause = nullptr;
    wxButton* m_stop = nullptr;
    wxStaticText* m_status = nullptr;
    std::unique_ptr<SESSION_RUNNER> m_runner;
    std::unique_ptr<SIMULATION_OVERLAY> m_overlay;
    SCHEMATIC_SNAPSHOT m_circuit;
    EDITOR_SESSION_VIEW m_session;
    bool m_active = false;
    bool m_errorShown = false;
};

void EDITOR_SIMULATION_TOOL::Install()
{
    if( m_panel )
        return;
    wxFileName worker( wxStandardPaths::Get().GetExecutablePath() );
    // Installed binaries share bin/. The source build places the worker in eeschema/sim/.
    worker.SetFullName( "circuitalive-sim-worker" );
    if( !worker.IsFileExecutable() )
        worker.AppendDir( "sim" );
    m_runner = std::make_unique<SESSION_RUNNER>( worker.GetFullPath().ToStdString() );
    m_overlay = std::make_unique<SIMULATION_OVERLAY>();
    m_frame->Schematic().AddListener( this );
    m_panel = new wxPanel( m_frame );
    auto* row = new wxBoxSizer( wxHORIZONTAL );
    row->Add( new wxStaticText( m_panel, wxID_ANY, "CircuitAlive" ), 0,
              wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8 );
    auto button = [&]( const wxString& label, TOOL_ACTION& action )
    {
        auto* control = new wxButton( m_panel, wxID_ANY, label, wxDefaultPosition,
                                      wxDefaultSize, wxBU_EXACTFIT );
        control->Bind( wxEVT_BUTTON, [this, &action]( wxCommandEvent& )
                       { m_toolMgr->RunAction( action ); } );
        row->Add( control, 0, wxALL, 2 );
        return control;
    };
    m_play = button( _( "Play" ), play );
    m_pause = button( _( "Pause" ), pause );
    m_stop = button( _( "Stop" ), stop );
    m_play->SetToolTip( _( "Start or resume the supported interactive circuit" ) );
    m_pause->SetToolTip( _( "Pause with capacitor and solver state preserved" ) );
    m_stop->SetToolTip( _( "Discard the session and reset initial conditions" ) );
    m_status = new wxStaticText( m_panel, wxID_ANY, _( "Stopped — t = 0 s" ),
                                 wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END );
    m_status->SetMinSize( m_panel->FromDIP( wxSize( 540, -1 ) ) );
    row->Add( m_status, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12 );
    m_panel->SetSizerAndFit( row );
    if( auto* manager = wxAuiManager::GetManager( m_frame ) )
    {
        manager->AddPane( m_panel, wxAuiPaneInfo().Name( "CircuitAliveInteractive" )
                .Caption( "CircuitAlive" ).Top().Layer( 1 ).Row( 1 ).ToolbarPane()
                .Gripper( false ).DockFixed( true ).Resizable( false ) );
        manager->Update();
    }
    Bind( wxEVT_TIMER, &EDITOR_SIMULATION_TOOL::tick, this );
    m_frame->GetCanvas()->Bind( wxEVT_LEFT_DOWN, &EDITOR_SIMULATION_TOOL::click, this );
    m_timer.Start( 33 );
}

void EDITOR_SIMULATION_TOOL::Shutdown()
{
    if( !m_panel )
        return;
    m_timer.Stop();
    Unbind( wxEVT_TIMER, &EDITOR_SIMULATION_TOOL::tick, this );
    m_frame->GetCanvas()->Unbind( wxEVT_LEFT_DOWN, &EDITOR_SIMULATION_TOOL::click, this );
    m_frame->Schematic().RemoveListener( this );
    if( m_overlay && m_frame->GetCanvas()->GetView()->HasItem( m_overlay.get() ) )
        m_frame->GetCanvas()->GetView()->Remove( m_overlay.get() );
    m_overlay.reset();
    m_runner.reset(); // bounded IPC shutdown; no queued callbacks refer to GUI objects
    m_panel = nullptr;
}

void EDITOR_SIMULATION_TOOL::setStatus( const wxString& aText )
{
    if( m_status && m_status->GetLabel() != aText )
    {
        m_status->SetLabel( aText );
        m_status->SetToolTip( aText );
        m_panel->Layout();
    }
}

void EDITOR_SIMULATION_TOOL::Invalidate()
{
    if( !m_runner || !m_active )
        return;
    m_runner->Stop();
    m_active = false;
    m_session = {};
    m_circuit = {};
    redraw();
    setStatus( _( "Circuit changed — press Play to rebuild" ) );
}

int EDITOR_SIMULATION_TOOL::Play( const TOOL_EVENT& )
{
    if( !m_runner )
        return 0;
    const auto state = m_runner->View().state;
    if( m_active && state == EDITOR_SESSION_STATE::PAUSED )
        m_runner->Resume();
    else if( state != EDITOR_SESSION_STATE::RUNNING && state != EDITOR_SESSION_STATE::PREPARING )
    {
        try
        {
            // Freeze a complete graph/model snapshot on this thread; the solver never reads it live.
            m_frame->RecalculateConnections( nullptr, NO_CLEANUP );
            m_circuit = CaptureSchematic( m_frame->Schematic() );
            m_runner->Start( m_circuit.circuit );
            m_active = true;
            m_errorShown = false;
        }
        catch( const std::exception& e )
        {
            setStatus( _( "Circuit needs attention" ) );
            wxMessageBox( wxString::FromUTF8( e.what() ), _( "Interactive simulation" ),
                          wxOK | wxICON_INFORMATION, m_frame );
        }
    }
    return 0;
}

int EDITOR_SIMULATION_TOOL::Pause( const TOOL_EVENT& )
{
    if( m_runner )
        m_runner->Pause();
    return 0;
}

int EDITOR_SIMULATION_TOOL::Stop( const TOOL_EVENT& )
{
    if( m_runner )
        m_runner->Stop();
    m_active = false;
    m_session = {};
    m_circuit = {};
    redraw();
    setStatus( _( "Stopped — t = 0 s" ) );
    return 0;
}

void EDITOR_SIMULATION_TOOL::click( wxMouseEvent& aEvent )
{
    if( m_active && ( m_session.state == EDITOR_SESSION_STATE::RUNNING
                     || m_session.state == EDITOR_SESSION_STATE::PAUSED ) )
    {
        VECTOR2D position = m_frame->GetCanvas()->GetView()->ToWorld(
                VECTOR2D( aEvent.GetPosition().x, aEvent.GetPosition().y ) );
        int index = m_overlay->SwitchAt( m_circuit, m_frame->GetCurrentSheet().PathAsString(),
                                        VECTOR2I( position ) );
        if( index >= 0 )
        {
            m_frame->GetCanvas()->SetFocus();
            m_runner->ToggleSwitch( static_cast<std::size_t>( index ) );
            return; // This click is an electrical event, not a schematic selection/edit.
        }
    }
    aEvent.Skip();
}

void EDITOR_SIMULATION_TOOL::redraw()
{
    if( !m_overlay )
        return;
    auto* view = m_frame->GetCanvas()->GetView();
    m_overlay->Render( m_circuit, m_session, m_frame->GetCurrentSheet().PathAsString() );
    if( !view->HasItem( m_overlay.get() ) )
        view->Add( m_overlay.get() );
    view->Update( m_overlay.get() );
    m_frame->GetCanvas()->Refresh();
}

void EDITOR_SIMULATION_TOOL::tick( wxTimerEvent& )
{
    if( !m_runner )
        return;
    m_session = m_runner->View();
    const auto state = m_session.state;
    m_play->Enable( state != EDITOR_SESSION_STATE::RUNNING && state != EDITOR_SESSION_STATE::PREPARING );
    m_pause->Enable( state == EDITOR_SESSION_STATE::RUNNING );
    m_stop->Enable( m_active );
    if( !m_active )
        return;
    const wxString names[] = { _( "Stopped" ), _( "Preparing" ), _( "Running" ),
                              _( "Paused" ), _( "Completed (1 h limit)" ), _( "Error" ) };
    wxString status = wxString::Format( "%s — t = %.3f s", names[static_cast<int>( state )],
                                       m_session.sample.latest.time );
    for( std::size_t i = 0; i < std::min<std::size_t>( 3, m_session.ledCurrents.size() ); ++i )
    {
        const auto& led = m_circuit.circuit.leds.at( i );
        status += wxString::Format( "   %s: %.2f mA", m_circuit.locations.at( led.component ).reference,
                                    m_session.ledCurrents[i] * 1000 );
    }
    if( m_session.ledCurrents.size() > 3 )
        status += wxString::Format( "   +%zu LEDs", m_session.ledCurrents.size() - 3 );
    setStatus( status );
    redraw();
    if( state == EDITOR_SESSION_STATE::ERROR && !m_errorShown )
    {
        m_errorShown = true;
        wxMessageBox( wxString::FromUTF8( m_session.error ), _( "Interactive simulation" ),
                      wxOK | wxICON_ERROR, m_frame );
    }
}

void RegisterEditorSimulation( TOOL_MANAGER* aManager )
{
    aManager->RegisterTool( new EDITOR_SIMULATION_TOOL );
}

void InstallEditorSimulation( SCH_EDIT_FRAME* aFrame )
{
    aFrame->GetToolManager()->GetTool<EDITOR_SIMULATION_TOOL>()->Install();
}

void ShutdownEditorSimulation( SCH_EDIT_FRAME* aFrame )
{
    if( aFrame->GetToolManager() )
        if( auto* tool = aFrame->GetToolManager()->GetTool<EDITOR_SIMULATION_TOOL>() )
            tool->Shutdown();
}

void InvalidateEditorSimulation( SCH_EDIT_FRAME* aFrame )
{
    if( aFrame->GetToolManager() )
        if( auto* tool = aFrame->GetToolManager()->GetTool<EDITOR_SIMULATION_TOOL>() )
            tool->Invalidate();
}

void RebindEditorSimulation( SCH_EDIT_FRAME* aFrame )
{
    if( aFrame->GetToolManager() )
        if( auto* tool = aFrame->GetToolManager()->GetTool<EDITOR_SIMULATION_TOOL>() )
            tool->Rebind();
}
}
