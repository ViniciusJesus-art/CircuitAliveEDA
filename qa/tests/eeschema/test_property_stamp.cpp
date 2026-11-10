/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <tools/property_stamp.h>
#include <lib_symbol.h>
#include <sch_field.h>
#include <sch_label.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <settings/settings_manager.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <limits>

using namespace CIRCUITALIVE;

BOOST_AUTO_TEST_SUITE( PropertyStamp )

BOOST_AUTO_TEST_CASE( LiteralAndNumberedPatterns )
{
    PROPERTY_STAMP_SETTINGS settings;
    settings.pattern = "Digital_D#_?";
    PROPERTY_STAMP_SEQUENCE literal( settings );
    BOOST_CHECK_EQUAL( literal.Current(), "Digital_D#_?" );
    BOOST_CHECK( literal.Advance() );
    BOOST_CHECK_EQUAL( literal.Current(), "Digital_D#_?" );
    settings.numbering = true; settings.start = 7; settings.step = 3;
    PROPERTY_STAMP_SEQUENCE numbered( settings );
    BOOST_CHECK_EQUAL( numbered.Current(), "Digital_D7_7" );
    BOOST_CHECK( numbered.Advance() );
    BOOST_CHECK_EQUAL( numbered.Current(), "Digital_D10_10" );
    settings.pattern = "Digital";
    BOOST_CHECK_THROW( PROPERTY_STAMP_SEQUENCE{ settings }, std::runtime_error );
}

BOOST_AUTO_TEST_CASE( DecrementZeroAndOverflow )
{
    PROPERTY_STAMP_SETTINGS settings;
    settings.pattern = "D?"; settings.numbering = true; settings.start = 1; settings.step = -2;
    PROPERTY_STAMP_SEQUENCE descending( settings );
    BOOST_CHECK_EQUAL( descending.Current(), "D1" );
    BOOST_CHECK( descending.Advance() );
    BOOST_CHECK_EQUAL( descending.Current(), "D-1" );
    settings.step = 0;
    PROPERTY_STAMP_SEQUENCE fixed( settings );
    BOOST_CHECK( fixed.Advance() ); BOOST_CHECK_EQUAL( fixed.Current(), "D1" );
    settings.start = std::numeric_limits<std::int64_t>::max(); settings.step = 1;
    PROPERTY_STAMP_SEQUENCE upper( settings );
    BOOST_CHECK_EQUAL( upper.Current(), "D9223372036854775807" );
    BOOST_CHECK( !upper.Advance() );
    BOOST_CHECK_THROW( upper.Current(), std::overflow_error );
    settings.start = std::numeric_limits<std::int64_t>::min(); settings.step = -1;
    PROPERTY_STAMP_SEQUENCE lower( settings );
    BOOST_CHECK_EQUAL( lower.Current(), "D-9223372036854775808" );
    BOOST_CHECK( !lower.Advance() );
    settings.start = 0; settings.step = std::numeric_limits<std::int64_t>::min();
    PROPERTY_STAMP_SEQUENCE largeStep( settings );
    BOOST_CHECK( largeStep.Advance() );
    BOOST_CHECK_EQUAL( largeStep.Current(), "D-9223372036854775808" );
    BOOST_CHECK( !largeStep.Advance() );
}

struct STAMP_FIXTURE
{
    STAMP_FIXTURE()
    {
        directory = wxFileName::CreateTempFileName( wxFileName::GetTempDir() + "/ca-stamp-" );
        wxRemoveFile( directory ); wxFileName::Mkdir( directory );
        const auto project = wxFileName( directory, "stamp.kicad_pro" ).GetFullPath();
        wxFFile file( project, "w" ); file.Write( "{}" ); file.Close();
        manager.LoadProject( project );
        schematic = std::make_unique<SCHEMATIC>( &manager.Prj() );
        root = new SCH_SHEET( schematic.get() );
        root->SetScreen( new SCH_SCREEN( schematic.get() ) );
        schematic->SetTopLevelSheets( { root } );
        schematic->RefreshHierarchy(); path.push_back( root );
        schematic->SetCurrentSheet( path );
    }
    ~STAMP_FIXTURE()
    {
        schematic.reset(); manager.UnloadProject( &manager.Prj(), false );
        wxFileName::Rmdir( directory, wxPATH_RMDIR_RECURSIVE );
    }
    SCH_SYMBOL* Symbol( const wxString& reference = "U1", int unit = 1, int units = 1 )
    {
        LIB_SYMBOL lib( "Chip" ); lib.SetUnitCount( units, false );
        lib.GetReferenceField().SetText( "U" );
        auto* symbol = new SCH_SYMBOL( lib, LIB_ID( "Test", "Chip" ), &path, unit, 0,
                                      VECTOR2I( unit * 10000, 10000 ), schematic.get() );
        symbol->SetRef( &path, reference ); root->GetScreen()->Append( symbol );
        return symbol;
    }
    bool Apply( SCH_ITEM& item, const wxString& property, const wxString& value )
    {
        settings.property = property;
        return PlanPropertyStamp( item, path, settings, value ).apply( nullptr );
    }
    wxString directory;
    SETTINGS_MANAGER manager;
    std::unique_ptr<SCHEMATIC> schematic;
    SCH_SHEET* root;
    SCH_SHEET_PATH path;
    PROPERTY_STAMP_SETTINGS settings;
};

BOOST_FIXTURE_TEST_CASE( SymbolFieldsAndCustomFieldCreation, STAMP_FIXTURE )
{
    auto* symbol = Symbol();
    const auto original = symbol->GetValue( false, &path, false, wxEmptyString );
    auto plan = PlanPropertyStamp( *symbol, path, settings, "Digital" );
    BOOST_CHECK_EQUAL( symbol->GetValue( false, &path, false, wxEmptyString ), original ); // Planning never mutates.
    BOOST_CHECK( plan.apply( nullptr ) );
    BOOST_CHECK_EQUAL( symbol->GetValue( false, &path, false, wxEmptyString ), "Digital" );
    BOOST_CHECK( Apply( *symbol, "Footprint", "Package:SOIC-14" ) );
    BOOST_CHECK_EQUAL( symbol->GetField( FIELD_T::FOOTPRINT )->GetText( &path ), "Package:SOIC-14" );
    settings.property = "field"; settings.customField = "Category";
    settings.createFields = false;
    BOOST_CHECK_THROW( PlanPropertyStamp( *symbol, path, settings, "Digital" ), std::runtime_error );
    BOOST_CHECK( !symbol->GetField( "Category" ) );
    settings.createFields = true;
    BOOST_CHECK( PlanPropertyStamp( *symbol, path, settings, "Digital" ).apply( nullptr ) );
    BOOST_REQUIRE( symbol->GetField( "Category" ) );
    BOOST_CHECK_EQUAL( symbol->GetField( "Category" )->GetText( &path ), "Digital" );
    BOOST_CHECK( !symbol->GetField( "Category" )->IsVisible() );
    BOOST_CHECK( symbol->GetField( "Category" )->GetPosition() == symbol->GetPosition() );
    BOOST_CHECK( Apply( *symbol, "field:Category", "Analog" ) );
    BOOST_CHECK_EQUAL( symbol->GetField( "Category" )->GetText( &path ), "Analog" );
}

BOOST_FIXTURE_TEST_CASE( ReferenceValidationIncludesAliases, STAMP_FIXTURE )
{
    auto* first = Symbol(); auto* second = Symbol( "U2" );
    settings.property = "Reference";
    BOOST_CHECK_THROW( PlanPropertyStamp( *first, path, settings, "U2" ), std::runtime_error );
    BOOST_CHECK_EQUAL( first->GetRef( &path ), "U1" );
    settings.property = "field"; settings.customField = "Reference";
    BOOST_CHECK_THROW( PlanPropertyStamp( *first, path, settings, "U2" ), std::runtime_error );
    BOOST_CHECK_THROW( PlanPropertyStamp( *first, path, settings, "123" ), std::runtime_error );
    settings.customField = "reference";
    BOOST_CHECK_THROW( PlanPropertyStamp( *first, path, settings, "U2" ), std::runtime_error );
    settings.property = "native:Reference";
    BOOST_CHECK_THROW( PlanPropertyStamp( *first, path, settings, "U2" ), std::runtime_error );
    BOOST_CHECK( Apply( *first, "Reference", "U9" ) );
    BOOST_CHECK_EQUAL( first->GetRef( &path ), "U9" );
    BOOST_CHECK_EQUAL( second->GetRef( &path ), "U2" );
}

BOOST_FIXTURE_TEST_CASE( NameWorksAcrossTextLabelsSheetsAndSymbols, STAMP_FIXTURE )
{
    SCH_LABEL label( VECTOR2I( 0, 0 ), "Old" );
    SCH_TEXT text( VECTOR2I( 0, 0 ), "Old" );
    SCH_SHEET sheet( schematic.get() );
    BOOST_CHECK( Apply( label, "name", "Digital_D7" ) );
    BOOST_CHECK_EQUAL( label.GetText(), "Digital_D7" );
    BOOST_CHECK( Apply( text, "name", "Digital" ) );
    BOOST_CHECK_EQUAL( text.GetText(), "Digital" );
    BOOST_CHECK( Apply( sheet, "name", "Digital" ) );
    BOOST_CHECK_EQUAL( sheet.GetName(), "Digital" );
    auto* symbol = Symbol();
    BOOST_CHECK( Apply( *symbol, "name", "Digital" ) );
    BOOST_CHECK_EQUAL( symbol->GetValue( false, &path, false, wxEmptyString ), "Digital" );
    settings.property = "name";
    BOOST_CHECK_THROW( PlanPropertyStamp( label, path, settings, "" ), std::runtime_error );
    BOOST_CHECK_THROW( PlanPropertyStamp( label, path, settings, "a\nb" ), std::runtime_error );
}

BOOST_FIXTURE_TEST_CASE( ReferencesCannotDuplicateAnotherInstanceOfTheSameScreen, STAMP_FIXTURE )
{
    auto* symbol = Symbol();
    auto* instance = new SCH_SHEET( schematic.get() );
    instance->SetScreen( root->GetScreen() );
    schematic->SetTopLevelSheets( { root, instance } );
    schematic->RefreshHierarchy();
    SCH_SHEET_PATH otherPath; otherPath.push_back( instance );
    symbol->SetRef( &otherPath, "U2" );
    settings.property = "Reference";
    BOOST_CHECK_THROW( PlanPropertyStamp( *symbol, path, settings, "U2" ), std::runtime_error );
    BOOST_CHECK_EQUAL( symbol->GetRef( &path ), "U1" );
    BOOST_CHECK_EQUAL( symbol->GetRef( &otherPath ), "U2" );
}

BOOST_FIXTURE_TEST_CASE( NativeBooleanChoicesAndReadonlyProperties, STAMP_FIXTURE )
{
    SCH_TEXT text( VECTOR2I( 0, 0 ), "Text" );
    BOOST_CHECK( Apply( text, "native:Bold", "true" ) ); BOOST_CHECK( text.IsBold() );
    BOOST_CHECK( Apply( text, "native:Bold", "0" ) ); BOOST_CHECK( !text.IsBold() );
    settings.property = "native:Bold";
    BOOST_CHECK_THROW( PlanPropertyStamp( text, path, settings, "maybe" ), std::runtime_error );
    auto* symbol = Symbol( "U1", 1, 2 );
    BOOST_CHECK( Apply( *symbol, "native:Unit", "B" ) );
    BOOST_CHECK_EQUAL( symbol->GetUnit(), 2 );
    settings.property = "native:Unit";
    BOOST_CHECK_THROW( PlanPropertyStamp( *symbol, path, settings, "Z" ), std::runtime_error );
    settings.property = "native:Library Link";
    BOOST_CHECK_THROW( PlanPropertyStamp( *symbol, path, settings, "Unknown:Part" ), std::runtime_error );
    settings.property = "Footprint";
    BOOST_CHECK_THROW( PlanPropertyStamp( text, path, settings, "Digital" ), std::runtime_error );
}

BOOST_FIXTURE_TEST_CASE( FieldsAndSheetPinsUseTheirUndoOwner, STAMP_FIXTURE )
{
    auto* symbol = Symbol();
    auto plan = PlanPropertyStamp( *symbol->GetField( FIELD_T::VALUE ), path, settings, "Digital" );
    BOOST_CHECK( plan.owner == symbol ); BOOST_CHECK( plan.apply( nullptr ) );
    BOOST_CHECK_EQUAL( symbol->GetValue( false, &path, false, wxEmptyString ), "Digital" );
    SCH_SHEET sheet( schematic.get() ); SCH_SHEET_PIN pin( &sheet );
    settings.property = "name";
    auto pinPlan = PlanPropertyStamp( pin, path, settings, "DATA" );
    BOOST_CHECK( pinPlan.owner == &sheet ); BOOST_CHECK( pinPlan.apply( nullptr ) );
    BOOST_CHECK_EQUAL( pin.GetText(), "DATA" );
}

BOOST_FIXTURE_TEST_CASE( MultiunitEditsSynchronizeOnlyTheChosenField, STAMP_FIXTURE )
{
    auto* first = Symbol( "U1", 1, 2 ); auto* second = Symbol( "U1", 2, 2 );
    second->GetField( FIELD_T::DATASHEET )->SetText( "keep.pdf" );
    BOOST_CHECK( Apply( *first, "Footprint", "Package:SOIC-14" ) );
    BOOST_CHECK_EQUAL( second->GetField( FIELD_T::FOOTPRINT )->GetText( &path ), "Package:SOIC-14" );
    BOOST_CHECK_EQUAL( second->GetField( FIELD_T::DATASHEET )->GetText( &path ), "keep.pdf" );
    BOOST_CHECK( Apply( *first, "field:Category", "Digital" ) );
    BOOST_REQUIRE( second->GetField( "Category" ) );
    BOOST_CHECK_EQUAL( second->GetField( "Category" )->GetText( &path ), "Digital" );
    BOOST_CHECK( Apply( *first, "Reference", "U8" ) );
    BOOST_CHECK_EQUAL( first->GetRef( &path ), "U8" );
    BOOST_CHECK_EQUAL( second->GetRef( &path ), "U8" );
}

BOOST_FIXTURE_TEST_CASE( VariantFieldEditsPreserveDefaultValues, STAMP_FIXTURE )
{
    auto* first = Symbol( "U1", 1, 2 ); auto* second = Symbol( "U1", 2, 2 );
    BOOST_CHECK( Apply( *first, "Value", "Default" ) );
    schematic->AddVariant( "Digital" ); schematic->SetCurrentVariant( "Digital" );
    BOOST_CHECK( Apply( *first, "Value", "Variant" ) );
    BOOST_CHECK( Apply( *first, "field:Category", "Logic" ) );
    for( auto* symbol : { first, second } )
    {
        BOOST_CHECK_EQUAL( symbol->GetField( FIELD_T::VALUE )->GetText( &path ), "Default" );
        BOOST_CHECK_EQUAL( symbol->GetField( FIELD_T::VALUE )->GetText( &path, "Digital" ), "Variant" );
        BOOST_REQUIRE( symbol->GetField( "Category" ) );
        BOOST_CHECK_EQUAL( symbol->GetField( "Category" )->GetText( &path ), "" );
        BOOST_CHECK_EQUAL( symbol->GetField( "Category" )->GetText( &path, "Digital" ), "Logic" );
    }
}

BOOST_FIXTURE_TEST_CASE( LabelFieldsAndGeneratedFieldValidation, STAMP_FIXTURE )
{
    SCH_LABEL label( VECTOR2I( 1000, 2000 ), "DATA" );
    BOOST_CHECK( Apply( label, "field:Category", "Digital" ) );
    BOOST_REQUIRE_EQUAL( label.GetFields().size(), 1u );
    BOOST_CHECK_EQUAL( label.GetFields().front().GetText(), "Digital" );
    BOOST_CHECK( !label.GetFields().front().IsVisible() );
    BOOST_CHECK( label.GetFields().front().GetPosition() == label.GetPosition() );
    settings.property = "field:${DNP}";
    BOOST_CHECK_THROW( PlanPropertyStamp( label, path, settings, "value" ), std::runtime_error );
    BOOST_REQUIRE_EQUAL( label.GetFields().size(), 1u );
    auto* symbol = Symbol();
    BOOST_CHECK_THROW( PlanPropertyStamp( *symbol, path, settings, "value" ), std::runtime_error );
    settings.property = "Sheet name";
    SCH_SHEET sheet( schematic.get() );
    BOOST_CHECK_THROW( PlanPropertyStamp( sheet, path, settings, "a/b" ), std::runtime_error );
}

BOOST_AUTO_TEST_SUITE_END()
