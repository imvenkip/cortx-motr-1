/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 07/20/2012
 */
#pragma once
#ifndef __MERO_RM_UT_RMUT_H__
#define __MERO_RM_UT_RMUT_H__

#include "rings.h"
#include "lib/types.h"            /* uint64_t */
#include "lib/chan.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/ut.h"
#include "db/db.h"
#include "cob/cob.h"
#include "net/lnet/lnet.h"
#include "net/buffer_pool.h"
#include "mdstore/mdstore.h"
#include "reqh/reqh.h"
#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"
#include "rpc/rpc_machine.h"
#include "fop/fom_generic.h"

enum obj_type {
	OBJ_DOMAIN = 1,
	OBJ_RES_TYPE,
	OBJ_RES,
	OBJ_OWNER
};

enum rm_server {
	SERVER_1 = 0,
	SERVER_2,
	SERVER_3,
	SERVER_NR,
	SERVER_INVALID,
};

enum rr_tests {
	TEST1 = 0,
	TEST2,
	TEST3,
	TEST4,
	TEST_NR,
};

/*
 * Resource manager class-collection.
 */
struct rm_ut_data {
	struct m0_rm_domain	   rd_dom;
	struct m0_rm_resource_type rd_rt;
	struct m0_rings		   rd_res;
	struct m0_rm_owner	   rd_owner;
	struct m0_rm_incoming	   rd_in;
	struct m0_rm_credit	   rd_credit;
};

/*
 * RM server context. It lives inside a thread in this test.
 */
struct rm_context {
	enum rm_server		  rc_id;
	const char		 *rc_ep_addr;
	struct m0_thread	  rc_thr;
	struct m0_chan		  rc_chan;
	struct m0_clink		  rc_clink;
	struct m0_rpc_machine	  rc_rpc;
	struct m0_dbenv		  rc_dbenv;
	struct m0_fol		  rc_fol;
	struct m0_cob_domain_id	  rc_cob_id;
	struct m0_mdstore	  rc_mdstore;
	struct m0_cob_domain	  rc_cob_dom;
	struct m0_net_domain	  rc_net_dom;
	struct m0_net_buffer_pool rc_bufpool;
	struct m0_net_xprt	 *rc_xprt;
	struct m0_reqh		  rc_reqh;
	struct m0_net_end_point	 *rc_ep[SERVER_NR];
	struct m0_rpc_conn	  rc_conn[SERVER_NR];
	struct m0_rpc_session	  rc_sess[SERVER_NR];
	struct rm_ut_data	  rc_test_data;
	enum rm_server		  creditor_id;
	enum rm_server		  debtor_id;
};

/*
 * Test variable(s)
 */
M0_EXTERN struct rm_ut_data test_data;

void rm_utdata_init(struct rm_ut_data *data, enum obj_type type);
void rm_utdata_fini(struct rm_ut_data *data, enum obj_type type);
void rm_test_owner_capital_raise(struct m0_rm_owner *owner,
				 struct m0_rm_credit *credit);

/* __MERO_RM_UT_RMUT_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
