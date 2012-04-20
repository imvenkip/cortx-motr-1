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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 02/10/2012
 */

#ifndef __LNET_IOCTL_H__
#define __LNET_IOCTL_H__

#include "lib/vec.h"   /* c2_bufvec, standard types */

/**
   @addtogroup LNetDev
   @{
 */

/**
   Parameters for the C2_LNET_DOM_INIT ioctl.
   @see nlx_dev_ioctl_dom_init()
 */
struct c2_lnet_dev_dom_init_params {
	/** The user space core private data pointer for the domain. */
	struct nlx_core_domain         *ddi_cd;
	/** Returned maximum buffer size (counting all segments). */
	c2_bcount_t                     ddi_max_buffer_size;
	/** Returned maximum size of a buffer segment. */
	c2_bcount_t                     ddi_max_buffer_segment_size;
	/** Returned maximum number of buffer segments. */
	int32_t                         ddi_max_buffer_segments;
};

/**
   Parameters for the C2_LNET_BUF_REGISTER ioctl.
   The c2_bufvec is copied into the parameters, not referenced, to simplify
   the logic required to map the data into kernel space.
   @see nlx_dev_ioctl_buf_register()
 */
struct c2_lnet_dev_buf_register_params {
	/** The user space core private data pointer for the buffer. */
	struct nlx_core_buffer         *dbr_lcbuf;
	/** value to be set in the nlx_core_buffer::cb_buffer_id field. */
	nlx_core_opaque_ptr_t           dbr_buffer_id;
	/** Buffer vector with user address space pointers. */
	struct c2_bufvec                dbr_bvec;
};

/**
   Parameters for the C2_LNET_BUF_DEREGISTER ioctl.
   @see nlx_dev_ioctl_buf_deregister()
 */
struct c2_lnet_dev_buf_deregister_params {
	/** The nlx_core_buffer::cb_kpvt for the core private TM data. */
	void                           *dbd_kb;
};

/**
   Parameters to various ioctl requests that operate on a transfer machine
   and a buffer.  These include:
   - C2_LNET_BUF_MSG_RECV
   - C2_LNET_BUF_MSG_SEND
   - C2_LNET_BUF_ACTIVE_RECV
   - C2_LNET_BUF_ACTIVE_SEND
   - C2_LNET_BUF_PASSIVE_RECV
   - C2_LNET_BUF_PASSIVE_SEND
   - C2_LNET_BUF_DEL

   @see nlx_dev_ioctl_buf_msg_recv(), nlx_dev_ioctl_buf_msg_send(),
	nlx_dev_ioctl_buf_active_recv(), nlx_dev_ioctl_buf_active_send(),
	nlx_dev_ioctl_buf_passive_recv(), nlx_dev_ioctl_buf_passive_send(),
	nlx_dev_ioctl_buf_del()
 */
struct c2_lnet_dev_buf_queue_params {
	/** The nlx_core_transfer_mc::ctm_kpvt for the core private TM data. */
	void                           *dbq_ktm;
	/** The nlx_core_buffer::cb_kpvt for the core private buffer data. */
	void                           *dbq_kb;
};

/**
   Parameters for the C2_LNET_BUF_EVENT_WAIT ioctl.
   @see nlx_dev_ioctl_buf_event_wait()
 */
struct c2_lnet_dev_buf_event_wait_params {
	/** The nlx_core_transfer_mc::ctm_kpvt for the core private TM data. */
	void                           *dbw_ktm;
	/**
	   Absolute time at which to stop waiting.  A value of 0
	   indicates that the request should not wait.
	 */
	c2_time_t                       dbw_timeout;
};

/**
   Parameters for the C2_LNET_NIDSTR_DECODE and C2_LNET_NIDSTR_ENCODE
   ioctl requests.
   @see nlx_dev_ioctl_nidstr_decode(), nlx_dev_ioctl_nidstr_encode()
 */
struct c2_lnet_dev_nid_encdec_params {
	/** Node ID to be encoded or decoded. */
	uint64_t                        dn_nid;
	/** NID string value to be encoded or decoded. */
	char                            dn_buf[C2_NET_LNET_NIDSTR_SIZE];
};

/**
   Parameters for the C2_LNET_NIDSTRS_GET ioctl request.
   @see nlx_dev_ioctl_nidstrs_get()
 */
struct c2_lnet_dev_nidstrs_get_params {
	/** The actual size of the buffer pointed to by dng_buf. */
	c2_bcount_t                     dng_size;
	/** The user space pointer to the nid strings buffer. */
	char                           *dng_buf;
};

/**
   Parameters for the C2_LNET_TM_START ioctl.
   @see nlx_dev_ioctl_tm_start()
 */
struct c2_lnet_dev_tm_start_params {
	/** The user space core private data pointer for the TM. */
	struct nlx_core_transfer_mc    *dts_ctm;
};

/**
   Parameters for the C2_LNET_TM_STOP ioctl.
   @see nlx_dev_ioctl_tm_stop()
 */
struct c2_lnet_dev_tm_stop_params {
	/** The nlx_core_transfer_mc::ctm_kpvt for the core private TM data. */
	void                           *dts_ktm;
};

/**
   Parameters for the C2_LNET_BEV_BLESS ioctl request.
   @see nlx_dev_ioctl_bev_bless()
 */
struct c2_lnet_dev_bev_bless_params {
	/** The nlx_core_transfer_mc::ctm_kpvt for the core private TM data. */
	void                           *dbb_ktm;
	/** The user space core private data pointer for the buffer event. */
	struct nlx_core_buffer_event   *dbb_bev;
};

/** The name of the C2_LNET device. */
#define C2_LNET_DEV         "c2lnet"

#define C2_LNET_IOC_MAGIC   'c'
#define C2_LNET_IOC_MIN_NR  0x21
#define C2_LNET_IOC_MAX_NR  0x4f

#define C2_LNET_DOM_INIT \
	_IOWR(C2_LNET_IOC_MAGIC, 0x21, struct c2_lnet_dev_dom_init_params)

#define C2_LNET_BUF_REGISTER \
	_IOW(C2_LNET_IOC_MAGIC, 0x26, struct c2_lnet_dev_buf_register_params)
#define C2_LNET_BUF_DEREGISTER \
	_IOW(C2_LNET_IOC_MAGIC, 0x27, struct c2_lnet_dev_buf_deregister_params)
#define C2_LNET_BUF_MSG_RECV \
	_IOW(C2_LNET_IOC_MAGIC, 0x28, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_MSG_SEND \
	_IOW(C2_LNET_IOC_MAGIC, 0x29, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_ACTIVE_RECV \
	_IOW(C2_LNET_IOC_MAGIC, 0x2a, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_ACTIVE_SEND \
	_IOW(C2_LNET_IOC_MAGIC, 0x2b, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_PASSIVE_RECV \
	_IOW(C2_LNET_IOC_MAGIC, 0x2c, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_PASSIVE_SEND \
	_IOW(C2_LNET_IOC_MAGIC, 0x2d, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_DEL \
	_IOW(C2_LNET_IOC_MAGIC, 0x2e, struct c2_lnet_dev_buf_queue_params)
#define C2_LNET_BUF_EVENT_WAIT \
	_IOW(C2_LNET_IOC_MAGIC, 0x2f, struct c2_lnet_dev_buf_event_wait_params)

#define C2_LNET_NIDSTR_DECODE \
	_IOWR(C2_LNET_IOC_MAGIC, 0x30, struct c2_lnet_dev_nid_encdec_params)
#define C2_LNET_NIDSTR_ENCODE \
	_IOWR(C2_LNET_IOC_MAGIC, 0x31, struct c2_lnet_dev_nid_encdec_params)
#define C2_LNET_NIDSTRS_GET \
	_IOW(C2_LNET_IOC_MAGIC, 0x32, struct c2_lnet_dev_nidstrs_get_params)

#define C2_LNET_TM_START \
	_IOW(C2_LNET_IOC_MAGIC, 0x33, struct c2_lnet_dev_tm_start_params)
#define C2_LNET_TM_STOP \
	_IOW(C2_LNET_IOC_MAGIC, 0x34, struct c2_lnet_dev_tm_stop_params)

#define C2_LNET_BEV_BLESS \
	_IOW(C2_LNET_IOC_MAGIC, 0x35, struct c2_lnet_dev_bev_bless_params)

/** @} */ /* LNetDev */

#endif /* __LNET_IOCTL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
