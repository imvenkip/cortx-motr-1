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

#include "lib/types.h"            /* uint64_t */
#include "lib/chan.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "ut/ut.h"
#include "fop/fom_generic.h"
#include "ut/ut_rpc_machine.h"

enum obj_type {
	OBJ_DOMAIN = 1,
	OBJ_RES_TYPE,
	OBJ_RES,
	OBJ_OWNER
};

/*
 * If you add another server, you will have to make changes in other places.
 */
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

/* Forward declaration */
struct rm_ut_data;

struct rm_ut_data_ops {
	void (*rtype_set)(struct rm_ut_data *self);
	void (*rtype_unset)(struct rm_ut_data *self);
	void (*resource_set)(struct rm_ut_data *self);
	void (*resource_unset)(struct rm_ut_data *self);
	void (*owner_set)(struct rm_ut_data *self);
	void (*owner_unset)(struct rm_ut_data *self);
	void (*credit_datum_set)(struct rm_ut_data *self);
};

/*
 * Resource manager class-collection.
 */
struct rm_ut_data {
	struct m0_rm_domain	     rd_dom;
	struct m0_rm_resource_type  *rd_rt;
	struct m0_rm_resource	    *rd_res;
	struct m0_rm_owner	    *rd_owner;
	struct m0_rm_incoming	     rd_in;
	struct m0_rm_credit	     rd_credit;
	const struct rm_ut_data_ops *rd_ops;
};

/*
 * RM server context. It lives inside a thread in this test.
 */
struct rm_context {
	enum rm_server             rc_id;
	struct m0_thread           rc_thr;
	struct m0_chan             rc_chan;
	struct m0_clink            rc_clink;
	struct m0_mutex            rc_mutex;
	struct m0_ut_rpc_mach_ctx  rc_rmach_ctx;
	struct m0_reqh_service    *rc_reqh_svc;
	struct m0_net_end_point   *rc_ep[SERVER_NR];
	struct m0_rpc_conn         rc_conn[SERVER_NR];
	struct m0_rpc_session      rc_sess[SERVER_NR];
	struct m0_clink           *rc_rev_sess_wait;
	struct rm_ut_data          rc_test_data;
	enum rm_server             creditor_id;
	enum rm_server             debtor_id;
};

/*
 * Test variable(s)
 */
extern struct rm_ut_data     rm_test_data;
M0_EXTERN struct rm_context  rm_ctx[];
M0_EXTERN const char        *serv_addr[];
M0_EXTERN const int          cob_ids[];
M0_EXTERN const char        *db_name[];

void rm_utdata_init(struct rm_ut_data *data, enum obj_type type);
void rm_utdata_fini(struct rm_ut_data *data, enum obj_type type);
void rm_test_owner_capital_raise(struct m0_rm_owner *owner,
				 struct m0_rm_credit *credit);

/* Test server functions */
void rm_ctx_config(enum rm_server id);
void rm_ctx_init(struct rm_context *rmctx);
void rm_ctx_fini(struct rm_context *rmctx);
void rm_ctx_connect(struct rm_context *src, const struct rm_context *dest);
void rm_ctx_disconnect(struct rm_context *src, const struct rm_context *dest);
void rm_ctx_server_start(enum rm_server srv_id);
void rm_ctx_server_windup(enum rm_server srv_id);
void rm_ctx_server_stop(enum rm_server srv_id);
void creditor_cookie_setup(enum rm_server dsrv_id, enum rm_server csrv_id);
void loan_session_set(enum rm_server csrv_id, enum rm_server dsrv_id);

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
