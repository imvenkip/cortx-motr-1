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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 13-Sep-2013
 */

#include "lib/memory.h" /* m0_alloc() */
#include "be/op.h"  /* m0_be_op_state */
#include "be/tx.h"  /* m0_be_tx_state */

struct m0_be_btree;
struct m0_be_btree_kv_ops;
struct m0_be_btree_cursor;

M0_INTERNAL void *m0_be_alloc(struct m0_be_allocator *a,
			      struct m0_be_tx *tx, struct m0_be_op *op,
			      m0_bcount_t size, unsigned shift)
{
	return m0_alloc(size);
}

M0_INTERNAL void m0_be_free(struct m0_be_allocator *a,
			    struct m0_be_tx *tx, struct m0_be_op *op,
			    void *ptr)
{
	m0_free(ptr);
}

M0_INTERNAL void m0_be_btree_init(struct m0_be_btree *tree,
				  struct m0_be_seg *seg,
				  const struct m0_be_btree_kv_ops *ops)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_fini(struct m0_be_btree *tree)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL bool m0_be_btree_is_empty(struct m0_be_btree *tree)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return true;
}

M0_INTERNAL void m0_be_btree_create_credit(const struct m0_be_btree *tree,
					   m0_bcount_t nr,
					   struct m0_be_tx_credit *accum)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_create(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_destroy_credit(struct m0_be_btree *tree,
					    m0_bcount_t nr,
					    struct m0_be_tx_credit *accum)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_destroy(struct m0_be_btree *tree,
				     struct m0_be_tx *tx,
				     struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_insert_credit(const struct m0_be_btree *tree,
					   m0_bcount_t nr,
					   m0_bcount_t ksize,
					   m0_bcount_t vsize,
					   struct m0_be_tx_credit *accum)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_insert(struct m0_be_btree *tree,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    const struct m0_buf *key,
				    const struct m0_buf *value)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}


M0_INTERNAL void m0_be_btree_cursor_init(struct m0_be_btree_cursor *cursor,
					 struct m0_be_btree *tree)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_cursor_fini(struct m0_be_btree_cursor *cursor)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL int m0_be_btree_cursor_get_sync(struct m0_be_btree_cursor *cur,
					    const struct m0_buf *key,
					    bool slant)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL int m0_be_btree_cursor_last_sync(struct m0_be_btree_cursor *cur)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL void m0_be_btree_cursor_kv_get(struct m0_be_btree_cursor *cur,
					   struct m0_buf *key,
					   struct m0_buf *val)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_btree_cursor_put(struct m0_be_btree_cursor *cursor)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_op_init(struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_op_fini(struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL int m0_be_op_wait(struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL enum m0_be_tx_state m0_be_tx_state(const struct m0_be_tx *tx)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL void
m0_be_op_state_set(struct m0_be_op *op, enum m0_be_op_state state)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg,
				      const char *name, void **out)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return 0;
}

M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg,
				      struct m0_sm_group *grp,
				      const char *name, void *value)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg,
				      struct m0_sm_group *grp,
				      const char *name)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_init(struct m0_be_tx     *tx,
			       uint64_t             tid,
			       struct m0_be_domain *dom,
			       struct m0_sm_group  *sm_group,
			       m0_be_tx_cb_t        persistent,
			       m0_be_tx_cb_t        discarded,
			       void               (*filler)(struct m0_be_tx *tx,
							    void *payload),
			       void                *datum)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_fini(struct m0_be_tx *tx)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_prep(struct m0_be_tx *tx,
			       const struct m0_be_tx_credit *credit)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_open(struct m0_be_tx *tx)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_close(struct m0_be_tx *tx)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL int m0_be_tx_timedwait(struct m0_be_tx *tx, int states,
				   m0_time_t timeout)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}
