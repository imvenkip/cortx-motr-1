/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 11/13/2012
 */

#pragma once

#ifndef __MERO_NET_LNET_LNET_ADDB_H__
#define __MERO_NET_LNET_LNET_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup LNetDFS
   @{
 */

/*
 ******************************************************************************
 * LNet ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_NET_LNET_MOD = 40,
	M0_ADDB_CTXID_NET_LNET_DOM = 41,
	M0_ADDB_CTXID_NET_LNET_TM  = 42,
};

M0_ADDB_CT(m0_addb_ct_net_lnet_mod, M0_ADDB_CTXID_NET_LNET_MOD);
M0_ADDB_CT(m0_addb_ct_net_lnet_dom, M0_ADDB_CTXID_NET_LNET_DOM);
M0_ADDB_CT(m0_addb_ct_net_lnet_tm,  M0_ADDB_CTXID_NET_LNET_TM);

/*
 ******************************************************************************
 * LNet ADDB posting locations.
 ******************************************************************************
 */
enum {
	/* common, non-xo */
	M0_NET_LNET_ADDB_LOC_C_BUF_REG   = 10,
	M0_NET_LNET_ADDB_LOC_C_DOM_INIT  = 20,
	M0_NET_LNET_ADDB_LOC_C_EP_CREATE = 30,
	M0_NET_LNET_ADDB_LOC_C_EV_WORKER = 40,
	M0_NET_LNET_ADDB_LOC_C_TM_INIT   = 50,
	/* kernel, kcore */
	M0_NET_LNET_ADDB_LOC_KC_BUF_ARECV = 1010,
	M0_NET_LNET_ADDB_LOC_KC_BUF_ASEND = 1020,
	M0_NET_LNET_ADDB_LOC_KC_BUF_MRECV = 1030,
	M0_NET_LNET_ADDB_LOC_KC_BUF_MSEND = 1040,
	M0_NET_LNET_ADDB_LOC_KC_BUF_PRECV = 1050,
	M0_NET_LNET_ADDB_LOC_KC_BUF_PSEND = 1060,
	M0_NET_LNET_ADDB_LOC_KC_TM_START  = 1070,
	/* kernel, driver */
	M0_NET_LNET_ADDB_LOC_KD_BEV_BLESS = 2010,
	M0_NET_LNET_ADDB_LOC_KD_BUF_REG   = 2020,
	M0_NET_LNET_ADDB_LOC_KD_BUF_REG1  = 2030,
	M0_NET_LNET_ADDB_LOC_KD_BUF_REG2  = 2040,
	M0_NET_LNET_ADDB_LOC_KD_BUF_REG3  = 2050,
	M0_NET_LNET_ADDB_LOC_KD_DOM_INIT  = 2060,
	M0_NET_LNET_ADDB_LOC_KD_FINI      = 2070,
	M0_NET_LNET_ADDB_LOC_KD_INIT      = 2080,
	M0_NET_LNET_ADDB_LOC_KD_IOCTL     = 2090,
	M0_NET_LNET_ADDB_LOC_KD_NID_GET   = 2100,
	M0_NET_LNET_ADDB_LOC_KD_OPEN      = 2110,
	M0_NET_LNET_ADDB_LOC_KD_TM_START  = 2120,
	/* kernel */
	M0_NET_LNET_ADDB_LOC_K_BEV_BLESS    = 3010,
	M0_NET_LNET_ADDB_LOC_K_BUF_REG      = 3020,
	M0_NET_LNET_ADDB_LOC_K_BUF_KLA2KIOV = 3030,
	M0_NET_LNET_ADDB_LOC_K_BUF_UVA2KIOV = 3040,
	M0_NET_LNET_ADDB_LOC_K_DOM_INIT     = 3050,
	M0_NET_LNET_ADDB_LOC_K_INIT_1       = 3060,
	M0_NET_LNET_ADDB_LOC_K_INIT_2       = 3061,
	M0_NET_LNET_ADDB_LOC_K_MD_UNLINK    = 3070,
	M0_NET_LNET_ADDB_LOC_K_SEG_UVA2KIOV = 3080,
	M0_NET_LNET_ADDB_LOC_K_TM_START     = 3090,
	/* user space */
	M0_NET_LNET_ADDB_LOC_U_BEV_BLESS   = 4010,
	M0_NET_LNET_ADDB_LOC_U_BUF_ARECV   = 4020,
	M0_NET_LNET_ADDB_LOC_U_BUF_ASEND   = 4030,
	M0_NET_LNET_ADDB_LOC_U_BUF_DEL     = 4040,
	M0_NET_LNET_ADDB_LOC_U_BUF_EV_WAIT = 4050,
	M0_NET_LNET_ADDB_LOC_U_BUF_MRECV   = 4060,
	M0_NET_LNET_ADDB_LOC_U_BUF_MSEND   = 4070,
	M0_NET_LNET_ADDB_LOC_U_BUF_PRECV   = 4080,
	M0_NET_LNET_ADDB_LOC_U_BUF_PSEND   = 4090,
	M0_NET_LNET_ADDB_LOC_U_BUF_REG     = 4100,
	M0_NET_LNET_ADDB_LOC_U_DOM_INIT    = 4210,
	M0_NET_LNET_ADDB_LOC_U_NID_DECODE  = 4320,
	M0_NET_LNET_ADDB_LOC_U_NID_GET1    = 4430,
	M0_NET_LNET_ADDB_LOC_U_NID_GET2    = 4540,
	M0_NET_LNET_ADDB_LOC_U_NID_ENCODE  = 4650,
	M0_NET_LNET_ADDB_LOC_U_TM_START    = 4760,
	/* xo */
	M0_NET_LNET_ADDB_LOC_X_BEV_DELIVER = 5010,
	M0_NET_LNET_ADDB_LOC_X_BUF_ADD1    = 5020,
	M0_NET_LNET_ADDB_LOC_X_BUF_ADD2    = 5030,
	M0_NET_LNET_ADDB_LOC_X_EP_CREATE   = 5040,
	M0_NET_LNET_ADDB_LOC_X_NBD_ALLOC   = 5050,
	M0_NET_LNET_ADDB_LOC_X_NBD_RECOVER = 5060,
	M0_NET_LNET_ADDB_LOC_X_TM_CONFINE  = 5070,
	M0_NET_LNET_ADDB_LOC_X_TM_START    = 5080,
};

/** @} */ /* end of networking group */

#endif /* __MERO_NET_LNET_LNET_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

