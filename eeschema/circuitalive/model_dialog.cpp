/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_dialog.h"
#include "simulation_model.h"
#include <dialog_shim.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <optional>

namespace CIRCUITALIVE
{
class MODEL_DIALOG : public DIALOG_SHIM
{
public:
    MODEL_DIALOG( wxWindow* parent, const CIRCUIT_COMPONENT& component ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Simulation model" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ), m_component( component ),
        m_repository( MODEL_REPOSITORY::DefaultDirectories( component.projectDirectory ) )
    {
        auto* layout = new wxBoxSizer( wxVERTICAL );
        layout->Add( new wxStaticText( this, wxID_ANY, wxString::Format( "%s — %s   Value: %s",
            wxString::FromUTF8( component.reference ), wxString::FromUTF8( component.libraryId ),
            wxString::FromUTF8( component.value ) ) ), 0, wxALL, 10 );
        auto* row = new wxBoxSizer( wxHORIZONTAL );
        m_choice = new wxChoice( this, wxID_ANY );
        m_choice->Append( _( "Automatic association" ) );
        m_models = m_repository.List();
        for( const auto& model : m_models ) m_choice->Append( wxString::FromUTF8( model.name ) );
        row->Add( m_choice, 1, wxRIGHT, 8 );
        auto* browse = new wxButton( this, wxID_ANY, _( "Load file..." ) );
        auto* reload = new wxButton( this, wxID_ANY, _( "Reload file" ) );
        row->Add( browse, 0, wxRIGHT, 8 ); row->Add( reload );
        layout->Add( row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_details = new wxStaticText( this, wxID_ANY, "" );
        layout->Add( m_details, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        layout->Add( new wxStaticText( this, wxID_ANY, _( "Model ports → physical symbol pins" ) ),
                     0, wxLEFT | wxRIGHT | wxTOP, 10 );
        m_pins = new wxGrid( this, wxID_ANY );
        m_pins->CreateGrid( 0, 3 );
        m_pins->SetColLabelValue( 0, _( "Model port" ) );
        m_pins->SetColLabelValue( 1, _( "Role" ) );
        m_pins->SetColLabelValue( 2, _( "Physical pin" ) );
        layout->Add( m_pins, 1, wxEXPAND | wxALL, 10 );
        layout->Add( new wxStaticText( this, wxID_ANY, _( "Parameters — blank uses the model default; $value follows the symbol Value" ) ),
                     0, wxLEFT | wxRIGHT, 10 );
        m_parameters = new wxGrid( this, wxID_ANY );
        m_parameters->CreateGrid( 0, 4 );
        m_parameters->SetColLabelValue( 0, _( "Parameter" ) );
        m_parameters->SetColLabelValue( 1, _( "Value" ) );
        m_parameters->SetColLabelValue( 2, _( "Unit" ) );
        m_parameters->SetColLabelValue( 3, _( "Model default" ) );
        layout->Add( m_parameters, 2, wxEXPAND | wxALL, 10 );
        layout->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 );
        SetSizer( layout );
        SetMinSize( wxSize( 760, 600 ) ); SetSize( 900, 760 ); Centre();
        int selected = 0;
        for( std::size_t i = 0; i < m_models.size(); ++i )
            if( component.modelFile == "builtin:" + m_models[i].id ) selected = static_cast<int>( i + 1 );
        if( !component.modelFile.empty() && !component.modelFile.starts_with( "builtin:" ) )
        {
            try
            {
                auto path = std::filesystem::path( component.modelFile );
                if( path.is_relative() ) path = std::filesystem::path( component.projectDirectory ) / path;
                selected = addModel( MODEL_REPOSITORY::Load( path ) );
            }
            catch( const std::exception& e ) { wxMessageBox( wxString::FromUTF8( e.what() ), _( "Simulation model" ), wxOK, this ); }
        }
        m_choice->SetSelection( selected );
        load( true );
        m_choice->Bind( wxEVT_CHOICE, [this]( wxCommandEvent& ) { load( false ); } );
        browse->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            wxFileDialog picker( this, _( "Load simulation model" ),
                wxString::FromUTF8( m_component.projectDirectory ), "",
                _( "CircuitAlive model (*.casim)|*.casim|All files|*" ), wxFD_OPEN | wxFD_FILE_MUST_EXIST );
            if( picker.ShowModal() != wxID_OK ) return;
            try
            {
                m_choice->SetSelection( addModel( MODEL_REPOSITORY::Load( picker.GetPath().ToStdString() ) ) );
                load( false );
            }
            catch( const std::exception& e ) { showError( e.what() ); }
        } );
        reload->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            try
            {
                if( m_model )
                {
                    collect();
                    auto refreshed = MODEL_REPOSITORY::Load( m_model->file );
                    if( m_choice->GetSelection() > 0 ) m_models.at( m_choice->GetSelection() - 1 ) = refreshed;
                    m_repository = MODEL_REPOSITORY( MODEL_REPOSITORY::DefaultDirectories( m_component.projectDirectory ) );
                    load( true );
                }
            }
            catch( const std::exception& e ) { showError( e.what() ); }
        } );
        Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            try
            {
                collect();
                const auto resolved = m_repository.Resolve( m_component );
                std::map<std::string, std::string> nodes;
                for( const auto& [pin, net] : m_component.pins ) nodes[pin] = "n" + std::to_string( nodes.size() + 1 );
                for( const auto& line : resolved.model.instance ) ExpandModelTemplate( line, resolved, nodes, 0 );
                for( const auto& line : resolved.model.definitions ) ExpandModelTemplate( line, resolved, nodes, 0 );
                EndQuasiModal( wxID_OK );
            }
            catch( const std::exception& e ) { showError( e.what() ); }
        }, wxID_OK );
    }
    const CIRCUIT_COMPONENT& Result() const { return m_component; }

private:
    int addModel( SIMULATION_MODEL model )
    {
        for( std::size_t i = 0; i < m_models.size(); ++i )
        {
            std::error_code error;
            if( std::filesystem::equivalent( model.file, m_models[i].file, error ) )
            {
                m_models[i] = std::move( model );
                m_choice->SetString( i + 1, wxString::FromUTF8( m_models[i].name ) );
                return static_cast<int>( i + 1 );
            }
        }
        m_models.push_back( std::move( model ) );
        m_choice->Append( wxString::FromUTF8( m_models.back().name ) );
        return static_cast<int>( m_models.size() );
    }
    void showError( const std::string& text ) { wxMessageBox( wxString::FromUTF8( text ), _( "Simulation model" ), wxOK | wxICON_INFORMATION, this ); }
    void load( bool preserve )
    {
        if( !preserve ) { m_component.modelPins.clear(); m_component.modelParameters.clear(); }
        m_model.reset();
        try
        {
            const auto selected = m_choice->GetSelection();
            if( selected == 0 )
            {
                m_component.modelFile.clear();
                m_model = m_repository.SelectModel( m_component );
            }
            else
            {
                m_model = m_models.at( selected - 1 );
                // Bundled choices retain an ID; user files retain a path, relative inside the project.
                const auto bundled = m_model->file.parent_path();
                bool builtin = false;
                for( const auto& directory : MODEL_REPOSITORY::DefaultDirectories() )
                {
                    std::error_code error;
                    if( std::filesystem::equivalent( bundled, directory, error ) ) builtin = true;
                }
                m_component.modelFile = "builtin:" + m_model->id;
                if( !builtin )
                {
                    std::error_code error;
                    auto relative = std::filesystem::relative( m_model->file, m_component.projectDirectory, error );
                    m_component.modelFile = !error && !relative.empty() && *relative.begin() != ".."
                            ? relative.string() : m_model->file.string();
                }
            }
            m_details->SetLabel( wxString::FromUTF8( m_model->file.string() + "\n" + m_model->description ) );
        }
        catch( const std::exception& e ) { m_details->SetLabel( wxString::FromUTF8( e.what() ) ); }
        if( m_pins->GetNumberRows() ) m_pins->DeleteRows( 0, m_pins->GetNumberRows() );
        if( m_parameters->GetNumberRows() ) m_parameters->DeleteRows( 0, m_parameters->GetNumberRows() );
        if( m_model )
        {
            wxArrayString physical;
            for( const auto& [pin, net] : m_component.pins ) physical.Add( wxString::FromUTF8( pin ) );
            for( const auto& pin : m_model->pins )
            {
                const int row = m_pins->GetNumberRows(); m_pins->AppendRows();
                m_pins->SetCellValue( row, 0, wxString::FromUTF8( pin.label + " [" + pin.id + "]" ) );
                m_pins->SetCellValue( row, 1, wxString::FromUTF8( pin.role ) );
                auto it = m_component.modelPins.find( pin.id );
                m_pins->SetCellValue( row, 2, wxString::FromUTF8( it == m_component.modelPins.end() ? pin.defaultPin : it->second ) );
                m_pins->SetReadOnly( row, 0 ); m_pins->SetReadOnly( row, 1 );
                m_pins->SetCellEditor( row, 2, new wxGridCellChoiceEditor( physical, false ) );
            }
            for( const auto& parameter : m_model->parameters )
            {
                const int row = m_parameters->GetNumberRows(); m_parameters->AppendRows();
                m_parameters->SetCellValue( row, 0, wxString::FromUTF8( parameter.label ) );
                auto it = m_component.modelParameters.find( parameter.id );
                m_parameters->SetCellValue( row, 1, wxString::FromUTF8( it == m_component.modelParameters.end() ? "" : it->second ) );
                m_parameters->SetCellValue( row, 2, wxString::FromUTF8( parameter.unit ) );
                m_parameters->SetCellValue( row, 3, wxString::FromUTF8( parameter.defaultValue ) );
                for( const int col : { 0, 2, 3 } ) m_parameters->SetReadOnly( row, col );
            }
        }
        for( auto* grid : { m_pins, m_parameters } )
        {
            grid->SetRowLabelSize( 30 ); grid->AutoSizeColumns(); grid->SetColSize( 0, 310 );
        }
        m_parameters->SetColSize( 0, 350 );
        m_parameters->SetColSize( 1, 130 );
        m_parameters->SetColSize( 3, 130 );
        m_details->Wrap( 830 ); Layout();
    }
    void collect()
    {
        if( !m_model ) throw std::runtime_error( "Choose a simulation model file first" );
        for( auto* grid : { m_pins, m_parameters } )
        {
            grid->SaveEditControlValue(); grid->HideCellEditControl();
        }
        m_component.modelPins.clear(); m_component.modelParameters.clear();
        for( std::size_t i = 0; i < m_model->pins.size(); ++i )
            m_component.modelPins[m_model->pins[i].id] = m_pins->GetCellValue( i, 2 ).ToStdString();
        for( std::size_t i = 0; i < m_model->parameters.size(); ++i )
            if( auto value = m_parameters->GetCellValue( i, 1 ).ToStdString(); !value.empty() )
                m_component.modelParameters[m_model->parameters[i].id] = value;
    }
    CIRCUIT_COMPONENT m_component;
    MODEL_REPOSITORY m_repository;
    std::vector<SIMULATION_MODEL> m_models;
    std::optional<SIMULATION_MODEL> m_model;
    wxChoice* m_choice;
    wxStaticText* m_details;
    wxGrid* m_pins;
    wxGrid* m_parameters;
};

bool EditSimulationModel( wxWindow* parent, CIRCUIT_COMPONENT& component )
{
    MODEL_DIALOG dialog( parent, component );
    if( dialog.ShowQuasiModal() != wxID_OK ) return false;
    component = dialog.Result();
    return true;
}
}
