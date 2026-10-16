/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <eeschema_test_utils.h>
#include <circuitalive/schematic_adapter.h>
#include <schematic.h>
#include <sch_symbol.h>
#include <sch_field.h>
#include <sch_sheet_path.h>
#include <sch_screen.h>

using namespace CIRCUITALIVE;

BOOST_FIXTURE_TEST_CASE( InteractiveSchematicConnectivitySnapshot, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    LoadSchematic( wxFileName( CIRCUITALIVE_TEST_SCHEMATIC ) );
    auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.locations.size(), 7 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.leds.size(), 1 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.switches.size(), 1 );
    BOOST_CHECK_EQUAL( snapshot.locations.at( snapshot.circuit.leds[0].component ).reference, "D1" );
    BOOST_CHECK_EQUAL( snapshot.locations.at( snapshot.circuit.switches[0].component ).reference, "SW1" );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( 1 );
    const auto sample = session.Snapshot();
    BOOST_CHECK_GT( sample.latest.values.at( 0 ), 0.003 );
    session.SetSourceValue( snapshot.circuit.switches[0].controlSource, 0 );
    session.AdvanceTo( 3 );
    BOOST_CHECK_LT( session.Snapshot().latest.values.at( 0 ), sample.latest.values[0] / 10 );

    // The stored snapshot survives deletion/mutation of its source symbols.
    for( SCH_ITEM* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        if( symbol->GetRef( &m_schematic->CurrentSheet() ) == "D1" )
        {
            SCH_FIELD pins( symbol, FIELD_T::USER, "Sim.Pins" );
            pins.SetText( "1=K 2=A" ); // Default field on newly placed KiCad LED symbols.
            symbol->AddField( pins );
            BOOST_CHECK_NO_THROW( CaptureSchematic( *m_schematic ) );
            symbol->GetField( "Sim.Pins" )->SetText( "1=A 2=K" );
            BOOST_CHECK_THROW( CaptureSchematic( *m_schematic ), std::runtime_error );
            symbol->GetField( "Sim.Pins" )->SetText( "1=K 2=A" );
            symbol->SetLibId( LIB_ID( "Unsupported", "Device" ) );
        }
    }
    BOOST_CHECK_THROW( CaptureSchematic( *m_schematic ), std::runtime_error );
    session.Stop();
    session.AdvanceTo( 1 );
    BOOST_CHECK_CLOSE( session.Snapshot().latest.values.at( 0 ), sample.latest.values[0], 0.001 );
}

BOOST_FIXTURE_TEST_CASE( StandardLibraryResistorWithKiCadSimulationFields, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    LoadSchematic( wxFileName( CIRCUITALIVE_TEST_SCHEMATIC ) );
    for( SCH_ITEM* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        const auto ref = symbol->GetRef( &m_schematic->CurrentSheet() );
        if( ref == "R1" || ref == "C1" )
        {
            const bool resistor = ref == "R1";
            symbol->GetField( FIELD_T::VALUE )->SetText( resistor ? "470R" : "1000uF" );
            const std::map<wxString, wxString> fields = {
                { "Sim.Device", resistor ? "R" : "C" }, { "Sim.Pins", "1=+ 2=-" },
                { "Sim.Params", resistor ? "r=\"999\"" : "c=\"1\"" }
            };
            for( const auto& [name, value] : fields )
            {
                SCH_FIELD field( symbol, FIELD_T::USER, name );
                field.SetText( value );
                symbol->AddField( field );
            }
        }
        if( ref == "D1" )
            symbol->GetField( FIELD_T::VALUE )->SetText( "LED" );
    }
    auto snapshot = CaptureSchematic( *m_schematic );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( 1 );
    // Value controls the ideal passives. Sim.Params does not silently replace 1000uF by 1F.
    BOOST_CHECK_GT( session.Snapshot().latest.values.at( 0 ), 0.002 );
    BOOST_CHECK_LT( session.Snapshot().latest.values.at( 0 ), 0.004 );
}

BOOST_FIXTURE_TEST_CASE( StandardLibrarySourceResistorLedWithSimulationMetadata, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC );
    file.SetFullName( "standard_library_led.kicad_sch" );
    LoadSchematic( file );
    auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.locations.size(), 3 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.leds.size(), 1 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( 0.02 );
    auto current = session.Snapshot().latest.values.at( 0 );
    BOOST_CHECK_GT( current, 0.005 );
    BOOST_CHECK_LT( current, 0.008 );
}
