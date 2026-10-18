/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "resource_dialog.h"
#include "circuit_snapshot.h"
#include "simulation_model.h"
#include "choice_controls.h"
#include <dialog_shim.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
namespace CIRCUITALIVE
{
bool EditModelResources( wxWindow* parent, CIRCUIT_COMPONENT& component, const SIMULATION_MODEL& model )
{
    DIALOG_SHIM dialog( parent, wxID_ANY, _( "Component data files" ), wxDefaultPosition, wxDefaultSize,
                        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    dialog.OptOut( &dialog );
    auto* layout = new wxBoxSizer( wxVERTICAL );
    layout->Add( new wxStaticText( &dialog, wxID_ANY, wxString::FromUTF8(component.reference) + " — " + ModelText(model.name) ),
                 0, wxALL, 10 );
    layout->Add(
            new wxStaticText(
                    &dialog, wxID_ANY,
                    _( "Files belong to this instance. Paths inside the project are saved relative to it.\nLoading a "
                       "file initializes the next simulation; it does not overwrite a paused session." ) ),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    struct ROW
    {
        MODEL_RESOURCE resource;
        wxTextCtrl *path, *offset;
        CLICK_CHOICE* format;
    };
    std::vector<ROW> rows;
    for( const auto& resource : model.resources )
    {
        auto selected = component.resources.contains( resource.id ) ? component.resources.at( resource.id )
                                                                    : RESOURCE_SELECTION{};
        auto* group = new wxBoxSizer( wxVERTICAL );
        group->Add( new wxStaticText( &dialog, wxID_ANY,
                                      ModelText( resource.label ) + ( resource.required ? _(" (required)") : _(" (optional)") ) ),
                    0, wxBOTTOM, 4 );
        auto* path = new wxTextCtrl( &dialog, wxID_ANY, wxString::FromUTF8( selected.path ) );
        auto* browse = new wxButton( &dialog, wxID_ANY, _( "Browse…" ) );
        auto* fileRow = new wxBoxSizer( wxHORIZONTAL );
        fileRow->Add( path, 1, wxRIGHT, 6 );
        fileRow->Add( browse );
        group->Add( fileRow, 0, wxEXPAND );
        auto* format = new CLICK_CHOICE( &dialog, wxID_ANY );
        format->Append( _( "Automatic (extension)" ) );
        format->SetSelection( 0 );
        for( std::size_t i = 0; i < resource.formats.size(); ++i )
        {
            format->Append( wxString::FromUTF8( resource.formats[i] ) );
            if( selected.format == resource.formats[i] )
                format->SetSelection( i + 1 );
        }
        auto* offset =
                new wxTextCtrl( &dialog, wxID_ANY, wxString::Format( "%llX", (unsigned long long)selected.address ) );
        auto* options = new wxBoxSizer( wxHORIZONTAL );
        options->Add( new wxStaticText( &dialog, wxID_ANY, _( "Format:" ) ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        options->Add( format, 1, wxRIGHT, 15 );
        options->Add( new wxStaticText( &dialog, wxID_ANY, _( "Load address (hex): 0x" ) ), 0,
                      wxALIGN_CENTER_VERTICAL | wxRIGHT, 6 );
        options->Add( offset, 0 );
        group->Add( options, 0, wxEXPAND | wxTOP, 6 );
        layout->Add( group, 0, wxEXPAND | wxALL, 10 );
        rows.push_back( { resource, path, offset, format } );
        browse->Bind(
                wxEVT_BUTTON,
                [&, path]( wxCommandEvent& )
                {
                    wxFileDialog picker(
                            &dialog, _( "Select component data" ), wxString::FromUTF8( component.projectDirectory ), "",
                            _( "Memory data (*.bin;*.hex;*.ihex;*.camem)|*.bin;*.hex;*.ihex;*.camem|All files|*" ),
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST );
                    if( picker.ShowModal() == wxID_OK )
                        path->ChangeValue( picker.GetPath() );
                } );
    }
    for( const auto& region : model.memories )
        layout->Add( new wxStaticText(
                             &dialog, wxID_ANY,
                             ModelText( region.label ) + " — " + wxString::Format(_("%zu bytes, %s"),
                                 (std::size_t(1) << region.addressBits) * ((region.wordBits + 7) / 8),
                                 region.writable ? _("RAM; unspecified bits start X")
                                     : _("read only; erased byte 0x") + wxString::FromUTF8(MemoryHex({static_cast<std::uint8_t>(region.fill)}))) ),
                     0, wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    layout->Add( dialog.CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 );
    dialog.SetSizerAndFit( layout );
    dialog.SetMinSize( wxSize( 740, 300 ) );
    dialog.Centre();
    dialog.Bind(
            wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                try
                {
                    auto copy = component;
                    copy.resources.clear();
                    for( const auto& row : rows )
                    {
                        auto text = row.path->GetValue().ToStdString();
                        if( text.empty() )
                        {
                            if( row.resource.required )
                                throw std::runtime_error( "File required: " + row.resource.label );
                            continue;
                        }
                        unsigned long long offset = 0;
                        if( !row.offset->GetValue().ToULongLong( &offset, 16 ) )
                            throw std::runtime_error( "Invalid hexadecimal load address" );
                        std::filesystem::path path( text );
                        if( path.is_absolute() )
                        {
                            std::error_code error;
                            auto relative = std::filesystem::relative( path, component.projectDirectory, error );
                            if( !error && !relative.empty() && *relative.begin() != ".." )
                                text = relative.string();
                        }
                        auto file = std::filesystem::path( text );
                        if( file.is_relative() )
                            file = std::filesystem::path( component.projectDirectory ) / file;
                        if( !std::filesystem::is_regular_file( file ) ||
                            std::filesystem::file_size( file ) > row.resource.maxBytes )
                            throw std::runtime_error( "Missing or oversized file: " + file.string() );
                        const int index = row.format->GetSelection();
                        copy.resources[row.resource.id] = {
                                text, index > 0 ? row.resource.formats.at( index - 1 ) : "auto", offset };
                    }
                    InitialModelMemories( copy, model );
                    component.resources = std::move( copy.resources );
                    dialog.EndQuasiModal( wxID_OK );
                }
                catch( const std::exception& e )
                {
                    wxMessageBox( DiagnosticText( e.what() ), _( "Component data files" ),
                                  wxOK | wxICON_INFORMATION, &dialog );
                }
            },
            wxID_OK );
    return dialog.ShowQuasiModal() == wxID_OK;
}
} // namespace CIRCUITALIVE
