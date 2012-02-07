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
#ifndef __COLIBRI_NET_KLNET_CORE_H__
#define __COLIBRI_NET_KLNET_CORE_H__

/**
   @defgroup KLNetCore LNet Transport Core Kernel Private Interface
   @ingroup LNetCore

   @{
 */

#include "lib/semaphore.h"
#include "lib/tlist.h"
#include "net/lnet/lnet_core.h"

#include <linux/spinlock.h>

enum {
	C2_NET_LNET_KCORE_DOM_MAGIC = 0x4b436f7265446f6dULL, /* KCoreDom */
	C2_NET_LNET_KCORE_TM_MAGIC  = 0x4b436f7265544dULL,   /* KCoreTM */
	C2_NET_LNET_KCORE_TMS_MAGIC = 0x4b436f7265544d73ULL, /* KCoreTMs */
	C2_NET_LNET_KCORE_BUF_MAGIC = 0x4b436f7265427566ULL, /* KCoreBuf */
	C2_NET_LNET_MAX_PORTALS     = 64, /**< Number of portals supported. */
	C2_NET_LNET_EQ_SIZE         = 8,  /**< Size of LNet event queue. */

	/** Portal mask when encoded in hdr_data */
	C2_NET_LNET_PORTAL_MASK     = C2_NET_LNET_BUFFER_ID_MAX,
};

/**
   Kernel domain private data.
   This structure is pointed to by nlx_core_domain::cd_kpvt.
 */
struct nlx_kcore_domain {
	uint64_t                         kd_magic;

	/** ADDB context for events related to this domain */
	struct c2_addb_ctx               kd_addb;
};

/**
   Kernel transfer machine private data.
   This structure is pointed to by c2_lnet_core_transfer_mc::ctm_kpvt.
 */
struct nlx_kcore_transfer_mc {
	uint64_t                         ktm_magic;

	/** Kernel pointer to the shared memory TM structure. */
	struct nlx_core_transfer_mc     *ktm_ctm;

	/** Transfer machine linkage */
	struct c2_tlink                  ktm_tm_linkage;

	/**
	   Spin lock to serialize access to the buffer event queue
	   from the LNet callback subroutine.
	 */
	spinlock_t                       ktm_bevq_lock;

	/** This semaphore increments with each LNet event added. */
	struct c2_semaphore              ktm_sem;

	/** Handle of the LNet EQ associated with this transfer machine */
	lnet_handle_eq_t                 ktm_eqh;

	/** ADDB context for events related to this transfer machine */
	struct c2_addb_ctx               ktm_addb;
};


/**
   Kernel buffer private data.
   This structure is pointed to by c2_lnet_core_buffer::cb_kpvt.
 */
struct nlx_kcore_buffer {
	uint64_t                      kb_magic;

	/** Minimum space remaining for re-use of the receive buffer.
	    The value is set from c2_net_buffer::nb_min_receive_size.
	 */
	c2_bcount_t                   kb_min_recv_size;

	/** Maximum number of messages that may be received in the buffer.
	    The value is set from c2_net_buffer::nb_max_receive_msgs.
	 */
	uint32_t                      kb_max_recv_msgs;

	/** Pointer to the shared memory buffer data. */
	struct nlx_core_buffer       *kb_cb;

	/** Pointer to kernel core TM data. */
	struct nlx_kcore_transfer_mc *kb_ktm;

	/** The LNet I/O vector. */
	lnet_kiov_t                  *kb_kiov;

	/** The number of elements in kb_kiov */
	size_t                        kb_kiov_len;

	/** The length of a kiov vector element is adjusted at runtime to
	    reflect the actual buffer data length under consideration.
	    This field keeps track of the element index adjusted.
	*/
	size_t                        kb_kiov_adj_idx;

	/** The kiov is adjusted at runtime to reflect the actual buffer
	    data length under consideration.
	    This field keeps track of original length.
	*/
	unsigned                      kb_kiov_orig_len;

	/** MD handle */
	lnet_handle_md_t              kb_mdh;

	/** ADDB context for events related to this buffer */
	struct c2_addb_ctx            kb_addb;
};

static bool nlx_kcore_domain_invariant(const struct nlx_kcore_domain *kd);
static bool nlx_kcore_buffer_invariant(const struct nlx_kcore_buffer *kcb);
static bool nlx_kcore_tm_invariant(const struct nlx_kcore_transfer_mc *kctm);
static int nlx_kcore_buffer_kla_to_kiov(struct nlx_kcore_buffer *kb,
					const struct c2_bufvec *bvec);
static bool nlx_kcore_kiov_invariant(const lnet_kiov_t *k, size_t len);

#ifdef PAGE_OFFSET
#undef PAGE_OFFSET
#endif
#define PAGE_OFFSET(addr) ((addr) & ~PAGE_MASK)

/**
   @}
 */

#endif /* __COLIBRI_NET_KLNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
