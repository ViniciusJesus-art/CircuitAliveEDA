/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
typedef struct CA_PROGRAMMABLE_PIN_BACKEND
{
    unsigned version;
    void* user;
    int (*drive)(void*, unsigned, double);
} CA_PROGRAMMABLE_PIN_BACKEND;
