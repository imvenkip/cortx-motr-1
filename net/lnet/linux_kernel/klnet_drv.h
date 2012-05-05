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
 * Original creation date: 02/08/2012
 */

#ifndef __KLNET_DRV_H__
#define __KLNET_DRV_H__

/**
   @page LNetDRVDLD-fspec LNet Transport Device Functional Specification

   - @ref LNetDRVDLD-fspec-ovw
   - @ref LNetDRVDLD-fspec-ds
   - @ref LNetDRVDLD-fspec-sub
   - @ref LNetDRVDLD-fspec-ioctl
   - @ref LNetDev "Detailed Functional Specification"

   @section LNetDRVDLD-fspec-ovw Overview

   The LNet Transport Device is a layer between the User LNet Core Transport and
   the Kernel LNet Core Transport.  It provides user-space access to the LNet
   Kernel Core Transport.  The API is presented as a set of ioctl requests that
   are handled by the LNet Transport Device Driver.  Each ioctl request takes an
   input parameter, which is a pointer to a shared object or a data structure
   containing the parameters required by the LNet Kernel Core Transport.  The
   device layer handles mapping shared data structures from user space into
   kernel space and tracks each such shared data structure.  The device layer
   uses APIs in the LNet Kernel Core layer to implement the ioctl request
   functionality.  When requested, the device layer unmaps shared data
   structures.  The device layer also cleans up shared data structures when the
   user space releases the device prematurely.

   @section LNetDRVDLD-fspec-ds Data Structures

   The API uses the following data structures to represent the parameters to the
   various ioctl requests.  Note that there is not a 1:1 correspondence
   between these data structures and the operations in the
   @ref LNetCore "LNet Transport Core Interface".  Several ioctl requests
   take the same parameters and use the same data structure to pass those
   parameters.  Some ioctl requests require only a single identifier as
   a parameters, and thus require no special parameter data structure.  A few
   Core interfaces require no ioctl request, because they operate directly
   on shared data structures in user space.

   - c2_lnet_dev_dom_init_params
   - c2_lnet_dev_buf_register_params
   - c2_lnet_dev_buf_queue_params
   - c2_lnet_dev_buf_event_wait_params
   - c2_lnet_dev_nid_encdec_params
   - c2_lnet_dev_nidstrs_get_params
   - c2_lnet_dev_bev_bless_params

   The API includes several operations whose side effect is to pin or unpin
   shared data corresponding to the following data structures defined in the
   @ref LNetCore "LNet Transport Core Interface".

   - nlx_core_domain
   - nlx_core_transfer_mc
   - nlx_core_buffer
   - nlx_core_buffer_event

   The API tracks its references to these objects using corresponding objects
   defined in the @ref KLNetCore "LNet Transport Core Kernel Private Interface".

   - nlx_kcore_domain
   - nlx_kcore_transfer_mc
   - nlx_kcore_buffer
   - nlx_kcore_buffer_event

   The API accesses kernel core functionality indirectly through the operations
   on the @c nlx_kcore_ops structure, @c nlx_kcore_domain::kd_drv_ops.

   @see @ref LNetDev "Detailed Functional Specification"

   @section LNetDRVDLD-fspec-sub Subroutines

   Subroutines are provided to initialize and finalize the device driver.
   - nlx_dev_init()
   - nlx_dev_fini()

   All other subroutines are internal to the device driver.

   @see @ref LNetDev "Detailed Functional Specification"

   @section LNetDRVDLD-fspec-ioctl Ioctl Requests

   The device driver recognizes the following ioctl requests.
   - #C2_LNET_DOM_INIT
   - #C2_LNET_BUF_REGISTER
   - #C2_LNET_BUF_DEREGISTER
   - #C2_LNET_BUF_MSG_RECV
   - #C2_LNET_BUF_MSG_SEND
   - #C2_LNET_BUF_ACTIVE_RECV
   - #C2_LNET_BUF_ACTIVE_SEND
   - #C2_LNET_BUF_PASSIVE_RECV
   - #C2_LNET_BUF_PASSIVE_SEND
   - #C2_LNET_BUF_DEL
   - #C2_LNET_BUF_EVENT_WAIT
   - #C2_LNET_NIDSTR_DECODE
   - #C2_LNET_NIDSTR_ENCODE
   - #C2_LNET_NIDSTRS_GET
   - #C2_LNET_TM_START
   - #C2_LNET_TM_STOP
   - #C2_LNET_BEV_BLESS

   @see @ref LNetDev "Detailed Functional Specification"
 */

/**
   @defgroup LNetDev LNet Transport Device
   @ingroup LNetDFS

   The external interfaces of the LNet transport device are obtained by
   including the file @ref net/lnet/linux_kernel/klnet_drv.h.

   The device appears in file system as /dev/c2lnet.

   @see The @ref LNetDRVDLD "LNet Transport Device and Driver DLD" its
   @ref LNetDRVDLD-fspec "Functional Specification"

   @{
*/

#define WRITABLE_USER_PAGE_GET(uaddr, pg)				\
	get_user_pages(current, current->mm, (unsigned long) (uaddr),	\
		       1, 1, 0, &(pg), NULL)

/** Put a writable user page after calling SetPageDirty(). */
#define WRITABLE_USER_PAGE_PUT(pg)		\
({						\
	struct page *__pg = (pg);		\
	if (!PageReserved(__pg))		\
		SetPageDirty(__pg);		\
	put_page(__pg);				\
})

/**
   Initialise the C2 LNet Transport device.
   Registers the device as a miscellaneous character device.
 */
int nlx_dev_init(void);
/** Finalise the C2 LNet device. */
void nlx_dev_fini(void);

/** @} */ /* LNetDev */

#endif /*  __KLNET_DRV_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
