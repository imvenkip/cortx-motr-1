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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 28-May-2013
 */

#pragma once
#ifndef __MERO_BE_SEG_H__
#define __MERO_BE_SEG_H__

#include "be/alloc.h"   /* m0_be_allocator */
#include "stob/stob.h"  /* m0_stob */

struct m0_be;
struct m0_be_op;
struct m0_be_reg_d;
struct m0_be_reg_area;

/**
 * @defgroup be
 *
 * @{
 */

enum m0_be_seg_states {
	M0_BSS_INIT,
	M0_BSS_OPENED,
	M0_BSS_CLOSED,
};

#define M0_BE_SEG_PG_PRESENT       0x8000000000000000ULL
#define M0_BE_SEG_PG_PIN_CNT_MASK  (~M0_BE_SEG_PG_PRESENT)

struct m0_be_seg {
	uint64_t               bs_id; /* see also m0_be::b_next_segid */
	struct m0_stob        *bs_stob;
	m0_bcount_t            bs_size;
	void                  *bs_addr;
	struct m0_be_allocator bs_allocator;
	struct m0_be          *bs_be;
	int                    bs_state;
	uint32_t               bs_bshift;  /* STOB block shift */
	uint32_t               bs_pgshift; /* seg page shift */
	m0_bcount_t           *bs_pgmap;
	m0_bcount_t            bs_pgnr;
};

M0_INTERNAL bool m0_be__seg_invariant(const struct m0_be_seg *seg);

M0_INTERNAL void m0_be_seg_init(struct m0_be_seg *seg,
				struct m0_stob *stob,
				struct m0_be *be);

M0_INTERNAL void m0_be_seg_fini(struct m0_be_seg *seg);

/** Opens existing stob, reads segment header from it, etc. */
M0_INTERNAL int m0_be_seg_open(struct m0_be_seg *seg);

M0_INTERNAL void m0_be_seg_close(struct m0_be_seg *seg);

/** Creates the segment of specified size on the storage. */
M0_INTERNAL int m0_be_seg_create(struct m0_be_seg *seg, m0_bcount_t size);

M0_INTERNAL int m0_be_seg_destroy(struct m0_be_seg *seg);

struct m0_be_reg {
	struct m0_be_seg *br_seg;
	m0_bcount_t       br_size;
	void             *br_addr;
};

M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg, const char *name,
					void **out);

M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg, const char *name,
					void *value);

M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg, const char *name);

#define M0_BE_REG(seg, size, addr) \
	((struct m0_be_reg) {      \
		.br_seg  = (seg),  \
		.br_size = (size), \
		.br_addr = (addr) })

#define M0_BE_REG_PTR(seg, ptr)	M0_BE_REG((seg), sizeof *(ptr), (ptr))

M0_INTERNAL bool m0_be_seg_contains(const struct m0_be_seg *seg, void *addr);

M0_INTERNAL int m0_be_seg_write(struct m0_be_seg *seg,
				void *regd_tree,
				struct m0_be_op *op);

M0_INTERNAL int m0_be_seg_bufvec_build(struct m0_be_seg *seg,
				       void *regd_tree,
				       struct m0_indexvec *iv,
				       struct m0_bufvec *bv);

M0_INTERNAL bool m0_be_reg_is_eq(const struct m0_be_reg *r1,
				 const struct m0_be_reg *r2);

/*
 * `reg' parameter is not const, because stob IO will update
 * m0_be_reg::br_addr when a region is loaded/stored.
 */
M0_INTERNAL void m0_be_reg_get(struct m0_be_reg *reg, struct m0_be_op *op);

#define M0_BE_PTR_GET(seg, op, ptr) \
	m0_be_reg_get(M0_BE_REG_PTR((seg), (ptr)), (op))

#define M0_BE_ARR_GET(seg, op, arr, nr) \
	m0_be_reg_get(M0_BE_REG((seg), (sizeof arr[0]) * (nr), (arr)), (op))

/**
 * XXX DOCUMENTME
 *
 * @pre m0_be__reg_is_pinned(reg)
 */
M0_INTERNAL void m0_be_reg_get_fast(const struct m0_be_reg *reg);

#define M0_BE_PTR_GET_FAST(seg, ptr) \
	m0_be_reg_get_fast(M0_BE_REG_PTR((seg), (ptr)))

/**
 * @pre m0_be__reg_is_pinned(reg)
 */
M0_INTERNAL void m0_be_reg_put(const struct m0_be_reg *reg);

#define M0_BE_PTR_PUT(seg, ptr) \
	m0_be_reg_put(M0_BE_REG((seg), (sizeof *ptr), (ptr)))

/* Returns true iff all region's pages are pinned. */
M0_INTERNAL bool m0_be__reg_is_pinned(const struct m0_be_reg *reg);

M0_INTERNAL bool m0_be__reg_invariant(const struct m0_be_reg *reg);

M0_INTERNAL int m0_be__reg_read(struct m0_be_seg *seg,
				struct m0_be_op *op,
				struct m0_be_reg *regs,
				m0_bcount_t nr);

M0_INTERNAL int m0_be__reg_write(struct m0_be_seg *seg,
				 struct m0_be_op *op,
				 struct m0_be_reg *regs,
				 m0_bcount_t nr);

/**
 * Simple segment write implementation which has to be removed ASAP.
 * It's introduced because we need straightforward possibility to
 * write data on the disk. This will unblock other tasks in BE. This
 * call will be replaced with m0_be_seg_write() in the nearest future.
 */
M0_INTERNAL void m0_be_seg_write_simple(struct m0_be_seg *seg,
					struct m0_be_op *op,
					struct m0_be_reg_area *area);

/** @} end of be group */
#endif /* __MERO_BE_SEG_H__ */
