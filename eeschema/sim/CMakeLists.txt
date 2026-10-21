# Isolated solver process on Linux and Windows.
if( CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 )
    find_package( Threads REQUIRED )
    add_subdirectory( ${PROJECT_SOURCE_DIR}/thirdparty/quickjs-ng
        ${CMAKE_CURRENT_BINARY_DIR}/quickjs-ng )
    add_library( circuitalive_programmable_runtime STATIC programmable_runtime.cpp avr_accelerator.cpp compiled_expression.cpp programmable_pipeline.cpp )
    set_target_properties( circuitalive_programmable_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON )
    target_include_directories( circuitalive_programmable_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )
    target_include_directories( circuitalive_programmable_runtime SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
    target_link_libraries( circuitalive_programmable_runtime PRIVATE circuitalive_quickjs Threads::Threads )
    set( CIRCUITALIVE_SIM_SESSION_SOURCES
        interactive_sim_session.cpp memory_image.cpp runtime_memory.cpp platform_runtime.cpp )
    function( circuitalive_add_session_library target )
        add_library( ${target} STATIC ${CIRCUITALIVE_SIM_SESSION_SOURCES} )
        target_include_directories( ${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )
        target_include_directories( ${target} SYSTEM PRIVATE
            ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json )
        target_link_libraries( ${target} PRIVATE Threads::Threads )
        if( WIN32 )
            target_link_libraries( ${target} PRIVATE psapi )
        endif()
    endfunction()
    circuitalive_add_session_library( circuitalive_sim_session )
    if( MSVC )
        # kicommon exports nlohmann::json through JSON_COMMON_EXPORT_STUB.
        # Desktop objects must see its dllimport before instantiating JSON.
        # Keep the original library independent for the isolated worker/tests.
        circuitalive_add_session_library( circuitalive_sim_session_gui )
        target_include_directories( circuitalive_sim_session_gui PRIVATE ${PROJECT_SOURCE_DIR}/include )
        target_compile_options( circuitalive_sim_session_gui PRIVATE /FIjson_common.h )
        target_link_libraries( circuitalive_sim_session_gui PRIVATE kicommon )
    endif()

    add_library( circuitalive-memory MODULE memory_cm/ifspec.c memory_cm/cfunc.c memory_cm/module.c
        memory_cm/programmable_pin.c memory_cm/programmable_pin_ifspec.c
        memory_cm/expression.c memory_cm/expression_ifspec.c
        ${PROJECT_SOURCE_DIR}/thirdparty/ngspice-xspice/dstring.c )
    set_target_properties( circuitalive-memory PROPERTIES PREFIX "" SUFFIX ".cm" )
    set_target_properties( circuitalive-memory PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON )
    target_compile_features( circuitalive-memory PRIVATE c_std_11 )
    if( MSVC )
        # Use the port's internal headers and Visual C++ config, completing
        # the private RFSPICE header omitted by its install rules.
        # Its ngspice 46 ABI differs from the bundled ngspice 47/MSYS2 headers.
        include( "${PROJECT_SOURCE_DIR}/cmake/CircuitAliveNgspiceHeaders.cmake" )
        circuitalive_ngspice_msvc_includes( "${NGSPICE_INCLUDE_DIR}" CA_NGSPICE_CM_INCLUDE )
    else()
        set( CA_NGSPICE_CM_INCLUDE "${PROJECT_SOURCE_DIR}/thirdparty/ngspice-xspice/include" )
        include( CheckLibraryExists )
        check_library_exists( "${NGSPICE_LIBRARY}" MIFbindCSC "" CA_NGSPICE_KLU )
        check_library_exists( "${NGSPICE_LIBRARY}" NUMDinfo "" CA_NGSPICE_CIDER )
        check_library_exists( "${NGSPICE_LIBRARY}" NIpred "" CA_NGSPICE_PREDICTOR )
        if( CA_NGSPICE_KLU )
            target_compile_definitions( circuitalive-memory PRIVATE KLU )
        endif()
        if( CA_NGSPICE_CIDER )
            target_compile_definitions( circuitalive-memory PRIVATE CIDER )
        endif()
        if( CA_NGSPICE_PREDICTOR )
            target_compile_definitions( circuitalive-memory PRIVATE PREDICTOR )
        endif()
    endif()
    target_include_directories( circuitalive-memory SYSTEM PRIVATE ${CA_NGSPICE_CM_INCLUDE} )
    include( CMakePushCheckState )
    include( CheckStructHasMember )
    cmake_push_check_state( RESET )
    set( CMAKE_REQUIRED_INCLUDES "${CA_NGSPICE_CM_INCLUDE}" )
    check_struct_has_member( "struct coreInfo_t" dllitf_MIFnoise
        "ngspice/config.h;ngspice/ngspice.h;ngspice/dllitf.h" CA_NGSPICE_HAS_NOISE LANGUAGE C )
    cmake_pop_check_state()
    if( CA_NGSPICE_HAS_NOISE )
        target_compile_definitions( circuitalive-memory PRIVATE CA_NGSPICE_HAS_NOISE )
    endif()
    target_link_libraries( circuitalive-memory PRIVATE ${NGSPICE_LIBRARY} )
    install( TARGETS circuitalive-memory LIBRARY DESTINATION ${KICAD_BIN} )
    add_executable( circuitalive-sim-worker interactive_sim_worker.cpp )
    target_include_directories( circuitalive-sim-worker SYSTEM PRIVATE
        ${PROJECT_SOURCE_DIR}/thirdparty/nlohmann_json ${NGSPICE_INCLUDE_DIR} )
    target_link_libraries( circuitalive-sim-worker PRIVATE ${NGSPICE_LIBRARY} Threads::Threads circuitalive_sim_session circuitalive_programmable_runtime ${CMAKE_DL_LIBS} )
    add_dependencies( circuitalive-sim-worker circuitalive-memory )
    install( TARGETS circuitalive-sim-worker RUNTIME DESTINATION ${KICAD_BIN} )
endif()
