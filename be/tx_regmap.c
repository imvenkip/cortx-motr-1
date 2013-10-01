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

#include "be/tx_regmap.h"

#include "be/tx.h"
#include "be/io.h"

#include "lib/ext.h"    /* m0_ext */
#include "lib/errno.h"  /* ENOMEM */
#include "lib/memory.h"	/* m0_alloc */
#include "lib/assert.h"	/* M0_POST */
#include "lib/misc.h"	/* M0_SET0 */

/**
 * @addtogroup be
 *
 * @{
 */

/** @note don't forget to undefine this at the end of the file */
#define REGD_EXT(rd) (struct m0_ext) {					    \
	.e_start = (m0_bindex_t)(rd)->rd_reg.br_addr,			    \
	.e_end   = (m0_bindex_t)(rd)->rd_reg.br_addr + (rd)->rd_reg.br_size \
}

M0_INTERNAL bool m0_be_reg_d__invariant(const struct m0_be_reg_d *rd)
{
#if 0 /* XXX USEME */
	/* XXX Could we also check that rd->rd_buf belongs
	 * transaction-private memory buffer? */
	return m0_be__reg_invariant(&rd->rd_reg);
#else
	const struct m0_be_reg *reg = &rd->rd_reg;
	return reg->br_addr != NULL && reg->br_size > 0;
#endif
}

M0_INTERNAL bool m0_be_reg_d_is_in(const struct m0_be_reg_d *rd, void *ptr)
{
	M0_CASSERT(sizeof(m0_bindex_t) >= sizeof(ptr));
	return m0_ext_is_in(&REGD_EXT(rd), (m0_bindex_t) ptr);
}

static bool be_reg_d_are_overlapping(const struct m0_be_reg_d *rd1,
				     const struct m0_be_reg_d *rd2)
{
	return m0_ext_are_overlapping(&REGD_EXT(rd1), &REGD_EXT(rd2));
}

static bool be_reg_d_is_partof(const struct m0_be_reg_d *super,
			       const struct m0_be_reg_d *sub)
{
	return m0_ext_is_partof(&REGD_EXT(super), &REGD_EXT(sub));
}

/** Return address of the first byte inside the region. */
static void *be_reg_d_fb(const struct m0_be_reg_d *rd)
{
	return rd->rd_reg.br_addr;
}

/** Return address of the byte before be_reg_d_fb(rd). */
static void *be_reg_d_fb1(const struct m0_be_reg_d *rd)
{
	return (void *) ((uintptr_t) be_reg_d_fb(rd) - 1);
}

/** Return address of the last byte inside the region. */
static void *be_reg_d_lb(const struct m0_be_reg_d *rd)
{
	return rd->rd_reg.br_addr + rd->rd_reg.br_size - 1;
}

/** Return address of the byte after be_reg_d_lb(rd). */
static void *be_reg_d_lb1(const struct m0_be_reg_d *rd)
{
	return (void *) ((uintptr_t) be_reg_d_lb(rd) + 1);
}

static m0_bcount_t be_reg_d_size(const struct m0_be_reg_d *rd)
{
	return rd->rd_reg.br_size;
}

static bool be_rdt_contains(const struct m0_be_reg_d_tree *rdt,
			    const struct m0_be_reg_d *rd)
{
	return &rdt->brt_r[0] <= rd && rd < &rdt->brt_r[rdt->brt_size];
}

M0_INTERNAL int m0_be_rdt_init(struct m0_be_reg_d_tree *rdt, size_t size_max)
{
	rdt->brt_size_max = size_max;
	M0_ALLOC_ARR(rdt->brt_r, rdt->brt_size_max);
	if (rdt->brt_r == NULL)
		return -ENOMEM;

	M0_POST(m0_be_rdt__invariant(rdt));
	return 0;
}

M0_INTERNAL void m0_be_rdt_fini(struct m0_be_reg_d_tree *rdt)
{
	M0_PRE(m0_be_rdt__invariant(rdt));
	m0_free(rdt->brt_r);
}

M0_INTERNAL bool m0_be_rdt__invariant(const struct m0_be_reg_d_tree *rdt)
{
	return _0C(rdt != NULL) &&
	       _0C(rdt->brt_r != NULL || rdt->brt_size == 0) &&
	       _0C(rdt->brt_size <= rdt->brt_size_max) &&
	       _0C(m0_forall(i, rdt->brt_size,
			     m0_be_reg_d__invariant(&rdt->brt_r[i]))) &&
	       _0C(m0_forall(i, rdt->brt_size == 0 ? 0 : rdt->brt_size - 1,
			     rdt->brt_r[i].rd_reg.br_addr <
			     rdt->brt_r[i + 1].rd_reg.br_addr)) &&
	       _0C(m0_forall(i, rdt->brt_size == 0 ? 0 : rdt->brt_size - 1,
			     !be_reg_d_are_overlapping(&rdt->brt_r[i],
						       &rdt->brt_r[i + 1])));
}

M0_INTERNAL size_t m0_be_rdt_size(const struct m0_be_reg_d_tree *rdt)
{
	return rdt->brt_size;
}

static size_t be_rdt_find_i(const struct m0_be_reg_d_tree *rdt, void *addr)
{
	struct m0_ext e;
	m0_bindex_t   a = (m0_bindex_t) addr;
	size_t	      i;

	M0_CASSERT(sizeof(a) >= sizeof(addr));
	/** @todo use binary search */
	for (i = 0; i < rdt->brt_size; ++i) {
		e = REGD_EXT(&rdt->brt_r[i]);
		if (m0_ext_is_in(&e, a) || a < e.e_start)
			break;
	}
	M0_POST(m0_be_rdt__invariant(rdt));
	return i;
}


M0_INTERNAL struct m0_be_reg_d *
m0_be_rdt_find(const struct m0_be_reg_d_tree *rdt, void *addr)
{
	struct m0_be_reg_d *rd;
	size_t		    i;

	M0_PRE(m0_be_rdt__invariant(rdt));

	i = be_rdt_find_i(rdt, addr);
	rd = i == rdt->brt_size ? NULL : &rdt->brt_r[i];

	M0_POST(ergo(rd != NULL, be_rdt_contains(rdt, rd)));
	return rd;
}

M0_INTERNAL struct m0_be_reg_d *
m0_be_rdt_next(const struct m0_be_reg_d_tree *rdt, struct m0_be_reg_d *prev)
{
	struct m0_be_reg_d *rd;

	M0_PRE(m0_be_rdt__invariant(rdt));
	M0_PRE(prev != NULL);
	M0_PRE(be_rdt_contains(rdt, prev));

	rd = prev == &rdt->brt_r[rdt->brt_size - 1] ? NULL : ++prev;

	M0_POST(ergo(rd != NULL, be_rdt_contains(rdt, rd)));
	return rd;
}

M0_INTERNAL void m0_be_rdt_ins(struct m0_be_reg_d_tree *rdt,
			       const struct m0_be_reg_d *rd)
{
	size_t index;
	size_t i;

	M0_PRE(m0_be_rdt__invariant(rdt));
	M0_PRE(m0_be_rdt_size(rdt) < rdt->brt_size_max);
	M0_PRE(rd->rd_reg.br_size > 0);

	index = be_rdt_find_i(rdt, be_reg_d_fb(rd));
	++rdt->brt_size;
	for (i = rdt->brt_size - 1; i > index; --i)
		rdt->brt_r[i] = rdt->brt_r[i - 1];
	rdt->brt_r[index] = *rd;

	M0_POST(m0_be_rdt__invariant(rdt));
}

M0_INTERNAL struct m0_be_reg_d *m0_be_rdt_del(struct m0_be_reg_d_tree *rdt,
					      const struct m0_be_reg_d *rd)
{
	size_t index;
	size_t i;

	M0_PRE(m0_be_rdt__invariant(rdt));
	M0_PRE(m0_be_rdt_size(rdt) > 0);

	index = be_rdt_find_i(rdt, be_reg_d_fb(rd));
	M0_ASSERT(m0_be_reg_eq(&rdt->brt_r[index].rd_reg, &rd->rd_reg));

	for (i = index; i + 1 < rdt->brt_size; ++i)
		rdt->brt_r[i] = rdt->brt_r[i + 1];
	--rdt->brt_size;

	M0_POST(m0_be_rdt__invariant(rdt));
	return index == m0_be_rdt_size(rdt) ? NULL : &rdt->brt_r[index];
}

M0_INTERNAL void m0_be_rdt_reset(struct m0_be_reg_d_tree *rdt)
{
	M0_PRE(m0_be_rdt__invariant(rdt));

	rdt->brt_size = 0;

	M0_POST(m0_be_rdt_size(rdt) == 0);
	M0_POST(m0_be_rdt__invariant(rdt));
}

M0_INTERNAL int m0_be_regmap_init(struct m0_be_regmap *rm,
				  const struct m0_be_regmap_ops *ops,
				  void *ops_data, size_t size_max)
{
	int rc;

	rc = m0_be_rdt_init(&rm->br_rdt, size_max);
	rm->br_ops = ops;
	rm->br_ops_data = ops_data;

	M0_POST(ergo(rc == 0, m0_be_regmap__invariant(rm)));
	return rc;
}

M0_INTERNAL void m0_be_regmap_fini(struct m0_be_regmap *rm)
{
	M0_PRE(m0_be_regmap__invariant(rm));
	m0_be_rdt_fini(&rm->br_rdt);
}

M0_INTERNAL bool m0_be_regmap__invariant(const struct m0_be_regmap *rm)
{
	return rm != NULL && m0_be_rdt__invariant(&rm->br_rdt);
}

static struct m0_be_reg_d *be_regmap_find_fb(struct m0_be_regmap *rm,
					     const struct m0_be_reg_d *rd)
{
	return m0_be_rdt_find(&rm->br_rdt, be_reg_d_fb(rd));
}

static void be_regmap_reg_d_cut(struct m0_be_regmap *rm,
				struct m0_be_reg_d *rd,
				m0_bcount_t cut_start,
				m0_bcount_t cut_end)
{
	struct m0_be_reg *r = &rd->rd_reg;

	M0_PRE(m0_be_reg_d__invariant(rd));
	M0_PRE(rd->rd_reg.br_size > cut_start + cut_end);

	rm->br_ops->rmo_cut(rm->br_ops_data, rd, cut_start, cut_end);

	r->br_size -= cut_start;
	r->br_addr += cut_start;

	r->br_size -= cut_end;

	M0_POST(m0_be_reg_d__invariant(rd));
}

M0_INTERNAL void m0_be_regmap_add(struct m0_be_regmap *rm,
				  const struct m0_be_reg_d *rd)
{
	struct m0_be_reg_d *rdi;
	struct m0_be_reg_d  rd_copy;

	M0_PRE(m0_be_regmap__invariant(rm));
	M0_PRE(rd != NULL);
	M0_PRE(m0_be_reg_d__invariant(rd));

	rdi = be_regmap_find_fb(rm, rd);
	if (rdi != NULL && be_reg_d_is_partof(rdi, rd)) {
		/* old region completely absorbs the new */
		rm->br_ops->rmo_cpy(rm->br_ops_data, rdi, rd);
	} else {
		m0_be_regmap_del(rm, rd);
		rd_copy = *rd;
		rm->br_ops->rmo_add(rm->br_ops_data, &rd_copy);
		m0_be_rdt_ins(&rm->br_rdt, &rd_copy);
	}

	M0_POST(m0_be_regmap__invariant(rm));
}

M0_INTERNAL void m0_be_regmap_del(struct m0_be_regmap *rm,
				  const struct m0_be_reg_d *rd)
{
	struct m0_be_reg_d *rdi;
	m0_bcount_t	    cut;

	M0_PRE(m0_be_regmap__invariant(rm));
	M0_PRE(rd != NULL);
	M0_PRE(m0_be_reg_d__invariant(rd));

	/* first intersection */
	rdi = be_regmap_find_fb(rm, rd);
	if (rdi != NULL && m0_be_reg_d_is_in(rdi, be_reg_d_fb(rd)) &&
	    !m0_be_reg_d_is_in(rdi, be_reg_d_lb1(rd)) &&
	    !be_reg_d_is_partof(rd, rdi)) {
		cut = be_reg_d_size(rdi);
		cut -= be_reg_d_fb(rd) - be_reg_d_fb(rdi);
		be_regmap_reg_d_cut(rm, rdi, 0, cut);
		rdi = m0_be_rdt_next(&rm->br_rdt, rdi);
	}
	/* delete all completely covered regions */
	while (rdi != NULL && be_reg_d_is_partof(rd, rdi)) {
		rm->br_ops->rmo_del(rm->br_ops_data, rdi);
		rdi = m0_be_rdt_del(&rm->br_rdt, rdi);
	}
	/* last intersection */
	if (rdi != NULL && !m0_be_reg_d_is_in(rdi, be_reg_d_fb1(rd)) &&
	    m0_be_reg_d_is_in(rdi, be_reg_d_lb(rd))) {
		cut = be_reg_d_size(rdi);
		cut -= be_reg_d_lb(rdi) - be_reg_d_lb(rd);
		be_regmap_reg_d_cut(rm, rdi, cut, 0);
	}
}

M0_INTERNAL struct m0_be_reg_d *m0_be_regmap_first(struct m0_be_regmap *rm)
{
	return m0_be_rdt_find(&rm->br_rdt, NULL);
}

M0_INTERNAL struct m0_be_reg_d *m0_be_regmap_next(struct m0_be_regmap *rm,
						  struct m0_be_reg_d *prev)
{
	return m0_be_rdt_next(&rm->br_rdt, prev);
}

M0_INTERNAL size_t m0_be_regmap_size(const struct m0_be_regmap *rm)
{
	M0_PRE(m0_be_regmap__invariant(rm));
	return m0_be_rdt_size(&rm->br_rdt);
}

M0_INTERNAL void m0_be_regmap_reset(struct m0_be_regmap *rm)
{
	m0_be_rdt_reset(&rm->br_rdt);
}

#undef REGD_EXT

static const struct m0_be_regmap_ops be_reg_area_copy_ops;
static const struct m0_be_regmap_ops be_reg_area_ops;

M0_INTERNAL int m0_be_reg_area_init(struct m0_be_reg_area *ra,
				    const struct m0_be_tx_credit *prepared,
				    bool data_copy)
{
	const struct m0_be_regmap_ops *ops =
		data_copy ? &be_reg_area_copy_ops : &be_reg_area_ops;
	int rc;

	*ra = (struct m0_be_reg_area) {
		.bra_data_copy = data_copy,
		.bra_prepared  = *prepared
	};

	rc = m0_be_regmap_init(&ra->bra_map, ops, ra,
			       ra->bra_prepared.tc_reg_nr);
	if (rc == 0 && data_copy) {
		M0_ALLOC_ARR(ra->bra_area, ra->bra_prepared.tc_reg_size);
		if (ra->bra_area == NULL) {
			m0_be_regmap_fini(&ra->bra_map);
			rc = -ENOMEM;
		}
	}

	/* invariant should work in each case */
	if (rc != 0)
		M0_SET0(ra);

	M0_POST(ergo(rc == 0, m0_be_reg_area__invariant(ra)));
	return rc;
}

M0_INTERNAL void m0_be_reg_area_fini(struct m0_be_reg_area *ra)
{
	M0_PRE(m0_be_reg_area__invariant(ra));
	m0_be_regmap_fini(&ra->bra_map);
	m0_free(ra->bra_area);
}

M0_INTERNAL bool m0_be_reg_area__invariant(const struct m0_be_reg_area *ra)
{
	return m0_be_regmap__invariant(&ra->bra_map) &&
	       ra->bra_data_copy == (ra->bra_area != NULL) &&
	       ra->bra_area_used <= ra->bra_prepared.tc_reg_size &&
	       m0_be_tx_credit_le(&ra->bra_captured, &ra->bra_prepared);
}

M0_INTERNAL void m0_be_reg_area_used(struct m0_be_reg_area *ra,
				     struct m0_be_tx_credit *used)
{
	struct m0_be_reg_d *rd;

	M0_PRE(m0_be_reg_area__invariant(ra));

	M0_SET0(used);
	for (rd = m0_be_regmap_first(&ra->bra_map); rd != NULL;
	     rd = m0_be_regmap_next(&ra->bra_map, rd))
		m0_be_tx_credit_add(used, &M0_BE_REG_D_CREDIT(rd));
}

M0_INTERNAL void m0_be_reg_area_prepared(struct m0_be_reg_area *ra,
					 struct m0_be_tx_credit *prepared)
{
	M0_PRE(m0_be_reg_area__invariant(ra));

	*prepared = ra->bra_prepared;
}

static void be_reg_d_cpy(void *dst, const struct m0_be_reg_d *rd)
{
	memcpy(dst, rd->rd_reg.br_addr, rd->rd_reg.br_size);
}

static void *be_reg_area_alloc(struct m0_be_reg_area *ra, m0_bcount_t size)
{
	void *ptr;

	ptr = ra->bra_area + ra->bra_area_used;
	ra->bra_area_used += size;
	M0_POST(ra->bra_area_used <= ra->bra_prepared.tc_reg_size);
	return ptr;
}

static void be_reg_area_add_copy(void *data, struct m0_be_reg_d *rd)
{
	struct m0_be_reg_area *ra = data;

	M0_PRE(m0_be_reg_d__invariant(rd));
	M0_PRE(rd->rd_buf == NULL);

	rd->rd_buf = be_reg_area_alloc(ra, rd->rd_reg.br_size);
	be_reg_d_cpy(rd->rd_buf, rd);
}

static void be_reg_area_add(void *data, struct m0_be_reg_d *rd)
{
	M0_PRE(rd->rd_buf != NULL);
}

static void be_reg_area_del(void *data, const struct m0_be_reg_d *rd)
{
	/* do nothing */
}

static void be_reg_area_cpy_copy(void *data, const struct m0_be_reg_d *super,
				 const struct m0_be_reg_d *rd)
{
	m0_bcount_t rd_offset;

	M0_PRE(m0_be_reg_d__invariant(rd));
	M0_PRE(be_reg_d_is_partof(super, rd));
	M0_PRE(super->rd_buf != NULL);
	M0_PRE(rd->rd_buf == NULL);

	rd_offset = rd->rd_reg.br_addr - super->rd_reg.br_addr;
	be_reg_d_cpy(super->rd_buf + rd_offset, rd);
}

/* XXX copy-paste from be_reg_area_cpy_copy() */
static void be_reg_area_cpy(void *data, const struct m0_be_reg_d *super,
			    const struct m0_be_reg_d *rd)
{
	m0_bcount_t rd_offset;

	M0_PRE(m0_be_reg_d__invariant(rd));
	M0_PRE(be_reg_d_is_partof(super, rd));
	M0_PRE(super->rd_buf != NULL);
	M0_PRE(rd->rd_buf != NULL);

	rd_offset = rd->rd_reg.br_addr - super->rd_reg.br_addr;
	memcpy(super->rd_buf + rd_offset, rd->rd_buf, rd->rd_reg.br_size);
}

static void be_reg_area_cut(void *data, struct m0_be_reg_d *rd,
			    m0_bcount_t cut_at_start, m0_bcount_t cut_at_end)
{
	rd->rd_buf += cut_at_start;
}

static const struct m0_be_regmap_ops be_reg_area_copy_ops = {
	.rmo_add = be_reg_area_add_copy,
	.rmo_del = be_reg_area_del,
	.rmo_cpy = be_reg_area_cpy_copy,
	.rmo_cut = be_reg_area_cut
};

static const struct m0_be_regmap_ops be_reg_area_ops = {
	.rmo_add = be_reg_area_add,
	.rmo_del = be_reg_area_del,
	.rmo_cpy = be_reg_area_cpy,
	.rmo_cut = be_reg_area_cut
};

M0_INTERNAL void m0_be_reg_area_capture(struct m0_be_reg_area *ra,
					const struct m0_be_reg_d *rd)
{
	M0_PRE(m0_be_reg_area__invariant(ra));
	M0_PRE(m0_be_reg_d__invariant(rd));

	m0_be_tx_credit_add(&ra->bra_captured, &M0_BE_REG_D_CREDIT(rd));
	M0_ASSERT(m0_be_tx_credit_le(&ra->bra_captured, &ra->bra_prepared));
	m0_be_regmap_add(&ra->bra_map, rd);

	M0_POST(m0_be_reg_d__invariant(rd));
	M0_POST(m0_be_reg_area__invariant(ra));
}

M0_INTERNAL void m0_be_reg_area_uncapture(struct m0_be_reg_area *ra,
					  const struct m0_be_reg_d *rd)
{
	M0_PRE(m0_be_reg_area__invariant(ra));
	M0_PRE(m0_be_reg_d__invariant(rd));

	m0_be_regmap_del(&ra->bra_map, rd);

	M0_POST(m0_be_reg_d__invariant(rd));
	M0_POST(m0_be_reg_area__invariant(ra));
}

M0_INTERNAL void m0_be_reg_area_merge_in(struct m0_be_reg_area *ra,
					 struct m0_be_reg_area *src)
{
	struct m0_be_reg_d *rd;

	M0_BE_REG_AREA_FORALL(src, rd) {
		m0_be_reg_area_capture(ra, rd);
	}
}

M0_INTERNAL void m0_be_reg_area_reset(struct m0_be_reg_area *ra)
{
	M0_PRE(m0_be_reg_area__invariant(ra));

	m0_be_regmap_reset(&ra->bra_map);
	ra->bra_area_used = 0;
	M0_SET0(&ra->bra_captured);

	M0_POST(m0_be_reg_area__invariant(ra));
}

M0_INTERNAL struct m0_be_reg_d *
m0_be_reg_area_first(struct m0_be_reg_area *ra)
{
	return m0_be_regmap_first(&ra->bra_map);
}

M0_INTERNAL struct m0_be_reg_d *
m0_be_reg_area_next(struct m0_be_reg_area *ra, struct m0_be_reg_d *prev)
{
	return m0_be_regmap_next(&ra->bra_map, prev);
}

M0_INTERNAL void m0_be_reg_area_io_add(struct m0_be_reg_area *ra,
				       struct m0_be_io *io)
{
	struct m0_be_reg_d *rd;
	struct m0_be_seg   *seg;

	M0_PRE(m0_be_reg_area__invariant(ra));

	seg = NULL;
	M0_BE_REG_AREA_FORALL(ra, rd) {
		M0_ASSERT(ergo(seg != NULL, seg == rd->rd_reg.br_seg));
		m0_be_io_add(io, rd->rd_buf, m0_be_reg_offset(&rd->rd_reg),
			     rd->rd_reg.br_size);
		seg = rd->rd_reg.br_seg;
		/* XXX temporary hack. FIXME ASAP */
		if (seg != NULL)
			io->bio_stob = seg->bs_stob;
	}

	M0_POST(m0_be_reg_area__invariant(ra));
}

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
