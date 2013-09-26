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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09-Sep-2010
 */

#include "lib/arith.h"         /* M0_3WAY */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"          /* M0_SET0 */
#include "lib/vec.h"
#include "mero/magic.h"
#include "rpc/rpc_opcodes.h"
#include "fol/fol.h"
#include "fol/fol_private.h"
#include "fol/fol_xc.h"       /* m0_xc_fol_init */
#include "fop/fop.h"          /* m0_fop_fol_rec_part_type */

/**
   @addtogroup fol

   <b>Implementation notes.</b>

   At the moment, fol is implemented as a m0_table. An alternative implemenation
   would re-use db5 transaction log to store fol records. The dis-advantage of
   the former approach is that records are duplicated between fol table and
   transaction log. The advantage is its simplicity (specifically, using db5 log
   for fol would require manual control over db5 log pruning policies).

   The fol table is (naturally) indexed by lsn. The record itself does not store
   the lsn (take a look at the comment before rec_init()) and has the following
   variable-sized format:

   @li struct m0_fol_rec_header
   @li followed by rh_obj_nr m0_fol_obj_ref-s
   @li followed by rh_sibling_nr m0_update_id-s
   @li followed by rh_parts_nr m0_fol_rec_part-s
   @li followed by rh_data_len bytes
   @li followed by the list of fol record parts.

   When a record is fetched from the fol, it is decoded by fol_record_decode().
   When a record is placed into the fol, its representation is prepared by
   fol_record_encode().

  @{
 */

M0_TL_DESCR_DEFINE(m0_rec_part, "fol record part", M0_INTERNAL,
		   struct m0_fol_rec_part, rp_link, rp_magic,
		   M0_FOL_REC_PART_LINK_MAGIC, M0_FOL_REC_PART_HEAD_MAGIC);
M0_TL_DEFINE(m0_rec_part, M0_INTERNAL, struct m0_fol_rec_part);

#define REC_SIBLING_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_fol_update_ref_xc, ptr)
#define REC_OBJ_REF_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_fol_obj_ref_xc, ptr)

#define REC_PART_HEADER_XCODE_OBJ(ptr) \
	M0_XCODE_OBJ(m0_fol_rec_part_header_xc, ptr)

#define REC_PART_XCODE_OBJ(r) (struct m0_xcode_obj) { \
	.xo_type = part->rp_ops != NULL ?             \
		   part->rp_ops->rpo_type->rpt_xt :   \
		   m0_fop_fol_rec_part_type.rpt_xt,   \
	.xo_ptr  = r->rp_data                         \
}

enum {
	FOL_KEY_SIZE = sizeof(m0_lsn_t),
#if !XXX_USE_DB5
	/*
	 * The maximum possible length of fol record.
	 *
	 * We need to obtain sufficient BE credit before adding new record
	 * to the fol. Fol records are of variable length and the actual
	 * length is hard, if possible, to calculate at the moment of
	 * m0_fol_credit() call. We use the empirical value of maximum
	 * possible record length instead.
	 *
	 * XXX REVISEME: If the value is not sufficient, increase it.
	 * Alternative (proper?) solution is to calculate the size of fol
	 * record as a function of rpc opcode.
	 */
	FOL_REC_MAXSIZE = 8 * 1024
#endif
};

/* ------------------------------------------------------------------
 * LSN
 * ------------------------------------------------------------------ */

static m0_bcount_t fol_rec_header_pack_size(struct m0_fol_rec_header *h);

M0_INTERNAL bool m0_lsn_is_valid(m0_lsn_t lsn)
{
	return lsn > M0_LSN_RESERVED_NR;
}

M0_INTERNAL int m0_lsn_cmp(m0_lsn_t lsn0, m0_lsn_t lsn1)
{
	M0_PRE(m0_lsn_is_valid(lsn0));
	M0_PRE(m0_lsn_is_valid(lsn1));

	return M0_3WAY(lsn0, lsn1);
}

M0_INTERNAL m0_lsn_t lsn_inc(m0_lsn_t lsn)
{
	M0_CNT_INC(lsn);

	M0_POST(m0_lsn_is_valid(lsn));
	return lsn;
}

M0_INTERNAL m0_lsn_t m0_fol_lsn_allocate(struct m0_fol *fol)
{
	m0_lsn_t lsn;
	/*
	 * Obtain next fol lsn under the lock. Alternatively, m0_fol::f_lsn
	 * could be made into a m0_atomic64 instance.
	 */
	m0_mutex_lock(&fol->f_lock);
	lsn = fol->f_lsn;
	fol->f_lsn = lsn_inc(fol->f_lsn);
	m0_mutex_unlock(&fol->f_lock);
	M0_POST(m0_lsn_is_valid(lsn));
	return lsn;
}

#if XXX_USE_DB5
static int lsn_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	const m0_lsn_t *lsn0 = key0;
	const m0_lsn_t *lsn1 = key1;

	return m0_lsn_cmp(*lsn0, *lsn1);
}

static const struct m0_table_ops fol_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = FOL_KEY_SIZE
		},
		[TO_REC] = {
			.max_size = 8192,
		}
	},
	.key_cmp = lsn_cmp
};
#else
static m0_bcount_t fol_ksize(const void *_)
{
	return FOL_KEY_SIZE;
}

static m0_bcount_t fol_vsize(const void *data)
{
	struct m0_fol_rec_header hdr;
	m0_bcount_t              len = fol_rec_header_pack_size(&hdr);
	struct m0_bufvec         bvec = M0_BUFVEC_INIT_BUF((void **)&data,
							   &len);
	struct m0_bufvec_cursor  cur;
	int                      rc;

	m0_bufvec_cursor_init(&cur, &bvec);
	rc = m0_xcode_encdec(&M0_REC_HEADER_XCODE_OBJ(&hdr), &cur,
			     M0_XCODE_DECODE);
	/* XXX There is no way to return an error code from
	 * m0_be_btree_kv_ops::ko_vsize(). */
	M0_ASSERT(rc == 0 && hdr.rh_magic == M0_FOL_REC_MAGIC);

	M0_POST(hdr.rh_data_len <= FOL_REC_MAXSIZE);
	return hdr.rh_data_len;
}

static int lsn_cmp(const void *key0, const void *key1)
{
	const m0_lsn_t *lsn0 = key0;
	const m0_lsn_t *lsn1 = key1;

	return m0_lsn_cmp(*lsn0, *lsn1);
}

static const struct m0_be_btree_kv_ops fol_kv_ops = {
	.ko_ksize   = fol_ksize,
	.ko_vsize   = fol_vsize,
	.ko_compare = lsn_cmp
};
#endif

/* ------------------------------------------------------------------
 * m0_fol operations
 * ------------------------------------------------------------------ */

static int fol_setup(struct m0_fol *fol, struct m0_be_tx *tx);

#if XXX_USE_DB5
M0_INTERNAL int m0_fol_init(struct m0_fol *fol, struct m0_dbenv *env)
{
	int result;

	M0_CASSERT(M0_LSN_ANCHOR > M0_LSN_RESERVED_NR);

	m0_mutex_init(&fol->f_lock);
	result = m0_table_init(&fol->f_table, env, "fol", 0, &fol_ops);
	if (result == 0) {
		struct m0_fol_rec       r;
		struct m0_fol_rec_desc *d;
		struct m0_db_tx         tx;
		int                     rc;

		d = &r.fr_desc;
		result = m0_db_tx_init(&tx, env, 0);
		if (result == 0) {
			result = m0_fol_rec_lookup(fol, &tx, M0_LSN_ANCHOR, &r);
			if (result == -ENOENT) {
				m0_fol_rec_init(&r);
				/* initialise new fol */
				M0_SET0(d);
				d->rd_header.rh_refcount = 1;
				d->rd_lsn = M0_LSN_ANCHOR;
				fol->f_lsn = M0_LSN_ANCHOR + 1;
				result = m0_fol_rec_add(fol, &tx, &r);
				m0_fol_rec_fini(&r);
			} else if (result == 0) {
				result = m0_db_cursor_last(&r.fr_ptr,
							   &r.fr_pair);
				if (result == 0)
					fol->f_lsn = lsn_inc(r.fr_desc.rd_lsn);
				m0_fol_lookup_rec_fini(&r);
			}
			rc = m0_db_tx_commit(&tx);
			result = result ?: rc;
		}
	}
	M0_POST(ergo(result == 0, m0_lsn_is_valid(fol->f_lsn)));
	return result;
}
#else
M0_INTERNAL void m0_fol_init(struct m0_fol *fol, struct m0_be_seg *seg)
{
	struct m0_be_btree *tree = &fol->f_store;

	m0_mutex_init(&fol->f_lock);
	m0_be_btree_init(tree, seg, &fol_kv_ops);
}

M0_INTERNAL int m0_fol_create(struct m0_fol *fol, struct m0_be_tx *tx,
			      struct m0_be_op *op)
{
	struct m0_be_btree *tree = &fol->f_store;
	int                 rc;

	M0_PRE(m0_be_tx_state(tx) == M0_BTS_ACTIVE);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	if (tree->bb_root == NULL)
		M0_BE_OP_SYNC(local_op,
			      m0_be_btree_create(tree, tx, &local_op));

	rc = fol_setup(fol, tx);
	if (rc != 0) {
		M0_BE_OP_SYNC(local_op,
			      m0_be_btree_destroy(tree, tx, &local_op));
		m0_fol_fini(fol);
	}
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
	return rc;
}

M0_INTERNAL void m0_fol_destroy(struct m0_fol *fol, struct m0_be_tx *tx,
			        struct m0_be_op *op)
{
	struct m0_be_btree *tree = &fol->f_store;

	M0_PRE(m0_be_tx_state(tx) == M0_BTS_ACTIVE);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	if (tree->bb_root != NULL)
		M0_BE_OP_SYNC(local_op,
			      m0_be_btree_destroy(tree, tx, &local_op));

	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}
#endif

M0_INTERNAL void m0_fol_fini(struct m0_fol *fol)
{
#if XXX_USE_DB5
	m0_table_fini(&fol->f_table);
#else
	m0_be_btree_fini(&fol->f_store);
	m0_mutex_fini(&fol->f_lock);
#endif
}

M0_INTERNAL int m0_fol_force(struct m0_fol *fol, m0_lsn_t upto)
{
#if XXX_USE_DB5
	return m0_dbenv_sync(fol->f_table.t_env);
#else
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
#endif
}

#if !XXX_USE_DB5
M0_INTERNAL void m0_fol_credit(const struct m0_fol *fol, enum m0_fol_op optype,
			       m0_bcount_t nr, struct m0_be_tx_credit *accum)
{
	const struct m0_be_btree *tree = &fol->f_store;

	M0_PRE(nr > 0);

	switch (optype) {
	case M0_FO_CREATE:
		/* Several m0_fol_init()s seem pointless. */
		M0_ASSERT(nr == 1);
		m0_be_btree_create_credit(tree, nr, accum);
		m0_be_btree_insert_credit(tree, nr, FOL_KEY_SIZE,
					  FOL_REC_MAXSIZE, accum);
		/* This const-removing typecast is inelegant but it lets `fol'
		 * parameter be const, while doing no no harm. */
		m0_be_btree_destroy_credit((struct m0_be_btree *)tree, nr,
					   accum);
		break;
	case M0_FO_DESTROY:
		m0_be_btree_destroy_credit((struct m0_be_btree *)tree, nr,
					   accum);
		break;
	case M0_FO_REC_ADD:
		m0_be_btree_insert_credit(tree, nr, FOL_KEY_SIZE,
					  FOL_REC_MAXSIZE, accum);
		break;
	default:
		M0_IMPOSSIBLE("Invalid optype");
	}
}

/** Inserts first record into the fol. */
static int fol_zero(struct m0_fol *fol, struct m0_be_tx *tx)
{
	struct m0_fol_rec rec;
	int               rc;

	M0_PRE(m0_be_btree_is_empty(&fol->f_store));

	M0_CASSERT(M0_LSN_ANCHOR > M0_LSN_RESERVED_NR);

	m0_fol_rec_init(&rec);
	rec.fr_desc = (struct m0_fol_rec_desc){
		.rd_header = { .rh_refcount = 1 },
		.rd_lsn    = M0_LSN_ANCHOR
	};
	M0_BE_OP_SYNC(op, rc = m0_fol_rec_add(fol, &rec, tx, &op));
	if (rc == 0)
		fol->f_lsn = M0_LSN_ANCHOR + 1;

	m0_fol_rec_fini(&rec);
	return rc;
}

/** Increments m0_fol::f_lsn. */
static int fol_inc(struct m0_fol *fol, struct m0_fol_rec *rec)
{
	int rc;

	rc = m0_be_btree_cursor_last_sync(&rec->fr_ptr);
	if (rc == 0) {
		m0_be_btree_cursor_kv_get(&rec->fr_ptr, &rec->fr_key,
					  &rec->fr_val);
		fol->f_lsn = lsn_inc(rec->fr_desc.rd_lsn);
	}
	return rc;
}

static int fol_setup(struct m0_fol *fol, struct m0_be_tx *tx)
{
	struct m0_fol_rec rec;
	int               rc;

	M0_PRE(fol->f_store.bb_root != NULL);

	rc = m0_fol_rec_lookup(fol, M0_LSN_ANCHOR, &rec);
	if (rc == -ENOENT)
		rc = fol_zero(fol, tx);
	else if (rc == 0)
		rc = fol_inc(fol, &rec);

	m0_fol_lookup_rec_fini(&rec);
	return rc;
}
#endif

/* ------------------------------------------------------------------
 * FOL records and their parts
 * ------------------------------------------------------------------ */

/**
 * Initializes fields in @rec.
 *
 * Note, that key buffer in the cursor is initialized to point to
 * rec->fr_desc.rd_lsn. As a result, the cursor's key follows lsn changes
 * automagically.
 *
 * @see m0_fol_rec_fini()
 */
#if XXX_USE_DB5
static int rec_init(struct m0_fol_rec *rec, struct m0_db_tx *tx)
{
	struct m0_db_pair *pair;
	M0_PRE(rec->fr_fol != NULL);

	m0_fol_rec_init(rec);
	pair = &rec->fr_pair;
	m0_db_pair_setup(pair, &rec->fr_fol->f_table, &rec->fr_desc.rd_lsn,
			 FOL_KEY_SIZE, NULL, 0);
	return m0_db_cursor_init(&rec->fr_ptr, &rec->fr_fol->f_table, tx, 0);
}
#else
static void rec_init(struct m0_fol_rec *rec, struct m0_fol *fol)
{
	M0_PRE(fol != NULL);

	m0_fol_rec_init(rec);
	rec->fr_fol = fol;
	m0_buf_init(&rec->fr_key, &rec->fr_desc.rd_lsn, FOL_KEY_SIZE);
	M0_SET0(&rec->fr_val);
	m0_be_btree_cursor_init(&rec->fr_ptr, &rec->fr_fol->f_store);
}
#endif

/**
 * Finalizes @rec.
 *
 * @see rec_init()
 */
M0_INTERNAL void m0_fol_lookup_rec_fini(struct m0_fol_rec *rec)
{
#if XXX_USE_DB5
	m0_db_cursor_fini(&rec->fr_ptr);
	m0_db_pair_fini(&rec->fr_pair);
#else
	m0_be_btree_cursor_put(&rec->fr_ptr);
	m0_be_btree_cursor_fini(&rec->fr_ptr);
#endif
	m0_fol_rec_fini(rec);
}

M0_INTERNAL void m0_fol_rec_init(struct m0_fol_rec *rec)
{
	m0_rec_part_tlist_init(&rec->fr_parts);
}

M0_INTERNAL void m0_fol_rec_fini(struct m0_fol_rec *rec)
{
	struct m0_fol_rec_part *part;

	m0_tl_for(m0_rec_part, &rec->fr_parts, part) {
		m0_fol_rec_part_fini(part);
	} m0_tl_endfor;
	m0_rec_part_tlist_fini(&rec->fr_parts);
}

M0_INTERNAL bool m0_fol_rec_invariant(const struct m0_fol_rec_desc *drec)
{
	const struct m0_fol_rec_header *h = &drec->rd_header;
	uint32_t i;
	uint32_t j;

	if (!m0_lsn_is_valid(drec->rd_lsn))
		return false;
	if (h->rh_magic != M0_FOL_REC_MAGIC)
		return false;
	for (i = 0; i < h->rh_obj_nr; ++i) {
		struct m0_fol_obj_ref *ref;

		ref = &drec->rd_ref[i];
		if (!m0_fid_is_valid(&ref->or_fid))
			return false;
		if (!m0_lsn_is_valid(ref->or_before_ver.vn_lsn) &&
		    ref->or_before_ver.vn_lsn != M0_LSN_NONE)
			return false;
		if (drec->rd_lsn <= ref->or_before_ver.vn_lsn)
			return false;
		for (j = 0; j < i; ++j) {
			if (m0_fid_eq(&ref->or_fid, &drec->rd_ref[j].or_fid))
				return false;
		}
	}
/* XXX DELETEME: Do we care about the disabled section?  --vvv */
#if 0
	if (!m0_epoch_is_valid(&drec->rd_epoch))
		return false;
	if (!m0_update_is_valid(&h->rh_self))
		return false;
	for (i = 0; i < h->rh_sibling_nr; ++i) {
		struct m0_fol_update_ref *upd;

		upd = &drec->rd_sibling[i];
		if (!m0_update_is_valid(&upd->ui_id))
			return false;
		if (!m0_update_state_is_valid(upd->ui_state))
			return false;
		for (j = 0; j < i; ++j) {
			if (m0_update_is_eq(&upd->ui_id,
					    &drec->rd_sibling[j].ui_id))
				return false;
		}
	}
#endif
	return true;
}

M0_INTERNAL void m0_fol_rec_part_init(struct m0_fol_rec_part *part, void *data,
				      const struct m0_fol_rec_part_type *type)
{
	M0_PRE(part != NULL);
	M0_PRE(type != NULL && type->rpt_ops != NULL);

	part->rp_data = data;
	type->rpt_ops->rpto_rec_part_init(part);
	m0_rec_part_tlink_init(part);
}

M0_INTERNAL void m0_fol_rec_part_fini(struct m0_fol_rec_part *part)
{
	M0_PRE(part != NULL);
	M0_PRE(part->rp_ops != NULL);
	M0_PRE(part->rp_data != NULL);

	if (m0_rec_part_tlink_is_in(part))
		m0_rec_part_tlist_del(part);
	m0_rec_part_tlink_fini(part);

	if (part->rp_flag == M0_XCODE_DECODE) {
		m0_xcode_free(&REC_PART_XCODE_OBJ(part));
		m0_free(part);
	} else {
	    if (part->rp_ops->rpo_type == &m0_fop_fol_rec_part_type) {
		m0_free(part->rp_data);
		m0_free(part);
	    } else
		m0_xcode_free(&REC_PART_XCODE_OBJ(part));
	}
}

/* ------------------------------------------------------------------
 * Record part types
 * ------------------------------------------------------------------ */

enum {
	FOL_REC_PART_TYPE_MAX = 128,
	PART_TYPE_START_INDEX = 1
};

static const struct m0_fol_rec_part_type *rptypes[FOL_REC_PART_TYPE_MAX];
static struct m0_mutex rptypes_lock;

M0_INTERNAL int m0_fols_init(void)
{
	m0_xc_fol_init();
	m0_mutex_init(&rptypes_lock);
	return 0;
}

M0_INTERNAL void m0_fols_fini(void)
{
	m0_xc_fol_fini();
	m0_mutex_fini(&rptypes_lock);
}

M0_INTERNAL int
m0_fol_rec_part_type_register(struct m0_fol_rec_part_type *type)
{
	int		result;
	static uint32_t index = PART_TYPE_START_INDEX;

	M0_PRE(type != NULL);
	M0_PRE(type->rpt_xt != NULL && type->rpt_ops != NULL);
	M0_PRE(type->rpt_index == 0);

	m0_mutex_lock(&rptypes_lock);
	if (IS_IN_ARRAY(index, rptypes)) {
		M0_ASSERT(rptypes[index] == NULL);
		rptypes[index]  = type;
		type->rpt_index = index;
		++index;
		result = 0;
	} else
		result = -EFBIG;
	m0_mutex_unlock(&rptypes_lock);
	return result;
}

M0_INTERNAL void
m0_fol_rec_part_type_deregister(struct m0_fol_rec_part_type *type)
{
	M0_PRE(type != NULL);

	m0_mutex_lock(&rptypes_lock);
	M0_PRE(IS_IN_ARRAY(type->rpt_index, rptypes));
	M0_PRE(rptypes[type->rpt_index] == type ||
	       rptypes[type->rpt_index] == NULL);

	rptypes[type->rpt_index] = NULL;
	m0_mutex_unlock(&rptypes_lock);
	type->rpt_index = 0;
	type->rpt_xt	= NULL;
	type->rpt_ops	= NULL;
}

static const struct m0_fol_rec_part_type *
fol_rec_part_type_lookup(uint32_t index)
{
	M0_PRE(IS_IN_ARRAY(index, rptypes));
	return rptypes[index];
}

/* ------------------------------------------------------------------
 * Record encoding/decoding
 * ------------------------------------------------------------------ */

static m0_bcount_t fol_rec_header_pack_size(struct m0_fol_rec_header *h)
{
	struct m0_xcode_ctx ctx;
	int len = m0_xcode_data_size(&ctx, &M0_REC_HEADER_XCODE_OBJ(h));

	M0_POST(len > 0);
	return len;
}

static size_t fol_record_pack_size(struct m0_fol_rec *rec)
{
	struct m0_fol_rec_desc       *desc = &rec->fr_desc;
	struct m0_fol_rec_header     *h = &desc->rd_header;
	const struct m0_fol_rec_part *part;
	struct m0_fol_rec_part_header rph;
	struct m0_xcode_ctx           ctx;
	m0_bcount_t                   len;

	len = fol_rec_header_pack_size(h) +
	      h->rh_obj_nr *
		m0_xcode_data_size(&ctx, &REC_OBJ_REF_XCODE_OBJ(desc->rd_ref)) +
	      h->rh_sibling_nr *
		m0_xcode_data_size(&ctx,
				   &REC_SIBLING_XCODE_OBJ(desc->rd_sibling)) +
	      h->rh_parts_nr *
		m0_xcode_data_size(&ctx, &REC_PART_HEADER_XCODE_OBJ(&rph));

	m0_tl_for(m0_rec_part, &rec->fr_parts, part) {
		len += m0_xcode_data_size(&ctx, &REC_PART_XCODE_OBJ(part));
	} m0_tl_endfor;

	len = m0_align(len, 8);
#if !XXX_USE_DB5
	M0_POST(len <= FOL_REC_MAXSIZE);
#endif
	return len;
}

static int fol_rec_desc_encdec(struct m0_fol_rec_desc *desc,
			       struct m0_bufvec_cursor *cur,
			       enum m0_xcode_what what)
{
	struct m0_fol_rec_header *h = &desc->rd_header;
	uint32_t		  i;
	int			  rc;

	M0_PRE(ergo(what == M0_XCODE_ENCODE, h->rh_magic == M0_FOL_REC_MAGIC));

	rc = m0_xcode_encdec(&M0_REC_HEADER_XCODE_OBJ(h), cur, what);
	if (rc != 0)
		return rc;

	if (what == M0_XCODE_DECODE && h->rh_refcount == 0)
		return -ENOENT;

	for (i = 0; i < h->rh_obj_nr; ++i) {
		struct m0_fol_obj_ref *r = &desc->rd_ref[i];
		rc = m0_xcode_encdec(&REC_OBJ_REF_XCODE_OBJ(r), cur, what);
		if (rc != 0)
			return rc;
	}

	for (i = 0; i < h->rh_sibling_nr; ++i) {
		struct m0_fol_update_ref *r = &desc->rd_sibling[i];
		rc = m0_xcode_encdec(&REC_SIBLING_XCODE_OBJ(r), cur, what);
		if (rc != 0)
			return rc;
	}

	M0_POST(ergo(what == M0_XCODE_DECODE, h->rh_magic == M0_FOL_REC_MAGIC));
	return 0;
}

static int fol_record_pack(struct m0_fol_rec *rec, struct m0_buf *buf)
{
	struct m0_fol_rec_part *part;
	m0_bcount_t	        len = buf->b_nob;
	struct m0_bufvec        bvec = M0_BUFVEC_INIT_BUF(&buf->b_addr, &len);
	struct m0_bufvec_cursor cur;
	int			rc;

	m0_bufvec_cursor_init(&cur, &bvec);

	rc = fol_rec_desc_encdec(&rec->fr_desc, &cur, M0_XCODE_ENCODE);
	if (rc != 0)
		return rc;

	m0_tl_for(m0_rec_part, &rec->fr_parts, part) {
		struct m0_fol_rec_part_header rph;
		uint32_t		      index;

		index = part->rp_ops != NULL ?
			part->rp_ops->rpo_type->rpt_index :
			m0_fop_fol_rec_part_type.rpt_index;

		rph = (struct m0_fol_rec_part_header) {
			.rph_index = index,
			.rph_magic = M0_FOL_REC_PART_MAGIC
		};

		rc = m0_xcode_encdec(&REC_PART_HEADER_XCODE_OBJ(&rph),
				     &cur, M0_XCODE_ENCODE) ?:
		     m0_xcode_encdec(&REC_PART_XCODE_OBJ(part),
				     &cur, M0_XCODE_ENCODE);
		if (rc != 0)
			return rc;
	} m0_tl_endfor;

	return rc;
}

static int fol_record_encode(struct m0_fol_rec *rec, struct m0_buf *out)
{
	struct m0_fol_rec_header *h = &rec->fr_desc.rd_header;
	size_t                    size;
	void                     *buf;

	h->rh_magic = M0_FOL_REC_MAGIC;
	h->rh_parts_nr = m0_rec_part_tlist_length(&rec->fr_parts);

	size = fol_record_pack_size(rec);
	M0_ASSERT(M0_IS_8ALIGNED(size));

	h->rh_data_len = size;

	buf = m0_alloc(size);
	if (buf == NULL)
		return -ENOMEM;

	m0_buf_init(out, buf, size);
	return fol_record_pack(rec, out);
}

static int fol_record_decode(struct m0_fol_rec *rec)
{
	struct m0_fol_rec_desc *desc = &rec->fr_desc;
#if XXX_USE_DB5
	struct m0_buf	       *rec_buf = &rec->fr_pair.dp_rec.db_buf;
	void		       *buf = &rec_buf->b_addr;
	m0_bcount_t		len = rec_buf->b_nob;
	struct m0_bufvec	bvec = M0_BUFVEC_INIT_BUF(buf, &len);
#else
	struct m0_bufvec        bvec = M0_BUFVEC_INIT_BUF(&rec->fr_val.b_addr,
							  &rec->fr_val.b_nob);
#endif
	struct m0_bufvec_cursor cur;
	uint32_t                i;
	int                     rc;

	m0_bufvec_cursor_init(&cur, &bvec);

	rc = fol_rec_desc_encdec(desc, &cur, M0_XCODE_DECODE);
	if (rc != 0)
		return rc;

	for (i = 0; rc == 0 && i < desc->rd_header.rh_parts_nr; ++i) {
		struct m0_fol_rec_part            *part;
		const struct m0_fol_rec_part_type *part_type;
		struct m0_fol_rec_part_header      ph;

		rc = m0_xcode_encdec(&REC_PART_HEADER_XCODE_OBJ(&ph), &cur,
				     M0_XCODE_DECODE);
		if (rc == 0) {
			void *rp_data;

			part_type = fol_rec_part_type_lookup(ph.rph_index);

			M0_ALLOC_PTR(part);
			if (part == NULL)
				return -ENOMEM;

			rp_data = m0_alloc(part_type->rpt_xt->xct_sizeof);
			if (rp_data == NULL) {
				m0_free(part);
				return -ENOMEM;
			}

			part->rp_flag = M0_XCODE_DECODE;

			m0_fol_rec_part_init(part, rp_data, part_type);
			rc = m0_xcode_encdec(&REC_PART_XCODE_OBJ(part), &cur,
					     M0_XCODE_DECODE);
			if (rc == 0)
				m0_fol_rec_part_add(rec, part);
		}
	}
	return rc;
}

/* ------------------------------------------------------------------
 * Adding and searching records
 * ------------------------------------------------------------------ */

#if XXX_USE_DB5
M0_INTERNAL int m0_fol_rec_add(struct m0_fol *fol,
			       struct m0_db_tx *tx,
			       struct m0_fol_rec *rec)
{
	struct m0_buf buf;
	int           rc;

	rc = fol_record_encode(rec, &buf);
	if (rc == 0) {
		rc = m0_fol_add_buf(fol, tx, &rec->fr_desc, &buf));
		m0_buf_free(&buf);
	}
	return rc;
}
#else
M0_INTERNAL int m0_fol_rec_add(struct m0_fol *fol,
			       struct m0_fol_rec *rec,
			       struct m0_be_tx *tx,
			       struct m0_be_op *op)
{
	struct m0_buf buf;
	int           rc;

	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	rc = fol_record_encode(rec, &buf);
	if (rc == 0) {
		M0_BE_OP_SYNC(local_op,
			      rc = m0_fol_add_buf(fol, &rec->fr_desc, &buf, tx,
						  &local_op));
		m0_buf_free(&buf);
	}

	m0_be_op_state_set(op, M0_BOS_SUCCESS);
	return rc;
}
#endif

M0_INTERNAL void m0_fol_rec_part_add(struct m0_fol_rec *rec,
				     struct m0_fol_rec_part *part)
{
	M0_PRE(rec != NULL && part != NULL);

	m0_rec_part_tlist_add_tail(&rec->fr_parts, part);
}

#if XXX_USE_DB5
M0_INTERNAL int m0_fol_add_buf(struct m0_fol *fol, struct m0_db_tx *tx,
			       struct m0_fol_rec_desc *drec, struct m0_buf *buf)
{
	struct m0_db_pair pair;

	M0_PRE(m0_lsn_is_valid(drec->rd_lsn));

	m0_db_pair_setup(&pair, &fol->f_table, &drec->rd_lsn, FOL_KEY_SIZE,
			 buf->b_addr, buf->b_nob);
	return m0_table_insert(tx, &pair);
}
#else
M0_INTERNAL int m0_fol_add_buf(struct m0_fol *fol, struct m0_fol_rec_desc *drec,
			       struct m0_buf *buf,
			       struct m0_be_tx *tx, struct m0_be_op *op)
{
	int rc;

	M0_PRE(m0_lsn_is_valid(drec->rd_lsn));

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	rc = M0_BE_OP_SYNC_RET(
		local_op,
		m0_be_btree_insert(&fol->f_store, tx, &local_op,
				   &M0_BUF_INIT(FOL_KEY_SIZE, &drec->rd_lsn),
				   buf),
		bo_u.u_btree.t_rc);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
	return rc;
}
#endif

#if XXX_USE_DB5
M0_INTERNAL int m0_fol_rec_lookup(struct m0_fol *fol, struct m0_db_tx *tx,
				  m0_lsn_t lsn, struct m0_fol_rec *out)
{
	int result;

	out->fr_fol = fol;
	result = rec_init(out, tx);
	if (result == 0) {
		out->fr_desc.rd_lsn = lsn;
		result = m0_db_cursor_get(&out->fr_ptr, &out->fr_pair) ?:
			 fol_record_decode(out);
		if (result != 0)
			m0_fol_lookup_rec_fini(out);
	}
	M0_POST(ergo(result == 0, out->fr_desc.rd_lsn == lsn));
	M0_POST(ergo(result == 0, out->fr_desc.rd_header.rh_refcount > 0));
	M0_POST(ergo(result == 0, m0_fol_rec_invariant(&out->fr_desc)));
	return result;
}
#else
M0_INTERNAL int
m0_fol_rec_lookup(struct m0_fol *fol, m0_lsn_t lsn, struct m0_fol_rec *out)
{
	struct m0_fol_rec_desc    *d   = &out->fr_desc;
	struct m0_be_btree_cursor *cur = &out->fr_ptr;
	int                        rc;

	rec_init(out, fol);

	d->rd_lsn = lsn;
	rc = m0_be_btree_cursor_get_sync(cur, &out->fr_key, true);
	if (rc != 0)
		goto out;
	m0_be_btree_cursor_kv_get(cur, &out->fr_key, &out->fr_val);

	rc = fol_record_decode(out);
	if (rc != 0)
		goto out;

	M0_POST(d->rd_header.rh_refcount > 0);
	M0_POST(m0_fol_rec_invariant(d));
out:
	if (rc != 0)
		m0_fol_lookup_rec_fini(out);
	return rc;
}
#endif

/** @} end of fol group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
