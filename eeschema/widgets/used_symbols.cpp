/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "used_symbols.h"
#include <widgets/symbol_preview_widget.h>
#include <schematic.h>
#include <sch_collectors.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <lib_symbol.h>
#include <eeschema_settings.h>
#include <paths.h>
#include <settings/settings_manager.h>
#include <tools/ee_grid_helper.h>
#include <tool/action_manager.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_tool_base.h>
#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <algorithm>
#include <optional>

namespace CIRCUITALIVE
{
TOOL_ACTION USED_SYMBOL_ACTIONS::showPanel( TOOL_ACTION_ARGS()
        .Name( "circuitalive.UsedSymbols.showPanel" ).Scope( AS_GLOBAL )
        .FriendlyName( wxTRANSLATE( "Used Components" ) )
        .Tooltip( wxTRANSLATE( "Show components used on the current page" ) ) );

class USED_SYMBOLS_TOOL : public wxEvtHandler, public SCH_TOOL_BASE<SCH_EDIT_FRAME>,
                          public SCHEMATIC_LISTENER
{
public:
    USED_SYMBOLS_TOOL() : SCH_TOOL_BASE<SCH_EDIT_FRAME>( "circuitalive.UsedSymbols" ), m_timer( this ) {}
    ~USED_SYMBOLS_TOOL() override { Shutdown(); }

    void Install()
    {
        if( m_panel )
            return;
        m_panel = std::make_unique<wxPanel>( m_frame );
        auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* toolbar = new wxBoxSizer( wxHORIZONTAL );
        toolbar->AddStretchSpacer();
        m_dockButton = new wxButton( m_panel.get(), wxID_ANY, _( "Float / dock" ),
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT );
        m_dockButton->SetToolTip( _( "Switch between a floating window and a docked panel" ) );
        m_dockButton->Bind( wxEVT_BUTTON, &USED_SYMBOLS_TOOL::dock, this );
        toolbar->Add( m_dockButton, 0 );
        layout->Add( toolbar, 0, wxEXPAND | wxALL, 4 );
        m_splitter = new wxSplitterWindow( m_panel.get(), wxID_ANY, wxDefaultPosition,
                                          wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH );
        m_splitter->SetMinimumPaneSize( m_panel->FromDIP( 80 ) );
        m_splitter->SetSashGravity( 0.0 );
        m_preview = new SYMBOL_PREVIEW_WIDGET( m_splitter, &m_frame->Kiway(), true,
                                               EDA_DRAW_PANEL_GAL::GAL_TYPE_CAIRO );
        m_preview->SetMinSize( m_panel->FromDIP( wxSize( 220, 60 ) ) );
        auto* contents = new wxPanel( m_splitter );
        auto* contentLayout = new wxBoxSizer( wxVERTICAL );
        m_instruction = _( "Click empty space to position, then click to place. Esc hides the symbol; Esc again cancels the choice." );
        m_hint = new wxStaticText( contents, wxID_ANY, m_instruction );
        m_hint->Wrap( m_panel->FromDIP( 260 ) );
        contentLayout->Add( m_hint, 0, wxEXPAND | wxALL, 8 );
        m_list = new wxListBox( contents, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               0, nullptr, wxLB_SINGLE | wxLB_HSCROLL );
        m_list->SetMinSize( m_panel->FromDIP( wxSize( -1, 80 ) ) );
        contentLayout->Add( m_list, 1, wxEXPAND | wxALL, 4 );
        contents->SetSizer( contentLayout );
        m_splitter->SplitHorizontally( m_preview, contents, m_panel->FromDIP( 200 ) );
        m_splitter->Bind( wxEVT_SPLITTER_SASH_POS_CHANGED, &USED_SYMBOLS_TOOL::sashChanged, this );
        m_splitter->Bind( wxEVT_SPLITTER_DOUBLECLICKED, []( wxSplitterEvent& event ) { event.Veto(); } );
        layout->Add( m_splitter, 1, wxEXPAND | wxALL, 4 );
        m_panel->SetSizer( layout );
        m_preview->SetStatusText( _( "Select a component" ) );
        m_list->Bind( wxEVT_LISTBOX, &USED_SYMBOLS_TOOL::choose, this );
        m_panel->Bind( wxEVT_SIZE, &USED_SYMBOLS_TOOL::resize, this );
        auto* manager = wxAuiManager::GetManager( m_frame );
        manager->AddPane( m_panel.get(), wxAuiPaneInfo().Name( "UsedComponents" )
                .Caption( _( "Used Components" ) ).Right().Layer( 1 ).Position( 1 )
                .CloseButton( true )
                .MinSize( m_panel->FromDIP( wxSize( 240, 300 ) ) )
                .BestSize( m_panel->FromDIP( wxSize( 300, 560 ) ) )
                .FloatingSize( m_panel->FromDIP( wxSize( 340, 560 ) ) ) );
        m_frame->Schematic().AddListener( this );
        Bind( wxEVT_TIMER, &USED_SYMBOLS_TOOL::refreshTimer, this );
        m_frame->Bind( wxEVT_CHAR_HOOK, &USED_SYMBOLS_TOOL::key, this );
        m_frame->Bind( wxEVT_AUI_PANE_CLOSE, &USED_SYMBOLS_TOOL::paneClosed, this );
        m_toolMgr->GetActionManager()->SetConditions( USED_SYMBOL_ACTIONS::showPanel,
                ACTION_CONDITIONS().Check( [this]( const SELECTION& )
                {
                    auto* aui = wxAuiManager::GetManager( m_frame );
                    return m_panel && aui && aui->GetPane( m_panel.get() ).IsShown();
                } ) );
        refresh();
    }

    void RestoreVisibility()
    {
        if( m_panel )
            wxAuiManager::GetManager( m_frame )->GetPane( m_panel.get() ).Show(
                    m_frame->eeconfig()->Get<bool>( "appearance.show_used_components" ).value_or( true ) );
    }

    void Shutdown()
    {
        if( !m_panel )
            return;
        m_timer.Stop();
        m_frame->Unbind( wxEVT_CHAR_HOOK, &USED_SYMBOLS_TOOL::key, this );
        m_frame->Unbind( wxEVT_AUI_PANE_CLOSE, &USED_SYMBOLS_TOOL::paneClosed, this );
        m_dockButton->Unbind( wxEVT_BUTTON, &USED_SYMBOLS_TOOL::dock, this );
        m_panel->Unbind( wxEVT_SIZE, &USED_SYMBOLS_TOOL::resize, this );
        m_splitter->Unbind( wxEVT_SPLITTER_SASH_POS_CHANGED, &USED_SYMBOLS_TOOL::sashChanged, this );
        m_list->Unbind( wxEVT_LISTBOX, &USED_SYMBOLS_TOOL::choose, this );
        m_preview->GetCanvas()->SetEvtHandlerEnabled( false );
        m_preview->GetCanvas()->StopDrawing();
        m_frame->Schematic().RemoveListener( this );
        if( auto* manager = wxAuiManager::GetManager( m_frame ) )
            manager->DetachPane( m_panel.get() );
        m_panel.reset();
    }

    void Rebind()
    {
        if( m_panel )
        {
            m_frame->Schematic().AddListener( this );
            cancel();
            schedule();
        }
    }

    void Reset( RESET_REASON aReason ) override
    {
        SCH_TOOL_BASE<SCH_EDIT_FRAME>::Reset( aReason );
        if( m_panel && ( aReason == SUPERMODEL_RELOAD || aReason == SHUTDOWN ) )
            cancel();
    }

    void OnSchItemsAdded( SCHEMATIC&, std::vector<SCH_ITEM*>& ) override { schedule(); }
    void OnSchItemsRemoved( SCHEMATIC&, std::vector<SCH_ITEM*>& ) override { schedule(); }
    void OnSchItemsChanged( SCHEMATIC&, std::vector<SCH_ITEM*>& ) override { schedule(); }
    void OnSchSheetChanged( SCHEMATIC& ) override { cancel(); refresh(); }
    void OnSchCurrentVariantChanged( SCHEMATIC& ) override { cancel(); schedule(); }

    bool BeginPlacement( const VECTOR2I& aPosition )
    {
        if( !m_panel || m_choice.IsEmpty() || !m_frame->ToolStackIsEmpty()
            || !wxAuiManager::GetManager( m_frame )->GetPane( m_panel.get() ).IsShown() )
            return false;
        SCH_SYMBOL* source = chosenSymbol();
        if( !source || source->IsMissingLibSymbol() )
            return false;
        EE_GRID_HELPER grid( m_toolMgr );
        auto symbol = CopyUsedSymbol( *source, m_frame->GetCurrentSheet(),
                grid.Align( aPosition, GRID_HELPER_GRIDS::GRID_CONNECTABLE ) );
        m_pending = symbol->m_Uuid;
        SCH_ACTIONS::PLACE_SYMBOL_PARAMS parameters;
        parameters.m_Symbol = symbol.release();
        m_toolMgr->PostAction( SCH_ACTIONS::placeSymbol, parameters );
        return true;
    }

    bool OwnsPlacement( const SCH_SYMBOL* aSymbol ) const
    {
        return aSymbol && m_pending && aSymbol->m_Uuid == *m_pending;
    }

    bool CancelPlacement( const SCH_SYMBOL* preview )
    {
        if( OwnsPlacement( preview ) )
        {
            m_pending.reset();
            return true;
        }
        cancel();
        return false;
    }

    void FinishPlacement( const SCH_SYMBOL* preview )
    {
        if( OwnsPlacement( preview ) )
            m_pending.reset();
    }

private:
    void sashChanged( wxSplitterEvent& event )
    {
        if( m_sashRestored )
        {
            m_frame->eeconfig()->Set<int>( "appearance.used_components_preview_height",
                                          m_panel->ToDIP( event.GetSashPosition() ) );
            // This preference is stored directly in JSON rather than a PARAM member.
            // Force the write so it persists even when no registered parameter changed.
            m_frame->eeconfig()->SaveToFile( PATHS::GetUserSettingsPath(), true );
        }
        event.Skip();
    }

    void resize( wxSizeEvent& event )
    {
        m_hint->SetLabel( m_instruction );
        m_hint->Wrap( std::max( 120, m_panel->GetClientSize().x - m_panel->FromDIP( 16 ) ) );
        if( !m_sashRestored )
            schedule();
        event.Skip();
    }

    void setTransitions() override { Go( &USED_SYMBOLS_TOOL::toggle, USED_SYMBOL_ACTIONS::showPanel.MakeEvent() ); }

    int toggle( const TOOL_EVENT& )
    {
        auto* manager = wxAuiManager::GetManager( m_frame );
        auto& pane = manager->GetPane( m_panel.get() );
        const bool shown = !pane.IsShown();
        pane.Show( shown );
        m_frame->eeconfig()->Set<bool>( "appearance.show_used_components", shown );
        if( !shown )
        {
            m_toolMgr->RunAction( ACTIONS::cancelInteractive );
            cancel();
        }
        manager->Update();
        if( shown )
        {
            m_panel->Layout();
            // Populate after AUI has shown/reparented the native list control.
            schedule();
        }
        return 0;
    }

    void paneClosed( wxAuiManagerEvent& event )
    {
        if( m_panel && event.GetPane() && event.GetPane()->window == m_panel.get() )
        {
            m_frame->eeconfig()->Set<bool>( "appearance.show_used_components", false );
            m_toolMgr->RunAction( ACTIONS::cancelInteractive );
            cancel();
        }
        event.Skip();
    }

    void dock( wxCommandEvent& )
    {
        auto* manager = wxAuiManager::GetManager( m_frame );
        auto& pane = manager->GetPane( m_panel.get() );
        if( pane.IsFloating() )
            pane.Dock();
        else
            pane.Float().FloatingPosition( m_frame->GetScreenPosition() + m_frame->FromDIP( wxPoint( 40, 80 ) ) );
        manager->Update();
        m_panel->Layout();
        schedule();
    }

    void key( wxKeyEvent& event )
    {
        if( event.GetKeyCode() == WXK_ESCAPE && !m_pending )
            cancel();
        event.Skip();
    }

    void cancel()
    {
        m_choice.clear();
        m_pending.reset();
        if( m_panel )
        {
            m_list->SetSelection( wxNOT_FOUND );
            m_preview->DisplayPart( nullptr, 1 );
            m_preview->SetStatusText( _( "Select a component" ) );
        }
    }

    void choose( wxCommandEvent& )
    {
        const int index = m_list->GetSelection();
        if( index == wxNOT_FOUND || std::size_t( index ) >= m_entries.size() )
            return;
        const wxString choice = m_entries[index].key;
        m_toolMgr->RunAction( ACTIONS::cancelInteractive );
        m_choice = choice;
        m_list->SetSelection( index );
        showPreview();
        m_frame->GetCanvas()->SetFocus();
    }

    SCH_SYMBOL* chosenSymbol()
    {
        const auto entry = std::find_if( m_entries.begin(), m_entries.end(),
                [this]( const auto& candidate ) { return candidate.key == m_choice; } );
        if( entry == m_entries.end() || !m_frame->GetScreen() )
            return nullptr;
        for( auto* item : m_frame->GetScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            if( item->m_Uuid == entry->symbol )
                return static_cast<SCH_SYMBOL*>( item );
        }
        return nullptr;
    }

    void showPreview()
    {
        if( SCH_SYMBOL* symbol = chosenSymbol() )
        {
            if( !symbol->GetLibSymbolRef() )
            {
                m_preview->SetStatusText( _( "Symbol definition unavailable" ) );
                return;
            }
            LIB_SYMBOL preview( *symbol->GetLibSymbolRef() );
            preview.GetValueField().SetText( symbol->GetValue( true, &m_frame->GetCurrentSheet(),
                    true, m_frame->Schematic().GetCurrentVariant() ) );
            preview.GetReferenceField().SetText( symbol->GetPrefix() + "?" );
            m_preview->DisplayPart( &preview, symbol->GetUnit(), symbol->GetBodyStyle() );
        }
        else
            cancel();
    }

    void schedule() { if( m_panel ) m_timer.StartOnce( 30 ); }
    void refreshTimer( wxTimerEvent& ) { if( !m_frame->IsClosing() ) refresh(); }

    void refresh()
    {
        if( !m_panel || m_frame->IsClosing() || !m_frame->GetScreen() )
            return;
        if( !m_sashRestored && m_splitter->GetClientSize().y > m_panel->FromDIP( 200 ) )
        {
            const int height = m_panel->FromDIP( m_frame->eeconfig()->Get<int>(
                    "appearance.used_components_preview_height" ).value_or( 200 ) );
            const int minimum = m_panel->FromDIP( 80 );
            m_splitter->SetSashPosition( std::clamp( height, minimum,
                    m_splitter->GetClientSize().y - minimum - m_splitter->GetSashSize() ) );
            m_sashRestored = true;
        }
        auto entries = BuildUsedSymbolList( m_frame->GetCurrentSheet() );
        wxArrayString labels;
        int selected = wxNOT_FOUND;
        for( std::size_t i = 0; i < entries.size(); ++i )
        {
            labels.Add( entries[i].label );
            if( entries[i].key == m_choice )
                selected = i;
        }
        m_entries = std::move( entries );
        m_list->Set( labels );
        m_list->SetSelection( selected );
        if( selected == wxNOT_FOUND )
            cancel();
        else
            showPreview();
    }

    std::unique_ptr<wxPanel> m_panel;
    SYMBOL_PREVIEW_WIDGET* m_preview = nullptr;
    wxButton* m_dockButton = nullptr;
    wxSplitterWindow* m_splitter = nullptr;
    bool m_sashRestored = false;
    wxStaticText* m_hint = nullptr;
    wxListBox* m_list = nullptr;
    wxTimer m_timer;
    std::vector<USED_SYMBOL_ENTRY> m_entries;
    wxString m_choice;
    wxString m_instruction;
    std::optional<KIID> m_pending;
};

void RegisterUsedSymbols( TOOL_MANAGER* manager ) { manager->RegisterTool( new USED_SYMBOLS_TOOL ); }
void InstallUsedSymbols( SCH_EDIT_FRAME* frame ) { frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>()->Install(); }
void RestoreUsedSymbolsVisibility( SCH_EDIT_FRAME* frame ) { frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>()->RestoreVisibility(); }
void ShutdownUsedSymbols( SCH_EDIT_FRAME* frame )
{
    if( frame->GetToolManager() )
    {
        if( auto* tool = frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>() )
            tool->Shutdown();
    }
}
void RebindUsedSymbols( SCH_EDIT_FRAME* frame ) { frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>()->Rebind(); }
bool BeginUsedSymbolPlacement( SCH_EDIT_FRAME* frame, const VECTOR2I& position )
{
    auto* tool = frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>();
    return tool && tool->BeginPlacement( position );
}
bool CancelUsedSymbolPlacement( SCH_EDIT_FRAME* frame, const SCH_SYMBOL* preview )
{
    if( auto* tool = frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>() )
        return tool->CancelPlacement( preview );
    return false;
}
void FinishUsedSymbolPlacement( SCH_EDIT_FRAME* frame, const SCH_SYMBOL* preview )
{
    if( auto* tool = frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>() )
        tool->FinishPlacement( preview );
}
bool UsedSymbolPlacementOverExisting( SCH_EDIT_FRAME* frame, const SCH_SYMBOL* symbol,
                                       const VECTOR2I& position )
{
    auto* tool = frame->GetToolManager()->GetTool<USED_SYMBOLS_TOOL>();
    if( !tool || !tool->OwnsPlacement( symbol ) )
        return false;
    SCH_COLLECTOR collector;
    frame->GetToolManager()->GetTool<SCH_SELECTION_TOOL>()->CollectHits( collector, position, { SCH_SYMBOL_T } );
    return collector.GetCount() > 0;
}
}
