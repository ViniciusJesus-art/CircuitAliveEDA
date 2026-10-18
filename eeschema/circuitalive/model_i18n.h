/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <wx/intl.h>
#include <wx/tokenzr.h>
#include "simulation_model.h"
#include "diagnostic_strings.h"

namespace CIRCUITALIVE
{
// Translate presentation only. Model IDs, formulas, values and saved JSON retain
// their original spelling, including when the user edits a model in another language.
inline wxString ModelText( const std::string& text )
{
    return wxGetTranslation( wxString::FromUTF8( text ) );
}

inline wxString ModelSummaryText( const std::string& text )
{
    wxString result;
    wxStringTokenizer lines( wxString::FromUTF8( text ), "\n", wxTOKEN_RET_EMPTY_ALL );
    while( lines.HasMoreTokens() )
    {
        result += wxGetTranslation( lines.GetNextToken() );
        if( lines.HasMoreTokens() ) result += '\n';
    }
    return result;
}

inline wxString DiagnosticLine( const wxString& text )
{
    const auto translated = wxGetTranslation(text);
    if( translated != text ) return translated;
    for( const auto* fragment : DIAGNOSTIC_STRINGS )
    {
        const auto source = wxString::FromUTF8(fragment);
        const auto target = wxGetTranslation(source);
        if( target == source ) continue;
        if( text.StartsWith(source) ) return target + DiagnosticLine(text.Mid(source.length()));
        if( text.EndsWith(source) ) return DiagnosticLine(text.Left(text.length()-source.length())) + target;
        if( source.StartsWith(": ") )
        {
            const auto at = text.find(source);
            if( at != wxString::npos )
                return text.Left(at) + target + text.Mid(at+source.length());
        }
    }
    // Component references and file/parameter identifiers remain verbatim.
    const auto separator = text.find(": ");
    if( separator != wxString::npos )
        return DiagnosticLine(text.Left(separator)) + ": " + DiagnosticLine(text.Mid(separator+2));
    return text;
}

inline wxString DiagnosticText( const std::string& text )
{
    wxString result;
    wxStringTokenizer lines(wxString::FromUTF8(text), "\n", wxTOKEN_RET_EMPTY_ALL);
    while( lines.HasMoreTokens() )
    {
        result += DiagnosticLine(lines.GetNextToken());
        if( lines.HasMoreTokens() ) result += '\n';
    }
    return result;
}

inline std::vector<SIMULATION_MODEL> ModelsForDisplay( std::vector<SIMULATION_MODEL> models )
{
    for( auto& model : models )
    {
        // Searching still accepts the original English terminology and part IDs.
        model.symbolNames.push_back(model.name + " " + model.description);
        model.name = ModelText( model.name ).ToStdString(wxConvUTF8);
        model.description = ModelText( model.description ).ToStdString(wxConvUTF8);
        model.family = ModelText( model.family ).ToStdString(wxConvUTF8);
        wxString category;
        wxStringTokenizer parts( wxString::FromUTF8( model.category ), "/" );
        while( parts.HasMoreTokens() )
        {
            if( !category.empty() ) category += '/';
            category += wxGetTranslation( parts.GetNextToken() );
        }
        model.category = category.ToStdString(wxConvUTF8);
    }
    return models;
}
}
