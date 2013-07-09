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
 * @defgroup be
 *
 * @{
 */

struct m0_be_list {
	struct m0_tl              bl_list;
	const struct m0_tl_descr *bl_descr;
	struct m0_be_seg         *bl_seg;
};

/**
 * Creates and captures a new list in pre-allocated segment space.
 *
 * @pre m0_be_pinned(&M0_BE_REG(list->bl_seg, sizeof *list, list))
 */
M0_INTERNAL void m0_be_list_create(const struct m0_tl_descr *d,
				   struct m0_be_list *list,
				   struct m0_be_tx *tx);

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list *list,
				    struct m0_be_tx *tx);

/**
 * Pins the head and first `nelems' of the list, loading them from disk
 * storage if necessary.
 */
M0_INTERNAL void m0_be_list_get(const struct m0_be_list *list,
				m0_bcount_t nelems,
				struct m0_be_op *op);

M0_INTERNAL void m0_be_list_put(const struct m0_be_list *list,
				m0_bcount_t nelems);

M0_INTERNAL void m0_be_list_init(struct m0_be_list *list,
				 struct m0_be_seg *seg);

M0_INTERNAL void m0_be_list_fini(struct m0_be_list *list);

/** List operations that modify memory. */
enum m0_be_list_op {
	M0_BLO_INSERT,
	M0_BLO_DELETE,
	M0_BLO_MOVE,
	M0_BLO_NR
};

/**
 * Calculates the credit needed to perform `nr' list operations of type
 * `optype' and adds this credit to `accum'.
 *
 * @see m0_be_tx_prep()
 */
M0_INTERNAL void m0_be_list_credit(const struct m0_be_list *list,
				   enum m0_be_list_op optype,
				   m0_bcount_t nr,
				   struct m0_be_tx_credit *accum);

/**
 * Captures (origin - left)-th .. (origin + right)-th elements of the list.
 *
 * @see m0_be_tx_capture()
 */
M0_INTERNAL void m0_be_list_capture(const struct m0_be_list *list,
				    struct m0_be_tx *tx,
				    const struct m0_tlink *origin,
				    m0_bcount_t left,
				    m0_bcount_t right);

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
