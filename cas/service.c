/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 26-Feb-2016
 */

/**
 * @addtogroup cas
 *
 * Catalogue service.
 *
 * @see https://docs.google.com/document/d/1Zhw1BVHZOFn-x2B8Yay1hZ0guTT5KFnpIA5gT3oaCXI/edit (HLD)
 *
 * Catalogue service exports BE btrees (be/btree.[ch]) to the network.
 *
 * @verbatim
 *                                !cas_is_valid()
 *                      FOPH_INIT----------------->FAILURE
 *                          |
 *                          |
 *                   [generic phases]
 *                          .
 *                          .
 *                          .
 *                          V                             meta_op
 *                       TXN_INIT-------------->CAS_START-----------+
 *                                                  |               |
 *                                                  V               |
 *                       TXN_OPEN<-----+      CAS_META_LOCK         |
 *                          |          |            |               |
 *                          |          |            V               |
 *                   [generic phases]  |     CAS_META_LOOKUP        |
 *                          .          |            |               |
 *                          .          |            V               |
 *                          .          |   CAS_META_LOOKUP_DONE     |
 *              alldone     V          |            |               |
 *     SUCCESS<---------CAS_LOOP<---+  |            V               |
 *                          |       |  |         CAS_LOCK           |
 *                          |       |  |            |               |
 *                          V       |  |            V               |
 *                      CAS_DONE----+  +---------CAS_PREP<----------+
 * @endverbatim
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CAS

#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "lib/assert.h"
#include "lib/arith.h"               /* min_check, M0_3WAY */
#include "lib/misc.h"                /* M0_IN */
#include "lib/errno.h"               /* ENOMEM, EPROTO */
#include "be/btree.h"
#include "be/domain.h"               /* m0_be_domain_seg0_get */
#include "be/tx_credit.h"
#include "be/op.h"
#include "fop/fom_long_lock.h"
#include "fop/fom_generic.h"
#include "format/format.h"
#include "reqh/reqh_service.h"
#include "rpc/rpc_opcodes.h"
#include "conf/schema.h"             /* M0_CST_CAS */
#include "module/instance.h"

#include "cas/cas.h"
#include "cas/cas_xc.h"

struct cas_index {
	struct m0_format_header ci_head;
	struct m0_be_btree      ci_tree;
	struct m0_format_footer ci_foot;
	struct m0_long_lock     ci_lock;
};

struct cas_service {
	struct m0_reqh_service  c_service;
	struct cas_index       *c_meta;
};

struct cas_fom {
	struct m0_fom             cf_fom;
	size_t                    cf_ipos;
	size_t                    cf_opos;
	struct cas_index         *cf_index;
	struct m0_long_lock_link  cf_lock;
	struct m0_long_lock_link  cf_meta;
	/**
	 * BE operation structure for b-tree operations, except for
	 * CO_CUR. Cursor operations use cas_fom::cf_cur.
	 *
	 * @note cas_fom::cf_cur has its own m0_be_op. It could be used for all
	 * operations, but it is marked deprecated in btree.h.
	 */
	struct m0_be_op           cf_beop;
	struct m0_buf             cf_buf;
	struct m0_be_btree_anchor cf_anchor;
	struct m0_be_btree_cursor cf_cur;
	uint64_t                  cf_curpos;
};

enum cas_fom_phase {
	CAS_LOOP = M0_FOPH_TYPE_SPECIFIC,
	CAS_DONE,

	CAS_START,
	CAS_META_LOCK,
	CAS_META_LOOKUP,
	CAS_META_LOOKUP_DONE,
	CAS_LOCK,
	CAS_PREP,
	CAS_NR
};

enum cas_opcode {
	CO_GET,
	CO_PUT,
	CO_DEL,
	CO_CUR,
	CO_REP,
	CO_NR
};

M0_BASSERT(M0_CAS_GET_FOP_OPCODE == CO_GET + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_PUT_FOP_OPCODE == CO_PUT + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_DEL_FOP_OPCODE == CO_DEL + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_CUR_FOP_OPCODE == CO_CUR + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_REP_FOP_OPCODE == CO_REP + M0_CAS_GET_FOP_OPCODE);

enum cas_type {
	CT_META,
	CT_BTREE
};

static int    cas_service_start        (struct m0_reqh_service *service);
static void   cas_service_stop         (struct m0_reqh_service *service);
static void   cas_service_fini         (struct m0_reqh_service *service);
static size_t cas_fom_home_locality    (const struct m0_fom *fom);
static int    cas_service_type_allocate(struct m0_reqh_service **service,
					const struct m0_reqh_service_type *st);

static struct m0_be_seg    *cas_seg    (void);
static struct m0_cas_op    *cas_op     (const struct m0_fom *fom);
static const struct m0_fid *cas_fid    (const struct m0_fom *fom);
static enum cas_type        cas_type   (const struct m0_fom *fom);
static int                  cas_buf_get(struct m0_buf *dst,
					const struct m0_buf *src);
static int                  cas_buf    (const struct m0_buf *src,
					struct m0_buf *buf);
static void                 cas_release(struct cas_fom *fom,
					struct m0_fom *fom0);
static struct m0_cas_rec   *cas_at     (struct m0_cas_op *op, int idx);
static bool                 cas_is_ro  (enum cas_opcode opc);
static enum cas_opcode      cas_opcode (const struct m0_fop *fop);
static uint64_t             cas_nr     (const struct m0_fop *fop);
static struct m0_be_op     *cas_beop   (struct cas_fom *fom);
static int                  cas_berc   (struct cas_fom *fom);
static int                  cas_place  (struct m0_buf *dst, struct m0_buf *src);
static m0_bcount_t          cas_ksize  (const void *key);
static m0_bcount_t          cas_vsize  (const void *val);
static int                  cas_cmp    (const void *key0, const void *key1);
static bool                 cas_in_ut  (void);
static int                  cas_lookup (struct cas_fom *fom,
					struct cas_index *index,
					const struct m0_buf *key, int next);
static void                 cas_prep   (enum cas_opcode opc, enum cas_type ct,
					struct cas_index *index,
					const struct m0_cas_rec *rec,
					struct m0_be_tx_credit *accum);
static int                  cas_exec   (struct cas_fom *fom,
					enum cas_opcode opc, enum cas_type ct,
					struct cas_index *index,
					const struct m0_cas_rec *rec, int next);
static void                 cas_done   (struct cas_fom *fom,
					enum cas_opcode opc, enum cas_type ct,
					const struct m0_cas_rec *rec,
					uint64_t rc, struct m0_cas_rec *out);
static int                  cas_init   (struct cas_service *service);

static bool cas_is_valid    (enum cas_opcode opc, enum cas_type ct,
			     const struct m0_cas_rec *rec);
static void cas_index_init  (struct cas_index *index, struct m0_be_seg *seg);
static int  cas_index_create(struct cas_index *index, struct m0_be_tx *tx);
static bool cas_fom_invariant(const struct cas_fom *fom);

static const struct m0_reqh_service_ops      cas_service_ops;
static const struct m0_reqh_service_type_ops cas_service_type_ops;
static const struct m0_fom_ops               cas_fom_ops;
static const struct m0_fom_type_ops          cas_fom_type_ops;
static       struct m0_sm_conf               cas_sm_conf;
static       struct m0_sm_state_descr        cas_fom_phases[];
static const struct m0_fom_type_ops          cas_fom_type_ops;
static const struct m0_be_btree_kv_ops       cas_btree_ops;

M0_INTERNAL struct m0_fop_type cas_get_fopt;
M0_INTERNAL struct m0_fop_type cas_put_fopt;
M0_INTERNAL struct m0_fop_type cas_del_fopt;
M0_INTERNAL struct m0_fop_type cas_cur_fopt;
M0_INTERNAL struct m0_fop_type cas_rep_fopt;

static const char cas_key[] = "cas-meta";

M0_INTERNAL int m0_cas_module_init(void)
{
	m0_fid_type_register(&m0_cas_index_fid_type);
	m0_sm_conf_extend(m0_generic_conf.scf_state, cas_fom_phases,
			  m0_generic_conf.scf_nr_states);
	m0_sm_conf_trans_extend(&m0_generic_conf, &cas_sm_conf);
	cas_fom_phases[M0_FOPH_TXN_OPEN].sd_allowed |= M0_BITS(CAS_START);
	cas_fom_phases[M0_FOPH_QUEUE_REPLY].sd_allowed |=
		M0_BITS(M0_FOPH_TXN_COMMIT_WAIT);
	m0_sm_conf_init(&cas_sm_conf);
	M0_FOP_TYPE_INIT(&cas_get_fopt,
			 .name      = "cas-get",
			 .opcode    = M0_CAS_GET_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = &cas_fom_type_ops,
			 .sm        = &cas_sm_conf,
			 .svc_type  = &m0_cas_service_type);
	M0_FOP_TYPE_INIT(&cas_put_fopt,
			 .name      = "cas-put",
			 .opcode    = M0_CAS_PUT_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
				      M0_RPC_ITEM_TYPE_MUTABO,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = &cas_fom_type_ops,
			 .sm        = &cas_sm_conf,
			 .svc_type  = &m0_cas_service_type);
	M0_FOP_TYPE_INIT(&cas_del_fopt,
			 .name      = "cas-del",
			 .opcode    = M0_CAS_DEL_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
				      M0_RPC_ITEM_TYPE_MUTABO,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = &cas_fom_type_ops,
			 .sm        = &cas_sm_conf,
			 .svc_type  = &m0_cas_service_type);
	M0_FOP_TYPE_INIT(&cas_cur_fopt,
			 .name      = "cas-cur",
			 .opcode    = M0_CAS_CUR_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = &cas_fom_type_ops,
			 .sm        = &cas_sm_conf,
			 .svc_type  = &m0_cas_service_type);
	M0_FOP_TYPE_INIT(&cas_rep_fopt,
			 .name      = "cas-rep",
			 .opcode    = M0_CAS_REP_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .xt        = m0_cas_rep_xc,
			 .svc_type  = &m0_cas_service_type);
	return  m0_fop_type_addb2_instrument(&cas_get_fopt) ?:
		m0_fop_type_addb2_instrument(&cas_put_fopt) ?:
		m0_fop_type_addb2_instrument(&cas_del_fopt) ?:
		m0_fop_type_addb2_instrument(&cas_cur_fopt) ?:
		m0_reqh_service_type_register(&m0_cas_service_type);
}

M0_INTERNAL void m0_cas_module_fini(void)
{
	m0_reqh_service_type_unregister(&m0_cas_service_type);
	m0_fop_type_addb2_deinstrument(&cas_cur_fopt);
	m0_fop_type_addb2_deinstrument(&cas_del_fopt);
	m0_fop_type_addb2_deinstrument(&cas_put_fopt);
	m0_fop_type_addb2_deinstrument(&cas_get_fopt);
	m0_fop_type_fini(&cas_rep_fopt);
	m0_fop_type_fini(&cas_cur_fopt);
	m0_fop_type_fini(&cas_del_fopt);
	m0_fop_type_fini(&cas_put_fopt);
	m0_fop_type_fini(&cas_get_fopt);
	m0_sm_conf_fini(&cas_sm_conf);
	m0_fid_type_unregister(&m0_cas_index_fid_type);
}

static int cas_service_start(struct m0_reqh_service *svc)
{
	struct cas_service *service = M0_AMB(service, svc, c_service);

	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STARTING);
	return cas_init(service);
}

static void cas_service_stop(struct m0_reqh_service *svc)
{
	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STOPPED);
}

static void cas_service_fini(struct m0_reqh_service *svc)
{
	struct cas_service *service = M0_AMB(service, svc, c_service);

	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STOPPED);
	m0_free(service);
}

static int cas_service_type_allocate(struct m0_reqh_service **svc,
				     const struct m0_reqh_service_type *stype)
{
	struct cas_service *service;

	M0_ALLOC_PTR(service);
	if (service != NULL) {
		*svc = &service->c_service;
		(*svc)->rs_type = stype;
		(*svc)->rs_ops  = &cas_service_ops;
		return M0_RC(0);
	} else
		return M0_ERR(-ENOMEM);
}

static int cas_fom_create(struct m0_fop *fop,
			  struct m0_fom **out, struct m0_reqh *reqh)
{
	struct cas_fom    *fom;
	struct m0_fom     *fom0;
	struct m0_fop     *repfop;
	struct m0_cas_rep *repdata;
	struct m0_cas_rec *repv;
	uint64_t           nr;

	M0_ALLOC_PTR(fom);
	/**
	 * @todo Validity (cas_is_valid()) of input records is not checked here,
	 * so "nr" can be bogus. Cannot check validity at this point, because
	 * ->fto_create() errors are silently ignored.
	 */
	nr = cas_nr(fop);
	repfop = m0_fop_reply_alloc(fop, &cas_rep_fopt);
	M0_ALLOC_ARR(repv, nr);
	if (fom != NULL && repfop != NULL && repv != NULL) {
		*out = fom0 = &fom->cf_fom;
		repdata = m0_fop_data(repfop);
		repdata->cgr_rep.cr_nr  = nr;
		repdata->cgr_rep.cr_rec = repv;
		m0_fom_init(fom0, &fop->f_type->ft_fom_type,
			    &cas_fom_ops, fop, repfop, reqh);
		m0_long_lock_link_init(&fom->cf_lock, fom0, NULL);
		m0_long_lock_link_init(&fom->cf_meta, fom0, NULL);
		return M0_RC(0);
	} else {
		m0_free(repfop);
		m0_free(repv);
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}
}

static int cas_fom_tick(struct m0_fom *fom0)
{
	int                 i;
	int                 rc;
	int                 result  = M0_FSO_AGAIN;
	struct cas_fom     *fom     = M0_AMB(fom, fom0, cf_fom);
	int                 phase   = m0_fom_phase(fom0);
	struct m0_cas_op   *op      = cas_op(fom0);
	struct m0_cas_rep  *rep     = m0_fop_data(fom0->fo_rep_fop);
	enum cas_opcode     opc     = cas_opcode(fom0->fo_fop);
	enum cas_type       ct      = cas_type(fom0);
	struct cas_index   *index   = fom->cf_index;
	size_t              pos     = fom->cf_ipos;
	struct cas_service *service = M0_AMB(service,
					     fom0->fo_service, c_service);
	struct cas_index   *meta    = service->c_meta;

	M0_PRE(cas_fom_invariant(fom));
	switch (phase) {
	case M0_FOPH_INIT ... M0_FOPH_NR - 1:
		if (phase == M0_FOPH_INIT) {
			if (m0_exists(i, op->cg_rec.cr_nr,
				      !cas_is_valid(opc, ct, cas_at(op, i)))) {
				m0_fom_phase_move(fom0, M0_ERR(-EPROTO),
						  M0_FOPH_FAILURE);
				break;
			}
		} else if (phase == M0_FOPH_FAILURE) {
			m0_long_unlock(&meta->ci_lock, &fom->cf_meta);
			if (fom->cf_index != NULL)
				m0_long_unlock(&fom->cf_index->ci_lock,
					       &fom->cf_lock);
			cas_release(fom, fom0);
			if (fom->cf_cur.bc_tree != NULL)
				m0_be_btree_cursor_fini(&fom->cf_cur);
		}
		result = m0_fom_tick_generic(fom0);
		if (m0_fom_phase(fom0) == M0_FOPH_TXN_OPEN) {
			M0_ASSERT(phase == M0_FOPH_TXN_INIT);
			m0_fom_phase_set(fom0, CAS_START);
		}
		if (cas_in_ut() && m0_fom_phase(fom0) == M0_FOPH_QUEUE_REPLY) {
			M0_ASSERT(phase == M0_FOPH_TXN_COMMIT);
			m0_fom_phase_set(fom0, M0_FOPH_TXN_COMMIT_WAIT);
		}
		break;
	case CAS_START:
		if (ct == CT_META) {
			fom->cf_index = meta;
			m0_fom_phase_set(fom0, CAS_LOCK);
		} else
			m0_fom_phase_set(fom0, CAS_META_LOCK);
		break;
	case CAS_META_LOCK:
		result = m0_long_read_lock(&meta->ci_lock,
					   &fom->cf_meta, CAS_META_LOOKUP);
		result = M0_FOM_LONG_LOCK_RETURN(result);
		break;
	case CAS_META_LOOKUP:
		rc = cas_buf_get(&fom->cf_buf,
				 &M0_BUF_INIT_CONST(sizeof op->cg_id.ci_fid,
						    &op->cg_id.ci_fid));
		if (rc == 0)
			result = cas_lookup(fom, meta,
					    &fom->cf_buf, CAS_META_LOOKUP_DONE);
		else
			m0_fom_phase_move(fom0, rc, M0_FOPH_FAILURE);
		break;
	case CAS_META_LOOKUP_DONE:
		rc = fom->cf_beop.bo_u.u_btree.t_rc;
		if (rc == 0) {
			struct m0_buf buf = {};

			rc = cas_buf(&fom->cf_anchor.ba_value, &buf);
			if (rc != 0)
				;
			else if (buf.b_nob == sizeof *fom->cf_index) {
				fom->cf_index = buf.b_addr;
				cas_index_init(fom->cf_index, cas_seg());
				cas_release(fom, fom0);
				m0_fom_phase_set(fom0, CAS_LOCK);
				break;
			} else
				rc = M0_ERR_INFO(-EPROTO, "Unexpected: %"PRIx64,
						 buf.b_nob);
		}
		m0_fom_phase_move(fom0, rc, M0_FOPH_FAILURE);
		break;
	case CAS_LOCK:
		M0_ASSERT(index != NULL);
		result = M0_FOM_LONG_LOCK_RETURN(m0_long_lock(&index->ci_lock,
							      !cas_is_ro(opc),
							      &fom->cf_lock,
							      CAS_PREP));
		if (ct != CT_META)
			m0_long_read_unlock(&meta->ci_lock, &fom->cf_meta);
		break;
	case CAS_PREP:
		for (i = 0; i < op->cg_rec.cr_nr; ++i) {
			cas_prep(opc, ct, index, cas_at(op, i),
				 &fom0->fo_tx.tx_betx_cred);
		}
		m0_fom_phase_set(fom0, M0_FOPH_TXN_OPEN);
		/*
		 * @todo waiting for transaction open with btree (which can be
		 * the meta-index) locked, because tree height has to be fixed
		 * for the correct credit calculation.
		 */
		break;
	case CAS_LOOP:
		/* Skip empty CUR requests. */
		while (opc == CO_CUR && pos < op->cg_rec.cr_nr &&
		       cas_at(op, pos)->cr_rc == 0)
			fom->cf_ipos = ++pos;
		/* If all input has been processed... */
		if (pos == op->cg_rec.cr_nr ||
		    /* ... or all output has been generated. */
		    fom->cf_opos == rep->cgr_rep.cr_nr) {
			m0_long_unlock(&index->ci_lock, &fom->cf_lock);
			m0_fom_phase_set(fom0, M0_FOPH_SUCCESS);
		} else
			result = cas_exec(fom, opc, ct, index,
					  cas_at(op, pos), CAS_DONE);
		break;
	case CAS_DONE:
		M0_ASSERT(fom->cf_opos < rep->cgr_rep.cr_nr);
		cas_done(fom, opc, ct, cas_at(op, pos), cas_berc(fom),
			 &rep->cgr_rep.cr_rec[fom->cf_opos]);
		m0_fom_phase_set(fom0, CAS_LOOP);
		break;
	default:
		M0_IMPOSSIBLE("Invalid phase");
	}
	M0_POST(cas_fom_invariant(fom));
	return M0_RC(result);
}

M0_INTERNAL void (*cas__ut_cb_done)(struct m0_fom *fom);
M0_INTERNAL void (*cas__ut_cb_fini)(struct m0_fom *fom);

static void cas_fom_fini(struct m0_fom *fom0)
{
	struct cas_fom *fom = M0_AMB(fom, fom0, cf_fom);

	if (cas_in_ut() && cas__ut_cb_done != NULL)
		cas__ut_cb_done(fom0);
	m0_long_lock_link_fini(&fom->cf_meta);
	m0_long_lock_link_fini(&fom->cf_lock);
	m0_fom_fini(fom0);
	m0_free(fom);
	if (cas_in_ut() && cas__ut_cb_fini != NULL)
		cas__ut_cb_fini(fom0);
}

static const struct m0_fid *cas_fid(const struct m0_fom *fom)
{
	return &cas_op(fom)->cg_id.ci_fid;
}

static size_t cas_fom_home_locality(const struct m0_fom *fom)
{
	return m0_fid_hash(cas_fid(fom));
}

static struct m0_cas_op *cas_op(const struct m0_fom *fom)
{
	return m0_fop_data(fom->fo_fop);
}

static enum cas_opcode cas_opcode(const struct m0_fop *fop)
{
	enum cas_opcode opcode;

	opcode = fop->f_item.ri_type->rit_opcode - M0_CAS_GET_FOP_OPCODE;
	M0_ASSERT(0 <= opcode && opcode < CO_NR);
	return opcode;
}

static int cas_lookup(struct cas_fom *fom, struct cas_index *index,
		      const struct m0_buf *key, int next_phase)
{
	struct m0_be_op *beop = &fom->cf_beop;

	m0_be_op_init(beop);
	fom->cf_index = index;
	m0_be_btree_lookup_inplace(&index->ci_tree, beop, key, &fom->cf_anchor);
	return m0_be_op_tick_ret(beop, &fom->cf_fom, next_phase);
}

static bool cas_is_valid(enum cas_opcode opc, enum cas_type ct,
			 const struct m0_cas_rec *rec)
{
	bool result;
	bool gotkey = m0_buf_is_set(&rec->cr_key);
	bool gotval = m0_buf_is_set(&rec->cr_val);
	bool meta   = ct == CT_META;

	switch (opc) {
	case CO_GET:
	case CO_DEL:
		result = gotkey && !gotval && rec->cr_rc == 0;
		break;
	case CO_PUT:
		result = gotkey && (gotval == !meta) && rec->cr_rc == 0;
		break;
	case CO_CUR:
		result = gotkey && !gotval && rec->cr_rc >= 0;
		break;
	case CO_REP:
		result = !gotval == (((int64_t)rec->cr_rc) < 0 || meta);
		break;
	default:
		M0_IMPOSSIBLE("Wrong opcode.");
	}
	if (meta && gotkey && result) {
		const struct m0_fid *fid = rec->cr_key.b_addr;

		result = rec->cr_key.b_nob == sizeof *fid &&
			m0_fid_is_valid(fid) &&
			m0_fid_type_getfid(fid) == &m0_cas_index_fid_type;
	}
	return M0_RC(result);
}

static bool cas_is_ro(enum cas_opcode opc)
{
	return M0_IN(opc, (CO_GET, CO_CUR, CO_REP));
}

static enum cas_type cas_type(const struct m0_fom *fom)
{
	if (m0_fid_eq(cas_fid(fom), &m0_cas_meta_fid))
		return CT_META;
	else
		return CT_BTREE;
}

static uint64_t cas_nr(const struct m0_fop *fop)
{
	const struct m0_cas_op *op = m0_fop_data(fop);
	uint64_t                nr;

	nr = op->cg_rec.cr_nr;
	if (cas_opcode(fop) == CO_CUR)
		nr = m0_reduce(i, nr, 0, + op->cg_rec.cr_rec[i].cr_rc);
	return nr;
}

static struct m0_be_op *cas_beop(struct cas_fom *fom)
{
	return cas_opcode(fom->cf_fom.fo_fop) == CO_CUR ?
		&fom->cf_cur.bc_op : &fom->cf_beop;
}

static int cas_berc(struct cas_fom *fom)
{
	return cas_beop(fom)->bo_u.u_btree.t_rc;
}

static int cas_buf(const struct m0_buf *val, struct m0_buf *buf)
{
	int result = -EPROTO;

	M0_CASSERT(sizeof buf->b_nob == 8);
	if (val->b_nob >= 8) {
		buf->b_nob = *(uint64_t *)val->b_addr;
		if (val->b_nob == buf->b_nob + 8) {
			buf->b_addr = ((char *)val->b_addr) + 8;
			result = 0;
		}
	}
	if (result != 0)
		return M0_ERR_INFO(-EPROTO, "Unexpected: %"PRIx64"/%"PRIx64,
				   val->b_nob, buf->b_nob);
	else
		return M0_RC(result);
}

static int cas_buf_get(struct m0_buf *dst, const struct m0_buf *src)
{
	m0_bcount_t nob = src->b_nob;

	dst->b_nob  = src->b_nob + sizeof nob;
	dst->b_addr = m0_alloc(dst->b_nob);
	if (dst->b_addr != NULL) {
		*((uint64_t *)dst->b_addr) = nob;
		memcpy(dst->b_addr + sizeof nob, src->b_addr, nob);
		return M0_RC(0);
	} else
		return M0_ERR(-ENOMEM);
}

static int cas_place(struct m0_buf *dst, struct m0_buf *src)
{
	struct m0_buf inner = {};
	int           result;

	result = cas_buf(src, &inner);
	if (result == 0)
		result = m0_buf_copy(dst, &inner);
	return M0_RC(result);
}

#define COMBINE(opc, ct) (((uint64_t)(opc)) | ((ct) << 16))

static void cas_prep(enum cas_opcode opc, enum cas_type ct,
		     struct cas_index *index, const struct m0_cas_rec *rec,
		     struct m0_be_tx_credit *accum)
{
	struct m0_be_btree *btree = &index->ci_tree;
	m0_bcount_t         knob  = rec->cr_key.b_nob + sizeof (uint64_t);
	m0_bcount_t         vnob  = rec->cr_val.b_nob + sizeof (uint64_t);

	switch (COMBINE(opc, ct)) {
	case COMBINE(CO_PUT, CT_META):
		m0_be_btree_create_credit(btree, 1, accum);
		M0_ASSERT(knob == sizeof (uint64_t) + sizeof (struct m0_fid));
		M0_ASSERT(vnob == sizeof (uint64_t));
		vnob = sizeof (struct cas_index) + sizeof (uint64_t);
		/* fallthru */
	case COMBINE(CO_PUT, CT_BTREE):
		m0_be_btree_insert_credit(btree, 1, knob, vnob, accum);
		break;
	case COMBINE(CO_DEL, CT_META):
		/*
		 * @todo It is not always possible to destroy a large btree in
		 * one transaction. See HLD for the solution.
		 */
		m0_be_btree_destroy_credit(btree, accum);
		/* fallthru */
	case COMBINE(CO_DEL, CT_BTREE):
		m0_be_btree_delete_credit(btree, 1, knob, vnob, accum);
		break;
	}
}

static struct m0_cas_rec *cas_at(struct m0_cas_op *op, int idx)
{
	M0_PRE(0 <= idx && idx < op->cg_rec.cr_nr);
	return &op->cg_rec.cr_rec[idx];
}

static int cas_exec(struct cas_fom *fom, enum cas_opcode opc, enum cas_type ct,
		    struct cas_index *index, const struct m0_cas_rec *rec,
		    int next)
{
	struct m0_be_btree        *btree  = &index->ci_tree;
	struct m0_fom             *fom0   = &fom->cf_fom;
	struct m0_be_btree_anchor *anchor = &fom->cf_anchor;
	struct m0_buf             *key    = &fom->cf_buf;
	struct m0_be_btree_cursor *cur    = &fom->cf_cur;
	struct m0_be_tx           *tx     = &fom0->fo_tx.tx_betx;
	struct m0_be_op           *beop   = cas_beop(fom);
	int                        rc;

	rc = cas_buf_get(key, &rec->cr_key);
	if (rc != 0) {
		beop->bo_u.u_btree.t_rc = rc;
		m0_fom_phase_set(fom0, next);
		return M0_FSO_AGAIN;
	}

	M0_SET0(beop);
	m0_be_op_init(beop);
	switch (COMBINE(opc, ct)) {
	case COMBINE(CO_GET, CT_BTREE):
	case COMBINE(CO_GET, CT_META):
		m0_be_btree_lookup_inplace(btree, beop, key, anchor);
		break;
	case COMBINE(CO_PUT, CT_BTREE):
		anchor->ba_value.b_nob = rec->cr_val.b_nob + sizeof (uint64_t);
		m0_be_btree_insert_inplace(btree, tx, beop, key, anchor);
		break;
	case COMBINE(CO_PUT, CT_META):
		anchor->ba_value.b_nob = sizeof (struct cas_index) +
			sizeof (uint64_t);
		m0_be_btree_insert_inplace(btree, tx, beop, key, anchor);
		break;
	case COMBINE(CO_DEL, CT_BTREE):
	case COMBINE(CO_DEL, CT_META):
		/**
		 * @todo delete the btree in META case.
		 */
		m0_be_btree_delete(btree, tx, beop, key);
		break;
	case COMBINE(CO_CUR, CT_BTREE):
	case COMBINE(CO_CUR, CT_META):
		if (fom->cf_curpos == 0) {
			if (cur->bc_tree == NULL)
				m0_be_btree_cursor_init(cur, btree);
			m0_be_btree_cursor_get(cur, key, false);
		} else
			m0_be_btree_cursor_next(cur);
		break;
	}
	return m0_be_op_tick_ret(beop, fom0, next);
}

static void cas_done(struct cas_fom *fom, enum cas_opcode opc, enum cas_type ct,
		     const struct m0_cas_rec *rec, uint64_t rc,
		     struct m0_cas_rec *out)
{
	struct m0_buf val;
	struct m0_buf key;
	void         *arena = fom->cf_anchor.ba_value.b_addr;

	if (rc == 0) {
		switch (COMBINE(opc, ct)) {
		case COMBINE(CO_GET, CT_BTREE):
			rc = cas_place(&out->cr_val, &fom->cf_anchor.ba_value);
			break;
		case COMBINE(CO_GET, CT_META):
		case COMBINE(CO_DEL, CT_META):
		case COMBINE(CO_DEL, CT_BTREE):
			/* Nothing to do: return code is all the user gets. */
			break;
		case COMBINE(CO_PUT, CT_BTREE):
			val = rec->cr_val;
			*(uint64_t *)arena = val.b_nob;
			memcpy(arena + 8, val.b_addr, val.b_nob);
			break;
		case COMBINE(CO_PUT, CT_META):
			*(uint64_t *)arena = sizeof (struct cas_index);
			rc = cas_index_create(arena + 8,
					      &fom->cf_fom.fo_tx.tx_betx);
			break;
		case COMBINE(CO_CUR, CT_BTREE):
		case COMBINE(CO_CUR, CT_META):
			m0_be_btree_cursor_kv_get(&fom->cf_cur, &key, &val);
			rc = cas_place(&out->cr_key, &key);
			if (ct == CT_BTREE && rc == 0)
				rc = cas_place(&out->cr_val, &val);
			break;
		}
	}
	cas_release(fom, &fom->cf_fom);
	if (opc == CO_CUR) {
		if (rc == 0 && (rc = ++fom->cf_curpos) < rec->cr_rc)
			/* Continue with the same iteration. */
			--fom->cf_ipos;
		else
			/*
			 * On error, always end the iteration to avoid infinite
			 * loop.
			 */
			m0_be_btree_cursor_put(&fom->cf_cur);
	}
	++fom->cf_ipos;
	++fom->cf_opos;
	out->cr_rc = rc;
}

#undef COMBINE

static void cas_release(struct cas_fom *fom, struct m0_fom *fom0)
{
	struct m0_be_op *beop  = cas_beop(fom);
	int              state = beop->bo_sm.sm_state;

	m0_be_btree_release(&fom0->fo_tx.tx_betx, &fom->cf_anchor);
	m0_buf_free(&fom->cf_buf);
	if (state >= M0_BOS_INIT && state != M0_BOS_DONE)
		m0_be_op_fini(beop);
}

static int cas_init(struct cas_service *service)
{
	struct m0_be_seg       *seg0  = cas_seg();
	struct m0_sm_group     *grp   = m0_locality0_get()->lo_grp;
	struct cas_index       *meta  = NULL;
	struct m0_be_tx         tx    = {};
	struct m0_be_tx_credit  cred  = M0_BE_TX_CREDIT(0, 0);
	struct m0_be_btree      dummy = { .bb_seg = seg0 };
	struct m0_be_btree     *bt;
	int                     result;

	/**
	 * @todo Use 0type.
	 */
	result = m0_be_seg_dict_lookup(seg0, "cas-meta", (void **)&meta);
	if (result == 0) {
		/**
		 * @todo Add checking, use header and footer.
		 */
		if (meta == NULL)
			result = M0_ERR(-EPROTO);
		else {
			service->c_meta = meta;
			cas_index_init(meta, seg0);
			return M0_RC(0);
		}
	}
	if (result != -ENOENT)
		return M0_ERR(result);

	m0_sm_group_lock(grp);
	m0_be_tx_init(&tx, 0, m0_get()->i_be_dom, grp, NULL, NULL, NULL, NULL);
	m0_be_seg_dict_insert_credit(seg0, cas_key, &cred);
	M0_BE_ALLOC_CREDIT_PTR(meta, seg0, &cred);
	m0_be_btree_create_credit(&dummy, 1, &cred);
	/* Error case: tree destruction and freeing. */
	m0_be_btree_destroy_credit(&dummy, &cred);
	M0_BE_FREE_CREDIT_PTR(meta, seg0, &cred);
	m0_be_tx_prep(&tx, &cred);
	result = m0_be_tx_exclusive_open_sync(&tx);
	if (result != 0) {
		m0_be_tx_fini(&tx);
		return M0_ERR(result);
	}
	M0_BE_ALLOC_PTR_SYNC(meta, seg0, &tx);
	if (meta != NULL) {
		bt = &meta->ci_tree;
		result = cas_index_create(meta, &tx);
		if (result == 0) {
			result = m0_be_seg_dict_insert(seg0, &tx, cas_key,meta);
			if (result == 0) {
				M0_BE_TX_CAPTURE_PTR(seg0, &tx, meta);
				service->c_meta = meta;
			} else {
				M0_BE_OP_SYNC(op, m0_be_btree_destroy(bt, &tx,
								      &op));
			}
		}
		if (result != 0)
			M0_BE_FREE_PTR_SYNC(meta, seg0, &tx);
	} else
		result = M0_ERR(-ENOSPC);
	m0_be_tx_close_sync(&tx);
	m0_be_tx_fini(&tx);
	m0_sm_group_unlock(grp);
	return M0_RC(result);
}

static void cas_index_init(struct cas_index *index, struct m0_be_seg *seg)
{
	m0_be_btree_init(&index->ci_tree, seg, &cas_btree_ops);
	m0_long_lock_init(&index->ci_lock);
}

static int cas_index_create(struct cas_index *index, struct m0_be_tx *tx)
{
	cas_index_init(index, m0_be_domain_seg0_get(tx->t_engine->eng_domain));
	return M0_BE_OP_SYNC_RET(op, m0_be_btree_create(&index->ci_tree, tx,
						       &op), bo_u.u_btree.t_rc);
}

static m0_bcount_t cas_ksize(const void *key)
{
	return sizeof (uint64_t) + *(const uint64_t *)key;
}

static m0_bcount_t cas_vsize(const void *val)
{
	return cas_ksize(val);
}

static int cas_cmp(const void *key0, const void *key1)
{
	m0_bcount_t knob0 = cas_ksize(key0);
	m0_bcount_t knob1 = cas_ksize(key1);

	/**
	 * @todo Cannot assert on on-disk data, but no interface to report
	 * errors from here.
	 */
	M0_ASSERT(knob0 >= 8);
	M0_ASSERT(knob1 >= 8);

	return memcmp(key0 + 8, key1 + 8, min_check(knob0, knob1) - 8) ?:
		M0_3WAY(knob0, knob1);
}

static struct m0_be_seg *cas_seg(void)
{
	return m0_be_domain_seg0_get(m0_get()->i_be_dom);
}

static bool cas_in_ut(void)
{
	return M0_FI_ENABLED("ut");
}

static bool cas_fom_invariant(const struct cas_fom *fom)
{
	const struct m0_fom *fom0    = &fom->cf_fom;
	int                  phase   = m0_fom_phase(fom0);
	struct m0_cas_op    *op      = cas_op(fom0);
	struct cas_service  *service = M0_AMB(service,
					      fom0->fo_service, c_service);

	return  _0C(ergo(phase > M0_FOPH_INIT && phase != M0_FOPH_FAILURE,
			 fom->cf_ipos <= op->cg_rec.cr_nr)) &&
		_0C(M0_IN(fom->cf_anchor.ba_tree,
			  (NULL, &fom->cf_index->ci_tree,
			   &service->c_meta->ci_tree))) &&
		_0C(phase <= CAS_NR);
}

static const struct m0_fom_ops cas_fom_ops = {
	.fo_tick          = &cas_fom_tick,
	.fo_home_locality = &cas_fom_home_locality,
	.fo_fini          = &cas_fom_fini
};

static const struct m0_fom_type_ops cas_fom_type_ops = {
	.fto_create = &cas_fom_create
};

static struct m0_sm_state_descr cas_fom_phases[] = {
	[CAS_START] = {
		.sd_name      = "start",
		.sd_allowed   = M0_BITS(CAS_META_LOCK, CAS_LOCK)
	},
	[CAS_META_LOCK] = {
		.sd_name      = "meta-lock",
		.sd_allowed   = M0_BITS(CAS_META_LOOKUP)
	},
	[CAS_META_LOOKUP] = {
		.sd_name      = "meta-lookup",
		.sd_allowed   = M0_BITS(CAS_META_LOOKUP_DONE, M0_FOPH_FAILURE)
	},
	[CAS_META_LOOKUP_DONE] = {
		.sd_name      = "meta-lookup-done",
		.sd_allowed   = M0_BITS(CAS_LOCK, M0_FOPH_FAILURE)
	},
	[CAS_LOCK] = {
		.sd_name      = "lock",
		.sd_allowed   = M0_BITS(CAS_PREP)
	},
	[CAS_PREP] = {
		.sd_name      = "prep",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_OPEN)
	},
	[CAS_LOOP] = {
		.sd_name      = "loop",
		.sd_allowed   = M0_BITS(CAS_DONE, M0_FOPH_SUCCESS)
	},
	[CAS_DONE] = {
		.sd_name      = "done",
		.sd_allowed   = M0_BITS(CAS_LOOP)
	}
};

struct m0_sm_trans_descr cas_fom_trans[] = {
	[ARRAY_SIZE(m0_generic_phases_trans)] =
	{ "tx-initialised",       M0_FOPH_TXN_OPEN,     CAS_START },
	{ "btree-op?",            CAS_START,            CAS_META_LOCK },
	{ "meta-op?",             CAS_START,            CAS_LOCK },
	{ "meta-locked",          CAS_META_LOCK,        CAS_META_LOOKUP },
	{ "meta-lookup-launched", CAS_META_LOOKUP,      CAS_META_LOOKUP_DONE },
	{ "key-alloc-failure",    CAS_META_LOOKUP,      M0_FOPH_FAILURE },
	{ "meta-lookup-done",     CAS_META_LOOKUP_DONE, CAS_LOCK },
	{ "meta-lookup-fail",     CAS_META_LOOKUP_DONE, M0_FOPH_FAILURE },
	{ "index-locked",         CAS_LOCK,             CAS_PREP },
	{ "tx-credit-calculated", CAS_PREP,             M0_FOPH_TXN_OPEN },
	{ "all-done?",            CAS_LOOP,             M0_FOPH_SUCCESS },
	{ "op-launched",          CAS_LOOP,             CAS_DONE },
	{ "next",                 CAS_DONE,             CAS_LOOP },

	{ "ut-short-cut",         M0_FOPH_QUEUE_REPLY, M0_FOPH_TXN_COMMIT_WAIT }
};

static struct m0_sm_conf cas_sm_conf = {
	.scf_name      = "cas-fom",
	.scf_nr_states = ARRAY_SIZE(cas_fom_phases),
	.scf_state     = cas_fom_phases,
	.scf_trans_nr  = ARRAY_SIZE(cas_fom_trans),
	.scf_trans     = cas_fom_trans
};

static const struct m0_reqh_service_type_ops cas_service_type_ops = {
	.rsto_service_allocate = &cas_service_type_allocate
};

static const struct m0_reqh_service_ops cas_service_ops = {
	.rso_start_async = &m0_reqh_service_async_start_simple,
	.rso_start       = &cas_service_start,
	.rso_stop        = &cas_service_stop,
	.rso_fini        = &cas_service_fini
};

M0_INTERNAL struct m0_reqh_service_type m0_cas_service_type = {
	.rst_name     = "cas",
	.rst_ops      = &cas_service_type_ops,
	.rst_level    = M0_RS_LEVEL_NORMAL,
	.rst_typecode = M0_CST_CAS
};

static const struct m0_be_btree_kv_ops cas_btree_ops = {
	.ko_ksize   = &cas_ksize,
	.ko_vsize   = &cas_vsize,
	.ko_compare = &cas_cmp
};

M0_INTERNAL struct m0_fid m0_cas_meta_fid = M0_FID_TINIT('i', 1, 1);

M0_INTERNAL struct m0_fid_type m0_cas_index_fid_type = {
	.ft_id   = 'i',
	.ft_name = "cas-index"
};

#undef M0_TRACE_SUBSYSTEM

/** @} end of cas group */

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
