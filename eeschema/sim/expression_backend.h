/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
typedef struct CA_EXPRESSION_BACKEND
{
    unsigned version;
    void* user;
    int (*evaluate)(void*,unsigned,const double*,unsigned,double,double*,const double**);
} CA_EXPRESSION_BACKEND;
