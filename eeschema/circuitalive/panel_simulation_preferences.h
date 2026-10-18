/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_preferences.h"
#include <widgets/resettable_panel.h>
#include <vector>

class wxCheckBox;
class wxSpinCtrlDouble;

namespace CIRCUITALIVE
{
class PANEL_SIMULATION_PREFERENCES : public RESETTABLE_PANEL
{
public:
    explicit PANEL_SIMULATION_PREFERENCES( wxWindow* aParent );
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;
    void ResetPanel() override;

private:
    void loadValues( SIMULATION_PREFERENCES aPreferences );
    wxSpinCtrlDouble *m_ledCutoff, *m_digitalCutoff, *m_refreshRate, *m_speed, *m_step;
    wxSpinCtrlDouble *m_timeStep, *m_duration, *m_timeout, *m_history;
    wxCheckBox *m_holdLeds, *m_markDigital, *m_showDigital, *m_pushReleased, *m_autoInstruments;
    wxCheckBox *m_overcurrent, *m_supply;
    struct NUMBER { wxSpinCtrlDouble* control; wxString label; };
    std::vector<NUMBER> m_numbers;
};
}
