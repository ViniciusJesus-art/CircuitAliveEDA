/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation_model.h"
#include <string>
#include <vector>

namespace CIRCUITALIVE
{
struct MODEL_CATALOG_RECORD
{
    std::size_t model;
    std::string name, category, family, group, libraries, search;
};

// Display/search index only. Electrical association continues to come from .casim.
class MODEL_CATALOG_INDEX
{
public:
    explicit MODEL_CATALOG_INDEX( const std::vector<SIMULATION_MODEL>& aModels );
    const std::vector<MODEL_CATALOG_RECORD>& Records() const { return m_records; }
    std::vector<std::size_t> Find( const std::string& aQuery, const std::string& aGroup = {} ) const;
private:
    std::vector<MODEL_CATALOG_RECORD> m_records;
};
}
