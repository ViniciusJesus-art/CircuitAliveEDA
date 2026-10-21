/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Structures for model: ca_expression
 *
 * Interface table following the ngspice cmpp layout, with native loaders.
 */


#include "ngspice/ngspice.h"
#include <stdio.h>
#include "ngspice/devdefs.h"
#include "ngspice/ifsim.h"
#include "ngspice/mifdefs.h"
#include "ngspice/mifproto.h"
#include "ngspice/mifparse.h"


static IFparm MIFmPTable[] = { IOP("slot",0,IF_INTEGER,"Compiled expression") };
static Mif_Port_Type_t inputTypes[]={MIF_VOLTAGE};
static char* inputNames[]={"v"};
static Mif_Port_Type_t outputTypes[]={MIF_DIFF_VOLTAGE,MIF_DIFF_CURRENT};
static char* outputNames[]={"vd","id"};
static Mif_Conn_Info_t MIFconnTable[]={
 {"inputs","Expression node voltages",MIF_IN,MIF_VOLTAGE,"v",1,inputTypes,inputNames,MIF_TRUE,MIF_TRUE,1,MIF_TRUE,64,MIF_FALSE},
 {"output","Expression source",MIF_OUT,MIF_DIFF_VOLTAGE,"vd",2,outputTypes,outputNames,MIF_FALSE,MIF_FALSE,0,MIF_FALSE,0,MIF_FALSE}
};
static union Mif_Parse_Value slot_default[]={ {.ivalue=0} };
static Mif_Param_Info_t MIFparamTable[]={
 {"slot","Compiled expression",MIF_INTEGER,1,slot_default,MIF_TRUE,{.ivalue=0},MIF_TRUE,{.ivalue=4095},MIF_FALSE,MIF_FALSE,0,MIF_FALSE,0,MIF_FALSE,0,MIF_TRUE}
};
extern void cm_ca_expression(Mif_Private_t *);
extern int cm_ca_expression_load(GENmodel*,CKTcircuit*);
extern int cm_ca_expression_converged(GENmodel*,CKTcircuit*);

static int val_terms             = 0;
static int val_numNames          = 0;
static int val_numInstanceParms  = 0;
static int val_numModelParms     = 1;
static int val_sizeofMIFinstance = sizeof(MIFinstance);
static int val_sizeofMIFmodel    = sizeof(MIFmodel);

SPICEdev cm_ca_expression_info = {
    .DEVpublic = {
        .name = "ca_expression",
        .description = "CircuitAlive compiled expression",
        .terms = &val_terms,
        .numNames = &val_numNames,
        .termNames = NULL,
        .numInstanceParms = &val_numInstanceParms,
        .instanceParms = NULL,
        .numModelParms = &val_numModelParms,
        .modelParms = MIFmPTable,
        .flags = 0,

        .cm_func = cm_ca_expression,
        .num_conn = 2,
        .conn = MIFconnTable,
        .num_param = 1,
        .param = MIFparamTable,
        .num_inst_var = 0,
        .inst_var = NULL,
    },

    .DEVparam = NULL,
    .DEVmodParam = MIFmParam,
    .DEVload = cm_ca_expression_load,
    .DEVsetup = MIFsetup,
    .DEVunsetup = MIFunsetup,
    .DEVpzSetup = NULL,
    .DEVtemperature = NULL,
    .DEVtrunc = MIFtrunc,
    .DEVfindBranch = NULL,
    .DEVacLoad = MIFload,
    .DEVaccept = NULL,
    .DEVdestroy = NULL,
    .DEVmodDelete = MIFmDelete,
    .DEVdelete = MIFdelete,
    .DEVsetic = NULL,
    .DEVask = MIFask,
    .DEVmodAsk = MIFmAsk,
    .DEVpzLoad = NULL,
    .DEVconvTest = cm_ca_expression_converged,
    .DEVsenSetup = NULL,
    .DEVsenLoad = NULL,
    .DEVsenUpdate = NULL,
    .DEVsenAcLoad = NULL,
    .DEVsenPrint = NULL,
    .DEVsenTrunc = NULL,
    .DEVdisto = NULL,
#ifdef CA_NGSPICE_HAS_NOISE
    .DEVnoise = MIFnoise,
#else
    .DEVnoise = NULL,
#endif
    .DEVsoaCheck = NULL,
    .DEVinstSize = &val_sizeofMIFinstance,
    .DEVmodSize = &val_sizeofMIFmodel,

#ifdef CIDER
    .DEVdump = NULL,
    .DEVacct = NULL,
#endif

#ifdef KLU
    .DEVbindCSC = MIFbindCSC,
    .DEVbindCSCComplex = MIFbindCSCComplex,
    .DEVbindCSCComplexToReal = MIFbindCSCComplexToReal,
#endif
};

