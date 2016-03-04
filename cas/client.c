/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 11-Apr-2016
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CAS

#include "lib/trace.h"
#include "lib/vec.h"
#include "lib/memory.h"
#include "sm/sm.h"
#include "fid/fid.h"     /* m0_fid */
#include "rpc/item.h"
#include "rpc/rpc_machine_internal.h" /* m0_rpc_machine_{lock,unlock} */
#include "rpc/rpc.h"     /* m0_rpc_post */
#include "rpc/session.h" /* m0_rpc_session */
#include "rpc/conn.h"    /* m0_rpc_conn */
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "cas/cas.h"
#include "cas/client.h"
#include "lib/finject.h"

/**
 * @addtogroup cas-client
 * @{
 */

#define CASREQ_FOP_DATA(fop) ((struct m0_cas_op *)m0_fop_data(fop))

static void cas_req_replied_cb(struct m0_rpc_item *item);

static const struct m0_rpc_item_ops cas_item_ops = {
	.rio_sent    = NULL,
	.rio_replied = cas_req_replied_cb
};

static struct m0_sm_state_descr cas_req_states[] = {
	[CASREQ_INIT] = {
		.sd_flags     = M0_SDF_INITIAL | M0_SDF_FINAL,
		.sd_name      = "init",
		.sd_allowed   = M0_BITS(CASREQ_INPROGRESS)
	},
	[CASREQ_INPROGRESS] = {
		.sd_name      = "in-progress",
		.sd_allowed   = M0_BITS(CASREQ_REPLIED, CASREQ_FAILURE),
	},
	[CASREQ_REPLIED] = {
		.sd_name      = "replied",
		.sd_flags     = M0_SDF_TERMINAL,
	},
	[CASREQ_FAILURE] = {
		.sd_name      = "failure",
		.sd_flags     = M0_SDF_TERMINAL | M0_SDF_FAILURE
	}
};

static struct m0_sm_trans_descr cas_req_trans[] = {
	{ "send-over-rpc", CASREQ_INIT,       CASREQ_INPROGRESS },
	{ "rpc-failure",   CASREQ_INPROGRESS, CASREQ_FAILURE    },
	{ "rpc-replied",   CASREQ_INPROGRESS, CASREQ_REPLIED    },
};

static const struct m0_sm_conf cas_req_sm_conf = {
	.scf_name      = "cas_req",
	.scf_nr_states = ARRAY_SIZE(cas_req_states),
	.scf_state     = cas_req_states,
	.scf_trans_nr  = ARRAY_SIZE(cas_req_trans),
	.scf_trans     = cas_req_trans
};

M0_INTERNAL void m0_cas_req_init(struct m0_cas_req     *req,
				 struct m0_rpc_session *sess,
				 struct m0_sm_group    *grp)
{
	M0_ENTRY();
	M0_PRE(sess != NULL);
	M0_PRE(M0_IS0(req));
	req->ccr_sess = sess;
	m0_sm_init(&req->ccr_sm, &cas_req_sm_conf, CASREQ_INIT, grp);
	M0_LEAVE();
}

static struct m0_rpc_machine *cas_req_rpc_mach(struct m0_cas_req *req)
{
	return req->ccr_sess->s_conn->c_rpc_machine;
}

static struct m0_sm_group *cas_req_smgrp(const struct m0_cas_req *req)
{
	return req->ccr_sm.sm_grp;
}

M0_INTERNAL void m0_cas_req_lock(struct m0_cas_req *req)
{
	M0_ENTRY();
	m0_sm_group_lock(cas_req_smgrp(req));
}

M0_INTERNAL void m0_cas_req_unlock(struct m0_cas_req *req)
{
	M0_ENTRY();
	m0_sm_group_unlock(cas_req_smgrp(req));
}

M0_INTERNAL bool m0_cas_req_is_locked(const struct m0_cas_req *req)
{
	return m0_mutex_is_locked(&cas_req_smgrp(req)->s_lock);
}

static void cas_req_state_set(struct m0_cas_req     *req,
			      enum m0_cas_req_state  state)
{
	M0_LOG(M0_DEBUG, "CAS req: %p, state change:[%s -> %s]\n",
	       req, m0_sm_state_name(&req->ccr_sm, req->ccr_sm.sm_state),
	       m0_sm_state_name(&req->ccr_sm, state));
	m0_sm_state_set(&req->ccr_sm, state);
}

static void cas_req_fini(struct m0_cas_req *req)
{
	uint32_t cur_state = req->ccr_sm.sm_state;

	M0_ENTRY();
	M0_PRE(m0_cas_req_is_locked(req));
	M0_PRE(M0_IN(cur_state, (CASREQ_INIT, CASREQ_REPLIED, CASREQ_FAILURE)));
	if (cur_state == CASREQ_REPLIED) {
		M0_ASSERT(req->ccr_reply_item != NULL);
		m0_rpc_machine_lock(cas_req_rpc_mach(req));
		m0_rpc_item_put(req->ccr_reply_item);
		m0_fop_put(&req->ccr_fop);
		m0_rpc_machine_unlock(cas_req_rpc_mach(req));
	}
	m0_sm_fini(&req->ccr_sm);
	M0_LEAVE();
}

M0_INTERNAL void m0_cas_req_fini(struct m0_cas_req *req)
{
	M0_PRE(m0_cas_req_is_locked(req));
	cas_req_fini(req);
	M0_SET0(req);
}

M0_INTERNAL void m0_cas_req_fini_lock(struct m0_cas_req *req)
{
	M0_ENTRY();
	m0_cas_req_lock(req);
	cas_req_fini(req);
	m0_cas_req_unlock(req);
	M0_SET0(req);
	M0_LEAVE();
}

/**
 * Assigns pointers to record key/value to NULL, so they are not
 * deallocated during FOP finalisation. User is responsible for deallocating
 * them.
 */
static void cas_kv_hold_down(struct m0_cas_rec *rec)
{
	rec->cr_key = M0_BUF_INIT0;
	rec->cr_val = M0_BUF_INIT0;
}

static void cas_keys_values_hold_down(struct m0_cas_recv *recv)
{
	uint64_t i;

	for (i = 0; i < recv->cr_nr; i++)
		cas_kv_hold_down(&recv->cr_rec[i]);
}

static void cas_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop;

	M0_ENTRY();
	M0_PRE(ref != NULL);
	fop = container_of(ref, struct m0_fop, f_ref);
	cas_keys_values_hold_down(&CASREQ_FOP_DATA(fop)->cg_rec);
	m0_fop_fini(fop);
	M0_SET0(fop);
	M0_LEAVE();
}

static struct m0_cas_req *item_to_cas_req(struct m0_rpc_item *item)
{
	return container_of(m0_rpc_item_to_fop(item), struct m0_cas_req,
			    ccr_fop);
}

static struct m0_rpc_item *cas_req_to_item(const struct m0_cas_req *req)
{
	return (struct m0_rpc_item *)&req->ccr_fop.f_item;
}

static struct m0_cas_rep *cas_rep(struct m0_rpc_item *reply)
{
	return m0_fop_data(m0_rpc_item_to_fop(reply));
}

M0_INTERNAL int m0_cas_req_generic_rc(struct m0_cas_req *req)
{
	struct m0_rpc_item *reply = cas_req_to_item(req)->ri_reply;

	M0_PRE(M0_IN(req->ccr_sm.sm_state, (CASREQ_REPLIED, CASREQ_FAILURE)));
	return M0_RC(req->ccr_sm.sm_rc ?:
		     m0_rpc_item_generic_reply_rc(reply) ?:
		     cas_rep(reply)->cgr_rc);
}

static bool cas_rep_val_is_valid(struct m0_buf *val, struct m0_fid *idx_fid)
{
	return m0_buf_is_set(val) == !m0_fid_eq(idx_fid, &m0_cas_meta_fid);
}

static int cas_rep_validate(const struct m0_cas_req *req)
{
	const struct m0_fop *rfop = &req->ccr_fop;
	struct m0_cas_op    *op = m0_fop_data(rfop);
	struct m0_cas_rep   *rep = cas_rep(cas_req_to_item(req)->ri_reply);
	struct m0_cas_rec   *rec;
	uint64_t             sum;
	uint64_t             i;

	if (rfop->f_type == &cas_cur_fopt) {
		sum = 0;
		for (i = 0; i < op->cg_rec.cr_nr; i++)
			sum += op->cg_rec.cr_rec[i].cr_rc;
		if (rep->cgr_rep.cr_nr > sum)
			return M0_ERR(-EPROTO);
		for (i = 0; i < rep->cgr_rep.cr_nr; i++) {
			rec = &rep->cgr_rep.cr_rec[i];
			if ((int32_t)rec->cr_rc > 0 &&
			    (!m0_buf_is_set(&rec->cr_key) ||
			     !cas_rep_val_is_valid(&rec->cr_val,
						   &op->cg_id.ci_fid)))
				rec->cr_rc = M0_ERR(-EPROTO);
		}
	} else {
		M0_ASSERT(M0_IN(rfop->f_type, (&cas_get_fopt, &cas_put_fopt,
					       &cas_del_fopt)));
		/*
		 * CAS service guarantees equal number of records in request and
		 * response for GET,PUT, DEL operations.  Otherwise, it's not
		 * possible to match requested records with the ones in reply,
		 * because keys in reply are absent.
		 */
		if (op->cg_rec.cr_nr != rep->cgr_rep.cr_nr)
			return M0_ERR(-EPROTO);
		/*
		 * Successful GET reply for ordinary index should always contain
		 * non-empty value.
		 */
		if (rfop->f_type == &cas_get_fopt)
			for (i = 0; i < rep->cgr_rep.cr_nr; i++) {
				rec = &rep->cgr_rep.cr_rec[i];
				if (rec->cr_rc == 0 &&
				    !cas_rep_val_is_valid(&rec->cr_val,
							  &op->cg_id.ci_fid))
					rec->cr_rc = M0_ERR(-EPROTO);
			}
	}
	return M0_RC(0);
}

static void cas_req_failure(struct m0_cas_req *req, int32_t rc)
{
	M0_PRE(rc != 0);
	m0_sm_fail(&req->ccr_sm, CASREQ_FAILURE, rc);
}

static void cas_req_failure_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_cas_req *req = container_of(ast, struct m0_cas_req,
					      ccr_failure_ast);
	int32_t rc = (long)ast->sa_datum;

	M0_PRE(rc != 0);
	cas_req_failure(req, M0_ERR(rc));
}

static void cas_req_failure_ast_post(struct m0_cas_req *req, int32_t rc)
{
	M0_ENTRY();
	req->ccr_failure_ast.sa_cb = cas_req_failure_ast;
	req->ccr_failure_ast.sa_datum = (void *)(long)rc;
	m0_sm_ast_post(cas_req_smgrp(req), &req->ccr_failure_ast);
	M0_LEAVE();
}

static void cas_req_replied_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_cas_req *req = container_of(ast, struct m0_cas_req,
					      ccr_replied_ast);
	int                rc;

	rc = cas_rep_validate(req);
	if (rc == 0)
		cas_req_state_set(req, CASREQ_REPLIED);
	else
		cas_req_failure(req, M0_ERR(rc));
}

static void cas_req_replied_cb(struct m0_rpc_item *item)
{
	struct m0_cas_req *req = item_to_cas_req(item);

	M0_ENTRY();
	if (item->ri_error == 0) {
		M0_ASSERT(item->ri_reply != NULL);
		req->ccr_reply_item = item->ri_reply;
		/*
		 * Get additional reference to analyse reply in m0_cas_*_rep().
		 * Will be released on CAS request finalisation.
		 */
		m0_rpc_item_get(item->ri_reply);
		req->ccr_replied_ast.sa_cb = cas_req_replied_ast;
		m0_sm_ast_post(cas_req_smgrp(req), &req->ccr_replied_ast);
	} else {
		cas_req_failure_ast_post(req, item->ri_error);
	}
	M0_LEAVE();
}

static int cas_fop_send(struct m0_cas_req *req)
{
	struct m0_rpc_item *item;
	int                 rc;

	M0_ENTRY();
	M0_PRE(m0_cas_req_is_locked(req));
	item = cas_req_to_item(req);
	item->ri_rmachine = cas_req_rpc_mach(req);
	item->ri_ops      = &cas_item_ops;
	item->ri_session  = req->ccr_sess;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = M0_TIME_IMMEDIATELY;
	rc = m0_rpc_post(item);
	if (rc == 0)
		cas_req_state_set(req, CASREQ_INPROGRESS);
	return M0_RC(rc);
}

static int cas_index_op_prepare(const struct m0_fid  *indices,
				   uint64_t           indices_nr,
				   struct m0_cas_op **out)
{
	struct m0_cas_op  *op;
	struct m0_cas_rec *rec;
	uint64_t           i;

	M0_ENTRY();

	if (M0_FI_ENABLED("cas_alloc_fail"))
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(op);
	M0_ALLOC_ARR(rec, indices_nr);
	if (op == NULL || rec == NULL) {
		m0_free(op);
		m0_free(rec);
		return M0_ERR(-ENOMEM);
	}
	op->cg_id.ci_fid = m0_cas_meta_fid;
	op->cg_rec.cr_nr = indices_nr;
	op->cg_rec.cr_rec = rec;
	for (i = 0; i < indices_nr; i++)
		rec[i].cr_key = M0_BUF_INIT_PTR_CONST(&indices[i]);
	*out = op;
	return M0_RC(0);
}

M0_INTERNAL uint64_t m0_cas_req_nr(const struct m0_cas_req *req)
{
	struct m0_rpc_item *reply = cas_req_to_item(req)->ri_reply;
	struct m0_cas_rep  *cas_rep = m0_fop_data(m0_rpc_item_to_fop(reply));

	return cas_rep->cgr_rep.cr_nr;
}

M0_INTERNAL int m0_cas_req_wait(struct m0_cas_req *req, uint64_t states,
				m0_time_t to)
{
	M0_ENTRY();
	M0_PRE(m0_cas_req_is_locked(req));
	return M0_RC(m0_sm_timedwait(&req->ccr_sm, states, to));
}

M0_INTERNAL int m0_cas_index_create(struct m0_cas_req   *req,
				    const struct m0_fid *indices,
				    uint64_t             indices_nr,
				    struct m0_dtx       *dtx)
{
	struct m0_cas_op *op;
	int               rc;

	M0_ENTRY();
	M0_PRE(req->ccr_sess != NULL);
	M0_PRE(m0_cas_req_is_locked(req));
	(void)dtx;
	rc = cas_index_op_prepare(indices, indices_nr, &op);
	if (rc != 0)
		return M0_ERR(rc);
	m0_fop_init(&req->ccr_fop, &cas_put_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

static void cas_rep_copy(const struct m0_cas_req *req,
			 uint64_t                 idx,
			 struct m0_cas_rec_reply *rep)
{
	struct m0_rpc_item *item = m0_fop_to_rpc_item(&req->ccr_fop);
	struct m0_cas_recv *recv = &cas_rep(item->ri_reply)->cgr_rep;

	M0_ASSERT(idx < m0_cas_req_nr(req));
	rep->crr_rc = recv->cr_rec[idx].cr_rc;
	rep->crr_hint = recv->cr_rec[idx].cr_hint;
}

M0_INTERNAL void m0_cas_index_create_rep(struct m0_cas_req       *req,
					 uint64_t                 idx,
					 struct m0_cas_rec_reply *rep)
{
	M0_ENTRY();
	cas_rep_copy(req, idx, rep);
	M0_LEAVE();
}

M0_INTERNAL int m0_cas_index_delete(struct m0_cas_req   *req,
				    const struct m0_fid *indices,
				    uint64_t             indices_nr,
				    struct m0_dtx       *dtx)
{
	struct m0_cas_op *op;
	int               rc;

	M0_ENTRY();
	M0_PRE(req->ccr_sess != NULL);
	M0_PRE(m0_cas_req_is_locked(req));
	(void)dtx;
	rc = cas_index_op_prepare(indices, indices_nr, &op);
	if (rc != 0)
		return M0_ERR(rc);
	m0_fop_init(&req->ccr_fop, &cas_del_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

M0_INTERNAL void m0_cas_index_delete_rep(struct m0_cas_req       *req,
					 uint64_t                 idx,
					 struct m0_cas_rec_reply *rep)
{
	M0_ENTRY();
	cas_rep_copy(req, idx, rep);
	M0_LEAVE();
}

M0_INTERNAL int m0_cas_index_lookup(struct m0_cas_req   *req,
				    const struct m0_fid *indices,
				    uint64_t             indices_nr)
{
	struct m0_cas_op  *op;
	int                rc;

	M0_ENTRY();
	M0_PRE(req->ccr_sess != NULL);
	M0_PRE(m0_cas_req_is_locked(req));
	rc = cas_index_op_prepare(indices, indices_nr, &op);
	if (rc != 0)
		return M0_ERR(rc);
	m0_fop_init(&req->ccr_fop, &cas_get_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

M0_INTERNAL void m0_cas_index_lookup_rep(struct m0_cas_req       *req,
					 uint64_t                 idx,
					 struct m0_cas_rec_reply *rep)
{
	M0_ENTRY();
	cas_rep_copy(req, idx, rep);
	M0_LEAVE();
}

M0_INTERNAL int m0_cas_index_list(struct m0_cas_req   *req,
				  const struct m0_fid *start_fid,
				  uint32_t             indices_nr)
{
	struct m0_cas_op *op;
	int               rc;

	M0_ENTRY();
	M0_PRE(start_fid != NULL);
	M0_PRE(req->ccr_sess != NULL);
	M0_PRE(m0_cas_req_is_locked(req));
	rc = cas_index_op_prepare(start_fid, 1, &op);
	if (rc != 0)
		return M0_ERR(rc);
	op->cg_rec.cr_rec[0].cr_rc = indices_nr;
	m0_fop_init(&req->ccr_fop, &cas_cur_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

static int cas_next_rc(int64_t service_rc)
{
	int rc;

	/*
	 * Zero return code means some error on service side.
	 * Service place sequence number of record starting from 1 in cr_rc on
	 * success.
	 */
	if (service_rc == 0)
		rc = M0_ERR(-EPROTO);
	else if (service_rc < 0)
		rc = service_rc;
	else
		rc = 0;
	return M0_RC(rc);
}

M0_INTERNAL void m0_cas_index_list_rep(struct m0_cas_req         *req,
				       uint32_t                   idx,
				       struct m0_cas_ilist_reply *rep)
{
	struct m0_rpc_item *item  = m0_fop_to_rpc_item(&req->ccr_fop);
	struct m0_cas_recv *recv  = &cas_rep(item->ri_reply)->cgr_rep;

	M0_ENTRY();
	M0_PRE(idx < m0_cas_req_nr(req));

	rep->clr_rc = cas_next_rc(recv->cr_rec[idx].cr_rc);
	if (rep->clr_rc == 0) {
		rep->clr_fid = *(struct m0_fid*)recv->cr_rec[idx].cr_key.b_addr;
		rep->clr_hint = recv->cr_rec[idx].cr_hint;
	}
	M0_LEAVE();
}

static int cas_records_op_prepare(const struct m0_cas_id  *index,
				  const struct m0_bufvec  *keys,
				  const struct m0_bufvec  *values,
				  struct m0_cas_op       **out)
{
	struct m0_cas_op  *op;
	struct m0_cas_rec *rec;
	uint32_t           keys_nr = keys->ov_vec.v_nr;
	uint64_t           i;

	M0_ENTRY();
	if (M0_FI_ENABLED("cas_alloc_fail"))
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(op);
	M0_ALLOC_ARR(rec, keys_nr);
	if (op == NULL || rec == NULL) {
		m0_free(op);
		m0_free(rec);
		return M0_ERR(-ENOMEM);
	}
	op->cg_id = *index;
	op->cg_rec.cr_nr = keys_nr;
	op->cg_rec.cr_rec = rec;
	for (i = 0; i < keys_nr; i++) {
		m0_buf_init(&rec[i].cr_key, keys->ov_buf[i],
			    keys->ov_vec.v_count[i]);
		if (values != NULL)
			m0_buf_init(&rec[i].cr_val, values->ov_buf[i],
				    values->ov_vec.v_count[i]);
	}
	*out = op;
	return M0_RC(0);
}

M0_INTERNAL int m0_cas_put(struct m0_cas_req      *req,
			   struct m0_cas_id       *index,
			   const struct m0_bufvec *keys,
			   const struct m0_bufvec *values,
			   struct m0_dtx          *dtx)
{
	struct m0_cas_op *op;
	int               rc;

	M0_ENTRY();
	M0_PRE(keys != NULL);
	M0_PRE(values != NULL);
	M0_PRE(keys->ov_vec.v_nr == values->ov_vec.v_nr);
	M0_PRE(m0_cas_req_is_locked(req));

	(void)dtx;
	rc = cas_records_op_prepare(index, keys, values, &op);
	if (rc != 0)
		return M0_ERR(rc);
	m0_fop_init(&req->ccr_fop, &cas_put_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

M0_INTERNAL void m0_cas_put_rep(struct m0_cas_req       *req,
				uint64_t                 idx,
				struct m0_cas_rec_reply *rep)
{
	M0_ENTRY();
	cas_rep_copy(req, idx, rep);
	M0_LEAVE();
}

M0_INTERNAL int m0_cas_get(struct m0_cas_req      *req,
			   struct m0_cas_id       *index,
			   const struct m0_bufvec *keys)
{
	struct m0_cas_op *op;
	int               rc;

	M0_ENTRY();
	M0_PRE(keys != NULL);
	M0_PRE(m0_cas_req_is_locked(req));

	rc = cas_records_op_prepare(index, keys, NULL, &op);
	if (rc != 0)
		return M0_ERR(rc);
	m0_fop_init(&req->ccr_fop, &cas_get_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

M0_INTERNAL void m0_cas_get_rep(const struct m0_cas_req *req,
				uint64_t                 idx,
				struct m0_cas_get_reply *rep)
{
	struct m0_rpc_item *reply = cas_req_to_item(req)->ri_reply;
	struct m0_cas_rep  *cas_rep = m0_fop_data(m0_rpc_item_to_fop(reply));
	struct m0_cas_rec  *cas_rec = &cas_rep->cgr_rep.cr_rec[idx];

	M0_ENTRY();
	rep->cge_rc = cas_rec->cr_rc;
	if (rep->cge_rc == 0)
		rep->cge_val = cas_rec->cr_val;
	M0_LEAVE();
}

M0_INTERNAL int m0_cas_next(struct m0_cas_req *req,
			    struct m0_cas_id  *index,
			    struct m0_bufvec  *start_keys,
			    uint32_t          *recs_nr)
{
	struct m0_cas_op *op;
	int               rc;
	uint32_t          i;

	M0_ENTRY();
	M0_PRE(start_keys != NULL);
	M0_PRE(m0_cas_req_is_locked(req));

	rc = cas_records_op_prepare(index, start_keys, NULL, &op);
	if (rc != 0)
		return M0_ERR(rc);
	for (i = 0; i < start_keys->ov_vec.v_nr; i++)
		op->cg_rec.cr_rec[i].cr_rc = recs_nr[i];
	m0_fop_init(&req->ccr_fop, &cas_cur_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

M0_INTERNAL void m0_cas_rep_mlock(const struct m0_cas_req *req,
				  uint64_t                 idx)
{
	struct m0_rpc_item *reply = cas_req_to_item(req)->ri_reply;
	struct m0_cas_rep  *cas_rep = m0_fop_data(m0_rpc_item_to_fop(reply));

	M0_PRE(M0_IN(req->ccr_fop.f_type, (&cas_get_fopt, &cas_cur_fopt)));
	cas_kv_hold_down(&cas_rep->cgr_rep.cr_rec[idx]);
}

M0_INTERNAL void m0_cas_next_rep(const struct m0_cas_req  *req,
				 uint32_t                  idx,
				 struct m0_cas_next_reply *rep)
{
	struct m0_rpc_item *reply = cas_req_to_item(req)->ri_reply;
	struct m0_cas_rep  *cas_rep = m0_fop_data(m0_rpc_item_to_fop(reply));
	struct m0_cas_rec  *cas_rec = &cas_rep->cgr_rep.cr_rec[idx];

	M0_ENTRY();
	rep->cnp_rc = cas_next_rc(cas_rec->cr_rc);
	if (rep->cnp_rc == 0) {
		rep->cnp_key = cas_rec->cr_key;
		rep->cnp_val = cas_rec->cr_val;
	}
}

M0_INTERNAL int m0_cas_del(struct m0_cas_req *req,
			   struct m0_cas_id  *index,
			   struct m0_bufvec  *keys,
			   struct m0_dtx     *dtx)
{
	struct m0_cas_op *op;
	int               rc;

	M0_ENTRY();
	M0_PRE(keys != NULL);
	M0_PRE(m0_cas_req_is_locked(req));

	(void)dtx;
	rc = cas_records_op_prepare(index, keys, NULL, &op);
	if (rc != 0)
		return M0_ERR(rc);
	m0_fop_init(&req->ccr_fop, &cas_del_fopt, (void *)op, cas_fop_release);
	return M0_RC(cas_fop_send(req));
}

M0_INTERNAL void m0_cas_del_rep(struct m0_cas_req       *req,
				uint64_t                 idx,
				struct m0_cas_rec_reply *rep)
{
	M0_ENTRY();
	cas_rep_copy(req, idx, rep);
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of cas-client group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
