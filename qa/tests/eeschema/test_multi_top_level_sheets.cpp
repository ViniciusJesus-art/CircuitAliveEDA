/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file test_multi_top_level_sheets.cpp
 * Test suite for multiple top-level sheet functionality
 */

#include <boost/test/unit_test.hpp>

#include <sch_sheet.h>
#include <sch_screen.h>
#include <schematic.h>
#include <project/project_file.h>
#include <settings/settings_manager.h>
#include <pgm_base.h>
#include <eeschema_helpers.h>
#include <widgets/used_symbols.h>
#include <widgets/project_pages.h>
#include <sch_symbol.h>
#include <sch_label.h>
#include <lib_symbol.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <wx/ffile.h>
#include <wx/filename.h>

static SCH_SHEET* createTopLevelSheet( SCHEMATIC& aSchematic, const wxString& aName,
                                       const wxString& aFileName )
{
    SCH_SHEET*  sheet = new SCH_SHEET( &aSchematic );
    SCH_SCREEN* screen = new SCH_SCREEN( &aSchematic );

    const_cast<KIID&>( sheet->m_Uuid ) = screen->GetUuid();
    sheet->SetScreen( screen );
    sheet->SetName( aName );
    sheet->SetFileName( aFileName );

    return sheet;
}

BOOST_AUTO_TEST_SUITE( MultiTopLevelSheets )

struct PROJECT_PAGES_FIXTURE
{
    PROJECT_PAGES_FIXTURE()
    {
        directory = wxFileName::CreateTempFileName( wxFileName::GetTempDir() + "/ca-pages-" );
        wxRemoveFile( directory );
        wxFileName::Mkdir( directory );
        project = wxFileName( directory, "project.kicad_pro" ).GetFullPath();
        wxFFile file( project, "w" );
        file.Write( "{}" );
        file.Close();
        manager.LoadProject( project );
        schematic = std::make_unique<SCHEMATIC>( &manager.Prj() );
        first = createTopLevelSheet( *schematic, "First", "first.kicad_sch" );
        second = createTopLevelSheet( *schematic, "Second", "second.kicad_sch" );
        first->GetScreen()->SetFileName( Path( "first.kicad_sch" ) );
        second->GetScreen()->SetFileName( Path( "second.kicad_sch" ) );
        schematic->SetTopLevelSheets( { first, second } );
        SCH_SHEET_PATH a, b;
        a.push_back( first );
        b.push_back( second );
        a.SetPageNumber( "1" );
        b.SetPageNumber( "2" );
        schematic->RefreshHierarchy();
    }

    ~PROJECT_PAGES_FIXTURE()
    {
        schematic.reset();
        manager.UnloadProject( &manager.Prj(), false );
        wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
    }

    wxString Path( const wxString& name ) const
    {
        return wxFileName( directory, name ).GetFullPath();
    }

    void Save()
    {
        SCH_IO_KICAD_SEXPR plugin;
        for( const auto& path : schematic->Hierarchy() )
            plugin.SaveSchematicFile( path.LastScreen()->GetFileName(), path.Last(), schematic.get() );
    }

    void Open( const wxString& name )
    {
        schematic.reset();
        schematic = std::make_unique<SCHEMATIC>( &manager.Prj() );
        SCH_IO_KICAD_SEXPR plugin;
        renumbered = EESCHEMA_HELPERS::LoadProjectSheets( plugin, *schematic, Path( name ) );
    }

    wxString directory, project;
    SETTINGS_MANAGER manager;
    std::unique_ptr<SCHEMATIC> schematic;
    SCH_SHEET* first;
    SCH_SHEET* second;
    bool renumbered = false;
};

BOOST_FIXTURE_TEST_CASE( OpenAnyPageDiscoversSiblingsAndRetainsOrder, PROJECT_PAGES_FIXTURE )
{
    Save();
    wxCopyFile( Path( "first.kicad_sch" ), Path( "_autosave-ignored.kicad_sch" ) );
    Open( "second.kicad_sch" );
    BOOST_REQUIRE_EQUAL( schematic->GetTopLevelSheets().size(), 2u );
    BOOST_CHECK_EQUAL( schematic->CurrentSheet().LastScreen()->GetFileName(), Path( "second.kicad_sch" ) );
    auto pages = schematic->Hierarchy();
    BOOST_CHECK_EQUAL( pages[0].LastScreen()->GetFileName(), Path( "first.kicad_sch" ) );
    BOOST_CHECK_EQUAL( pages[1].LastScreen()->GetFileName(), Path( "second.kicad_sch" ) );
    pages[0].SetPageNumber( "2" );
    pages[1].SetPageNumber( "1" );
    Save();
    Open( "first.kicad_sch" );
    BOOST_REQUIRE_EQUAL( schematic->Hierarchy().size(), 2u );
    BOOST_CHECK_EQUAL( schematic->Hierarchy()[0].LastScreen()->GetFileName(), Path( "second.kicad_sch" ) );
    BOOST_CHECK_EQUAL( schematic->CurrentSheet().LastScreen()->GetFileName(), Path( "first.kicad_sch" ) );
}

BOOST_FIXTURE_TEST_CASE( DuplicateStandalonePageNumbersFollowProjectOrder, PROJECT_PAGES_FIXTURE )
{
    auto pages = schematic->Hierarchy();
    pages[1].SetPageNumber( "1" );
    manager.Prj().GetProjectFile().GetTopLevelSheets() = {
        { second->m_Uuid, "Second", "second.kicad_sch" },
        { first->m_Uuid, "First", "first.kicad_sch" }
    };
    Save();
    Open( "first.kicad_sch" );
    BOOST_REQUIRE_EQUAL( schematic->Hierarchy().size(), 2u );
    BOOST_CHECK_EQUAL( schematic->Hierarchy()[0].Last()->GetName(), "Second" );
    BOOST_CHECK_EQUAL( schematic->Hierarchy()[0].GetPageNumber(), "1" );
    BOOST_CHECK_EQUAL( schematic->Hierarchy()[1].GetPageNumber(), "2" );
    BOOST_CHECK( renumbered );
    BOOST_CHECK( !schematic->Hierarchy().IsModified() );
}

BOOST_FIXTURE_TEST_CASE( IgnoreNonSchematicProjectEntries, PROJECT_PAGES_FIXTURE )
{
    Save();
    for( const auto& name : { "notes.txt", "board.kicad_pcb", "library.kicad_sym", "legacy.sch" } )
    {
        wxFFile file( Path( name ), "w" );
        file.Write( "This is not a schematic in the selected format." );
        manager.Prj().GetProjectFile().GetTopLevelSheets().emplace_back( niluuid, name, name );
    }
    Open( "second.kicad_sch" );
    BOOST_CHECK_EQUAL( schematic->GetTopLevelSheets().size(), 2u );
    BOOST_CHECK_EQUAL( schematic->CurrentSheet().LastScreen()->GetFileName(), Path( "second.kicad_sch" ) );
}

BOOST_FIXTURE_TEST_CASE( DeletedPageStaysRemovedAfterDiscovery, PROJECT_PAGES_FIXTURE )
{
    Save();
    SCH_SHEET_PATH deleted;
    deleted.push_back( second );
    schematic->SetCurrentSheet( deleted );
    BOOST_REQUIRE( CIRCUITALIVE::RemoveProjectPage( *schematic, deleted ) );
    BOOST_CHECK( schematic->CurrentSheet().Last() == first );
    BOOST_CHECK( wxFileExists( Path( "second.kicad_sch" ) ) );
    BOOST_CHECK( CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "second.kicad_sch" ) ) );
    manager.SaveProject();
    schematic.reset();
    manager.UnloadProject( &manager.Prj(), false );
    manager.LoadProject( project );
    Open( "first.kicad_sch" );
    BOOST_REQUIRE_EQUAL( schematic->GetTopLevelSheets().size(), 1u );
    BOOST_CHECK( !CIRCUITALIVE::CanDeleteProjectPage( *schematic, schematic->CurrentSheet() ) );
    BOOST_CHECK( !CIRCUITALIVE::RemoveProjectPage( *schematic, schematic->CurrentSheet() ) );
    // Opening a removed file explicitly restores it instead of showing an unrelated page.
    Open( "second.kicad_sch" );
    BOOST_CHECK_EQUAL( schematic->GetTopLevelSheets().size(), 2u );
    BOOST_CHECK( !CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "second.kicad_sch" ) ) );
}

BOOST_FIXTURE_TEST_CASE( DeleteHierarchicalPageRetargetsParentAndExcludesFile, PROJECT_PAGES_FIXTURE )
{
    auto* child = createTopLevelSheet( *schematic, "Child", "child.kicad_sch" );
    child->GetScreen()->SetFileName( Path( "child.kicad_sch" ) );
    child->SetParent( first );
    first->GetScreen()->Append( child );
    SCH_SHEET_PATH childPath;
    childPath.push_back( first );
    childPath.push_back( child );
    childPath.SetPageNumber( "3" );
    schematic->RefreshHierarchy();
    Save();
    schematic->SetCurrentSheet( childPath );
    BOOST_REQUIRE( CIRCUITALIVE::RemoveProjectPage( *schematic, childPath ) );
    BOOST_CHECK( schematic->CurrentSheet().Last() == first );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 2u );
    BOOST_CHECK( CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "child.kicad_sch" ) ) );
    Save();
    Open( "second.kicad_sch" );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 2u );
}

BOOST_FIXTURE_TEST_CASE( DeleteFirstPageKeepsSharedChildInSurvivingHierarchy, PROJECT_PAGES_FIXTURE )
{
    auto* child = createTopLevelSheet( *schematic, "Child", "child.kicad_sch" );
    child->GetScreen()->SetFileName( Path( "child.kicad_sch" ) );
    child->SetParent( first );
    first->GetScreen()->Append( child );
    auto* shared = new SCH_SHEET( second );
    shared->SetName( "Shared" );
    shared->SetFileName( "child.kicad_sch" );
    shared->SetScreen( child->GetScreen() );
    second->GetScreen()->Append( shared );
    schematic->RefreshHierarchy();
    SCH_SHEET_PATH removed;
    removed.push_back( first );
    schematic->SetCurrentSheet( removed );
    BOOST_REQUIRE( CIRCUITALIVE::RemoveProjectPage( *schematic, removed ) );
    BOOST_CHECK( schematic->CurrentSheet().Last() == second );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 2u );
    BOOST_CHECK( CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "first.kicad_sch" ) ) );
    BOOST_CHECK( !CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "child.kicad_sch" ) ) );
}

BOOST_FIXTURE_TEST_CASE( UsedComponentsReflectCurrentPageAndType, PROJECT_PAGES_FIXTURE )
{
    SCH_SHEET_PATH path;
    path.push_back( first );
    LIB_SYMBOL lib( "NE555" );
    lib.GetReferenceField().SetText( "U" );
    lib.SetUnitCount( 2, false );
    auto* source = new SCH_SYMBOL( lib, LIB_ID( "Timer", "NE555" ), &path, 1,
                                   0, VECTOR2I( 0, 0 ), schematic.get() );
    source->SetRef( &path, "U1" );
    source->GetField( FIELD_T::VALUE )->SetText( "NE555" );
    first->GetScreen()->Append( source );
    auto copy = CIRCUITALIVE::CopyUsedSymbol( *source, path, VECTOR2I( 10000, 20000 ) );
    BOOST_CHECK( copy->m_Uuid != source->m_Uuid );
    BOOST_CHECK_EQUAL( copy->GetRef( &path ), "U?" );
    BOOST_CHECK_EQUAL( source->GetRef( &path ), "U1" );
    BOOST_CHECK( copy->GetPosition() == VECTOR2I( 10000, 20000 ) );
    copy->SetRef( &path, "U2" );
    first->GetScreen()->Append( copy.release() );
    auto configured = CIRCUITALIVE::CopyUsedSymbol( *source, path, VECTOR2I( 30000, 40000 ) );
    configured->GetField( FIELD_T::VALUE )->SetText( "LM555" );
    configured->GetField( FIELD_T::FOOTPRINT )->SetText( "Package_DIP:DIP-8_W7.62mm" );
    configured->SetRef( &path, "U3" );
    configured->SetUnit( 2 );
    SCH_FIELD model( configured.get(), FIELD_T::USER, "Sim.Model" );
    model.SetText( "different_model" );
    configured->AddField( model );
    first->GetScreen()->Append( configured.release() );
    first->GetScreen()->Append( new SCH_LABEL( VECTOR2I( 0, 0 ), "SUPPLY" ) );
    auto* ground = new SCH_SYMBOL();
    auto* groundLib = new LIB_SYMBOL( "GND" );
    groundLib->SetGlobalPower();
    ground->SetLibSymbol( groundLib );
    first->GetScreen()->Append( ground );
    auto* probe = new SCH_SYMBOL();
    SCH_FIELD probeField( probe, FIELD_T::USER, "CA.Probe" );
    probe->AddField( probeField );
    first->GetScreen()->Append( probe );

    auto entries = CIRCUITALIVE::BuildUsedSymbolList( path );
    BOOST_REQUIRE_EQUAL( entries.size(), 1u );
    BOOST_CHECK_EQUAL( entries[0].label, "Timer:NE555" );
    BOOST_CHECK( entries[0].symbol == source->m_Uuid );
    SCH_SHEET_PATH other;
    other.push_back( second );
    BOOST_CHECK( CIRCUITALIVE::BuildUsedSymbolList( other ).empty() );
    source->GetField( FIELD_T::VALUE )->SetText( "LM555" );
    entries = CIRCUITALIVE::BuildUsedSymbolList( path );
    BOOST_REQUIRE_EQUAL( entries.size(), 1u );
    BOOST_CHECK_EQUAL( entries[0].label, "Timer:NE555" );
    first->GetScreen()->Remove( source );
    delete source;
    entries = CIRCUITALIVE::BuildUsedSymbolList( path );
    BOOST_REQUIRE_EQUAL( entries.size(), 1u );
    BOOST_CHECK_EQUAL( entries[0].label, "Timer:NE555" );

    LIB_SYMBOL resistor( "R" );
    auto* r1 = new SCH_SYMBOL( resistor, LIB_ID( "Device", "R" ), &path, 1,
                              0, VECTOR2I( 0, 0 ), schematic.get() );
    r1->GetField( FIELD_T::VALUE )->SetText( "10k" );
    first->GetScreen()->Append( r1 );
    auto r2 = CIRCUITALIVE::CopyUsedSymbol( *r1, path, VECTOR2I( 10000, 0 ) );
    r2->GetField( FIELD_T::VALUE )->SetText( "330" );
    first->GetScreen()->Append( r2.release() );
    entries = CIRCUITALIVE::BuildUsedSymbolList( path );
    BOOST_REQUIRE_EQUAL( entries.size(), 2u );
    BOOST_CHECK_EQUAL( entries[0].label, "Device:R" );
    BOOST_CHECK_EQUAL( entries[1].label, "Timer:NE555" );
}

BOOST_FIXTURE_TEST_CASE( DiscoverPagesKeepsHierarchicalChildrenAsSingleInstances, PROJECT_PAGES_FIXTURE )
{
    auto* child = createTopLevelSheet( *schematic, "Child", "child.kicad_sch" );
    child->GetScreen()->SetFileName( Path( "child.kicad_sch" ) );
    child->SetParent( first );
    first->GetScreen()->Append( child );
    SCH_SHEET_PATH childPath;
    childPath.push_back( first );
    childPath.push_back( child );
    childPath.SetPageNumber( "3" );
    schematic->RefreshHierarchy();
    Save();
    // Discovery also runs when the child file itself is the file opened by the user.
    Open( "child.kicad_sch" );
    BOOST_REQUIRE_EQUAL( schematic->GetTopLevelSheets().size(), 2u );
    BOOST_REQUIRE_EQUAL( schematic->Hierarchy().size(), 3u );
    BOOST_CHECK_EQUAL( schematic->CurrentSheet().size(), 2u );
    BOOST_CHECK_EQUAL( schematic->CurrentSheet().LastScreen()->GetFileName(), Path( "child.kicad_sch" ) );
}

BOOST_FIXTURE_TEST_CASE( DirectPageFindsOwningProjectWithDifferentBasename, PROJECT_PAGES_FIXTURE )
{
    Save();
    wxFFile file( project, "w" );
    file.Write( R"({"schematic":{"top_level_sheets":[{"filename":"second.kicad_sch"}]}})" );
    file.Close();
    // Another project in the folder must not hide an explicit page membership.
    wxFFile other( Path( "other.kicad_pro" ), "w" );
    other.Write( "{}" );
    other.Close();
    BOOST_CHECK_EQUAL( EESCHEMA_HELPERS::FindProjectForSchematic( Path( "second.kicad_sch" ) ), project );
}

BOOST_AUTO_TEST_CASE( TestVirtualRootCreation )
{
    // Create a virtual root
    SCH_SHEET virtualRoot;
    const_cast<KIID&>( virtualRoot.m_Uuid ) = niluuid;
    virtualRoot.SetScreen( nullptr );

    // Verify it's recognized as virtual root
    BOOST_CHECK( virtualRoot.m_Uuid == niluuid );
    BOOST_CHECK( virtualRoot.GetScreen() == nullptr );
}

BOOST_AUTO_TEST_CASE( TestAddTopLevelSheet )
{
    SCHEMATIC schematic( nullptr );

    SCH_SHEET* sheet1 = createTopLevelSheet( schematic, "Sheet1", "sheet1.kicad_sch" );

    schematic.SetTopLevelSheets( { sheet1 } );

    // Verify the sheet was added
    const std::vector<SCH_SHEET*>& topSheets = schematic.GetTopLevelSheets();
    BOOST_CHECK_EQUAL( topSheets.size(), 1 );
    BOOST_CHECK_EQUAL( topSheets[0], sheet1 );
    BOOST_CHECK_EQUAL( topSheets[0]->GetName(), "Sheet1" );
    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 1 );
}

BOOST_AUTO_TEST_CASE( TestAddMultipleTopLevelSheets )
{
    SCHEMATIC schematic( nullptr );

    SCH_SHEET* sheet1 = createTopLevelSheet( schematic, "Sheet1", "sheet1.kicad_sch" );
    schematic.SetTopLevelSheets( { sheet1 } );

    // Create and add multiple top-level sheets
    for( int i = 2; i <= 3; i++ )
    {
        SCH_SHEET* sheet = createTopLevelSheet( schematic, wxString::Format( "Sheet%d", i ),
                                                wxString::Format( "sheet%d.kicad_sch", i ) );
        schematic.AddTopLevelSheet( sheet );
    }

    // Verify all sheets were added
    const std::vector<SCH_SHEET*>& topSheets = schematic.GetTopLevelSheets();
    BOOST_CHECK_EQUAL( topSheets.size(), 3 );
    BOOST_CHECK_EQUAL( topSheets[0]->GetName(), "Sheet1" );
    BOOST_CHECK_EQUAL( topSheets[1]->GetName(), "Sheet2" );
    BOOST_CHECK_EQUAL( topSheets[2]->GetName(), "Sheet3" );
    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 3 );
}

BOOST_AUTO_TEST_CASE( TestRemoveTopLevelSheet )
{
    SCHEMATIC schematic( nullptr );

    SCH_SHEET* sheet1 = createTopLevelSheet( schematic, "Sheet1", "sheet1.kicad_sch" );
    schematic.SetTopLevelSheets( { sheet1 } );

    SCH_SHEET* sheet2 = createTopLevelSheet( schematic, "Sheet2", "sheet2.kicad_sch" );
    schematic.AddTopLevelSheet( sheet2 );

    // Remove first sheet
    schematic.RemoveTopLevelSheet( sheet1 );

    // Verify only sheet2 remains
    const std::vector<SCH_SHEET*>& topSheets = schematic.GetTopLevelSheets();
    BOOST_CHECK_EQUAL( topSheets.size(), 1 );
    BOOST_CHECK_EQUAL( topSheets[0], sheet2 );
    BOOST_CHECK_EQUAL( topSheets[0]->GetName(), "Sheet2" );
    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 1 );

    // Cannot remove the final remaining sheet
    BOOST_CHECK( !schematic.RemoveTopLevelSheet( topSheets[0] ) );
}

BOOST_AUTO_TEST_CASE( TestBuildSheetListWithMultipleRoots )
{
    SCHEMATIC schematic( nullptr );

    SCH_SHEET* top1 = createTopLevelSheet( schematic, "TopSheet1", "top_sheet_1.kicad_sch" );
    SCH_SHEET* top2 = createTopLevelSheet( schematic, "TopSheet2", "top_sheet_2.kicad_sch" );

    SCH_SHEET* child1 = createTopLevelSheet( schematic, "ChildSheet1", "child_sheet_1.kicad_sch" );
    child1->SetParent( top1 );
    top1->GetScreen()->Append( child1 );

    SCH_SHEET* child2 = createTopLevelSheet( schematic, "ChildSheet2", "child_sheet_2.kicad_sch" );
    child2->SetParent( top2 );
    top2->GetScreen()->Append( child2 );

    schematic.SetTopLevelSheets( { top1, top2 } );

    // Build sheet list
    SCH_SHEET_LIST sheetList = schematic.Hierarchy();

    // Should have 4 sheet paths: 2 top-level + 2 children
    BOOST_CHECK_EQUAL( sheetList.size(), 4 );
}


BOOST_AUTO_TEST_CASE( TestHierarchyUpdatesOnSheetOperations )
{
    SCHEMATIC schematic( nullptr );

    SCH_SHEET* baseSheet = createTopLevelSheet( schematic, "Base", "base.kicad_sch" );
    schematic.SetTopLevelSheets( { baseSheet } );

    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 1 );

    SCH_SHEET* copiedSheet = createTopLevelSheet( schematic, "Copy", "copy.kicad_sch" );
    schematic.AddTopLevelSheet( copiedSheet );

    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 2 );

    // Move the copied sheet ahead of the base sheet
    schematic.SetTopLevelSheets( { copiedSheet, baseSheet } );
    BOOST_CHECK_EQUAL( schematic.GetTopLevelSheet()->GetName(), "Copy" );
    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 2 );

    // Removing one sheet should keep hierarchy valid
    BOOST_CHECK( schematic.RemoveTopLevelSheet( baseSheet ) );
    BOOST_CHECK_EQUAL( schematic.Hierarchy().size(), 1 );
    BOOST_CHECK_EQUAL( schematic.GetTopLevelSheet()->GetName(), "Copy" );
}

BOOST_AUTO_TEST_CASE( TestTopLevelSheetInfoSerialization )
{
    TOP_LEVEL_SHEET_INFO info1( KIID(), "TestSheet", "test_sheet.kicad_sch" );

    // Verify members
    BOOST_CHECK_EQUAL( info1.name, "TestSheet" );
    BOOST_CHECK_EQUAL( info1.filename, "test_sheet.kicad_sch" );

    // Test equality
    TOP_LEVEL_SHEET_INFO info2( info1.uuid, "TestSheet", "test_sheet.kicad_sch" );
    BOOST_CHECK( info1 == info2 );

    // Test inequality
    TOP_LEVEL_SHEET_INFO info3( KIID(), "OtherSheet", "other_sheet.kicad_sch" );
    BOOST_CHECK( info1 != info3 );
}

BOOST_AUTO_TEST_SUITE_END()
