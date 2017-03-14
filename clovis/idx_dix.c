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
#include "dix/fid_convert.h"
#include "dix/meta.h"
#include "dix/client.h"

/**
 * @addtogroup clovis-index-dix
 *
 * @{
 */

#define OI_IFID(oi) (struct m0_fid *)&(oi)->oi_idx->in_entity.en_id

static bool dixreq_clink_cb(struct m0_clink *cl);
static bool dix_meta_req_clink_cb(struct m0_clink *cl);

struct dix_inst {
	struct m0_dix_cli di_dixc;
	/**
	 * Default mero pool version, where all distributed indices are created.
	 */
	/**
	 * @todo This field is temporary. Clovis interface should be extended,
	 * so user can define pool version for index. Use constant pool version
	 * until this is done. Actually, this pool version equals to the one
	 * used for root index, which is defined in filesystem configuration
	 * object.
	 */
	struct m0_fid     di_index_pver;
};

struct dix_req {
	struct m0_clovis_op_idx *idr_oi;
	struct m0_sm_ast         idr_ast;
	struct m0_clink          idr_clink;
	/**
	 * Starting key for NEXT operation.
	 * It's allocated internally and key value is copied from user.
	 */
	struct m0_bufvec         idr_start_key;
	struct m0_dix_req        idr_dreq;
	/** DIX meta-request for index list operation. */
	struct m0_dix_meta_req   idr_mreq;
};

static struct dix_inst *dix_inst(const struct m0_clovis_op_idx *oi)
{
	struct m0_clovis *m0c;

	m0c = m0_clovis__entity_instance(oi->oi_oc.oc_op.op_entity);
	return (struct dix_inst *)m0c->m0c_idx_svc_ctx.isc_svc_inst;
}

static struct m0_dix_cli *op_dixc(const struct m0_clovis_op_idx *oi)
{
	return &dix_inst(oi)->di_dixc;
}

static void dix_build(const struct m0_clovis_op_idx *oi,
		      struct m0_dix                 *out)
{
	M0_SET0(out);
	out->dd_fid = *OI_IFID(oi);
}

static int dix_mreq_create(struct m0_clovis_op_idx  *oi,
			   struct dix_req          **out)
{
	struct dix_req *req;

	M0_ALLOC_PTR(req);
	if (req == NULL)
		return M0_ERR(-ENOMEM);
	m0_dix_meta_req_init(&req->idr_mreq, op_dixc(oi), oi->oi_sm_grp);
	m0_clink_init(&req->idr_clink, dix_meta_req_clink_cb);
	req->idr_oi = oi;
	*out = req;
	return M0_RC(0);
}

static int dix_req_create(struct m0_clovis_op_idx  *oi,
			  struct dix_req          **out)
{
	struct dix_req *req;
	int             rc = 0;

	M0_ALLOC_PTR(req);
	if (req != NULL) {
		m0_dix_req_init(&req->idr_dreq, op_dixc(oi), oi->oi_sm_grp);
		m0_clink_init(&req->idr_clink, dixreq_clink_cb);
		req->idr_oi = oi;
		*out = req;
	} else
		rc = M0_ERR(-ENOMEM);
	return M0_RC(rc);
}

static void dix_req_destroy(struct dix_req *req)
{
	M0_ENTRY();
	m0_clink_fini(&req->idr_clink);
	m0_bufvec_free(&req->idr_start_key);
	if (M0_IN(req->idr_oi->oi_oc.oc_op.op_code,
		  (M0_CLOVIS_IC_LIST, M0_CLOVIS_IC_LOOKUP)))
		m0_dix_meta_req_fini(&req->idr_mreq);
	else
		m0_dix_req_fini(&req->idr_dreq);
	m0_free(req);
	M0_LEAVE();
}

static void dixreq_completed_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = req->idr_oi;
	int                      rc = oi->oi_ar.ar_rc;

	M0_ENTRY();
	oi->oi_ar.ar_ast.sa_cb = (rc == 0) ? clovis_idx_op_ast_complete :
					     clovis_idx_op_ast_fail;
	m0_sm_ast_post(oi->oi_sm_grp, &oi->oi_ar.ar_ast);
	dix_req_destroy(req);
	M0_LEAVE();
}

static void dixreq_completed_post(struct dix_req *req, int rc)
{
	struct m0_clovis_op_idx *oi = req->idr_oi;

	M0_ENTRY();
	oi->oi_ar.ar_rc = rc;
	req->idr_ast.sa_cb = dixreq_completed_ast;
	req->idr_ast.sa_datum = req;
	m0_sm_ast_post(oi->oi_sm_grp, &req->idr_ast);
	M0_LEAVE();
}

static int dix_list_reply_copy(struct m0_dix_meta_req *req,
			       int32_t                *rcs,
			       struct m0_bufvec       *bvec)
{
	uint64_t rep_count = m0_dix_index_list_rep_nr(req);
	uint64_t i;

	/* Assertion is guaranteed by DIX/CAS client. */
	M0_PRE(bvec->ov_vec.v_nr >= rep_count);
	for (i = 0; i < rep_count; i++) {
		/* User should allocate buffer of appropriate size */
		M0_ASSERT(bvec->ov_vec.v_count[i] == sizeof(struct m0_fid));
		rcs[i] = m0_dix_index_list_rep(req, i,
				(struct m0_fid *)bvec->ov_buf[i]);
	}

	/*
	 * If number of listed indices is less than was requested by user, then
	 * there are no more indices to list. Fill tail of return codes array
	 * with -ENOENT for non-existing indices.
	 */
	for (i = rep_count; i < bvec->ov_vec.v_nr; i++)
		rcs[i] = -ENOENT;
	return rep_count > 0 ? M0_RC(0) : M0_ERR(-ENODATA);
}

static void dix_get_reply_copy(struct m0_dix_req *dreq,
			       int32_t           *rcs,
			       struct m0_bufvec  *bvec)
{
	uint64_t                rep_count = m0_dix_req_nr(dreq);
	struct m0_dix_get_reply rep;
	uint64_t                i;

	/* Assertion is guaranteed by DIX client. */
	M0_PRE(bvec->ov_vec.v_nr >= rep_count);
	for (i = 0; i < rep_count; i++) {
		m0_dix_get_rep(dreq, i, &rep);
		M0_ASSERT(bvec->ov_vec.v_count[i] == 0);
		M0_ASSERT(bvec->ov_buf[i] == NULL);
		rcs[i] = rep.dgr_rc;
		if (rep.dgr_rc == 0) {
			m0_dix_get_rep_mlock(dreq, i);
			bvec->ov_vec.v_count[i] = rep.dgr_val.b_nob;
			bvec->ov_buf[i] = rep.dgr_val.b_addr;
		}
	}
}

static void dix_next_reply_copy(struct m0_dix_req *req,
				int32_t           *rcs,
				struct m0_bufvec  *keys,
				struct m0_bufvec  *vals)
{
	uint64_t                 rep_count = m0_dix_next_rep_nr(req, 0);
	struct m0_dix_next_reply rep;
	uint64_t                 i;
	uint64_t                 k = 0;

	/* Assertions are guaranteed by DIX client. */
	M0_PRE(keys->ov_vec.v_nr >= rep_count);
	M0_PRE(vals->ov_vec.v_nr >= rep_count);
	for (i = 0; i < rep_count; i++) {
		rcs[i] = 0;
		m0_dix_next_rep(req, 0, i, &rep);
		m0_dix_next_rep_mlock(req, 0, i);
		keys->ov_vec.v_count[k] = rep.dnr_key.b_nob;
		keys->ov_buf[k] = rep.dnr_key.b_addr;
		vals->ov_vec.v_count[k] = rep.dnr_val.b_nob;
		vals->ov_buf[k] = rep.dnr_val.b_addr;
		k++;
	}
	/*
	 * If number of retrieved records is less than was requested by user,
	 * then there are no more records in the index. Fill tail of return
	 * codes array with -ENOENT for non-existing records.
	 */
	for (i = rep_count; i < keys->ov_vec.v_nr; i++)
		rcs[i] = -ENOENT;
}

static bool dix_meta_req_clink_cb(struct m0_clink *cl)
{
	struct dix_req          *dix_req = container_of(cl, struct dix_req,
							idr_clink);
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix_meta_req  *mreq = &dix_req->idr_mreq;
	struct m0_clovis_op     *op;
	int                      rc;

	m0_clink_del(cl);
	op = &oi->oi_oc.oc_op;
	M0_ASSERT(M0_IN(op->op_code,(M0_CLOVIS_IC_LIST, M0_CLOVIS_IC_LOOKUP)));
	rc = m0_dix_meta_generic_rc(mreq) ?:
		(op->op_code == M0_CLOVIS_IC_LIST) ?
			dix_list_reply_copy(mreq, oi->oi_rcs, oi->oi_keys) :
			m0_dix_layout_rep_get(mreq, 0, NULL);
	dixreq_completed_post(dix_req, rc);
	return false;
}

static bool dixreq_clink_cb(struct m0_clink *cl)
{
	struct dix_req          *dix_req = container_of(cl, struct dix_req,
							idr_clink);
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_sm            *req_sm = container_of(cl->cl_chan,
						       struct m0_sm, sm_chan);
	struct m0_dix_req       *dreq = container_of(req_sm, struct m0_dix_req,
						     dr_sm);
	uint32_t                 state = dreq->dr_sm.sm_state;
	struct m0_clovis_op     *op;
	int                      i;
	int                      rc;

	if (!M0_IN(state, (DIXREQ_FAILURE, DIXREQ_FINAL)))
		return false;

	m0_clink_del(cl);
	op = &oi->oi_oc.oc_op;

	rc = m0_dix_generic_rc(dreq);
	if (rc == 0) {
		/*
		 * Response from CAS service is validated by CAS/DIX client,
		 * including number of records in response.
		 */
		switch (op->op_code) {
		case M0_CLOVIS_EO_CREATE:
		case M0_CLOVIS_EO_DELETE:
			M0_ASSERT(m0_dix_req_nr(dreq) == 1);
			rc = m0_dix_item_rc(dreq, 0);
			break;
		case M0_CLOVIS_IC_PUT:
		case M0_CLOVIS_IC_DEL:
			for (i = 0; i < m0_dix_req_nr(dreq); i++)
				oi->oi_rcs[i] = m0_dix_item_rc(dreq, i);
			break;
		case M0_CLOVIS_IC_GET:
			dix_get_reply_copy(dreq, oi->oi_rcs, oi->oi_vals);
			break;
		case M0_CLOVIS_IC_NEXT:
			dix_next_reply_copy(dreq, oi->oi_rcs, oi->oi_keys,
					    oi->oi_vals);
			break;
		default:
			M0_IMPOSSIBLE("Invalid op code");
		}
	}

	dixreq_completed_post(dix_req, rc);
	return false;
}

static void dix_req_immed_failure(struct dix_req *req, int rc)
{
	M0_ENTRY();
	M0_PRE(rc != 0);
	m0_clink_del(&req->idr_clink);
	dixreq_completed_post(req, rc);
	M0_LEAVE();
}

static void dix_req_exec(struct dix_req  *req,
			 void           (*exec_fn)(struct m0_sm_group *grp,
				                   struct m0_sm_ast   *ast))
{
	M0_ENTRY();
	req->idr_ast.sa_cb = exec_fn;
	req->idr_ast.sa_datum = req;
	m0_sm_ast_post(req->idr_oi->oi_sm_grp, &req->idr_ast);
	M0_LEAVE();
}

static void dix_index_create_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix_req       *dreq = &dix_req->idr_dreq;
	struct m0_dix            dix;
	int                      rc;

	M0_ENTRY();
	dix_build(oi, &dix);
	/*
	 * Use default layout for all indices:
	 * - city hash function;
	 * - infinity identity mask (use key as is);
	 * - default pool version (the same as for root index).
	 * In future clovis user will be able to pass layout as an argument.
	 */
	dix.dd_layout.dl_type = DIX_LTYPE_DESCR;
	m0_dix_ldesc_init(&dix.dd_layout.u.dl_desc,
			  &(struct m0_ext) { .e_start = 0, .e_end = IMASK_INF },
			  1, HASH_FNC_CITY, &dix_inst(oi)->di_index_pver);
	m0_clink_add(&dreq->dr_sm.sm_chan, &dix_req->idr_clink);
	rc = m0_dix_create(dreq, &dix, 1, NULL);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	m0_dix_fini(&dix);
	M0_LEAVE();
}

static bool dix_iname_args_are_valid(const struct m0_clovis_op_idx *oi)
{
	return m0_dix_fid_validate_dix(OI_IFID(oi));
}

static int dix_index_create(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	M0_ASSERT(dix_iname_args_are_valid(oi));

	rc = dix_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_index_create_ast);
	return 1;
}

static void dix_index_delete_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix_req       *dreq = &dix_req->idr_dreq;
	struct m0_dix            dix;
	int                      rc;

	M0_ENTRY();
	dix_build(oi, &dix);
	m0_clink_add(&dreq->dr_sm.sm_chan, &dix_req->idr_clink);
	rc = m0_dix_delete(dreq, &dix, 1, NULL);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_index_delete(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	M0_ASSERT(dix_iname_args_are_valid(oi));
	rc = dix_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_index_delete_ast);
	return 1;
}

static void dix_index_lookup_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix_meta_req  *mreq = &dix_req->idr_mreq;
	int                      rc;

	M0_ENTRY();
	m0_clink_add_lock(&mreq->dmr_chan, &dix_req->idr_clink);
	rc = m0_dix_layout_get(mreq, OI_IFID(oi), 1);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_index_lookup(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	M0_ASSERT(dix_iname_args_are_valid(oi));
	rc = dix_mreq_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_index_lookup_ast);
	return 1;
}

static void dix_index_list_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix            dix;
	struct m0_dix_meta_req  *mreq = &dix_req->idr_mreq;
	int                      rc;

	M0_ENTRY();
	dix_build(oi, &dix);
	m0_clink_add_lock(&mreq->dmr_chan, &dix_req->idr_clink);
	rc = m0_dix_index_list(mreq, OI_IFID(oi), oi->oi_keys->ov_vec.v_nr);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_index_list(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	M0_ASSERT(dix_iname_args_are_valid(oi));
	rc = dix_mreq_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_index_list_ast);
	return 1;
}

static void dix_dreq_prepare(struct dix_req          *req,
			     struct m0_dix           *dix,
			     struct m0_clovis_op_idx *oi)
{
	dix_build(oi, dix);
	m0_clink_add(&req->idr_dreq.dr_sm.sm_chan, &req->idr_clink);
}

static void dix_put_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix            dix;
	struct m0_dix_req       *dreq = &dix_req->idr_dreq;
	uint32_t                 flags = 0;
	int                      rc;

	M0_ENTRY();
	dix_dreq_prepare(dix_req, &dix, oi);
	if (oi->oi_flags & M0_OIF_OVERWRITE)
		flags = COF_OVERWRITE;
	rc = m0_dix_put(dreq, &dix, oi->oi_keys, oi->oi_vals, NULL, flags);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_put(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	rc = dix_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_put_ast);
	return 1;
}

static void dix_get_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix            dix;
	struct m0_dix_req       *dreq = &dix_req->idr_dreq;
	int                      rc;

	M0_ENTRY();
	dix_dreq_prepare(dix_req, &dix, oi);
	rc = m0_dix_get(dreq, &dix, oi->oi_keys);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_get(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	M0_ASSERT_INFO(oi->oi_keys->ov_vec.v_nr != 0,
		       "At least one key should be specified");
	M0_ASSERT_INFO(!m0_exists(i, oi->oi_keys->ov_vec.v_nr,
			          oi->oi_keys->ov_buf[i] == NULL),
		       "NULL key is not allowed");
	rc = dix_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_get_ast);
	return 1;
}

static void dix_del_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	struct m0_dix            dix;
	struct m0_dix_req       *dreq = &dix_req->idr_dreq;
	int                      rc;

	M0_ENTRY();
	dix_dreq_prepare(dix_req, &dix, oi);
	rc = m0_dix_del(dreq, &dix, oi->oi_keys, NULL);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_del(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	rc = dix_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_del_ast);
	return 1;
}

static void dix_next_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct dix_req          *dix_req = ast->sa_datum;
	struct m0_clovis_op_idx *oi = dix_req->idr_oi;
	m0_bcount_t              ksize;
	struct m0_bufvec        *start_key = &dix_req->idr_start_key;
	struct m0_dix            dix;
	struct m0_dix_req       *dreq = &dix_req->idr_dreq;
	int                      rc;

	M0_ENTRY();
	dix_dreq_prepare(dix_req, &dix, oi);
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
	rc = m0_dix_next(dreq, &dix, start_key, &oi->oi_keys->ov_vec.v_nr);
	if (rc != 0)
		dix_req_immed_failure(dix_req, M0_ERR(rc));
	M0_LEAVE();
}

static int dix_next(struct m0_clovis_op_idx *oi)
{
	struct dix_req *req;
	int             rc;

	rc = dix_req_create(oi, &req);
	if (rc != 0)
		return M0_ERR(rc);
	dix_req_exec(req, dix_next_ast);
	return 1;
}

static struct m0_clovis_idx_query_ops dix_query_ops = {
	.iqo_namei_create = dix_index_create,
	.iqo_namei_delete = dix_index_delete,
	.iqo_namei_lookup = dix_index_lookup,
	.iqo_namei_list   = dix_index_list,

	.iqo_get          = dix_get,
	.iqo_put          = dix_put,
	.iqo_del          = dix_del,
	.iqo_next         = dix_next,
};

/**-------------------------------------------------------------------------*
 *               Index backend Initialisation and Finalisation              *
 *--------------------------------------------------------------------------*/

static int dix_root_idx_pver(struct m0_clovis *m0c, struct m0_fid *out)
{
	struct m0_reqh            *reqh = &m0c->m0c_reqh;
	struct m0_conf_filesystem *fs;
	int                        rc;

	/* Clovis will release the lock on confc before calling idx_dix_init() */
	rc = m0_conf_fs_get(&reqh->rh_profile, m0_reqh2confc(reqh), &fs);
	if (rc != 0)
		return M0_ERR(rc);
	*out = fs->cf_imeta_pver;
	m0_confc_close(&fs->cf_obj);

	return 0;
}

static int dix_client_init(struct dix_inst          *inst,
			   struct m0_clovis         *m0c,
		           struct m0_idx_dix_config *config)
{
	struct m0_dix_cli  *dixc = &inst->di_dixc;
	struct m0_sm_group *grp  = m0_locality0_get()->lo_grp;
	struct m0_fid       root_pver;
	int                 rc;

	/*
	 * dix_init() is called from clovis initialisation machine.
	 * DIX client start is synchronoush operation that uses it's own
	 * machine internally. These two state machines should belong to
	 * different state machine groups, otherwise DIX client start will hang.
	 */
	M0_ASSERT(grp != m0c->m0c_initlift_sm.sm_grp);

	rc = dix_root_idx_pver(m0c, &root_pver) ?:
	     m0_dix_cli_init(dixc, grp, &m0c->m0c_pools_common,
			     &m0c->m0c_reqh.rh_ldom, &root_pver);
	if (rc != 0)
		return M0_ERR(rc);

	if (config->kc_create_meta) {
		m0_dix_cli_bootstrap_lock(dixc);
		rc = m0_dix_meta_create(dixc, grp, &config->kc_layout_ldesc,
					&config->kc_ldescr_ldesc);
		if (rc != 0) {
			m0_dix_cli_stop_lock(dixc);
			goto cli_fini;
		}
	}

	rc = m0_dix_cli_start_sync(&inst->di_dixc);
	if (rc != 0)
		goto cli_fini;

	/*
	 * Use pool version of root index as default pool version for all
	 * distributed indices. It is temporary until clovis interface is
	 * extended to allow user providing pool version for new indices.
	 */
	inst->di_index_pver = root_pver;
	return M0_RC(0);

cli_fini:
	m0_dix_cli_fini_lock(&inst->di_dixc);
	return M0_ERR(rc);
}

static int idx_dix_init(void *svc)
{
	struct m0_clovis_idx_service_ctx *ctx;
	struct dix_inst                  *inst;
	struct m0_clovis                 *m0c;
	int                               rc;

	M0_ENTRY();
	ctx = (struct m0_clovis_idx_service_ctx *)svc;
	M0_PRE(ctx->isc_svc_conf != NULL);

	M0_ALLOC_PTR(inst);
	if (inst == NULL)
		return M0_ERR(-ENOMEM);
	m0c = container_of(ctx, struct m0_clovis, m0c_idx_svc_ctx);

	rc = dix_client_init(inst, m0c,
			(struct m0_idx_dix_config *)ctx->isc_svc_conf);
	if (rc != 0) {
		m0_free(inst);
		return M0_ERR(rc);
	}
	ctx->isc_svc_inst = inst;

	M0_POST(ctx->isc_svc_inst != NULL);
	return M0_RC(0);
}

static int idx_dix_fini(void *svc)
{
	struct m0_clovis_idx_service_ctx *ctx;
	struct dix_inst                  *inst;

	M0_ENTRY();
	ctx = (struct m0_clovis_idx_service_ctx *)svc;
	M0_PRE(ctx->isc_svc_inst != NULL);
	inst = ctx->isc_svc_inst;
	m0_dix_cli_stop_lock(&inst->di_dixc);
	m0_dix_cli_fini_lock(&inst->di_dixc);
	m0_free0(&inst);
	return M0_RC(0);
}

static struct m0_clovis_idx_service_ops dix_svc_ops = {
	.iso_init = idx_dix_init,
	.iso_fini = idx_dix_fini
};

M0_INTERNAL void m0_clovis_idx_dix_register(void)
{
	m0_clovis_idx_service_register(M0_CLOVIS_IDX_DIX, &dix_svc_ops,
				       &dix_query_ops);

}

#undef M0_TRACE_SUBSYSTEM

/** @} end of clovis-index-dix group */

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
