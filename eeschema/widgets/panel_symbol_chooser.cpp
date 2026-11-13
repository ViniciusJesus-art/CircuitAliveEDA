/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014 Henner Zeller <h.zeller@acm.org>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "panel_symbol_chooser.h"

#include <pgm_base.h>
#include <settings/common_settings.h>
#include <kiface_base.h>
#include <sch_base_frame.h>
#include <sch_pin.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <widgets/lib_tree.h>
#include <widgets/symbol_preview_widget.h>
#include <widgets/footprint_preview_widget.h>
#include <widgets/footprint_select_widget.h>
#include <settings/settings_manager.h>
#include <project/project_file.h>
#include <eeschema_settings.h>
#include <symbol_editor_settings.h>
#include <symbol_library_common.h>         // For SYMBOL_LIBRARY_FILTER
#include <trace_helpers.h>
#include <algorithm>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/display.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/timer.h>
#include <wx/wxhtml.h>
#include <wx/log.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/srchctrl.h>


wxString PANEL_SYMBOL_CHOOSER::g_symbolSearchString;
wxString PANEL_SYMBOL_CHOOSER::g_powerSearchString;
SCH_BASE_FRAME* PANEL_SYMBOL_CHOOSER::m_frame = nullptr;

static EESCHEMA_SETTINGS::PANEL_SYM_CHOOSER& ChooserPreferences()
{
    return GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" )->m_SymChooserPanel;
}

static void SaveChooserPreferences()
{
    Pgm().GetSettingsManager().Save( GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" ) );
}

PANEL_SYMBOL_CHOOSER::PANEL_SYMBOL_CHOOSER( SCH_BASE_FRAME* aFrame, wxWindow* aParent,
                                            const SYMBOL_LIBRARY_FILTER* aFilter,
                                            std::vector<PICKED_SYMBOL>&  aHistoryList,
                                            std::vector<PICKED_SYMBOL>&  aAlreadyPlaced,
                                            bool aAllowFieldEdits, bool aShowFootprints, bool& aCancelled,
                                            std::function<void()> aAcceptHandler,
                                            std::function<void()> aEscapeHandler ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize ),
        m_symbol_preview( nullptr ),
        m_hsplitter( nullptr ),
        m_vsplitter( nullptr ),
        m_fp_sel_ctrl( nullptr ),
        m_fp_preview( nullptr ),
        m_tree( nullptr ),
        m_details( nullptr ),
        m_acceptHandler( std::move( aAcceptHandler ) ),
        m_escapeHandler( std::move( aEscapeHandler ) ),
        m_showPower( false ),
        m_allow_field_edits( aAllowFieldEdits ),
        m_show_footprints( aShowFootprints )
{
    m_frame = aFrame;

    SYMBOL_LIBRARY_ADAPTER*   libmgr = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() );
    COMMON_SETTINGS::SESSION& session = Pgm().GetCommonSettings()->m_Session;
    PROJECT_FILE&             project = m_frame->Prj().GetProjectFile();

    // Make sure settings are loaded before we start running multi-threaded symbol loaders
    GetAppSettings<EESCHEMA_SETTINGS>( "eeschema" );
    GetAppSettings<SYMBOL_EDITOR_SETTINGS>( "symbol_editor" );

    m_adapter = SYMBOL_TREE_MODEL_ADAPTER::Create( m_frame, libmgr );
    SYMBOL_TREE_MODEL_ADAPTER* adapter = static_cast<SYMBOL_TREE_MODEL_ADAPTER*>( m_adapter.get() );

    if( aFilter )
    {
        const wxArrayString& liblist = aFilter->GetAllowedLibList();

        for( const wxString& nickname : liblist )
        {
            if( libmgr->HasLibrary( nickname, true ) )
            {
                bool pinned = alg::contains( session.pinned_symbol_libs, nickname )
                                || alg::contains( project.m_PinnedSymbolLibs, nickname );

                std::optional<LIBRARY_TABLE_ROW*> row = libmgr->GetRow( nickname );

                if( row.has_value() && !row.value()->Hidden()  )
                    adapter->AddLibrary( nickname, pinned );
            }
        }

        adapter->AssignIntrinsicRanks();

        if( aFilter->GetFilterPowerSymbols() )
        {
            static std::function<bool( LIB_TREE_NODE& )> powerFilter =
                    []( LIB_TREE_NODE& aNode ) -> bool
                    {
                        return aNode.m_IsPower;
                    };

            adapter->SetFilter( &powerFilter );

            m_showPower = true;
            m_show_footprints = false;
        }
    }

    std::vector<LIB_SYMBOL>     history_list_storage;
    std::vector<LIB_TREE_ITEM*> history_list;
    std::vector<LIB_SYMBOL>     already_placed_storage;
    std::vector<LIB_TREE_ITEM*> already_placed;

    // Lambda to encapsulate the common logic
    auto processList =
            [&]( const std::vector<PICKED_SYMBOL>& inputList,
                 std::vector<LIB_SYMBOL>&          storageList,
                 std::vector<LIB_TREE_ITEM*>&      resultList )
            {
                storageList.reserve( inputList.size() );

                for( const PICKED_SYMBOL& i : inputList )
                {
                    LIB_SYMBOL* symbol = m_frame->GetLibSymbol( i.LibId, false, false );

                    if( symbol )
                    {
                        storageList.emplace_back( *symbol );
                        storageList.back().SetLibId( i.LibId );

                        for( const auto& [fieldType, fieldValue] : i.Fields )
                        {
                            SCH_FIELD* field = storageList.back().GetField( fieldType );

                            if( field )
                                field->SetText( fieldValue );
                        }

                        resultList.push_back( &storageList.back() );
                    }
                }
            };

    // Sort the already placed list since it is potentially from multiple sessions,
    // but not the most recent list since we want this listed by most recent usage.
    std::sort( aAlreadyPlaced.begin(), aAlreadyPlaced.end(),
               []( PICKED_SYMBOL const& a, PICKED_SYMBOL const& b )
               {
                   return a.LibId.GetLibItemName() < b.LibId.GetLibItemName();
               } );

    std::vector<PICKED_SYMBOL> history = aHistoryList;
    for( const auto& identity : ChooserPreferences().recent_symbols )
    {
        LIB_ID id;
        if( id.Parse( identity ) < 0 && id.IsValid()
            && std::none_of( history.begin(), history.end(), [&]( const auto& item ) { return item.LibId == id; } ) )
        {
            PICKED_SYMBOL picked;
            picked.LibId = id;
            history.push_back( picked );
        }
    }
    processList( history, history_list_storage, history_list );
    processList( aAlreadyPlaced, already_placed_storage, already_placed );

    adapter->DoAddLibrary( wxT( "-- " ) + _( "Recently Used" ) + wxT( " --" ), wxEmptyString,
                           history_list, false, true )
            .m_IsRecentlyUsedGroup = true;

    if( !aHistoryList.empty() )
        adapter->SetPreselectNode( aHistoryList[0].LibId, aHistoryList[0].Unit );

    adapter->DoAddLibrary( wxT( "-- " ) + _( "Already Placed" ) + wxT( " --" ), wxEmptyString,
                           already_placed, false, true )
            .m_IsAlreadyPlacedGroup = true;

    adapter->AddLibraries( m_frame );

    // -------------------------------------------------------------------------------------
    // Construct the actual panel
    //

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    // Use a slightly different layout, with a details pane spanning the entire window,
    // if we're not showing footprints.
    if( m_show_footprints )
    {
        m_hsplitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE | wxSP_NOBORDER | wxSP_3DSASH );

        //Avoid the splitter window being assigned as the Parent to additional windows
        m_hsplitter->SetExtraStyle( wxWS_EX_TRANSIENT );

        sizer->Add( m_hsplitter, 1, wxEXPAND, 5 );
    }
    else
    {
        m_vsplitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE | wxSP_NOBORDER | wxSP_3DSASH );

        m_hsplitter = new wxSplitterWindow( m_vsplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE | wxSP_NOBORDER | wxSP_3DSASH );

        // Avoid the splitter window being assigned as the parent to additional windows.
        m_vsplitter->SetExtraStyle( wxWS_EX_TRANSIENT );
        m_hsplitter->SetExtraStyle( wxWS_EX_TRANSIENT );

        wxPanel*    detailsPanel = new wxPanel( m_vsplitter );
        wxBoxSizer* detailsSizer = new wxBoxSizer( wxVERTICAL );
        detailsPanel->SetSizer( detailsSizer );

        m_details = new HTML_WINDOW( detailsPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize  );
        detailsSizer->Add( m_details, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );
        detailsPanel->Layout();
        detailsSizer->Fit( detailsPanel );

        m_vsplitter->SetSashGravity( 0.5 );
        m_vsplitter->SetMinimumPaneSize( 20 );
        m_vsplitter->SplitHorizontally( m_hsplitter, detailsPanel );

        sizer->Add( m_vsplitter, 1, wxEXPAND | wxBOTTOM, 5 );
    }

    wxPanel*    treePanel = new wxPanel( m_hsplitter );
    wxBoxSizer* treeSizer = new wxBoxSizer( wxVERTICAL );
    treePanel->SetSizer( treeSizer );

    m_tree = new LIB_TREE( treePanel, m_showPower ? wxT( "power" ) : wxT( "symbols" ), m_adapter,
                           LIB_TREE::FLAGS::ALL_WIDGETS, m_details );

    if( !m_showPower )
    {
        adapter->SetCompactInfo( true );
        m_tree->SetCompactDetails( true );
        buildChoiceTools( treePanel, treeSizer );
    }

    treeSizer->Add( m_tree, 1, wxALL | wxEXPAND, 5 );
    treePanel->Layout();
    treeSizer->Fit( treePanel );

    m_adapter->FinishTreeInitialization();

    if( m_showPower )
        m_tree->SetSearchString( g_powerSearchString );
    else
        m_tree->SetSearchString( g_symbolSearchString );

    m_hsplitter->SetSashGravity( 0.8 );
    m_hsplitter->SetMinimumPaneSize( 20 );
    m_hsplitter->SplitVertically( treePanel, constructRightPanel( m_hsplitter ) );

    if( !m_showPower )
    {
        m_originalFilter = m_adapter->GetFilter();
        m_choiceFilter = [this]( LIB_TREE_NODE& node )
        {
            if( m_originalFilter && !( *m_originalFilter )( node ) ) return false;
            const auto& settings = ChooserPreferences();
            const wxString id = node.m_LibId.Format();
            if( m_quickList->GetSelection() == 1
                && !alg::contains( settings.favorites, id ) ) return false;
            if( m_quickList->GetSelection() == 2
                && !alg::contains( settings.recent_symbols, id ) ) return false;
            const int pins = m_pinFilter && m_pinFilter->GetValue() ? m_pinFilter->GetValue() : m_query.pins;
            auto& item = node.m_Type == LIB_TREE_NODE::TYPE::UNIT && node.m_Parent
                         ? *node.m_Parent : node;
            if( pins > 0 && item.m_PinCount == 0 && item.m_LibId.IsValid() )
            {
                // Derived symbols can have an empty cached count even though
                // their parent supplies all physical pins. Resolve it once.
                if( const auto* symbol = m_frame->GetLibSymbol( item.m_LibId, false, false ) )
                    for( const auto* pin : symbol->GetGraphicalPins( 0, 1 ) )
                        item.m_PinCount += pin->GetStackedPinCount();
            }
            if( pins > 0 && item.m_PinCount != pins ) return false;
            if( !m_query.package.empty() && node.m_LibId.IsValid() )
            {
                // A package-only search should omit symbols whose library explicitly
                // restricts them to another package (for example, SOIC-only parts).
                const auto* symbol = m_frame->GetLibSymbol( node.m_LibId, false, false );
                const auto patterns = symbol ? symbol->GetFPFilters() : wxArrayString{};
                if( !patterns.empty()
                    && std::none_of( patterns.begin(), patterns.end(),
                            [&]( const wxString& pattern )
                            {
                                return pattern == "*" || CIRCUITALIVE::MatchesPhysicalText(
                                        CIRCUITALIVE::AlternatePackagePattern( pattern.ToStdString( wxConvUTF8 ) ),
                                        m_query.package == "pdip" ? "dip" : m_query.package );
                            } ) )
                    return false;
            }
            return true;
        };
        m_adapter->SetFilter( &m_choiceFilter );
        m_tree->SetSearchPreprocessor( [this]( const wxString& search ) { return prepareSearch( search ); } );
    }

    m_dbl_click_timer = new wxTimer( this );
    m_open_libs_timer = new wxTimer( this );

    SetSizer( sizer );

    Layout();

    Bind( wxEVT_TIMER, &PANEL_SYMBOL_CHOOSER::onCloseTimer, this, m_dbl_click_timer->GetId() );
    Bind( wxEVT_TIMER, &PANEL_SYMBOL_CHOOSER::onOpenLibsTimer, this, m_open_libs_timer->GetId() );
    Bind( EVT_LIBITEM_SELECTED, &PANEL_SYMBOL_CHOOSER::onSymbolSelected, this );
    Bind( EVT_LIBITEM_CHOSEN, &PANEL_SYMBOL_CHOOSER::onSymbolChosen, this );
    aFrame->Bind( wxEVT_MENU_OPEN, &PANEL_SYMBOL_CHOOSER::onMenuOpen, this );
    aFrame->Bind( wxEVT_MENU_CLOSE, &PANEL_SYMBOL_CHOOSER::onMenuClose, this );

    if( m_fp_sel_ctrl )
        m_fp_sel_ctrl->Bind( EVT_FOOTPRINT_SELECTED, &PANEL_SYMBOL_CHOOSER::onFootprintSelected, this );

    if( m_details )
        m_details->Bind( wxEVT_CHAR_HOOK, &PANEL_SYMBOL_CHOOSER::OnDetailsCharHook, this );

    // Open the user's previously opened libraries on timer expiration.
    // This is done on a timer because we need a gross hack to keep GTK from garbling the
    // display. Must be longer than the search debounce timer.
    m_open_libs_timer->StartOnce( 300 );
}


PANEL_SYMBOL_CHOOSER::~PANEL_SYMBOL_CHOOSER()
{
    m_shuttingDown = true;
    m_tree->SetSearchPreprocessor( {} );
    m_adapter->SetFilter( m_originalFilter );
    if( m_fp_preview && m_fp_preview->IsInitialized() )
        m_fp_preview->GetPreviewPanel()->Shutdown3DPreview();
    m_frame->Unbind( wxEVT_MENU_OPEN, &PANEL_SYMBOL_CHOOSER::onMenuOpen, this );
    m_frame->Unbind( wxEVT_MENU_CLOSE, &PANEL_SYMBOL_CHOOSER::onMenuClose, this );
    Unbind( wxEVT_TIMER, &PANEL_SYMBOL_CHOOSER::onCloseTimer, this );
    Unbind( EVT_LIBITEM_SELECTED, &PANEL_SYMBOL_CHOOSER::onSymbolSelected, this );
    Unbind( EVT_LIBITEM_CHOSEN, &PANEL_SYMBOL_CHOOSER::onSymbolChosen, this );

    // Stop the timer during destruction early to avoid potential race conditions (that do happen)
    m_dbl_click_timer->Stop();
    m_open_libs_timer->Stop();
    delete m_dbl_click_timer;
    delete m_open_libs_timer;

    if( m_showPower )
        g_powerSearchString = m_tree->GetSearchString();
    else
        g_symbolSearchString = m_tree->GetSearchString();

    if( m_fp_sel_ctrl )
        m_fp_sel_ctrl->Unbind( EVT_FOOTPRINT_SELECTED, &PANEL_SYMBOL_CHOOSER::onFootprintSelected, this );

    if( m_details )
        m_details->Unbind( wxEVT_CHAR_HOOK, &PANEL_SYMBOL_CHOOSER::OnDetailsCharHook, this );

    if( EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() ) )
    {
        // Save any changes to column widths, etc.
        m_adapter->SaveSettings();

        cfg->m_SymChooserPanel.width = GetParent()->ToDIP( GetParent()->GetSize().x );
        cfg->m_SymChooserPanel.height = GetParent()->ToDIP( GetParent()->GetSize().y );

        cfg->m_SymChooserPanel.sash_pos_h = m_hsplitter->GetSashPosition();

        if( m_vsplitter )
            cfg->m_SymChooserPanel.sash_pos_v = m_vsplitter->GetSashPosition();

        cfg->m_SymChooserPanel.sort_mode = m_tree->GetSortMode();
    }

    m_frame = nullptr;
}


void PANEL_SYMBOL_CHOOSER::onMenuOpen( wxMenuEvent& aEvent )
{
    m_tree->BlockPreview( true );
    aEvent.Skip();
}


void PANEL_SYMBOL_CHOOSER::onMenuClose( wxMenuEvent& aEvent )
{
    m_tree->BlockPreview( false );
    aEvent.Skip();
}


void PANEL_SYMBOL_CHOOSER::OnChar( wxKeyEvent& aEvent )
{
    if( aEvent.GetKeyCode() == WXK_ESCAPE )
    {
        wxObject* eventSource = aEvent.GetEventObject();

        if( wxTextCtrl* textCtrl = dynamic_cast<wxTextCtrl*>( eventSource ) )
        {
            // First escape cancels search string value
            if( textCtrl->GetValue() == m_tree->GetSearchString()
                && !m_tree->GetSearchString().IsEmpty() )
            {
                m_tree->SetSearchString( wxEmptyString );
                return;
            }
        }

        m_escapeHandler();
    }
    else
    {
        aEvent.Skip();
    }
}


wxPanel* PANEL_SYMBOL_CHOOSER::constructRightPanel( wxWindow* aParent )
{
    EDA_DRAW_PANEL_GAL::GAL_TYPE backend = EDA_DRAW_PANEL_GAL::GAL_TYPE::GAL_TYPE_OPENGL;

    if( m_frame->GetCanvas() )
        backend = m_frame->GetCanvas()->GetBackend();
    else if( COMMON_SETTINGS* cfg = Pgm().GetCommonSettings() )
        backend = static_cast<EDA_DRAW_PANEL_GAL::GAL_TYPE>( cfg->m_Graphics.canvas_type );

    wxPanel*    panel = new wxPanel( aParent );
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    m_symbol_preview = new SYMBOL_PREVIEW_WIDGET( panel, &m_frame->Kiway(), true, backend );
    m_symbol_preview->SetLayoutDirection( wxLayout_LeftToRight );

    if( m_show_footprints )
    {
        sizer->Add( m_symbol_preview, 10, wxEXPAND | wxALL, 5 );

        auto* filters = new wxBoxSizer( wxHORIZONTAL );
        m_mounting = new wxChoice( panel, wxID_ANY );
        for( const auto& label : { _( "Any mounting" ), wxString( "PTH" ), wxString( "SMD" ) } )
            m_mounting->Append( label );
        m_mounting->SetSelection( 0 );
        m_mounting->SetToolTip( _( "Filter footprints by through-hole or surface mounting" ) );
        filters->Add( m_mounting, 0, wxRIGHT, 4 );
        m_pinFilter = new wxSpinCtrl( panel, wxID_ANY, "0", wxDefaultPosition,
                                     FromDIP( wxSize( 72, -1 ) ), wxSP_ARROW_KEYS, 0, 2048, 0 );
        m_pinFilter->SetToolTip( _( "Physical pins: 0 uses the selected symbol's pin count" ) );
        filters->Add( new wxStaticText( panel, wxID_ANY, _( "Pins" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
        filters->Add( m_pinFilter, 0, wxRIGHT, 4 );
        m_package = new wxTextCtrl( panel, wxID_ANY );
        m_package->SetHint( _( "Package / size / power" ) );
        m_package->SetToolTip( _( "Examples: DIP, SOIC, 5mm, 1/4W. Filters the compatible footprints below." ) );
        filters->Add( m_package, 1 );
        sizer->Add( filters, 0, wxEXPAND | wxALL, 5 );

        auto* dimensions = new wxBoxSizer( wxHORIZONTAL );
        dimensions->Add( new wxStaticText( panel, wxID_ANY, _( "Max W / H (mm)" ) ),
                         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
        m_width = new wxSpinCtrlDouble( panel, wxID_ANY, "0", wxDefaultPosition,
                                       FromDIP( wxSize( 78, -1 ) ), wxSP_ARROW_KEYS, 0, 1000, 0, .5 );
        m_height = new wxSpinCtrlDouble( panel, wxID_ANY, "0", wxDefaultPosition,
                                        FromDIP( wxSize( 78, -1 ) ), wxSP_ARROW_KEYS, 0, 1000, 0, .5 );
        m_width->SetDigits( 2 );
        m_height->SetDigits( 2 );
        for( auto* control : { m_width, m_height } )
        {
            control->SetToolTip( _( "Maximum footprint extent excluding text; 0 means unrestricted" ) );
            dimensions->Add( control, 0, wxRIGHT, 4 );
        }
        auto* reset = new wxButton( panel, wxID_ANY, _( "Reset filters" ) );
        dimensions->Add( reset, 0 );
        reset->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            m_mounting->SetSelection( 0 ); m_pinFilter->SetValue( 0 );
            m_width->SetValue( 0 ); m_height->SetValue( 0 ); m_package->ChangeValue( "" );
            m_tree->SetSearchString( "" ); m_tree->Regenerate( false ); refreshPhysicalFilters();
        } );
        sizer->Add( dimensions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );

        auto filterChanged = [this]( wxCommandEvent& )
        {
            if( !m_shuttingDown ) { m_tree->Regenerate( true ); refreshPhysicalFilters(); }
        };
        m_mounting->Bind( wxEVT_CHOICE, filterChanged );
        m_pinFilter->Bind( wxEVT_TEXT, filterChanged );
        m_package->Bind( wxEVT_TEXT, filterChanged );
        m_width->Bind( wxEVT_TEXT, filterChanged );
        m_height->Bind( wxEVT_TEXT, filterChanged );

        m_fp_sel_ctrl = new FOOTPRINT_SELECT_WIDGET( m_frame, panel );
        sizer->Add( m_fp_sel_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT, 5 );

        m_makeDefault = new wxButton( panel, wxID_ANY, _( "Make default for this symbol" ) );
        m_makeDefault->SetToolTip( _( "Save the selected footprint for future choices in every project. Select empty to save no footprint." ) );
        sizer->Add( m_makeDefault, 0, wxEXPAND | wxALL, 5 );
        m_makeDefault->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            const auto id = GetSelectedLibId();
            if( !id.IsValid() ) return;
            auto& cfg = ChooserPreferences();
            const wxString key = id.Format();
            cfg.preferred_footprints[key.ToStdString( wxConvUTF8 )] = m_displayedFootprint;
            if( !alg::contains( cfg.override_library_footprints, key ) )
                cfg.override_library_footprints.push_back( key );
            SaveChooserPreferences();
        } );

        m_fp_preview = new FOOTPRINT_PREVIEW_WIDGET( panel, m_frame->Kiway() );
        m_fp_preview->SetUserUnits( m_frame->GetUserUnits() );

        auto* previews = new wxBoxSizer( wxHORIZONTAL );
        auto* layout2D = new wxBoxSizer( wxVERTICAL );
        layout2D->Add( new wxStaticText( panel, wxID_ANY, _( "Footprint" ) ), 0, wxALL, 3 );
        layout2D->Add( m_fp_preview, 1, wxEXPAND );
        previews->Add( layout2D, 1, wxEXPAND | wxRIGHT, 4 );
        if( m_fp_preview->IsInitialized() )
        {
            auto* layout3D = new wxBoxSizer( wxVERTICAL );
            layout3D->Add( new wxStaticText( panel, wxID_ANY, _( "3D — drag to rotate" ) ), 0, wxALL, 3 );
            layout3D->Add( m_fp_preview->GetPreviewPanel()->Create3DPreview( panel ), 1, wxEXPAND );
            previews->Add( layout3D, 1, wxEXPAND );
        }
        sizer->Add( previews, 12, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5 );
    }
    else
    {
        sizer->Add( m_symbol_preview, 1, wxEXPAND | wxALL, 5 );
    }

    panel->SetSizer( sizer );
    panel->Layout();
    sizer->Fit( panel );

    return panel;
}


void PANEL_SYMBOL_CHOOSER::FinishSetup()
{
    if( EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() ) )
    {
        auto horizPixelsFromDU =
                [&]( int x ) -> int
                {
                    wxSize sz( x, 0 );
                    return GetParent()->ConvertDialogToPixels( sz ).x;
                };

        EESCHEMA_SETTINGS::PANEL_SYM_CHOOSER& panelCfg = cfg->m_SymChooserPanel;

        // The persisted size is stored in DIP so it is independent of the monitor it was saved
        // on. Restoring raw pixels would scale the window by the DPI ratio when reopened on a
        // different-scale display, producing a window that spills across monitors.
        int w = panelCfg.width > 40 ? GetParent()->FromDIP( panelCfg.width ) : horizPixelsFromDU( 440 );
        int h = panelCfg.height > 40 ? GetParent()->FromDIP( panelCfg.height ) : horizPixelsFromDU( 340 );

        // Cap to the work area so a stale pre-DIP setting cannot reopen the window across monitors.
        if( int display = wxDisplay::GetFromWindow( GetParent() ); display != wxNOT_FOUND )
        {
            wxRect workArea = wxDisplay( display ).GetClientArea();
            w = std::min( w, workArea.GetWidth() );
            h = std::min( h, workArea.GetHeight() );
        }

        GetParent()->SetSize( wxSize( w, h ) );
        GetParent()->Layout();

        // We specify the width of the right window (m_symbol_view_panel), because specify
        // the width of the left window does not work as expected when SetSashGravity() is called

        if( panelCfg.sash_pos_h < 0 )
            panelCfg.sash_pos_h = horizPixelsFromDU( 220 );

        if( panelCfg.sash_pos_v < 0 )
            panelCfg.sash_pos_v = horizPixelsFromDU( 230 );

        m_hsplitter->SetSashPosition( panelCfg.sash_pos_h );

        if( m_vsplitter )
            m_vsplitter->SetSashPosition( panelCfg.sash_pos_v );

        m_adapter->SetSortMode( (LIB_TREE_MODEL_ADAPTER::SORT_MODE) panelCfg.sort_mode );
    }

    if( m_fp_preview && m_fp_preview->IsInitialized() )
    {
        // This hides the GAL panel and shows the status label
        m_fp_preview->SetStatusText( wxEmptyString );
    }

    if( m_fp_sel_ctrl )
        m_fp_sel_ctrl->Load( m_frame->Kiway(), m_frame->Prj() );
}


void PANEL_SYMBOL_CHOOSER::OnDetailsCharHook( wxKeyEvent& e )
{
    if( m_details && e.GetKeyCode() == 'C' && e.ControlDown() &&
        !e.AltDown() && !e.ShiftDown() && !e.MetaDown() )
    {
        wxString txt = m_details->SelectionToText();
        wxLogNull doNotLog; // disable logging of failed clipboard actions

        if( wxTheClipboard->Open() )
        {
            wxTheClipboard->SetData( new wxTextDataObject( txt ) );
            wxTheClipboard->Flush(); // Allow data to be available after closing KiCad
            wxTheClipboard->Close();
        }
    }
    else
    {
        e.Skip();
    }
}


void PANEL_SYMBOL_CHOOSER::SetPreselect( const LIB_ID& aPreselect )
{
    m_adapter->SetPreselectNode( aPreselect, 0 );

    if( m_tree && aPreselect.IsValid() )
        m_tree->SelectLibId( aPreselect );
}


LIB_ID PANEL_SYMBOL_CHOOSER::GetSelectedLibId( int* aUnit ) const
{
    return m_tree->GetSelectedLibId( aUnit );
}


void PANEL_SYMBOL_CHOOSER::ShutdownCanvases()
{
    m_shuttingDown = true;
    m_dbl_click_timer->Stop();
    m_open_libs_timer->Stop();
    wxLogTrace( traceGalContext, wxS( "Symbol chooser shutting down preview canvas %p" ),
                m_symbol_preview->GetCanvas() );

    m_symbol_preview->GetCanvas()->SetEvtHandlerEnabled( false );
    m_symbol_preview->GetCanvas()->StopDrawing();

    if( m_fp_preview && m_fp_preview->IsInitialized() )
    {
        m_fp_preview->GetPreviewPanel()->Shutdown3DPreview();
        m_fp_preview->GetPreviewPanel()->GetCanvas()->SetEvtHandlerEnabled( false );
        m_fp_preview->GetPreviewPanel()->GetCanvas()->StopDrawing();
    }
}


void PANEL_SYMBOL_CHOOSER::onCloseTimer( wxTimerEvent& aEvent )
{
    // Hack because of eaten MouseUp event. See PANEL_SYMBOL_CHOOSER::onSymbolChosen
    // for the beginning of this spaghetti noodle.

    wxMouseState state = wxGetMouseState();

    if( state.LeftIsDown() )
    {
        // Mouse hasn't been raised yet, so fire the timer again. Otherwise the
        // purpose of this timer is defeated.
        m_dbl_click_timer->StartOnce( PANEL_SYMBOL_CHOOSER::DBLCLICK_DELAY );
    }
    else
    {
        m_acceptHandler();
    }
}


void PANEL_SYMBOL_CHOOSER::onOpenLibsTimer( wxTimerEvent& aEvent )
{
    if( EESCHEMA_SETTINGS* cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() ) )
        m_adapter->OpenLibs( cfg->m_LibTree.open_libs );
}


void PANEL_SYMBOL_CHOOSER::showFootprintFor( LIB_ID const& aLibId )
{
    if( !m_fp_preview || !m_fp_preview->IsInitialized() )
        return;

    LIB_SYMBOL* symbol = nullptr;

    try
    {
        symbol = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() )->LoadSymbol( aLibId );
    }
    catch( const IO_ERROR& ioe )
    {
        wxLogError( _( "Error loading symbol %s from library '%s'." ) + wxS( "\n%s" ),
                    aLibId.GetLibItemName().wx_str(),
                    aLibId.GetLibNickname().wx_str(),
                    ioe.What() );
    }

    if( !symbol )
        return;

    SCH_FIELD* fp_field = symbol->GetField( FIELD_T::FOOTPRINT );
    wxString   fp_name = preferredFootprint( aLibId, fp_field ? fp_field->GetFullText() : wxString( "" ) );

    m_fp_override.reset();
    if( fp_field && ( fp_name != fp_field->GetFullText() || m_keepEmptyFootprint ) )
    {
        m_fp_override = fp_name;
        m_field_edits.emplace_back( FIELD_T::FOOTPRINT, fp_name );
    }
    showFootprint( fp_name );
}


void PANEL_SYMBOL_CHOOSER::showFootprint( wxString const& aFootprint )
{
    m_displayedFootprint = aFootprint;
    if( !m_fp_preview || !m_fp_preview->IsInitialized() )
        return;

    if( aFootprint == wxEmptyString )
    {
        m_fp_preview->SetStatusText( _( "No footprint specified" ) );
        m_fp_preview->GetPreviewPanel()->Update3DPreview( false );
    }
    else
    {
        LIB_ID lib_id;

        if( lib_id.Parse( aFootprint ) == -1 && lib_id.IsValid() )
        {
            m_fp_preview->ClearStatus();
            m_fp_preview->DisplayFootprint( lib_id );
            m_fp_preview->GetPreviewPanel()->Update3DPreview( true );
        }
        else
        {
            m_fp_preview->SetStatusText( _( "Invalid footprint specified" ) );
            m_fp_preview->GetPreviewPanel()->Update3DPreview( false );
        }
    }
}


void PANEL_SYMBOL_CHOOSER::populateFootprintSelector( LIB_ID const& aLibId )
{
    if( !m_fp_sel_ctrl )
        return;

    m_fp_sel_ctrl->ClearFilters();

    LIB_SYMBOL* symbol = nullptr;

    if( aLibId.IsValid() )
    {
        try
        {
            symbol = PROJECT_SCH::SymbolLibAdapter( &m_frame->Prj() )->LoadSymbol( aLibId );
        }
        catch( const IO_ERROR& ioe )
        {
            wxLogError( _( "Error loading symbol %s from library '%s'." ) + wxS( "\n%s" ),
                        aLibId.GetLibItemName().wx_str(),
                        aLibId.GetLibNickname().wx_str(),
                        ioe.What() );
        }
    }

    if( symbol != nullptr )
    {
        int        pinCount = symbol->GetGraphicalPins( 0 /* all units */, 1 /* single bodyStyle */ ).size();
        SCH_FIELD* fp_field = symbol->GetField( FIELD_T::FOOTPRINT );
        wxString   fp_name = preferredFootprint( aLibId, fp_field ? fp_field->GetFullText() : wxString( "" ) );

        if( m_fp_override )
            fp_name = *m_fp_override;

        m_fp_sel_ctrl->FilterByPinCount( pinCount );
        m_fp_sel_ctrl->FilterByFootprintFilters( symbol->GetFPFilters(), true );
        if( m_pinFilter )
        {
            const int requested = m_pinFilter->GetValue() ? m_pinFilter->GetValue() : m_query.pins;
            if( requested ) m_fp_sel_ctrl->FilterByPinCount( requested );
            const auto physical = CIRCUITALIVE::ParseComponentQuery( m_package->GetValue().ToStdString( wxConvUTF8 ) );
            const wxString mounting = m_mounting->GetSelection() == 1 ? "pth"
                                      : m_mounting->GetSelection() == 2 ? "smd"
                                      : wxString::FromUTF8( m_query.mounting );
            wxString extra = wxString::FromUTF8( m_query.footprint );
            extra += " " + wxString::FromUTF8( physical.symbols + " " + physical.footprint );
            m_fp_sel_ctrl->FilterByPhysicalProperties( mounting, extra,
                    wxString::FromUTF8( physical.package.empty() ? m_query.package : physical.package ),
                    m_width->GetValue() ? m_width->GetValue() : m_query.width,
                    m_height->GetValue() ? m_height->GetValue() : m_query.height );
        }
        m_fp_sel_ctrl->SetDefaultFootprint( fp_name );
        m_fp_sel_ctrl->UpdateList();
        m_fp_sel_ctrl->Enable();
        if( m_allow_field_edits && !m_keepEmptyFootprint
            && ( !m_query.footprint.empty() || !m_query.package.empty()
            || !m_query.mounting.empty() || m_query.width > 0 || m_query.height > 0
            || ( m_package && !m_package->GetValue().IsEmpty() )
            || ( m_mounting && m_mounting->GetSelection() > 0 )
            || ( m_width && m_width->GetValue() > 0 ) || ( m_height && m_height->GetValue() > 0 ) ) )
            m_fp_sel_ctrl->SelectPhysicalMatch();
    }
    else
    {
        m_fp_sel_ctrl->FilterByFootprintFilters( {}, true );
        m_fp_sel_ctrl->UpdateList();
        m_fp_sel_ctrl->Disable();
    }
}


void PANEL_SYMBOL_CHOOSER::onFootprintSelected( wxCommandEvent& aEvent )
{
    if( m_shuttingDown ) return;
    // Automatic matching must not undo a deliberate empty choice.
    if( aEvent.GetInt() && m_keepEmptyFootprint ) return;
    if( !aEvent.GetInt() ) m_keepEmptyFootprint = aEvent.GetString().IsEmpty();
    m_fp_override = aEvent.GetString();

    std::erase_if( m_field_edits,
            []( std::pair<FIELD_T, wxString> const& i )
            {
                return i.first == FIELD_T::FOOTPRINT;
            } );

    m_field_edits.emplace_back( FIELD_T::FOOTPRINT, *m_fp_override );

    showFootprint( *m_fp_override );
    updateChoiceTools();
}


void PANEL_SYMBOL_CHOOSER::onSymbolSelected( wxCommandEvent& aEvent )
{
    if( m_shuttingDown ) return;
    LIB_TREE_NODE* node = m_tree->GetCurrentTreeNode();

    m_field_edits.clear();
    if( node && node->m_LibId.IsValid() )
    {
        m_symbol_preview->DisplaySymbol( node->m_LibId, node->m_Unit );

        const wxString identity = node->m_LibId.Format();
        const auto& preferences = ChooserPreferences();
        const auto saved = preferences.preferred_footprints.find( identity.ToStdString( wxConvUTF8 ) );
        const bool hasPreference = saved != preferences.preferred_footprints.end();
        if( identity != m_choiceSymbol )
        {
            m_choiceSymbol = identity;
            m_fp_override.reset();
            m_keepEmptyFootprint = hasPreference && saved->second.IsEmpty()
                    && alg::contains( preferences.override_library_footprints, identity );
        }
        if( m_fp_override )
        {
            m_field_edits.emplace_back( FIELD_T::FOOTPRINT, *m_fp_override );
            showFootprint( *m_fp_override );
        }
        else if( !node->m_Footprint.IsEmpty() && !hasPreference )
        {
            wxCommandEvent evt( EVT_FOOTPRINT_SELECTED );
            evt.SetString( node->m_Footprint);
            evt.SetInt( 1 );
            onFootprintSelected( evt );
        }
        else
        {
            showFootprintFor( node->m_LibId );
        }

        populateFootprintSelector( node->m_LibId );
    }
    else
    {
        m_choiceSymbol.clear();
        m_fp_override.reset();
        m_keepEmptyFootprint = false;
        m_displayedFootprint.clear();
        m_symbol_preview->SetStatusText( _( "No symbol selected" ) );

        if( m_fp_preview && m_fp_preview->IsInitialized() )
            m_fp_preview->SetStatusText( wxEmptyString );

        populateFootprintSelector( LIB_ID() );
        if( m_fp_preview && m_fp_preview->IsInitialized() )
            m_fp_preview->GetPreviewPanel()->Update3DPreview( false );
    }
    updateChoiceTools();
}


void PANEL_SYMBOL_CHOOSER::onSymbolChosen( wxCommandEvent& aEvent )
{
    if( m_tree->GetSelectedLibId().IsValid() )
    {
        // Got a selection. We can't just end the modal dialog here, because wx leaks some events
        // back to the parent window (in particular, the MouseUp following a double click).
        //
        // NOW, here's where it gets really fun. wxTreeListCtrl eats MouseUp.  This isn't really
        // feasible to bypass without a fully custom wxDataViewCtrl implementation, and even then
        // might not be fully possible (docs are vague). To get around this, we use a one-shot
        // timer to schedule the dialog close.
        //
        // See PANEL_SYMBOL_CHOOSER::onCloseTimer for the other end of this spaghetti noodle.
        m_dbl_click_timer->StartOnce( PANEL_SYMBOL_CHOOSER::DBLCLICK_DELAY );
    }
}


void PANEL_SYMBOL_CHOOSER::Regenerate()
{
    LIB_ID savedSelection = m_tree->GetSelectedLibId();
    m_tree->Regenerate( true );

    if( savedSelection.IsValid() )
        m_tree->CenterLibId( savedSelection );
}

void PANEL_SYMBOL_CHOOSER::buildChoiceTools( wxWindow* aParent, wxBoxSizer* aSizer )
{
    auto* tools = new wxBoxSizer( wxHORIZONTAL );
    m_quickList = new wxChoice( aParent, wxID_ANY );
    for( const auto& label : { _( "All components" ), _( "Favorites" ), _( "Recent" ) } )
        m_quickList->Append( label );
    m_quickList->SetSelection( 0 );
    tools->Add( m_quickList, 1, wxRIGHT, 5 );
    m_favorite = new wxButton( aParent, wxID_ANY, wxString::FromUTF8( "☆ Favorite" ) );
    m_favorite->SetToolTip( _( "Add or remove this symbol from your application-wide favorites" ) );
    tools->Add( m_favorite, 0, wxRIGHT, 5 );
    m_advanced = new wxCheckBox( aParent, wxID_ANY, _( "Advanced (KiCad)" ) );
    m_advanced->SetValue( ChooserPreferences().advanced );
    m_advanced->SetToolTip( _( "Use the original library search syntax and all available KiCad libraries" ) );
    tools->Add( m_advanced, 0, wxALIGN_CENTER_VERTICAL );
    aSizer->Add( tools, 0, wxEXPAND | wxALL, 5 );
    if( auto* search = dynamic_cast<wxSearchCtrl*>( m_tree->GetFocusTarget() ) )
        search->SetDescriptiveText( _( "resistor 1/4W, DIP 14, LED 5mm, 74HC00…" ) );
    m_quickList->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { m_tree->Regenerate( false ); } );
    m_advanced->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
    {
        ChooserPreferences().advanced = m_advanced->GetValue();
        SaveChooserPreferences();
        m_tree->Regenerate( false );
        refreshPhysicalFilters();
    } );
    m_favorite->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    {
        const auto id = GetSelectedLibId();
        if( !id.IsValid() ) return;
        auto& favorites = ChooserPreferences().favorites;
        const wxString key = id.Format();
        if( alg::contains( favorites, key ) ) std::erase( favorites, key );
        else favorites.push_back( key );
        SaveChooserPreferences();
        updateChoiceTools();
        m_tree->Regenerate( true );
    } );
}

wxString PANEL_SYMBOL_CHOOSER::prepareSearch( const wxString& aSearch )
{
    auto query = m_advanced && m_advanced->GetValue() ? CIRCUITALIVE::COMPONENT_QUERY{}
                                                     : CIRCUITALIVE::ParseComponentQuery( aSearch.ToStdString( wxConvUTF8 ) );
    const bool physicalChanged = query.footprint != m_query.footprint || query.package != m_query.package
            || query.mounting != m_query.mounting || query.pins != m_query.pins
            || query.width != m_query.width || query.height != m_query.height;
    m_query = std::move( query );
    if( physicalChanged && m_fp_sel_ctrl )
        CallAfter( [this]() { if( !m_shuttingDown ) refreshPhysicalFilters(); } );
    if( m_advanced && m_advanced->GetValue() ) return aSearch;
    if( m_query.symbols.empty() && ( m_query.pins > 0 || !m_query.footprint.empty()
        || !m_query.mounting.empty() || !m_query.package.empty() || m_query.width > 0 || m_query.height > 0
        || m_quickList->GetSelection() != 0 || ( m_pinFilter && m_pinFilter->GetValue() > 0 ) ) )
        return "*";
    return wxString::FromUTF8( m_query.symbols );
}

void PANEL_SYMBOL_CHOOSER::refreshPhysicalFilters()
{
    if( m_fp_sel_ctrl ) populateFootprintSelector( GetSelectedLibId() );
}

wxString PANEL_SYMBOL_CHOOSER::preferredFootprint( const LIB_ID& aSymbol,
                                                 const wxString& aLibraryFootprint ) const
{
    const auto& cfg = ChooserPreferences();
    const wxString key = aSymbol.Format();
    const auto it = cfg.preferred_footprints.find( key.ToStdString( wxConvUTF8 ) );
    if( it == cfg.preferred_footprints.end() ) return aLibraryFootprint;
    return wxString::FromUTF8( CIRCUITALIVE::PreferredComponentFootprint(
            aLibraryFootprint.ToStdString( wxConvUTF8 ), it->second.ToStdString( wxConvUTF8 ),
            alg::contains( cfg.override_library_footprints, key ) ) );
}

void PANEL_SYMBOL_CHOOSER::updateChoiceTools()
{
    if( !m_favorite ) return;
    const auto id = GetSelectedLibId();
    const wxString key = id.Format();
    const auto& cfg = ChooserPreferences();
    m_favorite->Enable( id.IsValid() );
    m_favorite->SetLabel( wxString::FromUTF8( alg::contains( cfg.favorites, key ) ? "★ Favorite" : "☆ Favorite" ) );
    if( m_makeDefault )
        m_makeDefault->Enable( id.IsValid() && m_allow_field_edits );
}

void PANEL_SYMBOL_CHOOSER::RememberSelection() const
{
    const auto id = GetSelectedLibId();
    if( !id.IsValid() || m_showPower ) return;
    auto& recents = ChooserPreferences().recent_symbols;
    const wxString key = id.Format();
    std::erase( recents, key );
    recents.insert( recents.begin(), key );
    if( recents.size() > 30 ) recents.resize( 30 );
    const auto* symbol = m_frame->GetLibSymbol( id );
    const auto* field = symbol ? symbol->GetField( FIELD_T::FOOTPRINT ) : nullptr;
    auto& cfg = ChooserPreferences();
    const auto preference = cfg.preferred_footprints.find( key.ToStdString( wxConvUTF8 ) );
    const bool hasPreference = preference != cfg.preferred_footprints.end()
            && ( !preference->second.IsEmpty() || alg::contains( cfg.override_library_footprints, key ) );
    if( m_allow_field_edits && field && CIRCUITALIVE::RememberFirstComponentFootprint(
            field->GetFullText().ToStdString( wxConvUTF8 ), hasPreference,
            m_displayedFootprint.ToStdString( wxConvUTF8 ) ) )
        cfg.preferred_footprints[key.ToStdString( wxConvUTF8 )] = m_displayedFootprint;
    SaveChooserPreferences();
}
