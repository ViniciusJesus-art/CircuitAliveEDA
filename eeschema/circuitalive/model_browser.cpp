/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "model_i18n.h"
#include "model_browser.h"
#include "model_catalog_index.h"
#include <dialog_shim.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <map>

namespace CIRCUITALIVE
{
namespace
{
wxString W( const std::string& s ) { return wxString::FromUTF8( s ); }
class MODEL_LIST : public wxListCtrl
{
public:
    MODEL_LIST( wxWindow* parent, const MODEL_CATALOG_INDEX& index ) :
        wxListCtrl( parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_VIRTUAL ), m_index( index )
    {
        InsertColumn( 0, _( "Model" ), wxLIST_FORMAT_LEFT, 300 );
        InsertColumn( 1, _( "Family" ), wxLIST_FORMAT_LEFT, 160 );
        InsertColumn( 2, _( "Library symbols" ), wxLIST_FORMAT_LEFT, 330 );
    }
    std::vector<std::size_t> rows;
private:
    wxString OnGetItemText( long item, long column ) const override
    {
        if( item < 0 || static_cast<std::size_t>( item ) >= rows.size() ) return {};
        const auto& r = m_index.Records().at( rows[item] );
        return W( column == 0 ? r.name : column == 1 ? r.family : r.libraries );
    }
    const MODEL_CATALOG_INDEX& m_index;
};
class BROWSER : public DIALOG_SHIM
{
public:
    BROWSER( wxWindow* parent, const std::vector<SIMULATION_MODEL>& models, int selected ) :
        DIALOG_SHIM( parent, wxID_ANY, _( "Choose simulation model" ), wxDefaultPosition,
            wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_models( models ), m_index( ModelsForDisplay( models ) ), m_timer( this )
    {
        OptOut( this ); SetInitialFocus( nullptr );
        auto* box = new wxBoxSizer( wxVERTICAL );
        m_search = new wxSearchCtrl( this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER );
        m_search->SetDescriptiveText( _( "Search name, ID, family or library symbol…" ) );
        m_search->ShowCancelButton( true ); box->Add( m_search, 0, wxEXPAND | wxALL, 10 );
        auto* middle = new wxBoxSizer( wxHORIZONTAL );
        m_tree = new wxTreeCtrl( this, wxID_ANY, wxDefaultPosition, wxSize( 240, 360 ),
            wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxBORDER_SUNKEN );
        const auto root = m_tree->AddRoot( _( "All models" ) ); m_paths[root] = "";
        std::map<std::string, wxTreeItemId> groups; groups[""] = root;
        for( const auto& r : m_index.Records() )
        {
            std::string path; auto parentItem = root;
            std::size_t start = 0;
            while( start < r.group.size() )
            {
                const auto end = r.group.find( '/', start ); const auto part = r.group.substr( start, end - start );
                if( !path.empty() ) path += '/';
                path += part;
                if( !groups.contains( path ) )
                { auto item = m_tree->AppendItem( parentItem, W( part ) ); groups[path] = item; m_paths[item] = path; }
                parentItem = groups.at( path );
                if( end == std::string::npos ) break;
                start = end + 1;
            }
        }
        for( const auto& [path, item] : groups ) m_tree->SortChildren( item );
        m_list = new MODEL_LIST( this, m_index );
        middle->Add( m_tree, 0, wxEXPAND | wxRIGHT, 8 ); middle->Add( m_list, 1, wxEXPAND );
        box->Add( middle, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );
        m_details = new wxStaticText( this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE );
        m_details->SetMinSize( wxSize( 0, FromDIP( 80 ) ) );
        box->Add( m_details, 0, wxEXPAND | wxALL, 10 );
        box->Add( CreateSeparatedButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 );
        SetSizer( box ); SetMinSize( wxSize( 820, 540 ) ); SetSize( 1120, 700 ); Centre();
        m_tree->Expand( root );
        // Open near the current model, but a typed search starts globally.
        const auto initial = selected >= 0 && static_cast<std::size_t>( selected ) < models.size()
            ? groups.at( m_index.Records()[selected].group ) : root;
        m_tree->SelectItem( initial ); m_tree->EnsureVisible( initial );
        refresh( selected );
        m_tree->Bind( wxEVT_TREE_SEL_CHANGED, [this]( wxTreeEvent& ) { refresh(); } );
        m_search->Bind( wxEVT_TEXT, [this, root]( wxCommandEvent& )
        { m_timer.Stop(); m_tree->SelectItem( root ); m_timer.StartOnce( 120 ); } );
        m_search->Bind( wxEVT_SEARCHCTRL_CANCEL_BTN, [this]( wxCommandEvent& ) { m_search->SetValue( "" ); } );
        m_search->Bind( wxEVT_TEXT_ENTER, [this]( wxCommandEvent& ) { m_timer.Stop(); refresh(); m_list->SetFocus(); } );
        Bind( wxEVT_TIMER, [this]( wxTimerEvent& ) { refresh(); } );
        m_list->Bind( wxEVT_LIST_ITEM_SELECTED, [this]( wxListEvent& ) { details(); } );
        m_list->Bind( wxEVT_LIST_ITEM_ACTIVATED, [this]( wxListEvent& ) { accept(); } );
        Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { accept(); }, wxID_OK );
        SetInitialFocus( m_search );
    }
    std::optional<std::size_t> result;
private:
    std::optional<std::size_t> selection() const
    {
        const auto row = m_list->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
        if( row < 0 || static_cast<std::size_t>( row ) >= m_list->rows.size() ) return {};
        return m_index.Records().at( m_list->rows[row] ).model;
    }
    void refresh( int prefer = -1 )
    {
        if( prefer < 0 ) if( const auto current = selection() ) prefer = *current;
        m_list->Freeze(); m_list->SetItemCount( 0 );
        const auto item = m_tree->GetSelection();
        m_list->rows = m_index.Find( m_search->GetValue().ToStdString(wxConvUTF8), item.IsOk() ? m_paths.at( item ) : "" );
        m_list->SetItemCount( m_list->rows.size() );
        if( !m_list->rows.empty() )
        {
            long chosen = 0;
            for( std::size_t i = 0; i < m_list->rows.size(); ++i )
                if( static_cast<int>( m_index.Records()[m_list->rows[i]].model ) == prefer ) chosen = i;
            m_list->SetItemState( chosen, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED ); m_list->EnsureVisible( chosen );
        }
        m_list->Thaw(); m_list->Refresh(); details();
    }
    void details()
    {
        FindWindow( wxID_OK )->Enable( selection().has_value() );
        wxString text = wxString::Format( _( "%zu models found" ), m_list->rows.size() );
        if( const auto index = selection() )
        {
            const auto& model = m_models.at( *index );
            text += "\n" + ModelText( model.description ) + "\n" + W( model.file.string() );
        }
        m_details->SetLabel( text ); m_details->Wrap( GetClientSize().x - FromDIP( 30 ) );
    }
    void accept() { if( auto picked = selection() ) { result = picked; EndQuasiModal( wxID_OK ); } }
    const std::vector<SIMULATION_MODEL>& m_models;
    MODEL_CATALOG_INDEX m_index;
    wxTimer m_timer;
    wxSearchCtrl* m_search;
    wxTreeCtrl* m_tree;
    MODEL_LIST* m_list;
    wxStaticText* m_details;
    std::map<wxTreeItemId, std::string> m_paths;
};
}
std::optional<std::size_t> ChooseSimulationModel( wxWindow* parent,
        const std::vector<SIMULATION_MODEL>& models, int selected )
{
    BROWSER browser( parent, models, selected );
    if( browser.ShowQuasiModal() != wxID_OK ) return {};
    return browser.result;
}
}
