/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "schematic_adapter.h"
#include "session_runner.h"
#include "scope_panel.h"
#include <wx/panel.h>
#include <functional>
#include <optional>

class wxNotebook;
class wxTextCtrl;
class wxStaticText;
class wxListCtrl;
class wxToggleButton;
class wxButton;
namespace CIRCUITALIVE
{

class INSTRUMENT_PANEL : public wxPanel
{
public:
    INSTRUMENT_PANEL( wxWindow* aParent, std::function<void( PROBE_KIND )> aChooseProbe,
        std::function<void( double )> aSpeed, std::function<void()> aStep );
    ~INSTRUMENT_PANEL() override;
    std::string ProbeName() const;
    void ClearProbeName();
    void SelectTools();
    void SetPlacement( std::optional<PROBE_KIND> aKind );
    void UpdateSession( const SCHEMATIC_SNAPSHOT& aCircuit, const EDITOR_SESSION_VIEW& aSession, bool aActive );

private:
    wxNotebook* m_tabs;
    wxTextCtrl* m_name;
    wxStaticText* m_help;
    CLICK_CHOICE* m_speed;
    wxButton* m_step;
    std::vector<wxToggleButton*> m_buttons;
    wxPanel *m_measurePage = nullptr, *m_scopePage = nullptr;
    wxListCtrl* m_values = nullptr;
    SCOPE_PANEL* m_scope = nullptr;
    SCOPE_PREFERENCES m_scopePreferences;
    std::vector<std::string> m_rows;
    bool m_wasActive = false;
    std::chrono::steady_clock::time_point m_lastValues{};
};
}
