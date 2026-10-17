/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <eeschema_test_utils.h>
#include <circuitalive/schematic_adapter.h>
#include <schematic.h>
#include <sch_symbol.h>
#include <sch_field.h>
#include <sch_sheet_path.h>
#include <sch_screen.h>
#include <circuitalive/probe_placement.h>
#include <circuitalive/model_profiles.h>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include <connection_graph.h>
#include <sch_pin.h>
#include <base_units.h>

using namespace CIRCUITALIVE;

BOOST_FIXTURE_TEST_CASE( StandardLibraryPowerDiscreteDemoResolvesModelsAndRetainsScrState,
                         KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "common_discrete_models.kicad_sch" );
    LoadSchematic( file );
    for( SCH_ITEM* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        BOOST_CHECK( symbol->GetField( "CA.Model" ) == nullptr ); // Automatic model association.
    }
    auto captured = CaptureSchematic( *m_schematic ); const auto& c = captured.circuit;
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( c.netlist, c.config ); session.AdvanceTo( .01 ); const auto sample = session.Snapshot().latest;
    int checked = 0;
    for( const auto& channel : c.measurements )
    {
        const double value = MeasurementValue( channel, sample );
        if( channel.quantity == MEASURE_QUANTITY::CURRENT && ( channel.name == "Q1" || channel.name == "Q2" || channel.name == "Q3" || channel.name == "Q4" ) )
        {
            BOOST_TEST_CONTEXT( channel.name ) { BOOST_CHECK_GT( std::abs( value ), .1 ); BOOST_CHECK_LT( std::abs( value ), .13 ); }
            ++checked;
        }
        if( channel.quantity == MEASURE_QUANTITY::VOLTAGE && channel.name == "D1" )
        {
            BOOST_CHECK_GT( -value, 5.0 ); BOOST_CHECK_LT( -value, 5.4 ); ++checked;
        }
    }
    BOOST_CHECK_EQUAL( checked, 5 );
}

BOOST_FIXTURE_TEST_CASE( ModelAssociationAndParametersPersistInSymbolFields, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC );
    file.SetFullName( "standard_library_led.kicad_sch" );
    LoadSchematic( file );
    SCH_SYMBOL* led = nullptr;
    for( SCH_ITEM* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        if( symbol->GetRef( &m_schematic->CurrentSheet() ) == "D1" ) led = symbol;
    }
    BOOST_REQUIRE( led );
    auto settings = ReadSymbolModelSettings( *led, m_schematic->CurrentSheet(), "", "" );
    const auto originalValue = settings.value;
    settings.modelFile = "builtin:led";
    settings.modelPins = { { "a", "2" }, { "k", "1" } };
    settings.modelParameters = { { "forward_voltage", "3.2V" }, { "reference_current", "20mA" } };
    settings.measurements = { true, true, true, "a", "k" };
    WriteSymbolModelSettings( *led, settings );
    BOOST_CHECK( !led->GetField( "CA.Model" )->IsVisible() );
    const auto read = ReadSymbolModelSettings( *led, m_schematic->CurrentSheet(), "", "" );
    BOOST_CHECK_EQUAL( read.modelFile, settings.modelFile );
    BOOST_CHECK( read.modelPins == settings.modelPins );
    BOOST_CHECK( read.modelParameters == settings.modelParameters );
    BOOST_CHECK( read.measurements.voltage && read.measurements.current && read.measurements.power );
    BOOST_CHECK_EQUAL( read.measurements.positive, "a" ); BOOST_CHECK_EQUAL( read.measurements.negative, "k" );
    BOOST_CHECK_EQUAL( read.value, originalValue );
    // The association, not the drawing's library ID, owns simulation compatibility.
    led->SetLibId( LIB_ID( "ArbitraryLibrary", "ArbitraryDrawing" ) );
    auto snapshot = CaptureSchematic( *m_schematic );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( .02 );
    const double current = session.Snapshot().latest.values.at( snapshot.circuit.leds[0].signal );
    BOOST_CHECK_GT( current, .003 ); BOOST_CHECK_LT( current, .005 );
    settings.modelFile.clear(); settings.modelPins.clear(); settings.modelParameters.clear();
    settings.measurements = {};
    WriteSymbolModelSettings( *led, settings );
    BOOST_CHECK( led->GetField( "CA.Model" ) == nullptr );
    BOOST_CHECK( led->GetField( "CA.Pins" ) == nullptr );
    BOOST_CHECK( led->GetField( "CA.Parameters" ) == nullptr );
    BOOST_CHECK_THROW( CaptureSchematic( *m_schematic ), std::runtime_error );
}

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

BOOST_FIXTURE_TEST_CASE( ProjectLocalResistorNoGroundLabelAndOpenPin, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC );
    file.SetFullName( "open_pins_auto_reference.kicad_sch" );
    LoadSchematic( file );
    auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.locations.size(), 4 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.automaticReferences.size(), 1 );
    BOOST_CHECK_EQUAL( snapshot.circuit.automaticReferences[0], "V2.2" );
    BOOST_CHECK( snapshot.circuit.floatingIslands.empty() );
    BOOST_CHECK_EQUAL( snapshot.circuit.openPins, 1 );
    std::size_t openResistor = 0;
    for( std::size_t i = 0; i < snapshot.locations.size(); ++i )
        if( snapshot.locations[i].reference == "R2" ) openResistor = i;
    snapshot.circuit.config.signals.push_back(
            "v(" + snapshot.circuit.pinNodes.at( openResistor ).at( "2" ) + ")" );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( 0.02 );
    const auto sample = session.Snapshot();
    BOOST_CHECK_GT( sample.latest.values.at( 0 ), 0.005 );
    BOOST_CHECK_LT( sample.latest.values.at( 0 ), 0.008 );
    BOOST_CHECK_CLOSE( sample.latest.values.back(), 5, 1e-5 );
}

BOOST_FIXTURE_TEST_CASE( NativeProbeSymbolsConnectAndKeepPhysicalPinOrientation, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "standard_library_led.kicad_sch" ); LoadSchematic( file );
    auto& sheet = m_schematic->CurrentSheet();
    VECTOR2I position;
    bool found = false;
    for( auto* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        if( !symbol->GetRef( &sheet ).StartsWith( "V" ) ) continue;
        for( const auto* pin : symbol->GetPins( &sheet ) )
            if( pin->GetShownNumber() == "1" ) { position = pin->GetPosition(); found = true; }
    }
    BOOST_REQUIRE( found );
    auto probe = MakeProbeSymbol( PROBE_KIND::VOLTAGE, sheet, position, false, 1, "Supply" );
    m_schematic->RootScreen()->Append( probe.release() );
    m_schematic->ConnectionGraph()->Recalculate( m_schematic->Hierarchy(), true );
    const auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_CHECK_EQUAL( snapshot.locations.size(), 3 ); BOOST_REQUIRE_EQUAL( snapshot.probes.size(), 1 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.measurements.size(), 1 );
    BOOST_CHECK_EQUAL( snapshot.circuit.measurements[0].name, "Supply" );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config ); session.AdvanceTo( .02 );
    BOOST_CHECK_CLOSE( MeasurementValue( snapshot.circuit.measurements[0], session.Snapshot().latest ), 5, 1e-5 );
    for( bool vertical : { false, true } )
    {
        auto ammeter = MakeProbeSymbol( PROBE_KIND::CURRENT, sheet, position, vertical, 2, "" );
        BOOST_REQUIRE_EQUAL( ammeter->GetPins( &sheet ).size(), 2 );
        for( const auto* pin : ammeter->GetPins( &sheet ) )
        {
            const auto actual = pin->GetPosition();
            const int direction = pin->GetShownNumber() == "1" ? -1 : 1;
            const auto delta = vertical ? VECTOR2I( 0, schIUScale.mmToIU( 1.27 ) ) : VECTOR2I( schIUScale.mmToIU( 1.27 ), 0 );
            BOOST_CHECK( actual == position + delta * direction );
        }
    }
}

BOOST_FIXTURE_TEST_CASE( SavedOscilloscopeProbeCapturesCapacitorCharge, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC );
    file.SetFullName( "scope_rc.kicad_sch" );
    LoadSchematic( file );
    const auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.probes.size(), 1 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.measurements.size(), 1 );
    const auto& channel = snapshot.circuit.measurements.front();
    BOOST_CHECK( channel.scope && channel.connected );
    BOOST_CHECK_EQUAL( channel.name, "Capacitor" );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( .02 );
    const double early = MeasurementValue( channel, session.Snapshot().latest );
    session.AdvanceTo( .5 );
    const double later = MeasurementValue( channel, session.Snapshot().latest );
    BOOST_CHECK_GT( early, 0 );
    BOOST_CHECK_GT( later, early + .1 );
    BOOST_CHECK_LT( later, 5 );
}

BOOST_FIXTURE_TEST_CASE( SavedMeasurementProbesPreserveOrderAndComponentSelections, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC );
    file.SetFullName( "measurement_probes.kicad_sch" );
    LoadSchematic( file );
    const auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.probes.size(), 4 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.measurements.size(), 10 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( .02 );
    const auto sample = session.Snapshot().latest;
    std::vector<std::string> probeNames;
    double componentPower = 0, wirePower = 0;
    for( const auto& channel : snapshot.circuit.measurements )
    {
        const double value = MeasurementValue( channel, sample );
        BOOST_CHECK( channel.connected );
        if( channel.probe != NO_SIGNAL )
        {
            probeNames.push_back( channel.name );
            if( channel.quantity == MEASURE_QUANTITY::POWER ) wirePower = value;
        }
        else if( channel.quantity == MEASURE_QUANTITY::POWER ) componentPower += value;
    }
    const std::vector<std::string> expected{ "Supply", "B", "C", "D" };
    BOOST_CHECK( probeNames == expected );
    BOOST_CHECK_CLOSE( componentPower, wirePower, 1e-4 );
    BOOST_CHECK_GT( wirePower, .02 );
}

BOOST_FIXTURE_TEST_CASE( NativeHc00UnitsShareOnePoweredSimulationPackage, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "digital_74hc00.kicad_sch" );
    LoadSchematic( file );
    const auto snapshot = CaptureSchematic( *m_schematic, true );
    BOOST_REQUIRE_EQUAL( snapshot.locations.size(), 8 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.switches.size(), 2 );
    std::size_t component = 0;
    for( std::size_t i = 0; i < snapshot.locations.size(); ++i )
        if( snapshot.locations[i].reference == "U1" ) component = i;
    std::size_t physicalPins = 0;
    for( const auto& pin : snapshot.pins ) if( pin.component == component ) ++physicalPins;
    BOOST_CHECK_EQUAL( physicalPins, 14 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config ); session.AdvanceTo( .02 );
    auto level = [&]( const char* number )
    {
        for( const auto& pin : snapshot.circuit.logicPins )
            if( pin.component == component && pin.pin == number ) return PinLogicLevel( pin, session.Snapshot().latest );
        throw std::runtime_error( "Missing HC00 pin" );
    };
    BOOST_CHECK( level( "1" ) == LOGIC_LEVEL::LOW );
    BOOST_CHECK( level( "3" ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( level( "9" ) == LOGIC_LEVEL::HIGH_Z );
    BOOST_CHECK( level( "8" ) == LOGIC_LEVEL::UNKNOWN );
    for( const auto& sw : snapshot.circuit.switches ) session.SetSourceValue( sw.controlSource, sw.off );
    session.AdvanceTo( .04 );
    BOOST_CHECK( level( "1" ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( level( "2" ) == LOGIC_LEVEL::HIGH );
    BOOST_CHECK( level( "3" ) == LOGIC_LEVEL::LOW );
}

BOOST_FIXTURE_TEST_CASE( NativeAcRectifierUsesStandardSourceDiodeAndScope, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "analog_rectifier.kicad_sch" );
    LoadSchematic( file );
    const auto snapshot = CaptureSchematic( *m_schematic, true );
    BOOST_REQUIRE_EQUAL( snapshot.locations.size(), 4 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.measurements.size(), 5 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config ); session.AdvanceTo( .1 );
    bool scope = false;
    for( const auto& channel : snapshot.circuit.measurements )
        if( channel.scope )
        {
            scope = true;
            const double voltage = MeasurementValue( channel, session.Snapshot().latest );
            BOOST_CHECK( channel.connected ); BOOST_CHECK_GT( voltage, 3 ); BOOST_CHECK_LT( voltage, 5 );
        }
    BOOST_CHECK( scope );
}

BOOST_FIXTURE_TEST_CASE( Native555DemoHasConnectedScopeChannelsAndOscillates, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "ne555_astable.kicad_sch" );
    LoadSchematic( file ); const auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.probes.size(), 2 );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.measurements.size(), 5 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config );
    session.AdvanceTo( .1 );
    for( const auto& channel : snapshot.circuit.measurements )
        if( channel.scope )
        {
            BOOST_CHECK( channel.connected );
            const double value = MeasurementValue( channel, session.Snapshot().latest );
            BOOST_CHECK_GT( value, 0 ); BOOST_CHECK_LT( value, 9 );
        }
}

BOOST_FIXTURE_TEST_CASE( NativeRegulatorDemoMeasuresBothOutputs, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "linear_regulators.kicad_sch" );
    LoadSchematic( file ); const auto snapshot = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( snapshot.circuit.measurements.size(), 6 );
    INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
    session.Configure( snapshot.circuit.netlist, snapshot.circuit.config ); session.AdvanceTo( .02 );
    for( const auto& channel : snapshot.circuit.measurements )
        if( channel.quantity == MEASURE_QUANTITY::VOLTAGE )
            BOOST_CHECK_CLOSE( MeasurementValue( channel, session.Snapshot().latest ), 5, 1 );
}

BOOST_FIXTURE_TEST_CASE( PlacementAdapterCopiesUserProfileIntoNativeSymbolFields, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    struct PROFILE_ENV
    {
        std::string old = std::getenv( "CIRCUITALIVE_PROFILE_FILE" ) ? std::getenv( "CIRCUITALIVE_PROFILE_FILE" ) : "";
        std::filesystem::path file = std::filesystem::temp_directory_path() / ( "ca-native-profile-" + std::to_string( getpid() ) + ".json" );
        PROFILE_ENV() { setenv( "CIRCUITALIVE_PROFILE_FILE", file.c_str(), 1 ); }
        ~PROFILE_ENV()
        {
            if( old.empty() ) unsetenv( "CIRCUITALIVE_PROFILE_FILE" ); else setenv( "CIRCUITALIVE_PROFILE_FILE", old.c_str(), 1 );
            std::error_code error; std::filesystem::remove( file, error ); std::filesystem::remove( file.string() + ".lock", error );
        }
    } environment;
    LoadSchematic( wxFileName( CIRCUITALIVE_TEST_SCHEMATIC ) );
    SCH_SYMBOL* cap = nullptr;
    for( auto* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* s = static_cast<SCH_SYMBOL*>( item );
        if( s->GetRef( &m_schematic->CurrentSheet() ) == "C1" ) cap = s;
    }
    BOOST_REQUIRE( cap );
    auto settings = ReadSymbolModelSettings( *cap, m_schematic->CurrentSheet(), "", "" );
    settings.modelFile = "builtin:capacitor"; settings.modelParameters = { { "capacitance", "22u" } };
    settings.modelPins = { { "p", "1" }, { "n", "2" } };
    MODEL_PROFILES().Save( settings );
    auto clear = settings; clear.modelFile.clear(); clear.modelPins.clear(); clear.modelParameters.clear();
    WriteSymbolModelSettings( *cap, clear );
    // Opening/capturing existing schematics must not apply user defaults retroactively.
    BOOST_CHECK( ReadSymbolModelSettings( *cap, m_schematic->CurrentSheet(), "", "" ).modelFile.empty() );
    ApplySavedSymbolModelSettings( *cap, m_schematic->CurrentSheet(), "", "" );
    const auto placed = ReadSymbolModelSettings( *cap, m_schematic->CurrentSheet(), "", "" );
    BOOST_CHECK_EQUAL( placed.modelFile, "builtin:capacitor" );
    BOOST_CHECK_EQUAL( placed.modelParameters.at( "capacitance" ), "22u" );
    BOOST_CHECK( !cap->GetField( "CA.Parameters" )->IsVisible() );
}

BOOST_FIXTURE_TEST_CASE( NativePowerSymbolsGenerateSuppliesAndKeepGlobalNetConnectivity, KI_TEST::SCHEMATIC_TEST_FIXTURE )
{
    wxFileName file( CIRCUITALIVE_TEST_SCHEMATIC ); file.SetFullName( "power_symbol_sources.kicad_sch" );
    LoadSchematic( file );
    auto s = CaptureSchematic( *m_schematic );
    BOOST_REQUIRE_EQUAL( s.locations.size(), 5 );
    BOOST_REQUIRE_EQUAL( s.circuit.currentWarnings.size(), 4 );
    std::map<wxString, std::size_t> parts;
    for( std::size_t i = 0; i < s.locations.size(); ++i ) parts[s.locations[i].reference] = i;
    BOOST_CHECK_EQUAL( s.circuit.pinNodes[parts.at( "R1" )].at( "1" ), s.circuit.pinNodes[parts.at( "R5" )].at( "1" ) );
    for( const auto& [ref, i] : parts ) BOOST_CHECK_EQUAL( s.circuit.pinNodes[i].at( "2" ), "0" );
    auto voltages = [&]( const SCHEMATIC_SNAPSHOT& snapshot )
    {
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( snapshot.circuit.netlist, snapshot.circuit.config ); session.AdvanceTo( .02 );
        std::map<std::string, double> result;
        for( const auto& channel : snapshot.circuit.measurements )
            if( channel.quantity == MEASURE_QUANTITY::VOLTAGE ) result[channel.name] = MeasurementValue( channel, session.Snapshot().latest );
        return result;
    };
    auto v = voltages( s );
    BOOST_CHECK_CLOSE( v.at( "R1" ), 5, 1e-6 ); BOOST_CHECK_CLOSE( v.at( "R5" ), 5, 1e-6 );
    BOOST_CHECK_CLOSE( v.at( "R2" ), 24, 1e-6 ); BOOST_CHECK_CLOSE( v.at( "R3" ), -12, 1e-6 );
    BOOST_CHECK_CLOSE( v.at( "R4" ), 3.3, 1e-6 );
    // Power library ID +12V with edited Value +24V was already resolved above.
    // Editing that Value to +5V must join the existing +5V net, with no duplicated ideal source.
    for( auto* item : m_schematic->RootScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        if( symbol->GetLibId().Format() == "power:+12V" ) symbol->GetField( FIELD_T::VALUE )->SetText( "+5V" );
    }
    m_schematic->ConnectionGraph()->Recalculate( m_schematic->Hierarchy(), true );
    s = CaptureSchematic( *m_schematic );
    BOOST_CHECK_EQUAL( s.circuit.currentWarnings.size(), 3 );
    v = voltages( s ); BOOST_CHECK_CLOSE( v.at( "R2" ), 5, 1e-6 );
    BOOST_CHECK_EQUAL( s.circuit.pinNodes[parts.at( "R1" )].at( "1" ), s.circuit.pinNodes[parts.at( "R2" )].at( "1" ) );
}
