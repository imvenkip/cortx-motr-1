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

#include "lib/adt.h"           /* c2_buf */
#include "lib/arith.h"         /* C2_3WAY */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"          /* C2_SET0 */
#include "lib/cdefs.h"         /* C2_EXPORTED */
#include "rpc/rpc_opcodes.h"
#include "fol/fol.h"

/**
   @addtogroup fol

   <b>Implementation notes.</b>

   At the moment, fol is implemented as a c2_table. An alternative implemenation
   would re-use db5 transaction log to store fol records. The dis-advantage of
   the former approach is that records are duplicated between fol table and
   transaction log. The advantage is its simplicity (specifically, using db5 log
   for fol would require manual control over db5 log pruning policies).

   The fol table is (naturally) indexed by lsn. The record itself does not store
   the lsn (take a look at the comment before rec_init()) and has the following
   variable-sized format:

   @li struct c2_fol_rec_header
   @li followed by rh_obj_nr c2_fol_obj_ref-s
   @li followed by rh_sibling_nr c2_update_id-s
   @li followed by rh_data_len bytes

   When a record is fetched from the fol, it is "parsed" by rec_open(). When a
   record is placed into the fol, its representation is prepared by
   c2_fol_rec_pack().

  @{
 */
bool c2_lsn_is_valid(c2_lsn_t lsn)
{
	return lsn > C2_LSN_RESERVED_NR;
}

int c2_lsn_cmp(c2_lsn_t lsn0, c2_lsn_t lsn1)
{
	C2_PRE(c2_lsn_is_valid(lsn0));
	C2_PRE(c2_lsn_is_valid(lsn1));

	return C2_3WAY(lsn0, lsn1);
}

c2_lsn_t lsn_inc(c2_lsn_t lsn)
{
	++lsn;
	C2_ASSERT(lsn != 0);
	C2_POST(c2_lsn_is_valid(lsn));
	return lsn;
}

static int lsn_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const c2_lsn_t *lsn0 = key0;
	const c2_lsn_t *lsn1 = key1;

	return c2_lsn_cmp(*lsn0, *lsn1);
}

static const struct c2_table_ops fol_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(c2_lsn_t)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = lsn_cmp
};

/**
   Reserves "delta" bytes in a buffer starting at *buf and having *nob
   bytes. Advances *buf by delta and returns original buffer starting
   address. If position is advanced past the end of the buffer---returns NULL
   without modifying *buf.
 */
static void *buf_move(void **buf, uint32_t *nob, uint32_t delta)
{
	void *consumed;

	C2_PRE(C2_IS_8ALIGNED(buf)); /* buffer is aligned */

	if (*nob >= delta) {
		consumed = *buf;
		*buf += delta;
		*nob -= delta;
	} else
		consumed = NULL;
	return consumed;
}

/**
   Parses a record representation and fills in @d.
 */
static int rec_parse(struct c2_fol_rec_desc *d, void *buf, uint32_t nob,
                     bool open)
{
	struct c2_fol_rec_header     *h;
	const struct c2_fol_rec_type *rtype;

	h = buf_move(&buf, &nob, sizeof *h);
	if (h == NULL)
		return -EIO;
	d->rd_header = *h;
	d->rd_ref = buf_move(&buf, &nob, h->rh_obj_nr * sizeof d->rd_ref[0]);
	if (d->rd_ref == NULL)
		return -EIO;
	d->rd_sibling = buf_move(&buf, &nob,
				 h->rh_sibling_nr * sizeof d->rd_sibling[0]);
	if (d->rd_sibling == NULL)
		return -EIO;
	d->rd_data = buf_move(&buf, &nob, h->rh_data_len);
	if (d->rd_data == NULL)
		return -EIO;
	if (nob != 0)
		return -EIO;
	if (open) {
		rtype = d->rd_type = c2_fol_rec_type_lookup(h->rh_opcode);
		if (rtype == NULL)
			return -EIO;
		if (rtype->rt_ops->rto_open != NULL)
			return rtype->rt_ops->rto_open(rtype, d);
	}
	return 0;
}

/**
   Parses a record without checking invariants.
 */
static int rec_open_internal(struct c2_fol_rec *rec, bool open)
{
	struct c2_buf *recbuf;

	recbuf = &rec->fr_pair.dp_rec.db_buf;
	return rec_parse(&rec->fr_desc, recbuf->b_addr, recbuf->b_nob, open);
}

/**
   Parses a record representation.
 */
static int rec_open(struct c2_fol_rec *rec)
{
	int result;

	result = rec_open_internal(rec, true);
	if (result == 0)
		result = c2_fol_rec_invariant(&rec->fr_desc) ? 0 : -EIO;
	return result;
}

/**
   Initializes fields in @rec.

   Note, that key buffer in the cursor is initialized to point to
   rec->fr_desc.rd_lsn. As a result, the cursor's key follows lsn changes
   automagically.

   @see rec_fini()
 */
static int rec_init(struct c2_fol_rec *rec, struct c2_db_tx *tx)
{
	struct c2_db_pair *pair;

	C2_PRE(rec->fr_fol != NULL);

	pair = &rec->fr_pair;
	c2_db_pair_setup(pair, &rec->fr_fol->f_table, &rec->fr_desc.rd_lsn,
			 sizeof rec->fr_desc.rd_lsn, NULL, 0);
	return c2_db_cursor_init(&rec->fr_ptr, &rec->fr_fol->f_table, tx, 0);
}

/**
   Finalizes @rec.

   @see rec_init()
 */
void rec_fini(struct c2_fol_rec *rec)
{
	c2_db_cursor_fini(&rec->fr_ptr);
	c2_db_pair_fini(&rec->fr_pair);
}

static size_t anchor_pack_size(struct c2_fol_rec_desc *desc)
{
	return 0;
}

static void anchor_pack(struct c2_fol_rec_desc *desc, void *buf)
{
}

/**
   Operations vector for an "anchor" record type. A unique anchor record is
   inserted into every fol on initialisation.
 */
static const struct c2_fol_rec_type_ops anchor_ops = {
	.rto_commit     = NULL,
	.rto_abort      = NULL,
	.rto_persistent = NULL,
	.rto_cull       = NULL,
	.rto_open       = NULL,
	.rto_fini       = NULL,
	.rto_pack_size  = anchor_pack_size,
	.rto_pack       = anchor_pack
};

/**
   A type of a dummy log record inserted into every log on creation.
 */
static const struct c2_fol_rec_type anchor_type = {
	.rt_name   = "anchor",
	.rt_opcode = C2_FOL_ANCHOR_TYPE_OPCODE,
	.rt_ops    = &anchor_ops
};

int c2_fol_init(struct c2_fol *fol, struct c2_dbenv *env)
{
	int result;

	C2_CASSERT(C2_LSN_ANCHOR > C2_LSN_RESERVED_NR);

	c2_mutex_init(&fol->f_lock);
	result = c2_table_init(&fol->f_table, env, "fol", 0, &fol_ops);
	if (result == 0) {
		struct c2_fol_rec       r;
		struct c2_fol_rec_desc *d;
		struct c2_db_tx         tx;
		int                     rc;

		d = &r.fr_desc;
		result = c2_db_tx_init(&tx, env, 0);
		if (result == 0) {
			result = c2_fol_rec_lookup(fol, &tx, C2_LSN_ANCHOR, &r);
			if (result == -ENOENT) {
				/* initialise new fol */
				C2_SET0(d);
				d->rd_header.rh_refcount = 1;
				d->rd_header.rh_opcode = anchor_type.rt_opcode;
				d->rd_type = &anchor_type;
				d->rd_lsn = C2_LSN_ANCHOR;
				fol->f_lsn = C2_LSN_ANCHOR + 1;
				result = c2_fol_add(fol, &tx, d);
			} else if (result == 0) {
				result = c2_db_cursor_last(&r.fr_ptr,
							   &r.fr_pair);
				if (result == 0) {
					result = rec_open_internal(&r, false);
					fol->f_lsn = lsn_inc(d->rd_lsn);
				}
				c2_fol_rec_fini(&r);
			}
			rc = c2_db_tx_commit(&tx);
			result = result ?: rc;
		}
	}
	C2_POST(ergo(result == 0, c2_lsn_is_valid(fol->f_lsn)));
	return result;
}

void c2_fol_fini(struct c2_fol *fol)
{
	c2_table_fini(&fol->f_table);
}

c2_lsn_t c2_fol_lsn_allocate(struct c2_fol *fol)
{
	c2_lsn_t lsn;

	/*
	 * Obtain next fol lsn under the lock. Alternatively, c2_fol::f_lsn
	 * could be made into a c2_atomic64 instance.
	 */
	c2_mutex_lock(&fol->f_lock);
	lsn = fol->f_lsn;
	fol->f_lsn = lsn_inc(fol->f_lsn);
	c2_mutex_unlock(&fol->f_lock);
	C2_POST(c2_lsn_is_valid(lsn));
	return lsn;
}

int c2_fol_rec_pack(struct c2_fol_rec_desc *desc, struct c2_buf *out)
{
	const struct c2_fol_rec_type *rtype;
	struct c2_fol_rec_header     *h;
	void                         *buf;
	size_t                        size;
	int                           result;
	uint32_t                      data_len;

	rtype = desc->rd_type;
	data_len = desc->rd_header.rh_data_len =
		rtype->rt_ops->rto_pack_size(desc);
	size = sizeof *h +
		desc->rd_header.rh_obj_nr * sizeof desc->rd_ref[0] +
		desc->rd_header.rh_sibling_nr * sizeof desc->rd_sibling[0] +
		data_len;
	desc->rd_header.rh_opcode = rtype->rt_opcode;
	//C2_ASSERT((size & 7) == 0);

	buf = c2_alloc(size);
	if (buf != NULL) {
		h = out->b_addr = buf;
		out->b_nob = size;
		*h = desc->rd_header;
		rtype->rt_ops->rto_pack(desc, h + 1);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

int c2_fol_add(struct c2_fol *fol, struct c2_db_tx *tx,
	       struct c2_fol_rec_desc *rec)
{
	int           result;
	struct c2_buf buf;

	C2_PRE(c2_lsn_is_valid(rec->rd_lsn));

	result = c2_fol_rec_pack(rec, &buf);
	if (result == 0) {
		result = c2_fol_add_buf(fol, tx, rec, &buf);
		c2_free(buf.b_addr);
	}
	return result;
}

int c2_fol_add_buf(struct c2_fol *fol, struct c2_db_tx *tx,
		   struct c2_fol_rec_desc *drec, struct c2_buf *buf)
{
	struct c2_db_pair pair;

	C2_PRE(c2_lsn_is_valid(drec->rd_lsn));

	c2_db_pair_setup(&pair, &fol->f_table,
			 &drec->rd_lsn, sizeof drec->rd_lsn,
			 buf->b_addr, buf->b_nob);
	return c2_table_insert(tx, &pair);
}

int c2_fol_force(struct c2_fol *fol, c2_lsn_t upto)
{
	return c2_dbenv_sync(fol->f_table.t_env);
}

bool c2_fol_rec_invariant(const struct c2_fol_rec_desc *drec)
{
	uint32_t i;
	uint32_t j;

	if (!c2_lsn_is_valid(drec->rd_lsn))
		return false;
	for (i = 0; i < drec->rd_header.rh_obj_nr; ++i) {
		struct c2_fol_obj_ref *ref;

		ref = &drec->rd_ref[i];
		if (!c2_fid_is_valid(&ref->or_fid))
			return false;
		if (!c2_lsn_is_valid(ref->or_before_ver.vn_lsn) &&
		    ref->or_before_ver.vn_lsn != C2_LSN_NONE)
			return false;
		if (drec->rd_lsn <= ref->or_before_ver.vn_lsn)
			return false;
		for (j = 0; j < i; ++j) {
			if (c2_fid_eq(&ref->or_fid, &drec->rd_ref[j].or_fid))
				return false;
		}
	}
#if 0
	if (!c2_epoch_is_valid(&drec->rd_epoch))
		return false;
	if (!c2_update_is_valid(&drec->rd_header.rh_self))
		return false;
	for (i = 0; i < drec->rd_header.rh_sibling_nr; ++i) {
		struct c2_fol_update_ref *upd;

		upd = &drec->rd_sibling[i];
		if (!c2_update_is_valid(&upd->ui_id))
			return false;
		if (!c2_update_state_is_valid(upd->ui_state))
			return false;
		for (j = 0; j < i; ++j) {
			if (c2_update_is_eq(&upd->ui_id,
					    &drec->rd_sibling[j].ui_id))
				return false;
		}
	}
#endif
	return true;
}

int c2_fol_rec_lookup(struct c2_fol *fol, struct c2_db_tx *tx, c2_lsn_t lsn,
		      struct c2_fol_rec *out)
{
	int result;

	out->fr_fol = fol;
	result = rec_init(out, tx);
	if (result == 0) {
		out->fr_desc.rd_lsn = lsn;
		result = c2_db_cursor_get(&out->fr_ptr, &out->fr_pair);
		if (result == 0) {
			struct c2_fol_rec_header *h;

			h = out->fr_pair.dp_rec.db_buf.b_addr;
			if (out->fr_desc.rd_lsn == lsn && h->rh_refcount > 0)
				result = rec_open(out);
			else
				result = -ENOENT;
		}
		if (result != 0)
			rec_fini(out);
	}
	C2_POST(ergo(result == 0, out->fr_desc.rd_lsn == lsn));
	C2_POST(ergo(result == 0, out->fr_desc.rd_header.rh_refcount > 0));
	C2_POST(ergo(result == 0, c2_fol_rec_invariant(&out->fr_desc)));
	return result;
}

void c2_fol_rec_fini(struct c2_fol_rec *rec)
{
	const struct c2_fol_rec_type *rtype;

	rtype = rec->fr_desc.rd_type;
	if (rtype != NULL && rtype->rt_ops->rto_fini != NULL)
		rtype->rt_ops->rto_fini(&rec->fr_desc);
	rec_fini(rec);
}

/*
 * FOL record type code.
 *
 * An extremely simplistic implementation for now.
 */

enum {
	C2_FOL_REC_TYPE_MAX = 128
};

static const struct c2_fol_rec_type *rtypes[C2_FOL_REC_TYPE_MAX];
static struct c2_mutex rtypes_lock;

int c2_fols_init(void)
{
	c2_mutex_init(&rtypes_lock);
	return c2_fol_rec_type_register(&anchor_type);
}

void c2_fols_fini(void)
{
	c2_fol_rec_type_unregister(&anchor_type);
	c2_mutex_fini(&rtypes_lock);
}

int c2_fol_rec_type_register(const struct c2_fol_rec_type *rt)
{
	int result;

	c2_mutex_lock(&rtypes_lock);
	if (IS_IN_ARRAY(rt->rt_opcode, rtypes)) {
		if (rtypes[rt->rt_opcode] == NULL) {
			rtypes[rt->rt_opcode] = rt;
			result = 0;
		} else
			result = -EEXIST;
	} else
		result = -EFBIG;
	c2_mutex_unlock(&rtypes_lock);
	return result;
}

void c2_fol_rec_type_unregister(const struct c2_fol_rec_type *rt)
{
	c2_mutex_lock(&rtypes_lock);

	C2_PRE(IS_IN_ARRAY(rt->rt_opcode, rtypes));
	C2_PRE(rtypes[rt->rt_opcode] == rt || rtypes[rt->rt_opcode] == NULL);

	rtypes[rt->rt_opcode] = NULL;
	c2_mutex_unlock(&rtypes_lock);
}

const struct c2_fol_rec_type *c2_fol_rec_type_lookup(uint32_t opcode)
{
	C2_PRE(IS_IN_ARRAY(opcode, rtypes));
	return rtypes[opcode];
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
