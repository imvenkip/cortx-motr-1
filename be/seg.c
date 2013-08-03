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

#include "be/seg.h"

#include "lib/misc.h"         /* M0_IN */
#include "lib/memory.h"       /* m0_alloc_aligned */
#include "lib/errno.h"        /* ENOMEM */

#include "be/seg_internal.h"  /* m0_be_seg_hdr */
#include "be/be.h"            /* m0_be_op */
#include "be/tx_regmap.h"     /* m0_be_reg_area */
#include "be/io.h"	      /* m0_be_io */

#include <sys/mman.h>         /* mmap */
#include <search.h>           /* twalk */

/**
 * @addtogroup be
 *
 * @{
 */

#define BE_SEG_DEFAULT_ADDR   ((void *)0x400000000000)
#define BE_SEG_HEADER_OFFSET  (0ULL)

static int
seg_header_create(struct m0_be_seg *seg, void *addr, m0_bcount_t size)
{
	struct m0_be_seg_hdr *hdrbuf;
	struct m0_be_reg      hdr_reg;
	int                   rc;

	M0_PRE(addr != NULL);
	M0_PRE(size > 0);

	M0_ALLOC_PTR(hdrbuf);
	if (hdrbuf == NULL)
		return -ENOMEM;

	hdrbuf->bh_addr = addr;
	hdrbuf->bh_size = size;
	seg->bs_addr = addr;
	seg->bs_size = size;

	hdr_reg = M0_BE_REG(seg, sizeof *hdrbuf,
			    (char *) addr + BE_SEG_HEADER_OFFSET);
	rc = m0_be_seg__write(&hdr_reg, hdrbuf);
	m0_free(hdrbuf);
	return rc;
}

M0_INTERNAL int m0_be_seg_create(struct m0_be_seg *seg, m0_bcount_t size)
{
	M0_PRE(seg->bs_state == M0_BSS_INIT);
	M0_PRE(seg->bs_stob->so_domain != NULL);
	M0_PRE(seg->bs_stob->so_state != CSS_EXISTS);

	return m0_stob_find(seg->bs_stob->so_domain,
			    &seg->bs_stob->so_id, &seg->bs_stob) ?:
		m0_stob_create(seg->bs_stob, NULL) ?:
		seg_header_create(seg, BE_SEG_DEFAULT_ADDR, size);
}

M0_INTERNAL int m0_be_seg_destroy(struct m0_be_seg *seg)
{
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));

	m0_stob_put(seg->bs_stob);

	/* XXX TODO: stob destroy ... */

	return 0;
}

M0_INTERNAL void
m0_be_seg_init(struct m0_be_seg *seg, struct m0_stob *stob, struct m0_be *be)
{
	*seg = (struct m0_be_seg) {
		.bs_reserved = sizeof(struct m0_be_seg_hdr),
		.bs_be	     = be,
		.bs_stob     = stob,
		.bs_state    = M0_BSS_INIT,
		.bs_pgshift  = 12
	};
}

M0_INTERNAL void m0_be_seg_fini(struct m0_be_seg *seg)
{
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));
}

M0_INTERNAL bool m0_be__seg_invariant(const struct m0_be_seg *seg)
{
	return seg != NULL && seg->bs_addr != NULL && seg->bs_size > 0
		&& seg->bs_pgmap != NULL && seg->bs_pgnr > 0;
}

bool m0_be__reg_invariant(const struct m0_be_reg *reg)
{
	return reg != NULL && reg->br_seg != NULL &&
		reg->br_size > 0 && reg->br_addr != NULL &&
		m0_be_seg_contains(reg->br_seg, reg->br_addr) &&
		m0_be_seg_contains(reg->br_seg,
				   (char *) reg->br_addr + reg->br_size - 1);
}

M0_INTERNAL int m0_be_seg_open(struct m0_be_seg *seg)
{
	int                   rc;
	struct m0_be_seg_hdr *hdrbuf;      /* seg hdr buffer */
	struct m0_be_reg      hdr_reg;
	void                 *seg_addr0;
	m0_bcount_t           seg_size;
	m0_bcount_t           i;
	void                 *p;

	/* Allocate buffer for segment header. */
	seg->bs_bshift = seg->bs_stob->so_op->sop_block_shift(seg->bs_stob);
	if (seg->bs_pgshift < seg->bs_bshift) {
		seg->bs_pgshift = seg->bs_bshift;
	}
	M0_ALLOC_PTR(hdrbuf);
	if (hdrbuf == NULL)
		return -ENOMEM;

	/* Read segment header from storage. */
	seg->bs_addr = BE_SEG_DEFAULT_ADDR;
	/* XXX */
	seg->bs_size = 1ULL << 48;
	hdr_reg = M0_BE_REG(seg, sizeof *hdrbuf,
			    (char *)BE_SEG_DEFAULT_ADDR + BE_SEG_HEADER_OFFSET);
	rc = m0_be_seg__read(&hdr_reg, hdrbuf);
	if (rc == 0) {
		seg_addr0 = hdrbuf->bh_addr;
		seg_size  = hdrbuf->bh_size;
		M0_ASSERT(seg_addr0 != NULL);
		M0_ASSERT(m0_addr_is_aligned(seg_addr0, seg->bs_bshift));
	}
	m0_free(hdrbuf);
	if (rc != 0)
		return rc;

	/* Allocate page map. */
	seg->bs_pgnr = (seg_size + (1 << seg->bs_pgshift) - 1) >>
		seg->bs_pgshift;
	seg->bs_pgmap = m0_alloc_aligned(sizeof(m0_bcount_t) * seg->bs_pgnr, 3);
	if (seg->bs_pgmap == NULL)
		return -ENOMEM;
	for (i = 0; i < seg->bs_pgnr; i++)
		seg->bs_pgmap[i] = 0; /* not present yet */

	/* mmap an area at bh_addr of bh_size. */
	p = mmap(seg_addr0, seg_size, PROT_READ|PROT_WRITE,
		 MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED)
		return -errno;
	M0_ASSERT(p == seg_addr0);

	/* Read whole segment from storage. */
	rc = m0_be_seg__read(&M0_BE_REG(seg, seg_size, seg_addr0), seg_addr0);
	if (rc == 0) {
		seg->bs_state = M0_BSS_OPENED;
		for (i = 0; i < seg->bs_pgnr; i++)
			seg->bs_pgmap[i] |= M0_BE_SEG_PG_PRESENT;
		seg->bs_size = seg_size;
		seg->bs_addr = seg_addr0;
	}
	return rc;
}

M0_INTERNAL void m0_be_seg_close(struct m0_be_seg *seg)
{
	M0_PRE(seg->bs_state == M0_BSS_OPENED);

	m0_free(seg->bs_pgmap);
	munmap(seg->bs_addr, seg->bs_size);
	seg->bs_state = M0_BSS_CLOSED;
}

static inline m0_bcount_t be_seg_pgno(const struct m0_be_seg *seg, void *addr)
{
	return (addr - seg->bs_addr) >> seg->bs_pgshift;
}

static inline m0_bcount_t be_seg_blkno(const struct m0_be_seg *seg, void *addr)
{
	return (addr - seg->bs_addr) >> seg->bs_bshift;
}

/** @todo XXX replace it. copy-pasted from above */
static void iovec_prepare2(struct m0_be_seg *seg, struct m0_be_reg_area *area,
			   struct m0_indexvec *iv, struct m0_bufvec *bv)
{
	struct m0_be_reg_d *rd;
	int		    nr;
	int		    i;

	M0_PRE(seg->bs_bshift == 0);

	nr = m0_be_regmap_size(&area->bra_map);

	M0_ALLOC_ARR(bv->ov_vec.v_count, nr);
	M0_ALLOC_ARR(bv->ov_buf, nr);

	M0_ALLOC_ARR(iv->iv_vec.v_count, nr);
	M0_ALLOC_ARR(iv->iv_index, nr);

	M0_ASSERT(bv->ov_vec.v_count != NULL && bv->ov_buf   != NULL);
	M0_ASSERT(iv->iv_vec.v_count != NULL && iv->iv_index != NULL);

	bv->ov_vec.v_nr = nr;
	iv->iv_vec.v_nr = nr;

	i = 0;
	for (rd = m0_be_reg_area_first(area); rd != NULL;
	     rd = m0_be_reg_area_next(area, rd)) {
		bv->ov_vec.v_count[i] = rd->rd_reg.br_size;
		bv->ov_buf[i]	      = rd->rd_buf;

		iv->iv_vec.v_count[i] = rd->rd_reg.br_size;
		iv->iv_index[i]       = be_seg_blkno(seg, rd->rd_reg.br_addr);
		++i;
	}
	M0_ASSERT(i == nr);
}

static bool be_seg_stobio_cb(struct m0_clink *link)
{
	struct m0_be_op   *op = container_of(link, struct m0_be_op,
					     bo_u.u_segio.si_clink);
	struct m0_stob_io *io = &op->bo_u.u_segio.si_stobio;

/* XXX: This probably should be deleted, but most likely in m0_be_op_fini() or
   ->sd_in() of M0_BOS_SUCCESS | M0_BOS_FAILURE states...
 */
/*        m0_clink_del_lock(link); */
/*        m0_clink_fini(link); */
/*        m0_stob_io_fini(io); */

	op->bo_sm.sm_rc = io->si_rc;
	m0_be_op_state_set(op, io->si_rc == 0 ? M0_BOS_SUCCESS : M0_BOS_FAILURE);

	return io->si_rc == 0;
}

M0_INTERNAL void m0_be_seg_write_simple(struct m0_be_seg *seg,
					struct m0_be_op *op,
					struct m0_be_reg_area *area)
{
	struct m0_stob_io *io    = &op->bo_u.u_segio.si_stobio;
	struct m0_clink   *clink = &op->bo_u.u_segio.si_clink;
	int                rc;

	/* Set up op, clink and io structs for SEGIO write. */
	op->bo_utype = M0_BOP_SEGIO;
	m0_clink_init(clink, &be_seg_stobio_cb);

	m0_stob_io_init(io);
	io->si_flags        = 0;
	io->si_opcode       = SIO_WRITE;
	io->si_fol_rec_part = (void *)1;

	iovec_prepare2(seg, area, &io->si_stob, &io->si_user);

	m0_clink_add_lock(&io->si_wait, clink);
	m0_be_op_state_set(op, M0_BOS_ACTIVE);
	rc = m0_stob_io_launch(io, seg->bs_stob, NULL, NULL);
}

M0_INTERNAL void m0_be_reg_get(struct m0_be_reg *reg, struct m0_be_op *op)
{
	m0_bcount_t n;

	M0_PRE(m0_be__reg_invariant(reg) && op != NULL);

	op->bo_utype   = M0_BOP_REG;
	op->bo_u.u_reg = *reg;
	m0_sm_state_set(&op->bo_sm, M0_BOS_ACTIVE);

	for (n = be_seg_pgno(reg->br_seg, reg->br_addr);
	     n <= be_seg_pgno(reg->br_seg, reg->br_addr + reg->br_size); n++) {
		/* XXX: launch stobio if page is not present. */
		M0_ASSERT(reg->br_seg->bs_pgmap[n] & M0_BE_SEG_PG_PRESENT);
		reg->br_seg->bs_pgmap[n]++;
	}

	m0_sm_state_set(&op->bo_sm, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_reg_get_fast(const struct m0_be_reg *reg)
{
	m0_bcount_t n;

	M0_PRE(m0_be__reg_invariant(reg) && m0_be__reg_is_pinned(reg));

	for (n = be_seg_pgno(reg->br_seg, reg->br_addr);
	     n <= be_seg_pgno(reg->br_seg, reg->br_addr + reg->br_size); n++) {
		M0_ASSERT(reg->br_seg->bs_pgmap[n] & M0_BE_SEG_PG_PRESENT);
		reg->br_seg->bs_pgmap[n]++;
	}
}

M0_INTERNAL void m0_be_reg_put(const struct m0_be_reg *reg)
{
	m0_bcount_t n;

	M0_PRE(m0_be__reg_invariant(reg) && m0_be__reg_is_pinned(reg));

	for (n = be_seg_pgno(reg->br_seg, reg->br_addr);
	     n <= be_seg_pgno(reg->br_seg, reg->br_addr + reg->br_size); n++) {
		M0_ASSERT(reg->br_seg->bs_pgmap[n] & M0_BE_SEG_PG_PRESENT);
		M0_ASSERT((reg->br_seg->bs_pgmap[n] &
			   M0_BE_SEG_PG_PIN_CNT_MASK) > 0);
		reg->br_seg->bs_pgmap[n]--;
	}
}

M0_INTERNAL bool m0_be__reg_is_pinned(const struct m0_be_reg *reg)
{
	m0_bcount_t n;

	M0_PRE(m0_be__reg_invariant(reg));

	for (n = be_seg_pgno(reg->br_seg, reg->br_addr);
	     n <= be_seg_pgno(reg->br_seg, reg->br_addr + reg->br_size); n++) {
		if (! (reg->br_seg->bs_pgmap[n] & M0_BE_SEG_PG_PRESENT)
		    || (reg->br_seg->bs_pgmap[n] &
			M0_BE_SEG_PG_PIN_CNT_MASK) == 0)
			return false;
	}
	return true;
}

M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg, const char *name,
				      void **out)
{
	*out = NULL;
	return -ENOSYS;
}

M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg, const char *name,
				      void *value)
{
	return -ENOSYS;
}

M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg, const char *name)
{
	return -ENOSYS;
}

M0_INTERNAL bool m0_be_seg_contains(const struct m0_be_seg *seg, void *addr)
{
	return seg->bs_addr <= addr && addr < seg->bs_addr + seg->bs_size;
}

M0_INTERNAL bool m0_be_reg_is_eq(const struct m0_be_reg *r1,
				 const struct m0_be_reg *r2)
{
	return r1->br_seg == r2->br_seg &&
	       r1->br_size == r2->br_size &&
	       r1->br_addr == r2->br_addr;
}

M0_INTERNAL m0_bindex_t m0_be_seg_offset(const struct m0_be_seg *seg,
					 void *addr)
{
	M0_PRE(m0_be_seg_contains(seg, addr));
	return (char *) addr - (char *) seg->bs_addr;
}

M0_INTERNAL m0_bindex_t m0_be_reg_offset(const struct m0_be_reg *reg)
{
	return m0_be_seg_offset(reg->br_seg, reg->br_addr);
}

static int
be_seg__io(struct m0_be_reg *reg, void *ptr, enum m0_stob_io_opcode opcode)
{
	struct m0_be_io io;
	struct m0_be_op op;
	int             rc;

	M0_PRE(m0_be__reg_invariant(reg));

	rc = m0_be_io_init(&io, reg->br_seg->bs_stob,
			   &M0_BE_TX_CREDIT(1, reg->br_size));
	if (rc != 0)
		return rc;

	m0_be_io_add(&io, ptr, m0_be_reg_offset(reg), reg->br_size);
	m0_be_io_configure(&io, opcode);

	m0_be_op_init(&op);
	rc = m0_be_io_launch(&io, &op);
	if (rc == 0) {
		m0_be_op_wait(&op);
		M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	}
	m0_be_op_fini(&op);

	m0_be_io_fini(&io);
	return rc;
}

M0_INTERNAL int m0_be_seg__read(struct m0_be_reg *reg, void *dst)
{
	return be_seg__io(reg, dst, SIO_READ);
}

M0_INTERNAL int m0_be_seg__write(struct m0_be_reg *reg, void *src)
{
	return be_seg__io(reg, src, SIO_WRITE);
}

M0_INTERNAL int m0_be_reg__read(struct m0_be_reg *reg)
{
	return m0_be_seg__read(reg, reg->br_addr);
}

M0_INTERNAL int m0_be_reg__write(struct m0_be_reg *reg)
{
	return m0_be_seg__write(reg, reg->br_addr);
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
