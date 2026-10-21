/* Finite GPIO conductances without auxiliary control voltage sources.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "ngspice/cm.h"
#include "ngspice/cktdefs.h"
#include "ngspice/mifdefs.h"
#include "ngspice/sperror.h"
#include "../programmable_pin_backend.h"
static CA_PROGRAMMABLE_PIN_BACKEND backend;
#ifdef _WIN32
__declspec(dllexport)
#endif
void CircuitAliveProgrammablePinSetBackend(const CA_PROGRAMMABLE_PIN_BACKEND* value) { backend=*value; }
void cm_ca_programmable_pin(Mif_Private_t* m)
{
    const unsigned slot=m->param[0]->element[0].ivalue;
    const int mode=backend.version==1 && backend.drive ? backend.drive(backend.user,slot,m->circuit.time) : 0;
    const double low=mode==1 ? 1.0/25 : 1e-12;
    const double high=mode==2 ? 1.0/25 : mode==3 ? 1.0/30000 : 1e-12;
    m->conn[0]->port[0]->output.rvalue=low*m->conn[0]->port[0]->input.rvalue;
    m->conn[1]->port[0]->output.rvalue=high*m->conn[1]->port[0]->input.rvalue;
    m->conn[0]->port[0]->partial[0].port[0]=low;
    m->conn[0]->port[0]->partial[1].port[0]=0;
    m->conn[1]->port[0]->partial[0].port[0]=0;
    m->conn[1]->port[0]->partial[1].port[0]=high;
}

int cm_ca_programmable_pin_load(GENmodel* models,CKTcircuit* ckt)
{
    MIFmodel* model=(MIFmodel*)models;MIFinstance* here;
    for(;model;model=MIFnextModel(model))for(here=MIFinstances(model);here;here=MIFnextInstance(here)) {
        const unsigned slot=here->param[0]->element[0].ivalue;
        const int mode=backend.version==1 && backend.drive?backend.drive(backend.user,slot,ckt->CKTtime):0;
        const double conductances[2]={mode==1?1.0/25:1e-12,mode==2?1.0/25:mode==3?1.0/30000:1e-12};
        unsigned rail;
        for(rail=0;rail<2;++rail) {
            Mif_Port_Data_t* port=here->conn[rail]->port[0];
            Mif_G_Ptr_t* p=&port->smp_data.input[rail].port[0].g;
            const double g=conductances[rail];
            *p->pos_poscntl+=g;*p->pos_negcntl-=g;*p->neg_poscntl-=g;*p->neg_negcntl+=g;
            port->output.rvalue=g*(ckt->CKTrhsOld[port->smp_data.pos_node]-ckt->CKTrhsOld[port->smp_data.neg_node]);
        }
        here->initialized=MIF_TRUE;
    }
    return OK;
}
