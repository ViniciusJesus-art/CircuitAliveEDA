/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "scope_acquisition.h"
#include "scope_waveform.h"
#include <chrono>
#include "session_runner.h"
#include <wx/panel.h>
#include <functional>
#include <map>

class wxTextCtrl;
class wxStaticText;
class wxCheckBox;
class wxToggleButton;
class wxListCtrl;
class wxNotebook;

namespace CIRCUITALIVE
{
class CLICK_CHOICE;
/// Owned by the instruments panel, so Stop/Play preserves controls in this editor.
struct SCOPE_PREFERENCES
{
    SCOPE_TIMEBASE timebase;
    SCOPE_TRIGGER trigger;
    std::string triggerSource, selectedChannel;
    std::map<std::string, SCOPE_CHANNEL_STYLE> channels;
    int cursorMode = 0; // Time = 1, amplitude = 2, both = 3.
    double cursorA = .3, cursorB = .7, levelA = 0, levelB = 1;
};

class SCOPE_PLOT;
class SCOPE_PANEL : public wxPanel
{
public:
    SCOPE_PANEL( wxWindow*, SCOPE_PREFERENCES&, std::function<void()> aExpand );
    ~SCOPE_PANEL() override;
    void UpdateSession( const CIRCUIT_SNAPSHOT&, const EDITOR_SESSION_VIEW& );

private:
    friend class SCOPE_PLOT;
    void configure();
    void retime();
    void present();
    void syncControls();
    void autoScale();
    void updateCursors();
    void exportCsv();
    const SCOPE_HISTORY& history() const;
    void triggerDragged();
    std::size_t selected() const;
    double origin() const;
    SCOPE_CHANNEL_STYLE& style( std::size_t );
    SCOPE_PREFERENCES& m_preferences;
    SCOPE_ACQUISITION m_acquisition;
    std::vector<MEASUREMENT_CHANNEL> m_channels;
    std::vector<SCOPE_STATISTICS> m_statistics;
    std::shared_ptr<const SCOPE_HISTORY> m_history;
    std::chrono::steady_clock::time_point m_lastPresentation{}, m_lastReadings{};
    std::uint64_t m_readingsSerial = 0;
    double m_frameGap = 0;
    bool m_wasVisible = false;
    double m_maxStep = 0, m_latest = -1;
    std::size_t m_capacity = 8192;
    std::uint64_t m_serial = 0;
    wxNotebook* m_controls;
    SCOPE_PLOT* m_plot;
    CLICK_CHOICE *m_timebase, *m_scale;
    CLICK_CHOICE *m_mode, *m_source, *m_edge, *m_channel, *m_cursorMode, *m_cursorSource;
    wxTextCtrl *m_level, *m_hysteresis, *m_holdoff, *m_pretrigger, *m_delay, *m_center;
    wxTextCtrl *m_cursorA, *m_cursorB, *m_levelA, *m_levelB;
    wxCheckBox *m_visible, *m_automatic, *m_inverted;
    wxToggleButton* m_hold;
    wxStaticText *m_status, *m_info, *m_cursorInfo, *m_cursorSummary;
    wxListCtrl* m_readings;
};
}
