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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 29-May-2013
 */

/**
 * @addtogroup be
 *
 * @{
 */
#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"
#include "lib/memory.h"
#include "be/alloc.h"
#include "be/list.h"
#include "be/seg.h"
#include "be/be.h"
#include "be/tx.h"

enum {
	ALLOC_SHIFT = 0,
};

M0_INTERNAL void m0_be_list_credit(const struct m0_be_list *list,
				   enum m0_be_list_op       optype,
				   m0_bcount_t		    nr,
				   struct m0_be_tx_credit  *accum)
{
	struct m0_be_allocator *a = &list->bl_seg->bs_allocator;
	struct m0_be_tx_credit  cred;

	m0_be_tx_credit_init(&cred);

	switch(optype) {
	case M0_BLO_CREATE:
	case M0_BLO_DESTROY:
		m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof(*list),
				       ALLOC_SHIFT, &cred);
		break;
	case M0_BLO_INSERT:
	case M0_BLO_DELETE:
		/* list header */
		m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT
				    (1, sizeof(struct m0_list)));
		/* left list link */
		m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT
				    (1, sizeof(struct m0_list_link)));
		/* right list link */
		m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT
				    (1, sizeof(struct m0_list_link)));
		/* inserted element */
		/* XXX: what about links with an array of bytes the last
		 * element?? struct { char mem[0]; }. From the other hand,
		 * memory after mem[0] is captured by the user in allocator...*/

		/* Here we have to capture just a link.. Assume deletion
		 * scenario, in which user still has to capture ambient object
		 * after this m0_be_free()...
		 *  m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT
		 * 		       (1, list->bl_descr->td_container_size));
		 */
		m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT
				    (1, sizeof(struct m0_list_link)));
		break;
	case M0_BLO_MOVE:
	default:
		M0_IMPOSSIBLE("");
	};

	m0_be_tx_credit_mul(&cred, nr);
	m0_be_tx_credit_add(accum, &cred);
}


M0_INTERNAL void m0_be_list_init(struct m0_be_list        *list,
				 const struct m0_tl_descr *desc,
				 struct m0_be_seg         *seg)
{
	list->bl_descr = desc;
	list->bl_seg   = seg;

	/* XXX: uncomment this in future for get (), put() */
	/* list->bl_list.t_unsafe = true; */
}

M0_INTERNAL void m0_be_list_fini(struct m0_be_list *list)
{
	m0_tlist_fini(list->bl_descr, &list->bl_list);
}

M0_INTERNAL void m0_be_list_create(struct m0_be_list        **list,
				   const struct m0_tl_descr  *desc,
				   struct m0_be_seg          *seg,
				   struct m0_be_op           *op,
				   struct m0_be_tx           *tx)
{
	struct m0_be_allocator *alloc = &seg->bs_allocator;

	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	*list = m0_be_alloc(alloc, tx, op, sizeof(**list), ALLOC_SHIFT);
	if (*list == NULL)
		return;

	m0_be_list_init(*list, desc, seg);
	m0_tlist_init(desc, &(*list)->bl_list);
	M0_BE_TX_CAPTURE_PTR(seg, tx, *list);
}

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list *list,
				    struct m0_be_op   *op,
				    struct m0_be_tx   *tx)
{
	struct m0_be_seg       *seg   = list->bl_seg;
	struct m0_be_allocator *alloc = &seg->bs_allocator;

	m0_be_list_fini(list);
	m0_be_free(alloc, tx, op, list);
	M0_BE_TX_CAPTURE_PTR(seg, tx, list);
}

/* This function has to be reimplemented using m0_be_op */
static void *list_side(struct m0_be_list *list, struct m0_be_op *op,
		       void* (*side)(const struct m0_tl_descr *d,
				       const struct m0_tl *list))
{
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);

	return side(list->bl_descr, &list->bl_list);
}

/* This function has to be reimplemented using m0_be_op */
static void *list_iter(struct m0_be_list *list, struct m0_be_op *op,
		       const void *obj,
		       void* (*iter)(const struct m0_tl_descr *d,
				     const struct m0_tl *list,
				     const void *obj))
{
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);

	return iter(list->bl_descr, &list->bl_list, obj);
}

M0_INTERNAL void *m0_be_list_head(struct m0_be_list *list, struct m0_be_op *op)
{
	return list_side(list, op, m0_tlist_head);
}

M0_INTERNAL void *m0_be_list_tail(struct m0_be_list *list, struct m0_be_op *op)
{
	return list_side(list, op, m0_tlist_tail);
}

M0_INTERNAL void *m0_be_list_next(struct m0_be_list *list, struct m0_be_op *op,
				  const void *obj)
{
	return list_iter(list, op, obj, m0_tlist_next);
}

M0_INTERNAL void *m0_be_list_prev(struct m0_be_list *list, struct m0_be_op *op,
				  const void *obj)
{
	return list_iter(list, op, obj, m0_tlist_prev);
}

static void neighborhood(struct m0_be_list *list, void *obj,
			 struct m0_tlink **prev,
			 struct m0_tlink **curr,
			 struct m0_tlink **next)
{
	const struct m0_tl_descr *d = list->bl_descr;
	void *onext = m0_tlist_next(d, &list->bl_list, obj);
	void *oprev = m0_tlist_prev(d, &list->bl_list, obj);

	if (curr != NULL)
		*curr = (struct m0_tlink *)(obj + d->td_link_offset);
	if (next != NULL)
		*next = (struct m0_tlink *)(onext == NULL ? NULL :
					    onext + d->td_link_offset);
	if (prev != NULL)
		*prev = (struct m0_tlink *)(oprev == NULL ? NULL :
					    oprev + d->td_link_offset);
}

/* captures changed regions in the list */
static void affected_capture(struct m0_be_list *list,
			     struct m0_be_tx   *tx,
			     void              *obj)
{
	struct m0_be_seg *seg   = list->bl_seg;
	struct m0_tlink  *curr;
	struct m0_tlink  *next;
	struct m0_tlink  *prev;

	neighborhood(list, obj, &prev, &curr, &next);

	M0_BE_TX_CAPTURE_PTR(seg, tx, &list->bl_list.t_head);
	M0_BE_TX_CAPTURE_PTR(seg, tx, &curr->t_link);
	if (next != NULL)
		M0_BE_TX_CAPTURE_PTR(seg, tx, &next->t_link);
	if (prev != NULL)
		M0_BE_TX_CAPTURE_PTR(seg, tx, &prev->t_link);
}

static void be_list_add(struct m0_be_list *list,
			struct m0_be_op   *op,
			struct m0_be_tx   *tx,
			void              *obj,
			void             (*add)(const struct m0_tl_descr *d,
						struct m0_tl *list, void *obj))
{
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	add(list->bl_descr, &list->bl_list, obj);
	affected_capture(list, tx, obj);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void          m0_be_list_add(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj)
{
	be_list_add(list, op, tx, obj, m0_tlist_add);
}

M0_INTERNAL void     m0_be_list_add_tail(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj)
{
	be_list_add(list, op, tx, obj, m0_tlist_add_tail);
}

static void be_list_add_pos(struct m0_be_list *list,
			    struct m0_be_op   *op,
			    struct m0_be_tx   *tx,
			    void              *obj,
			    void              *new,
			    void             (*add)(const struct m0_tl_descr *d,
						    void *obj,
						    void *new))
{
	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);

	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	add(list->bl_descr, obj, new);
	affected_capture(list, tx, obj);
	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}

M0_INTERNAL void    m0_be_list_add_after(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj,
					 void              *new)
{
	be_list_add_pos(list, op, tx, obj, new, m0_tlist_add_after);
}

M0_INTERNAL void   m0_be_list_add_before(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj,
					 void              *new)
{
	be_list_add_pos(list, op, tx, obj, new, m0_tlist_add_before);
}

M0_INTERNAL void          m0_be_list_del(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj)
{
	struct m0_tlink *next;
	struct m0_tlink *prev;

	M0_PRE(m0_be_op_state(op) == M0_BOS_INIT);
	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	/* delete() is a special case for capturing, because while deletion
	   link is finished(), so pointer to the left and right are overwritten.
	   Code has to save it beforehand and update these values */
	neighborhood(list, obj, &prev, NULL, &next);
	m0_tlist_del(list->bl_descr, obj);
	affected_capture(list, tx, obj);

	if (next != NULL)
		M0_BE_TX_CAPTURE_PTR(list->bl_seg, tx, &next->t_link);
	if (prev != NULL)
		M0_BE_TX_CAPTURE_PTR(list->bl_seg, tx, &prev->t_link);

	m0_be_op_state_set(op, M0_BOS_SUCCESS);
}


#undef M0_TRACE_SUBSYSTEM

/* -------------------------------------------------------------------------
 * XXX: old
 * ------------------------------------------------------------------------- */
#if 0 /* old be_list */
M0_INTERNAL void
m0_be_list_create(const struct m0_tl_descr *d,
		  struct m0_be_list *list,
		  struct m0_be_tx *tx)
{
	M0_PRE(list != NULL && d != NULL && tx != NULL);
	M0_PRE(m0_be__reg_is_pinned(&M0_BE_REG(list->bl_seg, sizeof *list,
					       list)));
	m0_tlist_init(d, &list->bl_list);
	list->bl_list.t_unsafe = true;  /* BE tlist is usually unsafe. */
	m0_be_tx_capture(tx, &M0_BE_REG(list->bl_seg, sizeof *list, list));
}

M0_INTERNAL void
m0_be_list_destroy(struct m0_be_list *list, struct m0_be_tx *tx)
{
	M0_PRE(list != NULL && list->bl_descr != NULL && tx != NULL);
	M0_PRE(m0_be__reg_is_pinned(&M0_BE_REG(list->bl_seg, sizeof *list,
					       list)));
	m0_tlist_fini(list->bl_descr, &list->bl_list);
	m0_be_tx_capture(tx, &M0_BE_REG(list->bl_seg, sizeof *list, list));
}

/**
 * Pins the head and first `nelems' of the list, loading them from disk
 * storage if necessary.
 */
M0_INTERNAL void m0_be_list_get(const struct m0_be_list *list,
				m0_bcount_t nelems,
				struct m0_be_op *op)
{
	struct m0_be_op      lreg_op;
	void                *lnode;

	M0_PRE(list != NULL && op != NULL);
	op->bo_utype                = M0_BOP_LIST;
	op->bo_u.u_list.l_list      = list;
	op->bo_u.u_list.l_nelems    = nelems;
	m0_sm_state_set(&op->bo_sm, M0_BOS_ACTIVE);

	/* Prepare sub-operation for m0_be_reg_get(). */
	lreg_op.bo_utype            = M0_BOP_REG;
	lreg_op.bo_fom              = NULL;
	lreg_op.bo_parent_op        = op;
	lreg_op.bo_u.u_reg.br_seg   = list->bl_seg;
	lreg_op.bo_u.u_reg.br_size  = list->bl_descr->td_container_size;
	m0_sm_init(&lreg_op.bo_sm, op->bo_sm.sm_conf, M0_BOS_INIT,
		   op->bo_sm.sm_grp);

	m0_tlist_for(list->bl_descr, &list->bl_list, lnode) {
		if (--nelems == 0)
			break;
		op->bo_u.u_list.l_lnode     = lnode;
		op->bo_u.u_list.l_nelems    = nelems + 1;
		lreg_op.bo_u.u_reg.br_addr  = lnode;
		m0_be_reg_get(&lreg_op.bo_u.u_reg, &lreg_op);
		/* XXX: for async completion we must must add clink/callback
		 * to lreg_op->bo_sm.sm_chan. */
		M0_ASSERT(m0_be_op_state(&lreg_op) == M0_BOS_SUCCESS);
	} m0_tlist_endfor;
	m0_sm_state_set(&op->bo_sm, M0_BOS_SUCCESS);
}

#undef  BE_LIST_PUT_REG
#define BE_LIST_PUT_REG(node, reg)                                     \
	if (node != NULL) {                                            \
		(reg).br_addr = (node);                                \
		/* Check list node's reg before doing mem access. */   \
		M0_ASSERT(m0_be__reg_is_pinned(&(reg)));               \
		m0_be_reg_put(&(reg));                                 \
	}
M0_INTERNAL void m0_be_list_put(const struct m0_be_list *list,
				m0_bcount_t nelems)
{
	void                *lnode;
	void                *prvnode = NULL;
	struct m0_be_reg     l_reg;

	M0_PRE(list != NULL);

	l_reg = M0_BE_REG(list->bl_seg, list->bl_descr->td_container_size,
			  NULL);

	m0_tlist_for(list->bl_descr, &list->bl_list, lnode) {
		if (--nelems == 0) break;
		/* Free previous node. */
		BE_LIST_PUT_REG(prvnode, l_reg);
		prvnode = lnode;
	} m0_tlist_endfor;

	/* Put last region. */
	BE_LIST_PUT_REG(prvnode, l_reg);
}
#undef  BE_LIST_PUT_REG

M0_INTERNAL void m0_be_list_credit(const struct m0_be_list *list,
				   enum m0_be_list_op optype,
				   m0_bcount_t nr,
				   struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit k;
	/*Number of pointers affected. */
	static const m0_bcount_t npointers[M0_BLO_NR] = {
		[M0_BLO_INSERT] = 4, /* l->ll_next/h->l_head,
				      * self->{ll_prev,ll_nextright},
				      * r->ll_prev/h->l_tail */
		[M0_BLO_DELETE] = 4, /* the same */
		[M0_BLO_MOVE]   = 6, /* the same plus new neighbours'
				      * ll_next/l_head and ll_prev/l_tail. */
	};

	M0_PRE(list != NULL && list->bl_descr != NULL);
	M0_PRE(IS_IN_ARRAY(optype, npointers));

	k.tc_reg_nr   = npointers[optype];
	k.tc_reg_size = k.tc_reg_nr * sizeof(void *);
	m0_be_tx_credit_add(accum, &k);
}

M0_INTERNAL void m0_be_list_capture(const struct m0_be_list *list,
				    struct m0_be_tx *tx,
				    const struct m0_tlink *origin,
				    m0_bcount_t left,
				    m0_bcount_t right)
{
	struct m0_be_reg          reg;
	void                     *lnode0;
	void                     *lnode;
	const struct m0_tl_descr *d;
	const struct m0_tl       *l;

	M0_PRE(list != NULL && origin != NULL && list->bl_descr != NULL
	       && list->bl_seg != NULL);

	d = list->bl_descr;
	l = &list->bl_list;
	reg.br_seg = list->bl_seg;
	reg.br_size = d->td_container_size;
	lnode0 = (void*)origin - d->td_link_offset;

	/* Capture originating list node. */
	reg.br_addr = lnode0;
	m0_be_tx_capture(tx, &reg);
	/* Capture list nodes to the left. */
	for (lnode = lnode0; left != 0 &&
	     (lnode = m0_tlist_prev(d, l, lnode)) != NULL; left--) {
		reg.br_addr = lnode;
		m0_be_tx_capture(tx, &reg);
	}
	/* Capture list nodes to the right. */
	for (lnode = lnode0; right != 0 &&
	     (lnode = m0_tlist_next(d, l, lnode)) != NULL; right--) {
		reg.br_addr = lnode;
		m0_be_tx_capture(tx, &reg);
	}
	/* Capture list head if left or right search terminates at it. */
	if (left || right) {
		reg.br_addr = (void *)list;
		reg.br_size = sizeof(*list);
		m0_be_tx_capture(tx, &reg);
	}
}

/*
 * Neither m0_be_list_init() nor m0_be_list_fini() will ever do anything.
 * We keep the interfaces for consistency.
 *
 * NOTE: The "bigger" structure may contain fields (e.g., mutexes) that get
 * filled with garbage when the structure is loaded from disk storage.
 * Initialiser of this "bigger" structure should do proper initialisation
 * of such fields.
 */
M0_INTERNAL void m0_be_list_init(struct m0_be_list *list,
				 struct m0_be_seg *seg)
{
	M0_PRE(list != NULL && seg != NULL);
	list->bl_seg = seg;
}
M0_INTERNAL void m0_be_list_fini(struct m0_be_list *list) {}
#endif /* old be_list */

/** @} end of be group */

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
