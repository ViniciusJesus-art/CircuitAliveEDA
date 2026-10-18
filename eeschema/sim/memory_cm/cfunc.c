#include "ngspice/cm.h"
extern void cm_ca_memory(Mif_Private_t *);
/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "../memory_backend.h"
#include <string.h>
static CA_MEMORY_BACKEND backend;
#ifdef _WIN32
__declspec(dllexport)
#endif
void CircuitAliveMemorySetBackend(const CA_MEMORY_BACKEND* b) { backend = *b; }
typedef struct { uint64_t token; int powered; Digital_t output[32]; } MemoryState;
void cm_ca_memory(Mif_Private_t *mif_private)
{
    unsigned i, address=0, data=0, known=0; int addressKnown=1;
    const unsigned slot=mif_private->param[0]->element[0].ivalue;
    MemoryState *state;
    if(mif_private->circuit.init) {
        cm_event_alloc(0,sizeof(MemoryState));
        state=cm_event_get_ptr(0,0); memset(state,0,sizeof(*state));
        for(i=0;i<32;++i) { state->output[i].state=UNKNOWN; state->output[i].strength=HI_IMPEDANCE; }
        for(i=0;i<mif_private->conn[0]->size;++i) mif_private->conn[0]->port[i]->load=0;
        for(i=0;i<mif_private->conn[1]->size;++i) mif_private->conn[1]->port[i]->load=0;
        mif_private->conn[3]->port[0]->load=mif_private->conn[4]->port[0]->load=mif_private->conn[5]->port[0]->load=mif_private->conn[6]->port[0]->load=0;
    } else {
        state=cm_event_get_ptr(0,0);
        *state=*(MemoryState*)cm_event_get_ptr(0,1);
    }
    if(!backend.read || backend.version!=1) return;
    for(i=0;i<mif_private->conn[0]->size;++i) {
        const int bit=((Digital_t*)(mif_private->conn[0]->port[i]->input.pvalue))->state;
        if(bit==ONE) address|=1u<<i; else if(bit!=ZERO) addressKnown=0;
    }
    const int powered=((Digital_t*)(mif_private->conn[5]->port[0]->input.pvalue))->state==ONE;
    const int selected=((Digital_t*)(mif_private->conn[4]->port[0]->input.pvalue))->state==ONE;
    const int ready=((Digital_t*)(mif_private->conn[6]->port[0]->input.pvalue))->state==ONE;
    if(!powered && state->powered) {
        state->token=backend.write(backend.user,slot,state->token,mif_private->circuit.time,0,0,0,1);
        state->powered=0;
    }
    if(powered) state->powered=1;
    if(powered && ready && selected && ((Digital_t*)(mif_private->conn[3]->port[0]->input.pvalue))->state==ONE) {
        for(i=0;i<mif_private->conn[1]->size;++i) {
            const int bit=((Digital_t*)(mif_private->conn[1]->port[i]->input.pvalue))->state;
            if(bit==ONE) data|=1u<<i;
            if(bit==ONE || bit==ZERO) known|=1u<<i;
        }
        state->token=backend.write(backend.user,slot,state->token,mif_private->circuit.time,address,data,known,!addressKnown);
    }
    data=0; known=0;
    if(powered && ready && selected && addressKnown)
        data=backend.read(backend.user,slot,state->token,address,&known);
    for(i=0;i<mif_private->conn[2]->size;++i) {
        const int strength=(powered && ready && selected) ? STRONG : HI_IMPEDANCE;
        const int value=(known&(1u<<i)) ? ((data&(1u<<i)) ? ONE : ZERO) : UNKNOWN;
        mif_private->conn[2]->port[i]->changed=mif_private->circuit.init || value!=state->output[i].state || strength!=state->output[i].strength;
        ((Digital_t*)(mif_private->conn[2]->port[i]->output.pvalue))->state=value; ((Digital_t*)(mif_private->conn[2]->port[i]->output.pvalue))->strength=strength;
        mif_private->conn[2]->port[i]->delay=mif_private->param[1]->element[0].rvalue;
        state->output[i].state=value; state->output[i].strength=strength;
    }
    backend.mark(backend.user,slot,state->token,mif_private->circuit.time);
}
