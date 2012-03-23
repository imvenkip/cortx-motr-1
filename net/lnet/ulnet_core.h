/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 11/01/2011
 *
 */
#ifndef __COLIBRI_NET_ULNET_CORE_H__
#define __COLIBRI_NET_ULNET_CORE_H__

/**
   @defgroup ULNetCore LNet Transport Core User Space Private Interface
   @ingroup LNetCore

   @{
 */

enum {
	C2_NET_LNET_UCORE_DOM_MAGIC = 0x55436f7265446f6dULL, /* UCoreDom */
	C2_NET_LNET_UCORE_TM_MAGIC  = 0x55436f7265544dULL,   /* UCoreTM */
	C2_NET_LNET_UCORE_BUF_MAGIC = 0x55436f7265427566ULL, /* UCoreBuf */
};

/**
   Userspace domain private data.
   This structure is pointed to by nlx_core_domain::cd_upvt.
 */
struct nlx_ucore_domain {
	uint64_t                        ud_magic;
	/** Cached maximum buffer size (counting all segments). */
	c2_bcount_t                     ud_max_buffer_size;
	/** Cached maximum size of a buffer segment. */
	c2_bcount_t                     ud_max_buffer_segment_size;
	/** Cached maximum number of buffer segments. */
	int32_t                         ud_max_buffer_segments;
	/** File descriptor to the kernel device. */
	int                             ud_fd;
	/** ADDB context for events related to this domain. */
	struct c2_addb_ctx              ud_addb;
};

/**
   Userspace transfer machine private data.
   This structure is pointed to by nlx_core_transfer_mc::ctm_upvt.
 */
struct nlx_ucore_transfer_mc {
	uint64_t                        utm_magic;
	/** ADDB context for events related to this transfer machine. */
	struct c2_addb_ctx              utm_addb;
};

/**
   Userspace buffer private data.
   This structure is pointed to by nlx_core_buffer::cb_upvt.
 */
struct nlx_ucore_buffer {
	uint64_t                        ub_magic;
	/** ADDB context for events related to this buffer. */
	struct c2_addb_ctx              ub_addb;
};

/** @} */ /* ULNetCore */

#endif /* __COLIBRI_NET_ULNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
