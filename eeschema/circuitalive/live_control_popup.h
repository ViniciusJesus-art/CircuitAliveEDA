/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "circuit_snapshot.h"
#include <wx/popupwin.h>
#include <functional>
namespace CIRCUITALIVE
{
class LIVE_CONTROL_POPUP : public wxPopupTransientWindow
{
public:
    LIVE_CONTROL_POPUP( wxWindow* aParent, const std::string& aReference,
        const std::vector<CIRCUIT_ADJUSTMENT>& aControls, const std::vector<double>& aValues,
        std::size_t aComponent, std::function<void( std::size_t, double )> aChange,
        std::size_t aSelected = NO_SIGNAL );
};
}
