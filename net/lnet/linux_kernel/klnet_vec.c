/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 01/05/2012
 */

/* This file is designed to be included in klnet_core.c.
   Logic in this file is liberally borrowed from Isaac Huang's
   ulamod_ubuf2kiov() subroutine in colibri/lnet/lnet/ula/ulamod_mem.c
   (unreleased ULA code).
 */

/**
   @addtogroup KLNetCore
   @{
 */

/**
   Invariant for an LNet compatible KIOV
 */
static bool nlx_kcore_kiov_invariant(const lnet_kiov_t *k, size_t len)
{
	size_t i;

	if (k == NULL || len == 0 || len > LNET_MAX_IOV)
		return false;
	for (i = 0; i < len; ++i, ++k) {
		if (k->kiov_page == NULL || k->kiov_len == 0 ||
		    k->kiov_len > PAGE_SIZE)
			return false;
		if (k->kiov_offset != 0 && i != 0)
			return false; /* permitted only on first */
		if (k->kiov_len != PAGE_SIZE && i != (len - 1) &&
		    k->kiov_offset == 0)
			return false; /* permitted only on last entry */
		if (k->kiov_offset != 0 &&
		    (k->kiov_offset + k->kiov_len) > PAGE_SIZE)
			return false;
	}
	return true;
}

/**
   Count the number of pages in a segment of a c2_bufvec.
   @param bvec Colibri buffer vector pointer
   @param n    Segment number in the vector
   @retval Number of pages in the segment.
 */
static unsigned bufvec_seg_page_count(const struct c2_bufvec *bvec, unsigned n)
{
	c2_bcount_t seg_len;
	void *seg_addr;
	unsigned npages;

	C2_ASSERT(n < bvec->ov_vec.v_nr);
	seg_len  = bvec->ov_vec.v_count[n];
	seg_addr = bvec->ov_buf[n];

	/* npages = last_page_num - first_page_num + 1 */
	npages = (uint64_t)(seg_addr + seg_len - 1) / PAGE_SIZE -
		(uint64_t)seg_addr / PAGE_SIZE + 1;

	return npages;
}

/**
   Fill in a kiov array with pages from a segment of a c2_bufvec containing
   kernel logical addresses.

   The page reference count is not incremented.

   @param bvec Colibri buffer vector pointer
   @param n    Segment number in the vector
   @param kiov Pointer to array of lnet_kiov_t structures.
   @retval Number of lnet_kiov_t elements added.
 */
static unsigned bufvec_seg_kla_to_kiov(const struct c2_bufvec *bvec,
				       unsigned n,
				       lnet_kiov_t *kiov)
{
	unsigned num_pages  = bufvec_seg_page_count(bvec, n);
	c2_bcount_t seg_len = bvec->ov_vec.v_count[n];
	uint64_t seg_addr   = (uint64_t) bvec->ov_buf[n];
	int pnum;

	for (pnum = 0; pnum < num_pages; ++pnum) {
		uint64_t offset = PAGE_OFFSET(seg_addr);
		uint64_t len    = PAGE_SIZE - offset;
		struct page *pg;

		if (len > seg_len)
			len = seg_len;

		pg = virt_to_page(seg_addr); /* KLA only! */
		C2_ASSERT(pg != 0);

		kiov[pnum].kiov_page   = pg;
		kiov[pnum].kiov_len    = len;
		kiov[pnum].kiov_offset = offset;

		/** @todo kmap(seg_addr) with kunmap on deregister? */

		seg_addr += len;
		seg_len  -= len;
	}
	return num_pages;
}

/**
   This subroutine sets up the LNet kernel I/O vector in the kcore buffer from
   a bufvec containing kernel logical addresses.

   The page reference count is not incremented.

   @param kb Kcore buffer private pointer
   @param bufvec Vector with kernel logical addresses.
   @retval -EFBIG if the IO vector is too large.
 */
int nlx_kcore_buffer_kla_to_kiov(struct nlx_kcore_buffer *kb,
				 const struct c2_bufvec *bvec)
{
	unsigned i;
	unsigned num_pages;
	unsigned knum;

	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(kb->kb_kiov == NULL && kb->kb_kiov_len == 0);

	/* compute the number of pages required */
	num_pages = 0;
	for (i = 0; i < bvec->ov_vec.v_nr; ++i)
		num_pages += bufvec_seg_page_count(bvec, i);
	C2_ASSERT(num_pages > 0);
	if (num_pages > LNET_MAX_IOV)
		return -EFBIG;

	/* allocate and fill in the kiov */
	C2_ALLOC_ARR(kb->kb_kiov, num_pages);
	if (kb->kb_kiov == 0)
		return -ENOMEM;
	kb->kb_kiov_len = num_pages;
	knum = 0;
	for (i = 0; i < bvec->ov_vec.v_nr; ++i)
		knum += bufvec_seg_kla_to_kiov(bvec, i, &kb->kb_kiov[knum]);
	C2_POST(knum == num_pages);
	C2_POST(nlx_kcore_kiov_invariant(kb->kb_kiov, kb->kb_kiov_len));

	return 0;
}

/**
   Helper subroutine to determine the number of kiov vector elements are
   required to match a specified byte length.
   @param kiov Pointer to the KIOV vector.
   @param kiov_len Length of the KIOV vector.
   @param bytes The byte count desired.
   @param last_len Returns the number of bytes used in the last kiov
   element.
   @retval uint32_t The number of KIOV entries required.
   @post return value <= kiov_len
 */
static size_t nlx_kcore_num_kiov_entries_for_bytes(const lnet_kiov_t *kiov,
						   size_t kiov_len,
						   c2_bcount_t bytes,
						   unsigned *last_len)
{
	c2_bcount_t count;
	size_t i;

	C2_PRE(kiov != NULL);
	C2_PRE(kiov_len > 0);
	C2_PRE(last_len != NULL);

	for (i = 0, count = 0; i < kiov_len && count < bytes; ++i, ++kiov) {
		count += (c2_bcount_t) kiov->kiov_len;
	}

	C2_POST(i <= kiov_len);
	--kiov;
	*last_len = (kiov->kiov_len - count + bytes) % kiov->kiov_len;
	if (*last_len == 0)
		*last_len = kiov->kiov_len;
#if 0
	NLXP("bytes=%lu n=%lu c=%ld k=%ld l=%u\n", (unsigned long) bytes,
	     (unsigned long) i, (unsigned long) count,
	     (unsigned long) kiov->kiov_len, *last_len);
#endif
	return i;
}


/**
   @}
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
