/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "model_dialog.h"
#include "resource_dialog.h"
#include "simulation_model.h"
#include "model_profiles.h"
#include <sim/platform_runtime.h>
#include "panel_led_appearance.h"
#include <dialog_shim.h>
#include <wx/button.h>
#include "choice_controls.h"
#include "model_browser.h"
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/notebook.h>
#include <optional>

namespace CIRCUITALIVE
{
class MODEL_DIALOG : public DIALOG_SHIM
{
public:
    MODEL_DIALOG( wxWindow* parent, const CIRCUIT_COMPONENT& component, bool excluded ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Simulation model" ), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ), m_component( component ),
        m_repository( MODEL_REPOSITORY::DefaultDirectories( component.projectDirectory ) )
    {
        // These controls describe a particular symbol, not a dialog preference.
        // DIALOG_SHIM's generic persistence must never overwrite its saved model/pins/measurements.
        OptOut( this );
        SetInitialFocus( nullptr );
        auto* layout = new wxBoxSizer( wxVERTICAL );
        layout->Add( new wxStaticText( this, wxID_ANY, wxString::Format( _("%s — %s   Value: %s"),
            wxString::FromUTF8( component.reference ), wxString::FromUTF8( component.libraryId ),
            wxString::FromUTF8( component.value ) ) ), 0, wxALL, 10 );
        m_exclude = new wxCheckBox( this, wxID_ANY, _( "Exclude this component from simulation" ) );
        m_exclude->SetValue( excluded );
        m_exclude->SetToolTip( _( "This instance is omitted before loading or validating its model. Existing model settings are kept. This does not change defaults for new symbols." ) );
        layout->Add( m_exclude, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        auto* row = new wxBoxSizer( wxHORIZONTAL );
        m_choice = new wxTextCtrl( this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_READONLY );
        auto* chooseModel = new wxButton( this, wxID_ANY, _( "Choose model…" ) );
        auto* automatic = new wxButton( this, wxID_ANY, _( "Automatic" ) );
        m_models = m_repository.List();
        row->Add( m_choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
        row->Add( chooseModel, 0, wxRIGHT, 8 ); row->Add( automatic, 0, wxRIGHT, 8 );
        auto* browse = new wxButton( this, wxID_ANY, _( "Load file..." ) );
        auto* reload = new wxButton( this, wxID_ANY, _( "Reload file" ) );
        row->Add( browse, 0, wxRIGHT, 8 ); row->Add( reload );
        layout->Add( row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_files=new wxButton(this,wxID_ANY,_("Data files…"));
        m_files->Hide();layout->Add(m_files,0,wxLEFT|wxRIGHT|wxBOTTOM,10);
        m_files->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){if(m_model)EditModelResources(this,m_component,*m_model);});
        m_details = new wxStaticText( this, wxID_ANY, "" );
        layout->Add( m_details, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        layout->Add( new wxStaticText( this, wxID_ANY, _( "Model ports → physical symbol pins" ) ),
                     0, wxLEFT | wxRIGHT | wxTOP, 10 );
        m_pins = new wxGrid( this, wxID_ANY );
        m_pins->SetMinSize( wxSize( 200, 150 ) );
        m_pins->CreateGrid( 0, 3 );
        m_pins->SetColLabelValue( 0, _( "Model port" ) );
        m_pins->SetColLabelValue( 1, _( "Role" ) );
        m_pins->SetColLabelValue( 2, _( "Physical pin" ) );
        layout->Add( m_pins, 1, wxEXPAND | wxALL, 10 );
        layout->Add( new wxStaticText( this, wxID_ANY, _( "Parameters — blank uses the model default; $value follows the symbol Value" ) ),
                     0, wxLEFT | wxRIGHT, 10 );
        m_parameterPages=new wxNotebook(this,wxID_ANY);
        auto* electrical=new wxPanel(m_parameterPages);auto* parameterLayout=new wxBoxSizer(wxVERTICAL);
        m_parameters = new wxGrid( electrical, wxID_ANY );
        m_parameters->SetMinSize( wxSize( 200, 180 ) );
        m_parameters->CreateGrid( 0, 4 );
        m_parameters->SetColLabelValue( 0, _( "Parameter" ) );
        m_parameters->SetColLabelValue( 1, _( "Value" ) );
        m_parameters->SetColLabelValue( 2, _( "Unit" ) );
        m_parameters->SetColLabelValue( 3, _( "Model default" ) );
        parameterLayout->Add(m_parameters,1,wxEXPAND);electrical->SetSizer(parameterLayout);
        m_parameterPages->AddPage(electrical,_("Electrical parameters"));
        m_appearance=new PANEL_LED_APPEARANCE(m_parameterPages);m_appearance->Hide();
        layout->Add( m_parameterPages, 2, wxEXPAND | wxALL, 10 );
        auto* measurements = new wxBoxSizer( wxVERTICAL );
        auto* checks = new wxBoxSizer( wxHORIZONTAL );
        m_voltage = new wxCheckBox( this, wxID_ANY, _( "Measure V" ) );
        m_current = new wxCheckBox( this, wxID_ANY, _( "Measure I" ) );
        m_power = new wxCheckBox( this, wxID_ANY, _( "Measure P" ) );
        m_voltage->SetValue( component.measurements.voltage );
        m_current->SetValue( component.measurements.current );
        m_power->SetValue( component.measurements.power );
        for( auto* check : { m_voltage, m_current, m_power } ) checks->Add( check, 0, wxRIGHT, 20 );
        measurements->Add( checks, 0, wxBOTTOM, 6 );
        auto* ports = new wxBoxSizer( wxHORIZONTAL );
        m_positive = new CLICK_CHOICE( this, wxID_ANY ); m_negative = new CLICK_CHOICE( this, wxID_ANY );
        ports->Add( new wxStaticText( this, wxID_ANY, _( "From (+):" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        ports->Add( m_positive, 1, wxRIGHT, 12 );
        ports->Add( new wxStaticText( this, wxID_ANY, _( "To (−):" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        ports->Add( m_negative, 1 ); measurements->Add( ports, 0, wxEXPAND );
        measurements->Add( new wxStaticText( this, wxID_ANY,
            _( "I enters the (+) terminal. P is signed power for this port pair." ) ), 0, wxTOP, 4 );
        layout->Add( measurements, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        auto* defaults = new wxBoxSizer( wxHORIZONTAL );
        m_remember = new wxCheckBox( this, wxID_ANY,
            wxString::Format( _( "Use these settings for new %s symbols" ), wxString::FromUTF8( component.libraryId ) ) );
        m_remember->SetValue( !excluded );
        m_remember->SetToolTip( _( "Copies the model, pin mapping, parameters and measurement choices into new symbols. Existing symbols keep their settings. $value follows each symbol's Value." ) );
        defaults->Add( m_remember, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
        auto* forget = new wxButton( this, wxID_ANY, _( "Forget saved default" ) );
        defaults->Add( forget );
        layout->Add( defaults, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        forget->Bind( wxEVT_BUTTON, [this, forget]( wxCommandEvent& )
        {
            m_forget = true; m_remember->SetValue( false );
            forget->SetLabel( _( "Removed on OK" ) );
        } );
        layout->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 );
        SetSizer( layout );
        SetMinSize( wxSize( 760, 720 ) ); SetSize( 900, 880 ); Centre();
        int selected = 0;
        for( std::size_t i = 0; i < m_models.size(); ++i )
            if( component.modelFile == "builtin:" + m_models[i].id ) selected = static_cast<int>( i + 1 );
        m_selected = selected;
        if( !excluded ) { selectExternalFile(); load( true ); }
        else m_details->SetLabel( _( "Excluded — no simulation model is required." ) );
        m_editingControls = { m_choice, chooseModel, automatic, browse, reload, m_files, m_pins, m_parameterPages,
            m_voltage, m_current, m_power, m_positive, m_negative, m_remember, forget };
        enableModelControls();
        m_exclude->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
        {
            enableModelControls();
            if( !m_exclude->GetValue() && !m_model ) { selectExternalFile(); load( true ); }
        } );
        chooseModel->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            int current = m_selected - 1;
            if( current < 0 && m_model )
                for( std::size_t i = 0; i < m_models.size(); ++i )
                    if( m_models[i].id == m_model->id ) current = i;
            if( const auto picked = ChooseSimulationModel( this, m_models, current ) )
            { m_selected = *picked + 1; load( false ); }
        } );
        automatic->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_selected = 0; load( false ); } );
        browse->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            wxFileDialog picker( this, _( "Load simulation model" ),
                wxString::FromUTF8( m_component.projectDirectory ), "",
                _( "CircuitAlive model (*.casim)|*.casim|All files|*" ), wxFD_OPEN | wxFD_FILE_MUST_EXIST );
            if( picker.ShowModal() != wxID_OK ) return;
            try
            {
                m_selected = addModel( MODEL_REPOSITORY::Load( std::filesystem::u8path( picker.GetPath().ToStdString(wxConvUTF8) ) ) );
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
                    if( m_selected > 0 ) m_models.at( m_selected - 1 ) = refreshed;
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
                if( m_exclude->GetValue() ) { EndQuasiModal( wxID_OK ); return; }
                collect();
                const auto resolved = m_repository.Resolve( m_component );
                InitialModelMemories(m_component,resolved.model);
                if( resolved.model.runtime ) InitialModelRuntime( m_component, resolved );
                std::map<std::string, std::string> nodes;
                for( const auto& [pin, net] : m_component.pins ) nodes[pin] = "n" + std::to_string( nodes.size() + 1 );
                for( const auto& line : resolved.model.instance ) ExpandModelTemplate( line, resolved, nodes, 0 );
                for( const auto& line : resolved.model.definitions ) ExpandModelTemplate( line, resolved, nodes, 0 );
                if( m_remember->GetValue() )
                {
                    auto profile = m_component;
                    if( profile.modelFile.empty() ) profile.modelFile = "builtin:" + resolved.model.id;
                    MODEL_PROFILES().Save( profile );
                }
                else if( m_forget ) MODEL_PROFILES().Remove( m_component.libraryId );
                EndQuasiModal( wxID_OK );
            }
            catch( const std::exception& e ) { showError( e.what() ); }
        }, wxID_OK );
        SetInitialFocus( m_pins );
    }
    const CIRCUIT_COMPONENT& Result() const { return m_component; }
    bool Excluded() const { return m_exclude->GetValue(); }

private:
    void enableModelControls()
    {
        for( auto* control : m_editingControls ) control->Enable( !m_exclude->GetValue() );
    }
    void selectExternalFile()
    {
        if( m_component.modelFile.empty() || m_component.modelFile.starts_with( "builtin:" ) ) return;
        try
        {
            auto path = std::filesystem::u8path( m_component.modelFile );
            if( path.is_relative() ) path = std::filesystem::u8path( m_component.projectDirectory ) / path;
            m_selected = addModel( MODEL_REPOSITORY::Load( path ) );
        }
        catch( const std::exception& e ) { showError( e.what() ); }
    }
    int addModel( SIMULATION_MODEL model )
    {
        for( std::size_t i = 0; i < m_models.size(); ++i )
        {
            std::error_code error;
            if( std::filesystem::equivalent( model.file, m_models[i].file, error ) )
            {
                m_models[i] = std::move( model );
                return static_cast<int>( i + 1 );
            }
        }
        m_models.push_back( std::move( model ) );
        return static_cast<int>( m_models.size() );
    }
    void showError( const std::string& text ) { wxMessageBox( DiagnosticText( text ), _( "Simulation model" ), wxOK | wxICON_INFORMATION, this ); }
    void load( bool preserve )
    {
        if( !preserve ) { m_component.modelPins.clear(); m_component.modelParameters.clear(); m_component.resources.clear(); }
        m_model.reset();
        try
        {
            const auto selected = m_selected;
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
                    auto relative = std::filesystem::relative( m_model->file, std::filesystem::u8path( m_component.projectDirectory ), error );
                    m_component.modelFile = !error && !relative.empty() && *relative.begin() != ".."
                            ? PathUtf8( relative ) : PathUtf8( m_model->file );
                }
            }
            m_choice->ChangeValue( ( m_selected == 0 ? _( "Automatic: " ) : wxString() ) + ModelText( m_model->name ) );
            m_details->SetLabel( wxString::FromUTF8( PathUtf8( m_model->file ) ) + "\n" + ModelText( m_model->description ) );
            if( m_model->runtime )
                m_details->SetLabel( m_details->GetLabel() + "\n"
                    + _( "Set CPU clock to the frequency used when compiling the firmware. Changing it changes firmware timing, including delays and timers." ) );
        }
        catch( const std::exception& e ) { m_choice->ChangeValue( _( "No model selected" ) ); m_details->SetLabel( DiagnosticText( e.what() ) ); }
        if( m_pins->GetNumberRows() ) m_pins->DeleteRows( 0, m_pins->GetNumberRows() );
        if( m_parameters->GetNumberRows() ) m_parameters->DeleteRows( 0, m_parameters->GetNumberRows() );
        m_positive->Clear(); m_negative->Clear();
        if( m_model )
        {
            std::map<std::string, std::string> inherited;
            if( m_component.modelFile.empty() && !m_model->legacyParameterMap.empty() )
            {
                // Show migrated waveform/passive values before OK; a broken
                // parameter must still leave the dialog usable for correction/exclusion.
                try { inherited = m_repository.Resolve( m_component ).parameterText; }
                catch( const std::exception& ) {}
            }
            wxArrayString physical;
            for( const auto& [pin, net] : m_component.pins ) physical.Add( wxString::FromUTF8( pin ) );
            for( const auto& pin : m_model->pins )
            {
                const int row = m_pins->GetNumberRows(); m_pins->AppendRows();
                m_pins->SetCellValue( row, 0, ModelText( pin.label ) + " [" + wxString::FromUTF8(pin.id) + "]" );
                m_pins->SetCellValue( row, 1, ModelText( pin.role ) );
                auto it = m_component.modelPins.find( pin.id );
                m_pins->SetCellValue( row, 2, wxString::FromUTF8( it == m_component.modelPins.end() ? pin.defaultPin : it->second ) );
                m_pins->SetReadOnly( row, 0 ); m_pins->SetReadOnly( row, 1 );
                m_pins->SetCellEditor( row, 2, new CLICK_CHOICE_EDITOR( physical, false ) );
                m_positive->Append( ModelText( pin.label ) + " [" + wxString::FromUTF8(pin.id) + "]" );
                m_negative->Append( ModelText( pin.label ) + " [" + wxString::FromUTF8(pin.id) + "]" );
            }
            auto choose = [&]( CLICK_CHOICE* choice, const std::string& selected, const std::string& fallback, int defaultIndex )
            {
                int index = defaultIndex;
                for( std::size_t i = 0; i < m_model->pins.size(); ++i )
                    if( m_model->pins[i].id == fallback ) index = i;
                for( std::size_t i = 0; i < m_model->pins.size(); ++i )
                    if( m_model->pins[i].id == selected ) index = i;
                if( index < static_cast<int>( m_model->pins.size() ) ) choice->SetSelection( index );
            };
            choose( m_positive, m_component.measurements.positive, m_model->measurePositive, 0 );
            choose( m_negative, m_component.measurements.negative, m_model->measureNegative, 1 );
            for( const auto& parameter : m_model->parameters )
            {
                const int row = m_parameters->GetNumberRows(); m_parameters->AppendRows();
                m_parameters->SetCellValue( row, 0, ModelText( parameter.label ) );
                auto it = m_component.modelParameters.find( parameter.id );
                auto value = it == m_component.modelParameters.end() ? std::string() : it->second;
                if( value.empty() && inherited.contains( parameter.id ) && inherited.at( parameter.id ) != parameter.defaultValue )
                    value = inherited.at( parameter.id );
                m_parameters->SetCellValue( row, 1, wxString::FromUTF8( value ) );
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
        if(m_model&&m_model->ledAppearance.Configurable())
        {
            m_appearance->Configure(*m_model,[this](const std::string& id)
            {
                for(std::size_t r=0;r<m_model->parameters.size();++r)if(m_model->parameters[r].id==id)
                {
                    const auto& p=m_model->parameters[r];const auto raw=m_parameters->GetCellValue(r,1).ToStdString(wxConvUTF8);
                    try{return ParseComponentValue(raw.empty()?p.defaultValue:raw);}catch(const std::exception&){return ParseComponentValue(p.defaultValue);}
                }
                throw std::runtime_error("Unknown LED appearance parameter");
            },[this](const std::string& id,const std::string& value)
            {for(std::size_t r=0;r<m_model->parameters.size();++r)if(m_model->parameters[r].id==id)m_parameters->SetCellValue(r,1,wxString::FromUTF8(value));});
            for(std::size_t r=0;r<m_model->parameters.size();++r)
                if(std::find(m_model->ledAppearance.ids.begin(),m_model->ledAppearance.ids.end(),m_model->parameters[r].id)!=m_model->ledAppearance.ids.end())m_parameters->HideRow(r);
            if(m_parameterPages->GetPageCount()==1)
            {m_appearance->Show();m_parameterPages->AddPage(m_appearance,_("LED appearance"));}
        }
        else if(m_parameterPages->GetPageCount()>1){m_parameterPages->RemovePage(1);m_appearance->Hide();}
        m_files->Show(m_model && !m_model->resources.empty());
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
        auto& selected = m_component.measurements;
        selected.voltage = m_voltage->GetValue(); selected.current = m_current->GetValue(); selected.power = m_power->GetValue();
        if( selected.Any() )
        {
            if( m_positive->GetSelection() < 0 || m_negative->GetSelection() < 0
                || m_positive->GetSelection() == m_negative->GetSelection() )
                throw std::runtime_error( "Select two distinct measurement ports" );
            selected.positive = m_model->pins.at( m_positive->GetSelection() ).id;
            selected.negative = m_model->pins.at( m_negative->GetSelection() ).id;
        }
        for( std::size_t i = 0; i < m_model->pins.size(); ++i )
            m_component.modelPins[m_model->pins[i].id] = m_pins->GetCellValue( i, 2 ).ToStdString(wxConvUTF8);
        for( std::size_t i = 0; i < m_model->parameters.size(); ++i )
            if( auto value = m_parameters->GetCellValue( i, 1 ).ToStdString(wxConvUTF8); !value.empty() )
                m_component.modelParameters[m_model->parameters[i].id] = value;
    }
    CIRCUIT_COMPONENT m_component;
    MODEL_REPOSITORY m_repository;
    std::vector<SIMULATION_MODEL> m_models;
    std::optional<SIMULATION_MODEL> m_model;
    wxButton* m_files;
    wxTextCtrl* m_choice;
    int m_selected = 0;
    wxStaticText* m_details;
    wxGrid* m_pins;
    wxGrid* m_parameters;
    wxNotebook* m_parameterPages;
    PANEL_LED_APPEARANCE* m_appearance;
    wxCheckBox *m_voltage, *m_current, *m_power;
    CLICK_CHOICE *m_positive, *m_negative;
    wxCheckBox* m_remember;
    wxCheckBox* m_exclude;
    std::vector<wxWindow*> m_editingControls;
    bool m_forget = false;
};

bool EditSimulationModel( wxWindow* parent, CIRCUIT_COMPONENT& component, bool& excluded )
{
    MODEL_DIALOG dialog( parent, component, excluded );
    if( dialog.ShowQuasiModal() != wxID_OK ) return false;
    component = dialog.Result();
    excluded = dialog.Excluded();
    return true;
}
}
