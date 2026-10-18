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
TOOL_ACTION SIMULATION_ACTIONS::memory( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.viewMemory" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "View memory…" ) )
        .Tooltip( wxTRANSLATE( "Inspect or export the initial image or paused memory contents" ) ) );
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
TOOL_ACTION SIMULATION_ACTIONS::probes( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.probes" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Measurement probes" ) )
        .Tooltip( wxTRANSLATE( "Show or hide probe tools; hiding returns to the cursor" ) ).Icon( BITMAPS::sim_run ) );
TOOL_ACTION SIMULATION_ACTIONS::logicLevels( TOOL_ACTION_ARGS().Name( "circuitalive.Interactive.logicLevels" )
        .Scope( AS_GLOBAL ).FriendlyName( wxTRANSLATE( "Pin logic levels" ) )
        .Tooltip( wxTRANSLATE( "Show pin levels: 1 red, 0 blue, Z gray, X yellow, ~ purple for fast switching; configure cutoffs in Real-time Simulation preferences" ) ).Icon( BITMAPS::sim_run ) );

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
    for( const auto* action : { &SIMULATION_ACTIONS::play, &SIMULATION_ACTIONS::pause,
                               &SIMULATION_ACTIONS::stop, &SIMULATION_ACTIONS::logicLevels } )
    {
        if( !toolbar->FindTool( action->GetUIId() ) )
        {
            toolbar->Add( *action, action == &SIMULATION_ACTIONS::logicLevels );
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
    static std::map<int, wxBitmapBundle> logicIcons, probeIcons;
    auto setIcon = [&]( ACTION_TOOLBAR* bar, const TOOL_ACTION& action,
                        std::map<int, wxBitmapBundle>& icons, const char* svg )
    {
        auto [entry, fresh] = icons.try_emplace( size );
        if( fresh ) entry->second = wxBitmapBundle::FromSVG( svg, wxSize( size, size ) );
        auto* tool = bar->FindTool( action.GetUIId() );
        if( tool && !tool->GetBitmapBundle().IsSameAs( entry->second ) ) bar->SetToolBitmap( action, entry->second );
    };
    setIcon( toolbar, SIMULATION_ACTIONS::logicLevels, logicIcons,
        "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'><circle cx='6' cy='6' r='4' fill='#eb4034'/>"
        "<circle cx='18' cy='6' r='4' fill='#3286ed'/><path d='M2 20h5v-7h10v7h5' fill='none' stroke='#92999e' stroke-width='2'/></svg>" );
    auto* right = dynamic_cast<ACTION_TOOLBAR*>( manager->GetPane( "RightToolbar" ).window );
    if( right )
    {
        if( !right->FindTool( SIMULATION_ACTIONS::probes.GetUIId() ) )
        {
            right->AddScaledSeparator( &aFrame ); right->Add( SIMULATION_ACTIONS::probes, true );
            right->KiRealize(); added = true;
        }
        setIcon( right, SIMULATION_ACTIONS::probes, probeIcons,
            "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'><path d='M3 21L20 4M9 4h11v11' "
            "fill='none' stroke='#318fd8' stroke-width='2'/></svg>" );
    }
    if( added )
    {
        toolbar->KiRealize();
        aFrame.OnToolbarSizeChanged();
    }
}
}
