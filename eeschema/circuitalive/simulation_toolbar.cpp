/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "simulation_toolbar.h"
#include <bitmaps.h>
#include <tool/tool_event.h>
#include <kiplatform/ui.h>
#include <pgm_base.h>
#include <sch_edit_frame.h>
#include <settings/common_settings.h>
#include <tool/action_toolbar.h>
#include <map>

namespace CIRCUITALIVE
{
TOOL_ACTION SIMULATION_ACTIONS::configure( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.configureModel" )
        .Scope( AS_GLOBAL ).DefaultHotkey( MD_CTRL + MD_ALT + 'M' )
        .FriendlyName( wxTRANSLATE( "Simulation model..." ) )
        .Tooltip( wxTRANSLATE( "Associate a model file, map physical pins and configure parameters" ) ) );
TOOL_ACTION SIMULATION_ACTIONS::play( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.play" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Interactive Play" ) )
        .Tooltip( wxTRANSLATE( "Start or resume interactive simulation" ) ).Icon( BITMAPS::sim_run ) );
TOOL_ACTION SIMULATION_ACTIONS::pause( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.pause" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Interactive Pause" ) )
        .Tooltip( wxTRANSLATE( "Pause and preserve the circuit state" ) ).Icon( BITMAPS::sim_stop ) );
TOOL_ACTION SIMULATION_ACTIONS::stop( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.stop" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Interactive Stop" ) )
        .Tooltip( wxTRANSLATE( "Stop and reset interactive simulation" ) ).Icon( BITMAPS::sim_stop ) );

void EnsureSimulationToolbar( SCH_EDIT_FRAME& aFrame )
{
    auto* manager = wxAuiManager::GetManager( &aFrame );
    if( !manager ) return;
    auto* toolbar = dynamic_cast<ACTION_TOOLBAR*>( manager->GetPane( "TopMainToolbar" ).window );
    if( !toolbar ) return;
    bool added = false;
    if( !toolbar->FindTool( SIMULATION_ACTIONS::play.GetUIId() )
        && !toolbar->FindTool( SIMULATION_ACTIONS::pause.GetUIId() )
        && !toolbar->FindTool( SIMULATION_ACTIONS::stop.GetUIId() ) )
        toolbar->AddScaledSeparator( &aFrame );
    for( const auto* action : { &SIMULATION_ACTIONS::play, &SIMULATION_ACTIONS::pause, &SIMULATION_ACTIONS::stop } )
    {
        if( !toolbar->FindTool( action->GetUIId() ) )
        {
            toolbar->Add( *action );
            added = true;
        }
    }

    // KiCad has native Play/Stop vectors. Keep Pause local instead of extending the global bitmap enum.
    const int size = Pgm().GetCommonSettings()->m_Appearance.toolbar_icon_size;
    const auto key = std::make_pair( size, KIPLATFORM::UI::IsDarkTheme() );
    static std::map<std::pair<int, bool>, wxBitmapBundle> pauseIcons;
    auto [icon, inserted] = pauseIcons.try_emplace( key );
    if( inserted )
        icon->second = wxBitmapBundle::FromSVG(
                "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'>"
                "<path fill='#1a81c4' d='M4 3h6v18H4zM14 3h6v18h-6z'/></svg>", wxSize( size, size ) );
    auto* item = toolbar->FindTool( SIMULATION_ACTIONS::pause.GetUIId() );
    if( item && !item->GetBitmapBundle().IsSameAs( icon->second ) )
        toolbar->SetToolBitmap( SIMULATION_ACTIONS::pause, icon->second );
    if( added )
    {
        toolbar->KiRealize();
        aFrame.OnToolbarSizeChanged();
    }
}
}
