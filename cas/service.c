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
#include "fop/fom_long_lock.h"
#include "fop/fom_generic.h"
#include "fop/fom_interpose.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"               /* m0_reqh */
#include "rpc/rpc_opcodes.h"
#include "rpc/rpc_machine.h"         /* m0_rpc_machine */
#include "conf/schema.h"             /* M0_CST_CAS */
#include "module/instance.h"
#include "addb2/addb2.h"
#include "cas/ctg_store.h"
#include "pool/pool.h"               /* m0_pool_nd_state */

#include "cas/cas_addb2.h"           /* M0_AVI_CAS_KV_SIZES */
#include "cas/cas.h"
#include "cas/cas_xc.h"
#include "dix/fid_convert.h"         /* m0_dix_fid_cctg_device_id */


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
 * - @b Catalogue-index: local (non-distributed) catalogue maintained by
 *   the index subsystem on each node in the pool. When a component catalogue
 *   is created for a distributed index, a record mapping the catalogue to the
 *   index is inserted in the catalogue-index. This record is used by the index
 *   repair and re-balance to find locations of other replicas.
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
 *                               cas_id_check() failed
 *                      FOPH_INIT-------------------->FAILURE
 *                          |     || !cas_is_valid()
 *                          |
 *                   [generic phases]
 *                          .
 *                          .                               meta_op
 *                          .                       +---------------------+
 *                          V                       |                     |
 *                       TXN_INIT-------------->CAS_START<-------+        |
 *                                                  |            |        |
 *                                                  V            |        |
 *                       TXN_OPEN<-----+      CAS_META_LOCK      |        |
 *                          |          |            |            |        |
 *                          |          |            V            |        |
 *                   [generic phases]  |     CAS_META_LOOKUP     |        |
 *                          .          |            |            |        |
 *                          .          |            V            |        |
 *                          .          |   CAS_META_LOOKUP_DONE  |        |
 *                          |          |            |            |        |
 *                          |          |            | ctg_crow   |        |
 *                          |          |            +----------+ |        |
 *                          |          |            |          | |        |
 *                          |          |            |          V |        |
 *                          V          |            |   CAS_CTG_CROW_DONE |
 *     SUCCESS<---------CAS_LOOP<----+ |            V                     |
 *                          |        | |    +->CAS_LOAD_KEY<--------------+
 *                          |        | |    |       |
 *                          |        | |    |       V
 *                          |        | |    |  CAS_LOAD_VAL
 *          ctidx_op_needed |        | |    |       |
 *        +-----------------+        | |    |       V
 *        |                 |        | |    +--CAS_LOAD_DONE
 *        V                 V        | |            |
 *    CAS_CTIDX---->CAS_PREPARE_SEND | |            V
 *                          |        | |        CAS_LOCK------------+
 *                          V        | |            |               |
 *                     CAS_SEND_KEY  | |            |meta_op        |
 *                          |        | |            |               |
 *                          V        | |            V               |
 *                     CAS_SEND_VAL  | |      CAS_CTIDX_LOCK        |
 *                          |        | |            |               |
 *                          V        | |            V               |
 *                      CAS_DONE-----+ +--------CAS_PREP<-----------+
 * @endverbatim
 *
 * @subsection cas-lspec-thread Threading and Concurrency Model
 * Catalogues (including meta catalogue) are protected with "multiple
 * readers/single writer" lock (see m0_cas_ctg::cc_lock). Writer starvation is
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
 * Memory for all indices (including meta-index) is allocated in the first
 * segment (m0_be_domain_seg_first()) of Mero global BE domain
 * (m0::i_be_dom). Address of a meta-index is stored in this segment
 * dictionary. Meta-index btree stores information about existing indices
 * (including itself) as key-value pairs, where key is a FID of an index and
 * value is a pointer to it.
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

struct cas_service {
	struct m0_reqh_service  c_service;
};

struct cas_kv {
	struct m0_buf ckv_key;
	struct m0_buf ckv_val;
};

struct cas_fom {
	struct m0_fom             cf_fom;
	uint64_t                  cf_ipos;
	uint64_t                  cf_opos;
	struct m0_ctg_op          cf_ctg_op;
	struct m0_cas_ctg        *cf_ctg;
	struct m0_long_lock_link  cf_lock;
	struct m0_long_lock_link  cf_meta;
	struct m0_long_lock_link  cf_ctidx;
	uint64_t                  cf_curpos;
	/**
	 * Key/value pairs from incoming FOP.
	 * They are loaded once from incoming RPC AT buffers
	 * (->cr_rec[].cr_{key,val}) during CAS_LOAD_* phases.
	 */
	struct cas_kv            *cf_ikv;
	/** ->cf_ikv array size. */
	uint64_t                  cf_ikv_nr;
	/**
	 * Catalogue identifiers decoded from incoming ->cr_rec[].cr_key buffers
	 * in case of meta request. They are decoded once during request
	 * validation.
	 */
	struct m0_cas_id         *cf_in_cids;
	/** ->cf_in_cids array size. */
	uint64_t                  cf_in_cids_nr;
	struct m0_fom_thralldom   cf_thrall;
	int                       cf_thrall_rc;
	/* ADDB2 structures to collect long-lock contention metrics. */
	struct m0_long_lock_addb2 cf_lock_addb2;
	struct m0_long_lock_addb2 cf_meta_addb2;
	struct m0_long_lock_addb2 cf_ctidx_addb2;
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
	CAS_CTG_CROW_DONE,
	CAS_LOCK,
	CAS_CTIDX_LOCK,
	CAS_CTIDX,
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

M0_BASSERT(M0_CAS_GET_FOP_OPCODE == CO_GET + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_PUT_FOP_OPCODE == CO_PUT + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_DEL_FOP_OPCODE == CO_DEL + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_CUR_FOP_OPCODE == CO_CUR + M0_CAS_GET_FOP_OPCODE);
M0_BASSERT(M0_CAS_REP_FOP_OPCODE == CO_REP + M0_CAS_GET_FOP_OPCODE);

static int    cas_service_start        (struct m0_reqh_service *service);
static void   cas_service_stop         (struct m0_reqh_service *service);
static void   cas_service_fini         (struct m0_reqh_service *service);
static size_t cas_fom_home_locality    (const struct m0_fom *fom);
static int    cas_service_type_allocate(struct m0_reqh_service **service,
					const struct m0_reqh_service_type *st);

static struct m0_cas_op    *cas_op     (const struct m0_fom *fom);
static const struct m0_fid *cas_fid    (const struct m0_fom *fom);
static enum m0_cas_type     cas_type   (const struct m0_fom *fom);
static struct m0_cas_rec   *cas_at     (struct m0_cas_op *op, int idx);
static struct m0_cas_rec   *cas_out_at (const struct m0_cas_rep *rep, int idx);
static bool                 cas_is_ro  (enum m0_cas_opcode opc);
static enum m0_cas_opcode   m0_cas_opcode (const struct m0_fop *fop);
static uint64_t             cas_in_nr  (const struct m0_fop *fop);
static uint64_t             cas_out_nr (const struct m0_fop *fop);
static void                 cas_prep   (struct cas_fom *fom,
					enum m0_cas_opcode opc,
					enum m0_cas_type ct,
					struct m0_cas_ctg *ctg,
					uint64_t rec_pos,
					struct m0_be_tx_credit *accum);
static int                  cas_exec   (struct cas_fom *fom,
					enum m0_cas_opcode opc,
					enum m0_cas_type ct,
					struct m0_cas_ctg *ctg,
					uint64_t rec_pos, int next);

static int  cas_done(struct cas_fom *fom, struct m0_cas_op *op,
		     struct m0_cas_rep *rep, enum m0_cas_opcode opc);

static int  cas_ctidx_exec(struct cas_fom *fom,enum m0_cas_opcode opc,
			   enum m0_cas_type ct, uint64_t rec_pos, uint64_t rc);

static bool cas_ctidx_op_needed(struct cas_fom *fom, enum m0_cas_opcode opc,
				enum m0_cas_type ct, uint64_t rec_pos);

static int  cas_prep_send(struct cas_fom *fom, enum m0_cas_opcode opc,
			  enum m0_cas_type ct, uint64_t rc);

static bool cas_is_valid     (struct cas_fom *fom, enum m0_cas_opcode opc,
			      enum m0_cas_type ct, const struct m0_cas_rec *rec,
			      uint64_t rec_pos);
static int  cas_op_recs_check(struct cas_fom *fom, enum m0_cas_opcode opc,
			      enum m0_cas_type ct, struct m0_cas_op *op);

static bool cas_fom_invariant(const struct cas_fom *fom);

static int  cas_buf_cid_decode(struct m0_buf    *enc_buf,
			       struct m0_cas_id *cid);
static bool cas_fid_is_cctg(const struct m0_fid *fid);
static int  cas_id_check(const struct m0_cas_id *cid,
			 uint32_t                flags);
static int  cas_device_check(struct cas_service     *svc,
			     const struct m0_cas_id *cid);
static int  cas_op_check(struct m0_cas_op *op,
			 struct cas_fom   *fom);

static void cas_incoming_kv(const struct cas_fom *fom,
			    uint64_t              rec_pos,
			    struct m0_buf        *key,
			    struct m0_buf        *val);
static int  cas_incoming_kv_setup(struct cas_fom         *fom,
				  const struct m0_cas_op *op);

static int cas_ctg_crow_handle(struct cas_fom *fom,
			       const struct m0_cas_id *cid);

static const struct m0_reqh_service_ops      cas_service_ops;
static const struct m0_reqh_service_type_ops cas_service_type_ops;
static const struct m0_fom_ops               cas_fom_ops;
static const struct m0_fom_type_ops          cas_fom_type_ops;
static       struct m0_sm_conf               cas_sm_conf;
static       struct m0_sm_state_descr        cas_fom_phases[];

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
	return m0_ctg_store_init();
}

static void cas_service_stop(struct m0_reqh_service *svc)
{
	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STOPPED);
	m0_ctg_store_fini();
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
	struct cas_kv     *ikv;
	uint64_t           in_nr;
	uint64_t           out_nr;

	M0_ALLOC_PTR(fom);
	/**
	 * @todo Validity (cas_is_valid()) of input records is not checked here,
	 * so "out_nr" can be bogus. Cannot check validity at this point,
	 * because ->fto_create() errors are silently ignored.
	 */
	in_nr = cas_in_nr(fop);
	if (in_nr != 0)
		M0_ALLOC_ARR(ikv, in_nr);
	else
		ikv = NULL;

	out_nr = cas_out_nr(fop);
	repfop = m0_fop_reply_alloc(fop, &cas_rep_fopt);
	/**
	 * In case out_nr is 0, M0_ALLOC_ARR returns non-NULL pointer.
	 * Two cases should be distinguished:
	 * - allocate operation has failed and repv is NULL;
	 * - out_nr == 0 and repv is NULL;
	 * The second one is a correct case.
	 */
	if (out_nr != 0)
		M0_ALLOC_ARR(repv, out_nr);
	else
		repv = NULL;
	if (fom != NULL && repfop != NULL &&
	    (in_nr == 0 || ikv != NULL) &&
	    (out_nr == 0 || repv != NULL)) {
		*out = fom0 = &fom->cf_fom;
		fom->cf_ikv = ikv;
		fom->cf_ikv_nr = in_nr;
		repdata = m0_fop_data(repfop);
		repdata->cgr_rep.cr_nr  = out_nr;
		repdata->cgr_rep.cr_rec = repv;
		m0_fom_init(fom0, &fop->f_type->ft_fom_type,
			    &cas_fom_ops, fop, repfop, reqh);
		m0_long_lock_link_init(&fom->cf_lock, fom0,
				       &fom->cf_lock_addb2);
		m0_long_lock_link_init(&fom->cf_meta, fom0,
				       &fom->cf_meta_addb2);
		m0_long_lock_link_init(&fom->cf_ctidx, fom0,
				       &fom->cf_ctidx_addb2);
		return M0_RC(0);
	} else {
		m0_free(ikv);
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

static void cas_incoming_kv(const struct cas_fom *fom,
			    uint64_t              rec_pos,
			    struct m0_buf        *key,
			    struct m0_buf        *val)
{
	*key = fom->cf_ikv[rec_pos].ckv_key;
	*val = fom->cf_ikv[rec_pos].ckv_val;
}

static int cas_incoming_kv_setup(struct cas_fom         *fom,
				 const struct m0_cas_op *op)
{
	uint64_t           i;
	struct m0_cas_rec *rec;
	struct m0_buf     *key;
	struct m0_buf     *val;
	int                rc = 0;

	for (i = 0; i < op->cg_rec.cr_nr && rc == 0; i++) {
		rec = &op->cg_rec.cr_rec[i];
		key = &fom->cf_ikv[i].ckv_key;
		val = &fom->cf_ikv[i].ckv_val;

		M0_ASSERT(m0_rpc_at_is_set(&rec->cr_key));
		rc = m0_rpc_at_get(&rec->cr_key, key);
		if (rc == 0) {
			if (m0_rpc_at_is_set(&rec->cr_val))
				rc = m0_rpc_at_get(&rec->cr_val, val);
			else
				*val = M0_BUF_INIT0;
		}
	}

	return M0_RC(rc);
}

/**
 * Returns AT buffer from client request that is used to transmit key/value from
 * server.
 */
static struct m0_rpc_at_buf *cas_out_complementary(enum m0_cas_opcode      opc,
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
			enum m0_cas_opcode       opc,
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
			enum m0_cas_opcode       opc,
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

static int cas_op_recs_check(struct cas_fom    *fom,
			     enum m0_cas_opcode opc,
			     enum m0_cas_type   ct,
			     struct m0_cas_op  *op)
{
	return op->cg_rec.cr_nr != 0 && op->cg_rec.cr_rec != NULL &&
		m0_forall(i, op->cg_rec.cr_nr,
			 cas_is_valid(fom, opc, ct, cas_at(op, i), i)) ?
		M0_RC(0) : M0_ERR(-EPROTO);
}

static int cas_op_check(struct m0_cas_op *op,
			struct cas_fom   *fom)
{
	struct m0_fom      *fom0    = &fom->cf_fom;
	enum m0_cas_opcode  opc     = m0_cas_opcode(fom0->fo_fop);
	enum m0_cas_type    ct      = cas_type(fom0);
	bool                is_meta = ct == CT_META;
	struct cas_service *service = M0_AMB(service,
					     fom0->fo_service, c_service);
	int                 rc      = 0;

	if (is_meta && opc == CO_PUT &&
	     (cas_op(fom0)->cg_flags & COF_OVERWRITE))
		rc = M0_ERR(-EPROTO);
	if (rc == 0)
		rc = cas_id_check(&op->cg_id, op->cg_flags) ?:
		     cas_device_check(service, &op->cg_id);
	if (rc == 0 && is_meta && fom->cf_ikv_nr != 0) {
		M0_ALLOC_ARR(fom->cf_in_cids, fom->cf_ikv_nr);
		if (fom->cf_in_cids == NULL)
			rc = M0_ERR(-ENOMEM);
	}
	if (rc == 0)
		rc = cas_op_recs_check(fom, opc, ct, op);

	return M0_RC(rc);
}

static void cas_fom_failure(struct cas_fom *fom, int rc, bool ctg_op_fini)
{
	struct m0_fom     *fom0   = &fom->cf_fom;
	struct m0_ctg_op  *ctg_op = &fom->cf_ctg_op;
	struct m0_cas_ctg *meta   = m0_ctg_meta();
	struct m0_cas_ctg *ctidx  = m0_ctg_ctidx();

	M0_PRE(rc < 0);

	m0_long_unlock(&meta->cc_lock, &fom->cf_meta);
	m0_long_unlock(&ctidx->cc_lock, &fom->cf_ctidx);
	if (fom->cf_ctg != NULL)
		m0_long_unlock(&fom->cf_ctg->cc_lock,
			       &fom->cf_lock);
	if (ctg_op_fini) {
		if (m0_ctg_cursor_is_initialised(ctg_op))
			m0_ctg_cursor_fini(ctg_op);
		m0_ctg_op_fini(ctg_op);
	}
	m0_fom_phase_move(fom0, rc, M0_FOPH_FAILURE);
}

static int cas_fom_tick(struct m0_fom *fom0)
{
	uint64_t            i;
	int                 rc;
	int                 result  = M0_FSO_AGAIN;
	struct cas_fom     *fom     = M0_AMB(fom, fom0, cf_fom);
	int                 phase   = m0_fom_phase(fom0);
	struct m0_cas_op   *op      = cas_op(fom0);
	struct m0_cas_rep  *rep     = m0_fop_data(fom0->fo_rep_fop);
	enum m0_cas_opcode  opc     = m0_cas_opcode(fom0->fo_fop);
	enum m0_cas_type    ct      = cas_type(fom0);
	bool                is_meta = ct == CT_META;
	struct m0_cas_ctg  *ctg     = fom->cf_ctg;
	size_t              pos     = fom->cf_ipos;
	struct m0_ctg_op   *ctg_op  = &fom->cf_ctg_op;
	struct cas_service *service = M0_AMB(service,
					     fom0->fo_service, c_service);
	struct m0_cas_ctg  *meta    = m0_ctg_meta();
	struct m0_cas_ctg  *ctidx   = m0_ctg_ctidx();
	struct m0_cas_rec  *rec     = NULL;

	M0_PRE(ctidx != NULL);
	M0_PRE(cas_fom_invariant(fom));
	switch (phase) {
	case M0_FOPH_INIT ... M0_FOPH_NR - 1:
		if (phase == M0_FOPH_INIT) {
			rc = cas_op_check(op, fom);
			if (rc != 0) {
				m0_fom_phase_move(fom0, M0_ERR(rc),
						  M0_FOPH_FAILURE);
				break;
			}
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
		if (is_meta) {
			fom->cf_ctg = meta;
			m0_fom_phase_set(fom0, CAS_LOAD_KEY);
		} else
			m0_fom_phase_set(fom0, CAS_META_LOCK);
		break;
	case CAS_META_LOCK:
		result = m0_long_read_lock(&meta->cc_lock,
					   &fom->cf_meta,
					   CAS_META_LOOKUP);
		result = M0_FOM_LONG_LOCK_RETURN(result);
		break;
	case CAS_META_LOOKUP:
		m0_ctg_op_init(&fom->cf_ctg_op, fom0, 0);
		result = m0_ctg_meta_lookup(ctg_op,
					    &op->cg_id.ci_fid,
					    CAS_META_LOOKUP_DONE);
		break;
	case CAS_META_LOOKUP_DONE:
		rc = m0_ctg_op_rc(ctg_op);
		if (rc == 0) {
			fom->cf_ctg = m0_ctg_meta_lookup_result(ctg_op);
			M0_ASSERT(fom->cf_ctg != NULL);
			m0_fom_phase_set(fom0, CAS_LOAD_KEY);
		} else if (rc == -ENOENT && opc == CO_PUT &&
			   (op->cg_flags & COF_CROW)) {
			m0_long_unlock(&meta->cc_lock, &fom->cf_meta);
			rc = cas_ctg_crow_handle(fom, &op->cg_id);
			if (rc == 0) {
				m0_fom_phase_set(fom0, CAS_CTG_CROW_DONE);
				result = M0_FSO_WAIT;
			}
		}
		m0_ctg_op_fini(ctg_op);
		if (rc != 0)
			cas_fom_failure(fom, rc, false);
		break;
	case CAS_CTG_CROW_DONE:
		if (fom->cf_thrall_rc == 0)
			m0_fom_phase_set(fom0, CAS_START);
		else
			cas_fom_failure(fom, fom->cf_thrall_rc, false);
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
			rc = cas_incoming_kv_setup(fom, op);
			if (rc != 0)
				cas_fom_failure(fom, M0_ERR(rc), false);
			else
				m0_fom_phase_set(fom0, CAS_LOCK);
			break;
		}
		/* Load next key/value. */
		m0_fom_phase_set(fom0, CAS_LOAD_KEY);
		break;
	case CAS_LOCK:
		M0_ASSERT(ctg != NULL);
		result = m0_long_lock(&ctg->cc_lock,
				      !cas_is_ro(opc),
				      &fom->cf_lock,
				      is_meta ? CAS_CTIDX_LOCK : CAS_PREP);
		result = M0_FOM_LONG_LOCK_RETURN(result);
		fom->cf_ipos = 0;
		if (!is_meta)
			m0_long_read_unlock(&meta->cc_lock, &fom->cf_meta);
		break;
	case CAS_CTIDX_LOCK:
		result = m0_long_lock(&m0_ctg_ctidx()->cc_lock, !cas_is_ro(opc),
				      &fom->cf_ctidx, CAS_PREP);
		result = M0_FOM_LONG_LOCK_RETURN(result);
		break;
	case CAS_PREP:
		M0_ASSERT(m0_forall(i, rep->cgr_rep.cr_nr,
				    rep->cgr_rep.cr_rec[i].cr_rc == 0));

		rc = is_meta ? 0 : cas_op_recs_check(fom, opc, ct, op);
		if (rc != 0) {
			cas_fom_failure(fom, M0_ERR(rc), false);
			break;
		}

		for (i = 0; i < op->cg_rec.cr_nr; i++)
			cas_prep(fom, opc, ct, ctg, i,
				 &fom0->fo_tx.tx_betx_cred);
		fom->cf_ipos = 0;
		fom->cf_opos = 0;
		if (opc == CO_CUR) {
			/*
			 * There is only one catalogue operation context for
			 * cursor operation.
			 */
			m0_ctg_op_init(&fom->cf_ctg_op, fom0,
				       cas_op(fom0)->cg_flags);
		}
		m0_fom_phase_set(fom0, M0_FOPH_TXN_OPEN);
		/*
		 * @todo waiting for transaction open with btree (which can be
		 * the meta-catalogue) locked, because tree height has to be
		 * fixed for the correct credit calculation.
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
			m0_long_unlock(&ctg->cc_lock, &fom->cf_lock);
			m0_long_unlock(&ctidx->cc_lock, &fom->cf_ctidx);
			if (opc == CO_CUR) {
				if (m0_ctg_cursor_is_initialised(ctg_op))
					m0_ctg_cursor_fini(ctg_op);
				m0_ctg_op_fini(ctg_op);
			}
			m0_fom_phase_set(fom0, M0_FOPH_SUCCESS);
		} else {
			bool do_ctidx;
			/*
			 * Check whether operation over catalogue-index
			 * index should be done.
			 */
			do_ctidx = cas_ctidx_op_needed(fom, opc, ct, pos);
			result = cas_exec(fom, opc, ct, ctg, pos,
					  do_ctidx ? CAS_CTIDX :
					  CAS_PREPARE_SEND);
		}
		break;
	case CAS_CTIDX:
		M0_ASSERT(fom->cf_opos < rep->cgr_rep.cr_nr);
		rec = cas_out_at(rep, fom->cf_opos);
		M0_ASSERT(rec != NULL);
		if (rec->cr_rc == 0)
			rec->cr_rc = cas_ctidx_exec(fom, opc, ct, pos,
						    m0_ctg_op_rc(ctg_op));
		m0_fom_phase_set(fom0, CAS_PREPARE_SEND);
		break;
	case CAS_PREPARE_SEND:
		M0_ASSERT(fom->cf_opos < rep->cgr_rep.cr_nr);
		rec = cas_out_at(rep, fom->cf_opos);
		M0_ASSERT(rec != NULL);
		if (rec->cr_rc == 0)
			rec->cr_rc = cas_prep_send(fom, opc, ct,
						   m0_ctg_op_rc(ctg_op));
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

static void cas_fom_fini(struct m0_fom *fom0)
{
	struct cas_fom *fom = M0_AMB(fom, fom0, cf_fom);
	uint64_t        i;

	if (cas_in_ut() && cas__ut_cb_done != NULL)
		cas__ut_cb_done(fom0);

	for (i = 0; i < fom->cf_in_cids_nr; i++)
		m0_cas_id_fini(&fom->cf_in_cids[i]);
	m0_free(fom->cf_in_cids);
	m0_free(fom->cf_ikv);
	m0_long_lock_link_fini(&fom->cf_meta);
	m0_long_lock_link_fini(&fom->cf_lock);
	m0_long_lock_link_fini(&fom->cf_ctidx);
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

static enum m0_cas_opcode m0_cas_opcode(const struct m0_fop *fop)
{
	enum m0_cas_opcode opcode;

	opcode = fop->f_item.ri_type->rit_opcode - M0_CAS_GET_FOP_OPCODE;
	M0_ASSERT(0 <= opcode && opcode < CO_NR);
	return opcode;
}

static int cas_sdev_state(struct m0_poolmach    *pm,
			  uint32_t               sdev_idx,
			  enum m0_pool_nd_state *state_out)
{
	int i;

	if (M0_FI_ENABLED("sdev_fail")) {
		*state_out = M0_PNDS_FAILED;
		return 0;
	}
	for (i = 0; i < pm->pm_state->pst_nr_devices; i++) {
		struct m0_pooldev *sdev = &pm->pm_state->pst_devices_array[i];

		if (sdev->pd_sdev_idx == sdev_idx) {
			*state_out = sdev->pd_state;
			return 0;
		}
	}
	return M0_ERR(-EINVAL);
}

/**
 * Checks that device where a component catalogue with a given cid resides is
 * available (online or rebalancing).
 *
 * Client should send requests against catalogues which are available. DIX
 * degraded mode logic is responsible to guarantee this.
 */
static int cas_device_check(struct cas_service     *svc,
			    const struct m0_cas_id *cid)
{
	uint32_t                device_id;
	enum m0_pool_nd_state   state;
	struct m0_pool_version *pver;
	struct m0_poolmach     *pm;
	int                     rc = 0;

	M0_PRE(svc != NULL);
	if (cas_fid_is_cctg(&cid->ci_fid)) {
		device_id = m0_dix_fid_cctg_device_id(&cid->ci_fid);
		pver = m0_pool_version_find(svc->c_service.rs_reqh->rh_pools,
					    &cid->ci_layout.u.dl_desc.ld_pver);
		if (pver != NULL) {
			pm = &pver->pv_mach;
			rc = cas_sdev_state(pm, device_id, &state);
			if (rc == 0 && !M0_IN(state, (M0_PNDS_ONLINE,
						      M0_PNDS_SNS_REBALANCING)))
				rc = M0_ERR(-EBADFD);
		} else
			rc = M0_ERR(-EINVAL);
	}
	return M0_RC(rc);
}

static int cas_id_check(const struct m0_cas_id *cid,
			uint32_t                flags)
{
	const struct m0_dix_layout *layout;
	struct m0_dix_layout       *stored_layout;
	int                         rc = 0;

	/* Check fid. */
	if (!m0_fid_is_valid(&cid->ci_fid) ||
	    !M0_IN(m0_fid_type_getfid(&cid->ci_fid), (&m0_cas_index_fid_type,
						      &m0_cctg_fid_type)))
		return M0_ERR(-EPROTO);

	if (cas_fid_is_cctg(&cid->ci_fid)) {
		layout = &cid->ci_layout;
		if (layout->dl_type == DIX_LTYPE_DESCR) {
			rc = m0_ctg_ctidx_lookup(&cid->ci_fid, &stored_layout);
			/*
			 * It's OK to not find layout with CROW flag set,
			 * because the catalogue may be not created yet.
			 */
			if (rc == -ENOENT && (flags & COF_CROW))
				rc = 0;
			/* Match stored and received layouts. */
			else if (rc == 0 &&
				 !m0_dix_layout_eq(layout, stored_layout))
				rc = M0_ERR(-EKEYEXPIRED);
		} else
			rc = M0_ERR(-EPROTO);
	}

	return M0_RC(rc);
}

static bool cas_is_valid(struct cas_fom *fom, enum m0_cas_opcode opc,
			 enum m0_cas_type ct, const struct m0_cas_rec *rec,
			 uint64_t rec_pos)
{
	bool result;
	bool gotkey;
	bool gotval;
	bool meta = ct == CT_META;

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
		int                        rc;
		struct m0_cas_id           cid = {};
		struct m0_buf              key;
		const struct m0_dix_imask *imask;

		/*
		 * Valid key is sent inline always, so result
		 * should be 0.
		 * Key is encoded m0_cas_id in meta case.
		 */
		rc = m0_rpc_at_get(&rec->cr_key, &key) ?:
			cas_buf_cid_decode(&key, &cid);
		if (rc == 0) {
			imask = &cid.ci_layout.u.dl_desc.ld_imask;

			result = m0_fid_is_valid(&cid.ci_fid) &&
				M0_IN(m0_fid_type_getfid(&cid.ci_fid),
				      (&m0_cas_index_fid_type,
				       &m0_cctg_fid_type));
			if (result) {
				if (cas_fid_is_cctg(&cid.ci_fid))
					result = (imask->im_range == NULL) ==
						(imask->im_nr == 0);
				else
					result = (imask->im_range == NULL &&
						  imask->im_nr == 0);
			}
		} else
			result = false;

		if (result) {
			fom->cf_in_cids[rec_pos] = cid;
			fom->cf_in_cids_nr++;
		}
	}
	return M0_RC(result);
}

static bool cas_is_ro(enum m0_cas_opcode opc)
{
	return M0_IN(opc, (CO_GET, CO_CUR, CO_REP));
}

static enum m0_cas_type cas_type(const struct m0_fom *fom)
{
	if (m0_fid_eq(cas_fid(fom), &m0_cas_meta_fid))
		return CT_META;
	else
		return CT_BTREE;
}

static uint64_t cas_in_nr(const struct m0_fop *fop)
{
	const struct m0_cas_op *op = m0_fop_data(fop);

	return op->cg_rec.cr_nr;
}

static uint64_t cas_out_nr(const struct m0_fop *fop)
{
	const struct m0_cas_op *op = m0_fop_data(fop);
	uint64_t                nr;

	nr = op->cg_rec.cr_nr;
	if (m0_cas_opcode(fop) == CO_CUR)
		nr = m0_reduce(i, nr, 0, + op->cg_rec.cr_rec[i].cr_rc);
	return nr;
}

static int cas_buf_cid_decode(struct m0_buf    *enc_buf,
			      struct m0_cas_id *cid)
{
	M0_PRE(enc_buf != NULL);
	M0_PRE(cid != NULL);

	M0_PRE(M0_IS0(cid));

	return m0_xcode_obj_dec_from_buf(
		&M0_XCODE_OBJ(m0_cas_id_xc, cid),
		&enc_buf->b_addr, &enc_buf->b_nob);
}

static bool cas_fid_is_cctg(const struct m0_fid *fid)
{
	M0_PRE(fid != NULL);
	return m0_fid_type_getfid(fid) == &m0_cctg_fid_type;
}

static int cas_place(struct m0_buf *dst, struct m0_buf *src, m0_bcount_t cutoff)
{
	int result = 0;

	if (M0_FI_ENABLED("place_fail"))
		return M0_ERR(-ENOMEM);

	if (src->b_nob >= cutoff) {
		dst->b_addr = m0_alloc_aligned(src->b_nob, PAGE_SHIFT);
		if (dst->b_addr == NULL)
			return M0_ERR(-ENOMEM);
		dst->b_nob = src->b_nob;
		memcpy(dst->b_addr, src->b_addr, src->b_nob);
	} else {
		result = m0_buf_copy(dst, src);
	}
	return M0_RC(result);
}

/**
 * Returns number of bytes required for key/value stored in btree given
 * user-supplied key/value buffer.
 *
 * Key/value stored in btree have first 8 bytes defining length and successive
 * bytes storing key/value.
 */
static m0_bcount_t cas_kv_nob(const struct m0_buf *inbuf)
{
	return inbuf->b_nob + sizeof(uint64_t);
}

static void cas_prep(struct cas_fom *fom, enum m0_cas_opcode opc,
		     enum m0_cas_type ct, struct m0_cas_ctg *ctg,
		     uint64_t rec_pos, struct m0_be_tx_credit *accum)
{
	struct m0_cas_id *cid;
	struct m0_buf     key;
	struct m0_buf     val;
	m0_bcount_t       knob;
	m0_bcount_t       vnob;

	cas_incoming_kv(fom, rec_pos, &key, &val);
	knob = cas_kv_nob(&key);
	vnob = cas_kv_nob(&val);
	switch (CTG_OP_COMBINE(opc, ct)) {
	case CTG_OP_COMBINE(CO_PUT, CT_META):
	case CTG_OP_COMBINE(CO_DEL, CT_META):
		M0_ASSERT(vnob == sizeof(uint64_t));
		cid = &fom->cf_in_cids[rec_pos];
		if (cas_fid_is_cctg(&cid->ci_fid)) {
			if (opc == CO_PUT)
				m0_ctg_ctidx_insert_credits(cid, accum);
			else
				m0_ctg_ctidx_delete_credits(cid, accum);
		}
		if (opc == CO_PUT)
			m0_ctg_meta_insert_credit(accum);
		else
			m0_ctg_meta_delete_credit(accum);
		break;
	case CTG_OP_COMBINE(CO_PUT, CT_BTREE):
	case CTG_OP_COMBINE(CO_DEL, CT_BTREE):
		if (opc == CO_PUT)
			m0_ctg_insert_credit(ctg, knob, vnob, accum);
		else
			m0_ctg_delete_credit(ctg, knob, vnob, accum);
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

static int cas_exec(struct cas_fom *fom, enum m0_cas_opcode opc,
		    enum m0_cas_type ct, struct m0_cas_ctg *ctg,
		    uint64_t rec_pos, int next)
{
	struct m0_ctg_op          *ctg_op = &fom->cf_ctg_op;
	struct m0_fom             *fom0   = &fom->cf_fom;
	uint32_t                   flags  = cas_op(fom0)->cg_flags;
	struct m0_buf              kbuf;
	struct m0_buf              vbuf;
	struct m0_cas_id          *cid;
	enum m0_fom_phase_outcome  ret = M0_FSO_AGAIN;

	cas_incoming_kv(fom, rec_pos, &kbuf, &vbuf);
	if (ct == CT_META)
		cid = &fom->cf_in_cids[rec_pos];
	else
		/*
		 * Initialise cid to NULL to suppress strange compiler warning.
		 * Cid is used only for meta operations, but compiler complains
		 * that it may be uninitialised.
		 */
		cid = NULL;

	if (opc != CO_CUR)
		m0_ctg_op_init(&fom->cf_ctg_op, fom0, flags);

	switch (CTG_OP_COMBINE(opc, ct)) {
	case CTG_OP_COMBINE(CO_GET, CT_BTREE):
		ret = m0_ctg_lookup(ctg_op, ctg, &kbuf, next);
		break;
	case CTG_OP_COMBINE(CO_PUT, CT_BTREE):
		ret = m0_ctg_insert(ctg_op, ctg, &kbuf, &vbuf, next);
		break;
	case CTG_OP_COMBINE(CO_DEL, CT_BTREE):
		ret = m0_ctg_delete(ctg_op, ctg, &kbuf, next);
		break;
	case CTG_OP_COMBINE(CO_GET, CT_META):
		ret = m0_ctg_meta_lookup(ctg_op, &cid->ci_fid, next);
		break;
	case CTG_OP_COMBINE(CO_PUT, CT_META):
		ret = m0_ctg_meta_insert(ctg_op, &cid->ci_fid, next);
		break;
	case CTG_OP_COMBINE(CO_DEL, CT_META):
		/**
		 * @todo delete the btree in META case.
		 */
		ret = m0_ctg_meta_delete(ctg_op, &cid->ci_fid, next);
		break;
	case CTG_OP_COMBINE(CO_CUR, CT_BTREE):
	case CTG_OP_COMBINE(CO_CUR, CT_META):
		if (fom->cf_curpos == 0) {
			if (!m0_ctg_cursor_is_initialised(ctg_op)) {
				if (ct == CT_META)
					m0_ctg_meta_cursor_init(ctg_op);
				else
					m0_ctg_cursor_init(ctg_op, ctg);
			}
			if (ct == CT_META)
				m0_ctg_meta_cursor_get(ctg_op, &cid->ci_fid,
						       next);
			else
				m0_ctg_cursor_get(ctg_op, &kbuf, next);
		} else
			m0_ctg_cursor_next(ctg_op, next);
		break;
	}

	return ret;
}

static bool cas_ctidx_op_needed(struct cas_fom *fom, enum m0_cas_opcode opc,
				enum m0_cas_type ct, uint64_t rec_pos)
{
	struct m0_cas_id *cid;
	bool              is_needed = false;

	switch (CTG_OP_COMBINE(opc, ct)) {
	case CTG_OP_COMBINE(CO_PUT, CT_META):
	case CTG_OP_COMBINE(CO_DEL, CT_META):
		cid = &fom->cf_in_cids[rec_pos];
		if (cas_fid_is_cctg(&cid->ci_fid))
			is_needed = true;
		break;
	default:
		break;
	}

	return is_needed;
}

static int cas_ctidx_exec(struct cas_fom *fom, enum m0_cas_opcode opc,
			  enum m0_cas_type ct, uint64_t rec_pos,
			  uint64_t rc)
{
	struct m0_cas_id *cid;
	struct m0_be_tx  *tx = &fom->cf_fom.fo_tx.tx_betx;

	if (rc == 0) {
		switch (CTG_OP_COMBINE(opc, ct)) {
		case CTG_OP_COMBINE(CO_PUT, CT_META):
		case CTG_OP_COMBINE(CO_DEL, CT_META):
			cid = &fom->cf_in_cids[rec_pos];
			M0_ASSERT(cas_fid_is_cctg(&cid->ci_fid));
			if (opc == CO_PUT)
				rc = m0_ctg_ctidx_insert(cid, tx);
			else
				rc = m0_ctg_ctidx_delete(cid, tx);
			break;
		default:
			/* Nothing to do. */
			break;
		}
	}

	return rc;
}

static m0_bcount_t cas_rpc_cutoff(const struct cas_fom *fom)
{
	return cas_in_ut() ? PAGE_SIZE :
		m0_fop_rpc_machine(fom->cf_fom.fo_fop)->rm_bulk_cutoff;
}

static int cas_prep_send(struct cas_fom *fom, enum m0_cas_opcode opc,
			 enum m0_cas_type ct, uint64_t rc)
{
	struct m0_buf     key;
	struct m0_buf     val;
	struct m0_ctg_op *ctg_op     = &fom->cf_ctg_op;
	m0_bcount_t       rpc_cutoff = cas_rpc_cutoff(fom);

	if (rc == 0) {
		switch (CTG_OP_COMBINE(opc, ct)) {
		case CTG_OP_COMBINE(CO_GET, CT_BTREE):
			m0_ctg_lookup_result(ctg_op, &val);
			rc = cas_place(&fom->cf_out_val,
				       &val,
				       rpc_cutoff);
			break;
		case CTG_OP_COMBINE(CO_GET, CT_META):
		case CTG_OP_COMBINE(CO_DEL, CT_META):
		case CTG_OP_COMBINE(CO_DEL, CT_BTREE):
		case CTG_OP_COMBINE(CO_PUT, CT_BTREE):
		case CTG_OP_COMBINE(CO_PUT, CT_META):
			/* Nothing to do: return code is all the user gets. */
			break;
		case CTG_OP_COMBINE(CO_CUR, CT_BTREE):
		case CTG_OP_COMBINE(CO_CUR, CT_META):
			m0_ctg_cursor_kv_get(ctg_op, &key, &val);
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
		    struct m0_cas_rep *rep, enum m0_cas_opcode opc)
{
	struct m0_cas_rec *rec_out;
	struct m0_cas_rec *rec;
	int                ctg_rc = m0_ctg_op_rc(&fom->cf_ctg_op);
	int                rc;
	bool               at_fini = true;

	M0_ASSERT(fom->cf_ipos < op->cg_rec.cr_nr);
	rec_out = cas_out_at(rep, fom->cf_opos);
	rec     = cas_at(op, fom->cf_ipos);

	rc = rec_out->cr_rc;
	if (opc == CO_CUR) {
		fom->cf_curpos++;
		if (rc == 0 && ctg_rc == 0)
			rc = fom->cf_curpos;
		if (ctg_rc == 0 && fom->cf_curpos < rec->cr_rc) {
			/* Continue with the same iteration. */
			--fom->cf_ipos;
			at_fini = false;
		} else {
			/*
			 * End the iteration on ctg cursor error because it
			 * doesn't make sense to continue with broken iterator.
			 */
			m0_ctg_cursor_put(&fom->cf_ctg_op);
			fom->cf_curpos = 0;
		}
	} else
		m0_ctg_op_fini(&fom->cf_ctg_op);

	++fom->cf_ipos;
	++fom->cf_opos;
	/*
	 * Overwrite return code of put operation if key is already exist and
	 * COF_CREATE is set or overwrite return code of del operation if key
	 * is not found and COF_CROW is set.
	 */
	if ((opc == CO_PUT && rc == -EEXIST && (op->cg_flags & COF_CREATE)) ||
	    (opc == CO_DEL && rc == -ENOENT && (op->cg_flags & COF_CROW)))
		rc = 0;

	M0_LOG(M0_DEBUG, "pos %zu: rc %d", fom->cf_opos, rc);
	rec_out->cr_rc = rc;
	if (at_fini) {
		/* Finalise input AT buffers. */
		cas_at_fini(&rec->cr_key);
		cas_at_fini(&rec->cr_val);
	}
	/*
	 * Out buffers are passed to RPC AT layer. They will be deallocated
	 * automatically as part of a reply FOP.
	 */
	fom->cf_out_key = M0_BUF_INIT0;
	fom->cf_out_val = M0_BUF_INIT0;

	return rec_out->cr_rc;
}

static int cas_ctg_crow_fop_buf_prepare(const struct m0_cas_id *cid,
					struct m0_rpc_at_buf   *at_buf)
{
	struct m0_buf buf;
	int           rc;

	M0_PRE(cid != NULL);
	M0_PRE(at_buf != NULL);

	m0_rpc_at_init(at_buf);
	rc = m0_xcode_obj_enc_to_buf(&M0_XCODE_OBJ(m0_cas_id_xc,
						   (struct m0_cas_id *)cid),
				     &buf.b_addr, &buf.b_nob);
	if (rc == 0) {
		at_buf->ab_type  = M0_RPC_AT_INLINE;
		at_buf->u.ab_buf = buf;
	}
	return rc;
}

static int cas_ctg_crow_fop_create(const struct m0_cas_id  *cid,
				   struct m0_fop          **out)
{
	struct m0_cas_op  *op;
	struct m0_cas_rec *rec;
	struct m0_fop     *fop;
	int                rc = 0;

	*out = NULL;

	M0_ALLOC_PTR(op);
	M0_ALLOC_PTR(rec);
	M0_ALLOC_PTR(fop);
	if (op == NULL || rec == NULL || fop == NULL)
		rc = -ENOMEM;
	if (rc == 0) {
		rc = cas_ctg_crow_fop_buf_prepare(cid, &rec->cr_key);
		if (rc == 0) {
			op->cg_id.ci_fid = m0_cas_meta_fid;
			op->cg_rec.cr_nr = 1;
			op->cg_rec.cr_rec = rec;
			m0_fop_init(fop, &cas_put_fopt, op, &m0_fop_release);
			*out = fop;
		}
	}
	if (rc != 0) {
		m0_free(op);
		m0_free(rec);
		m0_free(fop);
	}
	return rc;
}

static void cas_ctg_crow_done_cb(struct m0_fom_thralldom *thrall,
				 struct m0_fom           *serf)
{
	struct cas_fom    *master = M0_AMB(master, thrall, cf_thrall);
	struct m0_cas_rep *rep;
	int                rc;

	rc = m0_fom_rc(serf);
	if (rc == 0) {
		M0_ASSERT(serf->fo_rep_fop != NULL);
		rep = (struct m0_cas_rep *)m0_fop_data(serf->fo_rep_fop);
		M0_ASSERT(rep != NULL);
		rc = rep->cgr_rc;
		if (rc == 0) {
			M0_ASSERT(rep->cgr_rep.cr_nr == 1);
			rc = rep->cgr_rep.cr_rec[0].cr_rc;
		}
	}
	master->cf_thrall_rc = rc;
}

static int cas_ctg_crow_handle(struct cas_fom *fom, const struct m0_cas_id *cid)
{
	struct m0_fop         *fop;
	struct m0_fom         *new_fom0;
	struct m0_reqh        *reqh;
	struct m0_rpc_machine *mach;
	int                    rc;

	/*
	 * Create fop to create component catalogue. For a new CAS FOM this FOP
	 * will appear as arrived from the network. FOP will be deallocated by a
	 * new CAS FOM.
	 */
	rc = cas_ctg_crow_fop_create(cid, &fop);
	if (rc == 0) {
		reqh = fom->cf_fom.fo_service->rs_reqh;
		mach = m0_reqh_rpc_mach_tlist_head(&reqh->rh_rpc_machines);
		m0_fop_rpc_machine_set(fop, mach);
		fop->f_item.ri_session = fom->cf_fom.fo_fop->f_item.ri_session;
		rc = cas_fom_create(fop, &new_fom0, reqh);
		if (rc == 0) {
			new_fom0->fo_local = true;
			m0_fom_enthrall(&fom->cf_fom,
					new_fom0,
					&fom->cf_thrall,
					&cas_ctg_crow_done_cb);
			m0_fom_queue(new_fom0);
		}
		/*
		 * New FOM got reference to FOP, release ref counter as this
		 * FOP is not needed here.
		 */
		m0_fop_put_lock(fop);
	}
	return rc;
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
		.sd_allowed   = M0_BITS(CAS_CTG_CROW_DONE, CAS_LOAD_KEY,
					M0_FOPH_FAILURE)
	},
	[CAS_CTG_CROW_DONE] = {
		.sd_name      = "ctg-crow-done",
		.sd_allowed   = M0_BITS(CAS_START, M0_FOPH_FAILURE)
	},
	[CAS_LOCK] = {
		.sd_name      = "lock",
		.sd_allowed   = M0_BITS(CAS_CTIDX_LOCK, CAS_PREP)
	},
	[CAS_CTIDX_LOCK] = {
		.sd_name      = "ctidx_lock",
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
		.sd_allowed   = M0_BITS(CAS_CTIDX, CAS_PREPARE_SEND,
					M0_FOPH_SUCCESS)
	},
	[CAS_CTIDX] = {
		.sd_name      = "ctidx",
		.sd_allowed   = M0_BITS(CAS_PREPARE_SEND)
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
	{ "ctg-crow-done",        CAS_META_LOOKUP_DONE, CAS_CTG_CROW_DONE },
	{ "ctg-crow-success",     CAS_CTG_CROW_DONE,    CAS_START },
	{ "ctg-crow-fail",        CAS_CTG_CROW_DONE,    M0_FOPH_FAILURE },
	{ "index-locked",         CAS_LOCK,             CAS_PREP },
	{ "key-loaded",           CAS_LOAD_KEY,         CAS_LOAD_VAL },
	{ "val-loaded",           CAS_LOAD_VAL,         CAS_LOAD_DONE },
	{ "more-kv-to-load",      CAS_LOAD_DONE,        CAS_LOAD_KEY },
	{ "meta-locked",          CAS_LOCK,             CAS_CTIDX_LOCK },
	{ "ctidx-locked",         CAS_CTIDX_LOCK,       CAS_PREP },
	{ "load-finished",        CAS_LOAD_DONE,        CAS_LOCK },
	{ "tx-credit-calculated", CAS_PREP,             M0_FOPH_TXN_OPEN },
	{ "keys-vals-invalid",    CAS_PREP,             M0_FOPH_FAILURE },
	{ "all-done?",            CAS_LOOP,             M0_FOPH_SUCCESS },
	{ "op-launched",          CAS_LOOP,             CAS_CTIDX },
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
	{ "ctidx-inserted",       CAS_CTIDX,            CAS_PREPARE_SEND },
	{ "key-add-reply",        CAS_DONE,             CAS_LOOP },

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
