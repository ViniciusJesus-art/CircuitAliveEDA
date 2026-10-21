/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ngspice/cm.h"
#include "ngspice/cktdefs.h"
#include "ngspice/mifdefs.h"
#include "ngspice/sperror.h"
#include "../expression_backend.h"
#include <math.h>
static CA_EXPRESSION_BACKEND backend;
#ifdef _WIN32
__declspec(dllexport)
#endif
void CircuitAliveExpressionSetBackend(const CA_EXPRESSION_BACKEND* value) { backend=*value; }
void cm_ca_expression(Mif_Private_t* m)
{
    double inputs[64],value=0;const double* partials=0;
    const unsigned count=m->conn[0]->size, slot=m->param[0]->element[0].ivalue;
    unsigned i;
    if(count>64)return;
    for(i=0;i<count;++i)inputs[i]=m->conn[0]->port[i]->input.rvalue;
    if(backend.version!=1 || !backend.evaluate
        || !backend.evaluate(backend.user,slot,inputs,count,m->circuit.time,&value,&partials)) {
        m->conn[1]->port[0]->output.rvalue=NAN;return;
    }
    m->conn[1]->port[0]->output.rvalue=value;
    for(i=0;i<count;++i) {
        if(m->circuit.anal_type==MIF_AC) {
            m->conn[1]->port[0]->ac_gain[0].port[i].real=partials[i];
            m->conn[1]->port[0]->ac_gain[0].port[i].imag=0;
        } else m->conn[1]->port[0]->partial[0].port[i]=partials[i];
    }
}

/* Stateless sources use the solver's current Newton iterate directly. This
 * avoids the generic mixed-signal port predictor/limiter, matching B sources. */
int cm_ca_expression_load(GENmodel* models,CKTcircuit* ckt)
{
    MIFmodel* model=(MIFmodel*)models;MIFinstance* here;
    for(;model;model=MIFnextModel(model))for(here=MIFinstances(model);here;here=MIFnextInstance(here)) {
        double inputs[64],value=0,rhs;const double* partials=0;unsigned i;
        const unsigned count=here->conn[0]->size,slot=here->param[0]->element[0].ivalue;
        Mif_Port_Data_t* out=here->conn[1]->port[0];
        Mif_Smp_Ptr_t* output=&out->smp_data;
        const int voltage=out->type==MIF_DIFF_VOLTAGE;
        const double factor=ckt->CKTmode&MODETRANOP?ckt->CKTsrcFact:1;
        if(count>64 || !backend.evaluate)return E_BADPARM;
        for(i=0;i<count;++i) {
            const Mif_Smp_Ptr_t* input=&here->conn[0]->port[i]->smp_data;
            inputs[i]=ckt->CKTrhsOld[input->pos_node]-ckt->CKTrhsOld[input->neg_node];
        }
        if(!backend.evaluate(backend.user,slot,inputs,count,ckt->CKTtime,&value,&partials))return E_BADPARM;
        out->output.rvalue=value;rhs=value;
        if(voltage) {
            *output->pos_branch+=1;*output->neg_branch-=1;
            *output->branch_pos+=1;*output->branch_neg-=1;
        }
        for(i=0;i<count;++i) {
            const double derivative=partials[i]*factor;
            rhs-=partials[i]*inputs[i];out->partial[0].port[i]=partials[i];
            if(voltage) {
                Mif_E_Ptr_t* p=&output->input[0].port[i].e;
                *p->branch_poscntl-=derivative;*p->branch_negcntl+=derivative;
            } else {
                Mif_G_Ptr_t* p=&output->input[0].port[i].g;
                *p->pos_poscntl+=derivative;*p->pos_negcntl-=derivative;
                *p->neg_poscntl-=derivative;*p->neg_negcntl+=derivative;
            }
        }
        if(voltage)ckt->CKTrhs[output->branch]+=factor*rhs;
        else {ckt->CKTrhs[output->pos_node]-=factor*rhs;ckt->CKTrhs[output->neg_node]+=factor*rhs;}
        here->initialized=MIF_TRUE;
    }
    return OK;
}
int cm_ca_expression_converged(GENmodel* models,CKTcircuit* ckt)
{
    MIFmodel* model=(MIFmodel*)models;MIFinstance* here;
    for(;model;model=MIFnextModel(model))for(here=MIFinstances(model);here;here=MIFnextInstance(here)) {
        double inputs[64],value=0,previous,tolerance;const double* partials=0;unsigned i;
        const unsigned count=here->conn[0]->size,slot=here->param[0]->element[0].ivalue;
        Mif_Port_Data_t* out=here->conn[1]->port[0];
        if(count>64 || !backend.evaluate)return E_BADPARM;
        for(i=0;i<count;++i) {
            const Mif_Smp_Ptr_t* input=&here->conn[0]->port[i]->smp_data;
            inputs[i]=ckt->CKTrhs[input->pos_node]-ckt->CKTrhs[input->neg_node];
        }
        if(!backend.evaluate(backend.user,slot,inputs,count,ckt->CKTtime,&value,&partials))return E_BADPARM;
        previous=out->output.rvalue;
        tolerance=ckt->CKTreltol*fmax(fabs(value),fabs(previous))
            +(out->type==MIF_DIFF_VOLTAGE?ckt->CKTvoltTol:ckt->CKTabstol);
        if(fabs(value-previous)>tolerance){++ckt->CKTnoncon;ckt->CKTtroubleElt=(GENinstance*)here;return OK;}
    }
    return OK;
}
