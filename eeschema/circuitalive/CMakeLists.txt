# All CircuitAlive editor code is assembled here, keeping the upstream build hook small.
if( TARGET circuitalive_sim_session )
    add_library( circuitalive_interactive STATIC circuit_snapshot.cpp session_runner.cpp )
    target_include_directories( circuitalive_interactive PUBLIC ${PROJECT_SOURCE_DIR}/eeschema )
    target_link_libraries( circuitalive_interactive PUBLIC circuitalive_sim_session )
    target_sources( eeschema_kiface_objects PRIVATE
        editor_simulation.cpp schematic_adapter.cpp simulation_overlay.cpp )
    target_link_libraries( eeschema_kiface_objects PUBLIC circuitalive_interactive )
    add_dependencies( eeschema circuitalive-sim-worker )
endif()
