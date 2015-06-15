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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 29-May-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/list.h"
#include "be/alloc.h"
#include "be/seg.h"
#include "be/tx_credit.h"
#include "be/tx.h"              /* M0_BE_TX_CAPTURE_PTR */
#include "be/op.h"              /* m0_be_op_active */

/**
 * @addtogroup be
 *
 * @{
 */

M0_INTERNAL void m0_be_list_credit(const struct m0_be_list *list,
				   enum m0_be_list_op       optype,
				   m0_bcount_t              nr,
				   struct m0_be_tx_credit  *accum)
{
	struct m0_be_tx_credit cred = {};

	switch (optype) {
	case M0_BLO_CREATE:
		M0_BE_ALLOC_CREDIT_PTR(list, list->bl_seg, &cred);
		break;
	case M0_BLO_DESTROY:
		M0_BE_FREE_CREDIT_PTR(list, list->bl_seg, &cred);
		break;
	case M0_BLO_INSERT:
	case M0_BLO_DELETE:
		/* list header */
		m0_be_tx_credit_add(&cred,
				    &M0_BE_TX_CREDIT_TYPE(struct m0_list));
		/* left list link */
		m0_be_tx_credit_add(&cred,
				    &M0_BE_TX_CREDIT_TYPE(struct m0_list_link));
		/* right list link */
		m0_be_tx_credit_add(&cred,
				    &M0_BE_TX_CREDIT_TYPE(struct m0_list_link));
		/* inserted element */
		/* XXX: what about links with an array of bytes the last
		 * element?? struct { char mem[0]; }. From the other hand,
		 * memory after mem[0] is captured by the user in allocator...*/

		/* Here we have to capture just a link.. Assume deletion
		 * scenario, in which user still has to capture ambient object
		 * after this m0_be_free()...
		 *  m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT(
		 *          1, list->bl_descr->td_container_size));
		 */
		m0_be_tx_credit_add(&cred,
				    &M0_BE_TX_CREDIT_TYPE(struct m0_list_link));
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

/* XXX TODO: m0_be_list_create() should return a pointer to the list. */
M0_INTERNAL void m0_be_list_create(struct m0_be_list       **list,
				   const struct m0_tl_descr *desc,
				   struct m0_be_seg         *seg,
				   struct m0_be_op          *op,
				   struct m0_be_tx          *tx)
{
	m0_be_op_active(op);

	M0_BE_ALLOC_PTR_SYNC(*list, seg, tx);
	if (*list == NULL)
		return;

	m0_be_list_init(*list, desc, seg);
	m0_tlist_init(desc, &(*list)->bl_list);
	M0_BE_TX_CAPTURE_PTR(seg, tx, *list);

	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list *list,
				    struct m0_be_op   *op,
				    struct m0_be_tx   *tx)
{
	struct m0_be_seg *seg   = list->bl_seg;

	m0_be_op_active(op);

	m0_be_list_fini(list);
	M0_BE_FREE_PTR_SYNC(list, seg, tx);
	M0_BE_TX_CAPTURE_PTR(seg, tx, list);

	m0_be_op_done(op);
}

/* XXX TODO: This function has to be reimplemented using m0_be_op */
static void *list_side(struct m0_be_list *list, struct m0_be_op *op,
		       void *(*side)(const struct m0_tl_descr *d,
				     const struct m0_tl *list))
{
	m0_be_op_active(op);
	m0_be_op_done(op);

	return side(list->bl_descr, &list->bl_list);
}

/* This function has to be reimplemented using m0_be_op */
static void *list_iter(struct m0_be_list *list, struct m0_be_op *op,
		       const void *obj,
		       void* (*iter)(const struct m0_tl_descr *d,
				     const struct m0_tl *list,
				     const void *obj))
{
	m0_be_op_active(op);
	m0_be_op_done(op);

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

/** Captures changed regions in the list. */
static void affected_capture(struct m0_be_list *list,
			     struct m0_be_tx   *tx,
			     void              *obj)
{
	struct m0_be_seg *seg = list->bl_seg;
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
	m0_be_op_active(op);
	add(list->bl_descr, &list->bl_list, obj);
	affected_capture(list, tx, obj);
	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_list_add(struct m0_be_list *list,
				struct m0_be_op   *op,
				struct m0_be_tx   *tx,
				void              *obj)
{
	be_list_add(list, op, tx, obj, m0_tlist_add);
}

M0_INTERNAL void m0_be_list_add_tail(struct m0_be_list *list,
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
						    void *obj, void *new))
{
	m0_be_op_active(op);
	add(list->bl_descr, obj, new);
	affected_capture(list, tx, obj);
	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_list_add_after(struct m0_be_list *list,
				      struct m0_be_op   *op,
				      struct m0_be_tx   *tx,
				      void              *obj,
				      void              *new)
{
	be_list_add_pos(list, op, tx, obj, new, m0_tlist_add_after);
}

M0_INTERNAL void m0_be_list_add_before(struct m0_be_list *list,
				       struct m0_be_op   *op,
				       struct m0_be_tx   *tx,
				       void              *obj,
				       void              *new)
{
	be_list_add_pos(list, op, tx, obj, new, m0_tlist_add_before);
}

M0_INTERNAL void m0_be_list_del(struct m0_be_list *list,
				struct m0_be_op   *op,
				struct m0_be_tx   *tx,
				void              *obj)
{
	struct m0_tlink *next;
	struct m0_tlink *prev;

	m0_be_op_active(op);

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

	m0_be_op_done(op);
}

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
