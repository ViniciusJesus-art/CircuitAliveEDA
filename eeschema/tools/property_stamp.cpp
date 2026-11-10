/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "property_stamp.h"
#include <eda_draw_frame.h>
#include <common.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <properties/pg_properties.h>
#include <libeval/numeric_evaluator.h>
#include <sch_collectors.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_field.h>
#include <sch_label.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>
#include <sch_tablecell.h>
#include <schematic.h>
#include <string_utils.h>
#include <validators.h>
#include <wildcards_and_files_ext.h>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace CIRCUITALIVE
{
namespace
{
void Fail( const wxString& message ) { throw std::runtime_error( message.ToStdString() ); }

wxVariant NativeValue( SCH_ITEM& item, PROPERTY_BASE& property, const wxString& text,
                       SCH_EDIT_FRAME* frame )
{
    if( property.TypeHash() == TYPE_HASH( wxString ) && !property.HasChoices() ) return wxVariant( text );
    if( property.TypeHash() == TYPE_HASH( bool ) )
    {
        const auto value = text.Lower();
        if( value == "true" || value == "yes" || value == "1" || value == "sim" ) return wxVariant( true );
        if( value == "false" || value == "no" || value == "0" || value == "não" || value == "nao" )
            return wxVariant( false );
        Fail( _( "Use true/false or 1/0 for this property." ) );
    }
    if( property.HasChoices() )
    {
        property.TranslateChoices();
        wxPGChoices choices = property.GetChoices( &item );
        if( choices.GetCount() == 0 ) choices = property.Choices();
        for( unsigned i = 0; i < choices.GetCount(); ++i )
            if( choices.GetLabel( i ).CmpNoCase( text ) == 0 )
                return property.TypeHash() == TYPE_HASH( wxString ) ? wxVariant( choices.GetLabel( i ) )
                    : wxVariant( long( choices.GetValue( i ) ) );
        Fail( _( "Use one of this property's choices from the Properties panel." ) );
    }
    if( property.Display() == PT_DEFAULT )
    {
        if( property.TypeHash() == TYPE_HASH( int ) || property.TypeHash() == TYPE_HASH( long ) )
        {
            long value;
            if( text.ToLong( &value ) && ( property.TypeHash() != TYPE_HASH( int )
                || ( value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max() ) ) )
                return wxVariant( value );
            Fail( _( "Enter a valid integer for this property." ) );
        }
        if( property.TypeHash() == TYPE_HASH( unsigned ) || property.TypeHash() == TYPE_HASH( unsigned long ) )
        {
            unsigned long value;
            if( !text.StartsWith( "-" ) && text.ToULong( &value )
                && value <= std::numeric_limits<unsigned>::max()
                && value <= static_cast<unsigned long>( std::numeric_limits<long>::max() ) )
                return wxVariant( static_cast<long>( value ) );
            Fail( _( "Enter a nonnegative integer for this property." ) );
        }
        if( property.TypeHash() == TYPE_HASH( double ) || property.TypeHash() == TYPE_HASH( float ) )
        {
            double value;
            if( text.ToDouble( &value ) && std::isfinite( value ) ) return wxVariant( value );
            Fail( _( "Enter a valid number for this property." ) );
        }
    }
    const auto type = property.TypeHash();
    const bool optionalInt = type == TYPE_HASH( std::optional<int> );
    const bool optionalDouble = type == TYPE_HASH( std::optional<double> );
    if( text.IsEmpty() && optionalInt )
        return wxVariant( new STD_OPTIONAL_INT_VARIANT_DATA( std::nullopt ) );
    if( text.IsEmpty() && optionalDouble )
        return wxVariant( new STD_OPTIONAL_DOUBLE_VARIANT_DATA( std::nullopt ) );
    if( property.Display() == PT_DEFAULT && ( optionalInt || optionalDouble ) )
    {
        double number;
        if( !text.ToDouble( &number ) || !std::isfinite( number ) )
            Fail( _( "Enter a valid number for this property." ) );
        if( optionalDouble ) return wxVariant( new STD_OPTIONAL_DOUBLE_VARIANT_DATA( number ) );
        if( number != std::trunc( number ) || number < std::numeric_limits<int>::min()
            || number > std::numeric_limits<int>::max() ) Fail( _( "Enter a valid integer for this property." ) );
        return wxVariant( new STD_OPTIONAL_INT_VARIANT_DATA( int( number ) ) );
    }
    if( property.Display() != PT_DEFAULT && property.Display() != PT_NET )
    {
        if( !frame ) Fail( _( "This property requires the editor's unit settings." ) );
        const auto display = property.Display();
        const bool angle = display == PT_DEGREE || display == PT_DECIDEGREE;
        const auto units = angle ? EDA_UNITS::DEGREES : display == PT_TIME ? EDA_UNITS::PS
            : display == PT_RATIO ? EDA_UNITS::UNSCALED : frame->GetUserUnits();
        NUMERIC_EVALUATOR evaluator( units );
        double number;
        if( !evaluator.Process( text ) || !evaluator.Result().ToDouble( &number ) || !std::isfinite( number ) )
            Fail( _( "Enter a valid number and units for this property." ) );
        if( angle )
        {
            if( type == TYPE_HASH( EDA_ANGLE ) ) return wxVariant( new EDA_ANGLE_VARIANT_DATA( number ) );
            if( display == PT_DECIDEGREE ) number *= 10;
        }
        else if( display != PT_RATIO )
        {
            number = EDA_UNIT_UTILS::UI::FromUserUnit( frame->GetIuScale(), units, number );
            if( display == PT_AREA ) number = EDA_UNIT_UTILS::UI::FromUserUnit( frame->GetIuScale(), units, number );
            if( display == PT_COORD ) number = frame->GetOriginTransforms().FromDisplay( number, property.CoordType() );
        }
        if( !std::isfinite( number ) ) Fail( _( "The value is outside this property's range." ) );
        if( display == PT_SIZE && number < 0 ) Fail( _( "Sizes cannot be negative." ) );
        if( type == TYPE_HASH( double ) ) return wxVariant( number );
        if( optionalDouble ) return wxVariant( new STD_OPTIONAL_DOUBLE_VARIANT_DATA( number ) );
        if( type == TYPE_HASH( long long ) && number >= double( std::numeric_limits<long long>::min() )
            && number < double( std::numeric_limits<long long>::max() ) )
            return wxVariant( wxLongLong( std::llround( number ) ) );
        if( number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max() )
            Fail( _( "The value is outside this property's range." ) );
        if( optionalInt ) return wxVariant( new STD_OPTIONAL_INT_VARIANT_DATA( int( std::lround( number ) ) ) );
        return wxVariant( long( std::lround( number ) ) );
    }
    // Color and other string-backed native editors provide their own conversion.
    std::unique_ptr<wxPGProperty> editor( PGPropertyFactory( &property, frame ) );
    wxVariant value;
    if( !editor || !editor->StringToValue( value, text ) ) Fail( _( "Invalid value for this property." ) );
    return value;
}

FIELD_T StandardField( const wxString& name )
{
    if( name.CmpNoCase( "Reference" ) == 0 ) return FIELD_T::REFERENCE;
    if( name.CmpNoCase( "Value" ) == 0 ) return FIELD_T::VALUE;
    if( name.CmpNoCase( "Footprint" ) == 0 ) return FIELD_T::FOOTPRINT;
    if( name.CmpNoCase( "Datasheet" ) == 0 ) return FIELD_T::DATASHEET;
    if( name.CmpNoCase( "Description" ) == 0 ) return FIELD_T::DESCRIPTION;
    return FIELD_T::USER;
}
}

PROPERTY_STAMP_SEQUENCE::PROPERTY_STAMP_SEQUENCE( const PROPERTY_STAMP_SETTINGS& settings ) :
    m_settings( settings ), m_number( settings.start )
{
    if( settings.numbering && !HasPlaceholder( settings.pattern ) )
        Fail( _( "Include # or ? in the pattern to use automatic numbering." ) );
}

bool PROPERTY_STAMP_SEQUENCE::HasPlaceholder( const wxString& pattern )
{
    return pattern.Contains( "#" ) || pattern.Contains( "?" );
}

wxString PROPERTY_STAMP_SEQUENCE::Current() const
{
    if( m_exhausted ) throw std::overflow_error( "Numbering sequence has ended" );
    if( !m_settings.numbering ) return m_settings.pattern;
    wxString value = m_settings.pattern;
    const auto number = wxString::Format( "%lld", static_cast<long long>( m_number ) );
    value.Replace( "#", number ); value.Replace( "?", number );
    return value;
}

bool PROPERTY_STAMP_SEQUENCE::Advance()
{
    if( !m_settings.numbering ) return true;
    if( m_exhausted ) return false;
    const auto step = m_settings.step;
    if( ( step > 0 && m_number > std::numeric_limits<std::int64_t>::max() - step )
        || ( step < 0 && m_number < std::numeric_limits<std::int64_t>::min() - step ) )
    {
        m_exhausted = true;
        return false;
    }
    m_number += step;
    return true;
}

PROPERTY_STAMP_EDIT PlanPropertyStamp( SCH_ITEM& clicked, SCH_SHEET_PATH& sheet,
    const PROPERTY_STAMP_SETTINGS& settings, const wxString& value, SCH_EDIT_FRAME* frame )
{
    SCH_ITEM* item = &clicked;
    auto& manager = PROPERTY_MANAGER::Instance();
    const bool native = settings.property.StartsWith( "native:" );
    wxString key = native ? settings.property.Mid( 7 ) : settings.property;
    if( auto* pin = dynamic_cast<SCH_PIN*>( item ) ) item = pin->GetParentSymbol();
    if( auto* field = dynamic_cast<SCH_FIELD*>( item ) )
    {
        auto* parent = dynamic_cast<SCH_ITEM*>( field->GetParent() );
        if( parent && ( !native || manager.GetProperty( TYPE_HASH( *parent ), key ) ) ) item = parent;
    }
    if( !item ) Fail( _( "Click an editable schematic item." ) );

    PROPERTY_STAMP_EDIT edit;
    edit.item = edit.owner = item;
    if( item->Type() == SCH_SHEET_PIN_T || item->Type() == SCH_TABLECELL_T || item->Type() == SCH_FIELD_T )
        edit.owner = dynamic_cast<SCH_ITEM*>( item->GetParent() );

    auto* symbol = dynamic_cast<SCH_SYMBOL*>( item );
    auto* label = dynamic_cast<SCH_LABEL_BASE*>( item );
    auto* subsheet = dynamic_cast<SCH_SHEET*>( item );
    if( !native && ( key == "name" || ( key == "Value" && !symbol ) ) )
    {
        if( symbol ) key = "Value";
        else if( label || dynamic_cast<SCH_TEXT*>( item ) ) key = "native:Text";
        else if( subsheet ) key = "Sheet name";
        else key = "native:Name";
    }

    bool explicitField = key == "field" || key.StartsWith( "field:" );
    wxString fieldName = key == "field" ? settings.customField : key.Mid( 6 );
    if( subsheet && explicitField )
    {
        if( fieldName.CmpNoCase( GetCanonicalFieldName( FIELD_T::SHEET_FILENAME ) ) == 0 )
        { key = "Sheet file"; explicitField = false; }
        else if( fieldName.CmpNoCase( GetCanonicalFieldName( FIELD_T::SHEET_NAME ) ) == 0 )
        { key = "Sheet name"; explicitField = false; }
    }
    auto fieldId = StandardField( key );
    if( explicitField && fieldId == FIELD_T::USER ) fieldId = StandardField( fieldName );
    if( symbol && ( fieldId != FIELD_T::USER || explicitField ) )
    {
        if( fieldId != FIELD_T::USER ) fieldName = GetCanonicalFieldName( fieldId );
        if( fieldName.IsEmpty() ) Fail( _( "Enter the custom field's name." ) );
        if( ::IsGeneratedField( fieldName ) ) Fail( _( "Generated fields are read-only." ) );
        if( auto error = GetFieldValidationErrorMessage( fieldId, value ); !error.IsEmpty() ) Fail( error );
        auto* existingField = fieldId == FIELD_T::USER ? symbol->GetField( fieldName ) : symbol->GetField( fieldId );
        if( !existingField && !settings.createFields ) Fail( _( "This component does not have the selected field." ) );
        std::vector<std::pair<SCH_SYMBOL*, SCH_SHEET_PATH>> targets{ { symbol, sheet } };
        if( symbol->Schematic() && symbol->GetUnitCount() > 1 && symbol->IsAnnotated( &sheet ) )
            for( auto path : symbol->Schematic()->Hierarchy() )
            {
                std::vector<SCH_SYMBOL*> units;
                CollectOtherUnits( symbol->GetRef( &sheet ), symbol->GetUnit(), symbol->GetLibId(), path, &units );
                for( auto* unit : units ) targets.emplace_back( unit, path );
            }
        if( fieldId == FIELD_T::REFERENCE )
        {
            if( symbol->Schematic() && value != symbol->GetRef( &sheet ) )
                for( auto path : symbol->Schematic()->Hierarchy() )
                    for( SCH_ITEM* other : path.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
                        if( ( other != symbol || path != sheet )
                            && static_cast<SCH_SYMBOL*>( other )->GetRef( &path ) == value )
                            Fail( _( "This component reference is already in use." ) );
        }
        edit.property = manager.GetProperty( TYPE_HASH( SCH_SYMBOL ),
            fieldId == FIELD_T::USER ? fieldName : GetCanonicalFieldName( fieldId ) );
        edit.syncUnits = false; // Synchronize only this field, including references, below.
        edit.apply = [symbol, targets, fieldName, fieldId, value]( SCH_COMMIT* commit ) mutable
        {
            for( auto& [target, path] : targets )
            {
                if( target != symbol && commit ) commit->Modify( target, path.LastScreen() );
                if( fieldId == FIELD_T::REFERENCE ) target->SetRef( &path, value );
                else
                {
                    auto* field = fieldId == FIELD_T::USER ? target->GetField( fieldName ) : target->GetField( fieldId );
                    const wxString variant = symbol->Schematic() ? symbol->Schematic()->GetCurrentVariant() : wxString();
                    if( !field )
                    {
                        SCH_FIELD added( target, FIELD_T::USER, fieldName );
                        added.SetPosition( target->GetPosition() );
                        added.SetVisible( false );
                        target->AddField( added )->SetText( value, &path, variant );
                    }
                    else field->SetText( value, &path, variant );
                }
            }
            return true;
        };
        return edit;
    }
    if( explicitField && ( label || subsheet ) )
    {
        if( fieldName.IsEmpty() ) Fail( _( "Enter the custom field's name." ) );
        if( ::IsGeneratedField( fieldName ) ) Fail( _( "Generated fields are read-only." ) );
        auto& existingFields = label ? label->GetFields() : subsheet->GetFields();
        bool exists = false;
        for( const auto& field : existingFields ) exists |= field.GetName() == fieldName;
        if( !exists && !settings.createFields ) Fail( _( "This item does not have the selected field." ) );
        edit.apply = [label, subsheet, fieldName, value]( SCH_COMMIT* )
        {
            auto& fields = label ? label->GetFields() : subsheet->GetFields();
            for( auto& field : fields ) if( field.GetName() == fieldName ) { field.SetText( value ); return true; }
            SCH_FIELD added( label ? static_cast<SCH_ITEM*>( label ) : subsheet, FIELD_T::USER, fieldName );
            added.SetPosition( label ? label->GetPosition() : subsheet->GetPosition() );
            added.SetVisible( false );
            added.SetText( value ); fields.push_back( added ); return true;
        };
        return edit;
    }
    if( subsheet && !native && ( key == "Sheet name" || key == "Sheet file" ) )
    {
        if( value.IsEmpty() ) Fail( _( "A sheet must have a name." ) );
        if( key == "Sheet file" )
        {
            if( !frame ) Fail( _( "Changing sheet files requires the schematic editor." ) );
            edit.apply = [subsheet, frame, value, undoable = edit.undoable]( SCH_COMMIT* )
            {
                const auto filename = EnsureFileExtension( value, FILEEXT::KiCadSchematicFileExtension );
                if( !IsFullFileNameValid( filename ) ) Fail( _( "Invalid sheet filename." ) );
                SCH_SHEET_LIST originalHierarchy;
                originalHierarchy.BuildSheetList( &frame->Schematic().Root(), true );
                bool clearAnnotation = false;
                if( !frame->ChangeSheetFile( subsheet, filename, &clearAnnotation, undoable.get() ) ) return false;
                subsheet->SetFileName( filename );
                if( clearAnnotation )
                {
                    SCH_SCREENS screens( frame->Schematic().Root() );
                    screens.ClearAnnotationOfNewSheetPaths( originalHierarchy );
                    subsheet->GetScreen()->ClearAnnotation( &frame->GetCurrentSheet(), false );
                }
                return true;
            };
        }
        else
        {
            if( auto error = GetFieldValidationErrorMessage( FIELD_T::SHEET_NAME, value ); !error.IsEmpty() ) Fail( error );
            edit.apply = [subsheet, value]( SCH_COMMIT* ) { subsheet->SetName( value ); return true; };
        }
        return edit;
    }

    if( key.StartsWith( "native:" ) ) key = key.Mid( 7 );
    else if( !native && key != "Text" ) Fail( _( "This property does not apply to this item." ) );
    auto* property = manager.GetProperty( TYPE_HASH( *item ), key );
    if( !property || !manager.IsAvailableFor( TYPE_HASH( *item ), property, item )
        || !manager.IsWriteableFor( TYPE_HASH( *item ), property, item ) )
        Fail( _( "This property is unavailable or read-only on this item." ) );
    wxString text = value;
    if( label && key == "Text" )
    {
        if( value.IsEmpty() || value.Contains( '\n' ) || value.Contains( '\r' ) )
            Fail( _( "Enter a nonempty, single-line label name." ) );
        text = EscapeString( value, CTX_NETNAME );
    }
    wxVariant converted = NativeValue( *item, *property, text, frame );
    if( auto invalid = property->Validate( converted.GetAny(), item ) )
        Fail( invalid->get()->Format( frame ) );
    edit.property = property;
    edit.apply = [item, property, converted]( SCH_COMMIT* ) { return item->Set( property, converted ); };
    return edit;
}
}
