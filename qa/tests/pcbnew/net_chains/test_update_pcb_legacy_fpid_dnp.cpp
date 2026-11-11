/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <boost/test/unit_test.hpp>
#include <settings/circuitalive_editor_layout.h>
#include <lib_id.h>
#include <netlist_reader/board_netlist_updater.h>
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <board.h>
#include <footprint.h>
#include <netlist_reader/pcb_netlist.h>
#include <widgets/pending_footprints.h>
#include <pad.h>
#include <pcb_track.h>
#include <pcb_painter.h>
#include <netinfo.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <wx/filename.h>


BOOST_AUTO_TEST_SUITE( UpdatePcbLegacyFpidDnp )


static LIB_ID makeFpid( const wxString& aLibrary, const wxString& aItem )
{
    LIB_ID id;
    id.SetLibNickname( aLibrary );
    id.SetLibItemName( aItem );
    return id;
}


// Reproduces https://gitlab.com/kicad/code/kicad/-/issues/24586
//
// A symbol carrying a legacy (bare) footprint field such as "0603" is placed on a board whose
// footprint is fully qualified as "my_footprints:0603". The variant handler in
// BOARD_NETLIST_UPDATER decides whether a footprint is the component's base footprint via
// fpidsEquivalent(). A strict equality test fails for legacy FPIDs (the board side always carries
// a library nickname), so the matching board footprint is mistaken for a non-base variant and
// gets a spurious "Do not place" attribute. The comparison must ignore the library nickname when
// the schematic side is legacy.
BOOST_AUTO_TEST_CASE( LegacySchematicFpidMatchesQualifiedBoardFpid )
{
    LIB_ID schematicFpid;
    BOOST_REQUIRE_EQUAL( schematicFpid.Parse( wxS( "0603" ), true ), -1 );
    BOOST_REQUIRE( schematicFpid.IsLegacy() );

    LIB_ID boardFpid = makeFpid( wxS( "my_footprints" ), wxS( "0603" ) );

    BOOST_CHECK( BOARD_NETLIST_UPDATER::fpidsEquivalent( boardFpid, schematicFpid ) );
}


// A legacy schematic FPID must not match a board footprint with a different item name, otherwise
// genuine non-base variant footprints would no longer be flagged.
BOOST_AUTO_TEST_CASE( LegacySchematicFpidDoesNotMatchDifferentItemName )
{
    LIB_ID schematicFpid;
    BOOST_REQUIRE_EQUAL( schematicFpid.Parse( wxS( "0603" ), true ), -1 );

    LIB_ID boardFpid = makeFpid( wxS( "my_footprints" ), wxS( "0805" ) );

    BOOST_CHECK( !BOARD_NETLIST_UPDATER::fpidsEquivalent( boardFpid, schematicFpid ) );
}


// When the schematic side is fully qualified, the comparison must be exact so that a footprint
// from a different library is correctly treated as a distinct (non-base) variant.
BOOST_AUTO_TEST_CASE( QualifiedSchematicFpidComparesExactly )
{
    LIB_ID schematicFpid = makeFpid( wxS( "Connector_PinHeader_2.54mm" ),
                                     wxS( "PinHeader_1x04_P2.54mm_Vertical" ) );

    LIB_ID sameBoardFpid = makeFpid( wxS( "Connector_PinHeader_2.54mm" ),
                                     wxS( "PinHeader_1x04_P2.54mm_Vertical" ) );
    LIB_ID otherLibBoardFpid = makeFpid( wxS( "my_footprints" ),
                                         wxS( "PinHeader_1x04_P2.54mm_Vertical" ) );

    BOOST_CHECK( BOARD_NETLIST_UPDATER::fpidsEquivalent( sameBoardFpid, schematicFpid ) );
    BOOST_CHECK( !BOARD_NETLIST_UPDATER::fpidsEquivalent( otherLibBoardFpid, schematicFpid ) );
}


BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( SchematicLinkVisibility )

struct ROUTING_FIXTURE
{
    ROUTING_FIXTURE()
    {
        auto* net = new NETINFO_ITEM( &board, "OLD", 1 );
        board.Add( net );
        for( int i = 0; i < 2; ++i )
        {
            const wxString ref = wxString::Format( "U%d", i + 1 );
            auto* footprint = new FOOTPRINT( &board );
            footprint->SetReference( ref );
            footprint->SetPosition( VECTOR2I( pcbIUScale.mmToIU( i * 10 ), 0 ) );
            auto* pad = new PAD( footprint );
            pad->SetNumber( "1" );
            pad->SetAttribute( PAD_ATTRIB::SMD );
            pad->SetLayerSet( LSET( { F_Cu } ) );
            pad->SetSize( F_Cu, VECTOR2I( pcbIUScale.mmToIU( 1 ), pcbIUScale.mmToIU( 1 ) ) );
            pad->SetPosition( footprint->GetPosition() );
            pad->SetNet( net );
            footprint->Add( pad );
            board.Add( footprint );
            auto* component = new COMPONENT( LIB_ID(), ref, "IC", {}, { KIID() } );
            component->AddNet( "1", "NEW", "I/O", "bidirectional" );
            netlist.AddComponent( component );
        }
        for( int i = 0; i < 2; ++i )
        {
            auto* track = new PCB_TRACK( &board );
            track->SetStart( VECTOR2I( pcbIUScale.mmToIU( i * 5 ), 0 ) );
            track->SetEnd( VECTOR2I( pcbIUScale.mmToIU( ( i + 1 ) * 5 ), 0 ) );
            track->SetLayer( F_Cu );
            track->SetNet( net );
            board.Add( track );
        }
        board.BuildConnectivity();
    }

    BOARD board;
    NETLIST netlist;
};

BOOST_FIXTURE_TEST_CASE( RenamingANetKeepsExistingCopperValid, ROUTING_FIXTURE )
{
    BOOST_CHECK( BOARD_NETLIST_UPDATER::FindObsoleteTracks( board, netlist ).empty() );
}

BOOST_FIXTURE_TEST_CASE( SplittingARoutedConnectionMarksItsPhysicalCopper, ROUTING_FIXTURE )
{
    netlist.GetComponent( 1 )->ClearNets();
    netlist.GetComponent( 1 )->AddNet( "1", "OTHER", "I/O", "bidirectional" );
    BOOST_CHECK_EQUAL( BOARD_NETLIST_UPDATER::FindObsoleteTracks( board, netlist ).size(), 2u );
}

BOOST_FIXTURE_TEST_CASE( RemovingASymbolOrPinMarksOldCopper, ROUTING_FIXTURE )
{
    netlist.GetComponent( 1 )->SetReference( "U99" );
    BOOST_CHECK_EQUAL( BOARD_NETLIST_UPDATER::FindObsoleteTracks( board, netlist ).size(), 2u );
    netlist.GetComponent( 1 )->SetReference( "U2" );
    netlist.GetComponent( 1 )->ClearNets();
    BOOST_CHECK_EQUAL( BOARD_NETLIST_UPDATER::FindObsoleteTracks( board, netlist ).size(), 2u );
}

BOOST_FIXTURE_TEST_CASE( RestoringTheConnectionClearsSavedWarnings, ROUTING_FIXTURE )
{
    for( PCB_TRACK* track : board.Tracks() )
        track->SetSchematicConnectionMissing( true );
    BOOST_CHECK( BOARD_NETLIST_UPDATER::FindObsoleteTracks( board, netlist ).empty() );
}

BOOST_FIXTURE_TEST_CASE( BoardOnlyCopperIsPreserved, ROUTING_FIXTURE )
{
    for( FOOTPRINT* footprint : board.Footprints() )
        footprint->SetBoardOnly( true );
    BOOST_CHECK( BOARD_NETLIST_UPDATER::FindObsoleteTracks( board, netlist ).empty() );
}

BOOST_AUTO_TEST_CASE( MissingCopperIsDimmedWithoutChangingGeometryOrPrinting )
{
    BOARD board;
    PCB_TRACK track( &board );
    KIGFX::PCB_RENDER_SETTINGS settings;
    settings.SetLayerColor( F_Cu, KIGFX::COLOR4D( 1.0, 0.2, 0.2, 1.0 ) );
    settings.SetBackgroundColor( KIGFX::COLOR4D::BLACK );
    const auto normal = settings.GetColor( &track, F_Cu );
    track.SetSchematicConnectionMissing( true );
    const auto missing = settings.GetColor( &track, F_Cu );
    BOOST_CHECK_LT( missing.GetBrightness(), normal.GetBrightness() );
    settings.SetIsPrinting( true );
    BOOST_CHECK_CLOSE( settings.GetColor( &track, F_Cu ).GetBrightness(), normal.GetBrightness(), 0.001 );
    std::unique_ptr<PCB_TRACK> copy( static_cast<PCB_TRACK*>( track.Clone() ) );
    copy->SetSchematicConnectionMissing( false );
    track.SwapItemData( copy.get() );
    BOOST_CHECK( !track.IsSchematicConnectionMissing() );
    BOOST_CHECK( copy->IsSchematicConnectionMissing() );
}

BOOST_AUTO_TEST_CASE( MissingSymbolDimsItsPadsAndRestoringTheLinkRestoresColor )
{
    BOARD board;
    FOOTPRINT footprint( &board );
    PAD* pad = new PAD( &footprint );
    footprint.Add( pad );
    KIGFX::PCB_RENDER_SETTINGS settings;
    settings.SetLayerColor( F_Cu, KIGFX::COLOR4D( 1.0, 0.2, 0.2, 1.0 ) );
    settings.SetBackgroundColor( KIGFX::COLOR4D::BLACK );
    const auto normal = settings.GetColor( pad, F_Cu );
    auto* status = new PCB_FIELD( &footprint, FIELD_T::USER, "CA.LinkStatus" );
    status->SetText( "Missing schematic symbol" );
    footprint.Add( status );
    BOOST_CHECK_LT( settings.GetColor( pad, F_Cu ).GetBrightness(), normal.GetBrightness() );
    footprint.Remove( status );
    delete status;
    BOOST_CHECK_CLOSE( settings.GetColor( pad, F_Cu ).GetBrightness(), normal.GetBrightness(), 0.001 );
}

BOOST_AUTO_TEST_CASE( MissingStatusSurvivesSavingTracksArcsAndVias )
{
    BOARD board;
    auto* track = new PCB_TRACK( &board );
    track->SetStart( VECTOR2I( 0, 0 ) );
    track->SetEnd( VECTOR2I( 1000000, 0 ) );
    auto* arc = new PCB_ARC( &board );
    arc->SetStart( VECTOR2I( 2000000, 0 ) );
    arc->SetMid( VECTOR2I( 2500000, 500000 ) );
    arc->SetEnd( VECTOR2I( 3000000, 0 ) );
    auto* via = new PCB_VIA( &board );
    for( PCB_TRACK* item : { track, static_cast<PCB_TRACK*>( arc ), static_cast<PCB_TRACK*>( via ) } )
    {
        item->SetSchematicConnectionMissing( true );
        std::unique_ptr<PCB_TRACK> snapshot( static_cast<PCB_TRACK*>( item->Clone() ) );
        BOOST_CHECK( snapshot->IsSchematicConnectionMissing() );
        snapshot->SetSchematicConnectionMissing( false );
        item->SwapItemData( snapshot.get() );
        BOOST_CHECK( !item->IsSchematicConnectionMissing() );
        BOOST_CHECK( snapshot->IsSchematicConnectionMissing() );
        item->SwapItemData( snapshot.get() );
        board.Add( item );
    }
    const wxString file = wxFileName::CreateTempFileName( wxFileName::GetTempDir() + "/ca-missing-" );
    PCB_IO_KICAD_SEXPR plugin;
    plugin.SaveBoard( file, &board );
    std::unique_ptr<BOARD> loaded( plugin.LoadBoard( file, nullptr ) );
    wxRemoveFile( file );
    BOOST_REQUIRE_EQUAL( loaded->Tracks().size(), 3u );
    for( const PCB_TRACK* item : loaded->Tracks() )
        BOOST_CHECK( item->IsSchematicConnectionMissing() );
}

BOOST_AUTO_TEST_SUITE_END()




BOOST_AUTO_TEST_SUITE( SchematicPcbAssociations )

using UpdatePcbLegacyFpidDnp::makeFpid;

struct LINK_FIXTURE
{
    FOOTPRINT* Footprint( const wxString& reference, std::initializer_list<KIID> path,
                          const wxString& name = "DIP-16" )
    {
        auto* footprint = new FOOTPRINT( &board );
        footprint->SetReference( reference );
        KIID_PATH instance;
        instance.insert( instance.end(), path.begin(), path.end() );
        footprint->SetPath( instance );
        footprint->SetFPID( makeFpid( "Test", name ) );
        footprint->SetPosition( VECTOR2I( 123000, 456000 ) );
        board.Add( footprint );
        return footprint;
    }

    COMPONENT* Component( const wxString& reference, std::initializer_list<KIID> path,
                          const std::vector<KIID>& uuids )
    {
        KIID_PATH instance;
        instance.insert( instance.end(), path.begin(), path.end() );
        auto* component = new COMPONENT( makeFpid( "Test", "DIP-16" ), reference, "74LS139", instance, uuids );
        netlist.AddComponent( component );
        return component;
    }

    BOARD board;
    NETLIST netlist;
};

BOOST_FIXTURE_TEST_CASE( IdentitySurvivesReferenceSwapsAndDifferentProcessingOrder, LINK_FIXTURE )
{
    const KIID sheet, a, b;
    auto* u1 = Footprint( "U1", { sheet, a } );
    auto* u2 = Footprint( "U2", { sheet, b } );
    auto* renamedB = Component( "U1", { sheet }, { b } );
    auto* renamedA = Component( "U2", { sheet }, { a } );
    const auto associations = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_CHECK( associations.ambiguousReferences.empty() );
    BOOST_CHECK( associations.matches.at( u1 ) == renamedA );
    BOOST_CHECK( associations.matches.at( u2 ) == renamedB );
    BOOST_CHECK( u1->GetPosition() == VECTOR2I( 123000, 456000 ) );
    BOOST_CHECK_EQUAL( u1->GetReference(), "U1" ); // Resolver/dry-run does not mutate the board.
}

BOOST_FIXTURE_TEST_CASE( PageMoveAndReferenceRenameRecoverTheOriginalFootprint, LINK_FIXTURE )
{
    const KIID oldSheet, newSheet, symbol;
    auto* u1 = Footprint( "U1", { oldSheet, symbol } );
    auto* u12 = Component( "U12", { newSheet }, { symbol } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_CHECK( links.matches.at( u1 ) == u12 );
}

BOOST_FIXTURE_TEST_CASE( IdentityModeDoesNotStealAReferenceReusedByANewSymbol, LINK_FIXTURE )
{
    const KIID sheet, oldA, oldB, newB, newC;
    auto* u1 = Footprint( "U1", { sheet, oldA } );
    auto* u2 = Footprint( "U2", { sheet, oldB } );
    auto* u12 = Component( "U12", { sheet }, { oldA } );
    auto* recreated = Component( "u2", { sheet }, { newB } );
    Component( "U1", { sheet }, { newC } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_REQUIRE_EQUAL( links.matches.size(), 1u );
    BOOST_CHECK( links.matches.at( u1 ) == u12 );
    BOOST_CHECK_EQUAL( links.matches.count( u2 ), 0u );
    const auto byReference = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, false );
    BOOST_CHECK( byReference.matches.at( u2 ) == recreated );
}

BOOST_FIXTURE_TEST_CASE( SharedSheetInstancesDoNotLinkBySymbolUuidAlone, LINK_FIXTURE )
{
    const KIID oldSheet, sheetA, sheetB, sharedSymbol;
    auto* u1 = Footprint( "U1", { oldSheet, sharedSymbol } );
    auto* u2 = Footprint( "U2", { sheetB, sharedSymbol } );
    auto* first = Component( "U1", { sheetA }, { sharedSymbol } );
    auto* second = Component( "U2", { sheetB }, { sharedSymbol } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_CHECK_EQUAL( links.matches.count( u1 ), 0u );
    BOOST_CHECK( links.matches.at( u2 ) == second );
    const auto byReference = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, false );
    BOOST_CHECK( byReference.matches.at( u1 ) == first );
    BOOST_CHECK( byReference.matches.at( u2 ) == second );
}

BOOST_FIXTURE_TEST_CASE( AnyUnitOfAMultiPagePackageCanRetainTheAssociation, LINK_FIXTURE )
{
    const KIID sheetA, sheetB, a, b, power;
    auto* footprint = Footprint( "U1", { sheetB, power } );
    auto* component = Component( "U13", { sheetA }, { a, b, power } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_CHECK( links.matches.at( footprint ) == component );
}

BOOST_FIXTURE_TEST_CASE( BoardOnlyAndDeletedComponentsStayUnassociated, LINK_FIXTURE )
{
    auto* mechanical = Footprint( "U1", {} );
    mechanical->SetBoardOnly( true );
    Footprint( "U2", { KIID(), KIID() } );
    Component( "U1", { KIID() }, { KIID() } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_CHECK( links.matches.empty() );
}

BOOST_FIXTURE_TEST_CASE( DuplicateReferencesAndUnlinkedCopiesAreReportedAsAmbiguous, LINK_FIXTURE )
{
    Component( "U1", { KIID() }, { KIID() } );
    Component( "u1", { KIID() }, { KIID() } );
    Component( "U2", { KIID() }, { KIID() } );
    Footprint( "U2", {} );
    Footprint( "U2", {} );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, false );
    BOOST_CHECK_EQUAL( links.ambiguousReferences.count( "U1" ), 1u );
    BOOST_CHECK_EQUAL( links.ambiguousReferences.count( "U2" ), 1u );
    BOOST_CHECK_EQUAL( links.matches.size(), 2u );
}

BOOST_FIXTURE_TEST_CASE( ExplicitVariantFootprintsMayShareAReference, LINK_FIXTURE )
{
    auto* component = Component( "U1", { KIID() }, { KIID() } );
    COMPONENT_VARIANT variant( "SMD" );
    variant.m_fields[GetCanonicalFieldName( FIELD_T::FOOTPRINT )] = "Test:SOIC-16";
    component->AddVariant( variant );
    auto* dip = Footprint( "U1", {} );
    auto* soic = Footprint( "U1", {}, "SOIC-16" );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, false );
    BOOST_CHECK( links.ambiguousReferences.empty() );
    BOOST_CHECK( links.matches.at( dip ) == component );
    BOOST_CHECK( links.matches.at( soic ) == component );
}

BOOST_FIXTURE_TEST_CASE( DuplicateLinkedFootprintsCannotSilentlyRepresentOneComponent, LINK_FIXTURE )
{
    const KIID sheet, symbol;
    Component( "U1", { sheet }, { symbol } );
    Footprint( "U1", { sheet, symbol } );
    Footprint( "U1", { sheet, symbol } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, true );
    BOOST_CHECK_EQUAL( links.ambiguousReferences.count( "U1" ), 1u );
}

BOOST_FIXTURE_TEST_CASE( ReferenceMatchesTakePriorityOverIdentityRecovery, LINK_FIXTURE )
{
    const KIID sheet, a, b;
    auto* footprint = Footprint( "U1", { sheet, a } );
    Component( "U12", { sheet }, { a } );
    auto* replacement = Component( "U1", { sheet }, { b } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, false );
    BOOST_CHECK( links.matches.at( footprint ) == replacement );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE( PendingFootprints, SchematicPcbAssociations::LINK_FIXTURE )

BOOST_AUTO_TEST_CASE( ReferenceModeRetainsRenamedFootprintsWithoutClaimingExistingReferences )
{
    const KIID sheet, symbol;
    auto* footprint = Footprint( "U1", { sheet, symbol } );
    auto* component = Component( "U12", { sheet }, { symbol } );
    const auto links = BOARD_NETLIST_UPDATER::BuildFootprintAssociations( board, netlist, false );
    BOOST_CHECK( links.matches.at( footprint ) == component );
    BOOST_CHECK( CIRCUITALIVE::BuildPendingFootprints( board, netlist ).empty() );
}

BOOST_AUTO_TEST_CASE( PendingListContainsEveryEligibleUnplacedComponentInReferenceOrder )
{
    Component( "U1", {}, { KIID() } );
    Component( "R10", {}, { KIID() } );
    Component( "R2", {}, { KIID() } )->SetValue( "10k" );
    Component( "#PWR01", {}, { KIID() } );
    Component( "U?", {}, { KIID() } );
    Component( "J1", {}, { KIID() } )->SetProperties( { { "exclude_from_board", "" } } );
    const auto pending = CIRCUITALIVE::BuildPendingFootprints( board, netlist );
    BOOST_REQUIRE_EQUAL( pending.size(), 3u );
    BOOST_CHECK_EQUAL( pending[0].reference, "R2" );
    BOOST_CHECK_EQUAL( pending[0].value, "10k" );
    BOOST_CHECK_EQUAL( pending[1].reference, "R10" );
    BOOST_CHECK_EQUAL( pending[2].reference, "U1" );
}

BOOST_AUTO_TEST_CASE( PlacementAndRemovalRefreshPendingListWithoutChangingTheSchematic )
{
    Component( "U1", {}, { KIID() } );
    Component( "U2", {}, { KIID() } );
    auto* placed = Footprint( "U1", {} );
    auto* mechanical = Footprint( "U2", {} );
    mechanical->SetBoardOnly( true );
    auto pending = CIRCUITALIVE::BuildPendingFootprints( board, netlist );
    BOOST_REQUIRE_EQUAL( pending.size(), 1u );
    BOOST_CHECK_EQUAL( pending[0].reference, "U2" );
    board.Remove( placed );
    delete placed;
    pending = CIRCUITALIVE::BuildPendingFootprints( board, netlist );
    BOOST_REQUIRE_EQUAL( pending.size(), 2u );
    BOOST_CHECK_EQUAL( netlist.GetCount(), 2u );
}

BOOST_AUTO_TEST_CASE( MissingAssignmentsStayVisibleAndVariantFootprintsHaveSeparateSlots )
{
    auto* component = Component( "U1", {}, { KIID() } );
    COMPONENT_VARIANT variant( "SMD" );
    variant.m_fields[GetCanonicalFieldName( FIELD_T::FOOTPRINT )] = "Test:SOIC-16";
    component->AddVariant( variant );
    Footprint( "U1", {} );
    Component( "R1", {}, { KIID() } )->SetFPID( LIB_ID() );
    auto pending = CIRCUITALIVE::BuildPendingFootprints( board, netlist );
    BOOST_REQUIRE_EQUAL( pending.size(), 2u );
    BOOST_CHECK( pending[0].footprint.empty() );
    BOOST_CHECK_EQUAL( pending[1].footprint.Format(), "Test:SOIC-16" );
    Footprint( "U1", {}, "SOIC-16" );
    pending = CIRCUITALIVE::BuildPendingFootprints( board, netlist );
    BOOST_REQUIRE_EQUAL( pending.size(), 1u );
    BOOST_CHECK_EQUAL( pending[0].reference, "R1" );
}

BOOST_AUTO_TEST_SUITE_END()


BOOST_AUTO_TEST_SUITE( DefaultEditorLayouts )

BOOST_AUTO_TEST_CASE( FreshSchematicGetsUsedComponentsOnLeft )
{
    APP_SETTINGS_BASE settings( "eeschema-layout-test", 0 );
    settings.Load();
    CIRCUITALIVE::ApplyDefaultEditorLayout( settings, true );
    BOOST_CHECK( settings.m_Window.perspective.Contains( "name=UsedComponents" ) );
    BOOST_CHECK( settings.m_Window.perspective.Contains( "dir=4;layer=0;row=0;pos=0" ) );
    BOOST_CHECK( !settings.Get<bool>( "aui.show_schematic_hierarchy" ).value() );
    BOOST_CHECK_EQUAL( settings.Get<int>( "appearance.used_components_preview_height" ).value(), 144 );
    BOOST_CHECK( !settings.Contains( "circuitalive.simulation" ) );
    BOOST_CHECK( !settings.Contains( "window.size_x" ) );
}

BOOST_AUTO_TEST_CASE( FreshPcbGetsPendingFootprintsAndAppearance )
{
    APP_SETTINGS_BASE settings( "pcb-layout-test", 0 );
    settings.Load();
    CIRCUITALIVE::ApplyDefaultEditorLayout( settings, false );
    BOOST_CHECK( settings.m_Window.perspective.Contains( "name=PendingFootprints" ) );
    BOOST_CHECK( settings.Get<bool>( "circuitalive.pending_visible" ).value() );
    BOOST_CHECK_EQUAL( settings.Get<int>( "aui.right_panel_width" ).value(), 229 );
    BOOST_CHECK( !settings.Contains( "window.pos_x" ) );
}

BOOST_AUTO_TEST_CASE( SavedLegacyOrModernLayoutIsPreserved )
{
    APP_SETTINGS_BASE settings( "saved-layout-test", 0 );
    settings.Load();
    settings.Set<wxString>( "window.perspective", "layout2|custom" );
    settings.Load();
    CIRCUITALIVE::ApplyDefaultEditorLayout( settings, true );
    BOOST_CHECK_EQUAL( settings.m_Window.perspective, "layout2|custom" );
    BOOST_CHECK( !settings.Contains( "appearance.used_components_preview_height" ) );

    settings.Set<wxString>( "window.perspective", "" );
    settings.Set<nlohmann::json>( "window.aui_state", { { "custom", true } } );
    settings.Load();
    CIRCUITALIVE::ApplyDefaultEditorLayout( settings, false );
    BOOST_CHECK( settings.m_Window.perspective.IsEmpty() );
    BOOST_CHECK( !settings.Contains( "circuitalive.pending_visible" ) );
}

BOOST_AUTO_TEST_CASE( DefaultsKeepExistingPanelPreferencesAndAreAppliedOnce )
{
    APP_SETTINGS_BASE settings( "partial-layout-test", 0 );
    settings.Set<bool>( "circuitalive.pending_visible", false );
    settings.Set<int>( "circuitalive.pending_sash", 123 );
    settings.Load();
    CIRCUITALIVE::ApplyDefaultEditorLayout( settings, false );
    BOOST_CHECK( !settings.Get<bool>( "circuitalive.pending_visible" ).value() );
    BOOST_CHECK_EQUAL( settings.Get<int>( "circuitalive.pending_sash" ).value(), 123 );
    settings.Set<int>( "aui.right_panel_width", 333 );
    CIRCUITALIVE::ApplyDefaultEditorLayout( settings, false );
    BOOST_CHECK_EQUAL( settings.Get<int>( "aui.right_panel_width" ).value(), 333 );
}

BOOST_AUTO_TEST_SUITE_END()
