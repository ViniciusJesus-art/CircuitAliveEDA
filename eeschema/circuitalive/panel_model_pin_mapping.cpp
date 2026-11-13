/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "panel_model_pin_mapping.h"
#include "model_i18n.h"
#include "choice_controls.h"
#include <widgets/symbol_preview_widget.h>
#include <lib_symbol.h>
#include <sch_pin.h>
#include <sch_field.h>
#include <sch_painter.h>
#include <string_utils.h>
#include <view/view.h>
#include <view/view_overlay.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <set>

namespace CIRCUITALIVE
{
namespace
{
wxString W( const std::string& text ) { return wxString::FromUTF8( text ); }
KIGFX::COLOR4D PinColor( std::size_t index )
{
    // Physical pin numbers also identify connections without relying on color.
    static const KIGFX::COLOR4D colors[] = {
        { .15, .55, .95, 1 }, { .9, .35, .12, 1 }, { .1, .65, .4, 1 },
        { .7, .3, .85, 1 }, { .65, .55, .05, 1 }, { .05, .65, .7, 1 }
    };
    return colors[index % std::size( colors )];
}
}

class MODEL_PIN_PREVIEW : public SYMBOL_PREVIEW_WIDGET
{
public:
    MODEL_PIN_PREVIEW( wxWindow* parent, LIB_SYMBOL* symbol, int unit, int bodyStyle,
                       std::function<void( const std::string& )> clicked ) :
        SYMBOL_PREVIEW_WIDGET( parent, nullptr, true, EDA_DRAW_PANEL_GAL::GAL_TYPE_CAIRO ),
        m_symbol( symbol ? std::make_unique<LIB_SYMBOL>( *symbol ) : nullptr ),
        m_unit( unit ), m_bodyStyle( bodyStyle ), m_clicked( std::move( clicked ) )
    {
        m_overlay = std::make_unique<KIGFX::VIEW_OVERLAY>();
        GetCanvas()->GetView()->Add( m_overlay.get() );
        GetRenderSettings()->m_ShowPinNumbers = true;
        GetRenderSettings()->m_ShowHiddenPins = true;
        if( m_symbol )
        {
            m_symbol->SetShowPinNumbers( true );
            m_symbol->SetShowPinNames( true );
            DisplayPart( m_symbol.get(), unit, bodyStyle );
        }
        else SetStatusText( _( "Symbol preview unavailable. Use the symbol pin list below." ) );
        GetCanvas()->Bind( wxEVT_LEFT_DOWN, [this]( wxMouseEvent& event )
        {
            if( m_panning ) return;
            const auto pins = pinsAt( event.GetPosition() );
            if( pins.size() == 1 ) m_clicked( *pins.begin() );
            else if( pins.size() > 1 )
                GetCanvas()->SetToolTip( _( "Overlapping pins: choose the physical pin in the list below, then click Connect." ) );
        } );
        GetCanvas()->Bind( wxEVT_MIDDLE_DOWN, [this]( wxMouseEvent& event )
        {
            if( !m_previewItem ) return;
            m_panning = true;
            m_panPosition = event.GetPosition();
            GetCanvas()->SetCursor( wxCursor( wxCURSOR_SIZING ) );
            if( !GetCanvas()->HasCapture() ) GetCanvas()->CaptureMouse();
        } );
        GetCanvas()->Bind( wxEVT_MIDDLE_UP, [this]( wxMouseEvent& ) { endPan(); } );
        GetCanvas()->Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& )
        {
            m_panning = false;
            GetCanvas()->SetCursor( wxNullCursor );
        } );
        GetCanvas()->Bind( wxEVT_MOTION, [this]( wxMouseEvent& event )
        {
            if( m_panning )
            {
                if( !event.MiddleIsDown() ) { endPan(); return; }
                const auto position = event.GetPosition();
                const auto delta = position - m_panPosition;
                auto* view = GetCanvas()->GetView();
                view->SetCenter( view->GetCenter() - view->ToWorld( VECTOR2D( delta.x, delta.y ), false ) );
                m_panPosition = position;
                UpdatePins();
                return;
            }
            const auto pins = pinsAt( event.GetPosition() );
            GetCanvas()->SetCursor( pins.empty() ? wxNullCursor : wxCursor( wxCURSOR_HAND ) );
            wxString tip;
            for( const auto& pin : pins )
            {
                if( !tip.empty() ) tip += ", ";
                tip += W( pin );
            }
            if( pins.size() > 1 ) tip += " — " + _( "Overlapping pins: use the list below" );
            if( GetCanvas()->GetToolTipText() != tip ) GetCanvas()->SetToolTip( tip );
        } );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& event )
        {
            event.Skip();
            CallAfter( [this] { UpdatePins(); } );
        } );
    }
    ~MODEL_PIN_PREVIEW() override
    {
        endPan();
        GetCanvas()->StopDrawing();
        GetCanvas()->GetView()->Remove( m_overlay.get() );
    }
    void SetUnit( int unit )
    {
        m_unit = unit;
        if( m_symbol ) DisplayPart( m_symbol.get(), unit, m_bodyStyle );
        UpdatePins();
    }
    void SetComponent( const CIRCUIT_COMPONENT& component )
    {
        if( !m_symbol ) return;
        m_symbol->GetReferenceField().SetText( W( component.reference ) );
        m_symbol->GetValueField().SetText( W( component.value ) );
        for( auto* pin : m_symbol->GetPins() )
            if( const auto it = component.pinNames.find( pin->GetNumber().ToStdString( wxConvUTF8 ) );
                it != component.pinNames.end() )
                pin->SetName( W( it->second ) );
        DisplayPart( m_symbol.get(), m_unit, m_bodyStyle );
    }
    void FitSymbol()
    {
        fitOnDrawArea();
        UpdatePins();
    }
    void SetMapping( const std::vector<MODEL_PIN>& ports,
                     const std::map<std::string, std::string>& mapping,
                     const std::vector<std::string>& physical, int selected )
    {
        m_connections.clear();
        m_physical = std::set<std::string>( physical.begin(), physical.end() );
        m_selected.clear();
        for( std::size_t i = 0; i < ports.size(); ++i )
        {
            const auto& pin = mapping.at( ports[i].id );
            if( !pin.empty() ) m_connections[pin] = i;
            if( static_cast<int>( i ) == selected ) m_selected = pin;
        }
        UpdatePins();
    }
    void UpdatePins()
    {
        m_overlay->Clear();
        auto* view = GetCanvas()->GetView();
        if( m_previewItem )
            for( const auto* pin : m_previewItem->GetPins() )
            {
                if( !visible( *pin ) ) continue;
                const auto number = pin->GetNumber().ToStdString( wxConvUTF8 );
                const auto connection = m_connections.find( number );
                const auto color = connection == m_connections.end() ? KIGFX::COLOR4D( .5, .5, .5, 1 )
                                                                    : PinColor( connection->second );
                const VECTOR2D position( pin->GetPosition() );
                m_overlay->SetLineWidth( view->ToWorld( 1.5 ) );
                m_overlay->SetIsStroke( true ); m_overlay->SetIsFill( true );
                m_overlay->SetStrokeColor( color ); m_overlay->SetFillColor( color );
                m_overlay->Circle( position, view->ToWorld( 4.5 ) );
                if( number == m_selected )
                {
                    m_overlay->SetIsFill( false );
                    m_overlay->SetStrokeColor( KIGFX::COLOR4D( 1, .55, .05, 1 ) );
                    m_overlay->SetLineWidth( view->ToWorld( 2.5 ) );
                    m_overlay->Circle( position, view->ToWorld( 8.5 ) );
                }
            }
        view->Update( m_overlay.get() ); GetCanvas()->ForceRefresh();
    }

private:
    void endPan()
    {
        m_panning = false;
        if( GetCanvas()->HasCapture() ) GetCanvas()->ReleaseMouse();
        GetCanvas()->SetCursor( wxNullCursor );
    }
    bool visible( const SCH_PIN& pin ) const
    {
        return ( !pin.GetUnit() || pin.GetUnit() == m_renderSettings->m_ShowUnit )
               && ( !pin.GetBodyStyle() || pin.GetBodyStyle() == m_renderSettings->m_ShowBodyStyle )
               && m_physical.contains( pin.GetNumber().ToStdString( wxConvUTF8 ) );
    }
    std::set<std::string> pinsAt( const wxPoint& point ) const
    {
        std::set<std::string> pins;
        if( !m_previewItem ) return pins;
        const VECTOR2D position = GetCanvas()->GetView()->ToWorld( VECTOR2D( point.x, point.y ) );
        double nearest = GetCanvas()->GetView()->ToWorld( FromDIP( 10 ) );
        for( const auto* pin : m_previewItem->GetPins() )
        {
            if( !visible( *pin ) ) continue;
            const double distance = ( VECTOR2D( pin->GetPosition() ) - position ).EuclideanNorm();
            if( distance > nearest ) continue;
            if( distance < nearest - .01 ) pins.clear();
            nearest = distance;
            pins.insert( pin->GetNumber().ToStdString( wxConvUTF8 ) );
        }
        return pins;
    }
    std::unique_ptr<LIB_SYMBOL> m_symbol;
    int m_unit, m_bodyStyle;
    std::function<void( const std::string& )> m_clicked;
    std::unique_ptr<KIGFX::VIEW_OVERLAY> m_overlay;
    std::map<std::string, std::size_t> m_connections;
    std::set<std::string> m_physical;
    std::string m_selected;
    bool m_panning = false;
    wxPoint m_panPosition;
};

PANEL_MODEL_PIN_MAPPING::PANEL_MODEL_PIN_MAPPING( wxWindow* parent, LIB_SYMBOL* symbol,
                                               int unit, int bodyStyle ) : wxPanel( parent )
{
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* hint = new wxStaticText( this, wxID_ANY,
        _( "1. Select a model port on the left.  2. Click the symbol pin to connect it.\n"
           "Colors identify connected pairs. Connecting an occupied pin swaps the connections." ) );
    layout->Add( hint, 0, wxEXPAND | wxALL, 10 );
    auto* middle = new wxBoxSizer( wxHORIZONTAL );
    m_ports = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, FromDIP( wxSize( 350, 240 ) ),
                              wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SUNKEN );
    m_ports->InsertColumn( 0, _( "Model port" ), wxLIST_FORMAT_LEFT, FromDIP( 210 ) );
    m_ports->InsertColumn( 1, _( "Symbol pin" ), wxLIST_FORMAT_LEFT, FromDIP( 135 ) );
    middle->Add( m_ports, 1, wxEXPAND | wxRIGHT, 10 );
    auto* right = new wxBoxSizer( wxVERTICAL );
    auto* previewRow = new wxBoxSizer( wxHORIZONTAL );
    auto* units = new CLICK_CHOICE( this, wxID_ANY );
    if( symbol )
        for( int i = 1; i <= symbol->GetUnitCount(); ++i ) units->Append( symbol->GetUnitDisplayName( i, true ) );
    units->SetSelection( symbol ? std::clamp( unit, 1, symbol->GetUnitCount() ) - 1 : wxNOT_FOUND );
    units->Enable( symbol && symbol->IsMultiUnit() );
    previewRow->Add( units, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8 );
    auto* fit = new wxButton( this, wxID_ANY, _( "Fit symbol" ) );
    previewRow->Add( fit ); right->Add( previewRow, 0, wxEXPAND | wxBOTTOM, 6 );
    m_preview = new MODEL_PIN_PREVIEW( this, symbol, unit, bodyStyle,
                                      [this]( const std::string& pin ) { assign( pin ); } );
    m_preview->SetMinSize( FromDIP( wxSize( 280, 220 ) ) );
    right->Add( m_preview, 1, wxEXPAND );
    auto* pinRow = new wxBoxSizer( wxHORIZONTAL );
    m_physical = new CLICK_CHOICE( this, wxID_ANY );
    m_physical->SetToolTip( _( "All physical pins, including hidden pins and pins in other units" ) );
    m_connect = new wxButton( this, wxID_ANY, _( "Connect" ) );
    pinRow->Add( m_physical, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 8 ); pinRow->Add( m_connect );
    right->Add( pinRow, 0, wxEXPAND | wxTOP, 6 );
    middle->Add( right, 1, wxEXPAND ); layout->Add( middle, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );
    auto* buttons = new wxBoxSizer( wxHORIZONTAL );
    m_unlink = new wxButton( this, wxID_ANY, _( "Disconnect selected" ) );
    m_clear = new wxButton( this, wxID_ANY, _( "Clear connections" ) );
    m_defaults = new wxButton( this, wxID_ANY, _( "Use model defaults" ) );
    for( auto* button : { m_unlink, m_clear, m_defaults } ) buttons->Add( button, 0, wxRIGHT, 8 );
    layout->Add( buttons, 0, wxALL, 10 );
    m_status = new wxStaticText( this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE );
    layout->Add( m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    SetSizer( layout );
    m_ports->Bind( wxEVT_LIST_ITEM_SELECTED, [this]( wxListEvent& event )
    { if( !m_refreshing ) select( event.GetIndex() ); } );
    units->Bind( wxEVT_COMBOBOX, [this, units]( wxCommandEvent& ) { m_preview->SetUnit( units->GetSelection() + 1 ); } );
    fit->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_preview->FitSymbol(); } );
    m_physical->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& )
    { m_connect->Enable( m_selected >= 0 && m_physical->GetSelection() >= 0 ); } );
    m_connect->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    { if( m_physical->GetSelection() >= 0 ) assign( m_physicalPins.at( m_physical->GetSelection() ) ); } );
    m_unlink->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { assign( "" ); } );
    m_clear->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    {
        for( auto& [port, pin] : m_mapping ) pin.clear();
        select( m_selected );
    } );
    m_defaults->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
    { m_mapping = m_defaultMapping; select( m_selected ); } );
}

void PANEL_MODEL_PIN_MAPPING::Configure( const SIMULATION_MODEL* model, const CIRCUIT_COMPONENT& component )
{
    m_selected = -1;
    m_modelPins = model ? model->pins : std::vector<MODEL_PIN>();
    m_mapping = model ? ModelPinMapping( *model, component ) : std::map<std::string, std::string>();
    auto defaults = component; defaults.modelPins.clear();
    m_defaultMapping = model ? ModelPinMapping( *model, defaults ) : std::map<std::string, std::string>();
    m_pinNames = component.pinNames;
    m_preview->SetComponent( component );
    m_physicalPins.clear(); m_physical->Clear();
    for( const auto& [pin, net] : component.pins ) m_physicalPins.push_back( pin );
    std::sort( m_physicalPins.begin(), m_physicalPins.end(),
               []( const auto& a, const auto& b ) { return StrNumCmp( W( a ), W( b ) ) < 0; } );
    for( const auto& pin : m_physicalPins ) m_physical->Append( pinLabel( pin ) );
    m_refreshing = true; m_ports->DeleteAllItems();
    for( std::size_t i = 0; i < m_modelPins.size(); ++i )
    {
        const auto& pin = m_modelPins[i];
        m_ports->InsertItem( i, wxString::Format( "%zu. ", i + 1 ) + ModelText( pin.label ) + " [" + W( pin.id ) + "]" );
    }
    m_refreshing = false;
    if( !m_modelPins.empty() )
    {
        int initial = 0;
        for( std::size_t i = 0; i < m_modelPins.size(); ++i )
            if( m_mapping.at( m_modelPins[i].id ).empty() ) { initial = i; break; }
        select( initial );
    }
    else refresh();
}

wxString PANEL_MODEL_PIN_MAPPING::pinLabel( const std::string& physical ) const
{
    if( physical.empty() ) return _( "Not connected" );
    const auto it = m_pinNames.find( physical );
    return W( physical ) + ( it == m_pinNames.end() || it->second.empty() || it->second == "~"
                            ? wxString() : " — " + W( it->second ) );
}

bool PANEL_MODEL_PIN_MAPPING::Complete() const
{
    return !m_modelPins.empty() && m_modelPins.size() == m_physicalPins.size()
           && std::all_of( m_mapping.begin(), m_mapping.end(), []( const auto& pair ) { return !pair.second.empty(); } );
}

void PANEL_MODEL_PIN_MAPPING::select( int row )
{
    m_selected = row;
    m_refreshing = true;
    m_ports->SetItemState( row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                          wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED );
    m_ports->EnsureVisible( row );
    m_refreshing = false;
    const auto& assigned = m_mapping.at( m_modelPins.at( row ).id );
    const auto pin = std::find( m_physicalPins.begin(), m_physicalPins.end(), assigned );
    m_physical->SetSelection( pin == m_physicalPins.end() ? wxNOT_FOUND : pin - m_physicalPins.begin() );
    refresh();
}

void PANEL_MODEL_PIN_MAPPING::assign( const std::string& physical )
{
    if( m_selected < 0 || m_modelPins.size() != m_physicalPins.size() ) return;
    if( !physical.empty() && std::find( m_physicalPins.begin(), m_physicalPins.end(), physical ) == m_physicalPins.end() ) return;
    AssignModelPin( m_mapping, m_modelPins.at( m_selected ).id, physical );
    select( m_selected );
}

void PANEL_MODEL_PIN_MAPPING::refresh()
{
    std::size_t connected = 0;
    for( std::size_t i = 0; i < m_modelPins.size(); ++i )
    {
        const auto& pin = m_mapping.at( m_modelPins[i].id );
        if( !pin.empty() ) ++connected;
        m_ports->SetItem( i, 1, pinLabel( pin ) );
        m_ports->SetItemTextColour( i, pin.empty() ? m_ports->GetForegroundColour() : PinColor( i ).ToColour() );
    }
    const bool compatible = !m_modelPins.empty() && m_modelPins.size() == m_physicalPins.size();
    m_connect->Enable( compatible && m_selected >= 0 && m_physical->GetSelection() >= 0 );
    m_unlink->Enable( compatible && m_selected >= 0 );
    m_clear->Enable( compatible ); m_defaults->Enable( compatible );
    wxString status = wxString::Format( _( "%zu / %zu connections" ), connected, m_modelPins.size() );
    if( m_modelPins.empty() ) status = _( "Choose a model with the same pin count as the symbol." );
    else if( !compatible ) status = _( "The model and symbol must have the same number of pins." );
    else if( m_selected >= 0 ) status += " — " + ModelText( m_modelPins[m_selected].label )
        + " → " + pinLabel( m_mapping.at( m_modelPins[m_selected].id ) );
    m_status->SetLabel( status );
    m_preview->SetMapping( m_modelPins, m_mapping, m_physicalPins, m_selected );
}
}
