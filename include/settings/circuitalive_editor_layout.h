// CircuitAliveEDA default editor layouts. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CIRCUITALIVE_EDITOR_LAYOUT_H
#define CIRCUITALIVE_EDITOR_LAYOUT_H

#include <settings/app_settings.h>
#include <nlohmann/json.hpp>
#include <wx/translation.h>

namespace CIRCUITALIVE
{
/**
 * Seed only layout settings for editors without a saved AUI layout. Embedded defaults
 * also work in installed packages without a launcher or a personal configuration file.
 * Window positions, screen dimensions, project paths and simulation preferences are
 * deliberately excluded. Later user layouts (legacy or JSON) always take precedence.
 */
inline void ApplyDefaultEditorLayout( APP_SETTINGS_BASE& aSettings, bool aSchematic )
{
    if( !aSettings.m_Window.perspective.IsEmpty() || !aSettings.m_Window.aui_state.empty() )
        return;

    static const nlohmann::json layouts = nlohmann::json::parse( R"layout(
{
  "eeschema": {
    "window": {
      "perspective": "layout2|name=MsgPanel;caption=;state=131452;dir=3;layer=6;row=0;pos=0;prop=100000;bestw=400;besth=38;minw=-1;minh=-1;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|name=SchematicHierarchy;caption=Schematic Hierarchy;state=2099150;dir=4;layer=0;row=0;pos=1;prop=100000;bestw=240;besth=200;minw=120;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=200;floath=200|name=PropertiesManager;caption=Properties;state=2099150;dir=4;layer=0;row=0;pos=1;prop=29320;bestw=300;besth=200;minw=240;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=300;floath=400|name=SelectionFilter;caption=Selection Filter;state=2098639;dir=4;layer=3;row=0;pos=0;prop=0;bestw=180;besth=-1;minw=180;minh=125;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=400;floath=250|name=DesignBlocks;caption=Design Blocks;state=2099150;dir=2;layer=3;row=0;pos=0;prop=100000;bestw=300;besth=200;minw=240;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=800;floath=600|name=RemoteSymbol;caption=Remote Symbols;state=2099150;dir=2;layer=3;row=0;pos=3;prop=100000;bestw=300;besth=200;minw=240;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=800;floath=600|name=NetNavigator;caption=Net Navigator;state=2099150;dir=4;layer=3;row=0;pos=0;prop=100000;bestw=200;besth=200;minw=120;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=200;floath=200|name=DrawFrame;caption=;state=1020;dir=5;layer=0;row=0;pos=0;prop=100000;bestw=-1;besth=-1;minw=-1;minh=-1;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|name=Search;caption=Search;state=2098686;dir=3;layer=0;row=0;pos=0;prop=100000;bestw=180;besth=100;minw=180;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=480;floath=200|name=UsedComponents;caption=Used Components;state=2099196;dir=4;layer=0;row=0;pos=0;prop=170680;bestw=300;besth=560;minw=240;minh=400;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=340;floath=560|name=CircuitAliveInstruments;caption=Simulation;state=1998;dir=2;layer=1;row=0;pos=0;prop=100000;bestw=479;besth=941;minw=300;minh=220;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=900;floath=750|dock_size(4,0,0)=299|"
    },
    "aui": {
      "show_properties": false,
      "show_search": false,
      "design_blocks_show": false,
      "show_schematic_hierarchy": false,
      "show_net_nav_panel": false,
      "remote_symbol_show": false,
      "schematic_hierarchy_float": false,
      "float_net_nav_panel": false
    },
    "appearance": {
      "used_components_preview_height": 144,
      "show_used_components": true,
      "simulation_pane": {
        "best_size": {
          "height": 650,
          "width": 479
        },
        "caption": "",
        "dock_direction": 2,
        "dock_layer": 1,
        "dock_pos": 0,
        "dock_proportion": 100000,
        "dock_row": 0,
        "floating_pos": {
          "x": -1,
          "y": -1
        },
        "floating_size": {
          "height": 750,
          "width": 900
        },
        "max_size": {
          "height": -1,
          "width": -1
        },
        "min_size": {
          "height": 220,
          "width": 300
        },
        "name": "CircuitAliveInstruments",
        "state": 1996
      }
    }
  },
  "pcbnew": {
    "window": {
      "perspective": "layout2|name=MsgPanel;caption=;state=131452;dir=3;layer=6;row=0;pos=0;prop=100000;bestw=400;besth=38;minw=-1;minh=-1;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|name=LayersManager;caption=Appearance;state=1532;dir=2;layer=4;row=0;pos=0;prop=100000;bestw=220;besth=-1;minw=200;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=220;floath=480|name=SelectionFilter;caption=Selection Filter;state=1532;dir=2;layer=4;row=0;pos=1;prop=0;bestw=229;besth=-1;minw=229;minh=171;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=229;floath=171|name=DesignBlocks;caption=Design Blocks;state=2099150;dir=2;layer=5;row=0;pos=0;prop=100000;bestw=300;besth=200;minw=240;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=800;floath=600|name=PropertiesManager;caption=Properties;state=2098686;dir=4;layer=5;row=0;pos=0;prop=100000;bestw=300;besth=200;minw=240;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=300;floath=200|name=DrawFrame;caption=;state=1020;dir=5;layer=0;row=0;pos=0;prop=100000;bestw=-1;besth=-1;minw=-1;minh=-1;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|name=NetInspector;caption=Net Inspector;state=2098686;dir=3;layer=0;row=0;pos=0;prop=100000;bestw=300;besth=200;minw=240;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=300;floath=200|name=Search;caption=Search;state=2098686;dir=3;layer=0;row=0;pos=0;prop=100000;bestw=180;besth=100;minw=180;minh=60;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=480;floath=200|name=PendingFootprints;caption=Pending Footprints;state=2099196;dir=4;layer=1;row=0;pos=0;prop=100000;bestw=300;besth=560;minw=240;minh=300;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=340;floath=560|dock_size(2,4,0)=229|dock_size(4,1,0)=302|"
    },
    "aui": {
      "show_properties": false,
      "show_search": false,
      "design_blocks_show": false,
      "right_panel_width": 229,
      "show_layer_manager": true,
      "show_net_inspector": false
    },
    "circuitalive": {
      "pending_pane": "name=PendingFootprints;caption=Pending Footprints;state=2099196;dir=4;layer=1;row=0;pos=0;prop=100000;bestw=300;besth=560;minw=240;minh=300;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=340;floath=560",
      "pending_sash": 200,
      "pending_visible": true
    }
  }
}
)layout" );
    const auto& defaults = layouts.at( aSchematic ? "eeschema" : "pcbnew" );

    for( const auto& [section, values] : defaults.items() )
    {
        for( const auto& [key, value] : values.items() )
        {
            const std::string path = section + "." + key;
            // Perspective may have been stored as an empty string; seed it as well.
            if( path == "window.perspective" )
            {
                wxString perspective = wxString::FromUTF8( value.get<std::string>() );
                for( const wxString& caption : { wxString( "Schematic Hierarchy" ),
                        wxString( "Properties" ), wxString( "Selection Filter" ),
                        wxString( "Design Blocks" ), wxString( "Remote Symbols" ),
                        wxString( "Net Navigator" ), wxString( "Search" ),
                        wxString( "Used Components" ), wxString( "Simulation" ),
                        wxString( "Appearance" ), wxString( "Net Inspector" ),
                        wxString( "Pending Footprints" ) } )
                {
                    perspective.Replace( ";caption=" + caption + ";",
                                         ";caption=" + wxGetTranslation( caption ) + ";" );
                }
                aSettings.Set<wxString>( path, perspective );
            }
            else if( !aSettings.Contains( path ) )
            {
                aSettings.Set<nlohmann::json>( path, value );
            }
        }
    }

    aSettings.Load();
}
}

#endif
