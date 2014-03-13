/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 25-Sep-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"
#include "lib/finject.h"

#include "conf/db.h"
#include "conf/onwire.h"     /* m0_confx_obj, m0_confx */
#include "conf/onwire_xc.h"
#include "conf/obj.h"        /* m0_conf_objtype */
#include "xcode/xcode.h"
#include "db/db.h"
#include "be/btree.h"
#include "be/tx.h"
#include "be/op.h"
#include "lib/memory.h"      /* m0_alloc, m0_free */
#include "lib/errno.h"       /* EINVAL */
#include "lib/misc.h"        /* M0_SET0 */

static int confdb_objs_count(struct m0_be_btree *btree, size_t *result);

/* ------------------------------------------------------------------
 * xcoding: m0_confx_obj <--> raw buffer
 * ------------------------------------------------------------------ */

static void *
_conf_xcode_alloc(struct m0_xcode_cursor *ctx M0_UNUSED, size_t nob)
{
	return m0_alloc(nob);
}

static void confx_to_xcode_obj(struct m0_confx_obj *xobj,
			       struct m0_xcode_obj *out, bool allocated)
{
	*out = M0_XCODE_OBJ(m0_confx_obj_xc, allocated ? xobj : NULL);
}

/* Note: m0_xcode_ctx_init() doesn't allow `xobj' to be const. Sigh. */
static void
xcode_ctx_init(struct m0_xcode_ctx *ctx, struct m0_confx_obj *xobj,
	       bool allocated)
{
	struct m0_xcode_obj obj;

	M0_ENTRY();

	confx_to_xcode_obj(xobj, &obj, allocated);
	m0_xcode_ctx_init(ctx, &obj);
	if (!allocated)
		ctx->xcx_alloc = _conf_xcode_alloc;

	M0_LEAVE();
}

static int confx_obj_measure(struct m0_confx_obj *xobj)
{
	struct m0_xcode_ctx ctx;

	M0_ENTRY();
	xcode_ctx_init(&ctx, xobj, true);
	M0_RETURN(m0_xcode_length(&ctx));
}


static m0_bcount_t confdb_ksize(const void *key)
{
	return sizeof(struct m0_fid);
}

static m0_bcount_t confdb_vsize(const void *val)
{
	return sizeof(struct m0_confx_obj);
}

static const struct m0_be_btree_kv_ops confdb_ops = {
	.ko_ksize   = &confdb_ksize,
	.ko_vsize   = &confdb_vsize,
	.ko_compare = (void *)&m0_fid_cmp
};


/* ------------------------------------------------------------------
 * Tables
 * ------------------------------------------------------------------ */

static const char btree_name[] = "conf";

static void confdb_table_fini(struct m0_be_seg *seg)
{
	int                 rc;
	struct m0_be_btree *btree;

	M0_ENTRY();
	rc = m0_be_seg_dict_lookup(seg, btree_name, (void **)&btree);
	if (rc == 0)
		m0_be_btree_fini(btree);
	M0_LEAVE();
}

static int confdb_table_init(struct m0_be_seg *seg, struct m0_be_btree **btree,
			     struct m0_be_tx *tx)
{
	int rc;

	M0_ENTRY();
	M0_BE_ALLOC_PTR_SYNC(*btree, seg, tx);
	m0_be_btree_init(*btree, seg, &confdb_ops);
	M0_BE_OP_SYNC(op, m0_be_btree_create(*btree, tx, &op));
	rc = m0_be_seg_dict_insert(seg, tx, btree_name, *btree);
	if (rc == 0 && M0_FI_ENABLED("ut_confdb_create_failure"))
		rc = -EINVAL;
	if (rc != 0) {
		confdb_table_fini(seg);
		m0_confdb_destroy(seg, tx);
	}

	M0_RETURN(rc);
}

/* ------------------------------------------------------------------
 * Database operations
 * ------------------------------------------------------------------ */
static int confx_obj_dup(struct m0_confx_obj **dest, struct m0_confx_obj *src,
			 struct m0_be_seg *seg, struct m0_be_tx *tx)
{
	struct m0_xcode_obj src_obj;
	struct m0_xcode_obj dest_obj;
	int                 rc = 0;

	confx_to_xcode_obj(src, &dest_obj, false);
	confx_to_xcode_obj(src, &src_obj, true);
	if (M0_FI_ENABLED("ut_confx_obj_dup_failure"))
		rc = -EINVAL;
	if (rc == 0)
		rc = m0_xcode_be_dup(&dest_obj, &src_obj, seg, tx);
	*dest = dest_obj.xo_ptr;
	return rc;
}

M0_INTERNAL int m0_confdb_create_credit(struct m0_be_seg *seg,
					const struct m0_confx *conf,
					struct m0_be_tx_credit *accum)
{
	struct m0_be_btree btree = { .bb_seg = seg };
	int                rc = 0;
	int                i;

	M0_ENTRY();
	M0_BE_ALLOC_CREDIT_PTR(&btree, seg, accum);
	m0_be_seg_dict_insert_credit(seg, btree_name, accum);
	m0_be_btree_create_credit(&btree, 1, accum);

	for (i = 0; i < conf->cx_nr; ++i) {
		struct m0_confx_obj *obj;

		obj = &conf->cx_objs[i];
		rc = confx_obj_measure(obj);
		if (rc < 0)
			break;
		m0_be_btree_insert_credit(&btree, 1, sizeof(struct m0_fid),
					  rc, accum);
		rc = 0;
	}

	M0_RETURN(rc);
}

M0_INTERNAL int m0_confdb_destroy_credit(struct m0_be_seg *seg,
					 struct m0_be_tx_credit *accum)
{
	struct m0_be_btree *btree;
	int                 rc;

	M0_ENTRY();
	rc = m0_be_seg_dict_lookup(seg, btree_name, (void **)&btree);
	if (rc == 0) {
		m0_be_btree_destroy_credit(btree, 1, accum);
		M0_BE_FREE_CREDIT_PTR(btree, seg, accum);
	}
	M0_RETURN(rc);
}

M0_INTERNAL int m0_confdb_destroy(struct m0_be_seg *seg, struct m0_be_tx *tx)
{
	struct m0_be_btree *btree;
	int                 rc;

	M0_ENTRY();

	/*
	 * FIXME: Does not free the internal be objects allocated during
	 *        confdb_create as part of xcode_dup operation.
	 */

	rc = m0_be_seg_dict_lookup(seg, btree_name, (void **)&btree);
	if (rc == 0) {
		M0_BE_OP_SYNC(op, m0_be_btree_destroy(btree, tx, &op));
		M0_BE_FREE_PTR_SYNC(btree, seg, tx);
		rc = m0_be_seg_dict_delete(seg, tx, btree_name);
	}
	M0_RETURN(rc);
}

M0_INTERNAL void m0_confdb_fini(struct m0_be_seg *seg)
{
	confdb_table_fini(seg);
}

M0_INTERNAL int m0_confdb_create(struct m0_be_seg *seg, struct m0_be_tx *tx,
				 const struct m0_confx *conf)
{
	struct m0_be_btree *btree;
	int                 i;
	int                 rc;

	M0_ENTRY();
	M0_PRE(conf->cx_nr > 0);

	rc = confdb_table_init(seg, &btree, tx);
	if (rc != 0)
		return rc;
	for (i = 0; i < conf->cx_nr && rc == 0; ++i) {
		struct m0_confx_obj *obj;
		struct m0_buf        key;
		struct m0_buf        val;

		rc = confx_obj_dup(&obj, &conf->cx_objs[i], seg, tx);
		if (rc != 0)
			break;
		M0_ASSERT(obj != NULL);
		/* discard const */
		key = M0_FID_BUF((struct m0_fid *)m0_conf_objx_fid(obj));
		val = M0_BUF_INIT(sizeof *obj, obj);
		rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_insert(btree, tx,
							      &op, &key, &val),
				       bo_u.u_btree.t_rc);
	}
	if (rc !=0) {
		confdb_table_fini(seg);
		m0_confdb_destroy(seg, tx);
	}
	M0_RETURN(rc);
}

static int confdb_objs_count(struct m0_be_btree *btree, size_t *result)
{
	struct m0_be_btree_cursor bcur;
	int                       rc;

	M0_ENTRY();
	*result = 0;
	m0_be_btree_cursor_init(&bcur, btree);
	for (rc = m0_be_btree_cursor_first_sync(&bcur); rc == 0;
	     rc = m0_be_btree_cursor_next_sync(&bcur)) {
		++*result;
	}
	m0_be_btree_cursor_fini(&bcur);
	/* Check for normal iteration completion. */
	if (rc == -ENOENT)
		rc = 0;
	M0_RETURN(rc);
}

static struct m0_confx *confx_alloc(size_t nr_objs)
{
	struct m0_confx *ret;

	M0_PRE(nr_objs > 0);

	M0_ALLOC_PTR(ret);
	if (ret == NULL)
		return NULL;

	M0_ALLOC_ARR(ret->cx_objs, nr_objs);
	if (ret->cx_objs == NULL) {
		m0_free(ret);
		return NULL;
	}

	ret->cx_nr = nr_objs;
	return ret;
}

static void confx_fill(struct m0_confx *dest, struct m0_be_btree *btree)
{
	struct m0_be_btree_cursor bcur;
	size_t                    i; /* index in dest->cx_objs[] */
	int                       rc;

	M0_ENTRY();
	M0_PRE(dest->cx_nr > 0);

	m0_be_btree_cursor_init(&bcur, btree);
	for (i = 0, rc = m0_be_btree_cursor_first_sync(&bcur); rc == 0;
	     rc = m0_be_btree_cursor_next_sync(&bcur), ++i) {
		struct m0_buf key;
		struct m0_buf val;

		m0_be_btree_cursor_kv_get(&bcur, &key, &val);
		M0_ASSERT(i < dest->cx_nr);
		/**
		 * @todo check validity of key and record addresses and
		 * sizes. Specifically, check that val.b_addr points to an
		 * allocated region in a segment with appropriate size and
		 * alignment. Such checks should be done generally by (not
		 * existing) beobj interface.
		 *
		 * @todo also check that key (fid) matches m0_conf_objx_fid().
		 */
		dest->cx_objs[i] = *(struct m0_confx_obj *)val.b_addr;
	}
	m0_be_btree_cursor_fini(&bcur);
	/** @todo handle iteration errors. */
	M0_ASSERT(rc == -ENOENT); /* end of the table */
}

M0_INTERNAL int m0_confdb_read(struct m0_be_seg *seg, struct m0_confx **out)
{
	struct m0_be_btree *btree;
	int                 rc;
	size_t              nr_objs = 0;

	M0_ENTRY();

	rc = m0_be_seg_dict_lookup(seg, btree_name, (void **)&btree);
	if (rc != 0)
		return rc;

	rc = confdb_objs_count(btree, &nr_objs);
	if (rc != 0)
		goto out;

	if (nr_objs == 0) {
		rc = -ENODATA;
		goto out;
	}

	*out = confx_alloc(nr_objs);
	if (*out == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	confx_fill(*out, btree);
out:
	M0_RETURN(rc);
}

#undef M0_TRACE_SUBSYSTEM
