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

#include "lib/semaphore.h"
#include "lib/tlist.h"
#include "net/lnet/lnet_core.h"

#include <linux/spinlock.h>

/**
   @defgroup KLNetCore LNet Transport Core Kernel Private Interface
   @ingroup LNetCore

   @{
 */

enum {
	C2_NET_LNET_KCORE_DOM_MAGIC = 0x4b436f7265446f6dULL, /* KCoreDom */
	C2_NET_LNET_KCORE_TM_MAGIC  = 0x4b436f7265544dULL,   /* KCoreTM */
	C2_NET_LNET_KCORE_TMS_MAGIC = 0x4b436f7265544d73ULL, /* KCoreTMs */
	C2_NET_LNET_KCORE_BUF_MAGIC = 0x4b436f7265427566ULL, /* KCoreBuf */
	C2_NET_LNET_KCORE_BEV_MAGIC = 0x4b436f7265426576ULL, /* KCoreBev */
	C2_NET_LNET_DEV_TMS_MAGIC   = 0x4b446576544d73ULL,   /* KDevTMs */
	C2_NET_LNET_DEV_BUFS_MAGIC  = 0x4b44657642756673ULL, /* KDevBufs */
	C2_NET_LNET_DEV_BEVS_MAGIC  = 0x4b44657642657673ULL, /* KDevBevs */
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

	/** Reference to the shared memory nlx_core_domain structure. */
	struct nlx_core_kmem_loc      kd_cd_loc;

	/** Synchronize access to driver resources for this domain. */
	struct c2_mutex               kd_drv_mutex;

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
	struct c2_tl                  kd_drv_bufs;

	/** ADDB context for events related to this domain */
	struct c2_addb_ctx            kd_addb;
};

/**
   Kernel transfer machine private data.
   This structure is pointed to by nlx_core_transfer_mc::ctm_kpvt.
 */
struct nlx_kcore_transfer_mc {
	uint64_t                      ktm_magic;

	/** Reference to the shared memory nlx_core_transfer_mc structure. */
	struct nlx_core_kmem_loc      ktm_ctm_loc;

	/** Transfer machine linkage of all TMs. */
	struct c2_tlink               ktm_tm_linkage;

	/** Transfer machine linkage of TMs tracked by driver, per domain. */
	struct c2_tlink               ktm_drv_linkage;

	/**
	   User space buffer events in this transfer tracked by the driver.
	   This list links through nlx_kcore_buffer_event::kbe_drv_linkage.
	 */
	struct c2_tl                  ktm_drv_bevs;

	/** The transfer machine address. */
	struct nlx_core_ep_addr       ktm_addr;

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

	unsigned                   _debug_;
};

/**
   Kernel buffer private data.
   This structure is pointed to by nlx_core_buffer::cb_kpvt.
 */
struct nlx_kcore_buffer {
	uint64_t                      kb_magic;

	/** Reference to the shared memory nlx_core_buffer structure. */
	struct nlx_core_kmem_loc      kb_cb_loc;

	/** Pointer to kernel core TM data. */
	struct nlx_kcore_transfer_mc *kb_ktm;

	/** Linkage of buffers tracked by driver, per domain. */
	struct c2_tlink               kb_drv_linkage;

	/**
	   The address of the c2_net_buffer structure in the transport address
	   space. The value is set by the nlx_kcore_buffer_register()
	   subroutine.
	 */
	nlx_core_opaque_ptr_t         kb_buffer_id;

	/**
	   The buffer queue type - copied from nlx_core_buffer::cb_qtype
	   when a buffer operation is initiated.
	 */
        enum c2_net_queue_type        kb_qtype;

	/** Time at which a buffer operation is initiated. */
	c2_time_t                     kb_add_time;

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
	uint64_t                      kbe_magic;

	/** Reference to the shared memory nlx_core_buffer_event structure. */
	struct nlx_core_kmem_loc      kbe_bev_loc;

	/** Linkage of buffer events tracked by driver, per TM. */
	struct c2_tlink               kbe_drv_linkage;
};

/**
   Performs a kernel core tranfer machine buffer queue send or receive
   operation (message, active, or passive).
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
typedef int (*nlx_kcore_queue_op_t)(struct nlx_kcore_transfer_mc *ktm,
				    struct nlx_core_buffer *cb,
				    struct nlx_kcore_buffer *kb);


/**
   Kernel core operations.
   The operations listed here implement the common code shared by both
   the core API implemented in the kernel and the core API support provided
   by the LNet transport driver.
 */
struct nlx_kcore_ops {
	/**
	   Initializes the core private data given a previously initialized
	   kernel core private data object.
	   @param kd Kernel core private data pointer.
	   @param cd Core private data pointer.
	 */
	int (*ko_dom_init)(struct nlx_kcore_domain *kd,
			   struct nlx_core_domain *cd);
	/**
	   Finilizes the core private data associated with a kernel core
	   private data object.
	   @param kd Kernel core private data pointer.
	   @param cd Core private data pointer.
	 */
	void (*ko_dom_fini)(struct nlx_kcore_domain *kd,
			    struct nlx_core_domain *cd);

	/**
	   Performs common kernel core tasks related to registering a network
	   buffer.  The nlx_kcore_buffer::kb_kiov is @b not set.
	   @param kd Kernel core private domain pointer.
	   @param buffer_id Value to set in the cb_buffer_id field.
	   @param cb The core private data pointer for the buffer.
	   @param kb Kernel core private buffer pointer.
	 */
	int (*ko_buf_register)(struct nlx_kcore_domain *kd,
			       nlx_core_opaque_ptr_t buffer_id,
			       struct nlx_core_buffer *cb,
			       struct nlx_kcore_buffer *kb);

	/**
	   Performs common kernel core tasks related to de-registering a buffer.
	   @param cb The core private data pointer for the buffer.
	   @param kb Kernel core private buffer pointer.
	 */
	void (*ko_buf_deregister)(struct nlx_core_buffer *cb,
				  struct nlx_kcore_buffer *kb);

	/**
	   Performs kernel core tasks related to starting a transfer machine.
	   Internally this results in the creation of the LNet EQ associated
	   with the transfer machine.
	   @param kd The kernel domain for this transfer machine.
	   @param ctm The transfer machine private data to be initialized.
	   The nlx_core_transfer_mc::ctm_addr must be set by the caller.  If the
	   lcpea_tmid field value is C2_NET_LNET_TMID_INVALID then a transfer
	   machine identifier is dynamically assigned to the transfer machine
	   and the nlx_core_transfer_mc::ctm_addr is modified in place.
	   @param ktm The kernel transfer machine private data to be
	   initialized.
	 */
	int (*ko_tm_start)(struct nlx_kcore_domain *kd,
			   struct nlx_core_transfer_mc *ctm,
			   struct nlx_kcore_transfer_mc *ktm);

	/**
	   Performs kernel core tasks relating to stopping a transfer machine.
	   Kernel resources are released.
	   @param ctm The transfer machine private data.
	   @param ktm The kernel transfer machine private data.
	 */
	void (*ko_tm_stop)(struct nlx_core_transfer_mc *ctm,
			   struct nlx_kcore_transfer_mc *ktm);

	/**
	   Performs kernel core tasks relating to adding a buffer to
	   the message receive queue.
	 */
	nlx_kcore_queue_op_t ko_buf_msg_recv;

	/**
	   Performs kernel core tasks relating to adding a buffer to
	   the message send queue.
	 */
	nlx_kcore_queue_op_t ko_buf_msg_send;

	/**
	   Performs kernel core tasks relating to adding a buffer to
	   the bulk active receive queue.
	 */
	nlx_kcore_queue_op_t ko_buf_active_recv;

	/**
	   Performs kernel core tasks relating to adding a buffer to
	   the bulk active send queue.
	 */
	nlx_kcore_queue_op_t ko_buf_active_send;

	/**
	   Performs kernel core tasks relating to adding a buffer to
	   the bulk passive receive queue.
	 */
	nlx_kcore_queue_op_t ko_buf_passive_recv;

	/**
	   Performs kernel core tasks relating to adding a buffer to
	   the bulk passive send queue.
	 */
	nlx_kcore_queue_op_t ko_buf_passive_send;

	/**
	   Performs kernel core tasks relating to canceling a buffer operation.
	   @param ktm The kernel transfer machine private data.
	   @param kb The kernel buffer private data.
	 */
	int (*ko_buf_del)(struct nlx_kcore_transfer_mc *ktm,
			  struct nlx_kcore_buffer *kb);

	/**
	   Performs common kernel core tasks to wait for buffer events.
	   @param ctm The transfer machine private data.
	   @param ktm The kernel transfer machine private data.
	   @param timeout Absolute time at which to stop waiting.
	 */
	int (*ko_buf_event_wait)(struct nlx_core_transfer_mc *ctm,
				 struct nlx_kcore_transfer_mc *ktm,
				 c2_time_t timeout);
};

static void nlx_core_kmem_loc_set(struct nlx_core_kmem_loc *loc,
				  struct page* pg, uint32_t off);

static bool nlx_kcore_domain_invariant(const struct nlx_kcore_domain *kd);
static bool nlx_kcore_buffer_invariant(const struct nlx_kcore_buffer *kcb);
static bool nlx_kcore_buffer_event_invariant(
				     const struct nlx_kcore_buffer_event *kbe);
static bool nlx_kcore_tm_invariant(const struct nlx_kcore_transfer_mc *kctm);
static int nlx_kcore_kcore_dom_init(struct nlx_kcore_domain *kd);
static void nlx_kcore_kcore_dom_fini(struct nlx_kcore_domain *kd);
static int nlx_kcore_buffer_kla_to_kiov(struct nlx_kcore_buffer *kb,
					const struct c2_bufvec *bvec);
static int nlx_kcore_buffer_uva_to_kiov(struct nlx_kcore_buffer *kb,
					const struct c2_bufvec *bvec);
static bool nlx_kcore_kiov_invariant(const lnet_kiov_t *k, size_t len);

#define NLX_PAGE_OFFSET(addr) ((addr) & ~PAGE_MASK)

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
