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
#include <sch_shape.h>
#include <sch_label.h>
#include <sch_group.h>
#include <sch_sheet_pin.h>
#include <sch_table.h>
#include <sch_tablecell.h>
#include <lib_symbol.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <netlist_exporters/netlist_exporter_kicad.h>
#include <netlist_reader/netlist.h>
#include <netlist_reader/netlist_reader.h>
#include <richio.h>
#include <sch_pin.h>
#include <connection_graph.h>
#include <circuitalive/probe_placement.h>
#include <circuitalive/probe_symbol.h>
#include <circuitalive/probe_geometry.h>
#include <sch_line.h>

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
    BOOST_REQUIRE( CIRCUITALIVE::RemoveProjectPage( *schematic, childPath, true ) );
    BOOST_CHECK( schematic->CurrentSheet().Last() == first );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 2u );
    BOOST_CHECK( CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "child.kicad_sch" ) ) );
    BOOST_CHECK( !wxFileExists( Path( "child.kicad_sch" ) ) );
    Save();
    Open( "second.kicad_sch" );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 2u );
}

BOOST_FIXTURE_TEST_CASE( ConfirmedPageDeletionRemovesItsSchematicFile, PROJECT_PAGES_FIXTURE )
{
    Save();
    SCH_SHEET_PATH removed;
    removed.push_back( second );
    BOOST_REQUIRE( CIRCUITALIVE::RemoveProjectPage( *schematic, removed, true ) );
    BOOST_CHECK( !wxFileExists( Path( "second.kicad_sch" ) ) );
    BOOST_CHECK( wxFileExists( Path( "first.kicad_sch" ) ) );
    Save();
    Open( "first.kicad_sch" );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 1u );
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
    Save();
    SCH_SHEET_PATH removed;
    removed.push_back( first );
    schematic->SetCurrentSheet( removed );
    BOOST_REQUIRE( CIRCUITALIVE::RemoveProjectPage( *schematic, removed, true ) );
    BOOST_CHECK( schematic->CurrentSheet().Last() == second );
    BOOST_CHECK_EQUAL( schematic->Hierarchy().size(), 2u );
    BOOST_CHECK( CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "first.kicad_sch" ) ) );
    BOOST_CHECK( !CIRCUITALIVE::IsExcludedProjectPage( manager.Prj(), Path( "child.kicad_sch" ) ) );
    BOOST_CHECK( !wxFileExists( Path( "first.kicad_sch" ) ) );
    BOOST_CHECK( wxFileExists( Path( "child.kicad_sch" ) ) );
    BOOST_CHECK( wxFileExists( Path( "second.kicad_sch" ) ) );
}

BOOST_FIXTURE_TEST_CASE( QuickListStartsWithLibraryDefaultsInsteadOfInstanceProperties, PROJECT_PAGES_FIXTURE )
{
    SCH_SHEET_PATH path;
    path.push_back( first );
    LIB_SYMBOL lib( "74LS139" );
    lib.GetReferenceField().SetText( "U" );
    lib.GetValueField().SetText( "74LS139" );
    lib.GetFootprintField().SetText( "Package_DIP:DIP-16_W7.62mm" );
    lib.SetUnitCount( 3, false );
    SCH_SYMBOL placed( lib, LIB_ID( "74xx", "74LS139" ), &path, 3,
                       0, VECTOR2I( 0, 0 ), schematic.get() );
    placed.SetRef( &path, "U12" );
    placed.GetField( FIELD_T::VALUE )->SetText( "changed value" );
    placed.GetField( FIELD_T::FOOTPRINT )->SetText( "changed footprint" );
    SCH_FIELD local( &placed, FIELD_T::USER, "LocalOnly" );
    local.SetText( "not a default" );
    placed.AddField( local );
    auto initial = CIRCUITALIVE::MakeUsedSymbolTemplate( placed, path );
    BOOST_REQUIRE( initial );
    BOOST_CHECK_EQUAL( initial->GetField( FIELD_T::VALUE )->GetText(), "74LS139" );
    BOOST_CHECK_EQUAL( initial->GetField( FIELD_T::FOOTPRINT )->GetText(), "Package_DIP:DIP-16_W7.62mm" );
    BOOST_CHECK( initial->GetField( "LocalOnly" ) == nullptr );
    BOOST_CHECK_EQUAL( initial->GetRef( &path ), "U?" );
    BOOST_CHECK_EQUAL( initial->GetUnit(), 1 );
    BOOST_CHECK_EQUAL( initial->GetUnitCount(), 3 );
    BOOST_CHECK_EQUAL( placed.GetRef( &path ), "U12" );

    // A remembered chooser template is independent of both the original occurrence
    // and every subsequent placement, while ordinary duplicate keeps instance values.
    initial->GetField( FIELD_T::FOOTPRINT )->SetText( "chooser footprint" );
    auto next = CIRCUITALIVE::CopyUsedSymbol( *initial, path, VECTOR2I( 10000, 20000 ) );
    next->GetField( FIELD_T::VALUE )->SetText( "per-instance edit" );
    auto another = CIRCUITALIVE::CopyUsedSymbol( *initial, path, VECTOR2I( 30000, 40000 ) );
    BOOST_CHECK_EQUAL( another->GetField( FIELD_T::VALUE )->GetText(), "74LS139" );
    BOOST_CHECK_EQUAL( another->GetField( FIELD_T::FOOTPRINT )->GetText(), "chooser footprint" );
    auto duplicate = CIRCUITALIVE::CopyUsedSymbol( placed, path, VECTOR2I( 0, 0 ) );
    BOOST_CHECK_EQUAL( duplicate->GetField( FIELD_T::VALUE )->GetText(), "changed value" );
}

#if defined( __linux__ ) || defined( _WIN32 )
BOOST_FIXTURE_TEST_CASE( ProbePlacementSupportsFloatingSymbolsWithRealTerminals, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path;
    path.push_back( first );
    const VECTOR2I position( schIUScale.mmToIU( 50 ), schIUScale.mmToIU( 50 ) );
    for( auto kind : { PROBE_KIND::VOLTAGE, PROBE_KIND::CURRENT, PROBE_KIND::POWER, PROBE_KIND::OSCILLOSCOPE,
                       PROBE_KIND::LOGIC, PROBE_KIND::PWM, PROBE_KIND::SINE, PROBE_KIND::SQUARE } )
    {
        auto plan = PlanProbePlacement( *first->GetScreen(), position, 1, kind, &path, true );
        BOOST_CHECK( !plan.wire && !plan.pin );
        BOOST_CHECK( plan.anchor == position );
        auto probe = MakeProbeSymbol( kind, path, plan.anchor, plan.vertical, 1, "test" );
        probe->SetParent( first->GetScreen() );
        const bool series = kind == PROBE_KIND::CURRENT || kind == PROBE_KIND::POWER;
        BOOST_CHECK_EQUAL( probe->GetPins( &path ).size(), series ? 2u : 1u );
        if( series )
        {
            BOOST_CHECK( plan.first != plan.second );
            for( auto* pin : probe->GetPins( &path ) )
                BOOST_CHECK( pin->GetPosition() == plan.first || pin->GetPosition() == plan.second );
        }
        else
            BOOST_CHECK( probe->GetPins( &path ).front()->GetPosition() == position );
    }
}

BOOST_FIXTURE_TEST_CASE( FloatingProbesRetainDirectWireAndSeriesSafetyRules, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path;
    path.push_back( first );
    const int mm = schIUScale.mmToIU( 1 );
    auto* wire = new SCH_LINE( VECTOR2I( 10 * mm, 20 * mm ), LAYER_WIRE );
    wire->SetEndPoint( VECTOR2I( 30 * mm, 20 * mm ) );
    first->GetScreen()->Append( wire );
    auto plan = PlanProbePlacement( *first->GetScreen(), VECTOR2I( 20 * mm, 20 * mm ), 1,
                                    PROBE_KIND::CURRENT, &path, true );
    BOOST_CHECK( plan.wire == wire );
    BOOST_CHECK( plan.first == plan.anchor - VECTOR2I( schIUScale.mmToIU( 1.27 ), 0 ) );
    BOOST_CHECK( plan.second == plan.anchor + VECTOR2I( schIUScale.mmToIU( 1.27 ), 0 ) );
    BOOST_CHECK_THROW( PlanProbePlacement( *first->GetScreen(), wire->GetStartPoint(), 1,
                       PROBE_KIND::CURRENT, &path, true ), std::runtime_error );
    auto floating = PlanProbePlacement( *first->GetScreen(), VECTOR2I( 20 * mm, 40 * mm ), 1,
                                        PROBE_KIND::CURRENT, &path, true );
    BOOST_CHECK( !floating.wire );
}

BOOST_FIXTURE_TEST_CASE( ProbePlacementPreservesRotationAndConnectsSeriesTerminalsToTheWire, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path;
    path.push_back( first );
    const int mm = schIUScale.mmToIU( 1 );
    const VECTOR2I position( 50 * mm, 50 * mm );
    for( auto kind : { PROBE_KIND::VOLTAGE, PROBE_KIND::CURRENT, PROBE_KIND::POWER, PROBE_KIND::OSCILLOSCOPE,
                       PROBE_KIND::LOGIC, PROBE_KIND::PWM, PROBE_KIND::SINE, PROBE_KIND::SQUARE } )
    for( int turns = 0; turns < 4; ++turns )
    {
        auto preview = MakeProbeSymbol( kind, path, position, false, 1, "clock" );
        for( int i = 0; i < turns; ++i ) preview->Rotate( position, true );
        const auto transform = preview->GetTransform();
        auto floating = PlanProbePlacement( *first->GetScreen(), position + VECTOR2I( 0, 10 * mm ),
                                            1, kind, &path, true );
        auto placed = std::make_unique<SCH_SYMBOL>( *preview );
        AlignProbeForPlacement( *placed, floating );
        BOOST_CHECK( placed->GetTransform() == transform );
        BOOST_CHECK( placed->GetPosition() == floating.anchor );
        for( auto* pin : placed->GetPins( &path ) )
            BOOST_CHECK( ( pin->GetPosition() - placed->GetPosition() ).EuclideanNorm()
                         == ( preview->GetPins( &path ).front()->GetPosition() - position ).EuclideanNorm() );
        if( kind != PROBE_KIND::CURRENT && kind != PROBE_KIND::POWER ) continue;
        for( bool vertical : { false, true } )
        {
            SCH_LINE wire( position, LAYER_WIRE );
            PROBE_PLACEMENT plan;
            plan.wire = &wire; plan.anchor = position; plan.vertical = vertical;
            const VECTOR2I delta = vertical ? VECTOR2I( 0, schIUScale.mmToIU( 1.27 ) )
                                            : VECTOR2I( schIUScale.mmToIU( 1.27 ), 0 );
            AlignProbeForPlacement( *placed, plan );
            const auto pins = placed->GetPins( &path );
            BOOST_REQUIRE_EQUAL( pins.size(), 2u );
            BOOST_CHECK( pins[0]->GetPosition() != pins[1]->GetPosition() );
            for( auto* pin : pins )
                BOOST_CHECK( pin->GetPosition() == position - delta || pin->GetPosition() == position + delta );
        }
    }
}

BOOST_FIXTURE_TEST_CASE( ProbesOnPinsPointAwayFromTheComponentAndFitAdjacentPinSpacing, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path; path.push_back( first );
    LIB_SYMBOL lib( "PinDirection" );
    auto* terminal = new SCH_PIN( &lib );
    terminal->SetNumber( "1" ); terminal->SetUnit( 1 ); terminal->SetBodyStyle( 1 );
    terminal->SetPosition( VECTOR2I( -schIUScale.mmToIU( 5.08 ), 0 ) );
    terminal->SetLength( schIUScale.mmToIU( 2.54 ) ); lib.AddDrawItem( terminal );
    SCH_SYMBOL component( lib, LIB_ID( "Test", "PinDirection" ), &path, 1, 1,
                          VECTOR2I( schIUScale.mmToIU( 40 ), schIUScale.mmToIU( 40 ) ), schematic.get() );
    for( int rotation : { SYM_ORIENT_0, SYM_ORIENT_90, SYM_ORIENT_180, SYM_ORIENT_270 } )
    for( int mirror : std::vector<int>{ 0, SYM_MIRROR_X, SYM_MIRROR_Y } )
    for( auto kind : { PROBE_KIND::LOGIC, PROBE_KIND::PWM, PROBE_KIND::SINE, PROBE_KIND::SQUARE,
                       PROBE_KIND::VOLTAGE, PROBE_KIND::OSCILLOSCOPE } )
    {
        component.SetOrientation( rotation + mirror );
        auto* pin = component.GetPins( &path ).front();
        PROBE_PLACEMENT plan; plan.pin = pin; plan.anchor = pin->GetPosition();
        auto probe = MakeProbeSymbol( kind, path, plan.anchor, false, 1, "" );
        AlignProbeForPlacement( *probe, plan );
        const auto outward = pin->GetPosition() - pin->GetPinRoot();
        const auto body = probe->GetBodyBoundingBox().GetCenter() - plan.anchor;
        BOOST_CHECK_GT( double( body.x ) * outward.x + double( body.y ) * outward.y, 0 );
        BOOST_CHECK( probe->GetPins( &path ).front()->GetPosition() == plan.anchor );
        if( !IsSourceProbe( kind ) ) continue;
        const auto box = probe->GetBodyBoundingBox();
        const auto textBox = probe->GetField( "CA.ProbeState" )->GetBoundingBox();
        const int pitch = schIUScale.mmToIU( 2.54 );
        BOOST_CHECK_LE( outward.x ? box.GetHeight() : box.GetWidth(), pitch );
        BOOST_CHECK_LE( outward.x ? textBox.GetHeight() : textBox.GetWidth(), pitch );
        BOOST_CHECK( box.Contains( probe->GetField( "CA.ProbeState" )->GetPosition() ) );
    }
}

BOOST_FIXTURE_TEST_CASE( LegacyProbeArtworkBecomesCompactWithoutChangingItsIdentityOrSettings, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path; path.push_back( first );
    SOURCE_PROBE_SETTINGS settings; settings.frequency = 123; settings.initialHigh = true;
    auto probe = MakeProbeSymbol( PROBE_KIND::LOGIC, path,
            VECTOR2I( schIUScale.mmToIU( 40 ), schIUScale.mmToIU( 40 ) ), false, 7, "debug", settings );
    probe->RemoveField( "CA.ProbeAppearance" );
    auto* oldBody = new SCH_SHAPE( SHAPE_T::RECTANGLE, LAYER_DEVICE );
    oldBody->SetStart( VECTOR2I( schIUScale.mmToIU( -5 ), schIUScale.mmToIU( -1.8 ) ) );
    oldBody->SetEnd( VECTOR2I( 0, schIUScale.mmToIU( 1.8 ) ) );
    oldBody->SetUnit( 1 ); oldBody->SetBodyStyle( 1 );
    probe->GetLibSymbolRef()->AddDrawItem( oldBody );
    probe->GetField( "CA.ProbeState" )->SetPosition( VECTOR2I( 0, 0 ) );
    const auto uuid = probe->m_Uuid;
    const auto json = probe->GetField( "CA.ProbeSettings" )->GetText();
    auto* placed = probe.release(); first->GetScreen()->Append( placed );
    BOOST_CHECK_GT( placed->GetBodyBoundingBox().GetHeight(), schIUScale.mmToIU( 3 ) );
    NormalizeSourceProbeSymbols( *schematic );
    BOOST_CHECK( placed->m_Uuid == uuid );
    BOOST_CHECK_EQUAL( placed->GetRef( &path ), "#P7" );
    BOOST_CHECK_EQUAL( placed->GetField( FIELD_T::VALUE )->GetText(), "debug" );
    BOOST_CHECK_EQUAL( placed->GetField( "CA.ProbeSettings" )->GetText(), json );
    BOOST_CHECK_LE( placed->GetBodyBoundingBox().GetHeight(), schIUScale.mmToIU( 2.54 ) );
    BOOST_CHECK( placed->GetBodyBoundingBox().Contains( placed->GetField( "CA.ProbeState" )->GetPosition() ) );
    BOOST_CHECK_EQUAL( placed->GetField( "CA.ProbeAppearance" )->GetText(), SOURCE_PROBE_APPEARANCE );
    NormalizeSourceProbeSymbols( *schematic );
    BOOST_CHECK_EQUAL( NextProbeSequence( *schematic ), 8u );
}

BOOST_FIXTURE_TEST_CASE( BatchCopiedProbesReceiveNewIdentitiesForEveryInstrumentKind, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path; path.push_back( first );
    std::set<SCH_SYMBOL*> copies;
    std::vector<SCH_SYMBOL*> originals;
    std::size_t index = 14;
    for( auto kind : { PROBE_KIND::LOGIC, PROBE_KIND::PWM, PROBE_KIND::SINE, PROBE_KIND::SQUARE,
                       PROBE_KIND::VOLTAGE, PROBE_KIND::CURRENT, PROBE_KIND::POWER, PROBE_KIND::OSCILLOSCOPE } )
    {
        auto original = MakeProbeSymbol( kind, path, VECTOR2I( index * 10000, 0 ), false, index++, "Debug" );
        auto* copy = static_cast<SCH_SYMBOL*>( original->Duplicate( IGNORE_PARENT_GROUP ) );
        BOOST_CHECK( copy->m_Uuid != original->m_Uuid );
        copy->Move( VECTOR2I( 0, 10000 ) ); copies.insert( copy );
        originals.push_back( original.get() );
        first->GetScreen()->Append( original.release() ); first->GetScreen()->Append( copy );
    }
    EnsureUniqueProbeIdentities( *schematic, copies );
    std::set<wxString> references, sequences;
    for( auto* item : first->GetScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        BOOST_CHECK( references.insert( symbol->GetRef( &path ) ).second );
        BOOST_CHECK( sequences.insert( symbol->GetField( "CA.ProbeSequence" )->GetText() ).second );
        BOOST_CHECK_EQUAL( symbol->GetField( FIELD_T::VALUE )->GetText(), "Debug" );
    }
    index = 14;
    for( auto* original : originals )
        BOOST_CHECK_EQUAL( original->GetRef( &path ), wxString::Format( "#P%zu", index++ ) );
    BOOST_CHECK_EQUAL( references.size(), 16u );
    BOOST_CHECK_EQUAL( NextProbeSequence( *schematic ), 30u );
}

BOOST_FIXTURE_TEST_CASE( ExistingProbeCollisionsAreRepairedAcrossPagesWithoutReannotatingComponents, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH a, b; a.push_back( first ); b.push_back( second );
    auto one = MakeProbeSymbol( PROBE_KIND::LOGIC, a, VECTOR2I( 0, 0 ), false, 14, "A" );
    auto two = MakeProbeSymbol( PROBE_KIND::VOLTAGE, b, VECTOR2I( 0, 0 ), false, 14, "B" );
    auto missing = MakeProbeSymbol( PROBE_KIND::OSCILLOSCOPE, b, VECTOR2I( 10000, 0 ), false, 99, "C" );
    missing->RemoveField( "CA.ProbeSequence" );
    first->GetScreen()->Append( one.release() ); second->GetScreen()->Append( two.release() );
    second->GetScreen()->Append( missing.release() );
    LIB_SYMBOL lib( "R" );
    auto* componentA = new SCH_SYMBOL( lib, LIB_ID( "Device", "R" ), &a, 1, 1, VECTOR2I( 0, 0 ) );
    auto* componentB = new SCH_SYMBOL( lib, LIB_ID( "Device", "R" ), &b, 1, 1, VECTOR2I( 0, 0 ) );
    componentA->SetRef( &a, "R1" ); componentB->SetRef( &b, "R1" );
    first->GetScreen()->Append( componentA ); second->GetScreen()->Append( componentB );
    BOOST_CHECK_EQUAL( NextProbeSequence( *schematic ), 100u );
    EnsureUniqueProbeIdentities( *schematic );
    std::set<wxString> references, sequences;
    for( auto& sheet : schematic->Hierarchy() )
        for( auto* item : sheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            auto* symbol = static_cast<SCH_SYMBOL*>( item );
            if( !symbol->GetField( "CA.Probe" ) ) continue;
            BOOST_CHECK( references.insert( symbol->GetRef( &sheet ) ).second );
            BOOST_CHECK( sequences.insert( symbol->GetField( "CA.ProbeSequence" )->GetText() ).second );
        }
    BOOST_CHECK_EQUAL( references.size(), 3u );
    BOOST_CHECK_EQUAL( componentA->GetRef( &a ), "R1" ); BOOST_CHECK_EQUAL( componentB->GetRef( &b ), "R1" );
    const auto next = NextProbeSequence( *schematic );
    EnsureUniqueProbeIdentities( *schematic );
    BOOST_CHECK_EQUAL( NextProbeSequence( *schematic ), next );
}

BOOST_FIXTURE_TEST_CASE( ProbeFieldsBelongToTheProbeAndSettingsRemainEditable, PROJECT_PAGES_FIXTURE )
{
    using namespace CIRCUITALIVE;
    SCH_SHEET_PATH path;
    path.push_back( first );
    SOURCE_PROBE_SETTINGS settings;
    settings.frequency = 10;
    auto probe = MakeProbeSymbol( PROBE_KIND::PWM, path, VECTOR2I( 0, 0 ), false, 1, "clock", settings );
    BOOST_CHECK( ProbeSymbolForItem( probe->GetField( "CA.ProbeState" ) ) == probe.get() );
    BOOST_CHECK( ProbeSymbolForItem( probe->GetField( FIELD_T::VALUE ) ) == probe.get() );
    BOOST_CHECK( ProbeSymbolForItem( probe->GetPins( &path ).front() ) == probe.get() );
    settings = ReadSourceProbeSettings( *probe );
    BOOST_CHECK_EQUAL( settings.frequency, 10.0 );
    settings.frequency = 123;
    settings.duty = .25;
    WriteSourceProbeSettings( *probe, PROBE_KIND::PWM, settings );
    const auto edited = ReadSourceProbeSettings( *probe );
    BOOST_CHECK_EQUAL( edited.frequency, 123.0 );
    BOOST_CHECK_EQUAL( edited.duty, .25 );
    first->GetScreen()->Append( probe.release() );
    BOOST_CHECK_EQUAL( NextProbeSequence( *schematic ), 2u );
    SCH_SYMBOL ordinary;
    BOOST_CHECK( !ProbeSymbolForItem( ordinary.GetField( FIELD_T::VALUE ) ) );
}
#endif

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

BOOST_AUTO_TEST_CASE( PlacementTemplatesPreserveLabelContentWithoutSharingSourceState )
{
    auto source = std::make_unique<SCH_GLOBALLABEL>( VECTOR2I( 100, 200 ), "SUPPLY5" );
    source->SetShape( LABEL_FLAG_SHAPE::L_OUTPUT );
    source->SetBold( true );
    source->SetFlags( IS_NEW | IS_MOVING | SELECTED );
    const KIID originalId = source->m_Uuid;
    auto saved = CIRCUITALIVE::CopyPlacementItems( { source.get() } );
    source.reset(); // A template must survive deletion/undo of the placed source.
    BOOST_REQUIRE_EQUAL( saved.size(), 1u );
    auto* label = dynamic_cast<SCH_GLOBALLABEL*>( saved.front().get() );
    BOOST_REQUIRE( label );
    BOOST_CHECK( label->m_Uuid != originalId );
    BOOST_CHECK_EQUAL( label->GetText(), "SUPPLY5" );
    BOOST_CHECK( label->GetShape() == LABEL_FLAG_SHAPE::L_OUTPUT );
    BOOST_CHECK( label->IsBold() );
    BOOST_CHECK( !label->IsSelected() );
    BOOST_CHECK( !label->IsNew() );
    BOOST_CHECK( !label->IsMoving() );
    auto preview = CIRCUITALIVE::CopyPlacementItems( { label } );
    BOOST_REQUIRE_EQUAL( preview.size(), 1u );
    auto* next = static_cast<SCH_GLOBALLABEL*>( preview.front().get() );
    BOOST_CHECK( next->m_Uuid != label->m_Uuid );
    next->SetText( "CHANGED" );
    next->Move( VECTOR2I( 10, 20 ) );
    BOOST_CHECK_EQUAL( label->GetText(), "SUPPLY5" );
    BOOST_CHECK( label->GetPosition() == VECTOR2I( 100, 200 ) );
}

BOOST_AUTO_TEST_CASE( PlacementTemplatesCopyGroupsOnceAndRetainOnlyCopiedMembers )
{
    SCH_GROUP outer, inner;
    SCH_LABEL label( VECTOR2I( 100, 200 ), "DATA" );
    outer.AddItem( &inner );
    inner.AddItem( &label );
    auto saved = CIRCUITALIVE::CopyPlacementItems( { &outer, &inner, &label } );
    BOOST_REQUIRE_EQUAL( saved.size(), 3u );
    SCH_GROUP* group = nullptr;
    SCH_LABEL* copy = nullptr;
    for( auto& item : saved )
    {
        BOOST_CHECK( item->m_Uuid != outer.m_Uuid && item->m_Uuid != inner.m_Uuid && item->m_Uuid != label.m_Uuid );
        if( !item->GetParentGroup() ) group = dynamic_cast<SCH_GROUP*>( item.get() );
        if( item->Type() == SCH_LABEL_T ) copy = static_cast<SCH_LABEL*>( item.get() );
    }
    BOOST_REQUIRE( group );
    BOOST_REQUIRE( copy );
    BOOST_CHECK( group->ContainsItem( copy ) );
    BOOST_CHECK( !group->ContainsItem( &label ) );
    group->Move( VECTOR2I( 50, 60 ) );
    BOOST_CHECK( copy->GetPosition() == VECTOR2I( 150, 260 ) );
    BOOST_CHECK( label.GetPosition() == VECTOR2I( 100, 200 ) );
    BOOST_CHECK( label.GetParentGroup() == &inner );
    BOOST_CHECK( inner.GetParentGroup() == &outer );
}

BOOST_FIXTURE_TEST_CASE( PlacementTemplatesKeepSheetContentsAliveAndCopyPins, PROJECT_PAGES_FIXTURE )
{
    auto source = std::make_unique<SCH_SHEET>( first );
    source->SetName( "Child" );
    source->SetFileName( "child.kicad_sch" );
    source->SetScreen( new SCH_SCREEN( schematic.get() ) );
    auto* pin = new SCH_SHEET_PIN( source.get(), VECTOR2I( 100, 200 ), "DATA" );
    source->AddPin( pin );
    auto saved = CIRCUITALIVE::CopyPlacementItems( { source.get() } );
    source.reset();
    auto* sheet = static_cast<SCH_SHEET*>( saved.front().get() );
    BOOST_REQUIRE( sheet->GetScreen() );
    BOOST_CHECK_EQUAL( sheet->GetFileName(), "child.kicad_sch" );
    BOOST_REQUIRE_EQUAL( sheet->GetPins().size(), 1u );
    BOOST_CHECK( sheet->GetPins().front()->GetParent() == sheet );
    BOOST_CHECK_EQUAL( sheet->GetPins().front()->GetText(), "DATA" );
    auto next = CIRCUITALIVE::CopyPlacementItems( { sheet } );
    auto* copy = static_cast<SCH_SHEET*>( next.front().get() );
    BOOST_CHECK( copy->m_Uuid != sheet->m_Uuid );
    BOOST_CHECK( copy->GetPins().front()->m_Uuid != sheet->GetPins().front()->m_Uuid );
    BOOST_CHECK( copy->GetPins().front()->GetParent() == copy );
    copy->GetPins().front()->SetText( "OTHER" );
    BOOST_CHECK_EQUAL( sheet->GetPins().front()->GetText(), "DATA" );
}

BOOST_AUTO_TEST_CASE( PlacementTemplatesKeepTableCellContentAndOwnership )
{
    auto source = std::make_unique<SCH_TABLE>( 0 );
    source->SetColCount( 1 );
    auto* cell = new SCH_TABLECELL();
    cell->SetText( "Notes" );
    source->AddCell( cell );
    auto saved = CIRCUITALIVE::CopyPlacementItems( { source.get() } );
    source.reset();
    auto* table = static_cast<SCH_TABLE*>( saved.front().get() );
    BOOST_REQUIRE_EQUAL( table->GetCells().size(), 1u );
    BOOST_CHECK_EQUAL( table->GetCells().front()->GetText(), "Notes" );
    BOOST_CHECK( table->GetCells().front()->GetParent() == table );
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
    // A stale independent project for that page must not select another PCB.
    wxFFile stale( Path( "second.kicad_pro" ), "w" );
    stale.Write( "{}" );
    stale.Close();
    BOOST_CHECK_EQUAL( EESCHEMA_HELPERS::FindProjectForSchematic( Path( "second.kicad_sch" ) ), project );
}

BOOST_FIXTURE_TEST_CASE( PcbNetlistContainsAllPagesAndEveryUnitIdentity, PROJECT_PAGES_FIXTURE )
{
    LIB_SYMBOL lib( "Decoder" );
    lib.GetReferenceField().SetText( "U" );
    lib.SetUnitCount( 3, false );
    for( int unit = 1; unit <= 3; ++unit )
    {
        auto* pin = new SCH_PIN( &lib );
        pin->SetUnit( unit );
        pin->SetNumber( wxString::Format( "%d", unit ) );
        lib.AddDrawItem( pin );
    }
    std::vector<KIID> uuids;
    for( int unit = 1; unit <= 3; ++unit )
    {
        SCH_SHEET_PATH path;
        path.push_back( unit == 1 ? first : second );
        auto* symbol = new SCH_SYMBOL( lib, LIB_ID( "Test", "Decoder" ), &path, unit, 0,
                                      VECTOR2I( unit * 10000, 0 ), schematic.get() );
        symbol->SetRef( &path, "U1" );
        symbol->SetUnitSelection( &path, unit );
        symbol->GetField( FIELD_T::FOOTPRINT )->SetText( "Test:DIP-16" );
        path.LastScreen()->Append( symbol );
        path.LastScreen()->Append( new SCH_GLOBALLABEL( symbol->GetPosition(), "SHARED" ) );
        uuids.push_back( symbol->m_Uuid );
    }
    LIB_SYMBOL resistor( "R" );
    resistor.GetReferenceField().SetText( "R" );
    SCH_SHEET_PATH secondPath;
    secondPath.push_back( second );
    auto* r1 = new SCH_SYMBOL( resistor, LIB_ID( "Device", "R" ), &secondPath, 1, 0,
                              VECTOR2I( 0, 10000 ), schematic.get() );
    r1->SetRef( &secondPath, "R1" );
    second->GetScreen()->Append( r1 );
    // Populate the native screen library caches as in a save/reopen cycle.
    first->GetScreen()->UpdateLocalLibSymbolLinks();
    second->GetScreen()->UpdateLocalLibSymbolLinks();
    schematic->ConnectionGraph()->Recalculate( schematic->Hierarchy(), true );

    for( const auto& selected : schematic->Hierarchy() )
    {
        schematic->SetCurrentSheet( selected );
        NETLIST_EXPORTER_KICAD exporter( schematic.get() );
        STRING_FORMATTER formatter;
        exporter.Format( &formatter, GNL_ALL | GNL_OPT_KICAD );
        // Parse the same format sent to the PCB editor, not a screen-specific export.
        NETLIST netlist;
        KICAD_NETLIST_READER reader( new STRING_LINE_READER( formatter.GetString(), "pages" ), &netlist );
        reader.LoadNetlist();
        BOOST_REQUIRE_EQUAL( netlist.GetCount(), 2u );
        COMPONENT* component = netlist.GetComponentByReference( "U1" );
        BOOST_REQUIRE( component );
        BOOST_REQUIRE_EQUAL( component->GetKIIDs().size(), 3u );
        for( const wxString& pin : { wxString( "1" ), wxString( "2" ), wxString( "3" ) } )
            BOOST_CHECK_EQUAL( component->GetNet( pin ).GetNetName(), "SHARED" );
        for( const KIID& uuid : uuids )
            BOOST_CHECK( std::find( component->GetKIIDs().begin(), component->GetKIIDs().end(), uuid )
                         != component->GetKIIDs().end() );
        BOOST_REQUIRE( netlist.GetComponentByReference( "R1" ) );
        BOOST_CHECK( schematic->CurrentSheet() == selected );
    }
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
