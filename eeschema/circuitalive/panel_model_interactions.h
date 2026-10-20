/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "model_design.h"
#include <wx/panel.h>
#include <functional>
class KIWAY;
class wxListBox;
namespace CIRCUITALIVE
{
class INTERACTION_PREVIEW;
class PANEL_MODEL_INTERACTIONS : public wxPanel
{
public:
    PANEL_MODEL_INTERACTIONS( wxWindow*, KIWAY*, MODEL_DESIGN&, std::function<void()> aChanged );
    void Reload();
private:
    void addExisting();
    void addInput( bool aSlider );
    void edit();
    void updateList();
    void invoke( const std::function<void()>& );
    MODEL_DESIGN& m_design;
    std::function<void()> m_changed;
    wxListBox* m_list;
    INTERACTION_PREVIEW* m_preview;
    int m_selected = -1;
};
}
