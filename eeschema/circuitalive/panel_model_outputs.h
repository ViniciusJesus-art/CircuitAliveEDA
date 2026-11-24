/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "model_design.h"
#include <wx/panel.h>
#include <functional>
class KIWAY;
class wxListBox;
namespace CIRCUITALIVE
{
class OUTPUT_PREVIEW;
class PANEL_MODEL_OUTPUTS : public wxPanel
{
public:
    PANEL_MODEL_OUTPUTS(wxWindow*,KIWAY*,MODEL_DESIGN&,std::function<void()>);
    void Reload();
private:
    void store();
    void list();
    void add(const std::string&);
    void edit();
    MODEL_DESIGN& m_design;
    std::function<void()> m_changed;
    std::vector<MODEL_SEGMENT> m_segments;
    int m_selected=-1;
    wxListBox* m_list;
    OUTPUT_PREVIEW* m_preview;
};
}
