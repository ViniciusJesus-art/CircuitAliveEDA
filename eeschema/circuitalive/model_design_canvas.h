/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "model_routing.h"
#include <wx/scrolwin.h>
#include <functional>
#include <optional>
namespace CIRCUITALIVE
{
class DESIGN_CANVAS : public wxScrolledWindow
{
public:
    DESIGN_CANVAS( wxWindow* aParent, MODEL_DESIGN& aDesign );
    MODEL_DESIGN& design;
    std::function<void()> changed, edit, selectionChanged;
    int selected = -1;
    void CancelConnection();
    void Zoom( double aFactor );
    void FitContent();
    void ResetRoutes();
private:
    wxPoint logicalPosition( wxPoint aPoint ) const;
    wxSize contentSize() const;
    void updateExtent();
    const SIMULATION_MODEL& visualModel( const DESIGN_BLOCK& aBlock ) const;
    int height( const DESIGN_BLOCK& aBlock ) const;
    std::vector<DESIGN_TERMINAL> terminals() const;
    const std::vector<DESIGN_WIRE>& wires();
    void down( wxMouseEvent& aEvent );
    void motion( wxMouseEvent& aEvent );
    void paint();
    void release();
    void storeRoute( const DESIGN_WIRE& aWire );
    std::pair<int,int> hitWire( wxPoint aPoint );
    mutable std::map<std::string, std::pair<std::string, SIMULATION_MODEL>> m_models;
    std::optional<DESIGN_TERMINAL> m_pending;
    std::optional<DESIGN_WIRE> m_dragWire;
    std::vector<DESIGN_WIRE> m_wires;
    std::vector<DESIGN_JUNCTION> m_junctions;
    std::string m_geometry;
    int m_drag = -1, m_selectedWire = -1, m_segment = -1;
    wxPoint m_offset;
    double m_zoom = 1;
};
}
