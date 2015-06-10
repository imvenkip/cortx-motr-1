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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 17-Jul-2013
 */

#pragma once

#ifndef __MERO_BE_OP_H__
#define __MERO_BE_OP_H__

#include "lib/buf.h"    /* m0_buf */
#include "lib/tlist.h"  /* m0_tl */
#include "lib/types.h"  /* bool */
#include "lib/mutex.h"  /* m0_mutex */

#include "sm/sm.h"      /* m0_sm */

/**
 * @defgroup be Meta-data back-end
 *
 * @{
 */

struct m0_fom;
struct m0_be_tx;
struct m0_be_btree;
struct m0_be_btree_anchor;

enum m0_be_op_state {
	M0_BOS_INIT,
	M0_BOS_ACTIVE,
	M0_BOS_DONE,
};

enum m0_be_op_type {
	M0_BOP_TREE,
	M0_BOP_LIST,
};

struct m0_be_op {
	struct m0_sm        bo_sm;
	struct m0_fom	   *bo_fom;
	/*
	 * Hack.
	 *
	 * In the future sm group for m0_be_op should be taken
	 * from m0_locality_here().
	 *
	 * @see MERO-787 comments for the reference.
	 */
	struct m0_sm_group  bo_sm_group;

	enum m0_be_op_type  bo_utype; /* bo_u type */
	union {
		struct {
			/**
			 * Pointer to memory that was allocated by
			 * m0_be_alloc() or m0_be_alloc_aligned().
			 */
			void *a_ptr;
			/* XXX @todo refactor all _rc into m0_be_op.bo_rc */
			int   a_rc;
		} u_allocator;
		struct m0_be_op__btree {
			struct m0_be_btree        *t_tree;
			struct m0_be_tx           *t_tx;
			/* XXX to be defined in btree.c */
			unsigned int               t_op;
			const struct m0_buf       *t_in;
			struct m0_buf              t_out_val;
			struct m0_buf              t_out_key;
			struct m0_be_btree_anchor *t_anchor;
			int                        t_rc;
		} u_btree;

		struct {
			int                        e_rc;
		} u_emap;
	} bo_u;

	/** list of children */
	struct m0_tl        bo_children;
	/** link for parent's m0_be_op::bo_children */
	struct m0_tlink     bo_set_link;
	/** magic for m0_be_op::bo_set_link */
	uint64_t            bo_set_link_magic;
	/** parent op */
	struct m0_be_op    *bo_parent;
	/* is this op an op_set */
	bool                bo_is_op_set;
};

M0_INTERNAL void m0_be_op_init(struct m0_be_op *op);
M0_INTERNAL void m0_be_op_fini(struct m0_be_op *op);

/** Moves op to M0_BOS_ACTIVE state. */
M0_INTERNAL void m0_be_op_active(struct m0_be_op *op);

/** Moves op to M0_BOS_DONE state. */
M0_INTERNAL void m0_be_op_done(struct m0_be_op *op);

/** Is op in M0_BOS_DONE state? */
M0_INTERNAL bool m0_be_op_is_done(struct m0_be_op *op);

/**
 * Waits for the operation to complete.
 *
 * @see M0_BE_OP_SYNC(), M0_BE_OP_SYNC_RET()
 */
M0_INTERNAL void m0_be_op_wait(struct m0_be_op *op);

/**
 * Moves the fom to the "next_state" and arranges for state transitions to
 * continue when "op" completes. Returns value suitable to be returned from
 * m0_fom_ops::fo_tick() implementation.
 */
M0_INTERNAL int m0_be_op_tick_ret(struct m0_be_op *op, struct m0_fom *fom,
				  int next_state);

/**
 * Adds @child to @parent, making the latter an "op set".
 */
M0_INTERNAL void m0_be_op_set_add(struct m0_be_op *parent,
				  struct m0_be_op *child);

/**
 * Performs the action, waiting for its completion.
 *
 * Example:
 * @code
 *         M0_BE_OP_SYNC(op, m0_be_btree_destroy(tree, tx, &op));
 *         M0_BE_OP_SYNC(op, rc = m0_fol_init(fol, seg, tx, &op));
 * @endcode
 */
#define M0_BE_OP_SYNC(op_obj, action)			\
	do {						\
		struct m0_be_op op_obj;			\
		M0_BE_OP_SYNC_WITH(&op_obj, action);	\
	} while (0)

/**
 * Similar to #M0_BE_OP_SYNC, but works with a caller-supplied operation
 * structure.
 */
#define M0_BE_OP_SYNC_WITH(op, action)		\
	do {					\
		struct m0_be_op *__opp = (op);	\
						\
		m0_be_op_init(__opp);		\
		action;				\
		m0_be_op_wait(__opp);	        \
		m0_be_op_fini(__opp);		\
	} while (0)


/**
 * Performs the action, waits for its completion, and returns
 * result of operation.
 *
 * Example:
 * @code
 *         int rc;
 *
 *         rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_create(tree, tx, &op),
 *                                bo_u.u_btree.t_rc);
 * @endcode
 */
#define M0_BE_OP_SYNC_RET(op_obj, action, member)		 \
	({							 \
		struct m0_be_op	op_obj;				 \
		M0_BE_OP_SYNC_RET_WITH(&op_obj, action, member); \
	})

/**
 * Similar to #M0_BE_OP_SYNC_RET, but works with a caller-supplied operation
 * structure.
 */
#define M0_BE_OP_SYNC_RET_WITH(op, action, member)	\
	({						\
		struct m0_be_op	      *__opp = (op);	\
		typeof(__opp->member)  __result;	\
							\
		m0_be_op_init(__opp);			\
		action;					\
		m0_be_op_wait(__opp);		        \
		__result = __opp->member;		\
		m0_be_op_fini(__opp);			\
		__result;				\
	})

/** @} end of be group */
#endif /* __MERO_BE_OP_H__ */

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
