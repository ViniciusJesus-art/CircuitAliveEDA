/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <circuitalive/model_author_api.h>
#include "model_design.h"
#include "model_design_presets.h"
#include <cctype>
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
#include <wx/choice.h>
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
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <unistd.h>
namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
namespace fs = std::filesystem;
namespace
{
wxString W( const std::string& s ) { return wxString::FromUTF8( s ); }
std::string Read( const fs::path& p )
{
    std::error_code error;
    if( fs::file_size( p, error ) > 512 * 1024 || error ) throw std::runtime_error( "Missing or oversized model: " + p.string() );
    std::ifstream in( p ); return std::string( std::istreambuf_iterator<char>( in ), {} );
}
void Error( wxWindow* p, const std::exception& e ) { wxMessageBox( W( e.what() ), _( "Simulation Model Editor" ), wxOK | wxICON_INFORMATION, p ); }
wxGrid* Grid( wxWindow* parent, const wxArrayString& columns )
{
    auto* g = new wxGrid( parent, wxID_ANY ); g->CreateGrid( 0, columns.size() );
    for( std::size_t i = 0; i < columns.size(); ++i ) g->SetColLabelValue( i, columns[i] );
    g->SetRowLabelSize( 30 ); g->SetDefaultColSize( 130 ); return g;
}
void Reset( wxGrid* grid ) { if( grid->GetNumberRows() ) grid->DeleteRows( 0, grid->GetNumberRows() ); }
void CommitGrid( wxGrid* grid ) { grid->SaveEditControlValue(); grid->HideCellEditControl(); }
struct TEMPLATE { std::string name, json; };
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
            if( ids.insert( model.id ).second ) result.push_back( { model.name, std::move( text ) } );
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
        s->Add( new wxStaticText( this, wxID_ANY, W( block.id + " — " + model.name ) ), 0, wxALL, 10 );
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
            m_pins->SetCellValue( r, 0, W( p.label + " [" + p.id + "]" ) ); m_pins->SetCellValue( r, 1, W( p.role ) );
            auto it = block.connections.find( p.id ); if( it != block.connections.end() ) m_pins->SetCellValue( r, 2, W( it->second ) );
            m_pins->SetReadOnly( r, 0 ); m_pins->SetReadOnly( r, 1 ); m_pins->SetCellEditor( r, 2, new wxGridCellChoiceEditor( choices, true ) );
        }
        m_pins->SetColSize( 0, 260 ); m_pins->SetColSize( 2, 220 ); s->Add( m_pins, 1, wxEXPAND | wxALL, 10 );
        s->Add( new wxStaticText( this, wxID_ANY, _( "Values: enter an SI value (10k, 100n, 5V), or choose @parameter to expose it in the component's configuration." ) ), 0, wxLEFT | wxRIGHT, 10 );
        m_values = Grid( this, { _( "Parameter" ), _( "Value" ), _( "Unit" ) } );
        wxArrayString parameterChoices;
        for( const auto& p : design.parameters ) parameterChoices.Add( W( "@" + p.id ) );
        for( const auto& p : model.parameters )
        {
            int r = m_values->GetNumberRows(); m_values->AppendRows(); m_parameters.push_back( p.id );
            m_values->SetCellValue( r, 0, W( p.label ) ); m_values->SetReadOnly( r, 0 ); m_values->SetReadOnly( r, 2 );
            auto it = block.values.find( p.id ); m_values->SetCellValue( r, 1, W( it == block.values.end() ? p.defaultValue : it->second ) );
            m_values->SetCellValue( r, 2, W( p.unit ) );
            m_values->SetCellEditor( r, 1, new wxGridCellChoiceEditor( parameterChoices, true ) );
        }
        m_values->SetColSize( 0, 290 ); m_values->SetColSize( 1, 220 ); s->Add( m_values, 1, wxEXPAND | wxALL, 10 );
        s->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 ); SetSizer( s );
        auto display = wxGetClientDisplayRect(); SetSize( 760, std::min( 760, display.height - 60 ) ); SetMinSize( { 650, 500 } ); Centre();
        Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            CommitGrid( m_pins ); CommitGrid( m_values );
            m_block.connections.clear(); m_block.values.clear();
            for( std::size_t r = 0; r < m_ports.size(); ++r )
                if( auto v = m_pins->GetCellValue( r, 2 ).ToStdString(); !v.empty() ) m_block.connections[m_ports[r]] = v;
            for( std::size_t r = 0; r < m_parameters.size(); ++r ) m_block.values[m_parameters[r]] = m_values->GetCellValue( r, 1 ).ToStdString();
            EndModal( wxID_OK );
        }, wxID_OK );
    }
private:
    DESIGN_BLOCK& m_block; wxGrid *m_pins, *m_values; std::vector<std::string> m_ports, m_parameters;
};
class DESIGN_CANVAS : public wxScrolledWindow
{
public:
    MODEL_DESIGN& design;
    std::function<void()> changed, edit;
    int selected = -1;
    DESIGN_CANVAS( wxWindow* parent, MODEL_DESIGN& doc ) : wxScrolledWindow( parent ), design( doc )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT ); SetScrollRate( 16, 16 ); SetVirtualSize( 2800, 2400 );
        Bind( wxEVT_PAINT, [this]( wxPaintEvent& ) { paint(); } );
        Bind( wxEVT_LEFT_DOWN, [this]( wxMouseEvent& e ) { down( e ); } );
        Bind( wxEVT_MOTION, [this]( wxMouseEvent& e )
        {
            if( m_drag >= 0 && e.LeftIsDown() )
            {
                auto p = logicalPosition( e.GetPosition() ) - m_offset;
                auto& b = design.blocks.at( m_drag ); b.x = std::max( 240, p.x ); b.y = std::max( 10, p.y );
                if( changed ) changed();
                Refresh();
            }
        } );
        Bind( wxEVT_LEFT_UP, [this]( wxMouseEvent& ) { m_drag = -1; if( HasCapture() ) ReleaseMouse(); } );
        Bind( wxEVT_MOUSE_CAPTURE_LOST, [this]( wxMouseCaptureLostEvent& ) { m_drag = -1; } );
        Bind( wxEVT_LEFT_DCLICK, [this]( wxMouseEvent& e ) { down( e ); m_drag = -1; if( HasCapture() ) ReleaseMouse(); if( selected >= 0 && edit ) edit(); } );
    }
    void CancelConnection() { m_pending.reset(); Refresh(); }
    void Zoom( double factor )
    {
        m_zoom = std::clamp( m_zoom * factor, .3, 2.0 ); updateExtent(); Refresh();
    }
    void FitContent()
    {
        const auto size = contentSize(); const auto client = GetClientSize();
        m_zoom = std::clamp( std::min( double( client.x - 24 ) / size.x,
                                      double( client.y - 24 ) / size.y ), .3, 1.0 );
        updateExtent(); Scroll( 0, 0 ); Refresh();
    }
private:
    struct TERMINAL { std::string block, port, net; wxPoint position; };
    wxPoint logicalPosition( wxPoint p ) const
    {
        p = CalcUnscrolledPosition( p ); return { int( p.x / m_zoom ), int( p.y / m_zoom ) };
    }
    wxSize contentSize() const
    {
        wxSize size( 260, 110 + int( design.pins.size() ) * 25 );
        for( const auto& b : design.blocks )
        {
            size.x = std::max( size.x, b.x + 250 );
            size.y = std::max( size.y, b.y + 100 + int( visualModel( b ).pins.size() ) * 23 );
        }
        return size;
    }
    void updateExtent()
    {
        const auto size = contentSize();
        SetVirtualSize( int( std::max( 2800, size.x ) * m_zoom ), int( std::max( 2400, size.y ) * m_zoom ) );
    }
    const SIMULATION_MODEL& visualModel( const DESIGN_BLOCK& b ) const
    {
        auto& entry = m_models[b.id];
        if( entry.first != b.modelJson ) { entry.second = MODEL_REPOSITORY::LoadText( b.modelJson ); entry.first = b.modelJson; }
        return entry.second;
    }
    mutable std::map<std::string, std::pair<std::string, SIMULATION_MODEL>> m_models;
    std::vector<TERMINAL> terminals() const
    {
        std::vector<TERMINAL> result;
        for( std::size_t i = 0; i < design.pins.size(); ++i ) result.push_back( { "", design.pins[i].id, "@" + design.pins[i].id, { 210, 76 + int( i ) * 25 } } );
        for( const auto& b : design.blocks )
        {
            const auto& model = visualModel( b );
            for( std::size_t i = 0; i < model.pins.size(); ++i )
            {
                const auto& p = model.pins[i]; bool right = p.role == "output" || p.role == "bidirectional";
                auto it = b.connections.find( p.id );
                result.push_back( { b.id, p.id, it == b.connections.end() ? "" : it->second, { b.x + ( right ? 220 : 0 ), b.y + 58 + int( i ) * 23 } } );
            }
        }
        return result;
    }
    void down( wxMouseEvent& e )
    {
        const auto point = logicalPosition( e.GetPosition() );
        for( const auto& t : terminals() ) if( std::abs( point.x - t.position.x ) <= 9 && std::abs( point.y - t.position.y ) <= 9 )
        {
            if( m_pending )
            {
                try { design.Connect( m_pending->block, m_pending->port, t.block, t.port ); if( changed ) changed(); }
                catch( const std::exception& error ) { Error( this, error ); }
                m_pending.reset();
            }
            else m_pending = t;
            Refresh(); return;
        }
        m_pending.reset(); selected = -1;
        for( std::size_t i = 0; i < design.blocks.size(); ++i )
        {
            const auto& b = design.blocks[i];
            const int height = 75 + int( visualModel( b ).pins.size() ) * 23;
            if( wxRect( b.x, b.y, 220, height ).Contains( point ) )
            {
                selected = i; m_drag = i; m_offset = point - wxPoint( b.x, b.y ); CaptureMouse(); break;
            }
        }
        Refresh();
    }
    void paint()
    {
        wxAutoBufferedPaintDC dc( this ); PrepareDC( dc ); dc.SetUserScale( m_zoom, m_zoom ); dc.SetBackground( wxBrush( wxColour( 28, 32, 37 ) ) ); dc.Clear();
        dc.SetTextForeground( wxColour( 225, 230, 235 ) );
        const auto points = terminals(); std::map<std::string, std::vector<wxPoint>> nets;
        for( const auto& t : points ) if( !t.net.empty() ) nets[t.net].push_back( t.position );
        dc.SetPen( wxPen( wxColour( 66, 169, 197 ), 2 ) );
        for( const auto& [net, positions] : nets ) for( std::size_t i = 1; i < positions.size(); ++i )
        {
            const auto a = positions[0], b = positions[i]; const int x = ( a.x + b.x ) / 2;
            dc.DrawLine( a, { x, a.y } ); dc.DrawLine( { x, a.y }, { x, b.y } ); dc.DrawLine( { x, b.y }, b );
        }
        dc.SetPen( wxPen( wxColour( 99, 114, 129 ) ) ); dc.SetBrush( wxBrush( wxColour( 42, 49, 57 ) ) );
        dc.DrawRoundedRectangle( 20, 28, 190, 65 + int( design.pins.size() ) * 25, 6 ); dc.DrawText( _( "Component pins" ), 35, 40 );
        for( std::size_t i = 0; i < design.pins.size(); ++i )
        {
            const auto& p = design.pins[i]; dc.DrawText( W( p.defaultPin + "  " + p.label ).Left( 24 ), 33, 68 + int( i ) * 25 );
        }
        if( !design.baseJson.empty() ) dc.DrawText( _( "Existing model core preserved. Pins and parameters can be edited in the other tabs." ), 270, 8 );
        for( std::size_t index = 0; index < design.blocks.size(); ++index )
        {
            const auto& b = design.blocks[index]; const auto& m = visualModel( b );
            dc.SetPen( wxPen( int( index ) == selected ? wxColour( 246, 181, 70 ) : wxColour( 99, 114, 129 ), 2 ) );
            dc.SetBrush( wxBrush( wxColour( 42, 49, 57 ) ) ); dc.DrawRoundedRectangle( b.x, b.y, 220, 75 + int( m.pins.size() ) * 23, 6 );
            dc.DrawText( W( b.id + " — " + m.name ).Left( 30 ), b.x + 10, b.y + 10 );
            for( std::size_t i = 0; i < m.pins.size(); ++i )
            {
                const auto& p = m.pins[i]; const auto label = W( p.label + " [" + p.id + "]" ).Left( 28 );
                const int x = p.role == "output" || p.role == "bidirectional" ? b.x + 210 - dc.GetTextExtent( label ).x : b.x + 10;
                dc.DrawText( label, x, b.y + 50 + int( i ) * 23 );
            }
        }
        for( const auto& t : points )
        {
            bool pending = m_pending && t.block == m_pending->block && t.port == m_pending->port;
            dc.SetPen( *wxTRANSPARENT_PEN ); dc.SetBrush( wxBrush( pending ? wxColour( 246, 181, 70 ) : t.net.empty() ? wxColour( 139, 147, 156 ) : wxColour( 86, 207, 166 ) ) );
            dc.DrawCircle( t.position, 5 );
        }
    }
    std::optional<TERMINAL> m_pending; int m_drag = -1; wxPoint m_offset; double m_zoom = 1;
};
class MODEL_AUTHOR : public DIALOG_SHIM
{
public:
    MODEL_AUTHOR( wxWindow* parent, KIWAY* kiway ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Simulation Model Editor — CircuitAliveEDA" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ), m_kiway( kiway ),
        m_project( kiway->Prj().GetProjectPath().ToStdString() )
    {
        OptOut( this ); SetInitialFocus( nullptr ); auto* s = new wxBoxSizer( wxVERTICAL );
        auto* tools = new wxBoxSizer( wxHORIZONTAL );
        auto button = [&]( const wxString& title, auto action )
        {
            auto* b = new wxButton( this, wxID_ANY, title ); tools->Add( b, 0, wxRIGHT, 6 );
            b->Bind( wxEVT_BUTTON, [this, action]( wxCommandEvent& ) { try { action(); } catch( const std::exception& e ) { Error( this, e ); } } );
        };
        button( _( "New" ), [this] { if( discard() ) { m_design = {}; m_file.clear(); refreshFields(); m_dirty = false; } } );
        button( _( "Open..." ), [this] { open(); } );
        button( _( "Save" ), [this] { save( false ); } ); button( _( "Save as..." ), [this] { save( true ); } );
        button( _( "Choose symbol..." ), [this] { chooseSymbol(); } );
        button( _( "Start from model..." ), [this] { fromTemplate(); } );
        button( _( "555 example" ), [this] { timerExample(); } );
        s->Add( tools, 0, wxEXPAND | wxALL, 10 );
        auto* fields = new wxBoxSizer( wxHORIZONTAL );
        fields->Add( new wxStaticText( this, wxID_ANY, _( "Name:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        m_name = new wxTextCtrl( this, wxID_ANY ); fields->Add( m_name, 2, wxRIGHT, 12 );
        fields->Add( new wxStaticText( this, wxID_ANY, _( "Model ID:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        m_id = new wxTextCtrl( this, wxID_ANY ); fields->Add( m_id, 1 ); s->Add( fields, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_symbol = new wxStaticText( this, wxID_ANY, "" ); s->Add( m_symbol, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_pages = new wxNotebook( this, wxID_ANY );
        auto* behavior = new wxPanel( m_pages ); auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* blockTools = new wxBoxSizer( wxHORIZONTAL );
        auto blockButton = [&]( const wxString& label, auto action )
        {
            auto* b = new wxButton( behavior, wxID_ANY, label ); blockTools->Add( b, 0, wxRIGHT, 6 );
            b->Bind( wxEVT_BUTTON, [this, action]( wxCommandEvent& ) { try { collect(); action(); } catch( const std::exception& e ) { Error( this, e ); } } );
        };
        blockButton( _( "Add block..." ), [this] { addBlock(); } ); blockButton( _( "Edit block..." ), [this] { editBlock(); } );
        blockButton( _( "Remove block" ), [this] { if( selected() ) { m_design.blocks.erase( m_design.blocks.begin() + m_canvas->selected ); m_canvas->selected = -1; changed(); } } );
        blockButton( _( "Disconnect block" ), [this] { if( selected() ) { m_design.blocks[m_canvas->selected].connections.clear(); changed(); } } );
        blockButton( _( "Cancel connection" ), [this] { m_canvas->CancelConnection(); } );
        blockButton( _( "Zoom +" ), [this] { m_canvas->Zoom( 1.25 ); } );
        blockButton( _( "Zoom -" ), [this] { m_canvas->Zoom( .8 ); } );
        blockButton( _( "Fit" ), [this] { m_canvas->FitContent(); } );
        layout->Add( blockTools, 0, wxEXPAND | wxALL, 6 );
        layout->Add( new wxStaticText( behavior, wxID_ANY, _( "Click two terminals to connect. Double-click a block to edit its values and wires. Drag a block to move it." ) ), 0, wxLEFT | wxRIGHT | wxBOTTOM, 6 );
        m_canvas = new DESIGN_CANVAS( behavior, m_design ); m_canvas->changed = [this] { changed(); }; m_canvas->edit = [this] { try { collect(); editBlock(); } catch( const std::exception& e ) { Error( this, e ); } };
        layout->Add( m_canvas, 1, wxEXPAND ); behavior->SetSizer( layout ); m_pages->AddPage( behavior, _( "Build behavior" ) );
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
        m_pages->Bind( wxEVT_NOTEBOOK_PAGE_CHANGED, [this]( wxBookCtrlEvent& e ) { if( !m_loading ) { try { collect(); m_canvas->Refresh(); } catch( const std::exception& error ) { Error( this, error ); } } e.Skip(); } );
        close->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { if( discard() ) EndModal( wxID_CLOSE ); } );
        Bind( wxEVT_CLOSE_WINDOW, [this]( wxCloseEvent& e ) { if( discard() ) EndModal( wxID_CLOSE ); else if( e.CanVeto() ) e.Veto(); } );
        Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { if( discard() ) EndModal( wxID_CANCEL ); }, wxID_CANCEL );
        refreshFields();
    }
private:
    void changed() { m_dirty = true; m_canvas->Refresh(); }
    bool selected() { return m_canvas->selected >= 0 && std::size_t( m_canvas->selected ) < m_design.blocks.size(); }
    void collect()
    {
        CommitGrid( m_pins ); CommitGrid( m_parameters ); m_design.name = m_name->GetValue().ToStdString(); m_design.id = m_id->GetValue().ToStdString();
        m_design.pins.clear(); m_design.parameters.clear();
        for( int r = 0; r < m_pins->GetNumberRows(); ++r ) m_design.pins.push_back( {
            m_pins->GetCellValue( r, 1 ).ToStdString(), m_pins->GetCellValue( r, 2 ).ToStdString(),
            m_pins->GetCellValue( r, 3 ).ToStdString(), m_pins->GetCellValue( r, 0 ).ToStdString() } );
        for( int r = 0; r < m_parameters->GetNumberRows(); ++r ) m_design.parameters.push_back( {
            m_parameters->GetCellValue( r, 0 ).ToStdString(), m_parameters->GetCellValue( r, 1 ).ToStdString(),
            m_parameters->GetCellValue( r, 3 ).ToStdString(), m_parameters->GetCellValue( r, 2 ).ToStdString(),
            ParseComponentValue( m_parameters->GetCellValue( r, 4 ).ToStdString() ), ParseComponentValue( m_parameters->GetCellValue( r, 5 ).ToStdString() ) } );
    }
    void refreshFields()
    {
        m_loading = true; m_name->SetValue( W( m_design.name ) ); m_id->SetValue( W( m_design.id ) );
        m_symbol->SetLabel( m_design.libraryId.empty() ? _( "Component: choose a symbol to import its pins" ) : _( "Component: " ) + W( m_design.libraryId ) );
        Reset( m_pins ); Reset( m_parameters ); Reset( m_stimuli );
        for( const auto& p : m_design.pins )
        {
            const int r = m_pins->GetNumberRows(); m_pins->AppendRows(); m_pins->SetCellValue( r, 0, W( p.defaultPin ) );
            m_pins->SetCellValue( r, 1, W( p.id ) ); m_pins->SetCellValue( r, 2, W( p.label ) ); m_pins->SetCellValue( r, 3, W( p.role ) );
            m_pins->SetCellEditor( r, 3, new wxGridCellChoiceEditor( wxArrayString{ "input", "output", "bidirectional", "passive", "power", "reference" }, false ) );
            m_stimuli->AppendRows(); m_stimuli->SetCellValue( r, 0, W( p.defaultPin + " — " + p.label ) ); m_stimuli->SetReadOnly( r, 0 );
            m_stimuli->SetCellValue( r, 1, p.role == "power" ? "5V" : p.role == "reference" || p.role == "input" ? "0V" : p.role == "output" ? "load:10k" : "" );
        }
        for( const auto& p : m_design.parameters )
        {
            int r = m_parameters->GetNumberRows(); m_parameters->AppendRows();
            m_parameters->SetCellValue( r, 0, W( p.id ) ); m_parameters->SetCellValue( r, 1, W( p.label ) ); m_parameters->SetCellValue( r, 2, W( p.defaultValue ) );
            m_parameters->SetCellValue( r, 3, W( p.unit ) ); m_parameters->SetCellValue( r, 4, wxString::Format( "%.12g", p.minimum ) ); m_parameters->SetCellValue( r, 5, wxString::Format( "%.12g", p.maximum ) );
        }
        m_canvas->selected = -1; m_canvas->CancelConnection(); m_canvas->Refresh(); m_results->Clear();
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
        wxFileDialog picker( this, _( "Open simulation model" ), W( m_project ), "", "CircuitAlive model (*.casim)|*.casim", wxFD_OPEN | wxFD_FILE_MUST_EXIST );
        if( picker.ShowModal() != wxID_OK ) return;
        auto doc = MODEL_DESIGN::Open( Read( fs::path( picker.GetPath().ToStdString() ) ) );
        m_design = std::move( doc ); m_file = picker.GetPath().ToStdString(); refreshFields(); m_dirty = false;
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
                "CircuitAlive model (*.casim)|*.casim", wxFD_SAVE | wxFD_OVERWRITE_PROMPT );
            if( picker.ShowModal() != wxID_OK ) return false;
            path = picker.GetPath().ToStdString(); if( path.extension() != ".casim" ) path += ".casim";
        }
        const auto temporary = path.string() + ".tmp." + std::to_string( getpid() );
        try
        {
            std::ofstream out( temporary ); out.exceptions( std::ios::failbit | std::ios::badbit ); out << text; out.close(); fs::rename( temporary, path );
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
        if( !symbol ) throw std::runtime_error( "Cannot load chosen symbol" );
        auto flat = symbol->Flatten(); auto physical = flat->GetPins();
        std::sort( physical.begin(), physical.end(), []( const auto* a, const auto* b ) { return a->GetNumber() < b->GetNumber(); } );
        std::vector<MODEL_PIN> ports; std::set<std::string> used, numbers;
        for( const auto* p : physical )
        {
            const auto number = p->GetNumber().ToStdString(); if( !numbers.insert( number ).second ) continue;
            auto existing = std::find_if( m_design.pins.begin(), m_design.pins.end(), [&]( const auto& pin ) { return pin.defaultPin == number; } );
            if( existing != m_design.pins.end() ) { ports.push_back( *existing ); used.insert( existing->id ); continue; }
            std::string label = p->GetName().ToStdString(), logical;
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
        m_design.pins = std::move( ports ); m_design.libraryId = picked.ToStdString(); refreshFields(); changed();
    }
    void fromTemplate()
    {
        if( !discard() ) return;
        auto choices = Templates( m_project ); wxArrayString names;
        for( const auto& c : choices ) names.Add( W( c.name ) );
        wxSingleChoiceDialog picker( this, _( "Choose an existing model to duplicate. Its current electrical core is preserved." ), _( "Start from model" ), names );
        if( picker.ShowModal() != wxID_OK ) return;
        m_design = MODEL_DESIGN::Open( choices.at( picker.GetSelection() ).json ); m_design.id = "custom_" + m_design.id;
        m_design.name = "Custom " + m_design.name; m_file.clear(); refreshFields(); changed();
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
        if( m_design.pins.empty() ) throw std::runtime_error( "Choose the component symbol before adding blocks" );
        auto choices = Templates( m_project ); wxArrayString names; for( const auto& c : choices ) names.Add( W( c.name ) );
        wxSingleChoiceDialog picker( this, _( "Choose a reusable circuit or logic block" ), _( "Add block" ), names );
        if( picker.ShowModal() != wxID_OK ) return;
        auto model = MODEL_REPOSITORY::LoadText( choices.at( picker.GetSelection() ).json );
        DESIGN_BLOCK block; block.modelJson = choices.at( picker.GetSelection() ).json;
        std::set<std::string> used; for( const auto& b : m_design.blocks ) used.insert( b.id );
        int n = 1; do { block.id = "B" + std::to_string( n++ ); } while( used.contains( block.id ) );
        block.x = 280 + int( m_design.blocks.size() % 3 ) * 280; block.y = 80 + int( m_design.blocks.size() / 3 ) * 230;
        for( const auto& p : model.parameters )
        {
            if( p.defaultValue == "$value" ) block.values[p.id] = model.id == "resistor" ? "1k" : model.id == "capacitor" ? "100n" : model.id == "inductor" ? "1m" : "5";
        }
        BLOCK_DIALOG dialog( this, block, m_design ); if( dialog.ShowModal() != wxID_OK ) return;
        m_design.blocks.push_back( std::move( block ) ); m_canvas->selected = m_design.blocks.size() - 1; changed();
    }
    void editBlock()
    {
        if( !selected() ) return;
        auto block = m_design.blocks[m_canvas->selected];
        BLOCK_DIALOG dialog( this, block, m_design );
        if( dialog.ShowModal() == wxID_OK ) { m_design.blocks[m_canvas->selected] = std::move( block ); changed(); }
    }
    void testModel()
    {
        collect(); CommitGrid( m_stimuli ); const auto text = m_design.Export();
        const auto file = fs::temp_directory_path() / ( "circuitalive-author-test-" + std::to_string( getpid() ) + ".casim" );
        struct CLEANUP { fs::path path; ~CLEANUP() { std::error_code error; fs::remove( path, error ); } } cleanup{ file };
        { std::ofstream out( file ); out << text; if( !out ) throw std::runtime_error( "Cannot prepare model test" ); }
        CIRCUIT_COMPONENT root; root.reference = "U1"; root.libraryId = m_design.libraryId; root.modelFile = file.string(); root.value = m_testValue->GetValue().ToStdString(); root.projectDirectory = m_project;
        std::vector<CIRCUIT_COMPONENT> circuit;
        for( std::size_t i = 0; i < m_design.pins.size(); ++i )
        {
            const auto& p = m_design.pins[i]; const auto stimulus = i < std::size_t( m_stimuli->GetNumberRows() ) ? m_stimuli->GetCellValue( i, 1 ).ToStdString() : "";
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
        if( snapshot.config.signals.empty() ) throw std::runtime_error( "The test needs a non-ground terminal" );
        const double duration = ParseComponentValue( m_testTime->GetValue().ToStdString() );
        if( duration <= 0 || duration > 1 ) throw std::runtime_error( "Test duration must be between 0 and 1 second" );
        wxBusyCursor busy;
        INTERACTIVE_SIM_SESSION session( FindSessionWorker( fs::read_symlink( "/proc/self/exe" ).string() ) );
        session.Configure( snapshot.netlist, snapshot.config ); session.AdvanceTo( duration ); const auto sample = session.Snapshot().latest;
        std::string report = "Test completed at t = " + std::to_string( sample.time ) + " s\n\n";
        for( std::size_t i = 0; i < m_design.pins.size(); ++i ) report += m_design.pins[i].defaultPin + " — " + m_design.pins[i].label + ": "
            + FormatMeasurement( channels[i] == NO_SIGNAL ? 0 : sample.values.at( channels[i] ), MEASURE_QUANTITY::VOLTAGE ) + "\n";
        m_results->SetValue( W( report ) );
    }
    KIWAY* m_kiway; std::string m_project; MODEL_DESIGN m_design; fs::path m_file;
    wxTextCtrl *m_name, *m_id, *m_testValue, *m_testTime, *m_results;
    wxStaticText *m_symbol, *m_status; wxNotebook* m_pages; wxGrid *m_pins, *m_parameters, *m_stimuli;
    DESIGN_CANVAS* m_canvas; wxCheckBox* m_associate; bool m_loading = false, m_dirty = false;
};
}
void ShowModelAuthor( wxWindow* parent, KIWAY* kiway )
{
    MODEL_AUTHOR dialog( parent, kiway ); dialog.ShowModal();
}
}
