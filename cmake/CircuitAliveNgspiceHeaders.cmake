# Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later

function( circuitalive_ngspice_msvc_includes include_dir output_var )
    if( NOT EXISTS "${include_dir}/ngspice/dllitf.h" OR
        NOT EXISTS "${include_dir}/ngspice/config.h" )
        message( FATAL_ERROR "MSVC requires the matching internal ngspice headers from vcpkg" )
    endif()

    set( includes "${include_dir}" )
    if( NOT EXISTS "${include_dir}/maths/dense/dense.h" )
        # The pinned port copies src/include but omits src/maths. Keep its
        # config.h/ABI headers, including RFSPICE, CIDER and KLU, unchanged.
        file( READ "${include_dir}/ngspice/config.h" config )
        if( NOT config MATCHES "#[ \t]*define[ \t]+VERSION[ \t]+\"46\"" )
            message( FATAL_ERROR "Revalidate the dense.h supplement for this ngspice version" )
        endif()

        set( dense "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../thirdparty/ngspice-xspice/maths/dense/dense.h" )
        file( READ "${dense}" contents )
        string( REPLACE "\r\n" "\n" contents "${contents}" )
        string( SHA256 checksum "${contents}" )
        # Identical to ngspice commit ebdaf58ec76a06ffaac7e0f138360dd1cf5ee4b6.
        if( NOT checksum STREQUAL "4c56586a20128f36967441abe428e4611fb29a7c7a1cc6e5399a5e10b35e2a29" )
            message( FATAL_ERROR "The ngspice 46 dense.h supplement has changed; verify its ABI" )
        endif()

        set( support "${CMAKE_CURRENT_BINARY_DIR}/ngspice-cm-support" )
        file( MAKE_DIRECTORY "${support}/ngspice" "${support}/maths/dense" )
        configure_file( "${dense}" "${support}/maths/dense/dense.h" COPYONLY )
        # cktdefs.h includes ../maths/dense/dense.h. This fallback resolves
        # that relative path without exposing the bundled ngspice 47 headers.
        list( APPEND includes "${support}/ngspice" )
    endif()
    set( ${output_var} "${includes}" PARENT_SCOPE )
endfunction()
