/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 09/09/2010
 */

#include "lib/adt.h"           /* m0_buf */
#include "lib/arith.h"         /* M0_3WAY */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"          /* M0_SET0 */
#include "lib/cdefs.h"         /* M0_EXPORTED */
#include "lib/vec.h"
#include "mero/magic.h"
#include "rpc/rpc_opcodes.h"
#include "fol/fol.h"
#include "xcode/xcode.h"

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

static int fol_record_encode(struct m0_fol_rec *rec, struct m0_buf *out);
static int fol_record_decode(struct m0_fol_rec *rec);
static size_t fol_record_pack_size(struct m0_fol_rec *rec);
static void fol_record_pack(struct m0_fol_rec *rec, struct m0_buf *buf);
static inline m0_bcount_t fol_rec_part_data_size(struct m0_fol_rec_part *part);
static inline int fol_rec_part_encdec(struct m0_fol_rec_part  *part,
			              struct m0_bufvec_cursor *cur,
			              enum m0_bufvec_what      what);

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
	++lsn;
	M0_ASSERT(lsn != 0);
	M0_POST(m0_lsn_is_valid(lsn));
	return lsn;
}

static int lsn_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	const m0_lsn_t *lsn0 = key0;
	const m0_lsn_t *lsn1 = key1;

	return m0_lsn_cmp(*lsn0, *lsn1);
}

static const struct m0_table_ops fol_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(m0_lsn_t)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = lsn_cmp
};

M0_INTERNAL void m0_fol_rec_part_list_init(struct m0_fol_rec *rec)
{
	M0_PRE(rec != NULL);

	m0_rec_part_tlist_init(&rec->fr_fol_rec_parts);
}

M0_INTERNAL void m0_fol_rec_part_list_fini(struct m0_fol_rec *rec)
{
	struct m0_fol_rec_part  *part;

	m0_tl_for(m0_rec_part, &rec->fr_fol_rec_parts, part) {
		m0_fol_rec_part_fini(part);
	} m0_tl_endfor;
	m0_rec_part_tlist_fini(&rec->fr_fol_rec_parts);
}

M0_INTERNAL void m0_fol_rec_part_list_add(struct m0_fol_rec *rec,
				          struct m0_fol_rec_part *part)
{
	M0_PRE(rec != NULL && part != NULL);

	m0_rec_part_tlist_add_tail(&rec->fr_fol_rec_parts, part);
}

/**
   Initializes fields in @rec.

   Note, that key buffer in the cursor is initialized to point to
   rec->fr_desc.rd_lsn. As a result, the cursor's key follows lsn changes
   automagically.

   @see m0_fol_rec_fini()
 */
static int rec_init(struct m0_fol_rec *rec, struct m0_db_tx *tx)
{
	struct m0_db_pair *pair;

	M0_PRE(rec->fr_fol != NULL);

	m0_fol_rec_part_list_init(rec);
	pair = &rec->fr_pair;
	m0_db_pair_setup(pair, &rec->fr_fol->f_table, &rec->fr_desc.rd_lsn,
			 sizeof rec->fr_desc.rd_lsn, NULL, 0);
	return m0_db_cursor_init(&rec->fr_ptr, &rec->fr_fol->f_table, tx, 0);
}

/**
   Finalizes @rec.

   @see rec_init()
 */
M0_INTERNAL void m0_fol_rec_fini(struct m0_fol_rec *rec)
{
	m0_fol_rec_part_list_fini(rec);
	m0_db_cursor_fini(&rec->fr_ptr);
	m0_db_pair_fini(&rec->fr_pair);
}

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
				result = rec_init(&r, &tx);
				M0_ASSERT(result == 0);
				/* initialise new fol */
				M0_SET0(d);
				d->rd_header.rh_refcount = 1;
				d->rd_lsn = M0_LSN_ANCHOR;
				fol->f_lsn = M0_LSN_ANCHOR + 1;
				result = m0_fol_rec_add(fol, &tx, &r);
			} else if (result == 0) {
				result = m0_db_cursor_last(&r.fr_ptr,
							   &r.fr_pair);
				if (result == 0)
					fol->f_lsn = lsn_inc(r.fr_desc.rd_lsn);
			}
			rc = m0_db_tx_commit(&tx);
			result = result ?: rc;
			m0_fol_rec_fini(&r);
		}
	}
	M0_POST(ergo(result == 0, m0_lsn_is_valid(fol->f_lsn)));
	return result;
}

M0_INTERNAL void m0_fol_fini(struct m0_fol *fol)
{
	m0_table_fini(&fol->f_table);
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

M0_INTERNAL int m0_fol_add_buf(struct m0_fol *fol, struct m0_db_tx *tx,
			       struct m0_fol_rec_desc *drec, struct m0_buf *buf)
{
	struct m0_db_pair pair;

	M0_PRE(m0_lsn_is_valid(drec->rd_lsn));

	m0_db_pair_setup(&pair, &fol->f_table,
			 &drec->rd_lsn, sizeof drec->rd_lsn,
			 buf->b_addr, buf->b_nob);
	return m0_table_insert(tx, &pair);
}

M0_INTERNAL int m0_fol_force(struct m0_fol *fol, m0_lsn_t upto)
{
	return m0_dbenv_sync(fol->f_table.t_env);
}

M0_INTERNAL bool m0_fol_rec_invariant(const struct m0_fol_rec_desc *drec)
{
	uint32_t i;
	uint32_t j;

	if (!m0_lsn_is_valid(drec->rd_lsn))
		return false;
	for (i = 0; i < drec->rd_header.rh_obj_nr; ++i) {
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
#if 0
	if (!m0_epoch_is_valid(&drec->rd_epoch))
		return false;
	if (!m0_update_is_valid(&drec->rd_header.rh_self))
		return false;
	for (i = 0; i < drec->rd_header.rh_sibling_nr; ++i) {
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

enum {
	FOL_REC_PART_TYPE_MAX = 128,
	PART_TYPE_START_INDEX = 1
};

static const struct m0_fol_rec_part_type *rptypes[FOL_REC_PART_TYPE_MAX];
static struct m0_mutex rptypes_lock;

M0_INTERNAL int m0_fols_init(void)
{
	m0_mutex_init(&rptypes_lock);
	return 0;
}

M0_INTERNAL void m0_fols_fini(void)
{
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
	m0_mutex_unlock(&rptypes_lock);
	M0_PRE(IS_IN_ARRAY(type->rpt_index, rptypes));
	M0_PRE(rptypes[type->rpt_index] == type ||
	       rptypes[type->rpt_index] == NULL);

	rptypes[type->rpt_index] = NULL;
	m0_mutex_unlock(&rptypes_lock);
	type->rpt_index = 0;
	type->rpt_xt	= NULL;
	type->rpt_ops	= NULL;
}

M0_INTERNAL void
m0_fol_rec_part_init(struct m0_fol_rec_part *part, void *data,
		     const struct m0_fol_rec_part_type *type)
{
	M0_PRE(part != NULL);

	part->rp_data = data;
	type->rpt_ops->rpto_rec_part_init(part);
	m0_rec_part_tlink_init(part);
}

M0_INTERNAL void m0_fol_rec_part_fini(struct m0_fol_rec_part *part)
{
	if (part->rp_data != NULL && part->rp_flag == M0_BUFVEC_DECODE)
		m0_xcode_free(&M0_FOL_REC_PART_XCODE_OBJ(part));

	if (m0_rec_part_tlink_is_in(part))
		m0_rec_part_tlist_del(part);
	m0_rec_part_tlink_fini(part);
}

M0_INTERNAL int m0_fol_rec_add(struct m0_fol *fol, struct m0_db_tx *tx,
			       struct m0_fol_rec *rec)
{
	int                     result;
	struct m0_buf           buf;
	struct m0_fol_rec_desc *desc = &rec->fr_desc;

	M0_PRE(m0_lsn_is_valid(desc->rd_lsn));

	result = fol_record_encode(rec, &buf);
	if (result == 0) {
		result = m0_fol_add_buf(fol, tx, desc, &buf);
		m0_free(buf.b_addr);
	}
	return result;
}

static int fol_record_encode(struct m0_fol_rec *rec, struct m0_buf *out)
{
	void                   *buf;
	size_t                  size;
	int                     result;
	struct m0_fol_rec_desc *desc = &rec->fr_desc;

	size = fol_record_pack_size(rec);
	M0_ASSERT(M0_IS_8ALIGNED(size));

	desc->rd_header.rh_data_len = size;
	desc->rd_header.rh_parts_nr =
		m0_rec_part_tlist_length(&rec->fr_fol_rec_parts);

	buf = m0_alloc(size);
	if (buf != NULL) {
		out->b_addr = buf;
		out->b_nob  = size;
		fol_record_pack(rec, out);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

static size_t fol_record_pack_size(struct m0_fol_rec *rec)
{
	m0_bcount_t len = 0;

	/**
	 * @todo compute the size of FOL record descriptor and
	 *  FOL record part's header and FOL record parts.
	 *  Use fol_rec_part_data_size() to get the size of
	 *  fol record part.
	 */
	return m0_align(len, 8);
}

static inline m0_bcount_t fol_rec_part_data_size(struct m0_fol_rec_part *part)
{
	struct m0_xcode_ctx  xc_ctx;

	return m0_xcode_data_size(&xc_ctx, &M0_FOL_REC_PART_XCODE_OBJ(part));
}

static void fol_record_pack(struct m0_fol_rec *rec, struct m0_buf *buf)
{
	/**
	 * @todo Encode FOL record descriptor and traverse
	 *  rec->fr_fol_rec_parts and encode fol record
	 *  parts in the buffer using fol_rec_part_encdec().
	 *  Also encode m0_fol_rec_part_type::rpt_index
	 *  for each FOl record type which will be used to decode this
	 *  from fol record.
	 * Add assertion part->rp_data != NULL before encoding the record part.
	 * Assign part->rp_flag to M0_BUFVEC_ENCODE;
	 */
}

static inline int fol_rec_part_encdec(struct m0_fol_rec_part  *part,
			              struct m0_bufvec_cursor *cur,
			              enum m0_bufvec_what      what)
{
	int		     rc;
	struct m0_xcode_ctx  xc_ctx;

	rc = m0_xcode_encdec(&xc_ctx, &M0_FOL_REC_PART_XCODE_OBJ(part), cur,
			     what);
	if (rc == 0 && what == M0_BUFVEC_DECODE)
		part->rp_data = m0_xcode_ctx_top(&xc_ctx);
	return rc;
}

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
			m0_fol_rec_fini(out);
	}
	M0_POST(ergo(result == 0, out->fr_desc.rd_lsn == lsn));
	M0_POST(ergo(result == 0, out->fr_desc.rd_header.rh_refcount > 0));
	M0_POST(ergo(result == 0, m0_fol_rec_invariant(&out->fr_desc)));
	return result;
}

static int fol_record_decode(struct m0_fol_rec *rec)
{
	/**
	 * @todo Decode FOL record descriptor and parts from record
	 * buffer rec->fr_pair.dp_rec.db_buf;
	 * Add assertion part->rp_data == NULL before decoding the record part.
	 * Assign part->rp_flag to M0_BUFVEC_DECODE;
	 */
	return 0;
}

M0_INTERNAL struct m0_fol_rec *m0_fol_record_init(void)
{
	struct m0_fol_rec *rec;

	M0_ALLOC_PTR(rec);
	if (rec == NULL)
		return NULL;
	m0_rec_part_tlist_init(&rec->fr_fol_rec_parts);
	return rec;
}

M0_INTERNAL void m0_fol_record_fini(struct m0_fol_rec *rec)
{
	struct m0_fol_rec_part  *part;

	m0_tl_for(m0_rec_part, &rec->fr_fol_rec_parts, part)
	{
		m0_fol_rec_part_fini(part);
	} m0_tl_endfor;
	m0_rec_part_tlist_fini(&rec->fr_fol_rec_parts);
	m0_free(rec);
}

static uint32_t fol_rec_parts_nr(struct m0_fol_rec *rec)
{
	M0_PRE(rec != NULL);

	return m0_rec_part_tlist_length(&rec->fr_fol_rec_parts);
}

M0_INTERNAL m0_bcount_t m0_fol_rec_part_data_size(struct m0_fol_rec_part *part)
{
	struct m0_xcode_ctx  xc_ctx;

	return m0_xcode_data_size(&xc_ctx, &M0_FOL_REC_PART_XCODE_OBJ(part));
}

M0_INTERNAL int m0_fol_rec_part_encdec(struct m0_fol_rec_part  *part,
			               struct m0_bufvec_cursor *cur,
			               enum m0_bufvec_what      what)
{
	int		     rc;
	struct m0_xcode_ctx  xc_ctx;

	rc = m0_xcode_encdec(&xc_ctx, &M0_FOL_REC_PART_XCODE_OBJ(part), cur,
			     what);
	if (rc == 0 && what == M0_BUFVEC_DECODE)
		part->rp_data = m0_xcode_ctx_top(&xc_ctx);
	return rc;
}

static int fol_rec_part_type_register(struct m0_fol_rec_part_type *type)
{
	/**
	 * @todo Global list of FOL record part type are mainitained to
	 * register them.
	 */
	return 0;
}

static void fol_rec_part_type_deregister(struct m0_fol_rec_part_type *type)
{

}

M0_INTERNAL int m0_fol_rec_part_type_init(struct m0_fol_rec_part_type *type,
					  const char *name,
					  const struct m0_xcode_type *xt)
{
	type->rpt_xt   = xt;
	type->rpt_name = name;
	return fol_rec_part_type_register(type);
}

M0_INTERNAL void m0_fol_rec_part_type_fini(struct m0_fol_rec_part_type *type)
{
	fol_rec_part_type_deregister(type);
	type->rpt_xt   = NULL;
	type->rpt_name = NULL;
}

static int fol_rec_part_data_alloc(struct m0_fol_rec_part *part)
{
	size_t fol_rec_part_size;
	M0_PRE(part->rp_ops != NULL && part->rp_ops->rpo_type != NULL);

	fol_rec_part_size = part->rp_ops->rpo_type->rpt_xt->xct_sizeof;

	part->rp_data = m0_alloc(fol_rec_part_size);
	return part->rp_data == NULL ? -ENOMEM : 0;
}

M0_INTERNAL struct m0_fol_rec_part *m0_fol_rec_part_init(
	const struct m0_fol_rec_part_ops *ops)
{
	struct m0_fol_rec_part *part;

	M0_ALLOC_PTR(part);
	if (part != NULL) {
		int rc;
		part->rp_ops  = ops;
		rc = fol_rec_part_data_alloc(part);
		if (rc != 0) {
			m0_free(part);
			return NULL;
		}
		m0_rec_part_tlink_init(part);
	}
	return part;
}

M0_INTERNAL void m0_fol_rec_part_fini(struct m0_fol_rec_part *part)
{
	if (part->rp_data != NULL)
		m0_xcode_free(&M0_FOL_REC_PART_XCODE_OBJ(part));

	if (m0_rec_part_tlink_is_in(part))
		m0_rec_part_tlist_del(part);
	m0_rec_part_tlink_fini(part);
	m0_free(part);
}

static size_t fol_record_pack_size(struct m0_fol_rec *rec)
{
	m0_bcount_t len = 0;

	/**
	 * @todo compute the size of FOL record descriptor and
	 *  FOL record part's header and FOL record parts.
	 */
	return m0_align(len, 8);
}

static void fol_record_pack(struct m0_fol_rec *rec, struct m0_buf *buf)
{
	/**
	 * @todo Encode FOL record descriptor and traverse
	 *  rec->fr_fol_rec_parts and encode FOL record
	 *  parts in the buffer using fol_rec_part_encdec().
	 *  Also encode m0_fol_rec_part_type::rpt_index
	 *  for each FOl record type which will be used to decode this
	 *  from FOL record.
	 */
}

static int fol_record_encode(struct m0_fol_rec *rec, struct m0_buf *out)
{
	void                   *buf;
	size_t                  size;
	int                     result;
	struct m0_fol_rec_desc *desc = &rec->fr_desc;

	size = fol_record_pack_size(rec);
	M0_ASSERT(M0_IS_8ALIGNED(size));

	desc->rd_header.rh_opcode   = desc->rd_type->rt_opcode;
	desc->rd_header.rh_data_len = size;
	desc->rd_header.rh_parts_nr = fol_rec_parts_nr(rec);

	buf = m0_alloc(size);
	if (buf != NULL) {
		out->b_addr = buf;
		out->b_nob  = size;
		fol_record_pack(rec, out);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

M0_INTERNAL int m0_fol_record_add(struct m0_fol *fol, struct m0_dtx *dtx)
{
	int                     result;
	struct m0_buf           buf;
	struct m0_fol_rec      *rec = dtx->tx_fol_rec;
	struct m0_fol_rec_desc *desc = &rec->fr_desc;

	M0_PRE(m0_lsn_is_valid(desc->rd_lsn));

	result = fol_record_encode(rec, &buf);
	if (result == 0) {
		result = m0_fol_add_buf(fol, &dtx->tx_dbtx, desc, &buf);
		m0_free(buf.b_addr);
	}
	return result;
}

static int fol_record_decode(struct m0_fol_rec *rec)
{
	/**
	 * @todo Decode FOL record descriptor and parts from record
	 * buffer rec->fr_pair.dp_rec.db_buf;
	 */
	return 0;
}

M0_INTERNAL int m0_fol_record_lookup(struct m0_fol *fol, struct m0_db_tx *tx,
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
			rec_fini(out);
	}
	M0_POST(ergo(result == 0, out->fr_desc.rd_lsn == lsn));
	M0_POST(ergo(result == 0, out->fr_desc.rd_header.rh_refcount > 0));
	M0_POST(ergo(result == 0, m0_fol_rec_invariant(&out->fr_desc)));
	return result;
}

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
