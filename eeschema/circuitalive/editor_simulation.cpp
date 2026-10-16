/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "editor_simulation.h"
#include "schematic_adapter.h"
#include "session_runner.h"
#include "simulation_overlay.h"
#include "simulation_toolbar.h"
#include "model_dialog.h"
#include "instrument_panel.h"
#include "probe_placement.h"
#include <tool/actions.h>
#include <wx/textdlg.h>
#include <algorithm>

#include <tools/sch_tool_base.h>
#include <tools/sch_selection_tool.h>
#include <sch_symbol.h>
#include <sch_screen.h>
#include <sch_commit.h>
#include <project.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <schematic.h>
#include <tool/tool_action.h>
#include <tool/action_manager.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>

namespace CIRCUITALIVE
{
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
        if( m_installed )
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
        Go( &EDITOR_SIMULATION_TOOL::Play, SIMULATION_ACTIONS::play.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Pause, SIMULATION_ACTIONS::pause.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Stop, SIMULATION_ACTIONS::stop.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Configure, SIMULATION_ACTIONS::configure.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::ToggleProbes, SIMULATION_ACTIONS::probes.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::ToggleLogic, SIMULATION_ACTIONS::logicLevels.MakeEvent() );
    }
    int Play( const TOOL_EVENT& );
    int Pause( const TOOL_EVENT& );
    int Stop( const TOOL_EVENT& );
    int Configure( const TOOL_EVENT& );
    int ToggleProbes( const TOOL_EVENT& );
    int ToggleLogic( const TOOL_EVENT& );
    void showInstruments( bool );
    void tick( wxTimerEvent& );
    void click( wxMouseEvent& );
    void key( wxKeyEvent& );
    void chooseProbe( PROBE_KIND );
    void redraw();
    void setStatus( const wxString& aText );
    void publishStatus();

    wxTimer m_timer;
    bool m_installed = false;
    wxString m_status;
    bool m_statusPending = false;
    std::unique_ptr<SESSION_RUNNER> m_runner;
    std::unique_ptr<SIMULATION_OVERLAY> m_overlay;
    std::unique_ptr<INSTRUMENT_PANEL> m_instruments;
    std::optional<PROBE_KIND> m_placing;
    SCHEMATIC_SNAPSHOT m_circuit;
    EDITOR_SESSION_VIEW m_session;
    bool m_active = false;
    bool m_errorShown = false;
    bool m_probePanel = false, m_logicLevels = false, m_instrumentsVisible = false;
    bool m_resultOpened = false;
};

void EDITOR_SIMULATION_TOOL::Install()
{
    if( m_installed )
        return;
    m_runner = std::make_unique<SESSION_RUNNER>( FindSessionWorker(
            wxStandardPaths::Get().GetExecutablePath().ToStdString() ) );
    m_overlay = std::make_unique<SIMULATION_OVERLAY>();
    m_frame->Schematic().AddListener( this );
    m_installed = true;
    auto* selection = m_toolMgr->GetTool<SCH_SELECTION_TOOL>();
    selection->GetToolMenu().GetMenu().AddItem( SIMULATION_ACTIONS::configure,
        []( const SELECTION& selected )
        { return selected.Size() == 1 && selected.Front()->Type() == SCH_SYMBOL_T; } );
    auto* actions = m_toolMgr->GetActionManager();
    actions->SetConditions( SIMULATION_ACTIONS::probes, ACTION_CONDITIONS().Check(
        [this]( const SELECTION& ) { return m_probePanel; } ) );
    actions->SetConditions( SIMULATION_ACTIONS::logicLevels, ACTION_CONDITIONS().Check(
        [this]( const SELECTION& ) { return m_logicLevels; } ) );
    actions->SetConditions( SIMULATION_ACTIONS::play, ACTION_CONDITIONS().Enable(
            [this]( const SELECTION& )
            { return m_session.state != EDITOR_SESSION_STATE::RUNNING
                     && m_session.state != EDITOR_SESSION_STATE::PREPARING; } ) );
    actions->SetConditions( SIMULATION_ACTIONS::pause, ACTION_CONDITIONS().Enable(
            [this]( const SELECTION& ) { return m_session.state == EDITOR_SESSION_STATE::RUNNING; } ) );
    actions->SetConditions( SIMULATION_ACTIONS::stop, ACTION_CONDITIONS().Enable(
            [this]( const SELECTION& ) { return m_active; } ) );
    EnsureSimulationToolbar( *m_frame );
    m_instruments = std::make_unique<INSTRUMENT_PANEL>( m_frame, [this]( PROBE_KIND kind ) { chooseProbe( kind ); } );
    if( auto* manager = wxAuiManager::GetManager( m_frame ) )
    {
        manager->AddPane( m_instruments.get(), wxAuiPaneInfo().Name( "CircuitAliveInstruments" )
            .Caption( _( "Simulation" ) ).Right().Layer( 1 ).Position( 0 ).CloseButton( false )
            .TopDockable( false ).BottomDockable( false ).MinSize( 300, 220 ).BestSize( 350, 650 ).Hide() );
        manager->Update();
    }
    setStatus( _( "Stopped — t = 0 s" ) );
    Bind( wxEVT_TIMER, &EDITOR_SIMULATION_TOOL::tick, this );
    m_frame->GetCanvas()->Bind( wxEVT_LEFT_DOWN, &EDITOR_SIMULATION_TOOL::click, this );
    m_frame->Bind( wxEVT_CHAR_HOOK, &EDITOR_SIMULATION_TOOL::key, this );
    m_timer.Start( 33 );
}

void EDITOR_SIMULATION_TOOL::Shutdown()
{
    if( !m_installed )
        return;
    m_timer.Stop();
    Unbind( wxEVT_TIMER, &EDITOR_SIMULATION_TOOL::tick, this );
    m_frame->GetCanvas()->Unbind( wxEVT_LEFT_DOWN, &EDITOR_SIMULATION_TOOL::click, this );
    m_frame->Unbind( wxEVT_CHAR_HOOK, &EDITOR_SIMULATION_TOOL::key, this );
    if( m_instruments )
    {
        if( auto* manager = wxAuiManager::GetManager( m_frame ) ) manager->DetachPane( m_instruments.get() );
        m_instruments.reset();
    }
    m_frame->Schematic().RemoveListener( this );
    if( m_overlay && m_frame->GetCanvas()->GetView()->HasItem( m_overlay.get() ) )
        m_frame->GetCanvas()->GetView()->Remove( m_overlay.get() );
    m_overlay.reset();
    m_runner.reset(); // bounded IPC shutdown; no queued callbacks refer to GUI objects
    m_installed = false;
}

void EDITOR_SIMULATION_TOOL::setStatus( const wxString& aText )
{
    m_status = aText;
    m_statusPending = true;
}

void EDITOR_SIMULATION_TOOL::publishStatus()
{
    // Publish after the toolbar action completes: native action cleanup can clear field 0.
    // Once idle, leave it available for the usual editor help messages.
    if( m_statusPending && m_frame->GetStatusBar() )
    {
        if( m_frame->GetStatusBar()->GetStatusText( 0 ) != m_status )
            m_frame->SetStatusText( m_status, 0 );
        m_statusPending = false;
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
    m_resultOpened = false;
    showInstruments( m_probePanel );
    if( m_instruments ) m_instruments->UpdateSession( m_circuit, m_session, false );
    redraw();
    setStatus( _( "Circuit changed — press Play to rebuild" ) );
}

int EDITOR_SIMULATION_TOOL::Play( const TOOL_EVENT& )
{
    m_placing.reset();
    if( m_instruments ) m_instruments->SetPlacement( {} );
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
            m_circuit = CaptureSchematic( m_frame->Schematic(), true );
            m_runner->Start( m_circuit.circuit );
            m_resultOpened = false;
            m_active = true;
            m_errorShown = false;
        }
        catch( const std::exception& e )
        {
            setStatus( _( "Circuit needs attention" ) + " — " + wxString::FromUTF8( e.what() ).Left( 160 ) );
            wxMessageBox( wxString::FromUTF8( e.what() ), _( "Interactive simulation" ),
                          wxOK | wxICON_INFORMATION, m_frame );
        }
    }
    return 0;
}

void EDITOR_SIMULATION_TOOL::showInstruments( bool visible )
{
    if( visible == m_instrumentsVisible ) return;
    if( auto* manager = wxAuiManager::GetManager( m_frame ) )
    {
        manager->GetPane( m_instruments.get() ).Show( visible ); manager->Update();
        m_instrumentsVisible = visible;
    }
}

int EDITOR_SIMULATION_TOOL::ToggleProbes( const TOOL_EVENT& )
{
    m_probePanel = !m_probePanel;
    m_placing.reset(); m_instruments->SetPlacement( {} );
    m_toolMgr->RunAction( ACTIONS::cancelInteractive );
    m_toolMgr->RunAction( ACTIONS::selectionTool );
    showInstruments( m_probePanel );
    if( m_probePanel ) m_instruments->SelectTools();
    m_frame->GetCanvas()->SetFocus();
    return 0;
}

int EDITOR_SIMULATION_TOOL::ToggleLogic( const TOOL_EVENT& )
{
    m_logicLevels = !m_logicLevels; redraw(); return 0;
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
    m_resultOpened = false;
    showInstruments( m_probePanel );
    if( m_instruments ) m_instruments->UpdateSession( m_circuit, m_session, false );
    redraw();
    setStatus( _( "Stopped — t = 0 s" ) );
    return 0;
}

int EDITOR_SIMULATION_TOOL::Configure( const TOOL_EVENT& )
{
    auto& selection = m_toolMgr->GetTool<SCH_SELECTION_TOOL>()->RequestSelection( { SCH_SYMBOL_T } );
    if( selection.Size() != 1 ) return 0;
    auto* symbol = static_cast<SCH_SYMBOL*>( selection.Front() );
    m_placing.reset(); if( m_instruments ) m_instruments->SetPlacement( {} );
    try
    {
        if( symbol->GetField( "CA.Probe" ) )
        {
            wxTextEntryDialog dialog( m_frame, _( "Probe name (blank uses the net label or A, B, C...)" ),
                _( "Probe properties" ), symbol->GetField( FIELD_T::VALUE )->GetText() );
            if( dialog.ShowModal() == wxID_OK )
            {
                SCH_COMMIT commit( m_frame ); commit.Modify( symbol, m_frame->GetScreen() );
                symbol->GetField( FIELD_T::VALUE )->SetText( dialog.GetValue() );
                commit.Push( _( "Rename simulation probe" ) ); Invalidate();
            }
            return 0;
        }
        auto component = ReadSymbolModelSettings( *symbol, m_frame->GetCurrentSheet(),
            m_frame->Schematic().GetCurrentVariant(), m_frame->Prj().GetProjectPath().ToStdString() );
        if( EditSimulationModel( m_frame, component ) )
        {
            SCH_COMMIT commit( m_frame );
            for( auto& sheet : m_frame->Schematic().Hierarchy() )
                for( auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
                {
                    auto* unit = static_cast<SCH_SYMBOL*>( item );
                    if( unit != symbol && ( !symbol->IsMultiUnit() || unit->GetRef( &sheet ).ToStdString() != component.reference
                        || unit->GetLibId() != symbol->GetLibId() ) ) continue;
                    commit.Modify( unit, sheet.LastScreen() ); WriteSymbolModelSettings( *unit, component );
                }
            commit.Push( _( "Configure simulation model" ) );
            Invalidate();
        }
    }
    catch( const std::exception& e )
    {
        wxMessageBox( wxString::FromUTF8( e.what() ), _( "Simulation model" ), wxOK, m_frame );
    }
    return 0;
}

void EDITOR_SIMULATION_TOOL::click( wxMouseEvent& aEvent )
{
    if( m_placing )
    {
        try
        {
            const auto position = m_frame->GetCanvas()->GetView()->ToWorld( VECTOR2D( aEvent.GetPosition().x, aEvent.GetPosition().y ) );
            PlaceProbe( *m_frame, *m_placing, m_instruments->ProbeName(), VECTOR2I( position ),
                        std::max( 1, int( m_frame->GetCanvas()->GetView()->ToWorld( 12.0 ) ) ) );
            m_instruments->ClearProbeName();
            setStatus( _( "Probe placed — click another wire or press Esc" ) );
        }
        catch( const std::exception& e ) { setStatus( wxString::FromUTF8( e.what() ) ); }
        return;
    }
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

void EDITOR_SIMULATION_TOOL::chooseProbe( PROBE_KIND kind )
{
    m_toolMgr->RunAction( ACTIONS::cancelInteractive );
    m_placing = kind; m_instruments->SetPlacement( kind );
    setStatus( _( "Click a wire to place the probe; Esc finishes placement" ) );
    m_frame->GetCanvas()->SetFocus();
}

void EDITOR_SIMULATION_TOOL::key( wxKeyEvent& event )
{
    if( m_placing && event.GetKeyCode() == WXK_ESCAPE )
    {
        m_placing.reset(); m_instruments->SetPlacement( {} );
        setStatus( _( "Probe placement finished" ) ); return;
    }
    event.Skip();
}

void EDITOR_SIMULATION_TOOL::redraw()
{
    if( !m_overlay )
        return;
    auto* view = m_frame->GetCanvas()->GetView();
    m_overlay->Render( m_circuit, m_session, m_frame->GetCurrentSheet().PathAsString(), m_logicLevels );
    if( !view->HasItem( m_overlay.get() ) )
        view->Add( m_overlay.get() );
    view->Update( m_overlay.get() );
    m_frame->GetCanvas()->Refresh();
}

void EDITOR_SIMULATION_TOOL::tick( wxTimerEvent& )
{
    if( !m_runner )
        return;
    EnsureSimulationToolbar( *m_frame );
    m_session = m_runner->View();
    const auto state = m_session.state;
    if( m_instruments ) m_instruments->UpdateSession( m_circuit, m_session, m_active );
    if( m_active && !m_circuit.circuit.measurements.empty() && !m_instrumentsVisible
        && !m_resultOpened )
    { showInstruments( true ); m_resultOpened = true; }
    if( !m_active )
    {
        publishStatus();
        return;
    }
    const wxString names[] = { _( "Stopped" ), _( "Preparing" ), _( "Running" ),
                              _( "Paused" ), _( "Completed" ), _( "Error" ) };
    wxString status = wxString::Format( "%s — t = %.3f s", names[static_cast<int>( state )],
                                       m_session.sample.latest.time );
    for( std::size_t i = 0; i < std::min<std::size_t>( 2, m_session.warnings.size() ); ++i )
        status += "   " + wxString::FromUTF8( m_session.warnings[i] );
    if( m_session.warnings.size() > 2 ) status += wxString::Format( _( "   +%zu warnings" ), m_session.warnings.size() - 2 );
    if( !m_circuit.circuit.floatingIslands.empty() )
        status += wxString::Format( _( "   Floating: %zu" ), m_circuit.circuit.floatingIslands.size() );
    if( state == EDITOR_SESSION_STATE::ERROR ) status += "   " + wxString::FromUTF8( m_session.error ).Left( 140 );
    setStatus( status );
    publishStatus();
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
