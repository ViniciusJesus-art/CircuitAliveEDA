# Vendored MIT QuickJS-NG 0.17.0. Only the interpreter core is linked; no libc/OS module.
add_library( circuitalive_quickjs STATIC quickjs.c dtoa.c libregexp.c libunicode.c )
target_compile_features( circuitalive_quickjs PRIVATE c_std_11 )
target_include_directories( circuitalive_quickjs SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )
target_compile_definitions( circuitalive_quickjs PRIVATE QUICKJS_NG_BUILD _GNU_SOURCE
    __STDC_NO_ATOMICS__=1 )
set_target_properties( circuitalive_quickjs PROPERTIES POSITION_INDEPENDENT_CODE ON )
if( WIN32 )
    target_compile_definitions( circuitalive_quickjs PRIVATE WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS )
    if( MSVC )
        # The isolated synchronous API exposes no SharedArrayBuffer/Atomics. Disable
        # that upstream feature above so no experimental MSVC C11 atomics are needed.
        target_compile_options( circuitalive_quickjs PRIVATE /W0 )
    endif()
else()
    target_link_libraries( circuitalive_quickjs PUBLIC m )
endif()
