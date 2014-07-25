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

#include "lib/memory.h"
#include "lib/cookie.h"
#include "lib/misc.h"
#include "rpc/rpclib.h"           /* m0_rpc_client_connect */
#include "ut/ut.h"
#include "lib/ub.h"
#include "rm/rm.h"
#include "rm/rm_service.h"        /* m0_rms_type */
#include "rm/rm_internal.h"
#include "rm/ut/rings.h"
#include "rm/ut/rmut.h"

extern const struct m0_tl_descr m0_remotes_tl;

const char *db_name[] = {"ut-rm-cob_1",
			 "ut-rm-cob_2",
			 "ut-rm-cob_3"
};

const char *serv_addr[] = { "0@lo:12345:34:1",
			    "0@lo:12345:34:2",
			    "0@lo:12345:34:3"
};

const int cob_ids[] = { 20, 30, 40 };
/*
 * Test variable(s)
 */
struct rm_ut_data rm_test_data;
struct rm_context rm_ctx[SERVER_NR];
struct m0_chan    rm_ut_tests_chan;
struct m0_mutex   rm_ut_tests_chan_mutex;

extern void rm_api_test(void);
extern void local_credits_test(void);
extern void remote_credits_test(void);
extern void rm_fom_funcs_test(void);
extern void rm_fop_funcs_test(void);
extern void flock_test(void);
extern void rm_group_test(void);
extern bool m0_rm_ur_tlist_is_empty(const struct m0_tl *list);
extern void m0_remotes_tlist_del(struct m0_rm_remote *other);
extern void rmsvc(void);

struct rm_ut_data test_data;

void rm_test_owner_capital_raise(struct m0_rm_owner *owner,
				 struct m0_rm_credit *credit)
{
	m0_rm_credit_init(credit, owner);
	/* Set the initial capital */
	credit->cr_ops->cro_initial_capital(credit);
	m0_rm_owner_selfadd(owner, credit);
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_owned[OWOS_CACHED]));
}

/*
 * Recursive call to initialise object hierarchy
 *
 * XXX TODO: Use module/module.h API.
 */
void rm_utdata_init(struct rm_ut_data *data, enum obj_type type)
{
	M0_UT_ASSERT(data != NULL);

	switch (type) {
		case OBJ_DOMAIN:
			/* Initialise test_domain */
			m0_rm_domain_init(&data->rd_dom);
			break;
		case OBJ_RES_TYPE:
			rm_utdata_init(data, OBJ_DOMAIN);
			/* Register test resource type */
			data->rd_ops->rtype_set(data);
			break;
		case OBJ_RES:
			rm_utdata_init(data, OBJ_RES_TYPE);
			data->rd_ops->resource_set(data);
			break;
		case OBJ_OWNER:
			rm_utdata_init(data, OBJ_RES);
			data->rd_ops->owner_set(data);
			break;
		default:
			M0_IMPOSSIBLE("Invalid value of obj_type");
	}
}

/*
 * Recursive call to finalise object hierarchy
 *
 * XXX TODO: Use module/module.h API.
 */
void rm_utdata_fini(struct rm_ut_data *data, enum obj_type type)
{
	struct m0_rm_remote *other;

	M0_UT_ASSERT(data != NULL);

	switch (type) {
		case OBJ_DOMAIN:
			/* Finalise test_domain */
			m0_rm_domain_fini(&data->rd_dom);
			break;
		case OBJ_RES_TYPE:
			/* De-register test resource type */
			data->rd_ops->rtype_unset(data);
			rm_utdata_fini(data, OBJ_DOMAIN);
			break;
		case OBJ_RES:
			m0_tl_teardown(m0_remotes,
				       &data->rd_res->r_remote, other) {
				m0_rm_remote_fini(other);
				m0_free(other);
			}
			data->rd_ops->resource_unset(data);
			rm_utdata_fini(data, OBJ_RES_TYPE);
			break;
		case OBJ_OWNER:
			data->rd_ops->owner_unset(data);
			rm_utdata_fini(data, OBJ_RES);
			break;
		default:
			M0_IMPOSSIBLE("Invalid value of obj_type");
	}
}

struct m0_reqh_service *rmservice[SERVER_NR];

void rm_ctx_init(struct rm_context *rmctx)
{
	enum rm_server id;
	int            rc;

	/* Determine `id'. */
	for (id = 0; id < SERVER_NR && rmctx != &rm_ctx[id]; ++id)
		;
	M0_PRE(id != SERVER_NR);

	*rmctx = (struct rm_context){
		.rc_id        = id,
		.rc_rmach_ctx = {
			.rmc_cob_id.id = cob_ids[id],
			.rmc_dbname    = db_name[id],
			.rmc_ep_addr   = serv_addr[id]
		}
	};
	m0_ut_rpc_mach_init_and_add(&rmctx->rc_rmach_ctx);
	m0_mutex_init(&rmctx->rc_mutex);
	rc = m0_reqh_service_setup(&rmservice[rmctx->rc_id],
				   &m0_rms_type, &rmctx->rc_rmach_ctx.rmc_reqh,
				   NULL, NULL);
	M0_UT_ASSERT(rc == 0);
	m0_chan_init(&rmctx->rc_chan, &rmctx->rc_mutex);
	m0_clink_init(&rmctx->rc_clink, NULL);
}

void rm_ctx_fini(struct rm_context *rmctx)
{
	m0_clink_fini(&rmctx->rc_clink);
	m0_chan_fini_lock(&rmctx->rc_chan);
	m0_mutex_fini(&rmctx->rc_mutex);
	m0_ut_rpc_mach_fini(&rmctx->rc_rmach_ctx);
}

void rm_ctx_connect(struct rm_context *src, const struct rm_context *dest)
{
	enum { MAX_RPCS_IN_FLIGHT = 15 };
	int rc;

	rc = m0_rpc_client_connect(&src->rc_conn[dest->rc_id],
				   &src->rc_sess[dest->rc_id],
				   &src->rc_rmach_ctx.rmc_rpc,
				   dest->rc_rmach_ctx.rmc_ep_addr,
				   MAX_RPCS_IN_FLIGHT);
	M0_UT_ASSERT(rc == 0);
}

void rm_ctx_disconnect(struct rm_context *src, const struct rm_context *dest)
{
	int rc;

	rc = m0_rpc_session_destroy(&src->rc_sess[dest->rc_id], M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_conn_destroy(&src->rc_conn[dest->rc_id], M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
}

void rm_ctx_server_start(enum rm_server srv_id)
{
	struct m0_rm_remote *creditor;
	struct m0_rm_owner  *owner;
	struct rm_ut_data   *data = &rm_ctx[srv_id].rc_test_data;
	enum rm_server	     cred_id = rm_ctx[srv_id].creditor_id;
	enum rm_server	     debtr_id;
	uint32_t             debtors_nr = rm_ctx[srv_id].rc_debtors_nr;
	uint32_t             i;

	rm_utdata_init(data, OBJ_OWNER);
	owner = data->rd_owner;

	/*
	 * If creditor id is valid, do creditor setup.
	 * If there is no creditor, this server is original owner.
	 * For original owner, raise capital.
	 */
	if (cred_id != SERVER_INVALID) {
		rm_ctx_connect(&rm_ctx[srv_id], &rm_ctx[cred_id]);
		M0_ALLOC_PTR(creditor);
		M0_UT_ASSERT(creditor != NULL);
		m0_rm_remote_init(creditor, owner->ro_resource);
		creditor->rem_session = &rm_ctx[srv_id].rc_sess[cred_id];
		owner->ro_creditor = creditor;
	} else
		rm_test_owner_capital_raise(owner, &data->rd_credit);

	for (i = 0; i < debtors_nr; ++i) {
		debtr_id = rm_ctx[srv_id].debtor_id[i];
		if (debtr_id != SERVER_INVALID)
			rm_ctx_connect(&rm_ctx[srv_id], &rm_ctx[debtr_id]);
	}

}

void rm_ctx_server_windup(enum rm_server srv_id)
{
	struct m0_rm_owner *owner = rm_ctx[srv_id].rc_test_data.rd_owner;
	enum rm_server      cred_id = rm_ctx[srv_id].creditor_id;

	if (cred_id != SERVER_INVALID) {
		M0_UT_ASSERT(owner->ro_creditor != NULL);
		m0_rm_remote_fini(owner->ro_creditor);
		m0_free0(&owner->ro_creditor);
	}
	rm_utdata_fini(&rm_ctx[srv_id].rc_test_data, OBJ_OWNER);
}

void rm_ctx_server_stop(enum rm_server srv_id)
{
	enum rm_server cred_id = rm_ctx[srv_id].creditor_id;
	enum rm_server debtr_id;
	uint32_t       debtors_nr = rm_ctx[srv_id].rc_debtors_nr;
	uint32_t       i;

	if (cred_id != SERVER_INVALID)
		rm_ctx_disconnect(&rm_ctx[srv_id], &rm_ctx[cred_id]);
	for (i = 0; i < debtors_nr; ++i) {
		debtr_id = rm_ctx[srv_id].debtor_id[i];
		if (debtr_id != SERVER_INVALID)
			rm_ctx_disconnect(&rm_ctx[srv_id], &rm_ctx[debtr_id]);
	}
}

void loan_session_set(enum rm_server csrv_id,
		      enum rm_server dsrv_id)
{
	struct m0_rm_owner  *owner = rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct m0_rm_loan   *loan;
	struct m0_rm_credit *credit;
	struct m0_rm_remote *remote;
	struct m0_cookie     dcookie;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_sublet));
	m0_tl_for(m0_rm_ur, &owner->ro_sublet, credit) {
		loan = bob_of(credit, struct m0_rm_loan, rl_credit, &loan_bob);
		M0_UT_ASSERT(loan != NULL);
		m0_cookie_init(&dcookie,
			       &rm_ctx[dsrv_id].rc_test_data.rd_owner->ro_id);
		remote = loan->rl_other;
		if (m0_cookie_is_eq(&dcookie, &remote->rem_cookie))
			remote->rem_session = &rm_ctx[csrv_id].rc_sess[dsrv_id];
	} m0_tl_endfor;
}

void creditor_cookie_setup(enum rm_server dsrv_id,
			   enum rm_server csrv_id)
{
	struct m0_rm_owner *creditor = rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct m0_rm_owner *owner = rm_ctx[dsrv_id].rc_test_data.rd_owner;

	m0_cookie_init(&owner->ro_creditor->rem_cookie, &creditor->ro_id);
}

const struct m0_test_suite rm_ut = {
	.ts_name = "rm-ut",
	.ts_tests = {
		{ "api", rm_api_test },
		{ "lcredits", local_credits_test },
		{ "fop-funcs", rm_fop_funcs_test },
#ifndef __KERNEL__
		{ "fom-funcs", rm_fom_funcs_test },
		{ "rcredits", remote_credits_test },
		{ "rmsvc", rmsvc },
		{ "flock", flock_test },
		{ "group", rm_group_test },
#endif
		{ NULL, NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
