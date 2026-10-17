/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <wx/odcombo.h>
#include <wx/grid.h>

namespace CIRCUITALIVE
{
// Use the generic popup rather than GTK's menu-style choice: release the first
// click to leave the list open, then click an item. Shared by all our new panels.
class CLICK_CHOICE : public wxOwnerDrawnComboBox
{
public:
    CLICK_CHOICE( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& value = {},
                  const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
                  int n = 0, const wxString* choices = nullptr, long style = wxCB_READONLY ) :
        wxOwnerDrawnComboBox( parent, id, value, pos, size, n, choices, style ) {}
};

class CLICK_CHOICE_EDITOR : public wxGridCellEditor
{
public:
    CLICK_CHOICE_EDITOR( const wxArrayString& choices, bool editable = false ) :
        m_choices( choices ), m_editable( editable ) {}
    wxGridCellEditor* Clone() const override { return new CLICK_CHOICE_EDITOR( m_choices, m_editable ); }
    void Create( wxWindow* parent, wxWindowID id, wxEvtHandler* handler ) override
    {
        m_control = new CLICK_CHOICE( parent, id, {}, wxDefaultPosition, wxDefaultSize,
            0, nullptr, wxTE_PROCESS_ENTER | ( m_editable ? 0 : wxCB_READONLY ) );
        for( const auto& choice : m_choices ) combo()->Append( choice );
        wxGridCellEditor::Create( parent, id, handler );
    }
    void BeginEdit( int row, int col, wxGrid* grid ) override
    {
        m_initial = grid->GetTable()->GetValue( row, col );
        if( m_editable ) combo()->ChangeValue( m_initial );
        else combo()->SetStringSelection( m_initial );
        combo()->SetFocus();
    }
    bool EndEdit( int, int, const wxGrid*, const wxString& old, wxString* value ) override
    {
        m_pending = combo()->GetValue();
        if( m_pending == old ) return false;
        if( value ) *value = m_pending;
        return true;
    }
    void ApplyEdit( int row, int col, wxGrid* grid ) override
    { grid->GetTable()->SetValue( row, col, m_pending ); }
    void Reset() override
    {
        if( m_editable ) combo()->ChangeValue( m_initial );
        else combo()->SetStringSelection( m_initial );
    }
    wxString GetValue() const override { return combo()->GetValue(); }
private:
    CLICK_CHOICE* combo() const { return static_cast<CLICK_CHOICE*>( m_control ); }
    wxArrayString m_choices;
    bool m_editable;
    wxString m_initial, m_pending;
};
}
