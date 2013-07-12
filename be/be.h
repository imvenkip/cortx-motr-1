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
#ifndef __MERO_BE_BE_H__
#define __MERO_BE_BE_H__

#include "be/tx.h"
#include "be/seg.h"

/**
 * @defgroup be
 *
 * @{
 */

struct m0_fom;
struct m0_buf;
struct m0_be_tx;
struct m0_be_btree;
struct m0_be_btree_anchor;

struct m0_be {
	struct m0_be_tx_engine b_tx_engine;
	struct m0_be_seg       b_seg0;
	struct m0_be_log_X    *b_log;
	/* This value is used to assign m0_be_seg::bs_id. */
	uint64_t               b_next_segid;
};

M0_INTERNAL void m0_be_init(struct m0_be *be);
M0_INTERNAL void m0_be_fini(struct m0_be *be);

enum m0_be_op_state {
	M0_BOS_INIT,
	M0_BOS_ACTIVE,
	M0_BOS_SUCCESS,
	M0_BOS_FAILURE,
	M0_BOS_NR
};

enum m0_be_op_type {
	M0_BOP_REG,
	M0_BOP_SEGIO,
	M0_BOP_TREE,
	M0_BOP_LIST,
	M0_BOP_NR
};

struct m0_be_op {
	struct m0_sm       bo_sm;
	struct m0_fom     *bo_fom;

	enum m0_be_op_type bo_utype; /* bo_u type */
	struct m0_be_op   *bo_parent_op;
	union {
		/* Used by m0_be_reg_get(). */
		struct m0_be_reg                   u_reg;

		/* Used by m0_be_seg_read(), m0_be_seg_write(). */
		struct {
			/* STOB i/o structure with allocated si_stob.iv_index
			 * array. */
			struct m0_stob_io          si_stobio;
			struct m0_clink            si_clink;
		} u_segio;

		/* Used by m0_be_list_get() and its callback. */
		struct {
			const struct m0_be_list   *l_list;
			void                      *l_lnode;
			m0_bcount_t                l_nelems;
		} u_list;

		struct {
			struct m0_be_btree        *t_tree;
			struct m0_be_tx           *t_tx;
			/* XXX to be defined in btree.c */
			unsigned int               t_op;
			const struct m0_buf       *t_in;
			struct m0_buf              t_out;
			struct m0_buf              t_out2;
			struct m0_be_btree_anchor *t_anchor;
		} u_btree;
	} bo_u;
};

M0_INTERNAL void m0_be_op_init(struct m0_be_op *op);
M0_INTERNAL void m0_be_op_fini(struct m0_be_op *op);

M0_INTERNAL enum m0_be_op_state m0_be_op_state(const struct m0_be_op *op);

M0_INTERNAL void m0_be_op_state_set(struct m0_be_op *op,
				    enum m0_be_op_state state);

/** Waits for the operation to complete and returns its rc. */
M0_INTERNAL int m0_be_op_wait(struct m0_be_op *op);

/**
 * Moves the fom to the "next_state" and arranges for state transitions to
 * continue when "op" completes. Returns value suitable to be returned from
 * m0_fom_ops::fo_tick() implementation.
 */
M0_INTERNAL int m0_be_op_tick_ret(struct m0_be_op *op, struct m0_fom *fom,
				  int next_state);

/* These two are called from mero/init.c. */
M0_INTERNAL int  m0_backend_init(void);
M0_INTERNAL void m0_backend_fini(void);

/** @} end of be group */
#endif /* __MERO_BE_BE_H__ */

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
