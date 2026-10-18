/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_model.h"
#include <wx/scrolwin.h>
#include <functional>
class wxColourPickerCtrl;
class wxSpinCtrlDouble;
namespace CIRCUITALIVE
{
class PANEL_LED_APPEARANCE : public wxScrolledWindow
{
public:
    explicit PANEL_LED_APPEARANCE(wxWindow*);
    void Configure(const SIMULATION_MODEL&,std::function<double(const std::string&)> aGet,
                   std::function<void(const std::string&,const std::string&)> aSet);
private:
    void preview();
    LED_APPEARANCE_PARAMETERS m_binding;
    LED_APPEARANCE m_value;
    wxColourPickerCtrl* m_color;
    std::array<wxSpinCtrlDouble*,4> m_numbers;
    wxPanel* m_preview;
    std::function<void(const std::string&,const std::string&)> m_set;
    bool m_loading=false;
};
}
