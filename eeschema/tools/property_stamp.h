/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <wx/string.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class SCH_ITEM;
class SCH_EDIT_FRAME;
class SCH_SHEET_PATH;
class SCH_COMMIT;
class PROPERTY_BASE;
class TOOL_MANAGER;

namespace CIRCUITALIVE
{
struct PROPERTY_STAMP_SETTINGS
{
    wxString property = "Value";
    wxString customField;
    wxString pattern = "Digital";
    bool numbering = false;
    std::int64_t start = 1;
    std::int64_t step = 1;
    bool createFields = true;
    bool wireLabels = true;
    int labelKind = 0; // Local, global, hierarchical.
};

class PROPERTY_STAMP_SEQUENCE
{
public:
    explicit PROPERTY_STAMP_SEQUENCE( const PROPERTY_STAMP_SETTINGS& aSettings );
    wxString Current() const;
    // False means that the last representable value has been used; never wrap around.
    bool Advance();
    static bool HasPlaceholder( const wxString& aPattern );
private:
    PROPERTY_STAMP_SETTINGS m_settings;
    std::int64_t m_number;
    bool m_exhausted = false;
};

struct PROPERTY_STAMP_EDIT
{
    SCH_ITEM* item = nullptr;
    SCH_ITEM* owner = nullptr;
    PROPERTY_BASE* property = nullptr;
    std::shared_ptr<bool> undoable = std::make_shared<bool>( true );
    bool syncUnits = true;
    std::function<bool( SCH_COMMIT* )> apply;
};

// Validate and prepare without changing the item. The caller owns the undo transaction.
PROPERTY_STAMP_EDIT PlanPropertyStamp( SCH_ITEM& aItem, SCH_SHEET_PATH& aSheet,
    const PROPERTY_STAMP_SETTINGS& aSettings, const wxString& aValue,
    SCH_EDIT_FRAME* aFrame = nullptr );
void RegisterPropertyStamp( TOOL_MANAGER* aManager );
}
