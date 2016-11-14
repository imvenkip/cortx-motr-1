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
#include "rpc/rpc_machine.h"         /* m0_rpc_machine */
#include "conf/schema.h"             /* M0_CST_CAS */
#include "module/instance.h"
#include "addb2/addb2.h"

#include "cas/cas_addb2.h"           /* M0_AVI_CAS_KV_SIZES */
#include "cas/cas.h"
#include "cas/cas_xc.h"


/**
 * @page cas-dld The catalogue service (CAS)
 *
 * - @ref cas-ovw
 * - @ref cas-def
 * - @ref cas-ref
 * - @ref cas-req
 * - @ref cas-depends
 * - @ref cas-highlights
 * - @ref cas-lspec
 *    - @ref cas-lspec-state
 *    - @ref cas-lspec-thread
 *    - @ref cas-lspec-layout
 * - @ref cas-conformance
 * - @subpage cas-fspec "Functional Specification"
 *
 * <hr>
 * @section cas-ovw Overview
 * Catalogue service exports BE btrees (be/btree.[ch]) to the network.
 *
 * <hr>
 * @section cas-def Definitions
 *
 * Some of definitions are copied from HLD (see @ref cas-ref).
 * - @b Catalogue (index): a container for records. A catalogue is explicitly
 *   created and deleted by a user and has an identifier (m0_fid), assigned by
 *   the user.
 *
 * - @b Meta-catalogue: single catalogue containing information about all
 *   existing catalogues.
 *
 * - @b Record: a key-value pair.
 *
 * - @b Key: an arbitrary sequence of bytes, used to identify a record in a
 *   catalogue.
 *
 * - @b Value: an arbitrary sequence of bytes, associated with a key.
 *
 * - @b Key @b order: total order, defined on keys within a given container.
 *   Iterating through the container, returns keys in this order. The order is
 *   defined as lexicographical order of keys, interpreted as bit-strings.
 *
 * - @b User: any Mero component or external application using a cas instance by
 *   sending fops to it.
 *
 * <hr>
 * @section cas-req Requirements
 * Additional requirements that are not covered in HLD (see @ref cas-ref)
 * - @b r.cas.bulk
 *   RPC bulk mechanism should be supported for transmission of CAS request
 *   that doesn't fit into one FOP.
 *
 * - @b r.cas.sync
 *   CAS service processes requests synchronously. If catalogue can't be locked
 *   immediately, then FOM is blocked.
 *
 * - @b r.cas.indices-list
 *   User should be able to request list of all indices in meta-index without
 *   prior knowledge of any index FID.
 *
 * - @b r.cas.addb
 *   ADDB statistics should be collected for:
 *   - Sizes for every requested key/value;
 *   - Read/write lock contention.
 *
 * <hr>
 * @section cas-depends Dependencies
 * - reqh service
 * - fom, fom long lock
 * - BE transaction, BE btree.
 *
 * <hr>
 * @section cas-highlights Design Highlights
 * - Catalogue service implementation doesn't bother about distribution of
 *   key/value records between nodes in the cluster. Distributed key/value
 *   storage may be built on the client side.
 * - Keys and values can be of arbitrary size, so they possibly don't fit into
 *   single FOP. Bulk RPC transmission should be used in this case (not
 *   implemented yet).
 * - Per-FOM BE transaction is used to perform modifications of index B-trees.
 * - B-tree for meta-index is stored in zero BE segment of mero global BE
 *   domain. Other B-trees are stored in BE segment of the request handler.
 *
 * <hr>
 * @section cas-lspec Logical Specification
 *
 * @subsection cas-lspec-state State Specification
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
 *                          V          |            |               |
 *     SUCCESS<---------CAS_LOOP<----+ |            V               |
 *                          |        | |    +--->CAS_LOAD<----------+
 *                          V        | |    |       |
 *                  CAS_PREPARE_SEND | |    |       V
 *                          |        | |    +--CAS_LOAD_DONE
 *                          V        | |            |
 *                +----->CAS_SEND    | |            V
 *                |         |        | |        CAS_LOCK
 *                |         V        | |            |
 *                +----CAS_SEND_DONE | |            V
 *                          |        | +--------CAS_PREP
 *                          |        |
 *                          V        |
 *                      CAS_DONE-----+
 * @endverbatim
 *
 * @subsection cas-lspec-thread Threading and Concurrency Model
 * Catalogues (including meta catalogue) are protected with "multiple
 * readers/single writer" lock (see cas_index::ci_lock). Writer starvation is
 * not possible due to FOM long lock design, because writer has priority over
 * readers.
 *
 * B-tree structure has internal rwlock (m0_be_btree::bb_lock), but it's not
 * convenient for usage inside FOM since it blocks the execution thread. Also,
 * index should be locked before FOM BE TX credit calculation, because amount of
 * credits depends on the height of BE tree. Index is unlocked when all
 * necessary B-tree operations are done.
 *
 * @subsection cas-lspec-layout
 *
 * Memory for all indices (including meta-index) is allocated in a meta-segment
 * (seg0) of Mero global BE domain (m0::i_be_dom). Address of a meta-index is
 * stored in this segment dictionary. Meta-index btree stores information about
 * existing indices (including itself) as key-value pairs, where key is a FID of
 * an index and value is a pointer to it.
 *
 * <hr>
 * @section cas-conformance Conformance
 *
 * - @b i.cas.bulk
 *   Not designed yet.
 *
 * - @b i.cas.sync
 *   FOM is blocked on waiting of per-index FOM long lock.
 *
 * - @b i.cas.indices-list
 *   Meta-index includes itself and has the smallest FID (0,0), so it is always
 *   the first by key order. User can specify m0_cas_meta_fid as s starting FID
 *   to list indices from the start.
 *
 * - @b i.cas.addb
 *   Per-index long lock is initialised with non-NULL addb2 structure.
 *   Key/value size of each record in request is collected in
 *   cas_fom_addb2_descr() function.
 *
 * <hr>
 * @section cas-ref References
 *
 * - HLD of the catalogue service
 *   https://docs.google.com/document/d/1Zhw1BVHZOFn-x2B8Yay1hZ0guTT5KFnpIA5gT3oaCXI/edit
 *
 * @{
 */
struct cas_index {
	struct m0_format_header ci_head;
	struct m0_format_footer ci_foot;
	/*
	 * m0_be_btree has it's own volatile-only fields, so it can't be placed
	 * before the m0_format_footer, where only persistent fields allowed
	 */
	struct m0_be_btree      ci_tree;
	struct m0_long_lock     ci_lock;
};

enum m0_cas_index_format_version {
	M0_CAS_INDEX_FORMAT_VERSION_1 = 1,

	/* future versions, uncomment and update M0_CAS_INDEX_FORMAT_VERSION */
	/*M0_CAS_INDEX_FORMAT_VERSION_2,*/
	/*M0_CAS_INDEX_FORMAT_VERSION_3,*/

	/** Current version, should point to the latest version present */
	M0_CAS_INDEX_FORMAT_VERSION = M0_CAS_INDEX_FORMAT_VERSION_1
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
	/* ADDB2 structures to collect long-lock contention metrics. */
	struct m0_long_lock_addb2 cf_lock_addb2;
	struct m0_long_lock_addb2 cf_meta_addb2;
	/* AT helper fields. */
	struct m0_buf             cf_out_key;
	struct m0_buf             cf_out_val;
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
	CAS_LOAD_KEY,
	CAS_LOAD_VAL,
	CAS_LOAD_DONE,
	CAS_PREPARE_SEND,
	CAS_SEND_KEY,
	CAS_KEY_SENT,
	CAS_SEND_VAL,
	CAS_VAL_SENT,
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
static struct m0_cas_rec   *cas_out_at (const struct m0_cas_rep *rep, int idx);
static bool                 cas_is_ro  (enum cas_opcode opc);
static enum cas_opcode      cas_opcode (const struct m0_fop *fop);
static uint64_t             cas_nr     (const struct m0_fop *fop);
static struct m0_be_op     *cas_beop   (struct cas_fom *fom);
static int                  cas_berc   (struct cas_fom *fom);
static m0_bcount_t          cas_ksize  (const void *key);
static m0_bcount_t          cas_vsize  (const void *val);
static int                  cas_cmp    (const void *key0, const void *key1);
static bool                 cas_in_ut  (void);
static int                  cas_lookup (struct cas_fom *fom,
					struct cas_index *index,
					const struct m0_buf *key, int next);
static void                 cas_prep   (struct cas_fom *fom,
					enum cas_opcode opc, enum cas_type ct,
					struct cas_index *index,
					const struct m0_cas_rec *rec,
					struct m0_be_tx_credit *accum);
static int                  cas_exec   (struct cas_fom *fom,
					enum cas_opcode opc, enum cas_type ct,
					struct cas_index *index,
					const struct m0_cas_rec *rec, int next);
static int                  cas_init   (struct cas_service *service);

static int  cas_done(struct cas_fom *fom, struct m0_cas_op *op,
		     struct m0_cas_rep *rep, enum cas_opcode opc);


static int  cas_prep_send(struct cas_fom *fom, enum cas_opcode opc,
			  enum cas_type ct, const struct m0_cas_rec *rec,
			  uint64_t rc);

static bool cas_is_valid    (enum cas_opcode opc, enum cas_type ct,
			     const struct m0_cas_rec *rec);
static void cas_index_init  (struct cas_index *index, struct m0_be_seg *seg);
static int  cas_index_create(struct cas_index *index, struct m0_be_tx *tx);
static void cas_index_destroy(struct cas_index *index, struct m0_be_tx *tx);
static bool cas_fom_invariant(const struct cas_fom *fom);

static const struct m0_reqh_service_ops      cas_service_ops;
static const struct m0_reqh_service_type_ops cas_service_type_ops;
static const struct m0_fom_ops               cas_fom_ops;
static const struct m0_fom_type_ops          cas_fom_type_ops;
static       struct m0_sm_conf               cas_sm_conf;
static       struct m0_sm_state_descr        cas_fom_phases[];
static const struct m0_be_btree_kv_ops       cas_btree_ops;

static const char cas_key[] = "cas-meta";

M0_INTERNAL void m0_cas_svc_init(void)
{
	m0_sm_conf_extend(m0_generic_conf.scf_state, cas_fom_phases,
			  m0_generic_conf.scf_nr_states);
	m0_sm_conf_trans_extend(&m0_generic_conf, &cas_sm_conf);
	cas_fom_phases[M0_FOPH_TXN_OPEN].sd_allowed |= M0_BITS(CAS_START);
	cas_fom_phases[M0_FOPH_QUEUE_REPLY].sd_allowed |=
		M0_BITS(M0_FOPH_TXN_COMMIT_WAIT);
	m0_sm_conf_init(&cas_sm_conf);
	m0_reqh_service_type_register(&m0_cas_service_type);
}

M0_INTERNAL void m0_cas_svc_fini(void)
{
	m0_reqh_service_type_unregister(&m0_cas_service_type);
	m0_sm_conf_fini(&cas_sm_conf);
}

M0_INTERNAL void m0_cas_svc_fop_args(struct m0_sm_conf            **sm_conf,
				     const struct m0_fom_type_ops **fom_ops,
				     struct m0_reqh_service_type  **svctype)
{
	*sm_conf = &cas_sm_conf;
	*fom_ops = &cas_fom_type_ops;
	*svctype = &m0_cas_service_type;
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

	M0_PRE(M0_IN(m0_reqh_service_state_get(svc),
		     (M0_RST_STOPPED, M0_RST_FAILED)));
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
	/**
	 * In case nr is 0, M0_ALLOC_ARR returns non-NULL pointer.
	 * Two cases should be distinguished:
	 * - allocate operation has failed and repv is NULL;
	 * - nr == 0 and repv is NULL;
	 * The second one is a correct case.
	 */
	if (nr != 0)
		M0_ALLOC_ARR(repv, nr);
	else
		repv = NULL;
	if (fom != NULL && repfop != NULL && (nr == 0 || repv != NULL)) {
		*out = fom0 = &fom->cf_fom;
		repdata = m0_fop_data(repfop);
		repdata->cgr_rep.cr_nr  = nr;
		repdata->cgr_rep.cr_rec = repv;
		m0_fom_init(fom0, &fop->f_type->ft_fom_type,
			    &cas_fom_ops, fop, repfop, reqh);
		m0_long_lock_link_init(&fom->cf_lock, fom0,
				       &fom->cf_lock_addb2);
		m0_long_lock_link_init(&fom->cf_meta, fom0,
				       &fom->cf_meta_addb2);
		return M0_RC(0);
	} else {
		m0_free(repfop);
		m0_free(repv);
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}
}

static int cas_at_load(struct m0_rpc_at_buf *ab, struct m0_fom *fom,
		       int next_phase)
{
	if (cas_in_ut()) {
		m0_fom_phase_set(fom, next_phase);
		return M0_FSO_AGAIN;
	}

	return m0_rpc_at_load(ab, fom, next_phase);
}

static int cas_at_reply(struct m0_rpc_at_buf *in,
			struct m0_rpc_at_buf *out,
			struct m0_buf        *repbuf,
			struct m0_fom        *fom,
			int                   next_phase)
{
	if (cas_in_ut()) {
		m0_fom_phase_set(fom, next_phase);
		out->ab_type = M0_RPC_AT_INLINE;
		out->u.ab_buf = *repbuf;
		return M0_FSO_AGAIN;
	}

	return m0_rpc_at_reply(in, out, repbuf, fom, next_phase);
}

static void cas_at_fini(struct m0_rpc_at_buf *ab)
{
	if (cas_in_ut()) {
		ab->ab_type = M0_RPC_AT_EMPTY;
		return;
	}

	m0_rpc_at_fini(ab);
}

static int cas_incoming_kv(const struct m0_cas_rec *rec,
			   struct m0_buf           *key,
			   struct m0_buf           *val)
{
	int rc;

	M0_PRE(m0_rpc_at_is_set(&rec->cr_key));
	rc = m0_rpc_at_get(&rec->cr_key, key);
	if (rc != 0)
		return M0_ERR(rc);
	if (m0_rpc_at_is_set(&rec->cr_val))
		rc = m0_rpc_at_get(&rec->cr_val, val);
	else
		*val = M0_BUF_INIT0;

	return M0_RC(rc);
}

static int cas_load_check(const struct m0_cas_op *op)
{
	uint64_t      i;
	struct m0_buf key;
	struct m0_buf val;
	int           rc;

	for (i = 0; i < op->cg_rec.cr_nr; i++) {
		rc = cas_incoming_kv(&op->cg_rec.cr_rec[i], &key, &val);
		if (rc != 0)
			return M0_ERR(rc);
	}
	return M0_RC(0);
}

/**
 * Returns AT buffer from client request that is used to transmit key/value from
 * server.
 */
static struct m0_rpc_at_buf *cas_out_complementary(enum cas_opcode         opc,
						   const struct m0_cas_op *op,
						   bool                    key,
						   size_t                  opos)
{
	struct m0_rpc_at_buf *ret;
	struct m0_cas_rec    *rec = NULL;
	uint64_t              i = 0;

	switch (opc) {
	case CO_PUT:
	case CO_DEL:
		ret = NULL;
		break;
	case CO_GET:
		ret = key ? NULL : &op->cg_rec.cr_rec[opos].cr_val;
		break;
	case CO_CUR:
		while (opos >= op->cg_rec.cr_rec[i].cr_rc) {
			opos -= op->cg_rec.cr_rec[i].cr_rc;
			i++;
		}
		if (i < op->cg_rec.cr_nr)
			rec = &op->cg_rec.cr_rec[i];
		if (rec != NULL && rec->cr_kv_bufs.cv_nr != 0) {
			struct m0_cas_kv_vec *kv;

			kv = &rec->cr_kv_bufs;
			if (opos < kv->cv_nr)
				ret = key ? &kv->cv_rec[opos].ck_key :
					    &kv->cv_rec[opos].ck_val;
			else
				ret = NULL;
		} else
			ret = NULL;
		break;
	default:
		M0_IMPOSSIBLE("Invalid opcode.");
	}
	return ret;
}

/**
 * Fills outgoing key AT buffer for current outgoing record in processing.
 *
 * The key buffer usually is not sent over network right here, but just included
 * into outgoing FOP. If the key is too big to fit into the FOP, then 'inbulk'
 * AT transmission method comes to play. See cas_at_reply() for more info.
 *
 * Current outgoing record in the outgoing FOP is identified by fom->cf_opos
 * index.
 */
static int cas_key_send(struct cas_fom          *fom,
			const struct m0_cas_op  *op,
			enum cas_opcode          opc,
			const struct m0_cas_rep *rep,
			enum cas_fom_phase       next_phase)
{
	struct m0_cas_rec    *orec = cas_out_at(rep, fom->cf_opos);
	struct m0_rpc_at_buf *in;
	int                   result;

	M0_ENTRY("fom %p, opc %d, opos %"PRIu64, fom, opc, fom->cf_opos);
	m0_rpc_at_init(&orec->cr_key);
	in = cas_out_complementary(opc, op, true, fom->cf_opos);
	result = cas_at_reply(in,
			      &orec->cr_key,
			      &fom->cf_out_key,
			      &fom->cf_fom,
			      next_phase);
	return result;
}

/**
 * Similar to cas_key_send(), but fills value AT buffer.
 */
static int cas_val_send(struct cas_fom          *fom,
			const struct m0_cas_op  *op,
			enum cas_opcode          opc,
			const struct m0_cas_rep *rep,
			enum cas_fom_phase       next_phase)
{
	struct m0_cas_rec    *orec = cas_out_at(rep, fom->cf_opos);
	struct m0_rpc_at_buf *in;
	int                   result;

	M0_ENTRY("fom %p, opc %d, opos %"PRIu64, fom, opc, fom->cf_opos);
	m0_rpc_at_init(&orec->cr_val);
	in = cas_out_complementary(opc, op, false, fom->cf_opos);
	result = cas_at_reply(in,
			      &orec->cr_val,
			      &fom->cf_out_val,
			      &fom->cf_fom,
			      next_phase);
	return result;
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
	struct m0_cas_rec  *rec;

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
			m0_fom_phase_set(fom0, CAS_LOAD_KEY);
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
				m0_fom_phase_set(fom0, CAS_LOAD_KEY);
				break;
			} else
				rc = M0_ERR_INFO(-EPROTO, "Unexpected: %"PRIx64,
						 buf.b_nob);
		}
		m0_fom_phase_move(fom0, rc, M0_FOPH_FAILURE);
		break;
	case CAS_LOAD_KEY:
		result = cas_at_load(&cas_at(op, pos)->cr_key, fom0,
				     CAS_LOAD_VAL);
		break;
	case CAS_LOAD_VAL:
		/*
		 * Don't check result of key loading here, result codes for all
		 * keys/values are checked in cas_load_check().
		 */
		result = cas_at_load(&cas_at(op, pos)->cr_val, fom0,
				     CAS_LOAD_DONE);
		break;
	case CAS_LOAD_DONE:
		/* Record key/value are loaded. */
		fom->cf_ipos++;
		M0_ASSERT(fom->cf_ipos <= op->cg_rec.cr_nr);
		/* Do we need to load other keys and values from op? */
		if (fom->cf_ipos == op->cg_rec.cr_nr) {
			fom->cf_ipos = 0;
			rc = cas_load_check(op);
			if (rc != 0)
				m0_fom_phase_move(fom0, M0_ERR(rc),
						  M0_FOPH_FAILURE);
			else
				m0_fom_phase_set(fom0, CAS_LOCK);
			break;
		}
		/* Load next key/value. */
		m0_fom_phase_set(fom0, CAS_LOAD_KEY);
		break;
	case CAS_LOCK:
		M0_ASSERT(index != NULL);
		result = M0_FOM_LONG_LOCK_RETURN(m0_long_lock(&index->ci_lock,
							      !cas_is_ro(opc),
							      &fom->cf_lock,
							      CAS_PREP));
		fom->cf_ipos = 0;
		if (ct != CT_META)
			m0_long_read_unlock(&meta->ci_lock, &fom->cf_meta);
		break;
	case CAS_PREP:
		if (m0_exists(i, op->cg_rec.cr_nr,
				!(ct == CT_META ||
				  cas_is_valid(opc, ct, cas_at(op, i))))) {
				m0_fom_phase_move(fom0, M0_ERR(-EPROTO),
						  M0_FOPH_FAILURE);
				break;
			}
		for (i = 0; i < op->cg_rec.cr_nr; ++i)
			cas_prep(fom, opc, ct, index, cas_at(op, i),
				 &fom0->fo_tx.tx_betx_cred);
		fom->cf_ipos = 0;
		fom->cf_opos = 0;
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
					  cas_at(op, pos), CAS_PREPARE_SEND);
		break;
	case CAS_PREPARE_SEND:
		M0_ASSERT(fom->cf_opos < rep->cgr_rep.cr_nr);
		rec = cas_out_at(rep, fom->cf_opos);
		M0_ASSERT(rec != NULL);
		rec->cr_rc = cas_prep_send(fom, opc, ct, cas_at(op, pos),
					   cas_berc(fom));
		m0_fom_phase_set(fom0, rec->cr_rc == 0 ?
					CAS_SEND_KEY : CAS_DONE);
		break;
	case CAS_SEND_KEY:
		if (opc == CO_CUR)
			result = cas_key_send(fom, op, opc, rep, CAS_KEY_SENT);
		else
			m0_fom_phase_set(fom0, CAS_SEND_VAL);
		break;
	case CAS_KEY_SENT:
		rec = cas_out_at(rep, fom->cf_opos);
		rec->cr_rc = m0_rpc_at_reply_rc(&rec->cr_key);
		/*
		 * Try to send value even if a key is not sent successfully.
		 * It's necessary to return proper reply in case if bulk
		 * transmission is required, but the user sent empty AT buffer.
		 */
		m0_fom_phase_set(fom0, CAS_SEND_VAL);
		break;
	case CAS_SEND_VAL:
		if (ct == CT_BTREE && M0_IN(opc, (CO_GET, CO_CUR)))
			result = cas_val_send(fom, op, opc, rep, CAS_VAL_SENT);
		else
			m0_fom_phase_set(fom0, CAS_DONE);
		break;
	case CAS_VAL_SENT:
		rec = cas_out_at(rep, fom->cf_opos);
		rec->cr_rc = m0_rpc_at_reply_rc(&rec->cr_val);
		m0_fom_phase_set(fom0, CAS_DONE);
		break;
	case CAS_DONE:
		cas_done(fom, op, rep, opc);
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

static void cas_at_reply_bufs_fini(struct m0_fom *fom0)
{
	struct m0_cas_rep    *rep;
	struct m0_rpc_at_buf *key;
	struct m0_rpc_at_buf *val;
	uint64_t              i;

	if (cas_in_ut())
		return;

	rep = m0_fop_data(fom0->fo_rep_fop);
	for (i = 0; i < rep->cgr_rep.cr_nr; i++) {
		key = &rep->cgr_rep.cr_rec[i].cr_key;
		val = &rep->cgr_rep.cr_rec[i].cr_val;
		if (m0_rpc_at_is_set(key))
			cas_at_fini(key);
		if (m0_rpc_at_is_set(val))
			cas_at_fini(val);
	}
}

static void cas_fom_fini(struct m0_fom *fom0)
{
	struct cas_fom *fom = M0_AMB(fom, fom0, cf_fom);

	if (cas_in_ut() && cas__ut_cb_done != NULL)
		cas__ut_cb_done(fom0);
	cas_at_reply_bufs_fini(fom0);
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
	bool          result;
	struct m0_buf key;
	bool          gotkey;
	bool          gotval;
	bool          meta = ct == CT_META;

	gotkey = m0_rpc_at_is_set(&rec->cr_key);
	gotval = m0_rpc_at_is_set(&rec->cr_val);
	switch (opc) {
	case CO_GET:
	case CO_DEL:
		result = gotkey && !gotval && rec->cr_rc == 0;
		break;
	case CO_PUT:
		result = gotkey && (gotval == !meta) && rec->cr_rc == 0;
		break;
	case CO_CUR:
		result = gotkey && !gotval;
		break;
	case CO_REP:
		result = !gotval == (((int64_t)rec->cr_rc) < 0 || meta);
		break;
	default:
		M0_IMPOSSIBLE("Wrong opcode.");
	}
	if (meta && gotkey && result) {
		const struct m0_fid *fid;
		int          rc;

		/* Valid key is sent inline always, so rc should be 0. */
		rc = m0_rpc_at_get(&rec->cr_key, &key);
		fid = key.b_addr;
		result = rc == 0 && key.b_nob == sizeof *fid &&
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
	if (M0_FI_ENABLED("be-failure"))
		return M0_ERR(-ENOMEM);
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

	if (M0_FI_ENABLED("cas_alloc_fail"))
		return M0_ERR(-ENOMEM);
	dst->b_nob  = src->b_nob + sizeof nob;
	dst->b_addr = m0_alloc(dst->b_nob);
	if (dst->b_addr != NULL) {
		*((uint64_t *)dst->b_addr) = nob;
		memcpy(dst->b_addr + sizeof nob, src->b_addr, nob);
		return M0_RC(0);
	} else
		return M0_ERR(-ENOMEM);
}

static int cas_place(struct m0_buf *dst, struct m0_buf *src, m0_bcount_t cutoff)
{
	struct m0_buf inner = {};
	int           result;

	if (M0_FI_ENABLED("place_fail"))
		return M0_ERR(-ENOMEM);

	result = cas_buf(src, &inner);
	if (result == 0) {
		if (inner.b_nob >= cutoff) {
			dst->b_addr = m0_alloc_aligned(inner.b_nob, PAGE_SHIFT);
			if (dst->b_addr == NULL)
				return M0_ERR(-ENOMEM);
			dst->b_nob = inner.b_nob;
			memcpy(dst->b_addr, inner.b_addr, inner.b_nob);
		} else {
			result = m0_buf_copy(dst, &inner);
		}
	}
	return M0_RC(result);
}

#define COMBINE(opc, ct) (((uint64_t)(opc)) | ((ct) << 16))

/**
 * Returns number of bytes required for key/value stored in btree given
 * user-supplied key/value buffer.
 *
 * Key/value stored in btree have first byte defining length and successive
 * bytes storing key/value.
 */
static m0_bcount_t cas_kv_nob(const struct m0_buf *inbuf)
{
	return inbuf->b_nob + sizeof(uint64_t);
}

static void cas_prep(struct cas_fom *fom, enum cas_opcode opc,
		     enum cas_type ct, struct cas_index *index,
		     const struct m0_cas_rec *rec,
		     struct m0_be_tx_credit *accum)
{
	struct m0_be_btree *btree = &index->ci_tree;
	struct m0_buf       key;
	struct m0_buf       val;
	m0_bcount_t         knob;
	m0_bcount_t         vnob;
	int                 rc;

	rc = cas_incoming_kv(rec, &key, &val);
	M0_ASSERT(rc == 0);
	knob = cas_kv_nob(&key);
	vnob = cas_kv_nob(&val);
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

static struct m0_cas_rec *cas_out_at(const struct m0_cas_rep *rep, int idx)
{
	M0_PRE(0 <= idx && idx < rep->cgr_rep.cr_nr);
	return &rep->cgr_rep.cr_rec[idx];
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
	uint32_t                   flags  = cas_op(fom0)->cg_flags;
	struct m0_buf              kbuf;
	struct m0_buf              vbuf;
	int                        rc;

	M0_ASSERT(m0_rpc_at_is_set(&rec->cr_key));
	rc = cas_incoming_kv(rec, &kbuf, &vbuf);
	M0_ASSERT(rc == 0);
	rc = cas_buf_get(key, &kbuf);
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
		anchor->ba_value.b_nob = vbuf.b_nob + sizeof (uint64_t);
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
			m0_be_btree_cursor_get(cur, key, !!(flags & COF_SLANT));
		} else
			m0_be_btree_cursor_next(cur);
		break;
	}
	return m0_be_op_tick_ret(beop, fom0, next);
}

static m0_bcount_t cas_rpc_cutoff(const struct cas_fom *fom)
{
	return cas_in_ut() ? PAGE_SIZE :
		m0_fop_rpc_machine(fom->cf_fom.fo_fop)->rm_bulk_cutoff;
}

static int cas_prep_send(struct cas_fom *fom, enum cas_opcode opc,
			 enum cas_type ct,
			 const struct m0_cas_rec *rec, uint64_t rc)
{
	struct m0_buf  key;
	struct m0_buf  val;
	m0_bcount_t    rpc_cutoff = cas_rpc_cutoff(fom);
	void          *arena = fom->cf_anchor.ba_value.b_addr;

	if (rc == 0) {
		rc = cas_incoming_kv(rec, &key, &val);
		M0_ASSERT(rc == 0);
		switch (COMBINE(opc, ct)) {
		case COMBINE(CO_GET, CT_BTREE):
			rc = cas_place(&fom->cf_out_val,
				       &fom->cf_anchor.ba_value,
				       rpc_cutoff);
			break;
		case COMBINE(CO_GET, CT_META):
		case COMBINE(CO_DEL, CT_META):
		case COMBINE(CO_DEL, CT_BTREE):
			/* Nothing to do: return code is all the user gets. */
			break;
		case COMBINE(CO_PUT, CT_BTREE):
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
			rc = cas_place(&fom->cf_out_key, &key, rpc_cutoff);
			if (ct == CT_BTREE && rc == 0)
				rc = cas_place(&fom->cf_out_val, &val,
					       rpc_cutoff);
			break;
		}
	}
	return rc;
}

static int cas_done(struct cas_fom *fom, struct m0_cas_op *op,
		    struct m0_cas_rep *rep, enum cas_opcode opc)
{
	struct m0_cas_rec *rec_out;
	struct m0_cas_rec *rec;
	int                berc = cas_berc(fom);
	bool               at_fini = true;

	M0_ASSERT(fom->cf_ipos < op->cg_rec.cr_nr);
	rec_out = cas_out_at(rep, fom->cf_opos);
	rec     = cas_at(op, fom->cf_ipos);

	cas_release(fom, &fom->cf_fom);
	if (opc == CO_CUR) {
		fom->cf_curpos++;
		if (rec_out->cr_rc == 0 && berc == 0)
			rec_out->cr_rc = fom->cf_curpos;
		if (berc == 0 && fom->cf_curpos < rec->cr_rc) {
			/* Continue with the same iteration. */
			--fom->cf_ipos;
			at_fini = false;
		} else {
			/*
			 * On BE error, always end the iteration because the
			 * iterator is broken.
			 */
			m0_be_btree_cursor_put(&fom->cf_cur);
			fom->cf_curpos = 0;
		}
	}
	++fom->cf_ipos;
	++fom->cf_opos;
	if (at_fini) {
		/* Finalise input AT buffers. */
		cas_at_fini(&rec->cr_key);
		cas_at_fini(&rec->cr_val);
	}
	/* Buffers are deallocated in cas_at_reply_bufs_fini(). */
	fom->cf_out_key = M0_BUF_INIT0;
	fom->cf_out_val = M0_BUF_INIT0;

	return rec_out->cr_rc;
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

static void cas_meta_selfadd_credit(struct m0_be_btree     *bt,
				    struct m0_be_tx_credit *accum)
{
	m0_be_btree_insert_credit(bt, 1,
				  sizeof(uint64_t) + sizeof(struct m0_fid),
				  sizeof (struct cas_index) + sizeof (uint64_t),
				  accum);
}

static void cas_meta_selfrm_credit(struct m0_be_btree     *bt,
				   struct m0_be_tx_credit *accum)
{
	m0_be_btree_delete_credit(bt, 1,
				  sizeof(uint64_t) + sizeof(struct m0_fid),
				  sizeof (struct cas_index) + sizeof (uint64_t),
				  accum);
}

static void cas_meta_key_fill(void *key)
{
	*(uint64_t *)key = sizeof(struct m0_fid);
	memcpy(key + 8, &m0_cas_meta_fid, sizeof(struct m0_fid));
}

static int cas_meta_selfadd(struct m0_be_btree *meta, struct m0_be_tx *tx)
{
	uint8_t                    key_data[sizeof(uint64_t) +
					    sizeof(struct m0_fid)];
	struct m0_buf              key;
	struct m0_be_btree_anchor  anchor;
	void                      *val_data;
	int                        rc;

	cas_meta_key_fill((void *)&key_data);
	key = M0_BUF_INIT_PTR(&key_data);
	anchor.ba_value.b_nob = sizeof(struct cas_index) + sizeof(uint64_t);
	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_insert_inplace(meta, tx, &op,
					     &key, &anchor), bo_u.u_btree.t_rc);
	/*
	 * It is a stub for meta-index inside itself, inserting records in it is
	 * prohibited. This stub is used to generalise listing indices
	 * operation.
	 */
	if (rc == 0) {
		val_data = anchor.ba_value.b_addr;
		*(uint64_t *)val_data = sizeof(struct cas_index);
	}
	m0_be_btree_release(tx, &anchor);
	return M0_RC(rc);
}

static void cas_meta_selfrm(struct m0_be_btree *meta, struct m0_be_tx *tx)
{
	uint8_t       key_data[sizeof(uint64_t) + sizeof(struct m0_fid)];
	struct m0_buf key;

	cas_meta_key_fill((void *)&key_data);
	key = M0_BUF_INIT_PTR(&key_data);
	M0_BE_OP_SYNC(op, m0_be_btree_delete(meta, tx, &op, &key));
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
	result = m0_be_seg_dict_lookup(seg0, cas_key, (void **)&meta);
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
	cas_meta_selfadd_credit(&dummy, &cred);
	/* Error case: tree destruction and freeing. */
	cas_meta_selfrm_credit(&dummy, &cred);
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
			result = cas_meta_selfadd(bt, &tx) ?:
				 m0_be_seg_dict_insert(seg0, &tx, cas_key,meta);
			if (result == 0) {
				M0_BE_TX_CAPTURE_PTR(seg0, &tx, meta);
				service->c_meta = meta;
			} else {
				cas_meta_selfrm(bt, &tx);
				cas_index_destroy(meta, &tx);
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
	m0_format_header_pack(&index->ci_head, &(struct m0_format_tag){
		.ot_version = M0_EXT_FORMAT_VERSION,
		.ot_type    = M0_FORMAT_TYPE_EXT,
		.ot_footer_offset = offsetof(struct cas_index, ci_foot)
	});
	m0_be_btree_init(&index->ci_tree, seg, &cas_btree_ops);
	m0_long_lock_init(&index->ci_lock);
	m0_format_footer_update(index);
}

static void cas_index_fini(struct cas_index *index)
{
	m0_be_btree_fini(&index->ci_tree);
	m0_long_lock_fini(&index->ci_lock);
}

static int cas_index_create(struct cas_index *index, struct m0_be_tx *tx)
{
	int rc;

	if (M0_FI_ENABLED("ci_create_failure"))
		return M0_ERR(-EFAULT);

	cas_index_init(index, m0_be_domain_seg0_get(tx->t_engine->eng_domain));
	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_create(&index->ci_tree, tx, &op),
			       bo_u.u_btree.t_rc);
	if (rc != 0)
		cas_index_fini(index);
	return M0_RC(rc);
}

static void cas_index_destroy(struct cas_index *index, struct m0_be_tx *tx)
{
	M0_BE_OP_SYNC(op, m0_be_btree_destroy(&index->ci_tree, tx, &op));
	cas_index_fini(index);
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

static void cas_fom_addb2_descr(struct m0_fom *fom)
{
	struct m0_cas_op  *op = cas_op(fom);
	struct m0_cas_rec *rec;
	int                i;

	for (i = 0; i < op->cg_rec.cr_nr; i++) {
		rec = cas_at(op, i);
		M0_ADDB2_ADD(M0_AVI_CAS_KV_SIZES, FID_P(&op->cg_id.ci_fid),
			     m0_rpc_at_len(&rec->cr_key),
			     m0_rpc_at_len(&rec->cr_val));
	}
}

static const struct m0_fom_ops cas_fom_ops = {
	.fo_tick          = &cas_fom_tick,
	.fo_home_locality = &cas_fom_home_locality,
	.fo_fini          = &cas_fom_fini,
	.fo_addb2_descr   = &cas_fom_addb2_descr
};

static const struct m0_fom_type_ops cas_fom_type_ops = {
	.fto_create = &cas_fom_create
};

static struct m0_sm_state_descr cas_fom_phases[] = {
	[CAS_START] = {
		.sd_name      = "start",
		.sd_allowed   = M0_BITS(CAS_META_LOCK, CAS_LOAD_KEY)
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
		.sd_allowed   = M0_BITS(CAS_LOAD_KEY, M0_FOPH_FAILURE)
	},
	[CAS_LOCK] = {
		.sd_name      = "lock",
		.sd_allowed   = M0_BITS(CAS_PREP)
	},
	[CAS_LOAD_KEY] = {
		.sd_name      = "load-key",
		.sd_allowed   = M0_BITS(CAS_LOAD_VAL)
	},
	[CAS_LOAD_VAL] = {
		.sd_name      = "load-value",
		.sd_allowed   = M0_BITS(CAS_LOAD_DONE)
	},
	[CAS_LOAD_DONE] = {
		.sd_name      = "load-done",
		.sd_allowed   = M0_BITS(CAS_LOAD_KEY, CAS_LOCK)
	},
	[CAS_PREP] = {
		.sd_name      = "prep",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_OPEN, M0_FOPH_FAILURE)
	},
	[CAS_LOOP] = {
		.sd_name      = "loop",
		.sd_allowed   = M0_BITS(CAS_PREPARE_SEND, M0_FOPH_SUCCESS)
	},
	[CAS_DONE] = {
		.sd_name      = "done",
		.sd_allowed   = M0_BITS(CAS_LOOP)
	},
	[CAS_PREPARE_SEND] = {
		.sd_name      = "prep-send",
		.sd_allowed   = M0_BITS(CAS_SEND_KEY, CAS_DONE)
	},
	[CAS_SEND_KEY] = {
		.sd_name      = "send-key",
		.sd_allowed   = M0_BITS(CAS_KEY_SENT, CAS_SEND_VAL)
	},
	[CAS_KEY_SENT] = {
		.sd_name      = "key-sent",
		.sd_allowed   = M0_BITS(CAS_SEND_VAL)
	},
	[CAS_SEND_VAL] = {
		.sd_name      = "send-val",
		.sd_allowed   = M0_BITS(CAS_VAL_SENT, CAS_DONE)
	},
	[CAS_VAL_SENT] = {
		.sd_name      = "val-sent",
		.sd_allowed   = M0_BITS(CAS_DONE)
	},
};

struct m0_sm_trans_descr cas_fom_trans[] = {
	[ARRAY_SIZE(m0_generic_phases_trans)] =
	{ "tx-initialised",       M0_FOPH_TXN_OPEN,     CAS_START },
	{ "btree-op?",            CAS_START,            CAS_META_LOCK },
	{ "meta-op?",             CAS_START,            CAS_LOAD_KEY },
	{ "meta-locked",          CAS_META_LOCK,        CAS_META_LOOKUP },
	{ "meta-lookup-launched", CAS_META_LOOKUP,      CAS_META_LOOKUP_DONE },
	{ "key-alloc-failure",    CAS_META_LOOKUP,      M0_FOPH_FAILURE },
	{ "meta-lookup-done",     CAS_META_LOOKUP_DONE, CAS_LOAD_KEY },
	{ "meta-lookup-fail",     CAS_META_LOOKUP_DONE, M0_FOPH_FAILURE },
	{ "index-locked",         CAS_LOCK,             CAS_PREP },
	{ "key-loaded",           CAS_LOAD_KEY,         CAS_LOAD_VAL },
	{ "val-loaded",           CAS_LOAD_VAL,         CAS_LOAD_DONE },
	{ "more-kv-to-load",      CAS_LOAD_DONE,        CAS_LOAD_KEY },
	{ "load-finished",        CAS_LOAD_DONE,        CAS_LOCK },
	{ "tx-credit-calculated", CAS_PREP,             M0_FOPH_TXN_OPEN },
	{ "keys-vals-invalid",    CAS_PREP,             M0_FOPH_FAILURE },
	{ "all-done?",            CAS_LOOP,             M0_FOPH_SUCCESS },
	{ "op-launched",          CAS_LOOP,             CAS_PREPARE_SEND },
	{ "ready-to-send",        CAS_PREPARE_SEND,     CAS_SEND_KEY },
	{ "prep-error",           CAS_PREPARE_SEND,     CAS_DONE },
	{ "key-sent",             CAS_SEND_KEY,         CAS_KEY_SENT },
	{ "skip-key-sending",     CAS_SEND_KEY,         CAS_SEND_VAL },
	{ "send-val",             CAS_KEY_SENT,         CAS_SEND_VAL },
	{ "val-sent",             CAS_SEND_VAL,         CAS_VAL_SENT },
	{ "skip-val-sending",     CAS_SEND_VAL,         CAS_DONE },
	{ "processing-done",      CAS_VAL_SENT,         CAS_DONE },
	{ "goto-next-rec",        CAS_DONE,             CAS_LOOP },

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
