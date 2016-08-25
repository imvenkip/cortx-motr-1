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
#include "be/op.h"              /* m0_be_op_active */

#include "lib/string.h"         /* strlen */

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
	struct m0_be_tx_credit cred_tlink;
	struct m0_be_tx_credit cred_tlink_magic;
	struct m0_be_tx_credit cred_list;

	cred_tlink       = M0_BE_TX_CREDIT_TYPE(struct m0_tlink);
	cred_tlink_magic = M0_BE_TX_CREDIT_TYPE(uint64_t);
	cred_list        = M0_BE_TX_CREDIT_PTR(list);

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
	case M0_BLO_MOVE:
	default:
		M0_IMPOSSIBLE("");
	};

	m0_be_tx_credit_mul(&cred, nr);
	m0_be_tx_credit_add(accum, &cred);
}


M0_INTERNAL void m0_be_list_init(struct m0_be_list *list,
				 struct m0_be_seg  *seg)
{
	list->bl_seg = seg;
}

M0_INTERNAL void m0_be_list_fini(struct m0_be_list *list)
{
}

static void be_tlink_capture(struct m0_tlink   *tlink,
			     struct m0_be_list *list,
			     struct m0_be_tx   *tx)
{
	if (tlink != NULL)
		M0_BE_TX_CAPTURE_PTR(list->bl_seg, tx, tlink);
}

static void be_tlink_capture_magic(struct m0_tlink   *tlink,
				   struct m0_be_list *list,
				   struct m0_be_tx   *tx)
{
	uint64_t *magic;

	if (tlink != NULL) {
		magic = (void *) tlink - list->bl_descr.td_link_offset +
					 list->bl_descr.td_link_magic_offset;
		M0_BE_TX_CAPTURE_PTR(list->bl_seg, tx, magic);
	}
}

static void be_list_capture(struct m0_be_list *list,
			    struct m0_be_tx   *tx)
{
	M0_BE_TX_CAPTURE_PTR(list->bl_seg, tx, list);
}

static void be_list_capture3(struct m0_be_list *list,
			     struct m0_be_tx   *tx,
			     struct m0_tlink   *tlink1,
			     struct m0_tlink   *tlink2,
			     struct m0_tlink   *tlink3)
{
	be_list_capture(list, tx);
	be_tlink_capture(tlink1, list, tx);
	be_tlink_capture(tlink2, list, tx);
	be_tlink_capture(tlink3, list, tx);
}

M0_INTERNAL void m0_be_list_create(struct m0_be_list        *list,
				   struct m0_be_tx          *tx,
				   struct m0_be_op          *op,
				   struct m0_be_seg         *seg,
				   const struct m0_tl_descr *desc)
{
	M0_PRE(strlen(desc->td_name) < sizeof list->bl_td_name);

	m0_be_op_active(op);

	m0_format_header_pack(&list->bl_format_header, &(struct m0_format_tag){
		.ot_version = M0_BE_LIST_FORMAT_VERSION,
		.ot_type    = M0_FORMAT_TYPE_BE_LIST,
		.ot_footer_offset = offsetof(struct m0_be_list, bl_format_footer)
	});

	list->bl_descr = *desc;
	list->bl_seg   = seg;
	strncpy(list->bl_td_name, desc->td_name, sizeof list->bl_td_name);
	list->bl_td_name[sizeof list->bl_td_name - 1] = '\0';
	list->bl_descr.td_name = list->bl_td_name;
	m0_tlist_init(&list->bl_descr, &list->bl_list);
	m0_format_footer_update(list);
	be_list_capture(list, tx);

	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list *list,
				    struct m0_be_tx   *tx,
				    struct m0_be_op   *op)
{
	m0_be_op_active(op);

	m0_tlist_fini(&list->bl_descr, &list->bl_list);
	be_list_capture(list, tx);

	m0_be_op_done(op);
}

M0_INTERNAL bool m0_be_list_is_empty(struct m0_be_list *list)
{
	return m0_tlist_is_empty(&list->bl_descr, &list->bl_list);
}

static void *be_list_side(struct m0_be_list   *list,
			  struct m0_be_op     *op,
			  void              *(*side)
						(const struct m0_tl_descr *d,
						 const struct m0_tl     *list))
{
	m0_be_op_active(op);
	m0_be_op_done(op);

	return side(&list->bl_descr, &list->bl_list);
}

static void *be_list_iter(struct m0_be_list   *list,
			  struct m0_be_op     *op,
			  const void          *obj,
			  void              *(*iter)
						(const struct m0_tl_descr *d,
						 const struct m0_tl      *list,
						 const void              *obj))
{
	m0_be_op_active(op);
	m0_be_op_done(op);

	return iter(&list->bl_descr, &list->bl_list, obj);
}

M0_INTERNAL void *m0_be_list_head(struct m0_be_list *list,
				  struct m0_be_op   *op)
{
	return be_list_side(list, op, m0_tlist_head);
}

M0_INTERNAL void *m0_be_list_tail(struct m0_be_list *list,
				  struct m0_be_op   *op)
{
	return be_list_side(list, op, m0_tlist_tail);
}

M0_INTERNAL void *m0_be_list_next(struct m0_be_list *list,
				  struct m0_be_op   *op,
				  const void        *obj)
{
	return be_list_iter(list, op, obj, m0_tlist_next);
}

M0_INTERNAL void *m0_be_list_prev(struct m0_be_list *list,
				  struct m0_be_op   *op,
				  const void        *obj)
{
	return be_list_iter(list, op, obj, m0_tlist_prev);
}

static struct m0_tlink *be_tlink_from_obj(void                    *obj,
					  const struct m0_be_list *list)
{
	return (struct m0_tlink *)
	       (obj == NULL ? NULL : obj + list->bl_descr.td_link_offset);
}

static void be_list_neighbourhood(struct m0_be_list  *list,
				  void               *obj,
				  struct m0_tlink   **prev,
				  struct m0_tlink   **curr,
				  struct m0_tlink   **next)
{
	const struct m0_tl_descr *d = &list->bl_descr;

	*curr = be_tlink_from_obj(obj, list);
	*next = be_tlink_from_obj(m0_tlist_next(d, &list->bl_list, obj), list);
	*prev = be_tlink_from_obj(m0_tlist_prev(d, &list->bl_list, obj), list);
}

/** Captures changed regions in the list. */
static void be_list_affected_capture(struct m0_be_list *list,
				     struct m0_be_tx   *tx,
				     void              *obj)
{
	struct m0_tlink *curr;
	struct m0_tlink *next;
	struct m0_tlink *prev;

	be_list_neighbourhood(list, obj, &prev, &curr, &next);
	be_list_capture3(list, tx, prev, curr, next);
}

static void be_list_add(struct m0_be_list *list,
			struct m0_be_op   *op,
			struct m0_be_tx   *tx,
			void              *obj,
			void             (*add)(const struct m0_tl_descr *d,
						struct m0_tl             *list,
						void                     *obj))
{
	m0_be_op_active(op);
	add(&list->bl_descr, &list->bl_list, obj);
	be_list_affected_capture(list, tx, obj);
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
			    void              *obj_new,
			    void             (*add)(const struct m0_tl_descr *d,
						    void                   *obj,
						    void              *obj_new))
{
	m0_be_op_active(op);
	add(&list->bl_descr, obj, obj_new);
	be_list_affected_capture(list, tx, obj_new);
	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_list_add_after(struct m0_be_list *list,
				      struct m0_be_op   *op,
				      struct m0_be_tx   *tx,
				      void              *obj,
				      void              *obj_new)
{
	be_list_add_pos(list, op, tx, obj, obj_new, m0_tlist_add_after);
}

M0_INTERNAL void m0_be_list_add_before(struct m0_be_list *list,
				       struct m0_be_op   *op,
				       struct m0_be_tx   *tx,
				       void              *obj,
				       void              *obj_new)
{
	be_list_add_pos(list, op, tx, obj, obj_new, m0_tlist_add_before);
}

M0_INTERNAL void m0_be_list_del(struct m0_be_list *list,
				struct m0_be_op   *op,
				struct m0_be_tx   *tx,
				void              *obj)
{
	struct m0_tlink *prev;
	struct m0_tlink *curr;
	struct m0_tlink *next;

	m0_be_op_active(op);

	/* delete() is a special case for capturing, because while deletion
	   link is finished(), so pointer to the left and right are overwritten.
	   Code has to save it beforehand and update these values */
	be_list_neighbourhood(list, obj, &prev, &curr, &next);
	m0_tlist_del(&list->bl_descr, obj);
	be_list_capture3(list, tx, prev, curr, next);

	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_tlink_init(void *obj, struct m0_be_list *list)
{
}

M0_INTERNAL void m0_be_tlink_fini(void *obj, struct m0_be_list *list)
{
}

static void be_tlink_create_destroy(void              *obj,
				    struct m0_be_tx   *tx,
				    struct m0_be_op   *op,
				    struct m0_be_list *list,
				    bool create)
{
	struct m0_tlink *tlink = be_tlink_from_obj(obj, list);

	m0_be_op_active(op);

	if (create)
		m0_tlink_init(&list->bl_descr, obj);
	else
		m0_tlink_fini(&list->bl_descr, obj);

	be_tlink_capture(tlink, list, tx);
	be_tlink_capture_magic(tlink, list, tx);

	m0_be_op_done(op);
}

M0_INTERNAL void m0_be_tlink_create(void              *obj,
				    struct m0_be_tx   *tx,
				    struct m0_be_op   *op,
				    struct m0_be_list *list)
{
	be_tlink_create_destroy(obj, tx, op, list, true);
}

M0_INTERNAL void m0_be_tlink_destroy(void              *obj,
				     struct m0_be_tx   *tx,
				     struct m0_be_op   *op,
				     struct m0_be_list *list)
{
	be_tlink_create_destroy(obj, tx, op, list, false);
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
