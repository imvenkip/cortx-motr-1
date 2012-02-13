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

/**
   @addtogroup LNetDRVDLDDFS
   @{
 */

#include "lib/types.h" /* uint64_t, uint32_t */

enum {
	C2_NET_LNET_MEM_AREA_MAGIC = 0x4c4d656d41726561ULL, /* LMemArea */
};

/**
   This data structure describes a memory area that is to be mapped or
   unmapped from user space.
 */
struct c2_net_lnet_mem_area {
	uint64_t nm_magic;
	/** Size of area to map */
	uint32_t nm_size;
	/** User space address of start of memory area */
	unsigned long nm_user_addr;
};

#define C2_LNET_IOC_MAGIC   'c'
#define C2_LNET_IOC_MIN_NR  0x21
#define C2_LNET_IOC_MAX_NR  0x3F

#define C2_LNET_PROTOREAD   _IOR(C2_LNET_IOC_MAGIC, 0x21, int)
#define C2_LNET_PROTOWRITE  _IOW(C2_LNET_IOC_MAGIC, 0x22, int)
#define C2_LNET_PROTOMAP \
		_IOW(C2_LNET_IOC_MAGIC, 0x23, struct c2_net_lnet_mem_area)
#define C2_LNET_PROTOUNMAP \
		_IOW(C2_LNET_IOC_MAGIC, 0x24, struct c2_net_lnet_mem_area)

/**
   @}
 */

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
