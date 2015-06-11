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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/seg.h"

#include "lib/misc.h"         /* M0_IN */
#include "lib/memory.h"       /* m0_alloc_aligned */
#include "lib/errno.h"        /* ENOMEM */
#include "lib/time.h"         /* m0_time_now */
#include "lib/atomic.h"       /* m0_atomic64 */

#include "stob/stob.h"	      /* m0_stob */
#include "stob/linux.h"       /* m0_stob_linux_container */

#include "be/seg_internal.h"  /* m0_be_seg_hdr */
#include "be/tx_regmap.h"     /* m0_be_reg_area */
#include "be/io.h"	      /* m0_be_io */

#include <sys/mman.h>         /* mmap */
#include <search.h>           /* twalk */

/**
 * @addtogroup be
 *
 * @{
 */

M0_INTERNAL int m0_be_seg_create(struct m0_be_seg *seg,
				 m0_bcount_t size,
				 void *addr)
{
	struct m0_be_seg_hdr hdr;
	unsigned char        last_byte;
	int                  rc;

	M0_ENTRY("seg=%p size=%lu addr=%p", seg, size, addr);

	M0_PRE(seg->bs_state == M0_BSS_INIT);
	M0_PRE(seg->bs_stob->so_domain != NULL);
	M0_PRE(addr != NULL);
	M0_PRE(size > 0);

	hdr = (struct m0_be_seg_hdr) {
		.bh_addr = addr,
		.bh_size = size,
	};
	rc = m0_be_io_single(seg->bs_stob, SIO_WRITE,
			     &hdr, M0_BE_SEG_HEADER_OFFSET, sizeof hdr);
	if (rc == 0) {
		/*
		 * Write the last byte on the backing store.
		 *
		 * mmap() will have problem with regular file mapping otherwise.
		 * Also checks that device (if used as backing storage) has
		 * enough size to be used as segment backing store.
		 */
		last_byte = 0;
		rc = m0_be_io_single(seg->bs_stob, SIO_WRITE,
				     &last_byte, size - 1, sizeof last_byte);
		if (rc != 0)
			M0_LOG(M0_WARN, "can't write segment's last byte");
	} else {
		M0_LOG(M0_WARN, "can't write segment header");
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_seg_destroy(struct m0_be_seg *seg)
{
	M0_ENTRY("seg=%p", seg);
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));

	/* XXX TODO: seg destroy ... */

	return M0_RC(0);
}

M0_INTERNAL void m0_be_seg_init(struct m0_be_seg *seg,
				struct m0_stob *stob,
				struct m0_be_domain *dom)
{
	M0_ENTRY("seg=%p", seg);
	*seg = (struct m0_be_seg) {
		.bs_reserved = M0_BE_SEG_HEADER_OFFSET +
			       sizeof(struct m0_be_seg_hdr),
		.bs_domain   = dom,
		.bs_stob     = stob,
		.bs_state    = M0_BSS_INIT,
		.bs_id       = m0_stob_fid_get(stob)->f_key,
	};
	m0_stob_get(seg->bs_stob);
	M0_LEAVE();
}

M0_INTERNAL void m0_be_seg_fini(struct m0_be_seg *seg)
{
	M0_ENTRY("seg=%p", seg);
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));
	m0_stob_put(seg->bs_stob);
	M0_LEAVE();
}

M0_INTERNAL bool m0_be_seg__invariant(const struct m0_be_seg *seg)
{
	return _0C(seg != NULL) &&
	       _0C(seg->bs_addr != NULL) &&
	       _0C(seg->bs_size > 0);
}

bool m0_be_reg__invariant(const struct m0_be_reg *reg)
{
	return _0C(reg != NULL) && _0C(reg->br_seg != NULL) &&
	       _0C(reg->br_size > 0) && _0C(reg->br_addr != NULL) &&
	       _0C(m0_be_seg_contains(reg->br_seg, reg->br_addr)) &&
	       _0C(m0_be_seg_contains(reg->br_seg,
				      reg->br_addr + reg->br_size - 1));
}

/* XXX temporary */
/* static */ int be_seg_read_all(struct m0_be_seg     *seg,
                                 struct m0_be_seg_hdr *hdr)
{
	m0_bindex_t pos;
	m0_bcount_t size;
	int         rc;

	for (pos = 0; pos < hdr->bh_size; pos += M0_BE_SEG_READ_SIZE_MAX) {
		size = min64(hdr->bh_size, M0_BE_SEG_READ_SIZE_MAX);
		rc = m0_be_io_single(seg->bs_stob, SIO_READ,
				     hdr->bh_addr + pos, pos, size);
		if (rc != 0)
			return M0_RC(rc);
	}
	return 0;
}

M0_INTERNAL int m0_be_seg_open(struct m0_be_seg *seg)
{
	struct m0_be_seg_hdr  hdr;
	void                 *p;
	int                   rc;
	int                   fd;

	M0_ENTRY("seg=%p", seg);
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));

	rc = m0_be_io_single(seg->bs_stob, SIO_READ,
			     &hdr, M0_BE_SEG_HEADER_OFFSET, sizeof hdr);
	if (rc != 0)
		return M0_RC(rc);
	/* XXX check for magic */

	fd = m0_stob_linux_container(seg->bs_stob)->sl_fd;
	p = mmap(hdr.bh_addr, hdr.bh_size, PROT_READ | PROT_WRITE,
		 MAP_FIXED | MAP_PRIVATE | MAP_NORESERVE, fd, 0);
	if (p != hdr.bh_addr)
		return M0_RC(-errno);

	/* rc = be_seg_read_all(seg, &hdr); */
	rc = 0;
	if (rc == 0) {
		seg->bs_size  = hdr.bh_size;
		seg->bs_addr  = hdr.bh_addr;
		seg->bs_state = M0_BSS_OPENED;
	} else {
		munmap(hdr.bh_addr, hdr.bh_size);
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_be_seg_close(struct m0_be_seg *seg)
{
	M0_ENTRY("seg=%p", seg);
	M0_PRE(seg->bs_state == M0_BSS_OPENED);

	munmap(seg->bs_addr, seg->bs_size);
	seg->bs_state = M0_BSS_CLOSED;
	M0_LEAVE();
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

M0_INTERNAL bool m0_be_reg__is_pinned(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
	return true;
}

M0_INTERNAL bool m0_be_seg_contains(const struct m0_be_seg *seg,
				    const void *addr)
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
					 const void *addr)
{
	M0_PRE(m0_be_seg_contains(seg, addr));
	return addr - seg->bs_addr;
}

M0_INTERNAL m0_bindex_t m0_be_reg_offset(const struct m0_be_reg *reg)
{
	return m0_be_seg_offset(reg->br_seg, reg->br_addr);
}

M0_INTERNAL m0_bcount_t m0_be_seg_reserved(const struct m0_be_seg *seg)
{
	return seg->bs_reserved;
}

M0_INTERNAL struct m0_be_allocator *m0_be_seg_allocator(struct m0_be_seg *seg)
{
	return &seg->bs_allocator;
}

static int
be_seg_io(struct m0_be_reg *reg, void *ptr, enum m0_stob_io_opcode opcode)
{
	return m0_be_io_single(reg->br_seg->bs_stob, opcode,
			       ptr, m0_be_reg_offset(reg), reg->br_size);
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

/* #define USE_TIME_NOW_AS_GEN_IDX */

M0_INTERNAL unsigned long m0_be_reg_gen_idx(const struct m0_be_reg *reg)
{
#ifdef USE_TIME_NOW_AS_GEN_IDX
	m0_time_t now = m0_time_now();

	M0_CASSERT(sizeof now == sizeof(unsigned long));
	return now;
#else
	static struct m0_atomic64 global_gen_idx = {};

	M0_CASSERT(sizeof global_gen_idx == sizeof(unsigned long));
	return m0_atomic64_add_return(&global_gen_idx, 1);
#endif
}

/** @} end of be group */
#undef M0_TRACE_SUBSYSTEM

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
