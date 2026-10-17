/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "memory_viewer.h"
#include "session_runner.h"
#include "choice_controls.h"
#include <dialog_shim.h>
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
namespace CIRCUITALIVE
{
namespace
{
class HEX_TABLE : public wxGridTableBase
{
  public:
    MEMORY_IMAGE image;
    explicit HEX_TABLE( MEMORY_IMAGE data ) : image( std::move( data ) ) {}
    int GetNumberRows() override
    {
        return ( image.data.size() + 15 ) / 16;
    }
    int GetNumberCols() override
    {
        return 17;
    }
    wxString GetColLabelValue( int col ) override
    {
        return col == 16 ? "ASCII" : wxString::Format( "%X", col );
    }
    wxString GetRowLabelValue( int row ) override
    {
        return wxString::Format( "%06X", row * 16 );
    }
    wxString GetValue( int row, int col ) override
    {
        const std::size_t start = std::size_t( row ) * 16;
        if( col == 16 )
        {
            wxString s;
            for( std::size_t i = start; i < std::min( start + 16, image.data.size() ); ++i )
            {
                auto b = image.data[i];
                s += ( image.known[i] == 255 && b >= 32 && b < 127 ) ? wxChar( b ) : wxChar( '.' );
            }
            return s;
        }
        const auto index = start + col;
        if( index >= image.data.size() )
            return {};
        auto nibble = [&]( unsigned shift ) -> wxString
        {
            return ( ( image.known[index] >> shift ) & 15 ) == 15
                           ? wxString::Format( "%X", ( image.data[index] >> shift ) & 15 )
                           : "?";
        };
        return nibble( 4 ) + nibble( 0 );
    }
    void SetValue( int, int, const wxString& ) override {}
    bool IsEmptyCell( int row, int col ) override
    {
        return std::size_t( row ) * 16 + ( col == 16 ? 0 : col ) >= image.data.size();
    }
};
} // namespace
void ShowMemoryViewer( wxWindow* parent, const std::string& reference, const std::vector<MEMORY_REGION_CONFIG>& regions,
                       SESSION_RUNNER* runner, const std::vector<std::size_t>& slots )
{
    if( regions.empty() )
        return;
    DIALOG_SHIM dialog( parent, wxID_ANY, wxString::FromUTF8( reference + " — Memory" ), wxDefaultPosition,
                        wxSize( 1000, 660 ), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    dialog.OptOut( &dialog );
    auto* layout = new wxBoxSizer( wxVERTICAL );
    auto* row = new wxBoxSizer( wxHORIZONTAL );
    auto* choice = new CLICK_CHOICE( &dialog, wxID_ANY );
    for( const auto& r : regions )
        choice->Append( wxString::FromUTF8( r.label ) );
    choice->SetSelection( 0 );
    auto* refresh = new wxButton( &dialog, wxID_ANY, _( "Capture paused state" ) );
    refresh->Enable( runner != nullptr );
    auto* exportFile = new wxButton( &dialog, wxID_ANY, _( "Export…" ) );
    row->Add( choice, 1, wxRIGHT, 10 );
    row->Add( refresh, 0, wxRIGHT, 10 );
    row->Add( exportFile );
    layout->Add( row, 0, wxEXPAND | wxALL, 10 );
    auto* info = new wxStaticText( &dialog, wxID_ANY, "" );
    layout->Add( info, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    auto* grid = new wxGrid( &dialog, wxID_ANY );
    HEX_TABLE* table = nullptr;
    grid->EnableEditing( false );
    grid->SetRowLabelSize( 80 );
    grid->SetDefaultCellFont( wxFontInfo( 11 ).Family( wxFONTFAMILY_TELETYPE ) );
    layout->Add( grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    auto* bottom = new wxBoxSizer( wxHORIZONTAL );
    auto* address = new wxTextCtrl( &dialog, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER );
    auto* go = new wxButton( &dialog, wxID_ANY, _( "Go to hex address" ) );
    bottom->Add( new wxStaticText( &dialog, wxID_ANY, "0x" ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
    bottom->Add( address, 0, wxRIGHT, 6 );
    bottom->Add( go, 0, wxRIGHT, 18 );
    bottom->Add( new wxStaticText( &dialog, wxID_ANY,
                                   _( "? = unknown bits (X). Read-only snapshot; capture does not advance time." ) ),
                 1, wxALIGN_CENTER_VERTICAL );
    layout->Add( bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
    layout->Add( dialog.CreateSeparatedButtonSizer( wxOK ), 0, wxEXPAND | wxALL, 10 );
    dialog.SetSizer( layout );
    dialog.SetMinSize( wxSize( 900, 450 ) );
    dialog.Centre();
    bool pending = false;
    std::uint64_t sequence = runner ? runner->View().memorySequence : 0;
    auto display = [&]( const MEMORY_IMAGE& image, const wxString& text )
    {
        grid->BeginBatch();
        table = new HEX_TABLE( image );
        grid->SetTable( table, true );
        grid->SetRowLabelSize( 80 );
        for( int c = 0; c < 16; ++c )
            grid->SetColSize( c, 40 );
        grid->SetColSize( 16, 205 );
        grid->EndBatch();
        info->SetLabel( text );
        exportFile->Enable( true );
    };
    auto capture = [&]()
    {
        const auto index = std::size_t( choice->GetSelection() );
        if( runner )
        {
            choice->Enable( false );
            sequence = runner->View().memorySequence;
            pending = true;
            exportFile->Enable( false );
            info->SetLabel( _( "Pausing and capturing memory…" ) );
            runner->CaptureMemory( slots.at( index ) );
        }
        else
            display( regions[index].initial,
                     wxString::Format( _( "Initial contents — %zu bytes; loaded on the next Play" ),
                                       regions[index].initial.data.size() ) );
    };
    wxTimer timer( &dialog );
    dialog.Bind( wxEVT_TIMER,
                 [&]( wxTimerEvent& )
                 {
                     if( !pending || !runner )
                         return;
                     const auto view = runner->View();
                     if( view.memorySequence <= sequence )
                         return;
                     pending = false;
                     choice->Enable( true );
                     sequence = view.memorySequence;
                     if( !view.memoryError.empty() )
                     {
                         info->SetLabel( wxString::FromUTF8( view.memoryError ) );
                         return;
                     }
                     const auto index = std::size_t( choice->GetSelection() );
                     if( view.memory && view.memory->id == regions[index].id )
                         display( view.memory->image,
                                  wxString::Format( _( "Paused snapshot — t = %.9g s; %zu bytes" ), view.memory->time,
                                                    view.memory->image.data.size() ) );
                 } );
    refresh->Bind( wxEVT_BUTTON,
                   [&]( wxCommandEvent& )
                   {
                       if( !pending )
                           capture();
                   } );
    choice->Bind( wxEVT_CHOICE, [&]( wxCommandEvent& ) { capture(); } );
    auto jump = [&]()
    {
        unsigned long long value;
        if( address->GetValue().ToULongLong( &value, 16 ) && table && value < table->image.data.size() )
        {
            grid->SetGridCursor( value / 16, value % 16 );
            grid->MakeCellVisible( value / 16, value % 16 );
        }
    };
    go->Bind( wxEVT_BUTTON, [&]( wxCommandEvent& ) { jump(); } );
    address->Bind( wxEVT_TEXT_ENTER, [&]( wxCommandEvent& ) { jump(); } );
    exportFile->Bind(
            wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                if( !table || pending )
                    return;
                wxFileDialog picker(
                        &dialog, _( "Export memory snapshot" ), "", wxString::FromUTF8( reference + ".camem" ),
                        _( "CircuitAlive snapshot (*.camem)|*.camem|Binary (*.bin)|*.bin|Intel HEX (*.hex)|*.hex" ),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT );
                if( picker.ShowModal() != wxID_OK )
                    return;
                try
                {
                    const char* formats[] = { "camem", "bin", "ihex" };
                    SaveMemoryImage( table->image, picker.GetPath().ToStdString(), formats[picker.GetFilterIndex()] );
                }
                catch( const std::exception& e )
                {
                    wxMessageBox( wxString::FromUTF8( e.what() ), _( "Memory export" ), wxOK | wxICON_INFORMATION,
                                  &dialog );
                }
            } );
    capture();
    timer.Start( 100 );
    dialog.ShowQuasiModal();
    timer.Stop();
}
} // namespace CIRCUITALIVE
