# CircuitAliveEDA stage-1 prototype. Process transport currently targets Linux.
if( CMAKE_SYSTEM_NAME STREQUAL "Linux" )
    find_package( Threads REQUIRED )
    add_library( circuitalive_sim_session STATIC interactive_sim_session.cpp )
    target_include_directories( circuitalive_sim_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )
    target_include_directories( circuitalive_sim_session SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
    target_link_libraries( circuitalive_sim_session PRIVATE Threads::Threads )

    add_executable( circuitalive-sim-worker interactive_sim_worker.cpp )
    target_include_directories( circuitalive-sim-worker SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json ${NGSPICE_INCLUDE_DIR} )
    target_link_libraries( circuitalive-sim-worker PRIVATE ${NGSPICE_LIBRARY} Threads::Threads )
    install( TARGETS circuitalive-sim-worker RUNTIME DESTINATION ${KICAD_BIN} )
endif()
