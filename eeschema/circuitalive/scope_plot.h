/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "scope_waveform.h"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <chrono>

namespace CIRCUITALIVE
{
class SCOPE_PANEL;
class SCOPE_PLOT : public wxPanel
{
public:
    SCOPE_PLOT( wxWindow*, SCOPE_PANEL& );
private:
    struct MARKER { wxRect bounds; int channel; bool outside; };
    wxRect area() const;
    int y( double, std::size_t ) const;
    int x( double ) const;
    std::size_t triggerSource() const;
    std::vector<MARKER> markers() const;
    void paint( wxPaintEvent& );
    void rebuild();
    void down( wxMouseEvent& );
    void move( wxPoint );
    void up( wxMouseEvent& );
    void hover( wxMouseEvent& );
    SCOPE_PANEL& m_owner;
    wxBitmap m_trace;
    std::uint64_t m_cachedSerial = -1;
    std::vector<SCOPE_CHANNEL_STYLE> m_cachedStyles;
    int m_drag = 0; // 1..4 cursors, 10 trigger, 100+channel zero markers.
    int m_dragOffset = 0;
    bool m_markerCursor = false;
    wxString m_tooltip;
    std::chrono::steady_clock::time_point m_lastDrag{};
};
}
