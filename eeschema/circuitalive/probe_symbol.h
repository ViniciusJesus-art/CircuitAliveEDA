/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sch_symbol.h>

namespace CIRCUITALIVE
{
inline SCH_SYMBOL* ProbeSymbolForItem( EDA_ITEM* aItem )
{
    if( aItem && ( aItem->Type() == SCH_FIELD_T || aItem->Type() == SCH_PIN_T ) )
        aItem = aItem->GetParent();
    if( aItem && aItem->Type() == SCH_SYMBOL_T )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( aItem );
        if( symbol->GetField( "CA.Probe" ) )
            return symbol;
    }
    return nullptr;
}
}
