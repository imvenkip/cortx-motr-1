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
#include "lib/misc.h"
#include "ut/ut.h"
#include "lib/ub.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/ut/rings.h"
#include "rm/ut/rmut.h"

extern const struct m0_tl_descr m0_remotes_tl;

/*
 * Test variable(s)
 */
struct rm_ut_data rm_test_data;

extern void rm_api_test(void);
extern void local_credits_test(void);
extern void remote_credits_test(void);
extern void rm_fom_funcs_test(void);
extern void rm_fop_funcs_test(void);
extern void flock_test(void);
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

void rm_ctx_config(enum rm_server id)
{
	M0_SET0(&rm_ctx[id]);
	rm_ctx[id].rc_id = id;
	rm_ctx[id].rc_rmach_ctx.rmc_cob_id.id = cob_ids[id];
	rm_ctx[id].rc_rmach_ctx.rmc_dbname = db_name[id];
	rm_ctx[id].rc_rmach_ctx.rmc_ep_addr = serv_addr[id];
	rm_ctx_init(&rm_ctx[id]);
}

struct m0_reqh_service *rsvc;

void rm_ctx_init(struct rm_context *rmctx)
{
	int                                 rc;
	static struct m0_reqh_service_type *stype = NULL;

	m0_ut_rpc_mach_init_and_add(&rmctx->rc_rmach_ctx);

	m0_mutex_init(&rmctx->rc_mutex);

	if (rmctx->rc_id == 0) {
		if (stype == NULL) {
			stype = m0_reqh_service_type_find("rmservice");
			M0_UT_ASSERT(stype != NULL);
		}

		rc = m0_reqh_service_allocate(&rsvc, stype, NULL);
		M0_UT_ASSERT(rc == 0);
		m0_reqh_service_init(rsvc,
				     &rmctx->rc_rmach_ctx.rmc_reqh, NULL);
		rc = m0_reqh_service_start(rsvc);
		M0_UT_ASSERT(rc == 0);
	} else {
		m0_reqh_lockers_set(&rmctx->rc_rmach_ctx.rmc_reqh,
				    stype->rst_key, rsvc);
	}

	m0_chan_init(&rmctx->rc_chan, &rmctx->rc_mutex);
	m0_clink_init(&rmctx->rc_clink, NULL);
}

void rm_ctx_fini(struct rm_context *rmctx)
{
	m0_clink_fini(&rmctx->rc_clink);
	m0_chan_fini_lock(&rmctx->rc_chan);
	m0_mutex_fini(&rmctx->rc_mutex);
	if (rmctx->rc_id == 0) {
		m0_reqh_service_stop(rsvc);
		m0_reqh_service_fini(rsvc);
	}
	m0_ut_rpc_mach_fini(&rmctx->rc_rmach_ctx);
}

void rm_connect(struct rm_context *src, const struct rm_context *dest)
{
	struct m0_net_end_point *ep;
	int		         rc;

	/*
	 * Create a local end point to communicate with remote server.
	 */
	rc = m0_net_end_point_create(&ep,
				     &src->rc_rmach_ctx.rmc_rpc.rm_tm,
				     dest->rc_rmach_ctx.rmc_ep_addr);
	M0_UT_ASSERT(rc == 0);
	src->rc_ep[dest->rc_id] = ep;

	rc = m0_rpc_conn_create(&src->rc_conn[dest->rc_id],
				ep, &src->rc_rmach_ctx.rmc_rpc, 15,
				M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_session_create(&src->rc_sess[dest->rc_id],
				   &src->rc_conn[dest->rc_id], 1,
				   M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
}

void rm_disconnect(struct rm_context *src, const struct rm_context *dest)
{
	int rc;

	rc = m0_rpc_session_destroy(&src->rc_sess[dest->rc_id], M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_conn_destroy(&src->rc_conn[dest->rc_id], M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	m0_net_end_point_put(src->rc_ep[dest->rc_id]);
}

void rm_ctx_server_start(enum rm_server srv_id)
{
	struct m0_rm_remote *creditor;
	struct m0_rm_owner  *owner;
	struct rm_ut_data   *data = &rm_ctx[srv_id].rc_test_data;
	enum rm_server	     cred_id = rm_ctx[srv_id].creditor_id;
	enum rm_server	     debt_id = rm_ctx[srv_id].debtor_id;

	rm_utdata_init(&rm_ctx[srv_id].rc_test_data, OBJ_OWNER);
	owner = rm_ctx[srv_id].rc_test_data.rd_owner;

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
		rm_test_owner_capital_raise(data->rd_owner, &data->rd_credit);

	if (debt_id != SERVER_INVALID)
		rm_ctx_connect(&rm_ctx[srv_id], &rm_ctx[debt_id]);

}

void rm_ctx_server_windup(enum rm_server srv_id)
{
	struct m0_rm_remote *creditor;
	struct m0_rm_owner  *owner = rm_ctx[srv_id].rc_test_data.rd_owner;
	enum rm_server	     cred_id = rm_ctx[srv_id].creditor_id;

	if (cred_id != SERVER_INVALID) {
		creditor = owner->ro_creditor;
		M0_UT_ASSERT(creditor != NULL);
		m0_rm_remote_fini(creditor);
		m0_free(creditor);
		owner->ro_creditor = NULL;
	}
	rm_utdata_fini(&rm_ctx[srv_id].rc_test_data, OBJ_OWNER);
}

void rm_ctx_server_stop(enum rm_server srv_id)
{
	enum rm_server cred_id = rm_ctx[srv_id].creditor_id;
	enum rm_server debt_id = rm_ctx[srv_id].debtor_id;

	if (cred_id != SERVER_INVALID)
		rm_ctx_disconnect(&rm_ctx[srv_id], &rm_ctx[cred_id]);
	if (debt_id != SERVER_INVALID)
		rm_ctx_disconnect(&rm_ctx[srv_id], &rm_ctx[debt_id]);
}

void loan_session_set(enum rm_server csrv_id,
		      enum rm_server dsrv_id)
{
	struct m0_rm_owner *owner = rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct m0_rm_loan  *loan;
	struct m0_rm_credit *credit;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_sublet));
	m0_tl_for(m0_rm_ur, &owner->ro_sublet, credit) {
		loan = bob_of(credit, struct m0_rm_loan, rl_credit, &loan_bob);
		M0_UT_ASSERT(loan != NULL && loan->rl_other != NULL);
		loan->rl_other->rem_session =
			&rm_ctx[csrv_id].rc_sess[dsrv_id];
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
		{ "fom-funcs", rm_fom_funcs_test },
		{ "fop-funcs", rm_fop_funcs_test },
#ifndef __KERNEL__
		{ "rcredits", remote_credits_test },
		{ "rmsvc", rmsvc },
		{ "flock", flock_test },
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
