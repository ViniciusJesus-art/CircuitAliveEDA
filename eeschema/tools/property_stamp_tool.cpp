/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "property_stamp.h"
#include <tools/sch_actions.h>
#include <tools/sch_tool_base.h>
#include <tools/ee_grid_helper.h>
#include <widgets/used_symbols.h>
#include <dialog_shim.h>
#include <eeschema_settings.h>
#include <kiplatform/ui.h>
#include <properties/property.h>
#include <properties/property_mgr.h>
#include <sch_collectors.h>
#include <sch_draw_panel.h>
#include <sch_field.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_symbol.h>
#include <schematic.h>
#include <status_popup.h>
#include <string_utils.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <set>

namespace CIRCUITALIVE
{
namespace
{
struct PROPERTY_CHOICE { wxString key, label; };

std::vector<PROPERTY_CHOICE> PropertyChoices( SCH_EDIT_FRAME& frame )
{
    std::vector<PROPERTY_CHOICE> choices = {
        { "name", _( "Name / text" ) }, { "Reference", _( "Reference" ) },
        { "Value", _( "Value" ) }, { "Footprint", _( "Footprint" ) },
        { "Datasheet", _( "Datasheet" ) }, { "Description", _( "Description" ) },
        { "Sheet name", _( "Sheet name" ) }, { "Sheet file", _( "Sheet file" ) },
        { "field", _( "Custom field..." ) }
    };
    std::set<wxString> properties, fields;
    auto& manager = PROPERTY_MANAGER::Instance();
    auto inspect = [&]( SCH_ITEM* item )
    {
        for( auto* property : manager.GetProperties( TYPE_HASH( *item ) ) )
            if( !property->IsHiddenFromPropertiesManager() && !property->IsHiddenFromDesignEditors()
                && manager.IsAvailableFor( TYPE_HASH( *item ), property, item )
                && manager.IsWriteableFor( TYPE_HASH( *item ), property, item ) )
                properties.insert( property->Name() );
    };
    for( SCH_ITEM* item : frame.GetScreen()->Items() )
    {
        inspect( item );
        item->RunOnChildren( inspect, RECURSE_MODE::NO_RECURSE );
        if( auto* symbol = dynamic_cast<SCH_SYMBOL*>( item ) )
            for( const auto& field : symbol->GetFields() )
                if( field.GetId() == FIELD_T::USER ) fields.insert( field.GetName() );
        if( auto* label = dynamic_cast<SCH_LABEL_BASE*>( item ) )
            for( const auto& field : label->GetFields() ) fields.insert( field.GetName() );
        if( auto* sheet = dynamic_cast<SCH_SHEET*>( item ) )
            for( const auto& field : sheet->GetFields() )
                if( field.GetId() == FIELD_T::USER ) fields.insert( field.GetName() );
    }
    for( const auto& field : fields ) choices.push_back( { "field:" + field, _( "Field: " ) + field } );
    for( const auto& property : properties )
        choices.push_back( { "native:" + property, _( "Property: " ) + wxGetTranslation( property ) } );
    return choices;
}

class STAMP_DIALOG : public DIALOG_SHIM
{
public:
    STAMP_DIALOG( SCH_EDIT_FRAME* frame, PROPERTY_STAMP_SETTINGS& settings ) :
        DIALOG_SHIM( frame, wxID_ANY, _( "Apply Properties by Click" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_settings( settings ), m_choices( PropertyChoices( *frame ) )
    {
        auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* form = new wxFlexGridSizer( 2, 8, 12 ); form->AddGrowableCol( 1 );
        auto row = [&]( const wxString& name, wxWindow* control )
        {
            form->Add( new wxStaticText( this, wxID_ANY, name ), 0, wxALIGN_CENTER_VERTICAL );
            form->Add( control, 1, wxEXPAND );
        };
        m_property = new wxChoice( this, wxID_ANY );
        for( const auto& choice : m_choices ) m_property->Append( choice.label );
        m_property->SetSelection( 0 );
        for( std::size_t i = 0; i < m_choices.size(); ++i )
            if( m_choices[i].key == settings.property ) m_property->SetSelection( i );
        row( _( "Property:" ), m_property );
        m_field = new wxTextCtrl( this, wxID_ANY, settings.customField );
        row( _( "Custom field name:" ), m_field );
        m_pattern = new wxTextCtrl( this, wxID_ANY, settings.pattern );
        row( _( "Value / pattern:" ), m_pattern );
        m_numbering = new wxCheckBox( this, wxID_ANY, _( "Automatic numbering (# or ?)" ) );
        m_numbering->SetValue( settings.numbering ); row( "", m_numbering );
        m_start = new wxTextCtrl( this, wxID_ANY, wxString::Format( "%lld", static_cast<long long>( settings.start ) ) );
        row( _( "Start:" ), m_start );
        m_step = new wxTextCtrl( this, wxID_ANY, wxString::Format( "%lld", static_cast<long long>( settings.step ) ) );
        row( _( "Step (negative to decrement):" ), m_step );
        m_create = new wxCheckBox( this, wxID_ANY, _( "Create missing custom fields" ) );
        m_create->SetValue( settings.createFields ); row( "", m_create );
        m_wireLabels = new wxCheckBox( this, wxID_ANY, _( "Create labels when clicking wires" ) );
        m_wireLabels->SetValue( settings.wireLabels ); row( "", m_wireLabels );
        m_labelKind = new wxChoice( this, wxID_ANY );
        m_labelKind->Append( _( "Local label" ) ); m_labelKind->Append( _( "Global label" ) );
        m_labelKind->Append( _( "Hierarchical label" ) );
        m_labelKind->SetSelection( std::clamp( settings.labelKind, 0, 2 ) );
        row( _( "Label type:" ), m_labelKind );
        layout->Add( form, 0, wxEXPAND | wxALL, 12 );
        auto* help = new wxStaticText( this, wxID_ANY,
            _( "Example: Digital_D# or Digital_D?. The number changes only after an accepted click.\n"
               "On wires, the generated value is used as the label's name.\n"
               "For numeric properties, use a number and units; for yes/no properties, use true/false." ) );
        layout->Add( help, 0, wxEXPAND | wxLEFT | wxRIGHT, 12 );
        m_preview = new wxStaticText( this, wxID_ANY, "" );
        layout->Add( m_preview, 0, wxEXPAND | wxALL, 12 );
        layout->Add( CreateStdDialogButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 8 );
        SetSizer( layout ); SetMinSize( FromDIP( wxSize( 580, -1 ) ) );
        Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { update(); } );
        Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& ) { update(); } );
        Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { update(); } );
        update(); SetInitialFocus( m_pattern ); finishDialogSettings();
    }

    bool TransferDataFromWindow() override
    {
        auto settings = m_settings;
        long long start, step;
        if( !m_start->GetValue().ToLongLong( &start ) || !m_step->GetValue().ToLongLong( &step ) )
        {
            wxMessageBox( _( "Enter valid integers for Start and Step." ), GetTitle(), wxOK | wxICON_ERROR, this );
            return false;
        }
        settings.property = m_choices.at( m_property->GetSelection() ).key;
        settings.customField = m_field->GetValue().Strip( wxString::both );
        settings.pattern = m_pattern->GetValue(); settings.numbering = m_numbering->GetValue();
        settings.start = start; settings.step = step; settings.createFields = m_create->GetValue();
        settings.wireLabels = m_wireLabels->GetValue(); settings.labelKind = m_labelKind->GetSelection();
        if( settings.property == "field" && settings.customField.IsEmpty() )
        {
            wxMessageBox( _( "Enter the custom field's name." ), GetTitle(), wxOK | wxICON_ERROR, this );
            return false;
        }
        try { PROPERTY_STAMP_SEQUENCE check( settings ); }
        catch( const std::exception& error )
        {
            wxMessageBox( wxString::FromUTF8( error.what() ), GetTitle(), wxOK | wxICON_ERROR, this );
            return false;
        }
        m_settings = settings; return true;
    }
private:
    void update()
    {
        m_field->Enable( m_choices.at( m_property->GetSelection() ).key == "field" );
        m_start->Enable( m_numbering->GetValue() ); m_step->Enable( m_numbering->GetValue() );
        m_labelKind->Enable( m_wireLabels->GetValue() );
        auto settings = m_settings; settings.pattern = m_pattern->GetValue();
        settings.numbering = m_numbering->GetValue(); long long start;
        if( !m_start->GetValue().ToLongLong( &start ) ) { m_preview->SetLabel( _( "Invalid start value" ) ); return; }
        settings.start = start;
        try { m_preview->SetLabel( _( "Next value: " ) + PROPERTY_STAMP_SEQUENCE( settings ).Current() ); }
        catch( const std::exception& error ) { m_preview->SetLabel( wxString::FromUTF8( error.what() ) ); }
    }
    PROPERTY_STAMP_SETTINGS& m_settings;
    std::vector<PROPERTY_CHOICE> m_choices;
    wxChoice *m_property, *m_labelKind;
    wxTextCtrl *m_field, *m_pattern, *m_start, *m_step;
    wxCheckBox *m_numbering, *m_create, *m_wireLabels;
    wxStaticText* m_preview;
};
}

class SCH_PROPERTY_STAMP_TOOL : public SCH_TOOL_BASE<SCH_EDIT_FRAME>
{
public:
    SCH_PROPERTY_STAMP_TOOL() : SCH_TOOL_BASE<SCH_EDIT_FRAME>( "eeschema.PropertyStamp" ) {}
private:
    void setTransitions() override { Go( &SCH_PROPERTY_STAMP_TOOL::Main, SCH_ACTIONS::propertyStamp.MakeEvent() ); }

    bool stampWire( SCH_LINE& wire, VECTOR2I cursor, const wxString& value )
    {
        if( !m_settings.wireLabels ) throw std::runtime_error( "Wire labels are disabled for this tool" );
        if( value.IsEmpty() || value.Contains( '\n' ) || value.Contains( '\r' ) )
            throw std::runtime_error( "Enter a nonempty, single-line label name" );
        EE_GRID_HELPER grid( m_toolMgr );
        const auto position = SEG( wire.GetStartPoint(), wire.GetEndPoint() ).NearestPoint(
                grid.Align( cursor, GRID_CONNECTABLE ) );
        SCH_COMMIT commit( m_toolMgr );
        for( SCH_ITEM* item : m_frame->GetScreen()->Items() )
            if( auto* existing = dynamic_cast<SCH_LABEL_BASE*>( item ); existing && existing->GetPosition() == position )
            {
                commit.Modify( existing, m_frame->GetScreen() );
                existing->SetText( EscapeString( value, CTX_NETNAME ) );
                commit.Push( _( "Apply Property by Click" ) ); return true;
            }
        std::unique_ptr<SCH_LABEL_BASE> label;
        const auto text = EscapeString( value, CTX_NETNAME );
        if( m_settings.labelKind == 1 ) label = std::make_unique<SCH_GLOBALLABEL>( position, text );
        else if( m_settings.labelKind == 2 ) label = std::make_unique<SCH_HIERLABEL>( position, text );
        else label = std::make_unique<SCH_LABEL>( position, text );
        label->SetParent( m_frame->GetScreen() );
        const int size = m_frame->Schematic().Settings().m_DefaultTextSize;
        label->SetTextSize( VECTOR2I( size, size ) );
        label->SetSpinStyle( wire.GetStartPoint().x == wire.GetEndPoint().x ? SPIN_STYLE::UP : SPIN_STYLE::RIGHT );
        label->SetFlags( IS_NEW );
        label->AutoplaceFields( nullptr, AUTOPLACE_AUTO );
        commit.Add( label.release(), m_frame->GetScreen() );
        commit.Push( _( "Label Wire by Click" ) ); return true;
    }

    int Main( const TOOL_EVENT& action )
    {
        CIRCUITALIVE::CancelUsedSymbolPlacement( m_frame );
        STAMP_DIALOG dialog( m_frame, m_settings );
        if( dialog.ShowModal() != wxID_OK ) return 0;
        PROPERTY_STAMP_SEQUENCE sequence( m_settings );
        m_toolMgr->RunAction( ACTIONS::selectionClear );
        m_frame->PushTool( action ); Activate();
        auto* controls = getViewControls(); controls->ShowCursor( true );
        STATUS_TEXT_POPUP popup( m_frame );
        auto update = [&]()
        {
            popup.SetText( sequence.Current() );
            popup.Move( KIPLATFORM::UI::GetMousePosition() + wxPoint( 16, 24 ) );
            popup.Popup();
            m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::BULLSEYE );
        };
        update();
        while( TOOL_EVENT* event = Wait() )
        {
            m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::BULLSEYE );
            if( event->IsCancelInteractive() ) { event->SetPassEvent( false ); break; }
            else if( event->IsActivate() )
            {
                if( event->IsPointEditor() ) event->SetPassEvent( false );
                else { event->SetPassEvent(); break; }
            }
            else if( event->IsMotion() ) update();
            else if( event->IsClick( BUT_LEFT ) || event->IsAction( &ACTIONS::cursorClick ) )
            {
                popup.Hide();
                m_toolMgr->RunAction( ACTIONS::selectionClear );
                EDA_ITEM* picked = nullptr;
                const auto position = controls->GetMousePosition();
                if( !m_selectionTool->SelectPoint( position, SCH_COLLECTOR::EditableItems, &picked ) || !picked )
                { update(); continue; }
                try
                {
                    auto* item = dynamic_cast<SCH_ITEM*>( picked );
                    bool accepted = false;
                    const auto value = sequence.Current();
                    if( auto* wire = dynamic_cast<SCH_LINE*>( item ); wire && wire->IsWire() && m_settings.wireLabels )
                        accepted = stampWire( *wire, position, value );
                    else if( item )
                    {
                        auto plan = PlanPropertyStamp( *item, m_frame->GetCurrentSheet(), m_settings, value, m_frame );
                        SCH_COMMIT commit( m_toolMgr );
                        PROPERTY_COMMIT_HANDLER handler( &commit );
                        try
                        {
                            commit.Modify( plan.owner, m_frame->GetScreen(), RECURSE_MODE::NO_RECURSE );
                            accepted = plan.apply( &commit );
                            if( accepted )
                            {
                                if( auto* symbol = dynamic_cast<SCH_SYMBOL*>( plan.owner ); symbol && plan.syncUnits )
                                    symbol->SyncOtherUnits( m_frame->GetCurrentSheet(), commit, plan.property,
                                                           m_frame->Schematic().GetCurrentVariant() );
                                commit.Push( _( "Apply Property by Click" ), *plan.undoable ? 0 : SKIP_UNDO );
                                if( !*plan.undoable )
                                {
                                    // Relinking a sheet invalidates prior undo entries, as in Edit Sheet Properties.
                                    m_frame->ClearUndoRedoList();
                                    std::vector<SCH_ITEM*> items{ plan.owner };
                                    m_frame->Schematic().OnItemsRemoved( items );
                                    m_frame->Schematic().OnItemsAdded( items );
                                    m_frame->Schematic().RefreshHierarchy();
                                    m_frame->UpdateHierarchyNavigator();
                                }
                                m_frame->UpdateItem( plan.owner );
                            }
                            else commit.Revert();
                        }
                        catch( ... ) { commit.Revert(); throw; }
                    }
                    m_toolMgr->RunAction( ACTIONS::selectionClear );
                    if( accepted && !sequence.Advance() ) break;
                    update();
                }
                catch( const std::exception& error )
                {
                    m_toolMgr->RunAction( ACTIONS::selectionClear );
                    m_frame->ShowInfoBarError( wxString::FromUTF8( error.what() ) );
                    update();
                }
            }
            else if( event->IsClick( BUT_RIGHT ) )
            {
                popup.Hide(); m_menu->ShowContextMenu( m_selectionTool->GetSelection() );
            }
            else event->SetPassEvent();
        }
        popup.Hide();
        m_toolMgr->RunAction( ACTIONS::selectionClear );
        controls->SetAutoPan( false ); controls->CaptureCursor( false ); controls->ForceCursorPosition( false );
        m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
        m_frame->PopTool( action ); return 0;
    }
    PROPERTY_STAMP_SETTINGS m_settings;
};

void RegisterPropertyStamp( TOOL_MANAGER* manager ) { manager->RegisterTool( new SCH_PROPERTY_STAMP_TOOL ); }
}
