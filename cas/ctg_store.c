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
 * Original author: Sergey Shilov <sergey.shilov@seagate.com>
 * Original creation date: 15-Aug-2016
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CAS

#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "lib/assert.h"
#include "lib/errno.h"               /* ENOMEM, EPROTO */
#include "lib/ext.h"                 /* m0_ext */
#include "be/domain.h"               /* m0_be_domain_seg_first */
#include "be/op.h"
#include "module/instance.h"

#include "cas/ctg_store.h"

enum {
	M0_CAS_STATE_FORMAT_VERSION = 1,
	M0_FORMAT_TYPE_CAS_STATE    = 1,
	/**
	 * Every key and value is stored with an individual 64-bit header.
	 * Currently header contains key/value length in bytes.
	 */
	KV_HDR_SIZE                 = sizeof(uint64_t)
};

/** Catalogue store state persisted on a disk. */
struct m0_cas_state {
        struct m0_format_header cs_header;
	/* Meta catalogue. */
        struct m0_cas_ctg       cs_meta;
        /**
	 * Total number of records in all catalogues in catalogue store. It's
	 * used by repair/re-balance services to report progress.
	 */
        uint64_t                cs_rec_nr;
        /**
	 * Total size of all records in catalogue store. The value is
	 * incremented on every record insertion. On record deletion it's not
	 * decremented, because before record deletion the size of the value is
	 * unknown.
	 */
        uint64_t                cs_rec_size;
        struct m0_format_footer cs_footer;
};

struct m0_ctg_store {
	/** The part of a catalogue store persisted on a disk. */
	struct m0_cas_state *cs_state;

	/** Mutex to protect cs_state counters updates. */
	struct m0_mutex      cs_state_mutex;

	/** Catalogue-index catalogue. */
	struct m0_cas_ctg   *cs_ctidx;

	/**
	 * Reference counter for number of catalogue store users.
	 * When it drops to 0, catalogue store structure is finalised.
	 */
	struct m0_ref        cs_ref;

	/**
	 * Flag indicating whether catalogue store is initialised or not.
	 */
	bool                 cs_initialised;
};

enum cursor_phase {
	CPH_NONE = 0,
	CPH_INIT,
	CPH_GET,
	CPH_NEXT
};

static struct m0_be_seg *cas_seg(void);

static int  ctg_berc           (struct m0_ctg_op *ctg_op);
static int  ctg_buf            (const struct m0_buf *val, struct m0_buf *buf);
static int  ctg_buf_get        (struct m0_buf *dst, const struct m0_buf *src);
static void ctg_fid_key_fill   (void *key, const struct m0_fid *fid);
static void ctg_init           (struct m0_cas_ctg *ctg, struct m0_be_seg *seg);
static void ctg_fini           (struct m0_cas_ctg *ctg);
static int  ctg_create         (struct m0_cas_ctg *ctg, struct m0_be_tx *tx);
static void ctg_destroy        (struct m0_cas_ctg *ctg, struct m0_be_tx *tx);
static int  ctg_meta_find_ctidx(struct m0_cas_ctg  *meta,
				struct m0_cas_ctg **ctidx);
static int  ctg_meta_insert    (struct m0_be_btree *meta,
				struct m0_fid      *fid,
				struct m0_cas_ctg  *ctg,
				struct m0_be_tx    *tx);
static int  ctg_meta_selfadd   (struct m0_be_btree *meta, struct m0_be_tx *tx);
static void ctg_meta_delete    (struct m0_be_btree *meta,
				struct m0_fid      *fid,
				struct m0_be_tx    *tx);
static void ctg_meta_selfrm    (struct m0_be_btree *meta, struct m0_be_tx *tx);

static void ctg_meta_insert_credit   (struct m0_be_btree     *bt,
				      m0_bcount_t             nr,
				      struct m0_be_tx_credit *accum);
static void ctg_meta_delete_credit   (struct m0_be_btree     *bt,
				      m0_bcount_t             nr,
				      struct m0_be_tx_credit *accum);
static void ctg_store_init_creds_calc(struct m0_be_seg       *seg,
				      struct m0_cas_state    *state,
				      struct m0_cas_ctg      *ctidx,
				      struct m0_be_tx_credit *cred);
static void ctg_ctidx_destroy(struct m0_cas_ctg *ctidx,
			      struct m0_be_seg *seg,
			      struct m0_be_tx  *tx);

static int  ctg_op_tick_ret  (struct m0_ctg_op *ctg_op, int next_state);
static int  ctg_op_exec      (struct m0_ctg_op *ctg_op, int next_phase);
static int  ctg_meta_exec    (struct m0_ctg_op    *ctg_op,
			      const struct m0_fid *fid,
			      int                  next_phase);
static int  ctg_exec         (struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      int                  next_phase);
static void ctg_store_release(struct m0_ref *ref);

static m0_bcount_t ctg_ksize (const void *key);
static m0_bcount_t ctg_vsize (const void *val);
static int         ctg_cmp   (const void *key0, const void *key1);


/**
 * Mutex to provide thread-safety for catalogue store singleton initialisation.
 */
static struct m0_mutex cs_init_guard = M0_MUTEX_SINIT(&cs_init_guard);

static const struct m0_be_btree_kv_ops cas_btree_ops;
static       struct m0_ctg_store       ctg_store       = {};
static const        char               cas_state_key[] = "cas-state-nr";

static struct m0_be_seg *cas_seg(void)
{
	struct m0_be_domain *dom = m0_get()->i_be_dom;
	struct m0_be_seg    *seg = m0_be_domain_seg_first(dom);

	if (seg == NULL && cas_in_ut())
		seg = m0_be_domain_seg0_get(dom);
	return seg;
}

static struct m0_be_op *ctg_beop(struct m0_ctg_op *ctg_op)
{
	return ctg_op->co_opcode == CO_CUR ?
		&ctg_op->co_cur.bc_op : &ctg_op->co_beop;
}

static int ctg_berc(struct m0_ctg_op *ctg_op)
{
	return ctg_beop(ctg_op)->bo_u.u_btree.t_rc;
}

static void ctg_memcpy(void *dst, const void *src, uint64_t nob)
{
	*(uint64_t *)dst = nob;
	memcpy(dst + KV_HDR_SIZE, src, nob);
}

static int ctg_buf_get(struct m0_buf *dst, const struct m0_buf *src)
{
	m0_bcount_t nob = src->b_nob;

	if (M0_FI_ENABLED("cas_alloc_fail"))
		return M0_ERR(-ENOMEM);
	dst->b_nob  = src->b_nob + KV_HDR_SIZE;
	dst->b_addr = m0_alloc(dst->b_nob);
	if (dst->b_addr != NULL) {
		ctg_memcpy(dst->b_addr, src->b_addr, nob);
		return M0_RC(0);
	} else
		return M0_ERR(-ENOMEM);
}

static int ctg_buf(const struct m0_buf *val, struct m0_buf *buf)
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
		return M0_ERR_INFO(result, "Unexpected: %"PRIx64"/%"PRIx64,
				   val->b_nob, buf->b_nob);
	else
		return M0_RC(result);
}

static void ctg_fid_key_fill(void *key, const struct m0_fid *fid)
{
	ctg_memcpy(key, fid, sizeof(struct m0_fid));
}

static m0_bcount_t ctg_ksize(const void *key)
{
	/* Size is stored in the header. */
	return KV_HDR_SIZE + *(const uint64_t *)key;
}

static m0_bcount_t ctg_vsize(const void *val)
{
	return ctg_ksize(val);
}

static int ctg_cmp(const void *key0, const void *key1)
{
	m0_bcount_t knob0 = ctg_ksize(key0);
	m0_bcount_t knob1 = ctg_ksize(key1);
	/**
	 * @todo Cannot assert on on-disk data, but no interface to report
	 * errors from here.
	 */
	M0_ASSERT(knob0 >= 8);
	M0_ASSERT(knob1 >= 8);

	return memcmp(key0 + 8, key1 + 8, min_check(knob0, knob1) - 8) ?:
		M0_3WAY(knob0, knob1);
}

static void ctg_init(struct m0_cas_ctg *ctg, struct m0_be_seg *seg)
{
	m0_format_header_pack(&ctg->cc_head, &(struct m0_format_tag){
		.ot_version = M0_EXT_FORMAT_VERSION,
		.ot_type    = M0_FORMAT_TYPE_EXT,
		.ot_footer_offset = offsetof(struct m0_cas_ctg, cc_foot)
	});
	m0_be_btree_init(&ctg->cc_tree, seg, &cas_btree_ops);
	m0_long_lock_init(&ctg->cc_lock);
	m0_format_footer_update(ctg);
}

static void ctg_fini(struct m0_cas_ctg *ctg)
{
	m0_be_btree_fini(&ctg->cc_tree);
	m0_long_lock_fini(&ctg->cc_lock);
}

static int ctg_create(struct m0_cas_ctg *ctg, struct m0_be_tx *tx)
{
	int rc;

	if (M0_FI_ENABLED("ctg_create_failure"))
		return M0_ERR(-EFAULT);

	ctg_init(ctg, cas_seg());
	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_create(&ctg->cc_tree, tx, &op),
			       bo_u.u_btree.t_rc);
	if (rc != 0)
		ctg_fini(ctg);
	return M0_RC(rc);
}

static void ctg_destroy(struct m0_cas_ctg *ctg, struct m0_be_tx *tx)
{
	M0_BE_OP_SYNC(op, m0_be_btree_destroy(&ctg->cc_tree, tx, &op));
	ctg_fini(ctg);
}

static int ctg_meta_find_ctidx(struct m0_cas_ctg  *meta,
			       struct m0_cas_ctg **ctidx)
{
	uint8_t                   key_data[KV_HDR_SIZE + sizeof(struct m0_fid)];
	struct m0_buf             key;
	struct m0_be_btree_anchor anchor;
	int                       rc;

	*ctidx = NULL;

	ctg_fid_key_fill((void *)&key_data, &m0_cas_ctidx_fid);
	key = M0_BUF_INIT_PTR(&key_data);

	rc = M0_BE_OP_SYNC_RET(op,
			       m0_be_btree_lookup_inplace(&meta->cc_tree,
							  &op,
							  &key,
							  &anchor),
			       bo_u.u_btree.t_rc);
	if (rc == 0) {
		struct m0_buf buf = {};

		rc = ctg_buf(&anchor.ba_value, &buf);
		if (rc == 0) {
			if (buf.b_nob == sizeof(struct m0_cas_ctg))
				*ctidx = buf.b_addr;
			else
				rc = M0_ERR_INFO(-EPROTO, "Unexpected: %"PRIx64,
						 buf.b_nob);
		}
	}

	return M0_RC(rc);
}

static int ctg_meta_insert(struct m0_be_btree *meta,
			   struct m0_fid      *fid,
			   struct m0_cas_ctg  *ctg,
			   struct m0_be_tx    *tx)
{
	uint8_t                    key_data[KV_HDR_SIZE +
					    sizeof(struct m0_fid)];
	struct m0_buf              key;
	struct m0_be_btree_anchor  anchor = {};
	void                      *val_data;
	int                        rc;

	ctg_fid_key_fill((void *)&key_data, fid);
	key = M0_BUF_INIT_PTR(&key_data);
	anchor.ba_value.b_nob = KV_HDR_SIZE + sizeof(struct m0_cas_ctg);
	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_insert_inplace(meta, tx, &op,
					     &key, &anchor), bo_u.u_btree.t_rc);
	/*
	 * If passed catalogue is NULL then it is a stub for meta-catalogue
	 * inside itself, inserting records in it is prohibited. This stub
	 * is used to generalise listing catalogues operation.
	 * Insert real catalogue otherwise (catalogue is not NULL).
	 */
	if (rc == 0) {
		val_data = anchor.ba_value.b_addr;
		*(uint64_t *)val_data = sizeof(struct m0_cas_ctg);
		if (ctg != NULL)
			memcpy(val_data + KV_HDR_SIZE, ctg,
			       sizeof(struct m0_cas_ctg));
	}
	m0_be_btree_release(tx, &anchor);
	return M0_RC(rc);
}

static int ctg_meta_selfadd(struct m0_be_btree *meta, struct m0_be_tx *tx)
{
	return ctg_meta_insert(meta, &m0_cas_meta_fid, NULL, tx);
}

static void ctg_meta_delete(struct m0_be_btree *meta,
			    struct m0_fid      *fid,
			    struct m0_be_tx    *tx)
{
	uint8_t       key_data[KV_HDR_SIZE + sizeof(struct m0_fid)];
	struct m0_buf key;

	ctg_fid_key_fill((void *)&key_data, fid);
	key = M0_BUF_INIT_PTR(&key_data);
	M0_BE_OP_SYNC(op, m0_be_btree_delete(meta, tx, &op, &key));
}

static void ctg_meta_selfrm(struct m0_be_btree *meta, struct m0_be_tx *tx)
{
	return ctg_meta_delete(meta, &m0_cas_meta_fid, tx);
}

static void ctg_meta_insert_credit(struct m0_be_btree     *bt,
				   m0_bcount_t             nr,
				   struct m0_be_tx_credit *accum)
{
	m0_be_btree_insert_credit2(bt, nr,
				   KV_HDR_SIZE + sizeof(struct m0_fid),
				   KV_HDR_SIZE + sizeof(struct m0_cas_ctg),
				   accum);
}

static void ctg_meta_delete_credit(struct m0_be_btree     *bt,
				   m0_bcount_t             nr,
				   struct m0_be_tx_credit *accum)
{
	m0_be_btree_delete_credit(bt, nr,
				  KV_HDR_SIZE + sizeof(struct m0_fid),
				  KV_HDR_SIZE + sizeof(struct m0_cas_ctg),
				  accum);
}

static void ctg_store_init_creds_calc(struct m0_be_seg       *seg,
				      struct m0_cas_state    *state,
				      struct m0_cas_ctg      *ctidx,
				      struct m0_be_tx_credit *cred)
{
	struct m0_be_btree dummy = {};

	dummy.bb_seg = seg;

	m0_be_seg_dict_insert_credit(seg, cas_state_key, cred);
	M0_BE_ALLOC_CREDIT_PTR(state, seg, cred);
	M0_BE_ALLOC_CREDIT_PTR(ctidx, seg, cred);
	m0_be_btree_create_credit(&dummy, 2, cred);
	ctg_meta_insert_credit(&dummy, 2, cred);
	/* Error case: tree destruction and freeing. */
	ctg_meta_delete_credit(&dummy, 2, cred);
	m0_be_btree_destroy_credit(&dummy, cred);
	M0_BE_FREE_CREDIT_PTR(state, seg, cred);
	M0_BE_FREE_CREDIT_PTR(ctidx, seg, cred);
}

static int ctg_state_create(struct m0_be_seg     *seg,
			    struct m0_be_tx      *tx,
			    struct m0_cas_state **state)
{
	struct m0_cas_state *out;
        struct m0_be_btree  *bt;
        int                  rc;

	*state = NULL;
	M0_BE_ALLOC_PTR_SYNC(out, seg, tx);
	if (out == NULL)
		return M0_ERR(-ENOSPC);
	m0_format_header_pack(&out->cs_header, &(struct m0_format_tag){
		.ot_version = M0_CAS_STATE_FORMAT_VERSION,
		.ot_type    = M0_FORMAT_TYPE_CAS_STATE,
		.ot_size    = sizeof(struct m0_cas_state)
	});
	m0_format_footer_generate(&out->cs_footer, out, sizeof *out);
	rc = ctg_create(&out->cs_meta, tx);
	if (rc == 0) {
		bt = &out->cs_meta.cc_tree;
                rc = ctg_meta_selfadd(bt, tx);
		/* @todo: update footer */
                if (rc != 0)
                        ctg_destroy(&out->cs_meta, tx);
	}
	if (rc != 0)
                M0_BE_FREE_PTR_SYNC(out, seg, tx);
        else
                *state = out;
        return M0_RC(rc);
}

static void ctg_state_destroy(struct m0_cas_state *state,
			      struct m0_be_seg    *seg,
			      struct m0_be_tx     *tx)
{
	struct m0_cas_ctg *meta = &state->cs_meta;

        ctg_meta_selfrm(&meta->cc_tree, tx);
        ctg_destroy(meta, tx);
        M0_BE_FREE_PTR_SYNC(state, seg, tx);
}

static int ctg_ctidx_create(struct m0_be_seg  *seg,
			    struct m0_be_tx   *tx,
			    struct m0_cas_ctg **ctidx)
{
	struct m0_cas_ctg *out;
	int               rc;

	*ctidx = NULL;

	M0_BE_ALLOC_PTR_SYNC(out, seg, tx);
	if (out == NULL)
		return M0_ERR(-ENOSPC);

	rc = ctg_create(out, tx);
	if (rc != 0)
		M0_BE_FREE_PTR_SYNC(out, seg, tx);
	else
		*ctidx = out;

	return M0_RC(rc);
}

static void ctg_ctidx_destroy(struct m0_cas_ctg *ctidx,
			      struct m0_be_seg *seg,
			      struct m0_be_tx  *tx)
{
	ctg_destroy(ctidx, tx);
	M0_BE_FREE_PTR_SYNC(ctidx, seg, tx);
}

/**
 * Initialisation function when catalogue store state was found on a disk.
 */
static int ctg_store__init(struct m0_be_seg *seg, struct m0_cas_state *state)
{
	int rc;

	M0_ENTRY();

	ctg_store.cs_state = state;
	ctg_init(&state->cs_meta, seg);

	/* Searching for catalogue-index catalogue. */
	rc = ctg_meta_find_ctidx(&state->cs_meta, &ctg_store.cs_ctidx);
	if (rc == 0)
		ctg_init(ctg_store.cs_ctidx, seg);
	else
		ctg_store.cs_ctidx = NULL;
	return M0_RC(rc);
}

/**
 * Initialisation function when catalogue store state was not found on a disk.
 *
 * It creates all necessary data in BE segment and initialises catalogue store.
 */
static int ctg_store_create(struct m0_be_seg *seg)
{
	struct m0_cas_state    *state = NULL;
	struct m0_cas_ctg      *ctidx = NULL;
	struct m0_be_tx         tx     = {};
	struct m0_be_tx_credit  cred   = M0_BE_TX_CREDIT(0, 0);
	struct m0_sm_group     *grp   = m0_locality0_get()->lo_grp;
	int                     rc;

	M0_ENTRY();
	m0_sm_group_lock(grp);
	m0_be_tx_init(&tx, 0, m0_get()->i_be_dom, grp, NULL, NULL, NULL, NULL);

	ctg_store_init_creds_calc(seg, state, ctidx, &cred);

	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_exclusive_open_sync(&tx);
	if (rc != 0) {
		m0_be_tx_fini(&tx);
		return M0_ERR(rc);
	}

	rc = ctg_state_create(seg, &tx, &state);
	if (rc != 0)
		goto end;

	/* Create catalog-index catalogue. */
	rc = ctg_ctidx_create(seg, &tx, &ctidx);
	if (rc != 0)
		goto meta_destroy;
	/*
	 * Insert catalogue-index catalogue into meta-catalogue.
	 */
	rc = ctg_meta_insert(&state->cs_meta.cc_tree, &m0_cas_ctidx_fid, ctidx,
			     &tx);
	if (rc != 0)
		goto ctidx_destroy;

	rc = m0_be_seg_dict_insert(seg, &tx, cas_state_key, state);
	if (rc != 0)
		goto state_delete;
	M0_BE_TX_CAPTURE_PTR(seg, &tx, ctidx);
	M0_BE_TX_CAPTURE_PTR(seg, &tx, state);
	ctg_store.cs_state = state;
	ctg_store.cs_ctidx = ctidx;
	goto end;
state_delete:
	ctg_meta_delete(&state->cs_meta.cc_tree,
			&m0_cas_ctidx_fid,
			&tx);
ctidx_destroy:
	ctg_ctidx_destroy(ctidx, seg, &tx);
meta_destroy:
	ctg_state_destroy(state, seg, &tx);
end:
	m0_be_tx_close_sync(&tx);
	m0_be_tx_fini(&tx);
	m0_sm_group_unlock(grp);
	return M0_RC(rc);
}

M0_INTERNAL int m0_ctg_store_init()
{
	struct m0_be_seg    *seg   = cas_seg();
	struct m0_cas_state *state = NULL;
	int                  result;

	M0_ENTRY();
	m0_mutex_lock(&cs_init_guard);
	if (ctg_store.cs_initialised) {
		m0_ref_get(&ctg_store.cs_ref);
		result = 0;
		goto end;
	}

	/**
	 * @todo Use 0type.
	 */
	result = m0_be_seg_dict_lookup(seg, cas_state_key, (void **)&state);
	if (result == 0) {
		/**
		 * @todo Add checking, use header and footer.
		 */
		M0_ASSERT(state != NULL);
		result = ctg_store__init(seg, state);
	} else if (result == -ENOENT) {
		M0_LOG(M0_DEBUG, "Ctg store state wasn't found on a disk.");
		result = ctg_store_create(seg);
	}

	if (result == 0) {
		m0_mutex_init(&ctg_store.cs_state_mutex);
		m0_ref_init(&ctg_store.cs_ref, 1, ctg_store_release);
		ctg_store.cs_initialised = true;
	}
end:
	m0_mutex_unlock(&cs_init_guard);
	return M0_RC(result);
}

static void ctg_store_release(struct m0_ref *ref)
{
	struct m0_ctg_store *ctg_store = M0_AMB(ctg_store, ref, cs_ref);

	m0_mutex_fini(&ctg_store->cs_state_mutex);
	ctg_store->cs_state = NULL;
	ctg_store->cs_ctidx = NULL;
	ctg_store->cs_initialised = false;
}

M0_INTERNAL void m0_ctg_store_fini()
{
	m0_ref_put(&ctg_store.cs_ref);
}

static void ctg_state_counter_add(uint64_t *counter, uint64_t val)
{
	if (*counter != ~0ULL) {
		if (*counter + val < *counter) {
			M0_LOG(M0_WARN,
			       "Ctg store counter overflow: counter %"
			       PRIx64" addendum %"PRIx64, *counter, val);
			*counter = ~0ULL;
		} else {
			*counter += val;
		}
	}
}

static void ctg_state_counter_sub(uint64_t *counter, uint64_t val)
{
	if (*counter != ~0ULL) {
		M0_ASSERT(*counter - val <= *counter);
		*counter -= val;
	}
}

static uint64_t ctg_state_update(struct m0_be_tx *tx, uint64_t size,
				 bool is_inc)
{
	uint64_t         *recs_nr  = &ctg_store.cs_state->cs_rec_nr;
	uint64_t         *rec_size = &ctg_store.cs_state->cs_rec_size;
	struct m0_be_seg *seg      = cas_seg();

	if (M0_FI_ENABLED("test_overflow"))
		size = ~0ULL - 1;

	m0_mutex_lock(&ctg_store.cs_state_mutex);
	/*
	 * recs_nr and rec_size counters update is done having possible overflow
	 * in mind. A counter value is sticked to ~0ULL in the case of overflow
	 * and further counter updates are ignored.
	 */
	if (is_inc) {
		/*
		 * Overflow is unlikely. If it happens, then calculation of DIX
		 * repair/re-balance progress may be incorrect.
		 */
		ctg_state_counter_add(recs_nr, 1);
		/*
		 * Overflow is possible, because total size is not decremented
		 * on record deletion.
		 */
		ctg_state_counter_add(rec_size, size);
	} else {
		M0_ASSERT(*recs_nr != 0);
		ctg_state_counter_sub(recs_nr, 1);
		ctg_state_counter_sub(rec_size, size);
	}
	m0_mutex_unlock(&ctg_store.cs_state_mutex);

	M0_BE_TX_CAPTURE_PTR(seg, tx, ctg_store.cs_state);
	return *recs_nr;
}

static void ctg_state_inc_update(struct m0_be_tx *tx, uint64_t size)
{
	(void)ctg_state_update(tx, size, true);
}

static void ctg_state_dec_update(struct m0_be_tx *tx, uint64_t size)
{
	(void)ctg_state_update(tx, size, false);
}

static bool ctg_op_cb(struct m0_clink *clink)
{
	struct m0_ctg_op *ctg_op  = container_of(clink,
						struct m0_ctg_op,
						co_clink);
	struct m0_be_op  *op      = ctg_beop(ctg_op);
	int               opc     = ctg_op->co_opcode;
	int               ct      = ctg_op->co_ct;
	struct m0_be_tx  *tx      = &ctg_op->co_fom->fo_tx.tx_betx;
	void             *arena   = ctg_op->co_anchor.ba_value.b_addr;
	struct m0_buf     cur_key = {};
	struct m0_buf     cur_val = {};
	int               rc;

	if (op->bo_sm.sm_state != M0_BOS_DONE)
		return true;

	rc = ctg_berc(ctg_op);
	if (rc == 0) {
		switch (CTG_OP_COMBINE(opc, ct)) {
		case CTG_OP_COMBINE(CO_GET, CT_BTREE):
		case CTG_OP_COMBINE(CO_GET, CT_META):
			rc = ctg_buf(&ctg_op->co_anchor.ba_value,
				     &ctg_op->co_out_val);
			if (rc == 0 && ct == CT_META &&
			    ctg_op->co_out_val.b_nob !=
			    sizeof(struct m0_cas_ctg))
				rc = M0_ERR(-EFAULT);
			break;
		case CTG_OP_COMBINE(CO_DEL, CT_BTREE):
			ctg_state_dec_update(tx, 0);
		case CTG_OP_COMBINE(CO_DEL, CT_META):
			/* Nothing to do. */
			break;
		case CTG_OP_COMBINE(CO_CUR, CT_META):
		case CTG_OP_COMBINE(CO_CUR, CT_BTREE):
			m0_be_btree_cursor_kv_get(&ctg_op->co_cur,
						  &cur_key,
						  &cur_val);
			rc = ctg_buf(&cur_key,
				     &ctg_op->co_out_key);
			if (rc == 0)
				rc = ctg_buf(&cur_val,
					     &ctg_op->co_out_val);
			break;
		case CTG_OP_COMBINE(CO_PUT, CT_BTREE):
			ctg_memcpy(arena, ctg_op->co_val.b_addr,
				   ctg_op->co_val.b_nob);
			ctg_state_inc_update(tx,
					ctg_op->co_key.b_nob - KV_HDR_SIZE +
					ctg_op->co_val.b_nob);
			break;
		case CTG_OP_COMBINE(CO_PUT, CT_META):
			*(uint64_t *)arena = sizeof(struct m0_cas_ctg);
			rc = ctg_create(arena + KV_HDR_SIZE,
					&ctg_op->co_fom->fo_tx.tx_betx);
			break;
		}
	}

	if (opc == CO_CUR) {
		/* Always finalise BE operation of the cursor. */
		m0_be_op_fini(&ctg_op->co_cur.bc_op);
		M0_SET0(&ctg_op->co_cur.bc_op);
	}

	if (opc == CO_PUT &&
	    ctg_op->co_flags & COF_CREATE &&
	    rc == -EEXIST)
		rc = 0;

	ctg_op->co_rc = rc;
	m0_chan_broadcast_lock(&ctg_op->co_channel);
	/*
	 * This callback may be called directly from ctg_op_tick_ret()
	 * without adding of clink to the channel.
	 */
	if (m0_clink_is_armed(clink))
		m0_clink_del(clink);

	return true;
}

static int ctg_op_tick_ret(struct m0_ctg_op *ctg_op,
			   int               next_state)
{
	struct m0_chan  *chan  = &ctg_op->co_channel;
	struct m0_fom   *fom   = ctg_op->co_fom;
	struct m0_clink *clink = &ctg_op->co_clink;
	struct m0_be_op *op    = ctg_beop(ctg_op);
	int              ret   = M0_FSO_AGAIN;
	bool             op_is_active;

	m0_be_op_lock(op);
	M0_PRE(M0_IN(op->bo_sm.sm_state, (M0_BOS_ACTIVE, M0_BOS_DONE)));

	op_is_active = op->bo_sm.sm_state == M0_BOS_ACTIVE;
	if (op_is_active) {
		ret = M0_FSO_WAIT;
		m0_clink_add(&op->bo_sm.sm_chan, clink);

		m0_chan_lock(chan);
		m0_fom_wait_on(fom, chan, &fom->fo_cb);
		m0_chan_unlock(chan);
	}

	m0_be_op_unlock(op);

	if (!op_is_active)
		clink->cl_cb(clink);

	m0_fom_phase_set(fom, next_state);

	return ret;
}

static int ctg_op_exec(struct m0_ctg_op *ctg_op, int next_phase)
{
	struct m0_buf             *key    = &ctg_op->co_key;
	struct m0_be_btree        *btree  = &ctg_op->co_ctg->cc_tree;
	struct m0_be_btree_anchor *anchor = &ctg_op->co_anchor;
	struct m0_be_btree_cursor *cur    = &ctg_op->co_cur;
	struct m0_be_tx           *tx     = &ctg_op->co_fom->fo_tx.tx_betx;
	struct m0_be_op           *beop   = ctg_beop(ctg_op);
	int                        opc    = ctg_op->co_opcode;
	int                        ct     = ctg_op->co_ct;

	switch (CTG_OP_COMBINE(opc, ct)) {
	case CTG_OP_COMBINE(CO_PUT, CT_BTREE):
		anchor->ba_value.b_nob = KV_HDR_SIZE + ctg_op->co_val.b_nob;
		m0_be_btree_save_inplace(btree, tx, beop, key, anchor,
				 !!(ctg_op->co_flags & COF_OVERWRITE));
		break;
	case CTG_OP_COMBINE(CO_PUT, CT_META):
		M0_ASSERT(!(ctg_op->co_flags & COF_OVERWRITE));
		anchor->ba_value.b_nob = KV_HDR_SIZE +
					 sizeof(struct m0_cas_ctg);
		m0_be_btree_insert_inplace(btree, tx, beop, key, anchor);
		break;
	case CTG_OP_COMBINE(CO_GET, CT_BTREE):
	case CTG_OP_COMBINE(CO_GET, CT_META):
		m0_be_btree_lookup_inplace(btree, beop, key, anchor);
		break;
	case CTG_OP_COMBINE(CO_DEL, CT_BTREE):
	case CTG_OP_COMBINE(CO_DEL, CT_META):
		m0_be_btree_delete(btree, tx, beop, key);
		break;
	case CTG_OP_COMBINE(CO_CUR, CT_BTREE):
	case CTG_OP_COMBINE(CO_CUR, CT_META):
		M0_ASSERT(ctg_op->co_cur_phase == CPH_GET ||
			  ctg_op->co_cur_phase == CPH_NEXT);
		if (ctg_op->co_cur_phase == CPH_GET)
			m0_be_btree_cursor_get(cur, key,
				       !!(ctg_op->co_flags & COF_SLANT));
		else
			m0_be_btree_cursor_next(cur);
		break;
	}

	return ctg_op_tick_ret(ctg_op, next_phase);
}

static int ctg_meta_exec(struct m0_ctg_op    *ctg_op,
			 const struct m0_fid *fid,
			 int                  next_phase)
{
	int ret;

	ctg_op->co_ctg = &ctg_store.cs_state->cs_meta;
	ctg_op->co_ct = CT_META;

	if (ctg_op->co_opcode != CO_CUR ||
	    ctg_op->co_cur_phase != CPH_NEXT)
		ctg_op->co_rc = ctg_buf_get(&ctg_op->co_key,
					    &M0_BUF_INIT_CONST(
						    sizeof(struct m0_fid),
						    fid));
	if (ctg_op->co_rc != 0) {
		ret = M0_FSO_AGAIN;
		m0_fom_phase_set(ctg_op->co_fom, next_phase);
	} else
		ret = ctg_op_exec(ctg_op, next_phase);

	return ret;
}


M0_INTERNAL int m0_ctg_meta_insert(struct m0_ctg_op    *ctg_op,
				   const struct m0_fid *fid,
				   int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_INIT);

	ctg_op->co_opcode = CO_PUT;

	return ctg_meta_exec(ctg_op, fid, next_phase);
}

M0_INTERNAL int m0_ctg_meta_lookup(struct m0_ctg_op    *ctg_op,
				   const struct m0_fid *fid,
				   int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_INIT);

	ctg_op->co_opcode = CO_GET;

	return ctg_meta_exec(ctg_op, fid, next_phase);
}

M0_INTERNAL
struct m0_cas_ctg *m0_ctg_meta_lookup_result(struct m0_ctg_op *ctg_op)
{
	struct m0_cas_ctg *ctg = NULL;

	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_DONE);
	M0_PRE(ctg_op->co_opcode == CO_GET);
	M0_PRE(ctg_op->co_ct == CT_META);

	if (ctg_op->co_rc == 0)
		ctg = (struct m0_cas_ctg *)ctg_op->co_out_val.b_addr;

	return ctg;
}

M0_INTERNAL int m0_ctg_meta_delete(struct m0_ctg_op    *ctg_op,
				   const struct m0_fid *fid,
				   int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_INIT);

	ctg_op->co_opcode = CO_DEL;

	return ctg_meta_exec(ctg_op, fid, next_phase);
}

static int ctg_exec(struct m0_ctg_op    *ctg_op,
		    struct m0_cas_ctg   *ctg,
		    const struct m0_buf *key,
		    int                  next_phase)
{
	int ret = M0_FSO_AGAIN;

	ctg_op->co_ctg = ctg;
	ctg_op->co_ct = CT_BTREE;

	if (ctg_op->co_opcode != CO_CUR ||
	    ctg_op->co_cur_phase != CPH_NEXT)
		ctg_op->co_rc = ctg_buf_get(&ctg_op->co_key, key);

	if (ctg_op->co_rc != 0)
		m0_fom_phase_set(ctg_op->co_fom, next_phase);
	else
		ret = ctg_op_exec(ctg_op, next_phase);

	return ret;
}

M0_INTERNAL int m0_ctg_insert(struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      const struct m0_buf *val,
			      int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg != NULL);
	M0_PRE(key != NULL);
	M0_PRE(val != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_INIT);

	ctg_op->co_opcode = CO_PUT;
	ctg_op->co_val = *val;
	return ctg_exec(ctg_op, ctg, key, next_phase);
}

M0_INTERNAL int m0_ctg_delete(struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg != NULL);
	M0_PRE(key != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_INIT);

	ctg_op->co_opcode = CO_DEL;

	return ctg_exec(ctg_op, ctg, key, next_phase);
}

M0_INTERNAL int m0_ctg_lookup(struct m0_ctg_op    *ctg_op,
			      struct m0_cas_ctg   *ctg,
			      const struct m0_buf *key,
			      int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg != NULL);
	M0_PRE(key != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_INIT);

	ctg_op->co_opcode = CO_GET;

	return ctg_exec(ctg_op, ctg, key, next_phase);
}

M0_INTERNAL void m0_ctg_lookup_result(struct m0_ctg_op *ctg_op,
				      struct m0_buf    *buf)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg_op->co_beop.bo_sm.sm_state == M0_BOS_DONE);
	M0_PRE(ctg_op->co_opcode == CO_GET);
	M0_PRE(ctg_op->co_ct == CT_BTREE);
	M0_PRE(ctg_op->co_rc == 0);

	*buf = ctg_op->co_out_val;
}

M0_INTERNAL bool m0_ctg_cursor_is_initialised(struct m0_ctg_op *ctg_op)
{
	M0_PRE(ctg_op != NULL);

	return ctg_op->co_cur_initialised;
}

M0_INTERNAL void m0_ctg_cursor_init(struct m0_ctg_op  *ctg_op,
				    struct m0_cas_ctg *ctg)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg != NULL);

	if (ctg_op->co_cur_initialised)
		return;

	ctg_op->co_ctg = ctg;
	ctg_op->co_opcode = CO_CUR;
	ctg_op->co_cur_phase = CPH_INIT;
	M0_SET0(&ctg_op->co_cur.bc_op);

	m0_be_btree_cursor_init(&ctg_op->co_cur, &ctg_op->co_ctg->cc_tree);
	ctg_op->co_cur_initialised = true;
}

M0_INTERNAL int m0_ctg_cursor_get(struct m0_ctg_op    *ctg_op,
				  const struct m0_buf *key,
				  int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(key != NULL);

	ctg_op->co_cur_phase = CPH_GET;

	/*
	 * Key may be set if cursor get is called twice between ctg operation
	 * init()/fini(), free it to avoid memory leak.
	 */
	m0_buf_free(&ctg_op->co_key);
	/* Cursor has its own beop. */
	m0_be_op_init(&ctg_op->co_cur.bc_op);

	return ctg_exec(ctg_op, ctg_op->co_ctg, key, next_phase);
}

M0_INTERNAL int m0_ctg_cursor_next(struct m0_ctg_op *ctg_op,
				   int               next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg_op->co_cur_phase != CPH_INIT);

	ctg_op->co_cur_phase = CPH_NEXT;

	/* BE operation must be initialised each time when next called. */
	m0_be_op_init(&ctg_op->co_cur.bc_op);

	return ctg_exec(ctg_op, ctg_op->co_ctg, NULL, next_phase);
}

M0_INTERNAL void m0_ctg_cursor_kv_get(struct m0_ctg_op *ctg_op,
				      struct m0_buf    *key,
				      struct m0_buf    *val)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(key != NULL);
	M0_PRE(val != NULL);
	M0_PRE(ctg_op->co_opcode == CO_CUR);
	M0_PRE(ctg_op->co_rc == 0);

	*key = ctg_op->co_out_key;
	*val = ctg_op->co_out_val;
}

M0_INTERNAL void m0_ctg_meta_cursor_init(struct m0_ctg_op *ctg_op)
{
	M0_PRE(ctg_op != NULL);

	m0_ctg_cursor_init(ctg_op, &ctg_store.cs_state->cs_meta);
}

M0_INTERNAL int m0_ctg_meta_cursor_get(struct m0_ctg_op    *ctg_op,
				       const struct m0_fid *fid,
				       int                  next_phase)
{
	M0_PRE(ctg_op != NULL);
	M0_PRE(fid != NULL);

	ctg_op->co_cur_phase = CPH_GET;

	/* Cursor has its own beop. */
	m0_be_op_init(&ctg_op->co_cur.bc_op);

	return ctg_meta_exec(ctg_op, fid, next_phase);
}

M0_INTERNAL void m0_ctg_cursor_put(struct m0_ctg_op *ctg_op)
{
	m0_be_btree_cursor_put(&ctg_op->co_cur);
}

M0_INTERNAL void m0_ctg_cursor_fini(struct m0_ctg_op *ctg_op)
{
	M0_PRE(ctg_op != NULL);

	m0_be_btree_cursor_fini(&ctg_op->co_cur);
	M0_SET0(&ctg_op->co_cur);
	ctg_op->co_cur_initialised = false;
	ctg_op->co_cur_phase = CPH_NONE;
}

M0_INTERNAL void m0_ctg_op_init(struct m0_ctg_op *ctg_op,
				struct m0_fom    *fom,
				uint32_t          flags)
{
	M0_ENTRY("ctg_op=%p", ctg_op);
	M0_PRE(ctg_op != NULL);
	M0_PRE(fom != NULL);

	M0_SET0(ctg_op);

	m0_be_op_init(&ctg_op->co_beop);
	m0_mutex_init(&ctg_op->co_channel_lock);
	m0_chan_init(&ctg_op->co_channel, &ctg_op->co_channel_lock);
	m0_clink_init(&ctg_op->co_clink, ctg_op_cb);
	ctg_op->co_fom = fom;
	ctg_op->co_flags = flags;
	ctg_op->co_cur_phase = CPH_NONE;
}

M0_INTERNAL int m0_ctg_op_rc(struct m0_ctg_op *ctg_op)
{
	M0_PRE(ctg_op != NULL);

	if (M0_FI_ENABLED("be-failure"))
		return M0_ERR(-ENOMEM);
	return ctg_op->co_rc;
}

M0_INTERNAL void m0_ctg_op_fini(struct m0_ctg_op *ctg_op)
{
	M0_ENTRY("ctg_op=%p", ctg_op);
	M0_PRE(ctg_op != NULL);
	M0_PRE(ctg_op->co_cur_initialised == false);

	m0_be_btree_release(&ctg_op->co_fom->fo_tx.tx_betx,
			    &ctg_op->co_anchor);
	m0_buf_free(&ctg_op->co_key);
	m0_chan_fini_lock(&ctg_op->co_channel);
	m0_mutex_fini(&ctg_op->co_channel_lock);
	m0_clink_fini(&ctg_op->co_clink);
	m0_be_op_fini(&ctg_op->co_beop);
	M0_SET0(ctg_op);
	M0_LEAVE();
}

M0_INTERNAL void m0_ctg_meta_insert_credit(struct m0_be_tx_credit *accum)
{
	m0_bcount_t         knob;
	m0_bcount_t         vnob;
	struct m0_be_btree *btree = &ctg_store.cs_state->cs_meta.cc_tree;

	m0_be_btree_create_credit(btree, 1, accum);

	knob = KV_HDR_SIZE + sizeof(struct m0_fid);
	vnob = KV_HDR_SIZE + sizeof(struct m0_cas_ctg);
	m0_be_btree_insert_credit2(btree, 1, knob, vnob, accum);
}

M0_INTERNAL void m0_ctg_meta_delete_credit(struct m0_be_tx_credit *accum)
{
	m0_bcount_t         knob;
	m0_bcount_t         vnob;
	struct m0_be_btree *btree = &ctg_store.cs_state->cs_meta.cc_tree;

	/*
	 * @todo It is not always possible to destroy a large btree in
	 * one transaction. See HLD for the solution.
	 */
	m0_be_btree_destroy_credit(btree, accum);

	knob = KV_HDR_SIZE + sizeof(struct m0_fid);
	vnob = KV_HDR_SIZE + sizeof(struct m0_cas_ctg);
	m0_be_btree_delete_credit(btree, 1, knob, vnob, accum);
}

M0_INTERNAL void m0_ctg_insert_credit(struct m0_cas_ctg      *ctg,
				      m0_bcount_t             knob,
				      m0_bcount_t             vnob,
				      struct m0_be_tx_credit *accum)
{
	m0_be_btree_insert_credit2(&ctg->cc_tree, 1, knob, vnob, accum);
}

M0_INTERNAL void m0_ctg_delete_credit(struct m0_cas_ctg      *ctg,
				      m0_bcount_t             knob,
				      m0_bcount_t             vnob,
				      struct m0_be_tx_credit *accum)
{
	m0_be_btree_delete_credit(&ctg->cc_tree, 1, knob, vnob, accum);
}

static void ctg_ctidx_op_credits(struct m0_cas_id       *cid,
				 bool                    insert,
				 struct m0_be_tx_credit *accum)
{
	const struct m0_dix_imask *imask;
	struct m0_be_seg          *seg   = cas_seg();
	struct m0_be_btree        *btree = &ctg_store.cs_ctidx->cc_tree;
	m0_bcount_t                knob;
	m0_bcount_t                vnob;

	knob = KV_HDR_SIZE + sizeof(struct m0_fid);
	vnob = KV_HDR_SIZE + sizeof(struct m0_dix_layout);
	if (insert)
		m0_be_btree_insert_credit2(btree, 1, knob, vnob, accum);
	else
		m0_be_btree_delete_credit(btree, 1, knob, vnob, accum);

	imask = &cid->ci_layout.u.dl_desc.ld_imask;
	if (!m0_dix_imask_is_empty(imask)) {
		if (insert)
			M0_BE_ALLOC_CREDIT_ARR(imask->im_range,
					       imask->im_nr,
					       seg,
					       accum);
		else
			M0_BE_FREE_CREDIT_ARR(imask->im_range,
					      imask->im_nr,
					      seg,
					      accum);
	}
}

M0_INTERNAL void m0_ctg_ctidx_insert_credits(struct m0_cas_id       *cid,
					     struct m0_be_tx_credit *accum)
{
	M0_PRE(cid != NULL);
	M0_PRE(accum != NULL);

	ctg_ctidx_op_credits(cid, true, accum);
}

M0_INTERNAL void m0_ctg_ctidx_delete_credits(struct m0_cas_id       *cid,
					     struct m0_be_tx_credit *accum)
{
	M0_PRE(cid != NULL);
	M0_PRE(accum != NULL);

	ctg_ctidx_op_credits(cid, false, accum);
}

M0_INTERNAL int m0_ctg_ctidx_lookup(const struct m0_fid  *fid,
				    struct m0_dix_layout **layout)
{
	uint8_t                   key_data[KV_HDR_SIZE + sizeof(struct m0_fid)];
	struct m0_buf             key;
	struct m0_be_btree_anchor anchor;
	struct m0_cas_ctg        *ctidx = m0_ctg_ctidx();
	int                       rc;

	M0_PRE(ctidx != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(layout != NULL);

	*layout = NULL;

	ctg_fid_key_fill(&key_data, fid);
	key = M0_BUF_INIT_PTR(&key_data);

	/** @todo Make it asynchronous. */
	rc = M0_BE_OP_SYNC_RET(op,
			       m0_be_btree_lookup_inplace(&ctidx->cc_tree,
							  &op,
							  &key,
							  &anchor),
			       bo_u.u_btree.t_rc);
	if (rc == 0) {
		struct m0_buf buf = {};

		rc = ctg_buf(&anchor.ba_value, &buf);
		if (rc == 0) {
			if (buf.b_nob == sizeof(struct m0_dix_layout))
				*layout = (struct m0_dix_layout *)buf.b_addr;
			else
				rc = M0_ERR_INFO(-EPROTO, "Unexpected: %"PRIx64,
						 buf.b_nob);
		}
	}
	m0_be_btree_release(NULL, &anchor);

	return M0_RC(rc);
}

M0_INTERNAL int m0_ctg_ctidx_insert(const struct m0_cas_id *cid,
				    struct m0_be_tx        *tx)
{
	uint8_t                    key_data[KV_HDR_SIZE +
					    sizeof(struct m0_fid)];
	struct m0_cas_ctg         *ctidx    = m0_ctg_ctidx();
	struct m0_be_btree_anchor  anchor   = {};
	struct m0_dix_layout      *layout;
	struct m0_ext             *im_range;
	const struct m0_dix_imask *imask;
	struct m0_buf              key;
	m0_bcount_t                size;
	int                        rc;

	/* The key is a component catalogue FID. */
	ctg_fid_key_fill((void *)&key_data, &cid->ci_fid);
	key = M0_BUF_INIT_PTR(&key_data);
	anchor.ba_value.b_nob = KV_HDR_SIZE + sizeof(struct m0_dix_layout);
	/** @todo Make it asynchronous. */
	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_insert_inplace(&ctidx->cc_tree,
							      tx, &op, &key,
							      &anchor),
			       bo_u.u_btree.t_rc);
	if (rc == 0) {
		ctg_memcpy(anchor.ba_value.b_addr, &cid->ci_layout,
			   sizeof(cid->ci_layout));
		imask = &cid->ci_layout.u.dl_desc.ld_imask;
		if (!m0_dix_imask_is_empty(imask)) {
			/*
			 * Alloc memory in BE segment for imask ranges
			 * and copy them.
			 */
			/** @todo Make it asynchronous. */
			M0_BE_ALLOC_ARR_SYNC(im_range, imask->im_nr,
					     cas_seg(), tx);
			size = imask->im_nr * sizeof(struct m0_ext);
			memcpy(im_range, imask->im_range, size);
			m0_be_tx_capture(tx, &M0_BE_REG(cas_seg(),
							size, im_range));
			/* Assign newly allocated imask ranges. */
			layout = (struct m0_dix_layout *)
				(anchor.ba_value.b_addr + KV_HDR_SIZE);
			layout->u.dl_desc.ld_imask.im_range = im_range;
		}
	}
	m0_be_btree_release(tx, &anchor);
	return M0_RC(rc);
}

M0_INTERNAL int m0_ctg_ctidx_delete(const struct m0_cas_id *cid,
				    struct m0_be_tx        *tx)
{
	struct m0_dix_layout *layout;
	struct m0_dix_imask  *imask;
	uint8_t               key_data[KV_HDR_SIZE + sizeof(struct m0_fid)];
	struct m0_cas_ctg    *ctidx  = m0_ctg_ctidx();
	struct m0_buf         key;
	int                   rc;

	/* Firstly we should free buffer allocated for imask ranges array. */
	rc = m0_ctg_ctidx_lookup(&cid->ci_fid, &layout);
	if (rc != 0)
		return rc;
	imask = &layout->u.dl_desc.ld_imask;
	/** @todo Make it asynchronous. */
	M0_BE_FREE_PTR_SYNC(imask->im_range, cas_seg(), tx);
	imask->im_range = NULL;
	imask->im_nr = 0;

	/* The key is a component catalogue FID. */
	ctg_fid_key_fill((void *)&key_data, &cid->ci_fid);
	key = M0_BUF_INIT_PTR(&key_data);
	/** @todo Make it asynchronous. */
	M0_BE_OP_SYNC(op, m0_be_btree_delete(&ctidx->cc_tree, tx, &op, &key));

	return rc;
}

M0_INTERNAL struct m0_cas_ctg *m0_ctg_meta()
{
	return &ctg_store.cs_state->cs_meta;
}

M0_INTERNAL struct m0_cas_ctg *m0_ctg_ctidx()
{
	return ctg_store.cs_ctidx;
}

M0_INTERNAL uint64_t m0_ctg_rec_nr(void)
{
	M0_ASSERT(ctg_store.cs_state != NULL);
	return ctg_store.cs_state->cs_rec_nr;
}

M0_INTERNAL uint64_t m0_ctg_rec_size(void)
{
	M0_ASSERT(ctg_store.cs_state != NULL);
        return ctg_store.cs_state->cs_rec_size;
}

static const struct m0_be_btree_kv_ops cas_btree_ops = {
	.ko_ksize   = &ctg_ksize,
	.ko_vsize   = &ctg_vsize,
	.ko_compare = &ctg_cmp
};

#undef M0_TRACE_SUBSYSTEM

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
