add_executable( circuitalive-launcher WIN32 windows-launcher.cpp )
set_target_properties( circuitalive-launcher PROPERTIES OUTPUT_NAME CircuitAliveEDA )
file( GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc"
    CONTENT "1 ICON \"${PROJECT_SOURCE_DIR}/resources/bitmaps_png/icons/icon_kicad.ico\"\n" )
target_sources( circuitalive-launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" )
install( TARGETS circuitalive-launcher RUNTIME DESTINATION ${KICAD_BIN} )
