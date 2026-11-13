/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "model_pin_mapping.h"
#include <wx/panel.h>

class LIB_SYMBOL;
class wxListCtrl;
class wxStaticText;
class wxButton;

namespace CIRCUITALIVE
{
class CLICK_CHOICE;
class MODEL_PIN_PREVIEW;

class PANEL_MODEL_PIN_MAPPING : public wxPanel
{
public:
    PANEL_MODEL_PIN_MAPPING( wxWindow* aParent, LIB_SYMBOL* aSymbol, int aUnit, int aBodyStyle );
    void Configure( const SIMULATION_MODEL* aModel, const CIRCUIT_COMPONENT& aComponent );
    const std::map<std::string, std::string>& Mapping() const { return m_mapping; }
    bool Complete() const;

private:
    void select( int aRow );
    void assign( const std::string& aPhysical );
    void refresh();
    wxString pinLabel( const std::string& aPhysical ) const;
    wxListCtrl* m_ports;
    MODEL_PIN_PREVIEW* m_preview;
    CLICK_CHOICE* m_physical;
    wxStaticText* m_status;
    wxButton *m_connect, *m_unlink, *m_clear, *m_defaults;
    std::vector<MODEL_PIN> m_modelPins;
    std::vector<std::string> m_physicalPins;
    std::map<std::string, std::string> m_pinNames, m_mapping, m_defaultMapping;
    int m_selected = -1;
    bool m_refreshing = false;
};
}
