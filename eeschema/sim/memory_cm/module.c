/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "ngspice/config.h"
#include "ngspice/devdefs.h"
extern SPICEdev cm_ca_memory_info;
static SPICEdev* devices[]={&cm_ca_memory_info};
static int count=1,zero=0;
static void* core;
void* CMdevs(void){return devices;}
void* CMdevNum(void){return &count;}
void* CMudns(void){return 0;}
void* CMudnNum(void){return &zero;}
void* CMgetCoreItfPtr(void){return &core;}
