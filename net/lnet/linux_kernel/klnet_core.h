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

struct nlx_kcore_ops;

/**
   Kernel domain private data.
   This structure is pointed to by nlx_core_domain::cd_kpvt.
 */
struct nlx_kcore_domain {
	uint64_t                      kd_magic;

	/** Kernel pointer to the shared memory domain structure. */
	struct nlx_core_domain       *kd_cd;

	/** Page pinned by driver corresponding to this object. */
	struct page                  *kd_drv_page;

	/** Offset of the object in the page. */
	uint32_t                      kd_drv_offset;

	/** Synchronize access to driver resources for this domain. */
	struct c2_mutex               kd_drv_mutex;

	/** Tracks operations in progress while the mutex is not held. */
	uint32_t                      kd_drv_inuse;

	/** Operations object driver uses to call kernel core operations. */
	struct nlx_kcore_ops         *kd_drv_ops;

	/**
	   User space transfer machines in this domain tracked by the driver.
	   This list links through nlx_kcore_transfer_mc::ktm_drv_linkage.
	 */
	struct c2_tl                  kd_drv_tms;

	/**
	   User space buffers in this domain tracked by the driver.
	   This list links through nlx_kcore_buffer::kb_drv_linkage.
	 */
	struct c2_tl                  kd_drv_buffers;

	/** ADDB context for events related to this domain */
	struct c2_addb_ctx            kd_addb;
};

/**
   Kernel core operations.
   The operations listed here implement the common code shared by both
   the core API implemented in the kernel and the core API support provided
   by the LNet transport driver.
   @todo add additional operations to this structure as needed
 */
struct nlx_kcore_ops {
	/**
	   Performs kernel core tasks related to starting a transfer machine.
	   Internally this results in the creation of the LNet EQ associated
	   with the transfer machine.
	   @param kd kernel domain for this transfer machine
	   @param lctm The transfer machine private data to be initialized.
	   The nlx_core_transfer_mc::ctm_addr must be set by the caller.  If the
	   lcpea_tmid field value is C2_NET_LNET_TMID_INVALID then a transfer
	   machine identifier is dynamically assigned to the transfer machine
	   and the nlx_core_transfer_mc::ctm_addr is modified in place.
	 */
	int (*ko_tm_start)(struct nlx_kcore_domain *kd,
			   struct nlx_core_transfer_mc *lctm);

	/**
	   Performs kernel core tasks relating to stopping a transfer machine.
	   Kernel resources are released.
	 */
	void (*ko_tm_stop)(struct nlx_kcore_domain *kd,
			   struct nlx_core_transfer_mc *lctm);
};

/**
   Kernel transfer machine private data.
   This structure is pointed to by nlx_core_transfer_mc::ctm_kpvt.
 */
struct nlx_kcore_transfer_mc {
	uint64_t                      ktm_magic;

	/** Kernel pointer to the shared memory TM structure, when mapped. */
	struct nlx_core_transfer_mc  *ktm_ctm;

	/** Transfer machine linkage of all TMs. */
	struct c2_tlink               ktm_tm_linkage;

	/** Transfer machine linkage of TMs tracked by driver, per domain. */
	struct c2_tlink               ktm_drv_linkage;

	/** Page pinned by driver corresponding to this object. */
	struct page                  *ktm_drv_page;

	/** Offset of the object in the page. */
	uint32_t                      ktm_drv_offset;

	/**
	   User space buffer events in this transfer tracked by the driver.
	   This list links through nlx_kcore_buffer_event::kbe_drv_linkage.
	 */
	struct c2_tl                  ktm_drv_bevs;

	/**
	   Spin lock to serialize access to the buffer event queue
	   from the LNet callback subroutine.
	 */
	spinlock_t                    ktm_bevq_lock;

	/** This semaphore increments with each LNet event added. */
	struct c2_semaphore           ktm_sem;

	/** Handle of the LNet EQ associated with this transfer machine. */
	lnet_handle_eq_t              ktm_eqh;

	/** ADDB context for events related to this transfer machine. */
	struct c2_addb_ctx            ktm_addb;
};


/**
   Kernel buffer private data.
   This structure is pointed to by nlx_core_buffer::cb_kpvt.
 */
struct nlx_kcore_buffer {
	uint64_t                      kb_magic;

	/** Linkage of buffers tracked by driver, per domain. */
	struct c2_tlink               kb_drv_linkage;

	/** Page pinned by driver corresponding to this object. */
	struct page                  *kb_drv_page;

	/** Offset of the object in the page. */
	uint32_t                      kb_drv_offset;

	/** Pointer to the shared memory buffer data, when mapped. */
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

	/** Track the receipt of an out-of-order REPLY */
	bool                          kb_ooo_reply;

	/** The saved mlength value in the case of an out-of-order
	    REPLY/SEND event sequence.
	*/
	unsigned                      kb_ooo_mlength;

	/** The saved status value in the case of an out-of-order
	    REPLY/SEND event sequence.
	*/
	int                           kb_ooo_status;

	/** The saved offset value in the case of an out-of-order
	    REPLY/SEND event sequence.
	*/
	unsigned                      kb_ooo_offset;

	/** ADDB context for events related to this buffer */
	struct c2_addb_ctx            kb_addb;
};

/**
   Kernel buffer event private data.
   This structure is only used by the driver and links together
   all of the buffer event objects blessed in the domain.
 */
struct nlx_kcore_buffer_event {
	/** Pointer to the shared memory buffer event data, when mapped. */
	struct nlx_core_buffer_event *kbe_bev;

	/** Linkage of buffer events tracked by driver, per TM. */
	struct c2_tlink               kbe_drv_linkage;

	/** Page pinned by driver corresponding to this object. */
	struct page                  *kbe_drv_page;

	/** Offset of the object in the page. */
	uint32_t                      kbe_drv_offset;
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

/** @} */ /* KLNetCore */

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
