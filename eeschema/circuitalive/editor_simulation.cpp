/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "editor_simulation.h"
#include "schematic_adapter.h"
#include "session_runner.h"
#include "visual_signals.h"
#include "simulation_overlay.h"
#include "simulation_toolbar.h"
#include "model_dialog.h"
#include "memory_viewer.h"
#include "simulation_model.h"
#include "instrument_panel.h"
#include "probe_placement.h"
#include "source_probe_dialog.h"
#include "probe_symbol.h"
#include <tools/ee_grid_helper.h>
#include <tools/sch_tool_utils.h>
#include <sch_pin.h>
#include <view/view_controls.h>
#include <gal/graphics_abstraction_layer.h>
#include <tool/tool_manager.h>
#include "live_control_popup.h"
#include "control_references.h"
#include <tool/actions.h>
#include <wx/textdlg.h>
#include <algorithm>

#include <tools/sch_tool_base.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_actions.h>
#include <sch_symbol.h>
#include <sch_screen.h>
#include <sch_commit.h>
#include <project.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <eeschema_settings.h>
#include <paths.h>
#include <widgets/wx_aui_utils.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <schematic.h>
#include <tool/tool_action.h>
#include <tool/action_manager.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/statusbr.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <wx/weakref.h>

namespace CIRCUITALIVE
{
namespace
{
TOOL_ACTION placeProbes( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.placeProbes" )
        .Scope( AS_GLOBAL ).Flags( AF_ACTIVATE ) );
}

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
    void ReloadPreferences();
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
    void OnSchSheetChanged( SCHEMATIC& ) override { dismissControl(); redraw(); }
    void OnSchCurrentVariantChanged( SCHEMATIC& ) override { Invalidate(); }

private:
    void setTransitions() override
    {
        Go( &EDITOR_SIMULATION_TOOL::Play, SIMULATION_ACTIONS::play.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Pause, SIMULATION_ACTIONS::pause.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Stop, SIMULATION_ACTIONS::stop.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Memory, SIMULATION_ACTIONS::memory.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Configure, SIMULATION_ACTIONS::configure.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::Configure, SIMULATION_ACTIONS::probeProperties.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::PlaceProbes, placeProbes.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::ToggleProbes, SIMULATION_ACTIONS::probes.MakeEvent() );
        Go( &EDITOR_SIMULATION_TOOL::ToggleLogic, SIMULATION_ACTIONS::logicLevels.MakeEvent() );
    }
    int Play( const TOOL_EVENT& );
    int Pause( const TOOL_EVENT& );
    int Stop( const TOOL_EVENT& );
    int Configure( const TOOL_EVENT& );
    int PlaceProbes( const TOOL_EVENT& );
    int Memory( const TOOL_EVENT& );
    int ToggleProbes( const TOOL_EVENT& );
    int ToggleLogic( const TOOL_EVENT& );
    void showInstruments( bool );
    void rememberInstruments();
    void saveInstrumentLayout();
    void tick( wxTimerEvent& );
    void click( wxMouseEvent& );
    void chooseProbe( PROBE_KIND );
    void redraw( bool aForce = true );
    void setStatus( const wxString& aText );
    void publishStatus();
    void dismissControl();

    wxTimer m_timer;
    std::chrono::steady_clock::time_point m_lastHud{}, m_lastToolbar{};
    std::chrono::steady_clock::time_point m_rateStarted{};
    double m_rateTime = 0, m_realSpeed = 0;
    bool m_rateValid = false;
    bool m_installed = false;
    wxString m_status;
    bool m_statusPending = false;
    std::shared_ptr<SESSION_RUNNER> m_runner;
    std::unique_ptr<SIMULATION_OVERLAY> m_overlay;
    std::unique_ptr<INSTRUMENT_PANEL> m_instruments;
    std::optional<PROBE_KIND> m_placing;
    std::map<PROBE_KIND, SOURCE_PROBE_SETTINGS> m_sourceSettings;
    SCHEMATIC_SNAPSHOT m_circuit;
    EDITOR_SESSION_VIEW m_session;
    SIMULATION_PREFERENCES m_preferences;
    bool m_active = false;
    bool m_errorShown = false;
    bool m_probePanel = false, m_logicLevels = false, m_instrumentsVisible = false;
    bool m_resultOpened = false;
    std::optional<wxAuiPaneInfo> m_instrumentLayout;
    wxString m_instrumentLayoutKey;
    bool m_instrumentLayoutPending = false;
    std::chrono::steady_clock::time_point m_instrumentLayoutChanged{};
    bool m_overlayCached = false, m_overlayVisible = false, m_overlayLogic = false;
    wxString m_overlaySheet;
    std::vector<int> m_overlayBrightness;
    std::vector<bool> m_overlaySwitches;
    std::vector<double> m_overlayAdjustments;
    std::vector<LOGIC_LEVEL> m_overlayLevels;
    std::vector<bool> m_overlayRapid;
    std::vector<bool> m_overlayContentions;
    wxWeakRef<LIVE_CONTROL_POPUP> m_liveControl;
};

void EDITOR_SIMULATION_TOOL::Install()
{
    if( m_installed )
        return;
    m_runner = std::make_shared<SESSION_RUNNER>( FindSessionWorker(
            wxStandardPaths::Get().GetExecutablePath().ToStdString(wxConvUTF8) ) );
    m_overlay = std::make_unique<SIMULATION_OVERLAY>();
    m_frame->Schematic().AddListener( this );
    m_installed = true;
    auto* selection = m_toolMgr->GetTool<SCH_SELECTION_TOOL>();
    selection->GetToolMenu().GetMenu().AddItem( SIMULATION_ACTIONS::configure,
        []( const SELECTION& selected )
        { return selected.Size() == 1 && selected.Front()->Type() == SCH_SYMBOL_T
                 && !ProbeSymbolForItem( selected.Front() ); } );
    selection->GetToolMenu().GetMenu().AddItem( SIMULATION_ACTIONS::probeProperties,
        []( const SELECTION& selected )
        { return selected.Size() == 1 && ProbeSymbolForItem( selected.Front() ); } );
    selection->GetToolMenu().GetMenu().AddItem( SIMULATION_ACTIONS::memory,
        [this]( const SELECTION& selected )
        {
            if(selected.Size()!=1 || selected.Front()->Type()!=SCH_SYMBOL_T)return false;
            auto* symbol=static_cast<SCH_SYMBOL*>(selected.Front());
            if(symbol->GetExcludedFromSim(&m_frame->GetCurrentSheet(),m_frame->Schematic().GetCurrentVariant()))return false;
            try{
                auto component=ReadSymbolModelSettings(*symbol,m_frame->GetCurrentSheet(),m_frame->Schematic().GetCurrentVariant(),m_frame->Prj().GetProjectPath().ToStdString(wxConvUTF8),true);
                return !MODEL_REPOSITORY(MODEL_REPOSITORY::DefaultDirectories(component.projectDirectory)).SelectModel(component).memories.empty();
            }catch(const std::exception&){return false;}
        } );
    auto* actions = m_toolMgr->GetActionManager();
    actions->SetConditions( SIMULATION_ACTIONS::probes, ACTION_CONDITIONS().Check(
        [this]( const SELECTION& ) { return m_instrumentsVisible; } ) );
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
    m_instruments = std::make_unique<INSTRUMENT_PANEL>( m_frame,
        [this]( PROBE_KIND kind ) { chooseProbe( kind ); },
        [this]( double speed )
        {
            m_preferences.playbackSpeed = speed;
            m_frame->eeconfig()->m_RealtimeSimulation.playbackSpeed = speed;
            m_frame->GetSettingsManager()->Save( m_frame->eeconfig() );
            m_runner->SetSpeed( speed );
        }, [this]() { m_runner->Step(); } );
    if( auto* manager = wxAuiManager::GetManager( m_frame ) )
    {
        wxAuiPaneInfo pane = wxAuiPaneInfo().Name( "CircuitAliveInstruments" )
            .Caption( _( "Simulation" ) ).Right().Layer( 1 ).Position( 0 ).CloseButton( false )
            .TopDockable( false ).BottomDockable( false )
            .MinSize( m_frame->FromDIP( wxSize( 300, 220 ) ) )
            .BestSize( m_frame->FromDIP( wxSize( 350, 650 ) ) )
            .FloatingSize( m_frame->FromDIP( wxSize( 900, 750 ) ) );
        if( const auto saved = m_frame->eeconfig()->Get<wxAuiPaneInfo>( "appearance.simulation_pane" ) )
        {
            pane.SafeSet( *saved );
            pane.Name( "CircuitAliveInstruments" ).Caption( _( "Simulation" ) );
            m_instrumentLayout = pane;
        }
        pane.Hide();
        manager->AddPane( m_instruments.get(), pane );
        manager->Update();
    }
    setStatus( _( "Stopped — t = 0 s" ) );
    Bind( wxEVT_TIMER, &EDITOR_SIMULATION_TOOL::tick, this );
    m_frame->GetCanvas()->Bind( wxEVT_LEFT_DOWN, &EDITOR_SIMULATION_TOOL::click, this );
    ReloadPreferences();
}

void EDITOR_SIMULATION_TOOL::ReloadPreferences()
{
    if( !m_installed ) return;
    if( const auto* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) )
        m_preferences = cfg->m_RealtimeSimulation;
    m_preferences.Normalize();
    m_runner->SetPreferences( m_preferences );
    m_logicLevels = m_preferences.showDigitalIndicators;
    m_timer.Start( m_preferences.FrameInterval().count() );
    redraw();
}

void EDITOR_SIMULATION_TOOL::Shutdown()
{
    if( !m_installed )
        return;
    m_timer.Stop();
    rememberInstruments();
    saveInstrumentLayout();
    dismissControl();
    Unbind( wxEVT_TIMER, &EDITOR_SIMULATION_TOOL::tick, this );
    m_frame->GetCanvas()->Unbind( wxEVT_LEFT_DOWN, &EDITOR_SIMULATION_TOOL::click, this );
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
    dismissControl();
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
            SCH_COMMIT probeIds( m_frame );
            EnsureUniqueProbeIdentities( m_frame->Schematic(), {}, &probeIds );
            if( !probeIds.Empty() ) probeIds.Push( _( "Assign probe identifiers" ) );
            m_frame->RecalculateConnections( nullptr, NO_CLEANUP );
            m_circuit = CaptureSchematic( m_frame->Schematic(), true, m_preferences );
            m_runner->SetSpeed( m_preferences.playbackSpeed );
            m_runner->Start( m_circuit.circuit );
            m_resultOpened = false;
            m_active = true;
            m_errorShown = false;
        }
        catch( const std::exception& e )
        {
            setStatus( _( "Circuit needs attention" ) + " — " + DiagnosticText( e.what() ).Left( 160 ) );
            wxMessageBox( DiagnosticText( e.what() ), _( "Interactive simulation" ),
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
        rememberInstruments();
        auto& pane = manager->GetPane( m_instruments.get() );
        if( visible && m_instrumentLayout )
            pane.SafeSet( *m_instrumentLayout );
        pane.Show( visible );
        manager->Update();
        if( visible && m_instrumentLayout && pane.IsDocked() && pane.best_size.x > 0 )
            SetAuiPaneSize( *manager, pane, pane.best_size.x, -1 );
        m_instrumentsVisible = visible;
        if( !visible )
            saveInstrumentLayout();
    }
}

void EDITOR_SIMULATION_TOOL::rememberInstruments()
{
    if( !m_instruments || !m_instrumentsVisible )
        return;
    auto* manager = wxAuiManager::GetManager( m_frame );
    // Opening/updating the PCB temporarily blocks schematic events. AUI finds
    // its manager through an event, so timer callbacks cannot query it then.
    if( !manager ) return;
    wxAuiPaneInfo pane = manager->GetPane( m_instruments.get() );
    if( !pane.IsOk() || !pane.IsShown() )
        return;
    // AUI stores dock widths on the dock, not on the pane's best_size. Keep the actual
    // size as well, so recreating a dock after Hide does not restore the original width.
    if( pane.IsDocked() )
    {
        pane.BestSize( m_instruments->GetSize() );
        manager->GetPane( m_instruments.get() ).BestSize( pane.best_size );
    }
    else if( m_instrumentLayout )
        pane.BestSize( m_instrumentLayout->best_size );
    const wxString key = manager->SavePaneInfo( pane );
    if( key != m_instrumentLayoutKey )
    {
        m_instrumentLayout = pane;
        m_instrumentLayoutKey = key;
        m_frame->eeconfig()->Set<wxAuiPaneInfo>( "appearance.simulation_pane", pane );
        m_instrumentLayoutPending = true;
        m_instrumentLayoutChanged = std::chrono::steady_clock::now();
    }
}

void EDITOR_SIMULATION_TOOL::saveInstrumentLayout()
{
    if( m_instrumentLayoutPending )
    {
        m_frame->eeconfig()->SaveToFile( PATHS::GetUserSettingsPath(), true );
        m_instrumentLayoutPending = false;
    }
}

int EDITOR_SIMULATION_TOOL::ToggleProbes( const TOOL_EVENT& )
{
    m_probePanel = !m_instrumentsVisible;
    m_placing.reset(); m_instruments->SetPlacement( {} );
    m_toolMgr->RunAction( ACTIONS::cancelInteractive );
    m_toolMgr->RunAction( ACTIONS::selectionTool );
    showInstruments( m_probePanel );
    m_frame->GetCanvas()->SetFocus();
    return 0;
}

int EDITOR_SIMULATION_TOOL::ToggleLogic( const TOOL_EVENT& )
{
    m_logicLevels = !m_logicLevels;
    m_preferences.showDigitalIndicators = m_logicLevels;
    if( auto* cfg = GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) )
        cfg->m_RealtimeSimulation.showDigitalIndicators = m_logicLevels;
    redraw();
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
    dismissControl();
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

int EDITOR_SIMULATION_TOOL::Memory( const TOOL_EVENT& )
{
    auto& selection = m_toolMgr->GetTool<SCH_SELECTION_TOOL>()->RequestSelection( { SCH_SYMBOL_T } );
    if( selection.Size() != 1 )
        return 0;
    auto* symbol = static_cast<SCH_SYMBOL*>( selection.Front() );
    try
    {
        auto component =
                ReadSymbolModelSettings( *symbol, m_frame->GetCurrentSheet(), m_frame->Schematic().GetCurrentVariant(),
                                         m_frame->Prj().GetProjectPath().ToStdString(wxConvUTF8) );
        std::vector<MEMORY_REGION_CONFIG> regions;
        std::vector<std::size_t> slots;
        if( m_active )
        {
            for( const auto& region : m_circuit.circuit.memories )
            {
                const auto& location = m_circuit.locations.at( region.component );
                if( location.reference.ToStdString(wxConvUTF8) == component.reference )
                {
                    slots.push_back( region.slot );
                    regions.push_back( m_circuit.circuit.config.memories.at( region.slot ) );
                }
            }
            if( regions.empty() )
                throw std::runtime_error( "This component has no memory in the active simulation. Stop and restart "
                                          "after changing its model." );
            ShowMemoryViewer( m_frame, component.reference, regions, m_runner, slots );
        }
        else
        {
            auto model = MODEL_REPOSITORY( MODEL_REPOSITORY::DefaultDirectories( component.projectDirectory ) )
                                 .SelectModel( component );
            regions = InitialModelMemories( component, model );
            ShowMemoryViewer( m_frame, component.reference, regions, m_runner );
        }
    }
    catch( const std::exception& e )
    {
        wxMessageBox( DiagnosticText( e.what() ), _( "Memory" ), wxOK | wxICON_INFORMATION, m_frame );
    }
    return 0;
}

bool EditSimulationProbe( SCH_EDIT_FRAME* frame, SCH_SYMBOL* symbol )
{
    if( !symbol || !symbol->GetField( "CA.Probe" ) )
        return false;
    try
    {
        const auto kind = ParseProbeKind( symbol->GetField( "CA.Probe" )->GetText().ToStdString() );
        if( IsSourceProbe( kind ) )
        {
            auto settings = ReadSourceProbeSettings( *symbol );
            auto name = symbol->GetField( FIELD_T::VALUE )->GetText().ToStdString();
            if( EditSourceProbe( frame, kind, settings, name ) )
            {
                SCH_COMMIT commit( frame );
                commit.Modify( symbol, frame->GetScreen() );
                WriteSourceProbeSettings( *symbol, kind, settings );
                symbol->GetField( FIELD_T::VALUE )->SetText( wxString::FromUTF8( name ) );
                commit.Push( _( "Configure source probe" ) );
                InvalidateEditorSimulation( frame );
            }
        }
        else
        {
            wxTextEntryDialog dialog( frame, _( "Probe name (blank uses the net label or A, B, C...)" ),
                    _( "Probe properties" ), symbol->GetField( FIELD_T::VALUE )->GetText() );
            if( dialog.ShowModal() == wxID_OK )
            {
                SCH_COMMIT commit( frame );
                commit.Modify( symbol, frame->GetScreen() );
                symbol->GetField( FIELD_T::VALUE )->SetText( dialog.GetValue() );
                commit.Push( _( "Rename simulation probe" ) );
                InvalidateEditorSimulation( frame );
            }
        }
    }
    catch( const std::exception& error )
    {
        wxMessageBox( DiagnosticText( error.what() ), _( "Probe properties" ), wxOK | wxICON_ERROR, frame );
    }
    return true;
}

int EDITOR_SIMULATION_TOOL::Configure( const TOOL_EVENT& )
{
    auto& selection = m_toolMgr->GetTool<SCH_SELECTION_TOOL>()->RequestSelection( { SCH_SYMBOL_T } );
    if( selection.Size() != 1 ) return 0;
    auto* symbol = static_cast<SCH_SYMBOL*>( selection.Front() );
    m_placing.reset(); if( m_instruments ) m_instruments->SetPlacement( {} );
    try
    {
        if( EditSimulationProbe( m_frame, symbol ) )
            return 0;
        auto component = ReadSymbolModelSettings( *symbol, m_frame->GetCurrentSheet(),
            m_frame->Schematic().GetCurrentVariant(), m_frame->Prj().GetProjectPath().ToStdString(wxConvUTF8), true );
        bool excluded = symbol->GetExcludedFromSim( &m_frame->GetCurrentSheet(),
                                                   m_frame->Schematic().GetCurrentVariant() );
        if( EditSimulationModel( m_frame, component, excluded ) )
        {
            SCH_COMMIT commit( m_frame );
            for( auto& sheet : m_frame->Schematic().Hierarchy() )
                for( auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
                {
                    auto* unit = static_cast<SCH_SYMBOL*>( item );
                    if( unit != symbol && ( !symbol->IsMultiUnit() || unit->GetRef( &sheet ).ToStdString(wxConvUTF8) != component.reference
                        || unit->GetLibId() != symbol->GetLibId() ) ) continue;
                    commit.Modify( unit, sheet.LastScreen() );
                    unit->SetExcludedFromSim( excluded, &sheet, m_frame->Schematic().GetCurrentVariant() );
                    if( !excluded ) WriteSymbolModelSettings( *unit, component );
                }
            commit.Push( _( "Configure simulation model" ) );
            Invalidate();
        }
    }
    catch( const std::exception& e )
    {
        wxMessageBox( DiagnosticText( e.what() ), _( "Simulation model" ), wxOK, m_frame );
    }
    return 0;
}

void EDITOR_SIMULATION_TOOL::click( wxMouseEvent& aEvent )
{
    if( m_placing )
    {
        // The native placement tool handles these clicks, including its moving preview.
        aEvent.Skip();
        return;
    }
    if( m_active && ( m_session.state == EDITOR_SESSION_STATE::RUNNING
                     || m_session.state == EDITOR_SESSION_STATE::PAUSED ) )
    {
        VECTOR2D position = m_frame->GetCanvas()->GetView()->ToWorld(
                VECTOR2D( aEvent.GetPosition().x, aEvent.GetPosition().y ) );
        const int adjustment = m_overlay->AdjustmentAt( m_circuit,
            m_frame->GetCurrentSheet().Path().AsString(), VECTOR2I( position ) );
        if( adjustment >= 0 )
        {
            dismissControl();
            const auto component = m_circuit.circuit.adjustments.at( adjustment ).component;
            auto controls=m_circuit.circuit.adjustments;
            for(auto& control:controls)
            {
                control.label=LiveControlText(m_circuit.circuit,m_session.switches,m_session.adjustments,control.component,control.id,"label",control.label);
                control.unit=LiveControlText(m_circuit.circuit,m_session.switches,m_session.adjustments,control.component,control.id,"unit",control.unit);
            }
            m_liveControl = new LIVE_CONTROL_POPUP( m_frame,
                m_circuit.locations.at( component ).reference.ToStdString(wxConvUTF8),
                controls, m_session.adjustments, component,
                [this]( std::size_t index, double value ) { m_runner->SetAdjustment( index, value ); },
                m_circuit.circuit.adjustments[adjustment].visual.custom ? adjustment : NO_SIGNAL );
            m_liveControl->Position( m_frame->GetCanvas()->ClientToScreen( aEvent.GetPosition() ), wxSize( 0, 16 ) );
            m_liveControl->Popup();
            return;
        }
        int index = m_overlay->SwitchAt( m_circuit, m_frame->GetCurrentSheet().Path().AsString(),
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

void EDITOR_SIMULATION_TOOL::dismissControl()
{
    if( m_liveControl ) { m_liveControl->Dismiss(); m_liveControl->Destroy(); m_liveControl = nullptr; }
}

void EDITOR_SIMULATION_TOOL::chooseProbe( PROBE_KIND kind )
{
    if( IsSourceProbe( kind ) )
    {
        auto name = m_instruments->ProbeName();
        auto settings = m_sourceSettings[kind];
        if( !EditSourceProbe( m_frame, kind, settings, name ) )
        { m_instruments->SetPlacement( m_placing ); return; }
        m_sourceSettings[kind] = settings;
        m_instruments->SetProbeName( name );
    }
    m_toolMgr->RunAction( ACTIONS::cancelInteractive );
    m_toolMgr->PostAction( placeProbes, kind );
    m_frame->GetCanvas()->SetFocus();
}

int EDITOR_SIMULATION_TOOL::PlaceProbes( const TOOL_EVENT& action )
{
    const auto kind = action.Parameter<PROBE_KIND>();
    m_placing = kind;
    m_instruments->SetPlacement( kind );
    m_toolMgr->RunAction( ACTIONS::selectionClear );
    m_frame->PushTool( action );
    Activate();
    auto* controls = getViewControls();
    auto* screen = m_frame->GetScreen();
    EE_GRID_HELPER grid( m_toolMgr );
    std::unique_ptr<SCH_SYMBOL> preview;
    auto* selection = m_toolMgr->GetTool<SCH_SELECTION_TOOL>();
    controls->ShowCursor( true );
    setStatus( _( "Click a connection to place directly, or empty space to preview. Esc finishes placement." ) );

    auto hidePreview = [&]()
    {
        if( preview ) selection->ClearSelection();
        getView()->ClearPreview();
        preview.reset();
        controls->SetAutoPan( false );
        controls->CaptureCursor( false );
    };
    auto updatePreview = [&]( const PROBE_PLACEMENT& plan )
    {
        getView()->ClearPreview();
        if( !preview )
        {
            preview = MakeProbeSymbol( kind, m_frame->GetCurrentSheet(), plan.anchor, plan.vertical,
                    NextProbeSequence( m_frame->Schematic() ), m_instruments->ProbeName(), m_sourceSettings[kind] );
            preview->SetParent( screen );
            selection->AddItemToSel( preview.get() );
            preview->SetFlags( IS_NEW | IS_MOVING );
        }
        AlignProbeForPlacement( *preview, plan );
        getView()->AddToPreview( preview.get(), false );
        controls->SetAutoPan( true );
    };

    while( TOOL_EVENT* event = Wait() )
    {
        if( !m_placing || m_frame->GetScreen() != screen )
        {
            event->SetPassEvent();
            break;
        }
        m_frame->GetCanvas()->SetCurrentCursor( preview ? KICURSOR::MOVING : KICURSOR::BULLSEYE );
        if( event->IsAction( &ACTIONS::undo ) )
        {
            event->SetPassEvent();
            break;
        }
        else if( event->IsCancelInteractive() )
        {
            event->SetPassEvent( false );
            if( preview )
                hidePreview();
            else
                break;
        }
        else if( event->IsActivate() )
        {
            event->SetPassEvent();
            break;
        }
        else if( preview && event->IsAction( &ACTIONS::refreshPreview ) )
        {
            getView()->ClearPreview();
            getView()->AddToPreview( preview.get(), false );
        }
        else if( preview && event->IsAction( &ACTIONS::doDelete ) )
            hidePreview();
        else if( preview && ( event->IsAction( &ACTIONS::redo ) || event->IsAction( &ACTIONS::duplicate )
                              || event->IsAction( &ACTIONS::paste )
                              || event->IsAction( &SCH_ACTIONS::repeatDrawItem )
                              || event->IsAction( &SCH_ACTIONS::properties )
                              || event->IsAction( &SIMULATION_ACTIONS::probeProperties )
                              || event->IsAction( &SIMULATION_ACTIONS::configure ) ) )
            wxBell();
        else if( preview && ( event->IsAction( &SCH_ACTIONS::rotateCW )
                              || event->IsAction( &SCH_ACTIONS::rotateCCW )
                              || event->IsAction( &SCH_ACTIONS::mirrorV )
                              || event->IsAction( &SCH_ACTIONS::mirrorH ) ) )
        {
            m_toolMgr->PostAction( ACTIONS::refreshPreview );
            event->SetPassEvent();
        }
        else if( event->IsMotion() || event->IsClick( BUT_LEFT )
                 || event->IsAction( &ACTIONS::cursorClick ) )
        {
            grid.SetSnap( !event->Modifier( MD_SHIFT ) );
            grid.SetUseGrid( getView()->GetGAL()->GetGridSnapping() && !event->DisableGridSnapping() );
            auto position = grid.Align( controls->GetMousePosition(), GRID_HELPER_GRIDS::GRID_CONNECTABLE );
            const int tolerance = std::max( 1, int( getView()->ToWorld( 12.0 ) ) );
            if( kind != PROBE_KIND::CURRENT && kind != PROBE_KIND::POWER )
            {
                if( const SCH_PIN* pin = FindPinConnectionAt( *screen, &m_frame->GetCurrentSheet(),
                        controls->GetMousePosition(), tolerance ) )
                    position = pin->GetPosition();
            }
            try
            {
                const auto plan = PlanProbePlacement( *screen, position, tolerance, kind,
                                                       &m_frame->GetCurrentSheet(), true );
                controls->ForceCursorPosition( true, plan.anchor );
                if( event->IsMotion() )
                {
                    if( preview ) updatePreview( plan );
                }
                else if( !preview && !plan.wire && !plan.pin )
                    updatePreview( plan );
                else
                {
                    PlaceProbe( *m_frame, kind, m_instruments->ProbeName(), plan.anchor, tolerance,
                                m_sourceSettings[kind], true, preview.get() );
                    hidePreview();
                    m_instruments->ClearProbeName();
                    setStatus( _( "Probe placed — click a connection or empty space; Esc finishes placement" ) );
                }
            }
            catch( const std::exception& error )
            {
                setStatus( DiagnosticText( error.what() ) );
            }
        }
        else if( event->IsClick( BUT_RIGHT ) )
            m_menu->ShowContextMenu( m_toolMgr->GetTool<SCH_SELECTION_TOOL>()->GetSelection() );
        else
            event->SetPassEvent();
    }
    hidePreview();
    controls->ForceCursorPosition( false );
    m_placing.reset();
    if( m_instruments ) m_instruments->SetPlacement( {} );
    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
    m_frame->PopTool( action );
    setStatus( _( "Probe placement finished" ) );
    return 0;
}

void EDITOR_SIMULATION_TOOL::redraw( bool force )
{
    if( !m_overlay )
        return;
    const bool visible = m_session.state != EDITOR_SESSION_STATE::STOPPED
                         && m_session.state != EDITOR_SESSION_STATE::FAILED;
    const auto sheet = m_frame->GetCurrentSheet().Path().AsString();
    std::vector<int> brightness;
    for( std::size_t i = 0; visible && i < m_circuit.circuit.leds.size(); ++i )
        brightness.push_back( int( 255 * LedBrightness( i < m_session.ledCurrents.size() ? m_session.ledCurrents[i] : 0,
                                                      m_circuit.circuit.leds[i].brightnessCurrent ) ) );
    if( !force && m_overlayCached && visible == m_overlayVisible && sheet == m_overlaySheet
        && m_logicLevels == m_overlayLogic && brightness == m_overlayBrightness
        && m_session.switches == m_overlaySwitches
        && m_session.adjustments == m_overlayAdjustments
        && m_session.netContentions == m_overlayContentions
        && ( !m_logicLevels || ( m_session.logicLevels == m_overlayLevels
                                 && m_session.logicRapid == m_overlayRapid ) ) ) return;
    m_overlayCached = true; m_overlayVisible = visible; m_overlaySheet = sheet; m_overlayLogic = m_logicLevels;
    m_overlayBrightness = std::move( brightness ); m_overlaySwitches = m_session.switches;
    m_overlayAdjustments = m_session.adjustments;
    m_overlayContentions = m_session.netContentions;
    m_overlayLevels = m_logicLevels ? m_session.logicLevels : std::vector<LOGIC_LEVEL>{};
    m_overlayRapid = m_logicLevels ? m_session.logicRapid : std::vector<bool>{};
    auto* view = m_frame->GetCanvas()->GetView();
    m_overlay->Render( m_circuit, m_session, m_frame->GetCurrentSheet().Path().AsString(), m_logicLevels );
    if( !view->HasItem( m_overlay.get() ) )
        view->Add( m_overlay.get() );
    view->Update( m_overlay.get() );
    m_frame->GetCanvas()->Refresh();
}

void EDITOR_SIMULATION_TOOL::tick( wxTimerEvent& )
{
    if( !m_runner )
        return;
    const auto now = std::chrono::steady_clock::now();
    rememberInstruments();
    if( m_instrumentLayoutPending && now - m_instrumentLayoutChanged >= std::chrono::milliseconds( 400 ) )
        saveInstrumentLayout();
    if( now - m_lastToolbar >= std::chrono::seconds( 1 ) )
    { EnsureSimulationToolbar( *m_frame ); m_lastToolbar = now; }
    const auto oldState = m_session.state;
    const auto oldTime = m_session.sample.latest.time;
    const auto oldEvents = m_session.sample.eventsApplied;
    const auto oldSpeed = m_session.speed;
    const auto oldPreferences = m_session.preferences;
    m_session = m_runner->View();
    const auto state = m_session.state;
    bool rateChanged = false;
    if( state != EDITOR_SESSION_STATE::RUNNING || oldState != state || oldSpeed != m_session.speed )
    {
        m_rateStarted = now; m_rateTime = m_session.sample.latest.time; m_rateValid = false;
    }
    else if( now - m_rateStarted >= std::chrono::milliseconds( 500 ) )
    {
        const auto seconds = std::chrono::duration<double>( now - m_rateStarted ).count();
        m_realSpeed = std::max( 0.0, ( m_session.sample.latest.time - m_rateTime ) / seconds );
        m_rateStarted = now; m_rateTime = m_session.sample.latest.time;
        m_rateValid = true; rateChanged = true;
    }
    if( m_instruments ) m_instruments->UpdateSession( m_circuit, m_session, m_active );
    if( m_active && m_preferences.autoOpenInstruments
        && !m_circuit.circuit.measurements.empty() && !m_resultOpened )
    { showInstruments( true ); m_resultOpened = true; }
    if( !m_active )
    {
        publishStatus();
        return;
    }
    if( state == oldState && m_session.sample.latest.time == oldTime
        && m_session.sample.eventsApplied == oldEvents && m_session.speed == oldSpeed
        && m_session.preferences == oldPreferences && !rateChanged ) { publishStatus(); return; }
    // Refresh electrical visuals at the timer rate; only status text is throttled.
    redraw( false );
    if( state == oldState && state == EDITOR_SESSION_STATE::RUNNING
        && now - m_lastHud < std::chrono::milliseconds( 100 ) ) return;
    m_lastHud = now;
    const wxString names[] = { _( "Stopped" ), _( "Preparing" ), _( "Running" ),
                              _( "Paused" ), _( "Completed" ), _( "Error" ) };
    wxString status = wxString::Format( "%s — t = %.6f s   %.3g×", names[static_cast<int>( state )],
                                       m_session.sample.latest.time, m_session.speed );
    if( state == EDITOR_SESSION_STATE::RUNNING && m_rateValid )
        status += wxString::Format( _( "   actual %.2g×" ), m_realSpeed );
    for( std::size_t i = 0; i < std::min<std::size_t>( 2, m_session.warnings.size() ); ++i )
        status += "   " + DiagnosticText( m_session.warnings[i] );
    if( m_session.warnings.size() > 2 ) status += wxString::Format( _( "   +%zu warnings" ), m_session.warnings.size() - 2 );
    if( !m_circuit.circuit.floatingIslands.empty() )
        status += wxString::Format( _( "   Floating: %zu" ), m_circuit.circuit.floatingIslands.size() );
    if( state == EDITOR_SESSION_STATE::FAILED ) status += "   " + DiagnosticText( m_session.error ).Left( 140 );
    setStatus( status );
    publishStatus();
    if( state == EDITOR_SESSION_STATE::FAILED && !m_errorShown )
    {
        m_errorShown = true;
        wxMessageBox( DiagnosticText( m_session.error ), _( "Interactive simulation" ),
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

void ReloadEditorSimulationPreferences( SCH_EDIT_FRAME* aFrame )
{
    if( aFrame->GetToolManager() )
        if( auto* tool = aFrame->GetToolManager()->GetTool<EDITOR_SIMULATION_TOOL>() )
            tool->ReloadPreferences();
}
}
