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
 * Original author: Maxim Medved <maxim_medved@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_TX_REGMAP_H__
#define __MERO_BE_TX_REGMAP_H__

#include "be/seg.h"		/* m0_be_reg */
#include "be/tx_credit.h"	/* m0_be_tx_credit */

struct m0_ext;
struct m0_be_op;
struct m0_be_io;

/**
 * @defgroup be Meta-data back-end
 *
 * @{
 */

/**
 * Region in a transaction-private memory buffer.
 *
 * When a memory region is captured in a transaction, the contents of this
 * region, i.e., new values placed in the memory by the user, are copied into
 * a transaction-private memory buffer.
 */
struct m0_be_reg_d {
	/**
	 * The region in a segment where the captured data is copied from.
	 */
	struct m0_be_reg rd_reg;
	/**
	 * The address within transaction-private memory buffer where the
	 * data is copied to.
	 */
	void            *rd_buf;
};

#define M0_BE_REG_D(reg, buf) (struct m0_be_reg_d) \
		{ .rd_reg = (reg), .rd_buf = (buf) }
#define M0_BE_REG_D_CREDIT(rd) M0_BE_TX_CREDIT(1, (rd)->rd_reg.br_size)

/** Regions tree. */
struct m0_be_reg_d_tree {
	size_t              brt_size;
	size_t              brt_size_max;
	struct m0_be_reg_d *brt_r;
};

struct m0_be_regmap_ops {
	void (*rmo_add)(void *data, struct m0_be_reg_d *rd);
	void (*rmo_del)(void *data, const struct m0_be_reg_d *rd);
	void (*rmo_cpy)(void *data, const struct m0_be_reg_d *super,
			const struct m0_be_reg_d *rd);
	void (*rmo_cut)(void *data, struct m0_be_reg_d *rd,
			m0_bcount_t cut_at_start, m0_bcount_t cut_at_end);
};

struct m0_be_regmap {
	struct m0_be_reg_d_tree        br_rdt;
	const struct m0_be_regmap_ops *br_ops;
	void                          *br_ops_data;
};

M0_INTERNAL bool m0_be_reg_d__invariant(const struct m0_be_reg_d *rd);
M0_INTERNAL bool m0_be_reg_d_is_in(const struct m0_be_reg_d *rd, void *ptr);

/**
 * Initialize m0_be_reg_d tree.
 *
 * - memory allocation takes place only in this function;
 * - m0_be_reg_d given to the m0_be_rdt_ins() will be copied to the tree;
 * - m0_be_reg_d in the tree are ordered by a region start address;
 * - there is no overlapping between regions in the tree;
 * - regions in the tree have size > 0;
 * - all fields of m0_be_reg_d except rd_reg are completely ignored in a tree
 *   functions;
 *
 * Region is from the tree iff it is returned by m0_be_rdt_find(),
 * m0_be_rdt_next(), m0_be_rdt_del().
 * @note Current implementation is based on array, so technically it is not
 * a tree. Optimizations should be made to make it a real tree.
 */
M0_INTERNAL int m0_be_rdt_init(struct m0_be_reg_d_tree *rdt, size_t size_max);
/** Finalize m0_be_reg_d tree. Free all memory allocated */
M0_INTERNAL void m0_be_rdt_fini(struct m0_be_reg_d_tree *rdt);
M0_INTERNAL bool m0_be_rdt__invariant(const struct m0_be_reg_d_tree *rdt);
/** Return current size of the tree, in number of m0_be_reg_d */
M0_INTERNAL size_t m0_be_rdt_size(const struct m0_be_reg_d_tree *rdt);

/**
 * Find region by address.
 *
 * - find first m0_be_reg_d that contains byte with the given address or
 * - first region after the given address or
 * - get first region in the tree if the given address is NULL.
 *
 * @see m0_be_rdt_init(), m0_be_rdt_next().
 */
M0_INTERNAL struct m0_be_reg_d *
m0_be_rdt_find(const struct m0_be_reg_d_tree *rdt, void *addr);
/**
 * Find next region after the given region.
 *
 * @param rdt m0_be_reg_d tree
 * @param prev region to find after. Should be a region from the tree.
 * @return NULL if prev is the last region in the tree or
 *	   next region after the prev if it is not.
 * @see m0_be_rdt_init().
 */
M0_INTERNAL struct m0_be_reg_d *
m0_be_rdt_next(const struct m0_be_reg_d_tree *rdt, struct m0_be_reg_d *prev);

/**
 * Insert a region in the tree.
 *
 * @pre rd->rd_reg.br_size > 0
 * @note rd will be copied to the tree.
 * @see m0_be_rdt_init().
 */
M0_INTERNAL void m0_be_rdt_ins(struct m0_be_reg_d_tree *rdt,
			       const struct m0_be_reg_d *rd);
/**
 * Delete a region from the tree.
 *
 * @param rdt m0_be_reg_d tree
 * @param rd region to delete
 * @return pointer to the next item after the deleted
 * @return NULL if the last item was deleted
 * @see m0_be_rdt_init().
 */
M0_INTERNAL struct m0_be_reg_d *m0_be_rdt_del(struct m0_be_reg_d_tree *rdt,
					      const struct m0_be_reg_d *rd);

M0_INTERNAL void m0_be_rdt_reset(struct m0_be_reg_d_tree *rdt);

M0_INTERNAL int m0_be_regmap_init(struct m0_be_regmap *rm,
				  const struct m0_be_regmap_ops *ops,
				  void *ops_data, size_t size_max);
M0_INTERNAL void m0_be_regmap_fini(struct m0_be_regmap *rm);
M0_INTERNAL bool m0_be_regmap__invariant(const struct m0_be_regmap *rm);

/* XXX add const */
M0_INTERNAL void m0_be_regmap_add(struct m0_be_regmap *rm,
				  const struct m0_be_reg_d *rd);
M0_INTERNAL void m0_be_regmap_del(struct m0_be_regmap *rm,
				  const struct m0_be_reg_d *rd);

M0_INTERNAL struct m0_be_reg_d *m0_be_regmap_first(struct m0_be_regmap *rm);
M0_INTERNAL struct m0_be_reg_d *m0_be_regmap_next(struct m0_be_regmap *rm,
						  struct m0_be_reg_d *prev);
M0_INTERNAL size_t m0_be_regmap_size(const struct m0_be_regmap *rm);

M0_INTERNAL void m0_be_regmap_reset(struct m0_be_regmap *rm);

struct m0_be_reg_area {
	struct m0_be_regmap    bra_map;
	bool		       bra_data_copy;
	char		      *bra_area;
	m0_bcount_t	       bra_area_used;
	struct m0_be_tx_credit bra_prepared;
	/**
	 * Sum of all regions that were submitted to m0_be_reg_area_capture().
	 * Used to catch credit calculation errors.
	 */
	struct m0_be_tx_credit bra_captured;
};

M0_INTERNAL int m0_be_reg_area_init(struct m0_be_reg_area *ra,
				    const struct m0_be_tx_credit *prepared,
				    bool data_copy);
M0_INTERNAL void m0_be_reg_area_fini(struct m0_be_reg_area *ra);
M0_INTERNAL bool m0_be_reg_area__invariant(const struct m0_be_reg_area *ra);
M0_INTERNAL void m0_be_reg_area_used(struct m0_be_reg_area *ra,
				     struct m0_be_tx_credit *used);
M0_INTERNAL void m0_be_reg_area_prepared(struct m0_be_reg_area *ra,
					 struct m0_be_tx_credit *prepared);
M0_INTERNAL void m0_be_reg_area_captured(struct m0_be_reg_area *ra,
					 struct m0_be_tx_credit *captured);

M0_INTERNAL void m0_be_reg_area_capture(struct m0_be_reg_area *ra,
					const struct m0_be_reg_d *rd);
M0_INTERNAL void m0_be_reg_area_uncapture(struct m0_be_reg_area *ra,
					  const struct m0_be_reg_d *rd);

M0_INTERNAL void m0_be_reg_area_merge_in(struct m0_be_reg_area *ra,
					 struct m0_be_reg_area *src);

M0_INTERNAL void m0_be_reg_area_reset(struct m0_be_reg_area *ra);

M0_INTERNAL struct m0_be_reg_d *m0_be_reg_area_first(struct m0_be_reg_area *ra);
M0_INTERNAL struct m0_be_reg_d *
m0_be_reg_area_next(struct m0_be_reg_area *ra, struct m0_be_reg_d *prev);

#define M0_BE_REG_AREA_FORALL(ra, rd)			\
	for ((rd) = m0_be_reg_area_first(ra);		\
	     (rd) != NULL;				\
	     (rd) = m0_be_reg_area_next((ra), (rd)))

M0_INTERNAL void m0_be_reg_area_io_add(struct m0_be_reg_area *ra,
				       struct m0_be_io *io);

/** @} end of be group */
#endif /* __MERO_BE_TX_REGMAP_H__ */

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
