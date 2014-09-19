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

#pragma once
#ifndef __MERO_BE_LIST_H__
#define __MERO_BE_LIST_H__

#include "lib/tlist.h"

/* import */
struct m0_be_op;
struct m0_be_seg;
struct m0_be_tx;
struct m0_be_tx_credit;

/**
 * @defgroup be Meta-data back-end
 *
 * @{
 */

struct m0_be_list {
	const struct m0_tl_descr *bl_descr;
	struct m0_tl              bl_list;
	struct m0_be_seg         *bl_seg;
};

/** List operations that modify memory. */
enum m0_be_list_op {
	M0_BLO_CREATE,
	M0_BLO_DESTROY,
	M0_BLO_INSERT,
	M0_BLO_DELETE,
	M0_BLO_MOVE,
	M0_BLO_NR
};

/**
 * Calculates the credit needed to perform @nr list operations of type
 * @optype and adds this credit to @accum.
 */
M0_INTERNAL void m0_be_list_credit(const struct m0_be_list *list,
				   enum m0_be_list_op       optype,
				   m0_bcount_t		    nr,
				   struct m0_be_tx_credit  *accum);

/* -------------------------------------------------------------------------
 * Construction/Destruction:
 * ------------------------------------------------------------------------- */
M0_INTERNAL void m0_be_list_init(struct m0_be_list        *list,
				 const struct m0_tl_descr *desc,
				 struct m0_be_seg         *seg);

M0_INTERNAL void m0_be_list_fini(struct m0_be_list    *list);

M0_INTERNAL void m0_be_list_create(struct m0_be_list        **list,
				   const struct m0_tl_descr  *desc,
				   struct m0_be_seg          *seg,
				   struct m0_be_op           *op,
				   struct m0_be_tx           *tx);

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list *list,
				    struct m0_be_op   *op,
				    struct m0_be_tx   *tx);

/* -------------------------------------------------------------------------
 * Iteration interfaces:
 * ------------------------------------------------------------------------- */

M0_INTERNAL void *m0_be_list_tail(struct m0_be_list *list, struct m0_be_op *op);
M0_INTERNAL void *m0_be_list_head(struct m0_be_list *list, struct m0_be_op *op);
M0_INTERNAL void *m0_be_list_prev(struct m0_be_list *list, struct m0_be_op *op,
				  const void *obj);
M0_INTERNAL void *m0_be_list_next(struct m0_be_list *list, struct m0_be_op *op,
				  const void *obj);

/* -------------------------------------------------------------------------
 * Modification interfaces:
 * ------------------------------------------------------------------------- */

M0_INTERNAL void          m0_be_list_add(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj);

M0_INTERNAL void    m0_be_list_add_after(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj,
					 void              *new);

M0_INTERNAL void   m0_be_list_add_before(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj,
					 void              *new);

M0_INTERNAL void     m0_be_list_add_tail(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj);

M0_INTERNAL void          m0_be_list_del(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj);


/** @} end of be group */
#endif /* __MERO_BE_LIST_H__ */

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
