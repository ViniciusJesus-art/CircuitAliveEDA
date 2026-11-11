/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "memory_viewer.h"
#include "memory_view_state.h"
#include "session_runner.h"
#include "choice_controls.h"
#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/frame.h>
#include <wx/grid.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <algorithm>
#include <limits>

namespace CIRCUITALIVE
{
namespace
{
class MEMORY_GRID : public wxGrid
{
public:
    using wxGrid::wxGrid;
    void RefreshSnapshot()
    {
        // wxGrid caches the focused cell's attributes independently of values.
        // Invalidate them when the previous-pause comparison changes as well.
        ClearAttrCache();
        ForceRefresh();
    }
};

class HEX_TABLE : public wxGridTableBase
{
public:
    std::shared_ptr<const MEMORY_SNAPSHOT> snapshot;
    std::vector<bool> changed;
    int GetNumberRows() override { return snapshot ? ( snapshot->image.data.size() + 15 ) / 16 : 0; }
    int GetNumberCols() override { return 17; }
    wxString GetColLabelValue( int col ) override { return col == 16 ? wxString( "ASCII" ) : wxString::Format( "%X", col ); }
    wxString GetRowLabelValue( int row ) override { return wxString::Format( "%06X", row * 16 ); }
    wxString GetValue( int row, int col ) override
    {
        if( !snapshot ) return {};
        const auto& image = snapshot->image;
        const std::size_t start = std::size_t( row ) * 16;
        if( col == 16 )
        {
            wxString text;
            for( std::size_t i = start; i < std::min( start + 16, image.data.size() ); ++i )
            {
                const auto b = image.data[i];
                text += image.known[i] == 255 && b >= 32 && b < 127 ? wxChar( b ) : wxChar( '.' );
            }
            return text;
        }
        const auto index = start + col;
        if( index >= image.data.size() ) return {};
        auto nibble = [&]( unsigned shift ) -> wxString
        {
            return ( ( image.known[index] >> shift ) & 15 ) == 15
                ? wxString::Format( "%X", ( image.data[index] >> shift ) & 15 ) : wxString( "?" );
        };
        return nibble( 4 ) + nibble( 0 );
    }
    void SetValue( int, int, const wxString& ) override {}
    bool IsEmptyCell( int row, int col ) override
    {
        return !snapshot || std::size_t( row ) * 16 + ( col == 16 ? 0 : col ) >= snapshot->image.data.size();
    }
    wxGridCellAttr* GetAttr( int row, int col, wxGridCellAttr::wxAttrKind ) override
    {
        const std::size_t start = std::size_t( row ) * 16;
        bool highlight = false;
        if( col == 16 )
            for( auto i = start; i < std::min( start + 16, changed.size() ); ++i ) highlight |= changed[i];
        else if( start + col < changed.size() ) highlight = changed[start + col];
        if( !highlight ) return nullptr;
        auto* attr = new wxGridCellAttr;
        attr->SetBackgroundColour( wxColour( 255, 214, 100 ) );
        attr->SetTextColour( wxColour( 40, 30, 0 ) );
        return attr;
    }
};

class MEMORY_VIEWER : public wxFrame
{
public:
    MEMORY_VIEWER( wxWindow* parent, std::string reference, const std::vector<MEMORY_REGION_CONFIG>& regions,
                   std::weak_ptr<SESSION_RUNNER> runner, const std::vector<std::size_t>& slots ) :
        wxFrame( parent, wxID_ANY, wxString::FromUTF8( reference ) + " — " + _( "Memory" ),
                 wxDefaultPosition, wxSize( 1060, 700 ) ),
        m_reference( std::move( reference ) ), m_runner( std::move( runner ) ), m_timer( this )
    {
        SetName( "CircuitAliveMemory:" + wxString::FromUTF8( m_reference ) );
        const auto session = m_runner.lock();
        const auto view = session ? session->View() : EDITOR_SESSION_VIEW{};
        for( std::size_t i = 0; i < regions.size(); ++i )
        {
            REGION region;
            region.modelId = regions[i].id;
            region.initial = std::make_shared<MEMORY_SNAPSHOT>( MEMORY_SNAPSHOT{ regions[i].id, 0, regions[i].initial } );
            if( i < slots.size() )
                for( const auto& binding : view.memoryRegions )
                    if( binding.slot == slots[i] && binding.reference == m_reference ) region.modelId = binding.modelId;
            m_regions.push_back( std::move( region ) );
        }
        m_panel = new wxPanel( this );
        auto* layout = new wxBoxSizer( wxVERTICAL );
        auto* row = new wxBoxSizer( wxHORIZONTAL );
        m_choice = new CLICK_CHOICE( m_panel );
        for( const auto& region : regions ) m_choice->Append( ModelText( region.label ) );
        m_choice->SetSelection( 0 );
        m_refresh = new wxButton( m_panel, wxID_ANY, _( "Re-Capture" ) );
        m_refresh->SetToolTip( _( "Read the current paused memory again without advancing time. Updates automatically after each pause." ) );
        m_export = new wxButton( m_panel, wxID_ANY, _( "Export…" ) );
        row->Add( m_choice, 1, wxRIGHT, 10 ); row->Add( m_export, 0, wxRIGHT, 10 ); row->Add( m_refresh );
        layout->Add( row, 0, wxEXPAND | wxALL, 10 );
        m_info = new wxStaticText( m_panel, wxID_ANY, {} );
        m_info->SetName( "memory_status" );
        layout->Add( m_info, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_grid = new MEMORY_GRID( m_panel, wxID_ANY );
        m_grid->SetName( "memory_grid" );
        m_table = new HEX_TABLE;
        m_grid->SetTable( m_table, true );
        m_grid->EnableEditing( false );
        m_grid->SetRowLabelSize( 80 );
        m_grid->SetDefaultCellFont( wxFontInfo( 11 ).Family( wxFONTFAMILY_TELETYPE ) );
        for( int col = 0; col < 16; ++col ) m_grid->SetColSize( col, 40 );
        m_grid->SetColSize( 16, 205 );
        layout->Add( m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        auto* bottom = new wxBoxSizer( wxHORIZONTAL );
        m_address = new wxTextCtrl( m_panel, wxID_ANY, "0", wxDefaultPosition, wxSize( 100, -1 ), wxTE_PROCESS_ENTER );
        m_go = new wxButton( m_panel, wxID_ANY, _( "Go to hex address" ) );
        bottom->Add( new wxStaticText( m_panel, wxID_ANY, "0x" ), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
        bottom->Add( m_address, 0, wxRIGHT, 6 ); bottom->Add( m_go, 0, wxRIGHT, 18 );
        auto* legend = new wxStaticText( m_panel, wxID_ANY,
            _( "Yellow = changed since the previous pause (first pause: initial contents). ? = unknown bits (X). Read-only; capture does not advance time." ) );
        legend->Wrap( 540 );
        bottom->Add( legend, 1, wxALIGN_CENTER_VERTICAL );
        layout->Add( bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10 );
        m_panel->SetSizer( layout );
        SetMinSize( wxSize( 960, 450 ) );
        CentreOnParent();
        m_choice->Bind( wxEVT_COMBOBOX, [this]( wxCommandEvent& ) { poll(); } );
        m_refresh->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& )
        {
            if( auto runner = m_runner.lock() )
            {
                const auto view = runner->View();
                if( paused( view ) )
                    if( const auto* binding = findRegion( view, selected() ) ) request( *runner, *binding, selected(), view.pauseSequence );
            }
            poll();
        } );
        m_go->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { jump(); } );
        m_address->Bind( wxEVT_TEXT_ENTER, [this]( wxCommandEvent& ) { jump(); } );
        m_export->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { exportMemory(); } );
        Bind( wxEVT_TIMER, [this]( wxTimerEvent& ) { poll(); } );
        Bind( wxEVT_CLOSE_WINDOW, [this]( wxCloseEvent& ) { m_timer.Stop(); Destroy(); } );
        poll();
        m_timer.Start( 100 );
    }
private:
    static constexpr auto NOT_REQUESTED = std::numeric_limits<std::uint64_t>::max();
    struct REGION
    {
        std::string modelId;
        std::shared_ptr<const MEMORY_SNAPSHOT> initial;
        MEMORY_PAUSE_COMPARISON comparison;
        std::uint64_t pending = 0, requestedPause = NOT_REQUESTED;
        std::string error;
    };
    REGION& selected() { return m_regions.at( m_choice->GetSelection() ); }
    static bool paused( const EDITOR_SESSION_VIEW& view )
    {
        return view.state == EDITOR_SESSION_STATE::PAUSED || view.state == EDITOR_SESSION_STATE::COMPLETED;
    }
    const SESSION_MEMORY_REGION* findRegion( const EDITOR_SESSION_VIEW& view, const REGION& region ) const
    {
        for( const auto& binding : view.memoryRegions )
            if( binding.reference == m_reference && binding.modelId == region.modelId ) return &binding;
        return nullptr;
    }
    void request( SESSION_RUNNER& runner, const SESSION_MEMORY_REGION& binding, REGION& region, std::uint64_t pause )
    {
        if( region.pending ) return;
        if( const auto request = runner.CaptureMemory( binding.slot ) )
        {
            region.pending = request;
            region.requestedPause = pause;
            region.error.clear();
        }
    }
    void poll()
    {
        const auto runner = m_runner.lock();
        const auto view = runner ? runner->View() : EDITOR_SESSION_VIEW{};
        if( view.sessionRevision != m_revision )
        {
            m_revision = view.sessionRevision;
            for( auto& region : m_regions )
            {
                region.comparison = {};
                region.pending = 0; region.requestedPause = NOT_REQUESTED; region.error.clear();
            }
        }
        m_everStarted |= view.state != EDITOR_SESSION_STATE::STOPPED;
        if( paused( view ) )
            for( auto& region : m_regions )
            {
                const auto* binding = findRegion( view, region );
                if( !binding ) continue;
                if( region.requestedPause != view.pauseSequence )
                {
                    region.pending = 0;
                    request( *runner, *binding, region, view.pauseSequence );
                }
                if( !region.pending ) continue;
                const auto it = view.memoryCaptures.find( binding->id );
                if( it == view.memoryCaptures.end() || it->second.request < region.pending
                    || it->second.pause != view.pauseSequence ) continue;
                region.pending = 0;
                region.error = it->second.error;
                if( it->second.snapshot )
                    region.comparison.Accept( it->second.snapshot, m_revision, view.pauseSequence, binding->initial );
            }
        else
            for( auto& region : m_regions ) region.pending = 0;

        auto& region = selected();
        const bool initial = !m_everStarted && view.state == EDITOR_SESSION_STATE::STOPPED;
        const bool current = paused( view ) && findRegion( view, region );
        const auto snapshot = initial ? region.initial : current ? region.comparison.Snapshot() : nullptr;
        const bool available = snapshot && region.error.empty();
        m_refresh->Enable( current && !region.pending );
        m_export->Enable( available && !region.pending );
        m_address->Enable( available ); m_go->Enable( available );
        if( m_grid->IsShown() != available ) { m_grid->Show( available ); m_panel->Layout(); }
        if( available && m_table->snapshot != snapshot )
        {
            m_grid->BeginBatch();
            const auto oldRows = m_table->GetNumberRows();
            m_table->snapshot = snapshot;
            m_table->changed = initial ? std::vector<bool>{} : region.comparison.Changed();
            const auto newRows = m_table->GetNumberRows();
            if( newRows > oldRows )
            {
                wxGridTableMessage message( m_table, wxGRIDTABLE_NOTIFY_ROWS_APPENDED, newRows - oldRows );
                m_grid->ProcessTableMessage( message );
            }
            else if( newRows < oldRows )
            {
                wxGridTableMessage message( m_table, wxGRIDTABLE_NOTIFY_ROWS_DELETED, newRows, oldRows - newRows );
                m_grid->ProcessTableMessage( message );
            }
            m_grid->EndBatch(); m_grid->RefreshSnapshot();
        }
        wxString status;
        if( view.state == EDITOR_SESSION_STATE::RUNNING || view.state == EDITOR_SESSION_STATE::PREPARING )
            status = _( "Simulation running — pause the simulation to view memory. The table updates automatically when paused." );
        else if( current && !region.error.empty() ) status = DiagnosticText( region.error );
        else if( current && region.pending ) status = _( "Capturing paused memory…" );
        else if( available && initial )
            status = wxString::Format( _( "Initial contents — %zu bytes; loaded on the next Play" ), snapshot->image.data.size() );
        else if( available )
            status = wxString::Format( _( "Paused snapshot — t = %.9g s; %zu bytes; %zu changed bytes" ),
                snapshot->time, snapshot->image.data.size(), std::size_t( std::count( region.comparison.Changed().begin(), region.comparison.Changed().end(), true ) ) );
        else if( paused( view ) ) status = _( "This memory is not present in the current simulation." );
        else status = _( "Simulation stopped — start and pause the simulation to view memory." );
        if( m_info->GetLabel() != status ) { m_info->SetLabel( status ); m_panel->Layout(); }
    }
    void jump()
    {
        unsigned long long value;
        if( m_grid->IsShown() && m_table->snapshot && m_address->GetValue().ToULongLong( &value, 16 )
            && value < m_table->snapshot->image.data.size() )
        {
            m_grid->SetGridCursor( value / 16, value % 16 );
            m_grid->MakeCellVisible( value / 16, value % 16 );
        }
    }
    void exportMemory()
    {
        if( !m_export->IsEnabled() || !m_table->snapshot ) return;
        const auto snapshot = m_table->snapshot;
        wxFileDialog picker( this, _( "Export memory snapshot" ), {}, wxString::FromUTF8( m_reference + ".camem" ),
            _( "CircuitAlive snapshot (*.camem)|*.camem|Binary (*.bin)|*.bin|Intel HEX (*.hex)|*.hex" ),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT );
        if( picker.ShowModal() != wxID_OK ) return;
        try
        {
            const char* formats[] = { "camem", "bin", "ihex" };
            SaveMemoryImage( snapshot->image, picker.GetPath().ToStdString( wxConvUTF8 ), formats[picker.GetFilterIndex()] );
        }
        catch( const std::exception& error )
        {
            wxMessageBox( DiagnosticText( error.what() ), _( "Memory export" ), wxOK | wxICON_INFORMATION, this );
        }
    }
    std::string m_reference;
    std::weak_ptr<SESSION_RUNNER> m_runner;
    wxTimer m_timer;
    std::vector<REGION> m_regions;
    std::uint64_t m_revision = 0;
    bool m_everStarted = false;
    wxPanel* m_panel;
    CLICK_CHOICE* m_choice;
    wxButton *m_refresh, *m_export, *m_go;
    wxStaticText* m_info;
    wxTextCtrl* m_address;
    MEMORY_GRID* m_grid;
    HEX_TABLE* m_table;
};
}

wxFrame* ShowMemoryViewer( wxWindow* parent, const std::string& reference, const std::vector<MEMORY_REGION_CONFIG>& regions,
                          std::weak_ptr<SESSION_RUNNER> runner, const std::vector<std::size_t>& slots )
{
    if( regions.empty() ) return nullptr;
    const auto name = "CircuitAliveMemory:" + wxString::FromUTF8( reference );
    for( auto* child : parent->GetChildren() )
        if( auto* frame = dynamic_cast<MEMORY_VIEWER*>( child ); frame && frame->GetName() == name && !frame->IsBeingDeleted() )
        {
            frame->Show(); frame->Raise(); return frame;
        }
    auto* viewer = new MEMORY_VIEWER( parent, reference, regions, std::move( runner ), slots );
    viewer->Show();
    return viewer;
}
}
