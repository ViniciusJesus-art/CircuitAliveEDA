/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "pending_footprints.h"
#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_edit_frame.h>
#include <pcbnew_settings.h>
#include <pcb_draw_panel_gal.h>
#include <footprint_preview_panel.h>
#include <netlist_reader/pcb_netlist.h>
#include <netlist_reader/netlist_reader.h>
#include <netlist_reader/board_netlist_updater.h>
#include <tools/pcb_tool_base.h>
#include <tools/pcb_actions.h>
#include <tool/action_manager.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <view/view.h>
#include <view/view_controls.h>
#include <kiface_base.h>
#include <kiway.h>
#include <kiway_player.h>
#include <mail_type.h>
#include <richio.h>
#include <reporter.h>
#include <string_utils.h>
#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/eventfilter.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <set>

namespace CIRCUITALIVE
{
std::vector<PENDING_FOOTPRINT> BuildPendingFootprints( BOARD& aBoard, NETLIST& aNetlist )
{
    std::vector<PENDING_FOOTPRINT> result;
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( aBoard, aNetlist, false );
    if( !links.ambiguousReferences.empty() )
        return result;

    for( unsigned i = 0; i < aNetlist.GetCount(); ++i )
    {
        COMPONENT* component = aNetlist.GetComponent( i );
        const wxString ref = component->GetReference();
        if( component->GetProperties().count( "exclude_from_board" ) || ref.IsEmpty()
            || ref.StartsWith( "#" ) || ref.Contains( "?" ) )
            continue;

        std::vector<LIB_ID> expected{ component->GetFPID() };
        for( const auto& [name, variant] : component->GetVariants() )
        {
            const auto it = variant.m_fields.find( GetCanonicalFieldName( FIELD_T::FOOTPRINT ) );
            LIB_ID id;
            if( it != variant.m_fields.end() && !it->second.empty() && id.Parse( it->second, true ) < 0
                && std::none_of( expected.begin(), expected.end(), [&]( const LIB_ID& other )
                    { return BOARD_NETLIST_UPDATER::fpidsEquivalent( id, other )
                             || BOARD_NETLIST_UPDATER::fpidsEquivalent( other, id ); } ) )
                expected.push_back( id );
        }

        for( std::size_t index = 0; index < expected.size(); ++index )
        {
            const LIB_ID& id = expected[index];
            bool placed = false;
            for( const auto& [footprint, associated] : links.matches )
            {
                if( associated != component )
                    continue;
                if( id.empty() || BOARD_NETLIST_UPDATER::fpidsEquivalent( footprint->GetFPID(), id ) )
                    placed = true;
                // A placed base whose replacement failed is still placed, not a second item
                // to insert. Other explicitly assigned variant footprints have separate slots.
                if( index == 0 && std::none_of( expected.begin() + 1, expected.end(),
                        [&]( const LIB_ID& variant )
                        { return BOARD_NETLIST_UPDATER::fpidsEquivalent( footprint->GetFPID(), variant ); } ) )
                    placed = true;
            }
            if( !placed )
                result.push_back( { ref, component->GetValue(), ref.Upper() + "\n" + id.Format(), id } );
        }
    }
    std::sort( result.begin(), result.end(), []( const auto& a, const auto& b )
    {
        const int cmp = StrNumCmp( a.reference, b.reference, true );
        return cmp == 0 ? a.footprint < b.footprint : cmp < 0;
    } );
    return result;
}

TOOL_ACTION PENDING_FOOTPRINT_ACTIONS::showPanel( TOOL_ACTION_ARGS()
        .Name( "circuitalive.PendingFootprints.showPanel" ).Scope( AS_GLOBAL )
        .FriendlyName( wxTRANSLATE( "Pending Footprints" ) )
        .Tooltip( wxTRANSLATE( "Show schematic components not yet placed on the PCB" ) ) );
TOOL_ACTION PENDING_FOOTPRINT_ACTIONS::place( TOOL_ACTION_ARGS()
        .Name( "circuitalive.PendingFootprints.place" ).Scope( AS_GLOBAL ).Flags( AF_ACTIVATE )
        .Parameter<VECTOR2I>( VECTOR2I() ) );

class SYNC_REPORTER : public REPORTER
{
public:
    REPORTER& Report( const wxString& text, SEVERITY severity ) override
    {
        if( severity == RPT_SEVERITY_ERROR && error.empty() )
            error = text;
        if( severity == RPT_SEVERITY_WARNING )
            ++warnings;
        return *this;
    }
    bool HasMessage() const override { return !error.empty() || warnings > 0; }
    wxString error;
    int warnings = 0;
};

class PENDING_FOOTPRINTS_TOOL : public wxEvtHandler, public PCB_TOOL_BASE, public wxEventFilter
{
public:
    PENDING_FOOTPRINTS_TOOL() : PCB_TOOL_BASE( "circuitalive.PendingFootprints" ), m_timer( this ) {}
    ~PENDING_FOOTPRINTS_TOOL() override { Shutdown(); }

    void Install()
    {
        m_frame = frame<PCB_EDIT_FRAME>();
        m_panel = std::make_unique<wxPanel>( m_frame );
        auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* bar = new wxBoxSizer( wxHORIZONTAL );
        bar->AddStretchSpacer();
        auto* dock = new wxButton( m_panel.get(), wxID_ANY, _( "Float / dock" ),
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
        bar->Add( dock );
        layout->Add( bar, 0, wxEXPAND | wxALL, 4 );
        dock->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            auto* manager = wxAuiManager::GetManager( m_frame );
            auto& pane = manager->GetPane( m_panel.get() );
            if( pane.IsFloating() ) pane.Dock();
            else pane.Float().FloatingPosition( m_frame->GetScreenPosition() + wxPoint( 40, 80 ) );
            manager->Update();
        } );
        m_splitter = new wxSplitterWindow( m_panel.get(), wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH );
        m_splitter->SetMinimumPaneSize( m_panel->FromDIP( 80 ) );
        m_preview = FOOTPRINT_PREVIEW_PANEL::New( &m_frame->Kiway(), m_splitter, m_frame );
        auto* lower = new wxPanel( m_splitter );
        auto* contents = new wxBoxSizer( wxVERTICAL );
        m_status = new wxStaticText( lower, wxID_ANY, _( "Focus the PCB to synchronize with the schematic." ) );
        contents->Add( m_status, 0, wxEXPAND | wxALL, 8 );
        auto* hint = new wxStaticText( lower, wxID_ANY,
                _( "Select a component. Click empty PCB space to preview and place. Esc cancels." ) );
        hint->Wrap( m_panel->FromDIP( 270 ) );
        contents->Add( hint, 0, wxEXPAND | wxALL, 8 );
        m_list = new wxListBox( lower, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               0, nullptr, wxLB_SINGLE | wxLB_HSCROLL );
        contents->Add( m_list, 1, wxEXPAND | wxALL, 4 );
        lower->SetSizer( contents );
        m_splitter->SplitHorizontally( m_preview, lower,
                m_panel->FromDIP( m_frame->GetPcbNewSettings()->Get<int>( "circuitalive.pending_sash" ).value_or( 200 ) ) );
        m_splitter->Bind( wxEVT_SPLITTER_DOUBLECLICKED, []( wxSplitterEvent& event ) { event.Veto(); } );
        layout->Add( m_splitter, 1, wxEXPAND | wxALL, 4 );
        m_panel->SetSizer( layout );
        m_list->Bind( wxEVT_LISTBOX, [this]( wxCommandEvent& )
        {
            const int index = m_list->GetSelection();
            if( index < 0 || std::size_t( index ) >= m_entries.size() ) return;
            const wxString key = m_entries[index].key;
            m_toolMgr->RunAction( ACTIONS::cancelInteractive );
            m_choice = key;
            m_armed = true;
            m_list->SetSelection( index );
            showPreview();
            m_frame->GetCanvas()->SetFocus();
        } );
        auto* manager = wxAuiManager::GetManager( m_frame );
        manager->AddPane( m_panel.get(), wxAuiPaneInfo().Name( "PendingFootprints" )
                .Caption( _( "Pending Footprints" ) ).Right().Layer( 1 ).Position( 1 )
                .CloseButton( true ).MinSize( m_panel->FromDIP( wxSize( 240, 300 ) ) )
                .BestSize( m_panel->FromDIP( wxSize( 300, 560 ) ) )
                .FloatingSize( m_panel->FromDIP( wxSize( 340, 560 ) ) ) );
        auto& pane = manager->GetPane( m_panel.get() );
        if( const auto saved = m_frame->GetPcbNewSettings()->Get<wxString>( "circuitalive.pending_pane" ) )
            manager->LoadPaneInfo( *saved, pane );
        pane.Show( m_frame->GetPcbNewSettings()->Get<bool>( "circuitalive.pending_visible" ).value_or( true ) );
        manager->Update();
        m_frame->Bind( wxEVT_ACTIVATE, &PENDING_FOOTPRINTS_TOOL::onActivate, this );
        m_frame->Bind( wxEVT_CHAR_HOOK, &PENDING_FOOTPRINTS_TOOL::onKey, this );
        m_frame->Bind( wxEVT_AUI_PANE_CLOSE, &PENDING_FOOTPRINTS_TOOL::onPaneClose, this );
        wxEvtHandler::AddFilter( this );
        Bind( wxEVT_TIMER, &PENDING_FOOTPRINTS_TOOL::onTimer, this );
        m_toolMgr->GetActionManager()->SetConditions( PENDING_FOOTPRINT_ACTIONS::showPanel,
                ACTION_CONDITIONS().Check( [this]( const SELECTION& )
                { return m_panel && wxAuiManager::GetManager( m_frame )->GetPane( m_panel.get() ).IsShown(); } ) );
        m_timer.Start( 250 );
        m_needsSync = true;
    }

    void Save()
    {
        if( !m_panel ) return;
        auto* manager = wxAuiManager::GetManager( m_frame );
        auto& pane = manager->GetPane( m_panel.get() );
        auto* cfg = m_frame->GetPcbNewSettings();
        cfg->Set<wxString>( "circuitalive.pending_pane", manager->SavePaneInfo( pane ) );
        cfg->Set<bool>( "circuitalive.pending_visible", pane.IsShown() );
        cfg->Set<int>( "circuitalive.pending_sash", m_panel->ToDIP( m_splitter->GetSashPosition() ) );
    }

    void Shutdown()
    {
        if( !m_panel ) return;
        m_timer.Stop();
        wxEvtHandler::RemoveFilter( this );
        Save();
        m_frame->Unbind( wxEVT_ACTIVATE, &PENDING_FOOTPRINTS_TOOL::onActivate, this );
        m_frame->Unbind( wxEVT_CHAR_HOOK, &PENDING_FOOTPRINTS_TOOL::onKey, this );
        m_frame->Unbind( wxEVT_AUI_PANE_CLOSE, &PENDING_FOOTPRINTS_TOOL::onPaneClose, this );
        m_preview->SetEvtHandlerEnabled( false );
        m_preview->StopDrawing();
        wxAuiManager::GetManager( m_frame )->DetachPane( m_panel.get() );
        m_panel.reset();
    }

    void Reset( RESET_REASON reason ) override
    {
        PCB_TOOL_BASE::Reset( reason );
        if( reason == SHUTDOWN ) { Shutdown(); return; }
        if( m_panel && ( reason == MODEL_RELOAD || reason == SUPERMODEL_RELOAD ) )
        {
            m_netlist.reset();
            m_armed = false;
            m_choice.clear();
            m_deferredPlacement.reset();
            m_needsSync = true;
        }
    }

    bool Begin( const VECTOR2I& position )
    {
        if( !m_panel || !m_netlist || !m_armed || m_choice.empty() || m_placing ) return false;
        if( !wxAuiManager::GetManager( m_frame )->GetPane( m_panel.get() ).IsShown() ) return false;
        if( m_needsSync ) m_deferredPlacement = position;
        else m_toolMgr->PostAction( PENDING_FOOTPRINT_ACTIONS::place, position );
        return true;
    }

private:
    bool ownsWindow( wxWindow* window ) const
    {
        for( ; window; window = window->GetParent() )
        {
            if( window == m_frame )
                return true;
        }
        return false;
    }
    int FilterEvent( wxEvent& event ) override
    {
        // Some platforms transfer focus directly between children or floating panes
        // without activating the main frame. Observe the first focus/click anywhere
        // in this editor, and let the event continue to its normal handler.
        if( event.GetEventType() == wxEVT_SET_FOCUS || event.GetEventType() == wxEVT_LEFT_DOWN
            || event.GetEventType() == wxEVT_RIGHT_DOWN || event.GetEventType() == wxEVT_MIDDLE_DOWN )
        {
            if( auto* window = dynamic_cast<wxWindow*>( event.GetEventObject() ) )
            {
                const bool focused = ownsWindow( window );
                if( focused && !m_hasFocus )
                    m_needsSync = true;
                m_hasFocus = focused;
            }
        }
        return Event_Skip;
    }
    void onActivate( wxActivateEvent& event )
    {
        if( !event.GetActive() && m_placing )
            m_toolMgr->RunAction( ACTIONS::cancelInteractive );
        if( event.GetActive() ) m_needsSync = true;
        else m_hasFocus = false;
        event.Skip();
    }
    void onKey( wxKeyEvent& event )
    {
        if( event.GetKeyCode() == WXK_ESCAPE && !m_placing )
        {
            m_armed = false;
            m_choice.clear();
            m_deferredPlacement.reset();
            m_list->SetSelection( wxNOT_FOUND );
        }
        event.Skip();
    }
    void onPaneClose( wxAuiManagerEvent& event )
    {
        if( event.GetPane() && event.GetPane()->window == m_panel.get() )
        {
            m_toolMgr->RunAction( ACTIONS::cancelInteractive );
            m_armed = false;
            m_deferredPlacement.reset();
        }
        event.Skip();
    }
    void onTimer( wxTimerEvent& )
    {
        if( !m_panel || m_syncing || m_placing || !m_frame->IsEnabled() || m_frame->IsClosing() ) return;
        const bool focused = m_hasFocus || ownsWindow( wxWindow::FindFocus() ) || m_frame->IsActive();
        const wxString identity = m_frame->Prj().GetProjectFullName() + "\n" + m_frame->GetBoard()->GetFileName();
        if( identity != m_document )
        {
            m_document = identity;
            m_netlist.reset(); m_armed = false; m_choice.clear();
            m_deferredPlacement.reset();
            m_needsSync = true;
            rebuildList();
        }
        if( m_needsSync && focused && m_frame->IsShownOnScreen() )
        {
            // A suspended drawing/routing tool can otherwise block every retry after
            // returning to the PCB. Finish cancelling its unplaced preview first.
            if( !m_frame->ToolStackIsEmpty() )
            {
                m_toolMgr->RunAction( ACTIONS::cancelInteractive );
                return;
            }
            m_needsSync = false;
            synchronize();
        }
        if( m_netlist && m_boardStamp != m_frame->GetBoard()->GetTimeStamp() ) rebuildList();
        if( m_deferredPlacement && !m_needsSync )
        {
            const VECTOR2I position = *m_deferredPlacement;
            m_deferredPlacement.reset();
            Begin( position );
        }
    }
    void synchronize()
    {
        if( Kiface().IsSingle() )
        { m_status->SetLabel( _( "Open this PCB through its project to synchronize." ) ); return; }
        m_syncing = true;
        struct GUARD { bool& flag; ~GUARD() { flag = false; } } guard{ m_syncing };
        try
        {
            // Background synchronization must never create or reopen an editor. In
            // particular, closing the schematic often activates the PCB window.
            KIWAY_PLAYER* schematic = m_frame->Kiway().Player( FRAME_SCH, false );
            if( !schematic || schematic->IsClosing() || schematic->IsBeingDeleted()
                || !schematic->IsShownOnScreen() )
            {
                m_netlist.reset();
                m_armed = false;
                m_choice.clear();
                m_deferredPlacement.reset();
                rebuildList();
                m_status->SetLabel( _( "Open the schematic to synchronize with this PCB." ) );
                return;
            }
            std::string payload = "circuitalive:pcb-auto-sync";
            m_frame->Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_GET_NETLIST, payload, m_frame );
            if( payload == "circuitalive:pcb-auto-sync" )
            { m_needsSync = true; return; }
            auto netlist = std::make_unique<NETLIST>();
            KICAD_NETLIST_READER reader( new STRING_LINE_READER( payload, "PCB synchronization" ), netlist.get() );
            reader.LoadNetlist();
            for( unsigned i = 0; i < netlist->GetCount(); ++i )
            {
                if( netlist->GetComponent( i )->GetReference().Contains( "?" ) )
                {
                    m_netlist.reset(); rebuildList();
                    m_status->SetLabel( _( "Annotate the new schematic symbols before synchronizing." ) );
                    return;
                }
            }
            netlist->SortByReference();
            // Reconcile on every return to the PCB. The debug NETLIST formatter omits
            // some pin/group/variant metadata and is not a reliable change fingerprint.
            // The updater already records only actual changes, so an unchanged netlist
            // leaves the board clean and does not add an undo entry.
            {
                SYNC_REPORTER reporter;
                BOARD_NETLIST_UPDATER updater( m_frame, m_frame->GetBoard() );
                configure( updater, reporter );
                updater.SetNewFootprintFilter( []( const COMPONENT&, const LIB_ID& ) { return false; } );
                if( !updater.UpdateNetlist( *netlist ) )
                {
                    m_netlist.reset(); rebuildList();
                    m_status->SetLabel( reporter.error );
                    m_status->Wrap( m_panel->FromDIP( 270 ) );
                    return;
                }
                m_frame->OnNetlistChanged( updater, nullptr );
                m_frame->GetCanvas()->GetView()->UpdateAllItems( KIGFX::ALL );
                m_frame->GetCanvas()->Refresh();
                if( !reporter.error.empty() )
                    m_status->SetToolTip( reporter.error );
            }
            m_netlist = std::move( netlist );
            rebuildList();
        }
        catch( const IO_ERROR& error )
        {
            m_netlist.reset(); rebuildList();
            m_status->SetLabel( error.Problem() );
            m_status->Wrap( m_panel->FromDIP( 270 ) );
        }
    }
    void configure( BOARD_NETLIST_UPDATER& updater, SYNC_REPORTER& reporter )
    {
        updater.SetReporter( &reporter );
        updater.SetLookupByTimestamp( false );
        updater.SetDeleteUnusedFootprints( false );
        updater.SetReplaceFootprints( true );
        updater.SetUpdateFields( true );
    }
    void rebuildList()
    {
        if( !m_panel ) return;
        int selected = m_list->GetSelection();
        auto entries = m_netlist ? BuildPendingFootprints( *m_frame->GetBoard(), *m_netlist )
                                : std::vector<PENDING_FOOTPRINT>();
        wxArrayString labels;
        int found = wxNOT_FOUND;
        for( std::size_t i = 0; i < entries.size(); ++i )
        {
            wxString label = entries[i].reference + "  " + entries[i].value;
            if( entries[i].footprint.empty() ) label += _( "  (no footprint)" );
            labels.Add( label );
            if( entries[i].key == m_choice ) found = i;
        }
        m_entries = std::move( entries );
        if( found == wxNOT_FOUND && m_armed && !m_entries.empty() )
            found = std::clamp( selected, 0, int( m_entries.size() ) - 1 );
        m_list->Set( labels );
        m_list->SetSelection( found );
        if( found != wxNOT_FOUND ) m_choice = m_entries[found].key;
        else { m_choice.clear(); m_armed = false; }
        m_status->SetLabel( m_entries.empty() ? _( "All schematic footprints are placed." )
                : wxString::Format( _( "%zu footprints to place" ), m_entries.size() ) );
        showPreview();
        m_boardStamp = m_frame->GetBoard()->GetTimeStamp();
    }
    const PENDING_FOOTPRINT* chosen() const
    {
        for( const auto& entry : m_entries ) if( entry.key == m_choice ) return &entry;
        return nullptr;
    }
    void showPreview()
    {
        m_preview->ClearViewAndData();
        if( const auto* entry = chosen(); entry && !entry->footprint.empty() )
        {
            m_preview->DisplayFootprint( entry->footprint );
            if( FOOTPRINT* footprint = m_preview->GetCurrentFootprint() )
            {
                footprint->SetReference( entry->reference );
                footprint->SetValue( entry->value );
                footprint->Reference().SetVisible( false );
                footprint->Value().SetVisible( false );
                m_preview->GetView()->Update( footprint, KIGFX::ALL );
            }
        }
        // Library fabrication text can cover the pads at this preview's small scale.
        m_preview->GetView()->SetLayerVisible( F_Fab, false );
        m_preview->GetView()->SetLayerVisible( B_Fab, false );
        m_preview->RefreshAll();
    }
    int showPanel( const TOOL_EVENT& )
    {
        auto* manager = wxAuiManager::GetManager( m_frame );
        auto& pane = manager->GetPane( m_panel.get() );
        pane.Show( !pane.IsShown() );
        if( !pane.IsShown() )
        {
            m_toolMgr->RunAction( ACTIONS::cancelInteractive );
            m_armed = false;
            m_deferredPlacement.reset();
        }
        manager->Update(); Save();
        return 0;
    }
    int place( const TOOL_EVENT& event )
    {
        const auto* selected = chosen();
        if( !selected || !m_netlist ) return 0;
        const PENDING_FOOTPRINT entry = *selected;
        if( entry.footprint.empty() )
        { m_status->SetLabel( _( "Assign a footprint in the schematic first." ) ); return 0; }
        std::unique_ptr<FOOTPRINT> preview( m_frame->LoadFootprint( entry.footprint ) );
        if( !preview )
        { m_status->SetLabel( _( "Footprint not found in the library." ) ); return 0; }
        preview->SetParent( m_frame->GetBoard() );
        preview->SetReference( entry.reference );
        preview->SetValue( entry.value );
        preview->SetPosition( event.Parameter<VECTOR2I>() );
        preview->SetFlags( IS_NEW | IS_MOVING );
        for( PAD* pad : preview->Pads() ) pad->SetNetCode( 0 );
        m_placing = true;
        m_frame->PushTool( event );
        Activate();
        auto* controls = getViewControls();
        controls->ShowCursor( true );
        controls->SetAutoPan( true );
        controls->CaptureCursor( true );
        getView()->Add( preview.get() );
        m_frame->GetCanvas()->Refresh();
        bool placed = false;
        while( TOOL_EVENT* evt = Wait() )
        {
            if( evt->IsCancelInteractive() || evt->IsAction( &ACTIONS::undo ) ) break;
            if( evt->IsActivate() ) { m_armed = false; break; }
            if( evt->IsMotion() )
                preview->SetPosition( controls->GetCursorPosition( !evt->DisableGridSnapping() ) );
            else if( evt->IsAction( &PCB_ACTIONS::rotateCw ) )
                preview->Rotate( preview->GetPosition(), -m_frame->GetRotationAngle() );
            else if( evt->IsAction( &PCB_ACTIONS::rotateCcw ) )
                preview->Rotate( preview->GetPosition(), m_frame->GetRotationAngle() );
            else if( evt->IsAction( &PCB_ACTIONS::flip ) )
                preview->Flip( preview->GetPosition(), m_frame->GetPcbNewSettings()->m_FlipDirection );
            else if( evt->IsClick( BUT_LEFT ) )
            {
                preview->SetPosition( controls->GetCursorPosition( !evt->DisableGridSnapping() ) );
                placed = true;
                break;
            }
            else evt->SetPassEvent();
            getView()->Update( preview.get() );
            m_frame->GetCanvas()->Refresh();
        }
        getView()->Remove( preview.get() );
        controls->SetAutoPan( false ); controls->CaptureCursor( false );
        if( placed )
        {
            SYNC_REPORTER reporter;
            BOARD_NETLIST_UPDATER updater( m_frame, m_frame->GetBoard() );
            configure( updater, reporter );
            updater.SetNewFootprintFilter( [&]( const COMPONENT& component, const LIB_ID& id )
                    { return component.GetReference() == entry.reference && id == entry.footprint; } );
            updater.SetNewFootprintInitializer( [&]( FOOTPRINT& footprint )
            {
                footprint.SetPosition( preview->GetPosition() );
                if( footprint.IsFlipped() != preview->IsFlipped() )
                    footprint.Flip( footprint.GetPosition(), m_frame->GetPcbNewSettings()->m_FlipDirection );
                footprint.SetOrientation( preview->GetOrientation() );
            } );
            if( updater.UpdateNetlist( *m_netlist ) )
                m_frame->OnNetlistChanged( updater, nullptr );
            rebuildList();
            if( !reporter.error.empty() ) m_status->SetLabel( reporter.error );
        }
        m_frame->PopTool( event );
        m_placing = false;
        m_frame->GetCanvas()->Refresh();
        return 0;
    }
    void setTransitions() override
    {
        Go( &PENDING_FOOTPRINTS_TOOL::showPanel, PENDING_FOOTPRINT_ACTIONS::showPanel.MakeEvent() );
        Go( &PENDING_FOOTPRINTS_TOOL::place, PENDING_FOOTPRINT_ACTIONS::place.MakeEvent() );
    }

    PCB_EDIT_FRAME* m_frame = nullptr;
    std::unique_ptr<wxPanel> m_panel;
    wxSplitterWindow* m_splitter = nullptr;
    FOOTPRINT_PREVIEW_PANEL* m_preview = nullptr;
    wxListBox* m_list = nullptr;
    wxStaticText* m_status = nullptr;
    wxTimer m_timer;
    std::unique_ptr<NETLIST> m_netlist;
    std::vector<PENDING_FOOTPRINT> m_entries;
    wxString m_choice, m_document;
    std::optional<VECTOR2I> m_deferredPlacement;
    int m_boardStamp = -1;
    bool m_hasFocus = false;
    bool m_needsSync = false, m_syncing = false, m_placing = false, m_armed = false;
};

void RegisterPendingFootprints( TOOL_MANAGER* manager ) { manager->RegisterTool( new PENDING_FOOTPRINTS_TOOL ); }
static PENDING_FOOTPRINTS_TOOL* tool( PCB_EDIT_FRAME* frame )
{ return frame->GetToolManager()->GetTool<PENDING_FOOTPRINTS_TOOL>(); }
void InstallPendingFootprints( PCB_EDIT_FRAME* frame ) { tool( frame )->Install(); }
void SavePendingFootprints( PCB_EDIT_FRAME* frame )
{ if( frame->GetToolManager() ) if( auto* t = tool( frame ) ) t->Save(); }
void ShutdownPendingFootprints( PCB_EDIT_FRAME* frame )
{ if( frame->GetToolManager() ) if( auto* t = tool( frame ) ) t->Shutdown(); }
bool BeginPendingFootprint( PCB_EDIT_FRAME* frame, const VECTOR2I& position )
{ auto* t = tool( frame ); return t && t->Begin( position ); }
}
