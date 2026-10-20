/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2009 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2019 CERN
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

#include <bitmaps.h>
#include <file_history.h>
#include <kiface_base.h>
#include <schematic.h>
#include <tool/action_manager.h>
#include <tool/action_menu.h>
#include <tool/tool_manager.h>
#include <tools/sch_selection_tool.h>
#include <tools/sch_actions.h>
#include "eeschema_id.h"
#include "sch_edit_frame.h"
#include <widgets/wx_menubar.h>
#include <widgets/panel_remote_symbol.h>
#include <widgets/pages_menu.h>
#include <widgets/project_pages.h>
#include <widgets/used_symbols.h>
#include <advanced_config.h>
#include <sch_commit.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <dialog_shim.h>
#include <project.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <widgets/ui_common.h>


namespace
{
class PAGE_DIALOG : public DIALOG_SHIM
{
public:
    using DIALOG_SHIM::DIALOG_SHIM;
    using DIALOG_SHIM::finishDialogSettings;
};

class PAGES_MENU : public ACTION_MENU
{
public:
    PAGES_MENU( SCH_EDIT_FRAME* aFrame, SCH_SELECTION_TOOL* aTool ) :
            ACTION_MENU( false, aTool ), m_frame( aFrame )
    {
        Add( SCH_ACTIONS::navigatePrevious );
        Add( SCH_ACTIONS::navigateNext );
        const int newId = wxWindow::NewControlId();
        const int editId = wxWindow::NewControlId();
        Append( newId, _( "New Page..." ) );
        Append( editId, _( "Edit Page..." ) );
        m_deleteId = wxWindow::NewControlId();
        Append( m_deleteId, _( "Delete Page..." ) );
        AppendSeparator();
        Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { m_frame->NewPage(); }, newId );
        Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { m_frame->EditPage(); }, editId );
        Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { CIRCUITALIVE::DeleteCurrentPage( *m_frame ); }, m_deleteId );
        update();
    }

protected:
    void update() override
    {
        // Rebuild only when the list changes; never accumulate event bindings on the frame.
        const auto sheets = m_frame->Schematic().Hierarchy();
        Enable( m_deleteId, CIRCUITALIVE::CanDeleteProjectPage(
                m_frame->Schematic(), m_frame->GetCurrentSheet() ) );
        std::vector<wxString> labels;
        std::vector<KIID_PATH> paths;
        for( const auto& sheet : sheets )
        {
            wxString name = sheet.size() == 1 ? sheet.Last()->GetShownName( false )
                                               : sheet.PathHumanReadable( false, true );
            if( name.IsEmpty() )
                name = wxFileName( sheet.LastScreen()->GetFileName() ).GetName();
            labels.push_back( wxString::Format( _( "%s (page %s)" ), name, sheet.GetPageNumber() ) );
            paths.push_back( sheet.Path() );
        }
        if( paths != m_paths || labels != m_labels )
        {
            for( int id : m_ids )
            {
                Unbind( wxEVT_MENU, &PAGES_MENU::onPage, this, id );
                Destroy( id );
            }
            m_ids.clear();
            m_paths = std::move( paths );
            m_labels = std::move( labels );
            for( const auto& label : m_labels )
            {
                const int id = wxWindow::NewControlId();
                m_ids.push_back( id );
                wxString escaped = label;
                escaped.Replace( "&", "&&" );
                AppendCheckItem( id, escaped );
                Bind( wxEVT_MENU, &PAGES_MENU::onPage, this, id );
            }
        }
        for( std::size_t i = 0; i < m_ids.size(); ++i )
            Check( m_ids[i], m_paths[i] == m_frame->GetCurrentSheet().Path() );
    }

private:
    void onPage( wxCommandEvent& aEvent )
    {
        const auto at = std::find( m_ids.begin(), m_ids.end(), aEvent.GetId() );
        if( at == m_ids.end() )
            return;
        const auto& path = m_paths[at - m_ids.begin()];
        for( auto sheet : m_frame->Schematic().Hierarchy() )
        {
            if( sheet.Path() == path )
            {
                m_frame->GetToolManager()->RunAction<SCH_SHEET_PATH*>(
                        SCH_ACTIONS::changeSheet, &sheet );
                return;
            }
        }
    }

    SCH_EDIT_FRAME* m_frame;
    int m_deleteId;
    std::vector<int> m_ids;
    std::vector<KIID_PATH> m_paths;
    std::vector<wxString> m_labels;
};
}


void UpdatePagesMenu( SCH_EDIT_FRAME& aFrame )
{
    if( wxMenuBar* bar = aFrame.GetMenuBar() )
    {
        for( std::size_t i = 0; i < bar->GetMenuCount(); ++i )
        {
            if( auto* pages = dynamic_cast<PAGES_MENU*>( bar->GetMenu( i ) ) )
                pages->UpdateAll();
        }
    }
}


void SCH_EDIT_FRAME::NewPage()
{
    wxTextEntryDialog dlg( this, _( "Page name:" ), _( "New Page" ), _( "Untitled" ) );
    if( dlg.ShowModal() != wxID_OK )
        return;
    wxString name = dlg.GetValue().Trim().Trim( false );
    if( name.IsEmpty() )
        return;

    wxString stem = name.Lower();
    for( wxUniChar character : wxFileName::GetForbiddenChars() + "/\\" )
        stem.Replace( wxString( character ), "_" );
    stem.Replace( " ", "_" );
    if( stem.EndsWith( ".kicad_sch" ) )
        stem.RemoveLast( 10 );
    if( stem.IsEmpty() || stem == "." || stem == ".." )
        stem = "page";

    const wxString directory = Prj().GetProjectPath();
    wxFileName file( directory, stem + ".kicad_sch" );
    const auto hierarchy = Schematic().Hierarchy();
    auto used = [&]()
    {
        if( file.FileExists() )
            return true;
        for( const auto& path : hierarchy )
        {
            wxFileName existing( path.LastScreen()->GetFileName() );
            existing.MakeAbsolute( directory );
            if( existing.SameAs( file ) )
                return true;
        }
        return false;
    };
    for( int suffix = 2; used(); ++suffix )
        file.SetFullName( wxString::Format( "%s_%d.kicad_sch", stem, suffix ) );

    auto* sheet = new SCH_SHEET( &Schematic() );
    auto* screen = new SCH_SCREEN( &Schematic() );
    const_cast<KIID&>( sheet->m_Uuid ) = screen->GetUuid();
    sheet->SetScreen( screen );
    sheet->SetName( name );
    sheet->SetFileName( file.GetFullName() );
    screen->SetFileName( file.GetFullPath() );
    screen->SetContentModified();

    // Append after existing pages, including projects with gaps in their numbering.
    long lastPage = 0;
    for( const auto& path : hierarchy )
    {
        long number = 0;
        if( path.GetPageNumber().ToLong( &number ) )
            lastPage = std::max( lastPage, number );
    }
    SCH_SHEET_PATH path;
    path.push_back( sheet );
    path.SetPageNumber( wxString::Format( "%ld", lastPage + 1 ) );
    Schematic().AddTopLevelSheet( sheet );

    SCH_COMMIT dummy( this );
    RecalculateConnections( &dummy, NO_CLEANUP );
    UpdateHierarchyNavigator();
    m_toolManager->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &path );
    OnModify();
}


void SCH_EDIT_FRAME::EditPage()
{
    auto pages = Schematic().Hierarchy();
    const auto current = std::find( pages.begin(), pages.end(), GetCurrentSheet() );
    if( current == pages.end() )
        return;
    const int oldPosition = current - pages.begin();
    PAGE_DIALOG dlg( this, wxID_ANY, _( "Edit Page" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    dlg.OptOut( &dlg );
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* fields = new wxFlexGridSizer( 2, 8, 12 );
    fields->AddGrowableCol( 1 );
    auto* name = new wxTextCtrl( &dlg, wxID_ANY, current->Last()->GetName() );
    auto* order = new wxSpinCtrl( &dlg, wxID_ANY );
    order->SetRange( 1, pages.size() );
    order->SetValue( oldPosition + 1 );
    fields->Add( new wxStaticText( &dlg, wxID_ANY, _( "Page name:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    fields->Add( name, 1, wxEXPAND );
    fields->Add( new wxStaticText( &dlg, wxID_ANY, _( "Page order:" ) ), 0, wxALIGN_CENTER_VERTICAL );
    fields->Add( order, 1, wxEXPAND );
    layout->Add( fields, 1, wxEXPAND | wxALL, 12 );
    layout->Add( dlg.CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 12 );
    dlg.SetSizerAndFit( layout );
    dlg.finishDialogSettings();
    dlg.SetMinSize( dlg.FromDIP( wxSize( 360, -1 ) ) );
    dlg.SetSize( dlg.FromDIP( 360 ), dlg.GetSize().y );
    dlg.SetInitialFocus( name );
    if( dlg.ShowModal() != wxID_OK )
        return;
    const wxString newName = name->GetValue().Trim().Trim( false );
    if( newName.IsEmpty() )
        return;

    SCH_COMMIT commit( this );
    SCH_SHEET_PATH selected = *current;
    auto modifyPage = [&]( const SCH_SHEET_PATH& page )
    {
        SCH_SHEET_PATH parent = page;
        parent.pop_back();
        // Snapshot the owning sheet through its name field. Passing a top-level sheet
        // directly would promote the undo item to the virtual root instead.
        commit.Modify( page.Last()->GetField( FIELD_T::SHEET_NAME ),
                       parent.empty() ? Schematic().Root().GetScreen() : parent.LastScreen() );
        page.LastScreen()->SetContentModified();
    };
    if( newName != selected.Last()->GetName() )
    {
        modifyPage( selected );
        selected.Last()->SetName( newName );
    }
    if( order->GetValue() != oldPosition + 1 )
    {
        pages.erase( pages.begin() + oldPosition );
        pages.insert( pages.begin() + order->GetValue() - 1, selected );
        for( std::size_t i = 0; i < pages.size(); ++i )
        {
            auto& page = pages[i];
            modifyPage( page );
            page.SetPageNumber( wxString::Format( "%zu", i + 1 ) );
        }
    }
    if( !commit.Empty() )
        commit.Push( _( "Edit page" ) );
    Schematic().RefreshHierarchy();
    UpdateHierarchyNavigator();
    OnPageSettingsChange();
}


void SCH_EDIT_FRAME::doReCreateMenuBar()
{
    SCH_SELECTION_TOOL* selTool = m_toolManager->GetTool<SCH_SELECTION_TOOL>();

    // wxWidgets handles the Mac Application menu behind the scenes, but that means
    // we always have to start from scratch with a new wxMenuBar.
    wxMenuBar*  oldMenuBar = GetMenuBar();
    WX_MENUBAR* menuBar    = new WX_MENUBAR();

    //-- File menu -----------------------------------------------------------
    //
    ACTION_MENU*   fileMenu = new ACTION_MENU( false, selTool );
    static ACTION_MENU* openRecentMenu;

    if( Kiface().IsSingle() )   // When not under a project mgr
    {
        FILE_HISTORY& fileHistory = GetFileHistory();

        // Add this menu to the list of menus managed by the file history
        // (the file history will be updated when adding/removing files in history)
        if( !openRecentMenu )
        {
            openRecentMenu = new ACTION_MENU( false, selTool );
            openRecentMenu->SetIcon( BITMAPS::recent );

            fileHistory.UseMenu( openRecentMenu );
            fileHistory.AddFilesToMenu( openRecentMenu );
        }

        // Ensure the title is up to date after changing language
        openRecentMenu->SetTitle( _( "Open Recent" ) );
        fileHistory.UpdateClearText( openRecentMenu, _( "Clear Recent Files" ) );

        fileMenu->Add( ACTIONS::doNew );
        fileMenu->Add( ACTIONS::open );

        wxMenuItem* item = fileMenu->Add( openRecentMenu->Clone() );

        // Add the file menu condition here since it needs the item ID for the submenu
        ACTION_CONDITIONS cond;
        cond.Enable( FILE_HISTORY::FileHistoryNotEmpty( fileHistory ) );
        RegisterUIUpdateHandler( item->GetId(), cond );
        fileMenu->AppendSeparator();
    }

    fileMenu->Add( ACTIONS::save );

    if( Kiface().IsSingle() )
        fileMenu->Add( ACTIONS::saveAs );
    else
        fileMenu->Add( SCH_ACTIONS::saveCurrSheetCopyAs );

    fileMenu->Add( ACTIONS::revert );

    fileMenu->AppendSeparator();

    // Import submenu
    ACTION_MENU* submenuImport = new ACTION_MENU( false, selTool );
    submenuImport->SetTitle( _( "Import" ) );
    submenuImport->SetIcon( BITMAPS::import );

    submenuImport->Add( SCH_ACTIONS::importNonKicadSchematic, ACTION_MENU::NORMAL, _( "Non-CircuitAliveEDA Schematic..." ) );
    submenuImport->Add( SCH_ACTIONS::importFPAssignments, ACTION_MENU::NORMAL, _( "Footprint Assignments..." ) );
    submenuImport->Add( SCH_ACTIONS::importGraphics, ACTION_MENU::NORMAL, _( "Graphics..." ) );

    fileMenu->Add( submenuImport );


    // Export submenu
    ACTION_MENU* submenuExport = new ACTION_MENU( false, selTool );
    submenuExport->SetTitle( _( "Export" ) );
    submenuExport->SetIcon( BITMAPS::export_file );
    submenuExport->Add( SCH_ACTIONS::drawSheetOnClipboard,   ACTION_MENU::NORMAL, _( "Drawing to Clipboard" ) );
    submenuExport->Add( SCH_ACTIONS::exportNetlist,          ACTION_MENU::NORMAL, _( "Netlist..." ) );
    submenuExport->Add( SCH_ACTIONS::exportSymbolsToLibrary, ACTION_MENU::NORMAL, _( "Symbols..." ) );
    fileMenu->Add( submenuExport );

    fileMenu->AppendSeparator();
    fileMenu->Add( SCH_ACTIONS::schematicSetup );

    fileMenu->AppendSeparator();
    fileMenu->Add( ACTIONS::pageSettings );
    fileMenu->Add( ACTIONS::print );
    fileMenu->Add( ACTIONS::plot );

    fileMenu->AppendSeparator();
    fileMenu->AddQuitOrClose( &Kiface(), _( "Schematic Editor" ) );


    //-- Edit menu -----------------------------------------------------------
    //
    ACTION_MENU* editMenu = new ACTION_MENU( false, selTool );

    editMenu->Add( ACTIONS::undo );
    editMenu->Add( ACTIONS::redo );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::cut );
    editMenu->Add( ACTIONS::copy );
    editMenu->Add( ACTIONS::copyAsText );
    editMenu->Add( ACTIONS::paste );
    editMenu->Add( ACTIONS::pasteSpecial );
    editMenu->Add( ACTIONS::doDelete );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::selectAll );
    editMenu->Add( ACTIONS::unselectAll );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::find );
    editMenu->Add( ACTIONS::findAndReplace );

    editMenu->AppendSeparator();
    editMenu->Add( ACTIONS::deleteTool );
    editMenu->Add( SCH_ACTIONS::editTextAndGraphics );
    editMenu->Add( SCH_ACTIONS::changeSymbols );
    editMenu->Add( SCH_ACTIONS::editPageNumber );

    ACTION_MENU* submenuAttributes = new ACTION_MENU( false, selTool );
    submenuAttributes->SetTitle( _( "Attributes" ) );

    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromSim,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBOM,    ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromBoard,  ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setExcludeFromPosFiles, ACTION_MENU::CHECK );
    submenuAttributes->Add( SCH_ACTIONS::setDNP,               ACTION_MENU::CHECK );

    editMenu->Add( submenuAttributes );

    //-- View menu -----------------------------------------------------------
    //
    ACTION_MENU* viewMenu = new ACTION_MENU( false, selTool );

    // Show / Hide Panels submenu
    ACTION_MENU* showHidePanels = new ACTION_MENU( false, selTool );
    showHidePanels->SetTitle( _( "Panels" ) );

    showHidePanels->Add( ACTIONS::showProperties,    ACTION_MENU::CHECK );
    showHidePanels->Add( ACTIONS::showSearch,        ACTION_MENU::CHECK );
    showHidePanels->Add( SCH_ACTIONS::showHierarchy, ACTION_MENU::CHECK );
    showHidePanels->Add( CIRCUITALIVE::USED_SYMBOL_ACTIONS::showPanel, ACTION_MENU::CHECK );

    if( ADVANCED_CFG::GetCfg().m_IncrementalConnectivity )
        showHidePanels->Add( SCH_ACTIONS::showNetNavigator, ACTION_MENU::CHECK );

    showHidePanels->Add( SCH_ACTIONS::showDesignBlockPanel, ACTION_MENU::CHECK, _( "Design Blocks" ) );
    wxMenuItem* remoteSymbolItem = showHidePanels->Add( SCH_ACTIONS::showRemoteSymbolPanel, ACTION_MENU::CHECK, _( "Remote Symbols" ) );

    if( m_remoteSymbolPane && !m_remoteSymbolPane->HasDataSources() )
    {
        remoteSymbolItem->Enable( false );
        remoteSymbolItem->SetHelp( _( "Search signed-in remote symbol providers and download verified libraries." ) );
    }

    viewMenu->Add( showHidePanels );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::showSymbolBrowser );

    viewMenu->AppendSeparator();
    viewMenu->Add( ACTIONS::zoomInCenter );
    viewMenu->Add( ACTIONS::zoomOutCenter );
    viewMenu->Add( ACTIONS::zoomFitScreen );
    viewMenu->Add( ACTIONS::zoomFitObjects );
    viewMenu->Add( ACTIONS::zoomFitSelection );
    viewMenu->Add( ACTIONS::zoomTool );
    viewMenu->Add( ACTIONS::zoomRedraw );

    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::navigateBack );
    viewMenu->Add( SCH_ACTIONS::navigateUp );
    viewMenu->Add( SCH_ACTIONS::navigateForward );
    viewMenu->Add( SCH_ACTIONS::navigatePrevious );
    viewMenu->Add( SCH_ACTIONS::navigateNext );



    viewMenu->AppendSeparator();
    viewMenu->Add( SCH_ACTIONS::toggleHiddenPins,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleHiddenFields,    ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleDirectiveLabels, ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCErrors,       ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCWarnings,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleERCExclusions,   ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::markSimExclusions,     ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPVoltages,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::toggleOPCurrents,      ACTION_MENU::CHECK );
    viewMenu->Add( SCH_ACTIONS::togglePinAltIcons,     ACTION_MENU::CHECK );

#ifdef __APPLE__
    viewMenu->AppendSeparator();
#endif

    //-- Place menu -----------------------------------------------------------
    //
    ACTION_MENU* placeMenu = new ACTION_MENU( false, selTool );

    placeMenu->Add( SCH_ACTIONS::placeSymbol );
    placeMenu->Add( SCH_ACTIONS::placePower );
    placeMenu->Add( SCH_ACTIONS::drawWire );
    placeMenu->Add( SCH_ACTIONS::drawBus );
    placeMenu->Add( SCH_ACTIONS::placeBusWireEntry );
    placeMenu->Add( SCH_ACTIONS::placeNoConnect );
    placeMenu->Add( SCH_ACTIONS::placeJunction );
    placeMenu->Add( SCH_ACTIONS::placeLabel );
    placeMenu->Add( SCH_ACTIONS::placeGlobalLabel );
    placeMenu->Add( SCH_ACTIONS::placeClassLabel );
    placeMenu->Add( SCH_ACTIONS::drawRuleArea );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeHierLabel );
    placeMenu->Add( SCH_ACTIONS::drawSheet );
    placeMenu->Add( SCH_ACTIONS::placeSheetPin );
    placeMenu->Add( SCH_ACTIONS::syncAllSheetsPins );
    placeMenu->Add( SCH_ACTIONS::importSheet );

    placeMenu->AppendSeparator();
    placeMenu->Add( SCH_ACTIONS::placeSchematicText );
    placeMenu->Add( SCH_ACTIONS::drawTextBox );
    placeMenu->Add( SCH_ACTIONS::drawTable );
    placeMenu->Add( SCH_ACTIONS::drawRectangle );
    placeMenu->Add( SCH_ACTIONS::drawCircle );
    placeMenu->Add( SCH_ACTIONS::drawArc );
    placeMenu->Add( SCH_ACTIONS::drawBezier );
    placeMenu->Add( SCH_ACTIONS::drawLines );
    placeMenu->Add( SCH_ACTIONS::placeImage );


    //-- Inspect menu -----------------------------------------------
    //
    ACTION_MENU* inspectMenu = new ACTION_MENU( false, selTool );

    inspectMenu->Add( SCH_ACTIONS::showBusSyntaxHelp );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::runERC );
    inspectMenu->Add( ACTIONS::prevMarker );
    inspectMenu->Add( ACTIONS::nextMarker );
    inspectMenu->Add( ACTIONS::excludeMarker );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::diffSymbol );

    inspectMenu->AppendSeparator();
    inspectMenu->Add( SCH_ACTIONS::showSimulator );


    //-- Tools menu -----------------------------------------------
    //
    ACTION_MENU* toolsMenu = new ACTION_MENU( false, selTool );

    toolsMenu->Add( ACTIONS::updatePcbFromSchematic )->Enable( !Kiface().IsSingle() );
    toolsMenu->Add( SCH_ACTIONS::showPcbNew );

    if( !Kiface().IsSingle() )
        toolsMenu->Add( ACTIONS::showProjectManager );

    toolsMenu->Add( ACTIONS::showCalculatorTools );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::showSymbolEditor );
    toolsMenu->Add( SCH_ACTIONS::updateSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::rescueSymbols );
    toolsMenu->Add( SCH_ACTIONS::remapSymbols );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::editSymbolFields );
    toolsMenu->Add( SCH_ACTIONS::editSymbolLibraryLinks );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::annotate );
    toolsMenu->Add( SCH_ACTIONS::incrementAnnotations );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( SCH_ACTIONS::assignFootprints );
    toolsMenu->Add( SCH_ACTIONS::generateBOM );
    toolsMenu->Add( SCH_ACTIONS::generateBOMLegacy );

    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::updateSchematicFromPcb )->Enable( !Kiface().IsSingle() );

    toolsMenu->AppendSeparator();
    ACTION_MENU* submenuVariants = new ACTION_MENU( false, selTool );
    submenuVariants->SetTitle( _( "Variants" ) );
    submenuVariants->Add( SCH_ACTIONS::addVariant );
    submenuVariants->Add( SCH_ACTIONS::removeVariant );
    submenuVariants->Add( SCH_ACTIONS::editVariantDescription );
    toolsMenu->Add( submenuVariants );

#ifdef KICAD_IPC_API
    toolsMenu->AppendSeparator();
    toolsMenu->Add( ACTIONS::pluginsReload );
#endif

    //-- Preferences menu -----------------------------------------------
    //
    ACTION_MENU* prefsMenu = new ACTION_MENU( false, selTool );

    prefsMenu->Add( ACTIONS::configurePaths );
    prefsMenu->Add( ACTIONS::showSymbolLibTable );
    prefsMenu->Add( ACTIONS::showDesignBlockLibTable );
    prefsMenu->Add( ACTIONS::openPreferences );

    prefsMenu->AppendSeparator();
    AddMenuLanguageList( prefsMenu, selTool );


    //-- Menubar -------------------------------------------------------------
    //
    menuBar->Append( fileMenu,    _( "&File" ) );
    menuBar->Append( editMenu,    _( "&Edit" ) );
    menuBar->Append( viewMenu,    _( "&View" ) );
    menuBar->Append( placeMenu,   _( "&Place" ) );
    menuBar->Append( inspectMenu, _( "&Inspect" ) );
    menuBar->Append( toolsMenu,   _( "&Tools" ) );
    menuBar->Append( prefsMenu,   _( "P&references" ) );
    menuBar->Append( new PAGES_MENU( this, selTool ), _( "Pa&ges" ) );
    AddStandardHelpMenu( menuBar );

    SetMenuBar( menuBar );
    delete oldMenuBar;
}
