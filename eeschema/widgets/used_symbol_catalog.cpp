/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "used_symbols.h"
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <sch_symbol.h>
#include <algorithm>
#include <map>

namespace CIRCUITALIVE
{
std::vector<USED_SYMBOL_ENTRY> BuildUsedSymbolList( SCH_SHEET_PATH& aSheet )
{
    std::vector<USED_SYMBOL_ENTRY> entries;
    if( !aSheet.LastScreen() )
        return entries;
    std::map<wxString, SCH_SYMBOL*> symbols;
    for( SCH_ITEM* item : aSheet.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        auto* symbol = static_cast<SCH_SYMBOL*>( item );
        if( symbol->IsPower() || symbol->GetField( "CA.Probe" ) )
            continue;
        // A library symbol is one component type, irrespective of values, models, units or
        // other instance properties. Keep a stable representative for copying its settings.
        const auto [at, inserted] = symbols.emplace( symbol->GetLibId().Format(), symbol );
        if( !inserted && ( symbol->GetUnit() < at->second->GetUnit()
            || ( symbol->GetUnit() == at->second->GetUnit()
                 && symbol->GetRef( &aSheet ) < at->second->GetRef( &aSheet ) ) ) )
            at->second = symbol;
    }
    for( const auto& [key, symbol] : symbols )
        entries.push_back( { key, key, symbol->m_Uuid } );
    std::stable_sort( entries.begin(), entries.end(), []( const auto& a, const auto& b )
    {
        const int comparison = a.label.CmpNoCase( b.label );
        return comparison == 0 ? a.key < b.key : comparison < 0;
    } );
    return entries;
}

std::unique_ptr<SCH_SYMBOL> CopyUsedSymbol( const SCH_SYMBOL& aSource,
                                           SCH_SHEET_PATH& aSheet, const VECTOR2I& aPosition )
{
    auto copy = std::unique_ptr<SCH_SYMBOL>(
            static_cast<SCH_SYMBOL*>( aSource.Duplicate( IGNORE_PARENT_GROUP ) ) );
    copy->SetParentGroup( nullptr );
    copy->ClearFlags();
    const auto instances = copy->GetInstances();
    for( const auto& instance : instances )
        copy->RemoveInstance( instance.m_Path );
    SCH_SYMBOL_INSTANCE instance;
    if( aSource.GetInstance( instance, aSheet.Path() ) )
    {
        // Preserve variant overrides from this occurrence, without inheriting other sheets'
        // references or unit selections.
        instance.m_Path = aSheet.Path();
        copy->AddHierarchicalReference( instance );
    }
    copy->ClearAnnotation( nullptr, true );
    copy->SetUnitSelection( &aSheet, aSource.GetUnit() );
    copy->Move( aPosition - copy->GetPosition() );
    return copy;
}
}
