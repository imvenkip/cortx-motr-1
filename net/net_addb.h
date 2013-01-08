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
 * Original creation date: 11/09/2012
 */

#pragma once

#ifndef __MERO_NET_NET_ADDB_H__
#define __MERO_NET_NET_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup net
   @{
 */

/*
 ******************************************************************************
 * Network ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_NET_MOD = 20,
	M0_ADDB_CTXID_NET_DOM = 21,
	M0_ADDB_CTXID_NET_TM  = 22,
	M0_ADDB_CTXID_NET_BP  = 23,
};

M0_ADDB_CT(m0_addb_ct_net_mod, M0_ADDB_CTXID_NET_MOD);
M0_ADDB_CT(m0_addb_ct_net_dom, M0_ADDB_CTXID_NET_DOM);
M0_ADDB_CT(m0_addb_ct_net_tm,  M0_ADDB_CTXID_NET_TM);
M0_ADDB_CT(m0_addb_ct_net_bp,  M0_ADDB_CTXID_NET_BP);

/*
 ******************************************************************************
 * Network ADDB record identifiers.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	/** See m0_net_transfer_machine::ntm_cntr_msg */
	M0_ADDB_RECID_NET_AGGR_MSG  = 20,
	/** See m0_net_transfer_machine::ntm_cntr_data */
	M0_ADDB_RECID_NET_AGGR_DATA = 21,
	/** See m0_net_transfer_machine::ntm_cntr_rb */
	M0_ADDB_RECID_NET_RECV_BUF  = 22,
	/* Message queue statistical records */
	M0_ADDB_RECID_NET_MQ_R      = 23,
	M0_ADDB_RECID_NET_MQ_S      = 24,
	M0_ADDB_RECID_NET_PQ_R      = 25,
	M0_ADDB_RECID_NET_PQ_S      = 26,
	M0_ADDB_RECID_NET_AQ_R      = 27,
	M0_ADDB_RECID_NET_AQ_S      = 28,
};

#undef KB
#define KB(d) (d) << 10
M0_ADDB_RT_CNTR(m0_addb_rt_net_aggr_msg,  M0_ADDB_RECID_NET_AGGR_MSG,
		KB(5), KB(10), KB(20), KB(50), KB(75),
		KB(100), KB(125), KB(150), KB(200));
M0_ADDB_RT_CNTR(m0_addb_rt_net_aggr_data, M0_ADDB_RECID_NET_AGGR_DATA,
		KB(100), KB(250), KB(500), KB(750));
M0_ADDB_RT_CNTR(m0_addb_rt_net_recv_buf,  M0_ADDB_RECID_NET_RECV_BUF,
		1, 2, 3, 4, 5, 6, 7);
#undef KB

/* Queue statistics data point record types */
#undef RT_QDP
#define RT_QDP(n, id)							\
M0_ADDB_RT_DP(m0_addb_rt_net_##n, M0_ADDB_RECID_NET_##id,		\
	      "num_adds", "num_dels", "num_s_events", "num_f_events",	\
	      "time_in_queue", "total_bytes", "max_bytes")
RT_QDP(mq_r, MQ_R);
RT_QDP(mq_s, MQ_S);
RT_QDP(pq_r, PQ_R);
RT_QDP(pq_s, PQ_S);
RT_QDP(aq_r, AQ_R);
RT_QDP(aq_s, AQ_S);
#undef RT_QDP

/*
 ******************************************************************************
 * Network ADDB posting locations.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_NET_ADDB_LOC_BP_INIT            = 10,
	M0_NET_ADDB_LOC_BUF_REG            = 20,
	M0_NET_ADDB_LOC_BUF_ADD            = 30,
	M0_NET_ADDB_LOC_BUF_EVENT_DEL_SYNC = 40,
	M0_NET_ADDB_LOC_DESC_COPY          = 50,
	M0_NET_ADDB_LOC_DOM_INIT           = 60,
	M0_NET_ADDB_LOC_EP_CREATE          = 70,
	M0_NET_ADDB_LOC_TM_INIT            = 80,
	M0_NET_ADDB_LOC_TM_START           = 90,
	M0_NET_ADDB_LOC_TM_STOP            = 100,
	M0_NET_ADDB_LOC_TM_CONFINE         = 110,
};

/** @} */ /* end of networking group */

#endif /* __MERO_NET_NET_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

