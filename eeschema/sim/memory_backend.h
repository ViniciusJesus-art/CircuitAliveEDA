/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    // Private, versioned bridge between our worker and its bundled XSPICE module.
    typedef struct CA_MEMORY_BACKEND
    {
        unsigned version;
        void* user;
        uint32_t ( *read )( void*, unsigned, uint64_t, unsigned, uint32_t* );
        uint64_t ( *write )( void*, unsigned, uint64_t, double, unsigned, uint32_t, uint32_t, int );
        void ( *mark )( void*, unsigned, uint64_t, double );
    } CA_MEMORY_BACKEND;
#ifdef __cplusplus
}
#endif
