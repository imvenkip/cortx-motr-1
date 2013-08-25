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
			    addr + M0_BE_SEG_HEADER_OFFSET);
	rc = m0_be_seg__write(&hdr_reg, hdrbuf);
	m0_free(hdrbuf);
	return rc;
}

M0_INTERNAL int m0_be_seg_create(struct m0_be_seg *seg,
				 m0_bcount_t size,
				 void *addr)
{
	M0_PRE(seg->bs_state == M0_BSS_INIT);
	M0_PRE(seg->bs_stob->so_domain != NULL);

	return seg_header_create(seg, addr, size);
}

M0_INTERNAL int m0_be_seg_destroy(struct m0_be_seg *seg)
{
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));

	/* XXX TODO: stob destroy ... */

	return 0;
}

M0_INTERNAL void m0_be_seg_init(struct m0_be_seg *seg,
				struct m0_stob *stob,
				struct m0_be_domain *dom)
{
	*seg = (struct m0_be_seg) {
		.bs_reserved = M0_BE_SEG_HEADER_OFFSET +
			       sizeof(struct m0_be_seg_hdr),
		.bs_domain   = dom,
		.bs_stob     = stob,
		.bs_state    = M0_BSS_INIT,
	};
}

M0_INTERNAL void m0_be_seg_fini(struct m0_be_seg *seg)
{
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));
}

M0_INTERNAL bool m0_be__seg_invariant(const struct m0_be_seg *seg)
{
	return seg != NULL && seg->bs_addr != NULL && seg->bs_size > 0;
}

bool m0_be__reg_invariant(const struct m0_be_reg *reg)
{
	return reg != NULL && reg->br_seg != NULL &&
		reg->br_size > 0 && reg->br_addr != NULL &&
		m0_be_seg_contains(reg->br_seg, reg->br_addr) &&
		m0_be_seg_contains(reg->br_seg,
				   reg->br_addr + reg->br_size - 1);
}

M0_INTERNAL int m0_be_seg_open(struct m0_be_seg *seg)
{
	int                   rc;
	struct m0_be_seg_hdr *hdrbuf;      /* seg hdr buffer */
	struct m0_be_reg      hdr_reg;
	void                 *seg_addr0;
	m0_bcount_t           seg_size;
	void                 *p;

	/* Allocate buffer for segment header. */
	M0_ALLOC_PTR(hdrbuf);
	if (hdrbuf == NULL)
		return -ENOMEM;

	/* Read segment header from storage. */
	seg->bs_addr = (void *) hdrbuf - M0_BE_SEG_HEADER_OFFSET;
	seg->bs_size = sizeof *hdrbuf + M0_BE_SEG_HEADER_OFFSET;
	hdr_reg = M0_BE_REG(seg, sizeof *hdrbuf,
			    seg->bs_addr + M0_BE_SEG_HEADER_OFFSET);
	rc = m0_be_seg__read(&hdr_reg, hdrbuf);
	if (rc == 0) {
		seg_addr0 = hdrbuf->bh_addr;
		seg_size  = hdrbuf->bh_size;
		M0_ASSERT(seg_addr0 != NULL);
	}
	m0_free(hdrbuf);
	if (rc != 0)
		return rc;

	/* mmap an area at bh_addr of bh_size. */
	p = mmap(seg_addr0, seg_size, PROT_READ|PROT_WRITE,
		 MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED)
		return -errno;
	M0_ASSERT(p == seg_addr0);

	/* Read whole segment from storage. */
	seg->bs_size = seg_size;
	seg->bs_addr = seg_addr0;
	rc = m0_be_seg__read(&M0_BE_REG(seg, seg_size, seg_addr0), seg_addr0);
	if (rc == 0) {
		seg->bs_state = M0_BSS_OPENED;
	}
	return rc;
}

M0_INTERNAL void m0_be_seg_close(struct m0_be_seg *seg)
{
	M0_PRE(seg->bs_state == M0_BSS_OPENED);

	munmap(seg->bs_addr, seg->bs_size);
	seg->bs_state = M0_BSS_CLOSED;
}

M0_INTERNAL void m0_be_reg_get(struct m0_be_reg *reg, struct m0_be_op *op)
{
	/* XXX not implemented */
}

M0_INTERNAL void m0_be_reg_get_fast(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
}

M0_INTERNAL void m0_be_reg_put(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
}

M0_INTERNAL bool m0_be__reg_is_pinned(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
	return true;
}

M0_INTERNAL bool m0_be_seg_contains(const struct m0_be_seg *seg, void *addr)
{
	return seg->bs_addr <= addr && addr < seg->bs_addr + seg->bs_size;
}

M0_INTERNAL bool m0_be_reg_eq(const struct m0_be_reg *r1,
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
	return addr - seg->bs_addr;
}

M0_INTERNAL m0_bindex_t m0_be_reg_offset(const struct m0_be_reg *reg)
{
	return m0_be_seg_offset(reg->br_seg, reg->br_addr);
}

static int
be_seg_io(struct m0_be_reg *reg, void *ptr, enum m0_stob_io_opcode opcode)
{
	return m0_be_io_sync(reg->br_seg->bs_stob,
			     ptr, m0_be_reg_offset(reg), reg->br_size, opcode);
}

M0_INTERNAL int m0_be_seg__read(struct m0_be_reg *reg, void *dst)
{
	return be_seg_io(reg, dst, SIO_READ);
}

M0_INTERNAL int m0_be_seg__write(struct m0_be_reg *reg, void *src)
{
	return be_seg_io(reg, src, SIO_WRITE);
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
