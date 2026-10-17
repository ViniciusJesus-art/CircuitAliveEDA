/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "../memory_backend.h"
#include <string.h>
static CA_MEMORY_BACKEND backend;
void CircuitAliveMemorySetBackend(const CA_MEMORY_BACKEND* b) { backend = *b; }
typedef struct { uint64_t token; int powered; Digital_t output[32]; } MemoryState;
void cm_ca_memory(ARGS)
{
    unsigned i, address=0, data=0, known=0; int addressKnown=1;
    const unsigned slot=PARAM(slot);
    MemoryState *state;
    if(INIT) {
        cm_event_alloc(0,sizeof(MemoryState));
        state=cm_event_get_ptr(0,0); memset(state,0,sizeof(*state));
        for(i=0;i<32;++i) { state->output[i].state=UNKNOWN; state->output[i].strength=HI_IMPEDANCE; }
        for(i=0;i<PORT_SIZE(address);++i) LOAD(address[i])=0;
        for(i=0;i<PORT_SIZE(data_in);++i) LOAD(data_in[i])=0;
        LOAD(write)=LOAD(select)=LOAD(power)=LOAD(ready)=0;
    } else {
        state=cm_event_get_ptr(0,0);
        *state=*(MemoryState*)cm_event_get_ptr(0,1);
    }
    if(!backend.read || backend.version!=1) return;
    for(i=0;i<PORT_SIZE(address);++i) {
        const int bit=INPUT_STATE(address[i]);
        if(bit==ONE) address|=1u<<i; else if(bit!=ZERO) addressKnown=0;
    }
    const int powered=INPUT_STATE(power)==ONE;
    const int selected=INPUT_STATE(select)==ONE;
    const int ready=INPUT_STATE(ready)==ONE;
    if(!powered && state->powered) {
        state->token=backend.write(backend.user,slot,state->token,TIME,0,0,0,1);
        state->powered=0;
    }
    if(powered) state->powered=1;
    if(powered && ready && selected && INPUT_STATE(write)==ONE) {
        for(i=0;i<PORT_SIZE(data_in);++i) {
            const int bit=INPUT_STATE(data_in[i]);
            if(bit==ONE) data|=1u<<i;
            if(bit==ONE || bit==ZERO) known|=1u<<i;
        }
        state->token=backend.write(backend.user,slot,state->token,TIME,address,data,known,!addressKnown);
    }
    data=0; known=0;
    if(powered && ready && selected && addressKnown)
        data=backend.read(backend.user,slot,state->token,address,&known);
    for(i=0;i<PORT_SIZE(data_out);++i) {
        const int strength=(powered && ready && selected) ? STRONG : HI_IMPEDANCE;
        const int value=(known&(1u<<i)) ? ((data&(1u<<i)) ? ONE : ZERO) : UNKNOWN;
        OUTPUT_CHANGED(data_out[i])=INIT || value!=state->output[i].state || strength!=state->output[i].strength;
        OUTPUT_STATE(data_out[i])=value; OUTPUT_STRENGTH(data_out[i])=strength;
        OUTPUT_DELAY(data_out[i])=PARAM(read_delay);
        state->output[i].state=value; state->output[i].strength=strength;
    }
    backend.mark(backend.user,slot,state->token,TIME);
}
