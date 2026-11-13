/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
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
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 * or you may search the http://www.gnu.org website for the version 32 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */


#include <qa_utils/wx_utils/unit_test_utils.h>
#include <schematic_utils/schematic_file_util.h>

#include <connection_graph.h>
#include <schematic.h>
#include <sch_sheet.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <sch_pin.h>
#include <sch_label.h>
#include <settings/settings_manager.h>
#include <locale_io.h>
#include <string_utils.h>

struct CONNECTIVITY_TEST_FIXTURE
{
    CONNECTIVITY_TEST_FIXTURE()
    { }

    SETTINGS_MANAGER           m_settingsManager;
    std::unique_ptr<SCHEMATIC> m_schematic;
};

// Ground keeps its name when a signal label is intentionally tied to it. Other
// signal labels remain independent, and moving the alias away restores its net.
BOOST_FIXTURE_TEST_CASE( GroundKeepsCanonicalNameWithSignalAliases, CONNECTIVITY_TEST_FIXTURE )
{
    KI_TEST::LoadSchematic( m_settingsManager, "ground_pin_test_ok", m_schematic );
    SCH_SHEET_LIST sheets = m_schematic->Hierarchy();
    SCH_SHEET_PATH sheet = sheets.at( 0 );
    SCH_SCREEN* screen = sheet.LastScreen();
    std::vector<SCH_PIN*> groundPins;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        for( SCH_PIN* pin : symbol->GetPins() )
        {
            if( pin->IsGlobalPower() && symbol->GetValue( false, nullptr, false ) == wxS( "GND" ) )
                groundPins.push_back( pin );
        }
    }

    BOOST_REQUIRE( !groundPins.empty() );
    auto* alias = new SCH_GLOBALLABEL( groundPins.front()->GetPosition(), wxS( "/CARD_DETECT" ) );
    auto* remote = new SCH_GLOBALLABEL( VECTOR2I( 10000000, 10000000 ), wxS( "/CARD_DETECT" ) );
    auto* unrelated = new SCH_GLOBALLABEL( VECTOR2I( 20000000, 10000000 ), wxS( "/OTHER_SIGNAL" ) );
    screen->Append( alias );
    screen->Append( remote );
    screen->Append( unrelated );

    auto* otherSheet = new SCH_SHEET( m_schematic.get() );
    auto* otherScreen = new SCH_SCREEN( m_schematic.get() );
    const_cast<KIID&>( otherSheet->m_Uuid ) = otherScreen->GetUuid();
    otherSheet->SetScreen( otherScreen );
    otherSheet->GetField( FIELD_T::SHEET_NAME )->SetText( wxS( "Remote" ) );
    otherScreen->SetFileName( wxS( "remote_ground_alias.kicad_sch" ) );
    otherScreen->SetPageNumber( wxS( "2" ) );
    auto* otherAlias = new SCH_GLOBALLABEL( VECTOR2I( 10000000, 10000000 ), wxS( "/CARD_DETECT" ) );
    otherScreen->Append( otherAlias );
    auto* otherGround = static_cast<SCH_SYMBOL*>( groundPins.front()->GetParentSymbol()->Clone() );
    const_cast<KIID&>( otherGround->m_Uuid ) = KIID();
    otherGround->SetPosition( VECTOR2I( 20000000, 20000000 ) );
    otherScreen->Append( otherGround );
    auto topSheets = m_schematic->GetTopLevelSheets();
    topSheets.push_back( otherSheet );
    m_schematic->SetTopLevelSheets( topSheets );
    m_schematic->RefreshHierarchy();
    sheets = m_schematic->Hierarchy();
    SCH_SHEET_PATH otherPath;
    for( const SCH_SHEET_PATH& path : sheets )
    {
        if( path.LastScreen() == otherScreen )
            otherPath = path;
    }
    BOOST_REQUIRE( otherPath.LastScreen() == otherScreen );

    m_schematic->ConnectionGraph()->Recalculate( sheets, true );

    for( SCH_PIN* pin : groundPins )
        BOOST_CHECK_EQUAL( pin->Connection( &sheet )->Name(), wxS( "GND" ) );

    BOOST_CHECK_EQUAL( alias->Connection( &sheet )->Name(), wxS( "GND" ) );
    BOOST_CHECK_EQUAL( remote->Connection( &sheet )->Name(), wxS( "GND" ) );
    BOOST_CHECK_EQUAL( otherAlias->Connection( &otherPath )->Name(), wxS( "GND" ) );
    BOOST_CHECK_EQUAL( otherGround->GetPins().front()->Connection( &otherPath )->Name(), wxS( "GND" ) );
    BOOST_CHECK_EQUAL( UnescapeString( unrelated->Connection( &sheet )->Name() ), wxS( "/OTHER_SIGNAL" ) );
    BOOST_CHECK_EQUAL( alias->GetText(), wxS( "/CARD_DETECT" ) );
    BOOST_CHECK_EQUAL( remote->GetText(), wxS( "/CARD_DETECT" ) );

    alias->SetPosition( VECTOR2I( 10000000, 20000000 ) );
    m_schematic->ConnectionGraph()->Recalculate( sheets, true );

    for( SCH_PIN* pin : groundPins )
        BOOST_CHECK_EQUAL( pin->Connection( &sheet )->Name(), wxS( "GND" ) );

    BOOST_CHECK_EQUAL( UnescapeString( alias->Connection( &sheet )->Name() ), wxS( "/CARD_DETECT" ) );
    BOOST_CHECK_EQUAL( UnescapeString( remote->Connection( &sheet )->Name() ), wxS( "/CARD_DETECT" ) );
    BOOST_CHECK_EQUAL( UnescapeString( otherAlias->Connection( &otherPath )->Name() ), wxS( "/CARD_DETECT" ) );
    BOOST_CHECK_EQUAL( otherGround->GetPins().front()->Connection( &otherPath )->Name(), wxS( "GND" ) );
    BOOST_CHECK_NE( remote->Connection( &sheet )->NetCode(), groundPins.front()->Connection( &sheet )->NetCode() );
}

BOOST_FIXTURE_TEST_CASE( CheckNetCounts, CONNECTIVITY_TEST_FIXTURE )
{
    LOCALE_IO dummy;

    std::vector<std::pair<wxString, int>> tests =
    {
        { "issue18092/issue18092",                   1  }
    };

    for( auto&[ name, nets] : tests )
    {
        KI_TEST::LoadSchematic( m_settingsManager, name, m_schematic );

        SCH_SHEET_LIST sheets = m_schematic->BuildSheetListSortedByPageNumbers();
        CONNECTION_GRAPH* graph = m_schematic->ConnectionGraph();

        BOOST_CHECK( nets == graph->GetNetMap().size() );

    }
}


/**
 * Test for issue #17771: Bus implicit connection not working across hierarchy.
 *
 * The schematic has a hierarchy where bus members should connect through implicit
 * bus connections. The bug was that V.Z1 connects through to the root but V.Z2 does not,
 * because the intermediate net W.X.Y.Z1 is explicitly labeled on the "sub" sheet while
 * W.X.Y.Z2 relies on implicit bus member expansion.
 *
 * Expected: Both nets should connect properly:
 *   - m.X.Y.Z1 (TP101/TP102 on root) should connect to V.Z1/V.Z2 (TP401/TP402 on sub3)
 */
BOOST_FIXTURE_TEST_CASE( Issue17771, CONNECTIVITY_TEST_FIXTURE )
{
    LOCALE_IO dummy;

    KI_TEST::LoadSchematic( m_settingsManager, "issue17771/issue1771", m_schematic );

    SCH_SHEET_LIST sheets = m_schematic->BuildSheetListSortedByPageNumbers();
    CONNECTION_GRAPH* graph = m_schematic->ConnectionGraph();

    // Find test points and their net connections
    // TP101 and TP401 should be on the same net (m.X.Y.Z1)
    // TP102 and TP402 should be on the same net (m.X.Y.Z2)
    std::map<wxString, int> tpNetCodes;  // Test point reference -> net code

    for( const auto& [key, subgraphs] : graph->GetNetMap() )
    {
        for( CONNECTION_SUBGRAPH* subgraph : subgraphs )
        {
            for( SCH_ITEM* item : subgraph->GetItems() )
            {
                if( item->Type() == SCH_PIN_T )
                {
                    SCH_PIN* pin = static_cast<SCH_PIN*>( item );
                    SCH_SYMBOL* symbol = dynamic_cast<SCH_SYMBOL*>( pin->GetParentSymbol() );

                    if( symbol )
                    {
                        wxString ref = symbol->GetRef( &subgraph->GetSheet() );

                        if( ref == "TP101" || ref == "TP102" ||
                            ref == "TP401" || ref == "TP402" )
                        {
                            tpNetCodes[ref] = key.Netcode;
                        }
                    }
                }
            }
        }
    }

    // Verify we found all test points
    BOOST_REQUIRE_MESSAGE( tpNetCodes.count( "TP101" ),
                           "TP101 should be found in netlist" );
    BOOST_REQUIRE_MESSAGE( tpNetCodes.count( "TP102" ),
                           "TP102 should be found in netlist" );
    BOOST_REQUIRE_MESSAGE( tpNetCodes.count( "TP401" ),
                           "TP401 should be found in netlist" );
    BOOST_REQUIRE_MESSAGE( tpNetCodes.count( "TP402" ),
                           "TP402 should be found in netlist" );

    // TP101 and TP401 should be on the same net (m.X.Y.Z1 path)
    BOOST_CHECK_MESSAGE( tpNetCodes["TP101"] == tpNetCodes["TP401"],
                         "TP101 and TP401 should be on the same net (m.X.Y.Z1)" );

    // TP102 and TP402 should be on the same net (m.X.Y.Z2 path)
    // This is the bug - without the fix, these will be on different nets
    BOOST_CHECK_MESSAGE( tpNetCodes["TP102"] == tpNetCodes["TP402"],
                         "TP102 and TP402 should be on the same net (m.X.Y.Z2)" );
}


/**
 * Issue #25086. Unannotated pins did not drive nets. Every wire got "<NO NET>"
 * and highlighting one net lit up the whole schematic. Now each unannotated
 * symbol drives its own net, named with a short hash of its UUID.
 */
BOOST_FIXTURE_TEST_CASE( Issue25086UnannotatedNetNames, CONNECTIVITY_TEST_FIXTURE )
{
    LOCALE_IO dummy;

    KI_TEST::LoadSchematic( m_settingsManager, "issue25086_unannotated_pins", m_schematic );

    SCH_SHEET_PATH sheet = m_schematic->BuildSheetListSortedByPageNumbers().at( 0 );
    SCH_SCREEN*    screen = sheet.LastScreen();

    std::vector<SCH_CONNECTION*> wireConns;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_LINE_T ) )
    {
        if( item->GetLayer() == LAYER_WIRE )
            wireConns.push_back( item->Connection( &sheet ) );
    }

    BOOST_REQUIRE_EQUAL( wireConns.size(), 2 );
    BOOST_REQUIRE( wireConns[0] && wireConns[1] );

    // Each wire joins its own pair of unannotated pins, so the nets must be distinct
    BOOST_CHECK_NE( wireConns[0]->Name(), wireConns[1]->Name() );

    // Either pin on a wire may win the driver tie-break, so accept any symbol's name
    std::vector<wxString> expectedNames;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        unsigned serial = (unsigned) ( item->m_Uuid.Hash() & 0xFFFFFFFF );
        expectedNames.push_back( wxString::Format( wxS( "Net-(TP?-%08x-Pad1)" ), serial ) );
    }

    BOOST_REQUIRE_EQUAL( expectedNames.size(), 4 );

    for( SCH_CONNECTION* conn : wireConns )
    {
        bool found = std::find( expectedNames.begin(), expectedNames.end(), conn->Name() ) != expectedNames.end();

        BOOST_CHECK_MESSAGE( found, "Unexpected net name: " + conn->Name().ToStdString() );
    }
}
