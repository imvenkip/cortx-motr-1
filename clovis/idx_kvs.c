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
 * Original creation date: 27-Apr-2016
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"
#include "lib/assert.h"
#include "lib/tlist.h"         /* m0_tl */
#include "lib/memory.h"
#include "fid/fid.h"           /* m0_fid */
#include "pool/pool.h"         /* pools_common_svc_ctx */
#include "clovis/clovis_internal.h"
#include "clovis/clovis_idx.h"
#include "cas/client.h"

/**
 * @addtogroup clovis-index-kvs
 *
 * @{
 */

#define OI_IFID(oi) (struct m0_fid *)&(oi)->oi_idx->in_entity.en_id

static bool casreq_clink_cb(struct m0_clink *cl);

struct kvs_req {
	struct m0_clovis_op_idx *idr_oi;
	struct m0_sm_ast         idr_ast;
	struct m0_clink          idr_clink;
	struct m0_cas_req        idr_creq;
	/**
	 * Starting key for NEXT operation.
	 * It's allocated internally and key value is copied from user.
	 */
	struct m0_bufvec         idr_start_key;
};

static struct m0_reqh_service_ctx *svc_find(const struct m0_clovis_op_idx *oi)
{
	struct m0_clovis *m0c;

	m0c = m0_clovis__entity_instance(oi->oi_oc.oc_op.op_entity);
	return m0_tl_find(pools_common_svc_ctx, ctx,
			  &m0c->m0c_pools_common.pc_svc_ctxs,
			  ctx->sc_type == M0_CST_CAS);
}

static int kvs_req_create(struct m0_clovis_op_idx  *oi,
			  struct kvs_req          **out)
{
	struct m0_reqh_service_ctx *svc;
	struct kvs_req             *req;

	M0_ALLOC_PTR(req);
	if (req == NULL)
		return M0_ERR(-ENOMEM);
	svc = svc_find(oi);
	M0_ASSERT(svc != NULL);
	m0_cas_req_init(&req->idr_creq, &svc->sc_rlink.rlk_sess, oi->oi_sm_grp);
	m0_clink_init(&req->idr_clink, casreq_clink_cb);
	req->idr_oi = oi;
	*out = req;
	return M0_RC(0);
}

static void kvs_req_destroy(struct kvs_req *req)
{
	M0_ENTRY();
	m0_clink_fini(&req->idr_clink);
	m0_cas_req_fini(&req->idr_creq);
	m0_bufvec_free(&req->idr_start_key);
	m0_free(req);
	M0_LEAVE();
}

static void casreq_completed_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = req->idr_oi;
	int                      rc = oi->oi_ar.ar_rc;

	M0_ENTRY();
	oi->oi_ar.ar_ast.sa_cb = (rc == 0) ? clovis_idx_op_ast_complete :
					     clovis_idx_op_ast_fail;
	m0_sm_ast_post(oi->oi_sm_grp, &oi->oi_ar.ar_ast);
	kvs_req_destroy(req);
	M0_LEAVE();
}

static void casreq_completed_post(struct kvs_req *req, int rc)
{
	struct m0_clovis_op_idx *oi = req->idr_oi;

	M0_ENTRY();
	oi->oi_ar.ar_rc = rc;
	req->idr_ast.sa_cb = casreq_completed_ast;
	req->idr_ast.sa_datum = req;
	m0_sm_ast_post(oi->oi_sm_grp, &req->idr_ast);
	M0_LEAVE();
}

static int kvs_list_reply_copy(struct m0_cas_req *req, struct m0_bufvec *bvec)
{
	uint64_t                  rep_count = m0_cas_req_nr(req);
	struct m0_cas_ilist_reply rep;
	uint64_t                  i;
	uint64_t                  k = 0;

	/* Assertion is guaranteed by CAS client. */
	M0_PRE(bvec->ov_vec.v_nr >= rep_count);
	for (i = 0; i < rep_count; i++) {
		m0_cas_index_list_rep(req, i, &rep);
		if (rep.clr_rc == 0) {
			/* User should allocate buffer of appropriate size */
			M0_ASSERT(bvec->ov_vec.v_count[k] ==
				  sizeof(struct m0_fid));
			*(struct m0_fid *)bvec->ov_buf[k] = rep.clr_fid;
			k++;
		}
	}
	return k > 0 ? M0_RC(0) : M0_ERR(-ENODATA);
}

static int kvs_get_reply_copy(struct m0_cas_req *req, struct m0_bufvec *bvec)
{
	uint64_t                rep_count = m0_cas_req_nr(req);
	struct m0_cas_get_reply rep;
	uint64_t                i;
	int                     rc = 0;

	/* Assertion is guaranteed by CAS client. */
	M0_PRE(bvec->ov_vec.v_nr >= rep_count);
	for (i = 0; i < rep_count; i++) {
		m0_cas_get_rep(req, i, &rep);
		if (rep.cge_rc == 0) {
			m0_cas_rep_mlock(req, i);
			bvec->ov_vec.v_count[i] = rep.cge_val.b_nob;
			bvec->ov_buf[i] = rep.cge_val.b_addr;
		} else {
			M0_ASSERT(bvec->ov_vec.v_count[i] == 0);
			M0_ASSERT(bvec->ov_buf[i] == NULL);
			rc = M0_ERR(rep.cge_rc);
		}
	}
	return M0_RC(rc);
}

static void kvs_next_reply_copy(struct m0_cas_req *req,
				struct m0_bufvec  *keys,
				struct m0_bufvec  *vals)
{
	uint64_t                 rep_count = m0_cas_req_nr(req);
	struct m0_cas_next_reply rep;
	uint64_t                 i;
	uint64_t                 k = 0;

	/* Assertions are guaranteed by CAS client. */
	M0_PRE(keys->ov_vec.v_nr >= rep_count);
	M0_PRE(vals->ov_vec.v_nr >= rep_count);
	for (i = 0; i < rep_count; i++) {
		m0_cas_next_rep(req, i, &rep);
		if (rep.cnp_rc == 0) {
			m0_cas_rep_mlock(req, i);
			keys->ov_vec.v_count[k] = rep.cnp_key.b_nob;
			keys->ov_buf[k] = rep.cnp_key.b_addr;
			vals->ov_vec.v_count[k] = rep.cnp_val.b_nob;
			vals->ov_buf[k] = rep.cnp_val.b_addr;
			k++;
		}
	}
}

static bool casreq_clink_cb(struct m0_clink *cl)
{
	struct kvs_req          *kvs_req = container_of(cl, struct kvs_req,
							idr_clink);
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_sm            *req_sm = container_of(cl->cl_chan,
						       struct m0_sm, sm_chan);
	struct m0_cas_req       *creq = container_of(req_sm, struct m0_cas_req,
						    ccr_sm);
	uint32_t                 state = creq->ccr_sm.sm_state;
	struct m0_clovis_op     *op;
	int                      rc;
	struct m0_cas_rec_reply  rep;
	uint64_t                 i;

	if (!M0_IN(state, (CASREQ_FAILURE, CASREQ_FINAL)))
		return false;

	m0_clink_del(cl);
	op = &oi->oi_oc.oc_op;

	rc = m0_cas_req_generic_rc(creq);
	if (rc == 0) {
		/*
		 * Response from CAS service is validated by CAS client,
		 * including number of records in response.
		 */
		switch (op->op_code) {
		case M0_CLOVIS_EO_CREATE:
			M0_ASSERT(m0_cas_req_nr(creq) == 1);
			m0_cas_index_create_rep(creq, 0, &rep);
			rc = rep.crr_rc;
			break;
		case M0_CLOVIS_EO_DELETE:
			M0_ASSERT(m0_cas_req_nr(creq) == 1);
			m0_cas_index_delete_rep(creq, 0, &rep);
			rc = rep.crr_rc;
			break;
		case M0_CLOVIS_IC_LOOKUP:
			M0_ASSERT(m0_cas_req_nr(creq) == 1);
			m0_cas_index_lookup_rep(creq, 0, &rep);
			rc = rep.crr_rc;
			break;
		case M0_CLOVIS_IC_LIST:
			rc = kvs_list_reply_copy(creq, oi->oi_keys);
			break;
		case M0_CLOVIS_IC_PUT:
			for (i = 0; i < m0_cas_req_nr(creq); i++) {
				m0_cas_put_rep(creq, 0, &rep);
				if (rep.crr_rc != 0) {
					rc = M0_ERR(rep.crr_rc);
					break;
				}
			}
			break;
		case M0_CLOVIS_IC_GET:
			rc = kvs_get_reply_copy(creq, oi->oi_vals);
			break;
		case M0_CLOVIS_IC_DEL:
			for (i = 0; i < m0_cas_req_nr(creq); i++) {
				m0_cas_del_rep(creq, 0, &rep);
				if (rep.crr_rc != 0) {
					rc = M0_ERR(rep.crr_rc);
					break;
				}
			}
			break;
		case M0_CLOVIS_IC_NEXT:
			kvs_next_reply_copy(creq, oi->oi_keys, oi->oi_vals);
			break;
		default:
			M0_IMPOSSIBLE("Invalid op code");
		}
	}

	casreq_completed_post(kvs_req, rc);
	return false;
}

static void kvs_req_immed_failure(struct kvs_req *req, int rc)
{
	M0_ENTRY();
	M0_PRE(rc != 0);
	m0_clink_del(&req->idr_clink);
	casreq_completed_post(req, rc);
	M0_LEAVE();
}

static void kvs_req_exec(struct kvs_req  *req,
			 void           (*exec_fn)(struct m0_sm_group *grp,
				                   struct m0_sm_ast   *ast))
{
	M0_ENTRY();
	req->idr_ast.sa_cb = exec_fn;
	req->idr_ast.sa_datum = req;
	m0_sm_ast_post(req->idr_oi->oi_sm_grp, &req->idr_ast);
	M0_LEAVE();
}

static void kvs_index_create_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	m0_clink_add(&creq->ccr_sm.sm_chan, &kvs_req->idr_clink);
	rc = m0_cas_index_create(creq, OI_IFID(oi), 1, NULL);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_index_create(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_index_create_ast);
	return 1;
}

static void kvs_index_delete_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	m0_clink_add(&creq->ccr_sm.sm_chan, &kvs_req->idr_clink);
	rc = m0_cas_index_delete(creq, OI_IFID(oi), 1, NULL);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_index_delete(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_index_delete_ast);
	return 1;
}

static void kvs_index_lookup_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	m0_clink_add(&creq->ccr_sm.sm_chan, &kvs_req->idr_clink);
	rc = m0_cas_index_lookup(creq, OI_IFID(oi), 1);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_index_lookup(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_index_lookup_ast);
	return 1;
}

static void kvs_index_list_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	m0_clink_add(&creq->ccr_sm.sm_chan, &kvs_req->idr_clink);
	rc = m0_cas_index_list(creq, OI_IFID(oi), oi->oi_keys->ov_vec.v_nr);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_index_list(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_index_list_ast);
	return 1;
}

static void kvs_creq_prepare(struct kvs_req          *req,
			     struct m0_cas_id        *id,
			     struct m0_clovis_op_idx *oi)
{
	id->ci_fid = *OI_IFID(oi);
	m0_clink_add(&req->idr_creq.ccr_sm.sm_chan, &req->idr_clink);
}

static void kvs_put_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_id         idx;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	kvs_creq_prepare(kvs_req, &idx, oi);
	rc = m0_cas_put(creq, &idx, oi->oi_keys, oi->oi_vals, NULL);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_put(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_put_ast);
	return 1;
}

static void kvs_get_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_id         idx;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	kvs_creq_prepare(kvs_req, &idx, oi);
	rc = m0_cas_get(creq, &idx, oi->oi_keys);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_get(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	M0_ASSERT_INFO(oi->oi_keys->ov_vec.v_nr != 0,
		       "At least one key should be specified");
	M0_ASSERT_INFO(!m0_exists(i, oi->oi_keys->ov_vec.v_nr,
			          oi->oi_keys->ov_buf[i] == NULL),
		       "NULL key is not allowed");
	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_get_ast);
	return 1;
}

static void kvs_del_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_id         idx;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	int                      rc;

	M0_ENTRY();
	kvs_creq_prepare(kvs_req, &idx, oi);
	rc = m0_cas_del(creq, &idx, oi->oi_keys, NULL);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_del(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_del_ast);
	return 1;
}

static void kvs_next_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct kvs_req          *kvs_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = kvs_req->idr_oi;
	struct m0_cas_id         idx;
	struct m0_cas_req       *creq = &kvs_req->idr_creq;
	m0_bcount_t              ksize;
	struct m0_bufvec        *start_key = &kvs_req->idr_start_key;
	int                      rc;

	M0_ENTRY();
	kvs_creq_prepare(kvs_req, &idx, oi);
	/**
	 * @todo Currently there is no way to pass several starting keys
	 * along with number of consecutive records for each key through
	 * m0_clovis_idx_op().
	 */
	ksize = oi->oi_keys->ov_vec.v_count[0];
	if (ksize == 0) {
		M0_ASSERT(oi->oi_keys->ov_buf[0] == NULL);
		/*
		 * Request records from the index beginning. Use the smallest
		 * key (1-byte zero key).
		 */
		m0_bufvec_alloc(start_key, 1, sizeof(uint8_t));
		*(uint8_t *)start_key->ov_buf[0] = 0;
	} else {
		m0_bufvec_alloc(start_key, 1, ksize);
		memcpy(start_key->ov_buf[0], oi->oi_keys->ov_buf[0], ksize);
	}
	rc = m0_cas_next(creq, &idx, start_key, &oi->oi_keys->ov_vec.v_nr,
			 true);
	if (rc != 0)
		kvs_req_immed_failure(kvs_req, M0_ERR(rc));
	M0_LEAVE();
}

static int kvs_next(struct m0_clovis_op_idx *oi)
{
	struct kvs_req *req;
	int             rc;

	rc = kvs_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	kvs_req_exec(req, kvs_next_ast);
	return 1;
}

static struct m0_clovis_idx_query_ops kvs_query_ops = {
	.iqo_namei_create = kvs_index_create,
	.iqo_namei_delete = kvs_index_delete,
	.iqo_namei_lookup = kvs_index_lookup,
	.iqo_namei_list   = kvs_index_list,

	.iqo_get          = kvs_get,
	.iqo_put          = kvs_put,
	.iqo_del          = kvs_del,
	.iqo_next         = kvs_next,
};

/**-------------------------------------------------------------------------*
 *               Index backend Initialisation and Finalisation              *
 *--------------------------------------------------------------------------*/

static int kvs_init(void *svc)
{
	struct m0_clovis_idx_service_ctx *ctx;

	M0_ENTRY();
	ctx  = (struct m0_clovis_idx_service_ctx *)svc;
	M0_ASSERT(ctx->isc_svc_conf == NULL);
	M0_ASSERT(ctx->isc_svc_inst == NULL);
	return M0_RC(0);
}

static int kvs_fini(void *svc)
{
	struct m0_clovis_idx_service_ctx *ctx;

	M0_ENTRY();
	ctx  = (struct m0_clovis_idx_service_ctx *)svc;
	M0_ASSERT(ctx->isc_svc_inst == NULL);
	M0_ASSERT(ctx->isc_svc_conf == NULL);
	return M0_RC(0);
}

static struct m0_clovis_idx_service_ops kvs_svc_ops = {
	.iso_init = kvs_init,
	.iso_fini = kvs_fini
};

M0_INTERNAL void m0_clovis_idx_kvs_register(void)
{
	m0_clovis_idx_service_register(M0_CLOVIS_IDX_MERO, &kvs_svc_ops,
				       &kvs_query_ops);

}

#undef M0_TRACE_SUBSYSTEM

/** @} end of clovis-index-kvs group */

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
