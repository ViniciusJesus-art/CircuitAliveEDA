/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include <circuitalive/model_author_api.h>
#include "model_design.h"
#include "model_design_canvas.h"
#include "model_design_presets.h"
#include "panel_led_appearance.h"
#include <cctype>
#include <charconv>
#include "model_profiles.h"
#include "session_runner.h"
#include <circuitalive/branding.h>
#include <dialog_shim.h>
#include <kiway.h>
#include <kiway_player.h>
#include <project.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <lib_symbol.h>
#include <sch_pin.h>
#include <pin_type.h>
#include <nlohmann/json.hpp>
#include <wx/button.h>
#include <wx/checkbox.h>
#include "choice_controls.h"
#include "model_browser.h"
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/utils.h>
#include <wx/choicdlg.h>
#include <wx/toolbar.h>
#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/accel.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <sim/platform_runtime.h>
namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
namespace fs = std::filesystem;
namespace
{
wxString W( const std::string& s ) { return wxString::FromUTF8( s ); }
wxString ParameterNumber(double value)
{
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,12);
    return W(std::string(buffer,result.ptr));
}
std::string Read( const fs::path& p )
{
    std::error_code error;
    if( fs::file_size( p, error ) > 512 * 1024 || error ) throw std::runtime_error( "Missing or oversized model: " + p.string() );
    std::ifstream in( p ); return std::string( std::istreambuf_iterator<char>( in ), {} );
}
void Error( wxWindow* p, const std::exception& e ) { wxMessageBox( DiagnosticText( e.what() ), _( "Simulation Model Editor" ), wxOK | wxICON_INFORMATION, p ); }
wxGrid* Grid( wxWindow* parent, const wxArrayString& columns )
{
    auto* g = new wxGrid( parent, wxID_ANY ); g->CreateGrid( 0, columns.size() );
    for( std::size_t i = 0; i < columns.size(); ++i ) g->SetColLabelValue( i, columns[i] );
    g->SetRowLabelSize( 30 ); g->SetDefaultColSize( 130 ); return g;
}
void Reset( wxGrid* grid ) { if( grid->GetNumberRows() ) grid->DeleteRows( 0, grid->GetNumberRows() ); }
void CommitGrid( wxGrid* grid ) { grid->SaveEditControlValue(); grid->HideCellEditControl(); }
struct TEMPLATE { std::string name, json; fs::path file; };
std::vector<TEMPLATE> Templates( const std::string& project )
{
    auto dirs = MODEL_DESIGN::BlockDirectories(); auto models = MODEL_REPOSITORY::DefaultDirectories( project );
    dirs.insert( dirs.end(), models.begin(), models.end() ); std::set<std::string> ids; std::vector<TEMPLATE> result;
    for( const auto& dir : dirs )
    {
        std::error_code error; if( !fs::is_directory( dir, error ) ) continue;
        for( const auto& file : fs::directory_iterator( dir ) ) if( file.is_regular_file() && file.path().extension() == ".casim" )
        {
            auto text = Read( file.path() ); auto model = MODEL_REPOSITORY::LoadText( text );
            if( ids.insert( model.id ).second ) result.push_back( { model.name, std::move( text ), file.path() } );
        }
    }
    std::sort( result.begin(), result.end(), []( const auto& a, const auto& b ) { return a.name < b.name; } ); return result;
}
class BLOCK_DIALOG : public DIALOG_SHIM
{
public:
    BLOCK_DIALOG( wxWindow* parent, DESIGN_BLOCK& block, const MODEL_DESIGN& design ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Block properties" ), wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ), m_block( block )
    {
        OptOut( this ); auto model = MODEL_REPOSITORY::LoadText( block.modelJson );
        auto* s = new wxBoxSizer( wxVERTICAL );
        s->Add( new wxStaticText( this, wxID_ANY, W( block.id ) + " — " + ModelText( model.name ) ), 0, wxALL, 10 );
        auto* explanation = new wxTextCtrl( this, wxID_ANY, ModelSummaryText( ModelBehaviorSummary( block.modelJson ) ),
            wxDefaultPosition, wxSize( -1, 110 ), wxTE_MULTILINE | wxTE_READONLY );
        s->Add( explanation, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        s->Add( new wxStaticText( this, wxID_ANY, _( "Connections: choose a component pin, select an existing wire or type a new wire name. Blank leaves a terminal open." ) ), 0, wxLEFT | wxRIGHT, 10 );
        m_pins = Grid( this, { _( "Terminal" ), _( "Role" ), _( "Connected to" ) } );
        wxArrayString choices; choices.Add( "" );
        for( const auto& p : design.pins ) choices.Add( W( "@" + p.id ) );
        std::set<std::string> wires;
        for( const auto& b : design.blocks ) for( const auto& [p, n] : b.connections ) if( !n.empty() && !n.starts_with( "@" ) ) wires.insert( n );
        for( const auto& wire : wires ) choices.Add( W( wire ) );
        for( const auto& p : model.pins )
        {
            int r = m_pins->GetNumberRows(); m_pins->AppendRows(); m_ports.push_back( p.id );
            m_pins->SetCellValue( r, 0, ModelText( p.label ) + " [" + W(p.id) + "]" ); m_pins->SetCellValue( r, 1, ModelText( p.role ) );
            auto it = block.connections.find( p.id ); if( it != block.connections.end() ) m_pins->SetCellValue( r, 2, W( it->second ) );
            m_pins->SetReadOnly( r, 0 ); m_pins->SetReadOnly( r, 1 ); m_pins->SetCellEditor( r, 2, new CLICK_CHOICE_EDITOR( choices, true ) );
        }
        m_pins->SetColSize( 0, 260 ); m_pins->SetColSize( 2, 220 ); s->Add( m_pins, 1, wxEXPAND | wxALL, 10 );
        s->Add( new wxStaticText( this, wxID_ANY, _( "Values: enter an SI value (10k, 100n, 5V), or choose @parameter to expose it in the component's configuration." ) ), 0, wxLEFT | wxRIGHT, 10 );
        auto* parameterPages=new wxNotebook(this,wxID_ANY);
        auto* electrical=new wxPanel(parameterPages);auto* valueLayout=new wxBoxSizer(wxVERTICAL);
        m_values = Grid( electrical, { _( "Parameter" ), _( "Value" ), _( "Unit" ) } );
        wxArrayString parameterChoices;
        for( const auto& p : design.parameters ) parameterChoices.Add( W( "@" + p.id ) );
        for( const auto& p : model.parameters )
        {
            int r = m_values->GetNumberRows(); m_values->AppendRows(); m_parameters.push_back( p.id );
            m_values->SetCellValue( r, 0, ModelText( p.label ) ); m_values->SetReadOnly( r, 0 ); m_values->SetReadOnly( r, 2 );
            auto it = block.values.find( p.id ); m_values->SetCellValue( r, 1, W( it == block.values.end() ? p.defaultValue : it->second ) );
            m_values->SetCellValue( r, 2, W( p.unit ) );
            m_values->SetCellEditor( r, 1, new CLICK_CHOICE_EDITOR( parameterChoices, true ) );
        }
        m_values->SetColSize( 0, 290 ); m_values->SetColSize( 1, 220 );valueLayout->Add(m_values,1,wxEXPAND);electrical->SetSizer(valueLayout);
        parameterPages->AddPage(electrical,_("Parameters"));
        if(model.ledAppearance.Configurable())
        {
            auto* appearance=new PANEL_LED_APPEARANCE(parameterPages);
            appearance->Configure(model,[this,&model,&design](const std::string& id)
            {
                const auto it=std::find(m_parameters.begin(),m_parameters.end(),id);const auto r=it-m_parameters.begin();
                auto value=m_values->GetCellValue(r,1).ToStdString(wxConvUTF8);
                if(value.starts_with('@'))for(const auto& p:design.parameters)if(p.id==value.substr(1)){value=p.defaultValue;break;}
                try{return ParseComponentValue(value);}catch(const std::exception&){return ParseComponentValue(model.parameters.at(r).defaultValue);}
            },[this](const std::string& id,const std::string& value)
            {auto it=std::find(m_parameters.begin(),m_parameters.end(),id);if(it!=m_parameters.end())m_values->SetCellValue(it-m_parameters.begin(),1,W(value));});
            for(std::size_t r=0;r<model.parameters.size();++r)
                if(std::find(model.ledAppearance.ids.begin(),model.ledAppearance.ids.end(),model.parameters[r].id)!=model.ledAppearance.ids.end())m_values->HideRow(r);
            parameterPages->AddPage(appearance,_("LED appearance"));
        }
        s->Add(parameterPages,1,wxEXPAND|wxALL,10);
        s->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 ); SetSizer( s );
        auto display = wxGetClientDisplayRect(); SetSize( 760, std::min( 760, display.height - 60 ) ); SetMinSize( { 650, 500 } ); Centre();
        Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            CommitGrid( m_pins ); CommitGrid( m_values );
            m_block.connections.clear(); m_block.values.clear();
            for( std::size_t r = 0; r < m_ports.size(); ++r )
                if( auto v = m_pins->GetCellValue( r, 2 ).ToStdString(wxConvUTF8); !v.empty() ) m_block.connections[m_ports[r]] = v;
            for( std::size_t r = 0; r < m_parameters.size(); ++r ) m_block.values[m_parameters[r]] = m_values->GetCellValue( r, 1 ).ToStdString(wxConvUTF8);
            EndModal( wxID_OK );
        }, wxID_OK );
    }
private:
    DESIGN_BLOCK& m_block; wxGrid *m_pins, *m_values; std::vector<std::string> m_ports, m_parameters;
};
class MODEL_AUTHOR : public DIALOG_SHIM
{
public:
    MODEL_AUTHOR( wxWindow* parent, KIWAY* kiway ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Simulation Model Editor — CircuitAliveEDA" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ), m_kiway( kiway ),
        m_project( kiway->Prj().GetProjectPath().ToStdString(wxConvUTF8) )
    {
        OptOut( this ); SetInitialFocus( nullptr ); auto* s = new wxBoxSizer( wxVERTICAL );
        s->Add( createToolbar(), 0, wxEXPAND | wxBOTTOM, 10 );
        auto* fields = new wxBoxSizer( wxHORIZONTAL );
        fields->Add( new wxStaticText( this, wxID_ANY, _( "Name:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        m_name = new wxTextCtrl( this, wxID_ANY ); fields->Add( m_name, 2, wxRIGHT, 12 );
        fields->Add( new wxStaticText( this, wxID_ANY, _( "Model ID:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        m_id = new wxTextCtrl( this, wxID_ANY ); fields->Add( m_id, 1 ); s->Add( fields, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_symbol = new wxStaticText( this, wxID_ANY, "" ); s->Add( m_symbol, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_pages = new wxNotebook( this, wxID_ANY );
        auto* behavior = new wxPanel( m_pages ); auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* hint = new wxStaticText( behavior, wxID_ANY,
            _( "Connect: click two terminals. Move: drag a block or a wire segment. Select a block to highlight its nets. Double-click to edit. Esc cancels a connection." ) );
        hint->Wrap( 1240 ); layout->Add( hint, 0, wxALL, 8 );
        auto* workspace = new wxBoxSizer( wxHORIZONTAL );
        m_canvas = new DESIGN_CANVAS( behavior, m_design );
        m_canvas->changed = [this] { changed(); };
        m_canvas->selectionChanged = [this] { inspectSelection(); };
        m_canvas->edit = [this] { try { collect(); editBlock(); } catch( const std::exception& e ) { Error( this, e ); } };
        workspace->Add( m_canvas, 1, wxEXPAND );
        m_inspector = new wxTextCtrl( behavior, wxID_ANY, "", wxDefaultPosition, wxSize( 300, -1 ), wxTE_MULTILINE | wxTE_READONLY );
        m_inspector->SetMinSize( { 260, -1 } ); workspace->Add( m_inspector, 0, wxEXPAND | wxLEFT, 8 );
        layout->Add( workspace, 1, wxEXPAND ); behavior->SetSizer( layout ); m_pages->AddPage( behavior, _( "Build behavior" ) );
        auto* pins = new wxPanel( m_pages ); auto* ps = new wxBoxSizer( wxVERTICAL );
        ps->Add( new wxStaticText( pins, wxID_ANY, _( "Choose a symbol to import every physical pin, including all units. Port IDs identify the terminals inside this model." ) ), 0, wxALL, 10 );
        m_pins = Grid( pins, { _( "Physical pin" ), _( "Port ID" ), _( "Name" ), _( "Role" ) } );
        m_pins->SetColSize( 2, 260 ); ps->Add( m_pins, 1, wxEXPAND | wxALL, 10 ); pins->SetSizer( ps ); m_pages->AddPage( pins, _( "Component pins" ) );
        auto* params = new wxPanel( m_pages ); auto* vs = new wxBoxSizer( wxVERTICAL ); auto* pt = new wxBoxSizer( wxHORIZONTAL );
        auto* add = new wxButton( params, wxID_ANY, _( "Add parameter" ) ); auto* remove = new wxButton( params, wxID_ANY, _( "Remove selected parameter" ) );
        pt->Add( add, 0, wxRIGHT, 6 ); pt->Add( remove ); vs->Add( pt, 0, wxALL, 10 );
        vs->Add( new wxStaticText( params, wxID_ANY, _( "Public parameters appear in Simulation model... on the schematic. Use @ID in a block's value to link it to a public parameter." ) ), 0, wxLEFT | wxRIGHT, 10 );
        m_parameters = Grid( params, { _( "ID" ), _( "Name" ), _( "Default" ), _( "Unit" ), _( "Minimum" ), _( "Maximum" ) } );
        m_parameters->SetColSize( 1, 280 ); vs->Add( m_parameters, 1, wxEXPAND | wxALL, 10 ); params->SetSizer( vs ); m_pages->AddPage( params, _( "Parameters" ) );
        add->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            int r = m_parameters->GetNumberRows(); m_parameters->AppendRows();
            m_parameters->SetCellValue( r, 0, "parameter" + std::to_string( r + 1 ) ); m_parameters->SetCellValue( r, 1, _( "New parameter" ) );
            m_parameters->SetCellValue( r, 2, "1" ); m_parameters->SetCellValue( r, 4, "0" ); m_parameters->SetCellValue( r, 5, "1e6" ); m_dirty = true;
        } );
        remove->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { const int r = m_parameters->GetGridCursorRow(); if( r >= 0 && r < m_parameters->GetNumberRows() ) { m_parameters->DeleteRows( r ); m_dirty = true; } } );
        auto* test = new wxPanel( m_pages ); auto* ts = new wxBoxSizer( wxVERTICAL );
        ts->Add( new wxStaticText( test, wxID_ANY, _( "Bench test: set each input/supply (5V, 0V), an output load (load:10k), or leave it blank for an open terminal. This does not change the model." ) ), 0, wxALL, 10 );
        auto* tr = new wxBoxSizer( wxHORIZONTAL ); tr->Add( new wxStaticText( test, wxID_ANY, _( "Symbol Value:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
        m_testValue = new wxTextCtrl( test, wxID_ANY, "1k" ); tr->Add( m_testValue, 1, wxRIGHT, 10 );
        tr->Add( new wxStaticText( test, wxID_ANY, _( "Duration:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5 );
        m_testTime = new wxTextCtrl( test, wxID_ANY, "20m" ); tr->Add( m_testTime, 1, wxRIGHT, 10 );
        auto* run = new wxButton( test, wxID_ANY, _( "Run test" ) ); tr->Add( run ); ts->Add( tr, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_stimuli = Grid( test, { _( "Pin" ), _( "Stimulus / load" ) } ); m_stimuli->SetColSize( 0, 320 ); m_stimuli->SetColSize( 1, 250 );
        ts->Add( m_stimuli, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );
        m_results = new wxTextCtrl( test, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY );
        ts->Add( m_results, 1, wxEXPAND | wxALL, 10 ); test->SetSizer( ts ); m_pages->AddPage( test, _( "Test model" ) );
        run->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { try { testModel(); } catch( const std::exception& e ) { Error( this, e ); } } );
        s->Add( m_pages, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );
        m_associate = new wxCheckBox( this, wxID_ANY, _( "Use this model for new symbols of the selected component" ) ); m_associate->SetValue( true );
        s->Add( m_associate, 0, wxALL, 10 ); m_status = new wxStaticText( this, wxID_ANY, "" ); s->Add( m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        auto* close = new wxButton( this, wxID_CLOSE, _( "Close" ) ); s->Add( close, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 10 ); SetSizer( s );
        const auto display = wxGetClientDisplayRect(); SetSize( std::min( 1400, display.width - 60 ), std::min( 950, display.height - 60 ) ); SetMinSize( { 950, 620 } ); Centre();
        m_name->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { if( !m_loading ) m_dirty = true; } ); m_id->Bind( wxEVT_TEXT, [this]( wxCommandEvent& ) { if( !m_loading ) m_dirty = true; } );
        for( auto* g : { m_pins, m_parameters } ) g->Bind( wxEVT_GRID_CELL_CHANGED, [this]( wxGridEvent& e ) { m_dirty = true; e.Skip(); } );
        m_pages->Bind( wxEVT_NOTEBOOK_PAGE_CHANGED, [this]( wxBookCtrlEvent& e ) { if( !m_loading ) { try { collect(); m_canvas->Refresh(); inspectSelection(); } catch( const std::exception& error ) { Error( this, error ); } } e.Skip(); } );
        close->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { if( discard() ) EndModal( wxID_CLOSE ); } );
        Bind( wxEVT_CLOSE_WINDOW, [this]( wxCloseEvent& e ) { if( discard() ) EndModal( wxID_CLOSE ); else if( e.CanVeto() ) e.Veto(); } );
        Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { if( discard() ) EndModal( wxID_CANCEL ); }, wxID_CANCEL );
        refreshFields();
    }
private:
    wxToolBar* createToolbar()
    {
        auto* bar = new wxToolBar( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT | wxTB_NODIVIDER );
        bar->SetToolBitmapSize( {16,16} );
        bar->SetMargins(FromDIP(10),FromDIP(6)); bar->SetToolPacking(FromDIP(12));
        auto* file = new wxMenu; auto* model = new wxMenu; auto* block = new wxMenu; auto* view = new wxMenu;
        std::vector<wxAcceleratorEntry> shortcuts;
        auto command = [&]( wxMenu* menu, const wxString& label, std::function<void()> action,
                            int flags = 0, int key = 0 )
        {
            const int id = wxWindow::NewControlId(); menu->Append( id, label );
            auto invoke = [this, action]( wxCommandEvent& )
            { try { action(); } catch( const std::exception& e ) { Error( this, e ); } };
            Bind( wxEVT_MENU, invoke, id ); Bind( wxEVT_TOOL, invoke, id );
            if( key ) shortcuts.emplace_back( flags, key, id );
            return id;
        };
        command( file, _( "New\tCtrl+N" ), [this] { if( discard() ) { m_design = {}; m_file.clear(); refreshFields(); m_dirty = false; } }, wxACCEL_CTRL, 'N' );
        command( file, _( "Open...\tCtrl+O" ), [this] { open(); }, wxACCEL_CTRL, 'O' );
        command( file, _( "Save\tCtrl+S" ), [this] { save( false ); }, wxACCEL_CTRL, 'S' );
        command( file, _( "Save as...\tCtrl+Shift+S" ), [this] { save( true ); }, wxACCEL_CTRL | wxACCEL_SHIFT, 'S' );
        command( model, _( "Choose component symbol..." ), [this] { chooseSymbol(); } );
        command( model, _( "Start from existing model..." ), [this] { fromTemplate(); } );
        command( model, _( "555 example" ), [this] { timerExample(); } );
        command( model, _( "Run bench test\tCtrl+R" ), [this] { m_pages->SetSelection(3); testModel(); }, wxACCEL_CTRL, 'R' );
        const int addId = command( block, _( "Add block...\tCtrl+B" ), [this] { collect(); addBlock(); m_pages->SetSelection(0); }, wxACCEL_CTRL, 'B' );
        const int editId = command( block, _( "Edit selected block...\tCtrl+E" ), [this] { collect(); editBlock(); }, wxACCEL_CTRL, 'E' );
        command( block, _( "Remove selected block" ), [this]
        { if( selected() ) { const auto id=m_design.blocks[m_canvas->selected].id; m_design.RemoveBlock(id); m_canvas->selected=-1; changed(); inspectSelection(); } } );
        command( block, _( "Disconnect selected block" ), [this]
        { if( selected() ) { m_design.blocks[m_canvas->selected].connections.clear(); changed(); inspectSelection(); } } );
        block->AppendSeparator();
        command( block, _( "Cancel connection" ), [this] { m_canvas->CancelConnection(); } );
        command( block, _( "Route all wires automatically" ), [this] { m_canvas->ResetRoutes(); } );
        command( view, _( "Zoom in" ), [this] { m_canvas->Zoom(1.25); } );
        command( view, _( "Zoom out" ), [this] { m_canvas->Zoom(.8); } );
        command( view, _( "Fit content\tCtrl+F" ), [this] { m_canvas->FitContent(); }, wxACCEL_CTRL, 'F' );
        command( view, _( "Arrange blocks in rows" ), [this]
        {
            int y=70;
            for( std::size_t i=0; i<m_design.blocks.size(); )
            {
                int rowHeight=0;
                for( int col=0; col<3 && i<m_design.blocks.size(); ++col, ++i )
                {
                    auto& b=m_design.blocks[i]; b.x=300+col*340; b.y=y;
                    rowHeight=std::max(rowHeight,115+int(MODEL_REPOSITORY::LoadText(b.modelJson).pins.size())*23);
                }
                y+=rowHeight+70;
            }
            m_canvas->ResetRoutes(); m_canvas->FitContent();
        } );
        auto dropdown = [&]( const wxString& label, wxMenu* menu, const wxArtID& icon )
        {
            auto* button = new wxButton(bar,wxID_ANY,label+"  ▾",wxDefaultPosition,FromDIP(wxSize(112,40)));
            button->SetBitmap(wxArtProvider::GetBitmap(icon,wxART_TOOLBAR,{16,16}),wxLEFT);
            button->SetBitmapMargins(FromDIP(7),0); bar->AddControl(button);
            // Icon, caption and arrow are one control with one click action.
            auto owned = std::shared_ptr<wxMenu>(menu);
            button->Bind(wxEVT_BUTTON,[button,owned](wxCommandEvent&)
                { button->PopupMenu(owned.get(),wxPoint(0,button->GetClientSize().y)); });
        };
        dropdown( _( "&File" ), file, wxART_FILE_OPEN ); dropdown( _( "&Model" ), model, wxART_EXECUTABLE_FILE );
        dropdown( _( "&Block" ), block, wxART_LIST_VIEW ); dropdown( _( "&View" ), view, wxART_FIND );
        bar->AddSeparator();
        auto tool = [&]( int id, const wxString& label, const wxArtID& icon, const wxString& help )
        {
            auto* button = new wxButton(bar,wxID_ANY,label,wxDefaultPosition,FromDIP(wxSize(128,40)));
            button->SetBitmap(wxArtProvider::GetBitmap(icon,wxART_TOOLBAR,{16,16}),wxLEFT);
            button->SetBitmapMargins(FromDIP(7),0);button->SetToolTip(help);bar->AddControl(button);
            button->Bind(wxEVT_BUTTON,[this,id](wxCommandEvent&)
                { wxCommandEvent action(wxEVT_MENU,id);GetEventHandler()->ProcessEvent(action); });
        };
        tool(addId,_("Add block"),wxART_PLUS,_("Add a building block (Ctrl+B)"));
        tool(editId,_("Edit block"),wxART_EDIT,_("Edit selected block (Ctrl+E)"));
        bar->Realize(); SetAcceleratorTable( wxAcceleratorTable( shortcuts.size(), shortcuts.data() ) );
        // Dispatch toolbar shortcuts consistently while a text field or grid owns the focus.
        Bind(wxEVT_CHAR_HOOK,[this,shortcuts](wxKeyEvent& e)
        {
            const int flags=(e.ControlDown()?wxACCEL_CTRL:0)|(e.ShiftDown()?wxACCEL_SHIFT:0)|(e.AltDown()?wxACCEL_ALT:0);
            for(const auto& shortcut:shortcuts) if(shortcut.GetFlags()==flags&&shortcut.GetKeyCode()==e.GetKeyCode())
            {
                wxCommandEvent action(wxEVT_MENU,shortcut.GetCommand());action.SetEventObject(this);
                GetEventHandler()->ProcessEvent(action);return;
            }
            e.Skip();
        });
        return bar;
    }
    void inspectSelection()
    {
        if( !selected() )
        {
            const auto intro = m_design.blocks.empty() ? wxString() : ModelText(m_design.name)+"\n\n"+ModelText(m_design.description)+"\n\n";
            m_inspector->ChangeValue( intro + _( "Build a component from connected blocks.\n\nStart with Model > Start from existing model, or choose a component symbol and add blocks.\n\nSelect a block to read its operation and see all its connections highlighted. Double-click to change values or connections.\n\nDrag any wire segment to make room. Your routes are saved with the model.\n\nIdeal logic and condition blocks need no supply pins; their voltages are relative to circuit ground. Powered blocks model supply behavior." ) );
            return;
        }
        const auto& b = m_design.blocks[m_canvas->selected]; const auto m = MODEL_REPOSITORY::LoadText(b.modelJson);
        wxString text = W(b.id) + " — " + ModelSummaryText(ModelBehaviorSummary(b.modelJson)) + "\n" + _("Connections") + "\n";
        for( const auto& p : m.pins )
        { auto it=b.connections.find(p.id); text += ModelText(p.label) + " → " + (it==b.connections.end() || it->second.empty() ? _("Open") : W(it->second)) + "\n"; }
        text += "\n" + _("Values") + "\n";
        for( const auto& p : m.parameters )
        {
            auto it = b.values.find(p.id); auto value = it==b.values.end() ? p.defaultValue : it->second;
            if( value.starts_with('@') )
                for( const auto& q : m_design.parameters ) if( q.id==value.substr(1) ) { value += " = " + q.defaultValue; break; }
            text += ModelText(p.label) + ": " + W(value);
            if(!p.unit.empty() && !value.ends_with(p.unit) && !(p.unit=="V" && value.ends_with("v"))) text += " " + W(p.unit);
            text += "\n";
        }
        m_inspector->ChangeValue(text);
    }

    void changed() { m_dirty = true; m_canvas->Refresh(); }
    bool selected() { return m_canvas->selected >= 0 && std::size_t( m_canvas->selected ) < m_design.blocks.size(); }
    static std::string roleId(const wxString& label)
    {
        for(const auto* role : {"input", "output", "bidirectional", "passive", "power", "reference"})
            if(label == ModelText(role)) return role;
        return label.ToStdString(wxConvUTF8);
    }
    void collect()
    {
        CommitGrid( m_pins ); CommitGrid( m_parameters ); m_design.name = m_name->GetValue().ToStdString(wxConvUTF8); m_design.id = m_id->GetValue().ToStdString(wxConvUTF8);
        m_design.pins.clear(); m_design.parameters.clear();
        for( int r = 0; r < m_pins->GetNumberRows(); ++r ) m_design.pins.push_back( {
            m_pins->GetCellValue( r, 1 ).ToStdString(wxConvUTF8), m_pins->GetCellValue( r, 2 ).ToStdString(wxConvUTF8),
            roleId(m_pins->GetCellValue(r,3)), m_pins->GetCellValue( r, 0 ).ToStdString(wxConvUTF8) } );
        for( int r = 0; r < m_parameters->GetNumberRows(); ++r ) m_design.parameters.push_back( {
            m_parameters->GetCellValue( r, 0 ).ToStdString(wxConvUTF8), m_parameters->GetCellValue( r, 1 ).ToStdString(wxConvUTF8),
            m_parameters->GetCellValue( r, 3 ).ToStdString(wxConvUTF8), m_parameters->GetCellValue( r, 2 ).ToStdString(wxConvUTF8),
            ParseComponentValue( m_parameters->GetCellValue( r, 4 ).ToStdString(wxConvUTF8) ), ParseComponentValue( m_parameters->GetCellValue( r, 5 ).ToStdString(wxConvUTF8) ) } );
    }
    void refreshFields()
    {
        m_loading = true; m_name->SetValue( W( m_design.name ) ); m_id->SetValue( W( m_design.id ) );
        m_symbol->SetLabel( m_design.libraryId.empty() ? _( "Component: choose a symbol to import its pins" ) : _( "Component: " ) + W( m_design.libraryId ) );
        Reset( m_pins ); Reset( m_parameters ); Reset( m_stimuli );
        for( const auto& p : m_design.pins )
        {
            const int r = m_pins->GetNumberRows(); m_pins->AppendRows(); m_pins->SetCellValue( r, 0, W( p.defaultPin ) );
            m_pins->SetCellValue( r, 1, W( p.id ) ); m_pins->SetCellValue( r, 2, W( p.label ) ); m_pins->SetCellValue( r, 3, ModelText( p.role ) );
            wxArrayString roles;
            for( const auto* role : {"input", "output", "bidirectional", "passive", "power", "reference"} ) roles.Add(ModelText(role));
            m_pins->SetCellEditor( r, 3, new CLICK_CHOICE_EDITOR( roles, false ) );
            m_stimuli->AppendRows(); m_stimuli->SetCellValue( r, 0, W(p.defaultPin) + " — " + ModelText(p.label) ); m_stimuli->SetReadOnly( r, 0 );
            m_stimuli->SetCellValue( r, 1, p.role == "power" ? "5V" : p.role == "reference" || p.role == "input" ? "0V" : p.role == "output" ? "load:10k" : "" );
        }
        for( const auto& p : m_design.parameters )
        {
            int r = m_parameters->GetNumberRows(); m_parameters->AppendRows();
            m_parameters->SetCellValue( r, 0, W( p.id ) ); m_parameters->SetCellValue( r, 1, W( p.label ) ); m_parameters->SetCellValue( r, 2, W( p.defaultValue ) );
            m_parameters->SetCellValue( r, 3, W( p.unit ) ); m_parameters->SetCellValue( r, 4, ParameterNumber(p.minimum) ); m_parameters->SetCellValue( r, 5, ParameterNumber(p.maximum) );
        }
        m_canvas->selected = m_design.blocks.size()==1 ? 0 : -1;
        m_canvas->CancelConnection(); m_canvas->Refresh(); m_results->Clear(); inspectSelection();
        m_associate->Enable( !m_design.libraryId.empty() ); m_status->SetLabel( m_file.empty() ? _( "Not saved" ) : W( m_file.string() ) ); m_loading = false; Layout();
    }
    bool discard()
    {
        if( !m_dirty ) return true;
        int answer = wxMessageBox( _( "Save changes to this simulation model?" ), _( "Unsaved model" ), wxYES_NO | wxCANCEL | wxICON_QUESTION, this );
        if( answer == wxCANCEL ) return false;
        if( answer == wxNO ) return true;
        try { return save( false ); } catch( const std::exception& e ) { Error( this, e ); return false; }
    }
    void open()
    {
        if( !discard() ) return;
        wxFileDialog picker( this, _( "Open simulation model" ), W( m_project ), "", _("CircuitAlive model (*.casim)|*.casim"), wxFD_OPEN | wxFD_FILE_MUST_EXIST );
        if( picker.ShowModal() != wxID_OK ) return;
        auto doc = MODEL_DESIGN::Open( Read( fs::path( picker.GetPath().ToStdString(wxConvUTF8) ) ) );
        doc.MakeCoreVisible();
        m_design = std::move( doc ); m_file = picker.GetPath().ToStdString(wxConvUTF8); refreshFields(); m_canvas->FitContent(); m_dirty = false;
    }
    bool save( bool as )
    {
        collect(); const auto text = m_design.Export(); auto path = m_file;
        if( path.empty() || as )
        {
            fs::path folder = path.empty() ? fs::path( m_project ) / "models" : path.parent_path();
            // The user chooses the destination; opening the picker creates no directory.
            if( !fs::is_directory( folder ) ) folder = m_project;
            wxFileDialog picker( this, _( "Save simulation model" ), W( folder.string() ), W( m_design.id + ".casim" ),
                _("CircuitAlive model (*.casim)|*.casim"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT );
            if( picker.ShowModal() != wxID_OK ) return false;
            path = picker.GetPath().ToStdString(wxConvUTF8); if( path.extension() != ".casim" ) path += ".casim";
        }
        const auto temporary = path.string() + ".tmp." + std::to_string( ProcessId() );
        try
        {
            std::ofstream out( temporary ); out.exceptions( std::ios::failbit | std::ios::badbit ); out << text; out.close(); ReplaceSavedFile( temporary, path );
        }
        catch( ... ) { std::error_code error; fs::remove( temporary, error ); throw; }
        m_file = path;
        if( m_associate->GetValue() && !m_design.libraryId.empty() )
        {
            CIRCUIT_COMPONENT c; c.libraryId = m_design.libraryId; c.modelFile = fs::absolute( path ).string(); c.projectDirectory = m_project;
            for( const auto& p : m_design.pins ) c.modelPins[p.id] = p.defaultPin;
            MODEL_PROFILES().Save( c );
        }
        m_dirty = false; m_status->SetLabel( _( "Saved: " ) + W( path.string() ) ); return true;
    }
    void chooseSymbol()
    {
        collect(); wxString picked = W( m_design.libraryId );
        auto* chooser = m_kiway->Player( FRAME_SYMBOL_CHOOSER, true, this ); if( !chooser ) return;
        bool accepted = chooser->ShowModal( &picked, this ); chooser->Destroy(); if( !accepted ) return;
        LIB_ID id; id.Parse( picked, true );
        auto* symbol = PROJECT_SCH::SymbolLibAdapter( &m_kiway->Prj() )->LoadSymbol( id );
        if( !symbol ) throw std::runtime_error( _("Cannot load chosen symbol").ToStdString(wxConvUTF8) );
        auto flat = symbol->Flatten(); auto physical = flat->GetPins();
        std::sort( physical.begin(), physical.end(), []( const auto* a, const auto* b ) { return a->GetNumber() < b->GetNumber(); } );
        std::vector<MODEL_PIN> ports; std::set<std::string> used, numbers;
        for( const auto* p : physical )
        {
            const auto number = p->GetNumber().ToStdString(wxConvUTF8); if( !numbers.insert( number ).second ) continue;
            auto existing = std::find_if( m_design.pins.begin(), m_design.pins.end(), [&]( const auto& pin ) { return pin.defaultPin == number; } );
            if( existing != m_design.pins.end() ) { ports.push_back( *existing ); used.insert( existing->id ); continue; }
            std::string label = p->GetName().ToStdString(wxConvUTF8), logical;
            for( unsigned char c : label ) if( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) logical += char( std::tolower( c ) );
            if( logical.empty() || used.contains( logical ) ) logical = "pin" + number;
            while( used.contains( logical ) ) logical += "_";
            used.insert( logical );
            std::string role = "passive";
            switch( p->GetType() )
            {
            case ELECTRICAL_PINTYPE::PT_INPUT: role = "input"; break;
            case ELECTRICAL_PINTYPE::PT_OUTPUT: case ELECTRICAL_PINTYPE::PT_POWER_OUT:
            case ELECTRICAL_PINTYPE::PT_TRISTATE: case ELECTRICAL_PINTYPE::PT_OPENCOLLECTOR: role = "output"; break;
            case ELECTRICAL_PINTYPE::PT_BIDI: role = "bidirectional"; break;
            case ELECTRICAL_PINTYPE::PT_POWER_IN: role = "power"; break;
            default: break;
            }
            if( logical == "gnd" || logical == "vss" || logical == "vee" ) role = "reference";
            ports.push_back( { logical, label.empty() ? number : label, role, number } );
        }
        m_design.pins = std::move( ports ); m_design.libraryId = picked.ToStdString(wxConvUTF8); refreshFields(); changed();
    }
    void fromTemplate()
    {
        if( !discard() ) return;
        auto choices = Templates( m_project );
        std::vector<SIMULATION_MODEL> models;
        for( const auto& c : choices ) models.push_back( MODEL_REPOSITORY::LoadText( c.json, c.file ) );
        const auto selected = ChooseSimulationModel( this, models ); if( !selected ) return;
        m_design = MODEL_DESIGN::Open( choices.at( *selected ).json ); m_design.MakeCoreVisible(); m_design.id = "custom_" + m_design.id;
        m_design.name = "Custom " + m_design.name; m_file.clear(); refreshFields(); m_pages->SetSelection(0); m_canvas->FitContent(); changed();
    }
    void timerExample()
    {
        if( !discard() ) return;
        std::map<std::string, std::string> choices;
        for( const auto& c : Templates( m_project ) ) choices[MODEL_REPOSITORY::LoadText( c.json ).id] = c.json;
        m_design = MakeTimer555Design( choices ); m_file.clear(); refreshFields(); m_pages->SetSelection( 0 ); m_canvas->FitContent(); changed();
    }
    void addBlock()
    {
        if( m_design.pins.empty() ) throw std::runtime_error( _("Choose the component symbol before adding blocks").ToStdString(wxConvUTF8) );
        auto choices = Templates( m_project );
        std::vector<SIMULATION_MODEL> models;
        for( const auto& c : choices ) models.push_back( MODEL_REPOSITORY::LoadText( c.json, c.file ) );
        const auto selected = ChooseSimulationModel( this, models ); if( !selected ) return;
        const auto& model = models.at( *selected );
        DESIGN_BLOCK block; block.modelJson = choices.at( *selected ).json;
        std::set<std::string> used; for( const auto& b : m_design.blocks ) used.insert( b.id );
        int n = 1; do { block.id = "B" + std::to_string( n++ ); } while( used.contains( block.id ) );
        block.y=std::numeric_limits<int>::max();
        for( int col=0; col<3; ++col )
        {
            const int x=280+col*320; int y=80;
            for( const auto& b:m_design.blocks ) if( x<b.x+240 && x+240>b.x )
                y=std::max(y,b.y+155+int(MODEL_REPOSITORY::LoadText(b.modelJson).pins.size())*23);
            if(y<block.y) {block.x=x;block.y=y;}
        }
        for( const auto& p : model.parameters )
        {
            if( p.defaultValue == "$value" ) block.values[p.id] = model.id == "resistor" ? "1k" : model.id == "capacitor" ? "100n" : model.id == "inductor" ? "1m" : "5";
        }
        BLOCK_DIALOG dialog( this, block, m_design ); if( dialog.ShowModal() != wxID_OK ) return;
        m_design.blocks.push_back( std::move( block ) ); m_canvas->selected = m_design.blocks.size() - 1; changed(); inspectSelection();
    }
    void editBlock()
    {
        if( !selected() ) return;
        auto block = m_design.blocks[m_canvas->selected];
        BLOCK_DIALOG dialog( this, block, m_design );
        if( dialog.ShowModal() == wxID_OK ) { m_design.blocks[m_canvas->selected] = std::move( block ); changed(); inspectSelection(); }
    }
    void testModel()
    {
        collect(); CommitGrid( m_stimuli ); const auto text = m_design.Export();
        const auto file = fs::temp_directory_path() / ( "circuitalive-author-test-" + std::to_string( ProcessId() ) + ".casim" );
        struct CLEANUP { fs::path path; ~CLEANUP() { std::error_code error; fs::remove( path, error ); } } cleanup{ file };
        { std::ofstream out( file ); out << text; if( !out ) throw std::runtime_error( _("Cannot prepare model test").ToStdString(wxConvUTF8) ); }
        CIRCUIT_COMPONENT root; root.reference = "U1"; root.libraryId = m_design.libraryId; root.modelFile = file.string(); root.value = m_testValue->GetValue().ToStdString(wxConvUTF8); root.projectDirectory = m_project;
        std::vector<CIRCUIT_COMPONENT> circuit;
        for( std::size_t i = 0; i < m_design.pins.size(); ++i )
        {
            const auto& p = m_design.pins[i]; const auto stimulus = i < std::size_t( m_stimuli->GetNumberRows() ) ? m_stimuli->GetCellValue( i, 1 ).ToStdString(wxConvUTF8) : "";
            auto node = "pin" + std::to_string( i + 1 ); if( p.role == "reference" && ( stimulus == "0V" || stimulus == "0" ) ) node = "0";
            root.pins[p.defaultPin] = node;
            if( stimulus.empty() || stimulus == "open" || node == "0" ) continue;
            const bool load = stimulus.starts_with( "load:" ); const auto value = load ? stimulus.substr( 5 ) : stimulus;
            circuit.push_back( { "T" + std::to_string( i + 1 ), load ? "Device:R" : "Simulation_SPICE:VDC", value,
                { { "1", node }, { "2", "0" } } } );
        }
        const auto index = circuit.size(); circuit.push_back( root ); auto snapshot = BuildCircuitSnapshot( circuit );
        std::vector<std::size_t> channels; snapshot.config.signals.clear();
        for( const auto& p : m_design.pins )
        {
            const auto node = snapshot.pinNodes[index].at( p.defaultPin );
            if( node == "0" ) channels.push_back( NO_SIGNAL );
            else { channels.push_back( snapshot.config.signals.size() ); snapshot.config.signals.push_back( "v(" + node + ")" ); }
        }
        if( snapshot.config.signals.empty() ) throw std::runtime_error( _("The test needs a non-ground terminal").ToStdString(wxConvUTF8) );
        const double duration = ParseComponentValue( m_testTime->GetValue().ToStdString(wxConvUTF8) );
        if( duration <= 0 || duration > 1 ) throw std::runtime_error( _("Test duration must be between 0 and 1 second").ToStdString(wxConvUTF8) );
        wxBusyCursor busy;
        INTERACTIVE_SIM_SESSION session( FindSessionWorker( PathUtf8(ExecutablePath()) ) );
        session.Configure( snapshot.netlist, snapshot.config ); session.AdvanceTo( duration ); const auto sample = session.Snapshot().latest;
        wxString report = wxString::Format(_("Test completed at t = %g s\n\n"), sample.time);
        for( std::size_t i = 0; i < m_design.pins.size(); ++i ) report += W(m_design.pins[i].defaultPin) + " — " + ModelText(m_design.pins[i].label) + ": "
            + W(FormatMeasurement( channels[i] == NO_SIGNAL ? 0 : sample.values.at( channels[i] ), MEASURE_QUANTITY::VOLTAGE )) + "\n";
        m_results->SetValue( report );
    }
    KIWAY* m_kiway; std::string m_project; MODEL_DESIGN m_design; fs::path m_file;
    wxTextCtrl *m_name, *m_id, *m_testValue, *m_testTime, *m_results, *m_inspector;
    wxStaticText *m_symbol, *m_status; wxNotebook* m_pages; wxGrid *m_pins, *m_parameters, *m_stimuli;
    DESIGN_CANVAS* m_canvas; wxCheckBox* m_associate; bool m_loading = false, m_dirty = false;
};
}
void ShowModelAuthor( wxWindow* parent, KIWAY* kiway )
{
    MODEL_AUTHOR dialog( parent, kiway ); dialog.ShowModal();
}
}
