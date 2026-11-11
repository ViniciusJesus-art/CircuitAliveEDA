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
#include <lib_id.h>
#include <netlist_reader/board_netlist_updater.h>
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <board.h>
#include <footprint.h>
#include <netlist_reader/pcb_netlist.h>
#include <widgets/pending_footprints.h>


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
