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
#include "be/op.h"      /* m0_be_op_state */
#include "be/tx.h"      /* m0_be_tx_state */

struct m0_be_btree;
struct m0_be_btree_kv_ops;
struct m0_be_btree_cursor;

M0_INTERNAL void m0_be_alloc(struct m0_be_allocator *a,
			     struct m0_be_tx *tx,
			     struct m0_be_op *op,
			     void **ptr,
			     m0_bcount_t size)
{
	void *p = m0_alloc(size);
	op->bo_u.u_allocator.a_ptr = p;
	if (ptr != NULL)
		*ptr = p;
}

M0_INTERNAL void m0_be_alloc_aligned(struct m0_be_allocator *a,
				     struct m0_be_tx *tx,
				     struct m0_be_op *op,
				     void **ptr,
				     m0_bcount_t size,
				     unsigned shift)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_allocator_credit(struct m0_be_allocator *a,
					enum m0_be_allocator_op optype,
					m0_bcount_t size,
					unsigned shift,
					struct m0_be_tx_credit *accum)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_free(struct m0_be_allocator *a,
			    struct m0_be_tx *tx,
			    struct m0_be_op *op,
			    void *ptr)
{
	m0_free(ptr);
}

M0_INTERNAL void m0_be_free_aligned(struct m0_be_allocator *a,
				    struct m0_be_tx *tx,
				    struct m0_be_op *op,
				    void *ptr)
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

M0_INTERNAL enum m0_be_op_state m0_be_op_state(const struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL int m0_be_op_wait(struct m0_be_op *op)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
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

M0_INTERNAL struct m0_be_allocator *m0_be_seg_allocator(struct m0_be_seg *seg)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return NULL;
}

M0_INTERNAL bool m0_be_seg_contains(const struct m0_be_seg *seg, void *addr)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return false;
}

M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg,
				      const char *name, void **out)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg,
				      struct m0_be_tx  *tx,
				      const char       *name,
				      void             *value)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg,
				      struct m0_be_tx  *tx,
				      const char       *name)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
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

M0_INTERNAL void
m0_be_tx_capture(struct m0_be_tx *tx, const struct m0_be_reg *reg)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_close(struct m0_be_tx *tx)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL int m0_be_tx_timedwait(struct m0_be_tx *tx, uint64_t states,
				   m0_time_t deadline)
{
	M0_IMPOSSIBLE("XXX Not implemented");
	return -1;
}

M0_INTERNAL void m0_be_tx_credit_add(struct m0_be_tx_credit *c0,
				     const struct m0_be_tx_credit *c1)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_credit_mul(struct m0_be_tx_credit *c, m0_bcount_t k)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}

M0_INTERNAL void m0_be_tx_credit_mac(struct m0_be_tx_credit *c,
				     const struct m0_be_tx_credit *c1,
				     m0_bcount_t k)
{
	M0_IMPOSSIBLE("XXX Not implemented");
}
