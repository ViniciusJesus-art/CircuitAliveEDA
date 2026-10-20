/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "panel_model_interactions.h"
#include "control_geometry.h"
#include "control_visual_json.h"
#include "circuit_snapshot.h"
#include "model_i18n.h"
#include "choice_controls.h"
#include <widgets/symbol_preview_widget.h>
#include <dialog_shim.h>
#include <lib_symbol.h>
#include <view/view.h>
#include <view/view_overlay.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <fstream>

namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
namespace
{
wxString W( const std::string& text ) { return wxString::FromUTF8( text ); }
JSON InputBlock( const std::string& name )
{
    for( const auto& dir : MODEL_DESIGN::BlockDirectories() )
    { std::ifstream in( dir / name ); if( in ) return JSON::parse( in ); }
    throw std::runtime_error( "Interactive input block was not found" );
}
std::string NewReference( const MODEL_DESIGN& design, bool slider )
{
    std::string name; int n = 1;
    do { name = std::string( slider ? "slider" : "button" ) + std::to_string( n++ ); }
    while( std::any_of( design.interactions.begin(), design.interactions.end(),
            [&]( const auto& c ) { return ControlReferenceName( c ) == name; } ) );
    return name;
}
class INTERACTION_DIALOG : public DIALOG_SHIM
{
public:
    INTERACTION_DIALOG( wxWindow* parent, DESIGN_INTERACTION& interaction, DESIGN_BLOCK& block ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Interactive control" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER )
    {
        OptOut( this );
        auto json = JSON::parse( block.modelJson );
        const bool slider = interaction.control.starts_with( "adjust:" );
        JSON* target = nullptr;
        std::string parameter;
        if( interaction.control == "switch" ) target = &json.at( "control" );
        else
        {
            const auto colon = interaction.control.find( ':' );
            auto& list = json.at( slider ? "adjustments" : "controls" );
            for( auto& item : list ) if( item.at( "id" ) == interaction.control.substr( colon + 1 ) ) target = &item;
        }
        if( !target ) throw std::runtime_error( "Unknown interaction control" );
        auto* layout = new wxBoxSizer( wxVERTICAL );
        layout->Add( new wxStaticText( this, wxID_ANY, W( block.id ) + " — "
                + ( slider ? _( "Click to open a slider" ) : _( "Click to toggle ON / OFF" ) ) ), 0, wxALL, 10 );
        auto* fields = new wxFlexGridSizer( 2, 8, 10 ); fields->AddGrowableCol( 1 );
        auto field = [&]( const wxString& label, const wxString& value )
        {
            fields->Add( new wxStaticText( this, wxID_ANY, label ), 0, wxALIGN_CENTER_VERTICAL );
            auto* text = new wxTextCtrl( this, wxID_ANY, value ); fields->Add( text, 1, wxEXPAND ); return text;
        };
        auto* label = field( _( "Label" ), W( interaction.label ) );
        auto* reference = field( _( "Reference name (use #name in values)" ), W( ControlReferenceName( interaction ) ) );
        fields->Add( new wxStaticText( this, wxID_ANY, _( "Clickable area" ) ), 0, wxALIGN_CENTER_VERTICAL );
        auto* placement = new CLICK_CHOICE( this ); placement->Append( _( "Whole symbol" ) );
        placement->Append( _( "Positioned rectangle" ) ); placement->SetSelection( interaction.visual.custom ? 1 : 0 );
        fields->Add( placement, 1, wxEXPAND );
        auto numberField = [&]( const wxString& name, double value ) { return field( name, wxString::Format( "%.10g", value ) ); };
        auto* px = numberField( _( "Left (% of symbol width)" ), 100 * interaction.visual.x );
        auto* py = numberField( _( "Top (% of symbol height)" ), 100 * interaction.visual.y );
        auto* pw = numberField( _( "Width (%)" ), 100 * interaction.visual.width );
        auto* ph = numberField( _( "Height (%)" ), 100 * interaction.visual.height );
        fields->AddSpacer( 1 );
        auto* hidden = new wxCheckBox( this, wxID_ANY, _( "Hide outline/text during simulation; keep clicks active" ) );
        hidden->SetValue( !interaction.visual.visible ); fields->Add( hidden );
        wxTextCtrl *minimum = nullptr, *maximum = nullptr, *step = nullptr, *unit = nullptr, *initial = nullptr;
        wxTextCtrl *on = nullptr, *off = nullptr;
        wxTextCtrl *onValue = nullptr, *offValue = nullptr;
        CLICK_CHOICE* mode = nullptr;
        wxCheckBox* closed = nullptr;
        if( slider )
        {
            parameter = target->at( "parameter" );
            const auto m = MODEL_REPOSITORY::LoadText( block.modelJson );
            auto p = std::find_if( m.parameters.begin(), m.parameters.end(), [&]( const auto& candidate ) { return candidate.id == parameter; } );
            const auto value = block.values.contains( parameter ) ? block.values.at( parameter ) : p->defaultValue;
            initial = field( _( "Initial value (or @parameter)" ), W( value ) );
            minimum = numberField( _( "Minimum" ), target->at( "min" ) );
            maximum = numberField( _( "Maximum" ), target->at( "max" ) );
            step = numberField( _( "Step" ), target->value( "step", .01 ) );
            unit = field( _( "Display unit" ), W( target->value( "unit", "" ) ) );
        }
        else
        {
            const auto modelId = json.at( "id" ).get<std::string>();
            if( modelId == "interactive_button" || modelId == "interactive_switch" )
            {
                fields->Add( new wxStaticText( this, wxID_ANY, _( "Electrical behavior" ) ), 0, wxALIGN_CENTER_VERTICAL );
                mode = new CLICK_CHOICE( this );
                mode->Append( _( "Switch: ON passes, OFF opens" ) );
                mode->Append( _( "Voltage input: ON / OFF levels" ) );
                mode->SetSelection( modelId == "interactive_switch" ? 0 : 1 ); fields->Add( mode, 1, wxEXPAND );
            }
            on = numberField( _( "ON source value" ), target->value( "on", 5.0 ) );
            auto* onLabel = fields->GetItem( fields->GetItemCount() - 2 )->GetWindow();
            off = numberField( _( "OFF source value" ), target->value( "off", 0.0 ) );
            auto* offLabel = fields->GetItem( fields->GetItemCount() - 2 )->GetWindow();
            onValue = field( _( "#reference value when ON" ), W( interaction.onValue ) );
            offValue = field( _( "#reference value when OFF" ), W( interaction.offValue ) );
            if( mode )
            {
                auto updateMode = [this,mode,on,off,onLabel,offLabel]()
                {
                    const bool voltage = mode->GetSelection() == 1;
                    if( !voltage ) { on->SetValue( "5" ); off->SetValue( "0" ); }
                    on->Show( voltage ); off->Show( voltage ); onLabel->Show( voltage ); offLabel->Show( voltage );
                    if( GetSizer() ) { Layout(); Fit(); }
                };
                mode->Bind( wxEVT_COMBOBOX, [updateMode]( wxCommandEvent& ) { updateMode(); } ); updateMode();
            }
            fields->AddSpacer( 1 ); closed = new wxCheckBox( this, wxID_ANY, _( "Initially ON" ) );
            closed->SetValue( target->value( "initially_closed", interaction.control == "switch" ) ); fields->Add( closed );
        }
        layout->Add( fields, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );
        auto* hint = new wxStaticText( this, wxID_ANY,
                _( "Position and size follow the symbol body. Negative positions place a control outside it.\n"
                   "Use #name in block values to follow this control. ON/OFF values accept text only when the destination is a text field.\n"
                   "The % display unit shows model values 0...1 as 0...100%." ) );
        hint->Wrap( 530 ); layout->Add( hint, 0, wxEXPAND | wxALL, 10 );
        layout->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 ); SetSizerAndFit( layout );
        SetMinSize( FromDIP( wxSize( 560, -1 ) ) ); Centre();
        // Keep all edits local until validation succeeds; Cancel changes no model data.
        Bind( wxEVT_BUTTON, [this, &interaction, &block, json, parameter, slider, placement,
                px, py, pw, ph, hidden, label, reference, minimum, maximum, step, unit, initial, on, off, closed, mode, onValue, offValue]( wxCommandEvent& ) mutable
        {
            try
            {
                auto number = []( wxTextCtrl* text ) { return ParseComponentValue( text->GetValue().ToStdString( wxConvUTF8 ) ); };
                CONTROL_VISUAL visual{ placement->GetSelection() == 1, number( px ) / 100, number( py ) / 100,
                    number( pw ) / 100, number( ph ) / 100 };
                visual.visible = !hidden->GetValue();
                ReadControlVisual( WriteControlVisual( visual ) );
                bool changedMode = false;
                if( mode )
                {
                    const std::string wanted = mode->GetSelection() == 0 ? "interactive_switch" : "interactive_button";
                    if( json.at( "id" ) != wanted ) { json = InputBlock( wanted + ".casim" ); changedMode = true; }
                }
                // Find the target in the dialog's private JSON copy.
                JSON* edited = nullptr;
                if( interaction.control == "switch" ) edited = &json["control"];
                else for( auto& item : json[slider ? "adjustments" : "controls"] )
                    if( item.at( "id" ) == interaction.control.substr( interaction.control.find( ':' ) + 1 ) ) edited = &item;
                if( slider )
                {
                    (*edited)["min"] = number( minimum ); (*edited)["max"] = number( maximum ); (*edited)["step"] = number( step );
                    (*edited)["unit"] = unit->GetValue().ToStdString( wxConvUTF8 );
                }
                else
                {
                    (*edited)["on"] = number( on ); (*edited)["off"] = number( off ); (*edited)["initially_closed"] = closed->GetValue();
                    if( number( on ) == number( off ) ) throw std::runtime_error( "ON and OFF values must differ" );
                }
                MODEL_REPOSITORY::LoadText( json.dump() );
                auto name = reference->GetValue().ToStdString( wxConvUTF8 );
                if( name.starts_with( '#' ) ) name.erase( 0, 1 );
                if( name.empty() ) throw std::runtime_error( "Set a control reference name" );
                if( changedMode )
                {
                    const auto previous = block.connections; block.connections.clear(); block.values.clear();
                    const bool sw = mode->GetSelection() == 0;
                    for( const auto& [from,to] : std::vector<std::pair<std::string,std::string>>{
                        {sw?"out":"p",sw?"p":"out"}, {sw?"ref":"n",sw?"n":"ref"} } )
                        if( previous.contains( from ) ) block.connections[to] = previous.at( from );
                }
                if( slider ) block.values[parameter] = initial->GetValue().ToStdString( wxConvUTF8 );
                block.modelJson = json.dump(); interaction.label = label->GetValue().ToStdString( wxConvUTF8 ); interaction.visual = visual;
                interaction.reference = name;
                if( !slider ) { interaction.onValue = onValue->GetValue().ToStdString( wxConvUTF8 ); interaction.offValue = offValue->GetValue().ToStdString( wxConvUTF8 ); }
                EndModal( wxID_OK );
            }
            catch( const std::exception& e ) { wxMessageBox( DiagnosticText( e.what() ), _( "Interactive control" ), wxOK, this ); }
        }, wxID_OK );
    }
};
}

class INTERACTION_PREVIEW : public SYMBOL_PREVIEW_WIDGET
{
public:
    INTERACTION_PREVIEW( wxWindow* parent, KIWAY* kiway, MODEL_DESIGN& design, int& selected,
                         std::function<void()> changed ) :
        SYMBOL_PREVIEW_WIDGET( parent, kiway, true, EDA_DRAW_PANEL_GAL::GAL_TYPE_CAIRO ),
        m_design( design ), m_selected( selected ), m_changed( std::move( changed ) )
    {
        m_overlay = std::make_unique<KIGFX::VIEW_OVERLAY>(); GetCanvas()->GetView()->Add( m_overlay.get() );
        auto* canvas = GetCanvas();
        canvas->Bind( wxEVT_LEFT_DOWN, [this]( wxMouseEvent& e )
        {
            if( !m_previewItem ) return;
            const VECTOR2I pos( GetCanvas()->GetView()->ToWorld( VECTOR2D( e.GetX(), e.GetY() ) ) );
            for( int i = int( m_design.interactions.size() ) - 1; i >= 0; --i )
            {
                const auto box=ControlBounds( body(), m_design.interactions[i].visual );
                BOX2I hit=box;hit.Inflate(std::lround(GetCanvas()->GetView()->ToWorld(6.)));
                if( hit.Contains( pos ) )
                {
                    m_selected = i;
                    auto& v = m_design.interactions[i].visual;
                    if( !v.custom )
                    {
                        const double w=std::max<double>(body().GetWidth(),schIUScale.mmToIU(1));
                        const double h=std::max<double>(body().GetHeight(),schIUScale.mmToIU(1));
                        v.custom=true;v.x=(box.GetOrigin().x-body().GetOrigin().x)/w;v.y=(box.GetOrigin().y-body().GetOrigin().y)/h;
                        v.width=box.GetWidth()/w;v.height=box.GetHeight()/h;
                    }
                    m_edges=edgesAt(box,pos);
                    m_start = pos; m_original = v; m_dragging = true;
                    GetCanvas()->CaptureMouse(); UpdateControls(); m_changed(); break;
                }
            }
        } );
        canvas->Bind( wxEVT_MOTION, [this]( wxMouseEvent& e )
        {
            const VECTOR2D pos = GetCanvas()->GetView()->ToWorld( VECTOR2D( e.GetX(), e.GetY() ) );
            if( !m_dragging )
            {
                int edges=0;
                if(m_previewItem)for(int i=int(m_design.interactions.size())-1;i>=0;--i)
                {
                    const auto box=ControlBounds(body(),m_design.interactions[i].visual);
                    BOX2I hit=box;hit.Inflate(std::lround(GetCanvas()->GetView()->ToWorld(6.)));
                    if(hit.Contains(VECTOR2I(pos))){edges=edgesAt(box,VECTOR2I(pos));break;}
                }
                const bool horizontal=edges&(CONTROL_LEFT|CONTROL_RIGHT),vertical=edges&(CONTROL_TOP|CONTROL_BOTTOM);
                if(horizontal&&vertical)GetCanvas()->SetCursor(wxCursor((edges== (CONTROL_LEFT|CONTROL_TOP)||edges==(CONTROL_RIGHT|CONTROL_BOTTOM))?wxCURSOR_SIZENWSE:wxCURSOR_SIZENESW));
                else if(horizontal)GetCanvas()->SetCursor(wxCursor(wxCURSOR_SIZEWE));
                else if(vertical)GetCanvas()->SetCursor(wxCursor(wxCURSOR_SIZENS));
                else GetCanvas()->SetCursor(wxNullCursor);
                return;
            }
            if( m_selected < 0 || m_selected >= int( m_design.interactions.size() ) ) return;
            auto& v = m_design.interactions[m_selected].visual;
            const double dx=(pos.x-m_start.x)/std::max<double>(body().GetWidth(),schIUScale.mmToIU(1));
            const double dy=(pos.y-m_start.y)/std::max<double>(body().GetHeight(),schIUScale.mmToIU(1));
            v=DragControlVisual(m_original,m_edges,dx,dy);
            UpdateControls(); m_changed();
        } );
        canvas->Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& ) { release(); } );
        canvas->Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_dragging = false; } );
        Bind( wxEVT_SIZE, [this]( wxSizeEvent& e ) { e.Skip(); CallAfter( [this] { fit(); } ); } );
    }
    ~INTERACTION_PREVIEW() override
    {
        release(); GetCanvas()->StopDrawing(); GetCanvas()->GetView()->Remove( m_overlay.get() );
    }
    void LoadSymbol()
    {
        release();
        if( m_design.libraryId.empty() ) SetStatusText( _( "Choose a component symbol to preview its controls" ) );
        else { LIB_ID id; id.Parse( W( m_design.libraryId ) ); DisplaySymbol( id, 1 ); fit(); }
        UpdateControls();
    }
    void UpdateControls()
    {
        m_overlay->Clear();
        if( m_previewItem )
            for( std::size_t i = 0; i < m_design.interactions.size(); ++i )
            {
                const auto& c = m_design.interactions[i]; const auto box = ControlBounds( body(), c.visual );
                m_overlay->SetIsFill( false ); m_overlay->SetIsStroke( true );
                m_overlay->SetLineWidth( schIUScale.mmToIU( .12 ) );
                m_overlay->SetStrokeColor( int( i ) == m_selected ? KIGFX::COLOR4D( 1, .55, .1, 1 ) : KIGFX::COLOR4D( .05, .7, .75, 1 ) );
                m_overlay->Rectangle( VECTOR2D( box.GetOrigin() ), VECTOR2D( box.GetEnd() ) );
                m_overlay->SetGlyphSize( VECTOR2I( schIUScale.mmToIU( 1 ), schIUScale.mmToIU( 1 ) ) );
                m_overlay->BitmapText( W( c.label ) + (c.visual.visible?wxString():" — "+_("hidden during simulation")), box.GetOrigin() - VECTOR2I( 0, schIUScale.mmToIU( 1.4 ) ), ANGLE_0 );
                if(int(i)==m_selected)
                {
                    const double half=GetCanvas()->GetView()->ToWorld(3.);
                    const VECTOR2D origin(box.GetOrigin()),end(box.GetEnd());const auto center=(origin+end)/2.;
                    m_overlay->SetIsFill(true);m_overlay->SetFillColor(KIGFX::COLOR4D(1,.55,.1,1));
                    for(const auto& p:std::vector<VECTOR2D>{{origin.x,origin.y},{center.x,origin.y},{end.x,origin.y},{end.x,center.y},
                        {end.x,end.y},{center.x,end.y},{origin.x,end.y},{origin.x,center.y}})
                        m_overlay->Rectangle(p-VECTOR2D(half,half),p+VECTOR2D(half,half));
                }
            }
        GetCanvas()->GetView()->Update( m_overlay.get() ); GetCanvas()->ForceRefresh();
    }
    void FitControls() { fit(); }
private:
    BOX2I body() const { return m_previewItem->GetBodyBoundingBox(); }
    int edgesAt(const BOX2I& box,const VECTOR2I& pos) const
    {
        const double tolerance=GetCanvas()->GetView()->ToWorld(6.);
        int edges=0;
        if(std::abs(pos.x-box.GetOrigin().x)<=tolerance)edges|=CONTROL_LEFT;
        else if(std::abs(pos.x-box.GetEnd().x)<=tolerance)edges|=CONTROL_RIGHT;
        if(std::abs(pos.y-box.GetOrigin().y)<=tolerance)edges|=CONTROL_TOP;
        else if(std::abs(pos.y-box.GetEnd().y)<=tolerance)edges|=CONTROL_BOTTOM;
        return edges;
    }
    void fit()
    {
        if( !m_previewItem ) return;
        BOX2I extent = m_itemBBox;
        for( const auto& control : m_design.interactions ) extent.Merge( ControlBounds( body(), control.visual ) );
        extent.Inflate( schIUScale.mmToIU( 2 ) );
        auto* view = GetCanvas()->GetView(); view->SetScale( 1.0 );
        const auto size = GetCanvas()->GetClientSize();
        const auto area = view->ToWorld( VECTOR2D( size.x, size.y ), false );
        view->SetScale( .8 * std::min( std::abs( area.x / extent.GetWidth() ), std::abs( area.y / extent.GetHeight() ) ) );
        view->SetCenter( extent.Centre() ); UpdateControls();
    }
    void release() { m_dragging = false; if( GetCanvas()->HasCapture() ) GetCanvas()->ReleaseMouse(); }
    MODEL_DESIGN& m_design; int& m_selected; std::function<void()> m_changed;
    std::unique_ptr<KIGFX::VIEW_OVERLAY> m_overlay;
    bool m_dragging = false; int m_edges=0; VECTOR2I m_start; CONTROL_VISUAL m_original;
};

PANEL_MODEL_INTERACTIONS::PANEL_MODEL_INTERACTIONS( wxWindow* parent, KIWAY* kiway, MODEL_DESIGN& design,
                                                std::function<void()> changed ) :
    wxPanel( parent ), m_design( design ), m_changed( std::move( changed ) )
{
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* hint = new wxStaticText( this, wxID_ANY,
            _( "Expose a block's button or slider on the component. Drag its rectangle to position it, or its edges/corners to resize it. Edit control sets values and visibility.\n"
               "Use #name in a block value to follow a control without wires. Voltage inputs also have Output/Return; switch buttons connect Input/Output when ON." ) );
    hint->Wrap( 1180 ); layout->Add( hint, 0, wxEXPAND | wxALL, 10 );
    auto* buttons = new wxBoxSizer( wxHORIZONTAL );
    auto button = [&]( const wxString& label, std::function<void()> action )
    {
        auto* b = new wxButton( this, wxID_ANY, label ); buttons->Add( b, 0, wxRIGHT, 6 );
        b->Bind( wxEVT_BUTTON, [this, action]( wxCommandEvent& ) { invoke( action ); } );
    };
    button( _( "Expose block control..." ), [this] { addExisting(); } );
    button( _( "Add slider input" ), [this] { addInput( true ); } );
    button( _( "Add button input" ), [this] { addInput( false ); } );
    button( _( "Edit control..." ), [this] { edit(); } );
    button( _( "Remove control" ), [this]
    {
        if( m_selected >= 0 && m_selected < int( m_design.interactions.size() ) )
        { m_design.interactions.erase( m_design.interactions.begin() + m_selected ); m_selected = -1; updateList(); m_changed(); }
    } );
    layout->Add( buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    auto* workspace = new wxBoxSizer( wxHORIZONTAL );
    m_list = new wxListBox( this, wxID_ANY ); m_list->SetMinSize( FromDIP( wxSize( 290, 240 ) ) );
    workspace->Add( m_list, 0, wxEXPAND | wxRIGHT, 10 );
    m_preview = new INTERACTION_PREVIEW( this, kiway, design, m_selected, [this] { m_list->SetSelection( m_selected ); m_changed(); } );
    workspace->Add( m_preview, 1, wxEXPAND ); layout->Add( workspace, 1, wxEXPAND | wxALL, 10 ); SetSizer( layout );
    m_list->Bind( wxEVT_LISTBOX, [this]( wxCommandEvent& e ) { m_selected = e.GetSelection(); m_preview->UpdateControls(); } );
    m_list->Bind( wxEVT_LISTBOX_DCLICK, [this]( wxCommandEvent& ) { invoke( [this] { edit(); } ); } );
}
void PANEL_MODEL_INTERACTIONS::invoke( const std::function<void()>& action )
{
    try { action(); }
    catch( const std::exception& e ) { wxMessageBox( DiagnosticText( e.what() ), _( "Interactive controls" ), wxOK, this ); }
}
void PANEL_MODEL_INTERACTIONS::Reload()
{
    m_design.ConfigureInteractions(); updateList(); m_preview->LoadSymbol();
}
void PANEL_MODEL_INTERACTIONS::updateList()
{
    m_list->Clear();
    for( const auto& i : m_design.interactions ) m_list->Append( W( "#" + ControlReferenceName( i ) + " — " + i.label + " — " + i.block ) );
    if( m_selected >= int( m_design.interactions.size() ) ) m_selected = -1;
    m_list->SetSelection( m_selected ); m_preview->UpdateControls(); m_preview->FitControls();
}
void PANEL_MODEL_INTERACTIONS::addExisting()
{
    std::vector<DESIGN_INTERACTION> choices;
    wxArrayString labels;
    for( auto candidate : m_design.AvailableInteractions() )
        if( std::none_of( m_design.interactions.begin(), m_design.interactions.end(), [&]( const auto& i )
                { return i.block == candidate.block && i.control == candidate.control; } ) )
        { labels.Add( W( candidate.block + " — " + candidate.label ) ); choices.push_back( std::move( candidate ) ); }
    if( choices.empty() )
    {
        wxMessageBox( _( "No unexposed block controls. Add a slider/button input here, or add an interactive switch or potentiometer in Build behavior." ),
                      _( "Interactive controls" ), wxOK, this ); return;
    }
    wxSingleChoiceDialog picker( this, _( "Choose the internal block control to expose" ), _( "Expose block control" ), labels );
    if( picker.ShowModal() != wxID_OK ) return;
    auto interaction = choices.at( picker.GetSelection() );
    interaction.reference = NewReference( m_design, interaction.control.starts_with( "adjust:" ) );
    auto block = std::find_if( m_design.blocks.begin(), m_design.blocks.end(), [&]( const auto& b ) { return b.id == interaction.block; } );
    auto edited = *block; INTERACTION_DIALOG dialog( this, interaction, edited );
    if( dialog.ShowModal() != wxID_OK ) return;
    *block = std::move( edited ); m_design.interactions.push_back( std::move( interaction ) );
    m_selected = m_design.interactions.size() - 1; updateList(); m_changed();
}
void PANEL_MODEL_INTERACTIONS::addInput( bool slider )
{
    const std::string filename = slider ? "interactive_value.casim" : "interactive_switch.casim";
    DESIGN_BLOCK block;
    for( const auto& dir : MODEL_DESIGN::BlockDirectories() )
    {
        std::ifstream in( dir / filename ); if( in ) { block.modelJson.assign( std::istreambuf_iterator<char>( in ), {} ); break; }
    }
    if( block.modelJson.empty() ) throw std::runtime_error( "Interactive input block was not found" );
    int n = 1;
    do { block.id = "Input" + std::to_string( n++ ); }
    while( std::any_of( m_design.blocks.begin(), m_design.blocks.end(), [&]( const auto& b ) { return b.id == block.id; } ) );
    block.x = 300 + ( int( m_design.blocks.size() ) % 3 ) * 330;
    block.y = 80; for( const auto& b : m_design.blocks ) block.y = std::max( block.y, b.y + 220 );
    DESIGN_INTERACTION interaction{ block.id, slider ? "adjust:value" : "switch", slider ? "Value" : "Button",
        { true, 1.2, double( m_design.interactions.size() ) * .5, .5, .3 } };
    interaction.reference = NewReference( m_design, slider );
    INTERACTION_DIALOG dialog( this, interaction, block ); if( dialog.ShowModal() != wxID_OK ) return;
    m_design.blocks.push_back( std::move( block ) ); m_design.interactions.push_back( std::move( interaction ) );
    m_selected = m_design.interactions.size() - 1; updateList(); m_changed();
}
void PANEL_MODEL_INTERACTIONS::edit()
{
    if( m_selected < 0 || m_selected >= int( m_design.interactions.size() ) ) return;
    auto interaction = m_design.interactions[m_selected];
    auto block = std::find_if( m_design.blocks.begin(), m_design.blocks.end(), [&]( const auto& b ) { return b.id == interaction.block; } );
    if( block == m_design.blocks.end() ) throw std::runtime_error( "Interaction block was removed" );
    auto edited = *block; INTERACTION_DIALOG dialog( this, interaction, edited );
    if( dialog.ShowModal() != wxID_OK ) return;
    *block = std::move( edited ); m_design.interactions[m_selected] = std::move( interaction ); updateList(); m_changed();
}
}
