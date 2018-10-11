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
 *                  Maxim Medved <max.medved@seagate.com>
 * Original creation date: 29-May-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/list.h"

#include "be/tx_credit.h"       /* m0_be_tx_credit */
#include "be/tx.h"              /* M0_BE_TX_CAPTURE_PTR */
#include "be/reg.h"             /* M0_BE_REG_GET_PTR */

#include "lib/string.h"         /* strlen */

/**
 * @addtogroup be
 *
 * @{
 */

enum {
	BE_LIST_POISON_BYTE = 0xCC,
};

static bool be_list_invariant(struct m0_be_list             *blist,
			      const struct m0_be_list_descr *descr,
			      struct m0_be_seg              *seg,
			      struct m0_be_tx               *tx)
{
	bool result;
	M0_BE_REG_GET_PTR(blist, seg, tx);
	result = _0C(equi(blist->bl_head.blh_head == (void *)&blist->bl_head,
	                  blist->bl_head.blh_tail == (void *)&blist->bl_head));
	M0_BE_REG_PUT_PTR(blist, seg, tx);
	return result;
}

static bool be_tlink_invariant(struct m0_be_list_link        *link,
			       const struct m0_be_list_descr *descr,
			       struct m0_be_tx               *tx)
{
	bool result;
	M0_BE_REG_GET_PTR(link, NULL, tx);
	result = _0C(equi(link->bll_next == link, link->bll_prev == link));
	M0_BE_REG_PUT_PTR(link, NULL, tx);
	return result;
}

M0_INTERNAL void m0_be_list_credit(enum m0_be_list_op      optype,
				   m0_bcount_t             nr,
				   struct m0_be_tx_credit *accum)
{
	struct m0_be_tx_credit cred = {};
	struct m0_be_tx_credit cred_tlink;
	struct m0_be_tx_credit cred_tlink_magic;
	struct m0_be_tx_credit cred_list;

	cred_tlink       = M0_BE_TX_CREDIT_TYPE(struct m0_be_list_link);
	cred_tlink_magic = M0_BE_TX_CREDIT_TYPE(uint64_t);
	cred_list        = M0_BE_TX_CREDIT_TYPE(struct m0_be_list);

	switch (optype) {
	case M0_BLO_CREATE:
	case M0_BLO_DESTROY:
		m0_be_tx_credit_add(&cred, &cred_list);
		break;
	case M0_BLO_ADD:
	case M0_BLO_DEL:
		/* list header */
		m0_be_tx_credit_add(&cred, &cred_list);
		/* left list link */
		m0_be_tx_credit_add(&cred, &cred_tlink);
		/* right list link */
		m0_be_tx_credit_add(&cred, &cred_tlink);
		/* added/deleted element */
		m0_be_tx_credit_add(&cred, &cred_tlink);
		break;
	case M0_BLO_TLINK_CREATE:
	case M0_BLO_TLINK_DESTROY:
		m0_be_tx_credit_add(&cred, &cred_tlink);
		m0_be_tx_credit_add(&cred, &cred_tlink_magic);
		break;
	default:
		M0_IMPOSSIBLE("");
	};

	m0_be_tx_credit_mul(&cred, nr);
	m0_be_tx_credit_add(accum, &cred);
}

/* -------------------------------------------------------------------------
 * Construction/Destruction:
 * ------------------------------------------------------------------------- */

static void be_list_capture(struct m0_be_list *blist,
			    struct m0_be_tx   *tx)
{
	M0_BE_REG_GET_PTR(blist, NULL, tx);
	M0_BE_TX_CAPTURE_PTR(NULL, tx, blist);
	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

static void be_list_head_capture(struct m0_be_list *blist,
				 struct m0_be_tx   *tx)
{
	M0_BE_TX_CAPTURE_PTR(NULL, tx, &blist->bl_head);
}

static void be_tlink_capture(struct m0_be_list_link *link,
			     struct m0_be_tx        *tx)
{
	M0_BE_TX_CAPTURE_PTR(NULL, tx, link);
}

static struct m0_be_list_link *
be_list_obj2link(const void                    *obj,
		 const struct m0_be_list_descr *descr)
{
	M0_PRE(!m0_addu64_will_overflow((uint64_t)obj,
					(uint64_t)descr->bld_link_offset));

	return (struct m0_be_list_link *)((char *)obj + descr->bld_link_offset);
}

static void *be_list_link2obj(struct m0_be_list_link        *link,
			      const struct m0_be_list_descr *descr)
{
	M0_PRE((uint64_t)link - (uint64_t)descr->bld_link_offset <
		(uint64_t)link);

	return (char *)link - descr->bld_link_offset;
}

M0_INTERNAL void m0_be_list_create(struct m0_be_list             *blist,
				   const struct m0_be_list_descr *descr,
				   struct m0_be_tx               *tx)
{
	M0_BE_REG_GET_PTR(blist, NULL, tx);

	m0_format_header_pack(&blist->bl_format_header, &(struct m0_format_tag){
		.ot_version = M0_BE_LIST_FORMAT_VERSION,
		.ot_type    = M0_FORMAT_TYPE_BE_LIST,
		.ot_footer_offset = offsetof(struct m0_be_list, bl_format_footer)
	});

	blist->bl_magic = descr->bld_head_magic;
	blist->bl_head.blh_head = (struct m0_be_list_link *)&blist->bl_head;
	blist->bl_head.blh_tail = (struct m0_be_list_link *)&blist->bl_head;

	m0_format_footer_update(blist);

	be_list_capture(blist, tx);
	M0_POST(be_list_invariant(blist, descr, NULL, tx));

	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list             *blist,
				    const struct m0_be_list_descr *descr,
				    struct m0_be_tx               *tx)
{
	M0_BE_REG_GET_PTR(blist, NULL, tx);

	M0_PRE(be_list_invariant(blist, descr, NULL, tx));
	M0_PRE(m0_be_list_is_empty(blist, descr, tx, NULL));

	memset(blist, BE_LIST_POISON_BYTE, sizeof *blist);
	be_list_capture(blist, tx);

	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

M0_INTERNAL bool m0_be_list_is_empty(struct m0_be_list             *blist,
				     const struct m0_be_list_descr *descr,
				     struct m0_be_tx               *tx,
				     struct m0_be_seg              *seg)
{
	bool result;

	/* XXX get */

	result = blist->bl_head.blh_head == (void *)&blist->bl_head;

	/* XXX put */

	return result;
}

M0_INTERNAL void m0_be_tlink_create(void                          *obj,
				    const struct m0_be_list_descr *descr,
				    struct m0_be_tx               *tx)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);

	M0_BE_REG_GET_PTR(link, NULL, tx);

	link->bll_next = link;
	link->bll_prev = link;
	be_tlink_capture(link, tx);

	M0_BE_REG_PUT_PTR(link, NULL, tx);
}

M0_INTERNAL void m0_be_tlink_destroy(void                          *obj,
				     const struct m0_be_list_descr *descr,
				     struct m0_be_tx               *tx)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);

	M0_BE_REG_GET_PTR(link, NULL, tx);

	memset(link, BE_LIST_POISON_BYTE, sizeof *link);
	be_tlink_capture(link, tx);

	M0_BE_REG_PUT_PTR(link, NULL, tx);
}

/* -------------------------------------------------------------------------
 * Iteration interfaces:
 * ------------------------------------------------------------------------- */

static bool be_tlink_is_tail(struct m0_be_list      *blist,
			     struct m0_be_list_link *link)
{
	return link->bll_next == (void *)&blist->bl_head;
}

static bool be_tlink_is_head(struct m0_be_list      *blist,
			     struct m0_be_list_link *link)
{
	return link->bll_prev == (void *)&blist->bl_head;
}

static bool be_tlink_is_in_list(struct m0_be_list_link *link,
                                struct m0_be_tx        *tx)
{
	bool result;
	M0_BE_REG_GET_PTR(link, NULL, tx);
	result = link->bll_prev != link;
	M0_BE_REG_PUT_PTR(link, NULL, tx);
	return result;
}

M0_INTERNAL void *m0_be_list_tail(struct m0_be_list             *blist,
				  const struct m0_be_list_descr *descr,
				  struct m0_be_tx               *tx,
				  struct m0_be_seg              *seg)
{
	void *result;

	/* XXX get */

	// XXX M0_PRE(be_list_invariant(blist, descr));

	if (m0_be_list_is_empty(blist, descr, tx, NULL))
		result = NULL;
	else {
		/*
		 * We don't have to get reference to the blh_tail in current
		 * implementation, because the pointer is not dereferenced.
		 */
		result = be_list_link2obj(blist->bl_head.blh_tail, descr);
	}

	/* XXX put */

	return result;
}

M0_INTERNAL void *m0_be_list_head(struct m0_be_list             *blist,
				  const struct m0_be_list_descr *descr,
				  struct m0_be_tx               *tx,
				  struct m0_be_seg              *seg)
{
	void *result;

	/* XXX get */

	// XXX M0_PRE(be_list_invariant(blist, descr));

	if (m0_be_list_is_empty(blist, descr, tx, NULL))
		result = NULL;
	else
		result = be_list_link2obj(blist->bl_head.blh_head, descr);

	/* XXX put */

	return result;
}

M0_INTERNAL void *m0_be_list_prev(struct m0_be_list             *blist,
				  const struct m0_be_list_descr *descr,
				  struct m0_be_tx               *tx,
				  struct m0_be_seg              *seg,
				  const void                    *obj)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);
	void                   *result;

	/* XXX get */
	/* XXX get link */

	// XXX M0_PRE(be_list_invariant(blist, descr));
	if (be_tlink_is_head(blist, link))
		result = NULL;
	else
		result = be_list_link2obj(link->bll_prev, descr);

	/* XXX put link */
	/* XXX put */

	return result;
}

M0_INTERNAL void *m0_be_list_next(struct m0_be_list             *blist,
				  const struct m0_be_list_descr *descr,
				  struct m0_be_tx               *tx,
				  struct m0_be_seg              *seg,
				  const void                    *obj)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);
	void                   *result;

	/* XXX get */
	/* XXX get link */

	// XXX M0_PRE(be_list_invariant(blist, descr));
	if (be_tlink_is_tail(blist, link))
		result = NULL;
	else
		result = be_list_link2obj(link->bll_next, descr);

	/* XXX put link */
	/* XXX put */

	return result;
}

/* -------------------------------------------------------------------------
 * Modification interfaces:
 * ------------------------------------------------------------------------- */

M0_INTERNAL void m0_be_list_add(struct m0_be_list             *blist,
				const struct m0_be_list_descr *descr,
				struct m0_be_tx               *tx,
				void                          *obj)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);
	struct m0_be_list_link *tmp;

	M0_BE_REG_GET_PTR(blist, NULL, tx);
	M0_BE_REG_GET_PTR(link, NULL, tx);

	M0_PRE(be_list_invariant(blist, descr, NULL, tx));
	M0_PRE(be_tlink_invariant(link, descr, tx));
	M0_PRE(!be_tlink_is_in_list(link, tx));

	if (m0_be_list_is_empty(blist, descr, tx, NULL)) {
		blist->bl_head.blh_tail = link;
	} else {
		tmp = blist->bl_head.blh_head;
		M0_BE_REG_GET_PTR(tmp, NULL, tx);
		tmp->bll_prev = link;
		be_tlink_capture(tmp, tx);
		M0_BE_REG_PUT_PTR(tmp, NULL, tx);
	}
	link->bll_prev = (struct m0_be_list_link *)&blist->bl_head;
	link->bll_next = blist->bl_head.blh_head;
	blist->bl_head.blh_head = link;

	be_list_head_capture(blist, tx);
	be_tlink_capture(link, tx);

	M0_POST(be_list_invariant(blist, descr, NULL, tx));

	M0_BE_REG_PUT_PTR(link, NULL, tx);
	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

M0_INTERNAL void m0_be_list_add_after(struct m0_be_list             *blist,
				      const struct m0_be_list_descr *descr,
				      struct m0_be_tx               *tx,
				      void                          *obj,
				      void                          *obj_new)
{
	struct m0_be_list_link *link = be_list_obj2link(obj_new, descr);
	struct m0_be_list_link *link_after = be_list_obj2link(obj, descr);
	struct m0_be_list_link *tmp;

	M0_BE_REG_GET_PTR(blist, NULL, tx);
	M0_BE_REG_GET_PTR(link, NULL, tx);
	M0_BE_REG_GET_PTR(link_after, NULL, tx);

	M0_PRE(be_list_invariant(blist, descr, NULL, tx));
	M0_PRE(be_tlink_invariant(link, descr, tx));
	M0_PRE(be_tlink_invariant(link_after, descr, tx));
	M0_PRE(!be_tlink_is_in_list(link, tx));
	M0_PRE(be_tlink_is_in_list(link_after, tx));

	if (be_tlink_is_tail(blist, link_after)) {
		blist->bl_head.blh_tail = link;
		be_list_head_capture(blist, tx);
	} else {
		tmp = link_after->bll_next;
		M0_BE_REG_GET_PTR(tmp, NULL, tx);
		tmp->bll_prev = link;
		be_tlink_capture(tmp, tx);
		M0_BE_REG_PUT_PTR(tmp, NULL, tx);
	}
	link->bll_prev = link_after;
	link->bll_next = link_after->bll_next;
	link_after->bll_next = link;

	be_tlink_capture(link, tx);
	be_tlink_capture(link_after, tx);

	M0_POST(be_list_invariant(blist, descr, NULL, tx));

	M0_BE_REG_PUT_PTR(link_after, NULL, tx);
	M0_BE_REG_PUT_PTR(link, NULL, tx);
	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

M0_INTERNAL void m0_be_list_add_before(struct m0_be_list             *blist,
				       const struct m0_be_list_descr *descr,
				       struct m0_be_tx               *tx,
				       void                          *obj,
				       void                          *obj_new)
{
	struct m0_be_list_link *link = be_list_obj2link(obj_new, descr);
	struct m0_be_list_link *link_before = be_list_obj2link(obj, descr);
	struct m0_be_list_link *tmp;

	M0_BE_REG_GET_PTR(blist, NULL, tx);
	M0_BE_REG_GET_PTR(link, NULL, tx);
	M0_BE_REG_GET_PTR(link_before, NULL, tx);

	M0_PRE(be_list_invariant(blist, descr, NULL, tx));
	M0_PRE(be_tlink_invariant(link, descr, tx));
	M0_PRE(be_tlink_invariant(link_before, descr, tx));
	M0_PRE(!be_tlink_is_in_list(link, tx));
	M0_PRE(be_tlink_is_in_list(link_before, tx));

	if (be_tlink_is_head(blist, link_before)) {
		blist->bl_head.blh_head = link;
		be_list_head_capture(blist, tx);
	} else {
		tmp = link_before->bll_prev;
		M0_BE_REG_GET_PTR(tmp, NULL, tx);
		tmp->bll_next = link;
		be_tlink_capture(tmp, tx);
		M0_BE_REG_PUT_PTR(tmp, NULL, tx);
	}
	link->bll_prev = link_before->bll_prev;
	link->bll_next = link_before;
	link_before->bll_prev = link;

	be_tlink_capture(link, tx);
	be_tlink_capture(link_before, tx);

	M0_POST(be_list_invariant(blist, descr, NULL, tx));

	M0_BE_REG_PUT_PTR(link_before, NULL, tx);
	M0_BE_REG_PUT_PTR(link, NULL, tx);
	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

M0_INTERNAL void m0_be_list_add_tail(struct m0_be_list             *blist,
				     const struct m0_be_list_descr *descr,
				     struct m0_be_tx               *tx,
				     void                          *obj)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);
	struct m0_be_list_link *tmp;

	M0_BE_REG_GET_PTR(blist, NULL, tx);
	M0_BE_REG_GET_PTR(link, NULL, tx);

	M0_PRE(be_list_invariant(blist, descr, NULL, tx));
	M0_PRE(be_tlink_invariant(link, descr, tx));
	M0_PRE(!be_tlink_is_in_list(link, tx));

	if (m0_be_list_is_empty(blist, descr, tx, NULL)) {
		blist->bl_head.blh_head = link;
	} else {
		tmp = blist->bl_head.blh_tail;
		M0_BE_REG_GET_PTR(tmp, NULL, tx);
		tmp->bll_next = link;
		be_tlink_capture(tmp, tx);
		M0_BE_REG_PUT_PTR(tmp, NULL, tx);
	}
	link->bll_prev = blist->bl_head.blh_tail;
	link->bll_next = (struct m0_be_list_link *)&blist->bl_head;
	blist->bl_head.blh_tail = link;

	be_list_head_capture(blist, tx);
	be_tlink_capture(link, tx);

	M0_POST(be_list_invariant(blist, descr, NULL, tx));

	M0_BE_REG_PUT_PTR(link, NULL, tx);
	M0_BE_REG_PUT_PTR(blist, NULL, tx);
}

M0_INTERNAL void m0_be_list_del(struct m0_be_list             *blist,
				const struct m0_be_list_descr *descr,
				struct m0_be_tx               *tx,
				void                          *obj)
{
	struct m0_be_list_link *link = be_list_obj2link(obj, descr);
	struct m0_be_list_link *tmp;
	bool                    head_is_dirty = false;


	M0_BE_REG_GET_PTR(blist, NULL, tx);
	M0_BE_REG_GET_PTR(link, NULL, tx);

	M0_PRE(be_list_invariant(blist, descr, NULL, tx));
	M0_PRE(be_tlink_invariant(link, descr, tx));
	M0_PRE(be_tlink_is_in_list(link, tx));

	if (be_tlink_is_head(blist, link)) {
		blist->bl_head.blh_head = link->bll_next;
		head_is_dirty = true;
	} else {
		tmp = link->bll_prev;
		M0_BE_REG_GET_PTR(tmp, NULL, tx);
		tmp->bll_next = link->bll_next;
		be_tlink_capture(tmp, tx);
		M0_BE_REG_PUT_PTR(tmp, NULL, tx);
	}

	if (be_tlink_is_tail(blist, link)) {
		blist->bl_head.blh_tail = link->bll_prev;
		head_is_dirty = true;
	} else {
		tmp = link->bll_next;
		M0_BE_REG_GET_PTR(tmp, NULL, tx);
		tmp->bll_prev = link->bll_prev;
		be_tlink_capture(tmp, tx);
		M0_BE_REG_PUT_PTR(tmp, NULL, tx);
	}

	if (head_is_dirty)
		be_list_head_capture(blist, tx);

	link->bll_prev = link;
	link->bll_next = link;
	be_tlink_capture(link, tx);

	M0_POST(be_list_invariant(blist, descr, NULL, tx));

	M0_BE_REG_PUT_PTR(link, NULL, tx);
	M0_BE_REG_PUT_PTR(blist, NULL, tx);
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
