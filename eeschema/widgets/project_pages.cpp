/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "project_pages.h"
#include <circuitalive/editor_simulation.h>
#include <connection_graph.h>
#include <project.h>
#include <project/project_file.h>
#include <sch_commit.h>
#include <sch_edit_frame.h>
#include <sch_draw_panel.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <schematic.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace CIRCUITALIVE
{
namespace
{
wxFileName absolutePage( PROJECT& project, const wxString& name )
{
    wxFileName file( name );
    file.MakeAbsolute( project.GetProjectPath() );
    return file;
}

std::vector<wxString> excludedPages( PROJECT& project )
{
    std::vector<wxString> result;
    const auto json = project.GetProjectFile().Get<nlohmann::json>( "schematic.excluded_pages" );
    if( json && json->is_array() )
    {
        for( const auto& name : *json )
            if( name.is_string() )
                result.push_back( wxString::FromUTF8( name.get<std::string>() ) );
    }
    return result;
}

void storeExclusions( PROJECT& project, const std::vector<wxString>& names )
{
    auto json = nlohmann::json::array();
    for( const auto& name : names )
    {
        auto file = absolutePage( project, name );
        file.MakeRelativeTo( project.GetProjectPath() );
        json.push_back( file.GetFullPath( wxPATH_UNIX ).ToStdString( wxConvUTF8 ) );
    }
    project.GetProjectFile().Set<nlohmann::json>( "schematic.excluded_pages", json );
}
}

bool IsExcludedProjectPage( PROJECT& project, const wxString& name )
{
    const auto selected = absolutePage( project, name );
    for( const auto& excluded : excludedPages( project ) )
        if( absolutePage( project, excluded ).SameAs( selected ) )
            return true;
    return false;
}

void IncludeProjectPage( PROJECT& project, const wxString& name )
{
    auto excluded = excludedPages( project );
    const auto selected = absolutePage( project, name );
    const auto count = excluded.size();
    std::erase_if( excluded, [&]( const wxString& file )
    {
        return absolutePage( project, file ).SameAs( selected );
    } );
    if( count != excluded.size() )
        storeExclusions( project, excluded );
}

bool CanDeleteProjectPage( const SCHEMATIC& schematic, const SCH_SHEET_PATH& page )
{
    return !page.empty() && schematic.Hierarchy().HasPath( page.Path() )
            && ( page.size() > 1 || schematic.GetTopLevelSheets().size() > 1 );
}

bool RemoveProjectPage( SCHEMATIC& schematic, const SCH_SHEET_PATH& page )
{
    if( !CanDeleteProjectPage( schematic, page ) )
        return false;
    const SCH_SHEET_PATH removedPath = page;
    SCH_SHEET* removed = removedPath.Last();
    std::vector<wxString> before;
    for( const auto& path : schematic.Hierarchy() )
        before.push_back( path.LastScreen()->GetFileName() );

    SCH_SHEET_PATH fallback = removedPath;
    fallback.pop_back();
    if( fallback.empty() )
    {
        for( const auto& path : schematic.Hierarchy() )
            if( path.at( 0 ) != removed ) { fallback = path; break; }
    }
    schematic.ConnectionGraph()->Reset();
    // Retarget before releasing any screens, including a displayed descendant.
    schematic.SetCurrentSheet( fallback );
    if( removedPath.size() == 1 )
    {
        if( !schematic.RemoveTopLevelSheet( removed ) )
            return false;
    }
    else
    {
        if( !fallback.LastScreen()->Remove( removed ) )
            return false;
        fallback.LastScreen()->SetContentModified();
        schematic.RefreshHierarchy();
    }
    std::unique_ptr<SCH_SHEET> detached( removed );
    schematic.GetCurrentScreen()->SetContentModified();

    auto& project = schematic.Project();
    auto excluded = excludedPages( project );
    for( const auto& file : before )
    {
        bool inUse = false;
        for( const auto& path : schematic.Hierarchy() )
            if( absolutePage( project, file ).SameAs( wxFileName( path.LastScreen()->GetFileName() ) ) )
                inUse = true;
        const bool alreadyExcluded = std::any_of( excluded.begin(), excluded.end(),
                [&]( const wxString& entry )
                { return absolutePage( project, entry ).SameAs( absolutePage( project, file ) ); } );
        if( !inUse && !file.IsEmpty() && !alreadyExcluded )
            excluded.push_back( file );
    }
    storeExclusions( project, excluded );
    auto& manifest = project.GetProjectFile().GetTopLevelSheets();
    manifest.clear();
    for( const auto* sheet : schematic.GetTopLevelSheets() )
    {
        auto file = absolutePage( project, sheet->GetScreen()->GetFileName() );
        file.MakeRelativeTo( project.GetProjectPath() );
        manifest.emplace_back( sheet->m_Uuid, sheet->GetName(), file.GetFullPath( wxPATH_UNIX ) );
    }
    return true;
}

void DeleteCurrentPage( SCH_EDIT_FRAME& frame )
{
    const SCH_SHEET_PATH page = frame.GetCurrentSheet();
    if( !CanDeleteProjectPage( frame.Schematic(), page ) )
        return;
    wxMessageDialog dialog( &frame, wxString::Format(
            _( "Delete page '%s' from this project?\n\nIts child pages will also be removed. "
               "The schematic files will be kept on disk. Save the project to keep this change." ),
            page.Last()->GetShownName( false ) ), _( "Delete Page" ),
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING );
    dialog.SetYesNoLabels( _( "Delete Page" ), _( "Cancel" ) );
    if( dialog.ShowModal() != wxID_YES )
        return;
    frame.GetToolManager()->RunAction( ACTIONS::cancelInteractive );
    InvalidateEditorSimulation( &frame );
    SCH_SHEET_PATH survivor = page;
    survivor.pop_back();
    if( survivor.empty() )
        for( const auto& candidate : frame.Schematic().Hierarchy() )
            if( candidate.at( 0 ) != page.at( 0 ) ) { survivor = candidate; break; }
    frame.GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &survivor );
    // A parent screen can itself display the sheet symbol that is about to be released.
    frame.GetCanvas()->GetView()->Clear();
    frame.ClearUndoRedoList();
    frame.ClearRepeatItemsList();
    if( RemoveProjectPage( frame.Schematic(), page ) )
    {
        SCH_COMMIT rebuild( &frame );
        frame.RecalculateConnections( &rebuild, NO_CLEANUP );
        frame.UpdateHierarchyNavigator();
        frame.DisplayCurrentSheet();
        frame.OnModify();
    }
}
}
