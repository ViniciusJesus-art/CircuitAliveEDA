# All CircuitAlive editor code is assembled here, keeping the upstream build hook small.
if( TARGET circuitalive_sim_session )
    add_library( circuitalive_interactive STATIC
        circuit_snapshot.cpp circuit_topology.cpp simulation_model.cpp model_profiles.cpp behavioral_model.cpp
        session_runner.cpp measurements.cpp scope_acquisition.cpp logic_signals.cpp power_rails.cpp model_design.cpp model_design_presets.cpp )
    target_compile_definitions( circuitalive_interactive PRIVATE
        CIRCUITALIVE_BUNDLED_MODEL_DIR="${PROJECT_SOURCE_DIR}/simulation/models"
        CIRCUITALIVE_BUNDLED_BLOCK_DIR="${PROJECT_SOURCE_DIR}/simulation/blocks" )
    target_include_directories( circuitalive_interactive PUBLIC ${PROJECT_SOURCE_DIR}/eeschema )
    target_include_directories( circuitalive_interactive SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
    target_include_directories( eeschema_kiface_objects SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
    target_link_libraries( circuitalive_interactive PUBLIC circuitalive_sim_session )
    target_sources( eeschema_kiface_objects PRIVATE
        editor_simulation.cpp schematic_adapter.cpp simulation_overlay.cpp simulation_toolbar.cpp model_dialog.cpp
        instrument_panel.cpp scope_panel.cpp probe_placement.cpp model_author.cpp )
    target_link_libraries( eeschema_kiface_objects PUBLIC circuitalive_interactive )
    add_dependencies( eeschema circuitalive-sim-worker )
    install( DIRECTORY ${PROJECT_SOURCE_DIR}/simulation/models/
             DESTINATION share/circuitalive/models FILES_MATCHING PATTERN "*.casim" )
    install( DIRECTORY ${PROJECT_SOURCE_DIR}/simulation/blocks/
             DESTINATION share/circuitalive/blocks FILES_MATCHING PATTERN "*.casim" )
endif()
