# All CircuitAlive editor code is assembled here, keeping the upstream build hook small.
if( TARGET circuitalive_sim_session )
    set( CIRCUITALIVE_INTERACTIVE_SOURCES
        circuit_snapshot.cpp circuit_topology.cpp model_catalog_index.cpp simulation_model.cpp model_profiles.cpp behavioral_model.cpp led_appearance.cpp control_references.cpp
        session_runner.cpp simulation_preferences.cpp visual_signals.cpp measurements.cpp scope_history.cpp scope_waveform.cpp scope_acquisition.cpp logic_signals.cpp power_rails.cpp model_design.cpp model_routing.cpp model_design_presets.cpp )
    function( circuitalive_add_interactive_library target session_target )
        add_library( ${target} STATIC ${CIRCUITALIVE_INTERACTIVE_SOURCES} )
        target_compile_definitions( ${target} PRIVATE
            CIRCUITALIVE_BUNDLED_MODEL_DIR="${PROJECT_SOURCE_DIR}/simulation/models"
            CIRCUITALIVE_BUNDLED_BLOCK_DIR="${PROJECT_SOURCE_DIR}/simulation/blocks" )
        target_include_directories( ${target} PUBLIC ${PROJECT_SOURCE_DIR}/eeschema )
        target_include_directories( ${target} SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
        target_link_libraries( ${target} PUBLIC ${session_target} )
    endfunction()
    circuitalive_add_interactive_library( circuitalive_interactive circuitalive_sim_session )
    set( CIRCUITALIVE_DESKTOP_LIBRARY circuitalive_interactive )
    if( MSVC )
        circuitalive_add_interactive_library( circuitalive_interactive_gui circuitalive_sim_session_gui )
        target_include_directories( circuitalive_interactive_gui PRIVATE ${PROJECT_SOURCE_DIR}/include )
        target_compile_options( circuitalive_interactive_gui PRIVATE /FIjson_common.h )
        target_link_libraries( circuitalive_interactive_gui PRIVATE kicommon )
        set( CIRCUITALIVE_DESKTOP_LIBRARY circuitalive_interactive_gui )
    endif()
    target_include_directories( eeschema_kiface_objects SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
    target_sources( eeschema_kiface_objects PRIVATE
        editor_simulation.cpp schematic_adapter.cpp simulation_overlay.cpp simulation_toolbar.cpp model_dialog.cpp resource_dialog.cpp memory_viewer.cpp model_browser.cpp live_control_popup.cpp
        instrument_panel.cpp scope_panel.cpp scope_plot.cpp probe_placement.cpp model_author.cpp model_design_canvas.cpp panel_led_appearance.cpp panel_model_interactions.cpp panel_simulation_preferences.cpp )
    target_link_libraries( eeschema_kiface_objects PUBLIC ${CIRCUITALIVE_DESKTOP_LIBRARY} )
    add_dependencies( eeschema circuitalive-sim-worker )
    install( DIRECTORY ${PROJECT_SOURCE_DIR}/simulation/models/
             DESTINATION share/circuitalive/models FILES_MATCHING PATTERN "*.casim" )
    install( DIRECTORY ${PROJECT_SOURCE_DIR}/simulation/blocks/
             DESTINATION share/circuitalive/blocks FILES_MATCHING PATTERN "*.casim" )
    install( DIRECTORY ${PROJECT_SOURCE_DIR}/simulation/runtime/
             DESTINATION share/circuitalive/runtime FILES_MATCHING PATTERN "*.js" PATTERN "*.md" )
    install( FILES ${PROJECT_SOURCE_DIR}/thirdparty/quickjs-ng/LICENSE
             DESTINATION share/circuitalive/licenses/quickjs-ng )
    install( FILES ${PROJECT_SOURCE_DIR}/thirdparty/avr8js/LICENSE
             DESTINATION share/circuitalive/licenses/avr8js )
endif()
