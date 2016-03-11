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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 24-Aug-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/seg_dict.h"

#include "lib/memory.h"       /* M0_ALLOC_PTR */
#include "lib/errno.h"        /* ENOMEM */

#include "be/seg.h"
#include "be/seg_internal.h"  /* m0_be_seg_hdr */
#include "be/tx.h"            /* m0_be_tx_init */

/**
 * @addtogroup be
 *
 * @{
 */

#define BUF_INIT_STR(str) M0_BUF_INIT(strlen(str)+1, (str))

/**
 * Invalid credit structure used to forcibly fail a transaction.
 *
 * This is declared here rather than in credit.c so that this symbol exists in
 * the kernel build.
 */
const struct m0_be_tx_credit m0_be_tx_credit_invalid =
	M0_BE_TX_CREDIT(M0_BCOUNT_MAX, M0_BCOUNT_MAX);

static m0_bcount_t dict_ksize(const void *key)
{
	return strlen(key)+1;
}

static m0_bcount_t dict_vsize(const void *data)
{
	return sizeof(void*);
}

static int dict_cmp(const void *key0, const void *key1)
{
	return strcmp(key0, key1);
}

static const struct m0_be_btree_kv_ops dict_ops = {
	.ko_ksize   = dict_ksize,
	.ko_vsize   = dict_vsize,
	.ko_compare = dict_cmp
};

static inline struct m0_be_btree *dict_get(const struct m0_be_seg *seg)
{
	return &((struct m0_be_seg_hdr *) seg->bs_addr)->bs_dict;
}

static inline const struct m0_be_btree *
dict_get_const(const struct m0_be_seg *seg)
{
	return &((const struct m0_be_seg_hdr *) seg->bs_addr)->bs_dict;
}

/* -------------------------------------------------------------------
 * Credits
 */

M0_INTERNAL void m0_be_seg_dict_insert_credit(const struct m0_be_seg *seg,
					      const char             *name,
					      struct m0_be_tx_credit *accum)
{
	const struct m0_be_btree *tree = dict_get_const(seg);
	M0_PRE(m0_be_seg__invariant(seg));

	m0_be_btree_insert_credit(tree, 1, dict_ksize(name), dict_vsize(NULL),
				  accum);
}

M0_INTERNAL void m0_be_seg_dict_delete_credit(const struct m0_be_seg *seg,
					      const char             *name,
					      struct m0_be_tx_credit *accum)
{
	const struct m0_be_btree *tree = dict_get_const(seg);

	M0_ENTRY("seg: %p", seg);
	M0_PRE(m0_be_seg__invariant(seg));
	m0_be_btree_delete_credit(tree, 1, dict_ksize(name), dict_vsize(NULL),
				  accum);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_seg_dict_create_credit(const struct m0_be_seg *seg,
					      struct m0_be_tx_credit *accum)
{
	const struct m0_be_btree *tree = dict_get_const(seg);

	M0_ENTRY("seg: %p", seg);
	M0_PRE(m0_be_seg__invariant(seg));
	m0_be_btree_create_credit(tree, 1, accum);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_seg_dict_destroy_credit(const struct m0_be_seg *seg,
					       struct m0_be_tx_credit *accum)
{
	struct m0_be_btree *tree = dict_get(seg);

	M0_PRE(m0_be_seg__invariant(seg));
	m0_be_btree_destroy_credit(tree, accum);
}

/* -------------------------------------------------------------------
 * Operations
 */

M0_INTERNAL void m0_be_seg_dict_init(struct m0_be_seg *seg)
{
	struct m0_be_btree *tree = dict_get(seg);

	M0_ENTRY("seg=%p", seg);
	M0_PRE(m0_be_seg__invariant(seg));

	m0_be_btree_init(tree, seg, &dict_ops);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_seg_dict_fini(struct m0_be_seg *seg)
{
	struct m0_be_btree *tree = dict_get(seg);

	M0_ENTRY("seg=%p", seg);
	M0_PRE(m0_be_seg__invariant(seg));

	m0_be_btree_fini(tree);

	M0_LEAVE();
}

M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg,
				      const char *name,
				      void **out)
{
	struct m0_be_btree *tree = dict_get(seg);
	struct m0_buf       key  = BUF_INIT_STR((char*)name);
	struct m0_buf       val  = M0_BUF_INIT(dict_vsize(*out), out);

	M0_ENTRY("seg=%p name='%s'", seg, name);
	return M0_RC(M0_BE_OP_SYNC_RET(op,
				    m0_be_btree_lookup(tree, &op, &key, &val),
				    bo_u.u_btree.t_rc));
}

M0_INTERNAL int _seg_dict_iterate(struct m0_be_seg *seg,
				  const char *prefix,
				  const char *start_key,
				  const char **this_key,
				  void **this_rec,
				  bool next)
{
	struct m0_be_btree_cursor  cursor;
	struct m0_be_btree        *tree = dict_get(seg);
	struct m0_buf		   start = M0_BUF_INITS((char*)start_key);
	struct m0_buf		   key;
	struct m0_buf              val;
	int                        rc;

	m0_be_btree_cursor_init(&cursor, tree);

	rc = m0_be_btree_cursor_get_sync(&cursor, &start, !next) ?:
		(!next ? 0 : m0_be_btree_cursor_next_sync(&cursor));
	if (rc == 0) {
		m0_be_btree_cursor_kv_get(&cursor, &key, &val);
		*this_key = (const char*)key.b_addr;
		*this_rec = (void *) *(uint64_t *)val.b_addr; /* XXX */

		rc = 0;
		if (strstr(*this_key, "M0_BE:") == NULL)
			rc = -ENOENT;
		if (/* XXX: next &&  */strstr(*this_key, prefix) == NULL)
			rc = -ENOENT;

		M0_LOG(M0_DEBUG, "rc=%d, this_key='%s', prefix='%s'",
			 rc, *this_key, prefix);
	}

	m0_be_btree_cursor_fini(&cursor);

	return M0_RC(rc);
}

M0_INTERNAL int m0_be_seg_dict_begin(struct m0_be_seg *seg,
				     const char *start_key,
				     const char **this_key,
				     void **this_rec)
{
	return _seg_dict_iterate(seg, start_key, start_key,
				 this_key, this_rec, false);
}

M0_INTERNAL int m0_be_seg_dict_next(struct m0_be_seg *seg,
				    const char *prefix,
				    const char *start_key,
				    const char **this_key,
				    void **this_rec)
{
	return _seg_dict_iterate(seg, prefix, start_key,
				 this_key, this_rec, true);
}

M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg,
				      struct m0_be_tx  *tx,
				      const char       *name,
				      void             *value)
{
	struct m0_be_btree *tree = dict_get(seg);
	struct m0_buf       key  = BUF_INIT_STR((char*)name);
	struct m0_buf       val  = M0_BUF_INIT(dict_vsize(value), &value);
	int                 rc;

	M0_ENTRY("seg=%p name='%s'", seg, name);
	M0_PRE(m0_be_seg__invariant(seg));

	rc = M0_BE_OP_SYNC_RET(
		op, m0_be_btree_insert(tree, tx, &op, &key, &val),
		bo_u.u_btree.t_rc);

	M0_POST(m0_be_seg__invariant(seg));
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg,
				      struct m0_be_tx  *tx,
				      const char       *name)
{
	struct m0_be_btree *tree = dict_get(seg);
	struct m0_buf       key  = BUF_INIT_STR((char*)name);
	int                 rc;

	M0_ENTRY("seg=%p", seg);
	M0_PRE(m0_be_seg__invariant(seg));

	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_delete(tree, tx, &op, &key),
			       bo_u.u_btree.t_rc);

	M0_POST(m0_be_seg__invariant(seg));
	return M0_RC(rc);
}

M0_INTERNAL void m0_be_seg_dict_create(struct m0_be_seg *seg,
				       struct m0_be_tx  *tx)
{
	struct m0_be_btree *tree = dict_get(seg);

	M0_ENTRY("seg=%p", seg);
	M0_PRE(m0_be_seg__invariant(seg));

	m0_be_btree_init(tree, seg, &dict_ops);
	M0_BE_OP_SYNC(op, m0_be_btree_create(tree, tx, &op));
	M0_LEAVE();
}

M0_INTERNAL void m0_be_seg_dict_destroy(struct m0_be_seg *seg,
					struct m0_be_tx  *tx)
{
	struct m0_be_btree *tree = dict_get(seg);

	M0_ENTRY("seg=%p", seg);
	M0_PRE(m0_be_seg__invariant(seg));

	M0_BE_OP_SYNC(op, m0_be_btree_destroy(tree, tx, &op));
	M0_LEAVE();
}

#undef BUF_INIT_STR

/** @} end of be group */
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
