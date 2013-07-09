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

#include "be/list.h"
#include "be/be.h"
#include "lib/memory.h"

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
