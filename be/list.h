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
 *                  Maxim Medved <max.medved@seagate.com>
 * Original creation date: 29-May-2013
 */

#pragma once
#ifndef __MERO_BE_LIST_H__
#define __MERO_BE_LIST_H__

#include "lib/tlist.h"          /* m0_tl */
#include "lib/tlist_xc.h"
#include "format/format.h"      /* m0_format_header */
#include "format/format_xc.h"

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

enum m0_be_list_format_version {
	M0_BE_LIST_FORMAT_VERSION_1 = 1,

	/* future versions, uncomment and update M0_BE_LIST_FORMAT_VERSION */
	/*M0_BE_LIST_FORMAT_VERSION_2,*/
	/*M0_BE_LIST_FORMAT_VERSION_3,*/

	/** Current version, should point to the latest version present */
	M0_BE_LIST_FORMAT_VERSION = M0_BE_LIST_FORMAT_VERSION_1
};

struct m0_be_list {
	struct m0_format_header  bl_format_header;
	struct m0_tl_descr       bl_descr;
	struct m0_tl             bl_list;
	/** m0_tl_descr::td_name */
	char                     bl_td_name[0x40];
	struct m0_format_footer  bl_format_footer;
	/*
	 * volatile-only fields
	 */
	struct m0_be_seg        *bl_seg;
} M0_XCA_RECORD;

/** List operations that modify memory. */
enum m0_be_list_op {
	M0_BLO_CREATE,          /**< m0_be_list_create() */
	M0_BLO_DESTROY,         /**< m0_be_list_destroy() */
	M0_BLO_ADD,             /**< m0_be_list_add(),
				     m0_be_list_add_after(),
				     m0_be_list_add_before(),
				     m0_be_list_add_tail() */
	M0_BLO_DEL,             /**< m0_be_list_del() */
	M0_BLO_MOVE,            /**< m0_be_list_create() */
	M0_BLO_TLINK_CREATE,    /**< m0_be_tlink_create() */
	M0_BLO_TLINK_DESTROY,   /**< m0_be_tlink_destroy() */
	M0_BLO_NR
};

/**
 * Calculates the credit needed to perform @nr list operations of type
 * @optype and adds this credit to @accum.
 */
M0_INTERNAL void m0_be_list_credit(const struct m0_be_list *list,
				   enum m0_be_list_op       optype,
				   m0_bcount_t              nr,
				   struct m0_be_tx_credit  *accum);

/* -------------------------------------------------------------------------
 * Construction/Destruction:
 * ------------------------------------------------------------------------- */
M0_INTERNAL void m0_be_list_init(struct m0_be_list *list,
				 struct m0_be_seg  *seg);

M0_INTERNAL void m0_be_list_fini(struct m0_be_list *list);

M0_INTERNAL void m0_be_list_create(struct m0_be_list        *list,
				   struct m0_be_tx          *tx,
				   struct m0_be_op          *op,
				   struct m0_be_seg         *seg,
				   const struct m0_tl_descr *desc);

M0_INTERNAL void m0_be_list_destroy(struct m0_be_list *list,
				    struct m0_be_tx   *tx,
				    struct m0_be_op   *op);


M0_INTERNAL bool m0_be_list_is_empty(struct m0_be_list *list);

/*
 * m0_be_link_*() functions follow BE naming pattern
 * and not m0_tlist naming.
 *
 * - m0_be_tlink_create() calls m0_tlink_init();
 * - m0_be_tlink_destroy() calls m0_tlink_fini();
 * - m0_be_tlink_init() and m0_be_tlink_fini() are no-op now.
 */
M0_INTERNAL void m0_be_tlink_init(void *obj, struct m0_be_list *list);
M0_INTERNAL void m0_be_tlink_fini(void *obj, struct m0_be_list *list);
M0_INTERNAL void m0_be_tlink_create(void              *obj,
				    struct m0_be_tx   *tx,
				    struct m0_be_op   *op,
				    struct m0_be_list *list);
M0_INTERNAL void m0_be_tlink_destroy(void              *obj,
				     struct m0_be_tx   *tx,
				     struct m0_be_op   *op,
				     struct m0_be_list *list);


/* -------------------------------------------------------------------------
 * Iteration interfaces:
 * ------------------------------------------------------------------------- */

M0_INTERNAL void *m0_be_list_tail(struct m0_be_list *list,
				  struct m0_be_op   *op);

M0_INTERNAL void *m0_be_list_head(struct m0_be_list *list,
				  struct m0_be_op   *op);

M0_INTERNAL void *m0_be_list_prev(struct m0_be_list *list,
				  struct m0_be_op   *op,
				  const void        *obj);

M0_INTERNAL void *m0_be_list_next(struct m0_be_list *list,
				  struct m0_be_op   *op,
				  const void        *obj);

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
					 void              *obj_new);

M0_INTERNAL void   m0_be_list_add_before(struct m0_be_list *list,
					 struct m0_be_op   *op,
					 struct m0_be_tx   *tx,
					 void              *obj,
					 void              *obj_new);

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
