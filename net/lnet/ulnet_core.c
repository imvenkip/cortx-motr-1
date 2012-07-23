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
 * Original creation date: 12/14/2011
 */

/**
   @page ULNetCoreDLD LNet Transport User Space Core DLD

   - @ref ULNetCoreDLD-ovw
   - @ref ULNetCoreDLD-def
   - @ref ULNetCoreDLD-req
   - @ref ULNetCoreDLD-depends
   - @ref ULNetCoreDLD-highlights
   - Functional Specification
        - @ref LNetCoreDLD-fspec "LNet Transport Core API"<!-- ./lnet_core.h -->
        - @ref ULNetCore "Core User Space Interface"     <!-- ./ulnet_core.h -->
	- @ref LNetDev "LNet Transport Device" <!-- linux_kernel/klnet_drv.h -->
   - @ref ULNetCoreDLD-lspec
      - @ref ULNetCoreDLD-lspec-comps
      - @ref ULNetCoreDLD-lspec-malloc
      - @ref ULNetCoreDLD-lspec-ioctl
      - @ref ULNetCoreDLD-lspec-dominit
      - @ref ULNetCoreDLD-lspec-domfini
      - @ref ULNetCoreDLD-lspec-reg
      - @ref ULNetCoreDLD-lspec-bev
      - @ref ULNetCoreDLD-lspec-tmstart
      - @ref ULNetCoreDLD-lspec-tmstop
      - @ref ULNetCoreDLD-lspec-buf
      - @ref ULNetCoreDLD-lspec-event
      - @ref ULNetCoreDLD-lspec-nids
      - @ref ULNetCoreDLD-lspec-state
      - @ref ULNetCoreDLD-lspec-thread
      - @ref ULNetCoreDLD-lspec-numa
   - @ref ULNetCoreDLD-conformance
   - @ref ULNetCoreDLD-ut
   - @ref ULNetCoreDLD-st
   - @ref ULNetCoreDLD-O
   - @ref ULNetCoreDLD-ref

   <hr>
   @section ULNetCoreDLD-ovw Overview
   The LNet Transport is built over an address space agnostic "core" I/O
   interface.  This document describes the user space implementation of this
   interface, which interacts with @ref KLNetCoreDLD "Kernel Core"
   by use of the @ref LNetDRVDLD "LNet Transport Device".

   <hr>
   @section ULNetCoreDLD-def Definitions

   Refer to <a href="https://docs.google.com/a/xyratex.com/document/d/
1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">
HLD of Colibri LNet Transport</a>.

   <hr>
   @section ULNetCoreDLD-req Requirements

   - @b r.c2.net.xprt.lnet.ioctl The user space core interacts with the kernel
     core through ioctl requests.
   - @b r.c2.net.xprt.lnet.aligned-objects The implementation must ensure
     that shared objects do not cross page boundaries.

   <hr>
   @section ULNetCoreDLD-depends Dependencies

   - @ref LNetDFS <br>
     The @c c2_net_lnet_ifaces_get() and @c c2_net_lnet_ifaces_put() APIs
     must be changed to require a @c c2_net_domain parameter, because the
     user space must interact with the kernel to get the interfaces, and
     a per-domain file descriptor is required for this interaction.

   - @ref LNetCore <!-- ./lnet_core.h --> <br>
     New @c nlx_core_ep_addr_encode() and @c nlx_core_nidstr_decode() functions
     are added to the core interface, allowing endpoint address encoding and
     decoding to be implemented in an address space independent manner.

   - @ref LNetDev <!-- ./lnet_ioctl.h --> <br>
     The Device driver provides access to the Kernel Core implementation
     through ioctl requests on the "/dev/c2lnet" device.

   <hr>
   @section ULNetCoreDLD-highlights Design Highlights
   - The Core API is an address space agnostic I/O interface intended for use
     by the Colibri Networking LNet transport operation layer in either user
     space or kernel space.
   - The user space implementation interacts with the kernel core
     implementation via a device driver.
   - Each user space @c c2_net_domain corresponds to opening a separate
     file descriptor.
   - Shared memory objects allow indirect interaction with the kernel and
     reduce the number of required context switches.
   - Those core operations that require direct kernel interaction do so via
     ioctl requests.

   <hr>
   @section ULNetCoreDLD-lspec Logical Specification

   - @ref ULNetCoreDLD-lspec-comps
   - @ref ULNetCoreDLD-lspec-malloc
   - @ref ULNetCoreDLD-lspec-ioctl
   - @ref ULNetCoreDLD-lspec-dominit
   - @ref ULNetCoreDLD-lspec-domfini
   - @ref ULNetCoreDLD-lspec-reg
   - @ref ULNetCoreDLD-lspec-bev
   - @ref ULNetCoreDLD-lspec-tmstart
   - @ref ULNetCoreDLD-lspec-tmstop
   - @ref ULNetCoreDLD-lspec-buf
   - @ref ULNetCoreDLD-lspec-event
   - @ref ULNetCoreDLD-lspec-nids
   - @ref ULNetCoreDLD-lspec-state
   - @ref ULNetCoreDLD-lspec-thread
   - @ref ULNetCoreDLD-lspec-numa

   @subsection ULNetCoreDLD-lspec-comps Component Overview
   The relationship between the various objects in the components of the LNet
   transport and the networking layer is illustrated in the following UML
   diagram.  @image html "../../net/lnet/lnet_xo.png" "LNet Transport Objects"

   The Core layer in user space has no sub-components but interfaces with
   the kernel core layer via the device driver layer.

   @see <a href="https://docs.google.com/a/xyratex.com/document/d/
1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">
HLD of Colibri LNet Transport</a>,
   specifically the Design Highlights component diagram.
   @see @ref KLNetCoreDLD-lspec-userspace
   "Kernel Support for User Space Transports".

   @subsection ULNetCoreDLD-lspec-malloc Memory Allocation Strategy

   The LNet driver layer requires that each shared object fit within a single
   page.  Assertions about the structures in question,

   - nlx_core_domain
   - nlx_core_transfer_mc
   - nlx_core_buffer
   - nlx_core_buffer_event

   ensure they are smaller than a page in size.  However, to guarantee that
   instances of these structures do not cross page boundaries, all allocations
   of these structures must be performed using @c c2_alloc_aligned().  The
   @c shift parameter for each allocation must be picked such that @c 1<<shift
   is at least the size of the structure.  Build-time assertions about
   these shifts can assure the correct shift is used.

   @subsection ULNetCoreDLD-lspec-ioctl Strategy for Kernel Interaction

   The user space core interacts with the kernel through ioctl requests on a
   file descriptor opened on the "/dev/c2lnet" device.  There is a 1:1
   correspondence between @c c2_net_domain (@c nlx_core_domain) objects and file
   descriptors.  So, each time a domain is initialized, a new file descriptor is
   obtained.  After the file descriptor is obtained, further interaction is in
   the form of ioctl requests.  When the @c c2_net_domain is finalized, the file
   descriptor is closed.  The specific interactions are detailed in the
   following sections.

   @see @ref LNetDRVDLD "LNet Transport Device DLD"

   @subsection ULNetCoreDLD-lspec-dominit Domain Initialization

   In the case of domain initialization, @c nlx_core_dom_init(), the following
   sequence of tasks is performed by the user space core.  This is the
   first interaction between the user space core and the kernel core.

   - The user space core allocates a @c nlx_core_domain object.
   - It performs upper layer initialization of this object, including allocating
     the @c nlx_ucore_domain object and setting the @c nlx_core_domain::cd_upvt
     field.
   - It opens the device using the @c open() system call.  The device is named
     @c "/dev/c2lnet" and the device is opened with @c O_RDWR|O_CLOEXEC flags.
     The file descriptor is saved in the @c nlx_ucore_domain::ud_fd field.
   - It declares a @c c2_lnet_dev_dom_init_params object, setting
     the @c c2_lnet_dev_dom_init_params::ddi_cd field.
   - It shares the @c nlx_core_domain object via the @c #C2_LNET_DOM_INIT
     ioctl request.  Note that a side effect of this request is that the
     @c nlx_core_domain::cd_kpvt is set.
   - It completes user space initialization of the @c nlx_core_domain object
     and the @c nlx_ucore_domain object, including caching the three buffer
     maximum size values returned in the @c c2_lnet_dev_dom_init_params.

   @see @ref LNetDRVDLD-lspec-dominit "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-domfini Domain Finalization

   During domain finalization, @c nlx_core_dom_fini(), the user space core
   performs the following steps.

   - It completes pre-checks of the @c nlx_ucore_domain
     and @c nlx_core_domain objects.
   - It calls @c close() to release the file descriptor.  This will typically
     cause the kernel to immediately finalize its private data and release
     resources (unless there is duplicate file descriptor, in which case the
     kernel will delay finalization until the final duplicate is closed;
     this is unlikely because the file descriptor is not exposed and the file
     is opened using @c O_CLOEXEC).
   - It completes any post-finalization steps, such as freeing its
     @c nlx_ucore_domain object.

   @see @ref LNetDRVDLD-lspec-domfini "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-reg Buffer Registration and De-registration

   The user space core implementations of @c nlx_core_get_max_buffer_size(),
   @c nlx_core_get_max_buffer_segment_size() and
   @c nlx_core_get_max_buffer_segments() each return the corresponding
   value cached in the @c nlx_ucore_domain object.

   The user space core completes the following tasks to perform
   buffer registration.

   - It performs upper layer initialization of the @c nlx_core_buffer object.
     This includes allocating and initializing the @c nlx_ucore_buffer object
     and setting the @c nlx_core_buffer::cb_upvt field.
   - It declares a @c c2_lnet_dev_buf_register_params object, setting
     the parameter fields from the @c nlx_core_buf_register() parameters.
   - It copies the data referenced by the bvec parameter to the dbr_bvec
     field.
   - It performs a @c #C2_LNET_BUF_REGISTER ioctl request to share the buffer
     with the kernel and complete the kernel part of buffer registration.
   - It completes any initialization of the @c nlx_ucore_buffer object.

   The user space core completes the following tasks to perform
   buffer de-registration.

   - It completes pre-checks of the @c nlx_core_buffer object.
   - It performs a @c #C2_LNET_BUF_DEREGISTER ioctl request, causing
     the kernel to complete the kernel part of buffer de-registration.
   - It completes any user space de-registration of the @c nlx_core_buffer and
     @c nlx_ucore_buffer objects.
   - It frees the @c nlx_ucore_buffer object and resets the
     @c nlx_core_buffer::cb_upvt to NULL.

   @see @ref LNetDRVDLD-lspec-reg "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-bev Managing the Buffer Event Queue

   The @c nlx_core_new_blessed_bev() helper allocates and blesses buffer event
   objects.  In user space, blessing the object requires interacting with the
   kernel.  After the object is blessed by the kernel, the user space core
   can add it to the buffer event queue directly, without further kernel
   interaction.  The following steps are taken by the user space core.

   - It allocates a new @c nlx_core_buffer_event object.
   - It declares a @c c2_lnet_dev_bev_bless_params object and sets its fields.
   - It performs a @c #C2_LNET_BEV_BLESS ioctl request to share the
     @c nlx_core_buffer_event object with the kernel and complete the kernel
     part of blessing the object.

   Buffer event objects are never removed from the buffer event queue until
   the transfer machine is stopped.

   @see @ref LNetDRVDLD-lspec-bev "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-tmstart Starting a Transfer Machine

   The user space core @c nlx_core_tm_start() subroutine completes the following
   tasks to start a transfer machine.  Recall that there is no core API
   corresponding to the @c nlx_xo_tm_init() function.

   - It performs upper layer initialization of the @c nlx_core_transfer_mc
     object.  This includes allocating and initializing the
     @c nlx_ucore_transfer_mc object and setting the
     @c nlx_core_transfer_mc::ctm_upvt field.
   - It performs a @c #C2_LNET_TM_START ioctl request to share the
     @c nlx_core_transfer_mc object with the kernel and complete the kernel
     part of starting the transfer machine.
   - It allocates and initializes two @c nlx_core_buffer_event objects, using
     the user space @c nlx_core_new_blessed_bev() helper.
   - It completes the user space initialization of the @c nlx_core_buffer and
     @c nlx_ucore_transfer_mc objects.  This including initializing the buffer
     event circular queue using the @c bev_cqueue_init() function.

   @see @ref LNetDRVDLD-lspec-tmstart "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-tmstop Stopping a Transfer Machine

   The user space core @c nlx_core_tm_stop() subroutine completes the following
   tasks to stop a transfer machine.  Recall that there is no core API
   corresponding to the @c nlx_xo_tm_fini() function.

   - It completes pre-checks of the @c nlx_core_transfer_mc object.
   - It performs a @c #C2_LNET_TM_STOP ioctl request, causing
     the kernel to complete the kernel part of stopping the transfer machine.
   - It frees the buffer event queue using the @c bev_cqueue_fini() function.
   - It frees the @c nlx_ucore_transfer_mc object and resets the
     @c nlx_core_transfer_mc::ctm_upvt to NULL.

   @see @ref LNetDRVDLD-lspec-tmstop "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-buf Transfer Machine Buffer Queue Operations

   Several LNet transport core subroutines,

   - @c nlx_core_buf_msg_recv()
   - @c nlx_core_buf_msg_send()
   - @c nlx_core_buf_active_recv()
   - @c nlx_core_buf_active_send()
   - @c nlx_core_buf_passive_recv()
   - @c nlx_core_buf_passive_send()
   - @c nlx_core_buf_del()

   operate on buffers and transfer machine queues.  In all user space core
   cases, the shared objects, @c nlx_core_buffer and @c nlx_core_transfer_mc,
   must have been previously shared with the kernel, through use of the @c
   #C2_LNET_BUF_REGISTER and @c #C2_LNET_TM_START ioctl requests, respectively.

   The ioctl requests available to the user space core for managing
   buffers and transfer machine buffer queues are as follows.
   - @c #C2_LNET_BUF_MSG_RECV
   - @c #C2_LNET_BUF_MSG_SEND
   - @c #C2_LNET_BUF_ACTIVE_RECV
   - @c #C2_LNET_BUF_ACTIVE_SEND
   - @c #C2_LNET_BUF_PASSIVE_RECV
   - @c #C2_LNET_BUF_PASSIVE_SEND
   - @c #C2_LNET_BUF_DEL

   In each case, the user space core performs the following steps.
   - Validates the parameters.
   - Declares a @c c2_lnet_dev_buf_queue_params object and sets the two fields.
     In this case, both fields are set to the kernel private pointers of
     the shared objects.
   - Performs the appropriate ioctl request from the list above.

   @see @ref LNetDRVDLD-lspec-buf "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-event Waiting for Buffer Events

   The user space core nlx_core_buf_event_wait() subroutine completes the
   following tasks to wait for buffer events.

   - It declares a @c c2_lnet_dev_buf_event_wait_params and sets the fields.
   - It performs a @c #C2_LNET_BUF_EVENT_WAIT ioctl request to wait for
     the kernel to generate additional buffer events.

   @see @ref LNetDRVDLD-lspec-event "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-nids Node Identifier Support

   Operations involving NID strings require ioctl requests to access
   kernel-only functions.

   Most of the @c nlx_core_ep_addr_decode() and
   @c nlx_core_ep_addr_encode() functions can be implemented common
   in user and kernel space code.  However, converting a NID to a string or
   vice versa requires access to functions which exists only in the kernel.
   The @c nlx_core_nidstr_decode() and @c nlx_core_nidstr_encode() functions
   provide separate user and kernel implementations of this conversion code.

   To convert a NID string to a NID, the user space core performs the
   following tasks.
   - It declares a @c c2_lnet_dev_nid_encdec_params and sets the @c dn_buf to
     the string to be decoded.
   - It calls the @c #C2_LNET_NIDSTR_DECODE ioctl request to cause the kernel
     to decode the string.  On successful return, the @c dn_nid field will be
     set to the corresponding NID.

   To convert a NID into a NID string, the user space core performs the
   following tasks.
   - It declares a @c c2_lnet_dev_nid_encdec_params and sets the @c dn_nid to
     the value to be converted.
   - It calls the @c #C2_LNET_NIDSTR_ENCODE ioctl request to cause the kernel
     to encode the string.  On successful return, the @c dn_buf field will be
     set to the corresponding NID string.

   The final operations involving NID strings are the @c nlx_core_nidstrs_get()
   and @c nlx_core_nidstrs_put() operations.  The user space core obtains
   the strings from the kernel using the @c #C2_LNET_NIDSTRS_GET ioctl request.
   This ioctl request returns a copy of the strings, rather than sharing a
   reference to them.  As such, there is no ioctl request to "put" the strings.
   To get the list of strings, the user space core performs the following
   tasks.

   - It allocates buffer where the NID strings are to be stored.
   - It declares a @c c2_lnet_dev_nidstrs_get_params object and sets the fields
     based on the allocated buffer and its size.
   - It performs a @c #C2_LNET_NIDSTRS_GET ioctl request to populate the buffer
     with the NID strings, which returns the number of NID strings (not 0) on
     success.
   - If the ioctl request returns -EFBIG, the buffer should be freed, a
     larger buffer allocated, and the ioctl request re-attempted.
   - It allocates a @c char** array corresponding to the number of NID strings
     (plus 1 for the required terminating NULL pointer).
   - It populates this array by iterating over the now-populated buffer, adding
     a pointer to each nul-terminated NID string, until the number of
     strings returned by the ioctl request have been populated.

   Currently, the kernel implementation caches the NID strings once; the user
   space core can assume this behavior and cache the result per domain,
   but may need to change in the future if the kernel implementation changes.

   @see @ref LNetDRVDLD-lspec-nids "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-state State Specification

   The User Space Core implementation does not introduce its own state model,
   but operates within the frameworks defined by the Colibri Networking Module
   and the Kernel device driver interface.

   Use of the driver requires a file descriptor.  This file descriptor is
   obtained as part of @c nlx_core_dom_init() and closed as part of
   @c nlx_core_dom_fini().

   @see @ref LNetDRVDLD-lspec-state "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-thread Threading and Concurrency Model

   The user space threading and concurrency model works in conjunction
   with the kernel core model.  No additional behavior is added in user space.

   @see
   @ref KLNetCoreDLD-lspec-thread "Kernel Core Threading and Concurrency Model"

   @subsection ULNetCoreDLD-lspec-numa NUMA optimizations

   The user space core does not allocate threads.  The user space application
   can control thread processor affiliation by confining the threads it uses
   to via use of @c c2_thread_confine().

   <hr>
   @section ULNetCoreDLD-conformance Conformance

   - @b i.c2.net.xprt.lnet.ioctl The @ref LNetDRVDLD-lspec covers how
     each LNet Core operation in user space is implemented using the
     driver ioctl requests.
   - @b i.c2.net.xprt.lnet.aligned-objects The @ref ULNetCoreDLD-lspec-malloc
     section discusses how shared objects can be allocated as required.

   <hr>
   @section ULNetCoreDLD-ut Unit Tests

   Unit tests already exist for testing the core API.  These tests have
   been used previously for the kernel core implementation.  Since the user
   space must implement the same behavior, the unit tests will be reused.

   @see @ref LNetDLD-ut "LNet Transport Unit Tests"

   <hr>
   @section ULNetCoreDLD-st System Tests
   System testing will be performed as part of the transport operation system
   test.

   <hr>
   @section ULNetCoreDLD-O Analysis

   The overall design of the LNet transport already addresses the need to
   minimize data copying between the kernel and user space, and the need to
   minimize context switching.  This is accomplished by use of shared memory and
   a circular buffer event queue maintained in shared memory.  For more
   information, refer to the
   <a href="https://docs.google.com/a/xyratex.com/document/d/
1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD</a>.

   In general, the User Core layer simply routes parameters to and from
   the Kernel Core via the LNet driver.  The complexity of this routing
   is analyzed in @ref LNetDRVDLD-O "LNet Driver Analysis".

   The user core requires a small structure for each shared core structure.
   These user core private structures, e.g. @c nlx_ucore_domain are of
   fixed size and their number is directly proportional to the number of
   core objects allocated by the transport layer.

   <hr>
   @section ULNetCoreDLD-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/
1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">
HLD of Colibri LNet Transport</a>
   - @ref KLNetCoreDLD "LNet Transport Kernel Core DLD" <!--
     ./linux_kernel/klnet_core.c -->
   - @ref LNetDRVDLD "LNet Transport Device DLD" <!--
     ./linux_kernel/klnet_drv.c -->
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

/**
   @addtogroup ULNetCore

   @{
*/

/** The name of the core device */
static const char *nlx_ucore_dev_name = "/dev/" C2_LNET_DEV;

/**
   Invariant for the nlx_ucore_domain structure.
 */
static bool nlx_ucore_domain_invariant(const struct nlx_ucore_domain *ud)
{
	return ud != NULL && ud->ud_magic == C2_NET_LNET_UCORE_DOM_MAGIC &&
	    ud->ud_fd >= 0 &&
	    ud->ud_max_buffer_size > 0 &&
	    ud->ud_max_buffer_segment_size > 0 &&
	    ud->ud_max_buffer_segments > 0 &&
	    ud->ud_nidstrs != NULL;
}

/**
   The nlx_ucore_buffer invariant.
 */
static bool nlx_ucore_buffer_invariant(const struct nlx_ucore_buffer *ub)
{
	return ub != NULL && ub->ub_magic == C2_NET_LNET_UCORE_BUF_MAGIC;
}

/**
   The nlx_ucore_tm invariant.
 */
static bool nlx_ucore_tm_invariant(const struct nlx_ucore_transfer_mc *utm)
{
	return utm != NULL && utm->utm_magic == C2_NET_LNET_UCORE_TM_MAGIC;
}

void *nlx_core_mem_alloc(size_t size, unsigned shift)
{
	return c2_alloc_aligned(size, shift);
}

void nlx_core_mem_free(void *data, size_t size, unsigned shift)
{
	c2_free_aligned(data, size, shift);
}

/**
   Wrapper subroutine to invoke the driver ioctl() and map errno on
   failure to a valid return code.
   @retval >=0 On success.
   @retval <0  On failure.
 */
static int nlx_ucore_ioctl(int fd, unsigned long cmd, void *arg)
{
	int rc;

	C2_PRE(fd >= 0);
	C2_PRE(_IOC_TYPE(cmd) == C2_LNET_IOC_MAGIC);
	C2_PRE(_IOC_NR(cmd)   >= C2_LNET_IOC_MIN_NR);
	C2_PRE(_IOC_NR(cmd)   <= C2_LNET_IOC_MAX_NR);

	rc = ioctl(fd, cmd, arg);
	if (rc >= 0)
		return rc;
	C2_ASSERT(errno > 0);
	C2_ASSERT(errno != EFAULT && errno != EBADR);
	return -errno;
}

/**
   Buffer (linear) increment size when fetching NID strings.
   Can be intercepted by unit tests.
 */
static unsigned nlx_ucore_nidstrs_thunk = 128;

/**
   Routine to fetch the NID strings from the device driver.
   The subroutine does not modify the domain private structure.
   @param ud Ucore domain. Only the fd and addb fields are required to
   be set, as this subroutine could be used during domain initialization
   to cache the NID strings.
   @param nidary A NULL-terminated (like argv) array of NID strings is returned.
 */
static int nlx_ucore_nidstrs_get(struct nlx_ucore_domain *ud, char ***nidary)
{
	struct c2_lnet_dev_nidstrs_get_params dngp;
	unsigned i;
	char *p;
	char **nidstrs;
	unsigned nidstrs_nr;
	int rc;

	C2_PRE(ud != NULL && ud->ud_fd >= 0);

	/* Repeat until the buffer is large enough to hold all the strings. */
	for (i = 0, dngp.dng_buf = NULL; dngp.dng_buf == NULL; ++i) {
		dngp.dng_size = ++i * nlx_ucore_nidstrs_thunk;
		C2_ALLOC_ARR_ADDB(dngp.dng_buf, dngp.dng_size,
				  &ud->ud_addb, &nlx_addb_loc);
		if (dngp.dng_buf == NULL)
			return -ENOMEM;
		rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_NIDSTRS_GET, &dngp);
		if (rc < 0) {
			c2_free(dngp.dng_buf);
			dngp.dng_buf = NULL;
			if (rc != -EFBIG)
				return rc;
		}
	}
	nidstrs_nr = rc;

	/* Create a string array. */
	C2_ALLOC_ARR_ADDB(nidstrs, nidstrs_nr + 1, &ud->ud_addb, &nlx_addb_loc);
	if (nidstrs == NULL) {
		c2_free(dngp.dng_buf);
		return -ENOMEM;
	}
	for (i = 0, p = dngp.dng_buf; i < nidstrs_nr; ++i, ++p) {
		nidstrs[i] = p;
		while (*p != '\0')
			++p;
	}
	nidstrs[nidstrs_nr] = NULL;

	*nidary = nidstrs;
	return 0;
}

/**
   Routine to release the strings allocated by nlx_ucore_nidstrs_get().
 */
static void nlx_ucore_nidstrs_put(struct nlx_ucore_domain *ud, char ***nidary)
{
	C2_PRE(nidary != NULL);
	c2_free((*nidary)[0]); /* string buffer */
	c2_free(*nidary);      /* string array */
	*nidary = NULL;
	return;
}


int nlx_core_dom_init(struct c2_net_domain *dom, struct nlx_core_domain *cd)
{
	struct nlx_ucore_domain *ud;
	struct c2_lnet_dev_dom_init_params ip = {
		.ddi_cd = cd,
	};
	int rc;

	C2_PRE(dom != NULL && cd != NULL);
	C2_PRE(cd->cd_kpvt == NULL && cd->cd_upvt == NULL);
	C2_ALLOC_PTR_ADDB(ud, &dom->nd_addb, &nlx_addb_loc);
	if (ud == NULL)
		return -ENOMEM;
	c2_addb_ctx_init(&ud->ud_addb, &nlx_core_domain_addb_ctx,
			 &dom->nd_addb);

	ud->ud_fd = open(nlx_ucore_dev_name, O_RDWR|O_CLOEXEC);
	if (ud->ud_fd == -1) {
		C2_ASSERT(errno != 0);
		rc = -errno;
		C2_ASSERT(rc < 0);
		goto fail_open;
	}

	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_DOM_INIT, &ip);
	if (rc < 0)
		goto fail_dom_init;
	C2_ASSERT(cd->cd_kpvt != NULL);

	/* cache buffer size constants */
#define NLX_IP_SET(f) ud->ud_##f = ip.ddi_##f
	NLX_IP_SET(max_buffer_size);
	NLX_IP_SET(max_buffer_segment_size);
	NLX_IP_SET(max_buffer_segments);
#undef NLX_IP_SET

	/* cache NID strings */
	rc = nlx_ucore_nidstrs_get(ud, &ud->ud_nidstrs);
	if (rc != 0)
		goto fail_dom_init;
	c2_atomic64_set(&ud->ud_nidstrs_refcount, 0);

	ud->ud_magic = C2_NET_LNET_UCORE_DOM_MAGIC;
	cd->cd_upvt = ud;

	C2_POST(nlx_ucore_domain_invariant(ud));
	return 0;

 fail_dom_init:
	close(ud->ud_fd);
 fail_open:
	c2_addb_ctx_fini(&ud->ud_addb);
	c2_free(ud);
	LNET_ADDB_FUNCFAIL_ADD(dom->nd_addb, rc);
	C2_POST(cd->cd_kpvt == NULL && cd->cd_upvt == NULL);
	return rc;
}

void nlx_core_dom_fini(struct nlx_core_domain *cd)
{
	struct nlx_ucore_domain *ud;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	C2_ASSERT(c2_atomic64_get(&ud->ud_nidstrs_refcount) == 0);
	nlx_ucore_nidstrs_put(ud, &ud->ud_nidstrs);

	close(ud->ud_fd);
	c2_addb_ctx_fini(&ud->ud_addb);
	ud->ud_magic = 0;
	c2_free(ud);
	cd->cd_upvt = NULL;
	cd->cd_kpvt = NULL;
	return;
}

c2_bcount_t nlx_core_get_max_buffer_size(struct nlx_core_domain *cd)
{
	struct nlx_ucore_domain *ud;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));
	return ud->ud_max_buffer_size;
}

c2_bcount_t nlx_core_get_max_buffer_segment_size(struct nlx_core_domain *cd)
{
	struct nlx_ucore_domain *ud;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));
	return ud->ud_max_buffer_segment_size;
}

int32_t nlx_core_get_max_buffer_segments(struct nlx_core_domain *cd)
{
	struct nlx_ucore_domain *ud;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));
	return ud->ud_max_buffer_segments;
}

int nlx_core_buf_register(struct nlx_core_domain *cd,
			  nlx_core_opaque_ptr_t buffer_id,
			  const struct c2_bufvec *bvec,
			  struct nlx_core_buffer *cb)
{
	int rc;
	struct nlx_ucore_buffer *ub;
	struct nlx_ucore_domain *ud;
	struct c2_lnet_dev_buf_register_params rp = {
		.dbr_lcbuf     = cb,
		.dbr_buffer_id = buffer_id,
	};

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));
	C2_PRE(buffer_id != 0);
	C2_PRE(bvec != NULL);
	C2_PRE(cb != NULL);
	C2_PRE(cb->cb_kpvt == NULL && cb->cb_upvt == NULL);

	C2_ALLOC_PTR_ADDB(ub, &ud->ud_addb, &nlx_addb_loc);
	if (ub == NULL)
		return -ENOMEM;
	ub->ub_magic = C2_NET_LNET_UCORE_BUF_MAGIC;
	C2_POST(nlx_ucore_buffer_invariant(ub));
	cb->cb_upvt = ub;

	rp.dbr_bvec = *bvec;
	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_BUF_REGISTER, &rp);
	if (rc < 0) {
		cb->cb_upvt = NULL;
		ub->ub_magic = 0;
		c2_free(ub);
		LNET_ADDB_FUNCFAIL_ADD(ud->ud_addb, rc);
		return rc;
	}
	c2_addb_ctx_init(&ub->ub_addb, &nlx_core_domain_addb_ctx, &ud->ud_addb);
	C2_ASSERT(cb->cb_kpvt != NULL);
	C2_ASSERT(cb->cb_upvt == ub);

	C2_POST(nlx_core_buffer_invariant(cb));
	return 0;
}

void nlx_core_buf_deregister(struct nlx_core_domain *cd,
			     struct nlx_core_buffer *cb)
{
	struct nlx_ucore_buffer *ub;
	struct nlx_ucore_domain *ud;
	struct c2_lnet_dev_buf_deregister_params dp;
	int rc;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	C2_PRE(nlx_core_buffer_invariant(cb));
	ub = cb->cb_upvt;
	C2_PRE(nlx_ucore_buffer_invariant(ub));
	C2_PRE(cb->cb_kpvt != NULL);
	dp.dbd_kb = cb->cb_kpvt;
	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_BUF_DEREGISTER, &dp);
	C2_ASSERT(rc == 0);

	c2_addb_ctx_fini(&ub->ub_addb);
	ub->ub_magic = 0;
	c2_free(ub);

	cb->cb_kpvt = NULL;
	cb->cb_upvt = NULL;
	return;
}

#define NLX_UCORE_BUF_OP(op, ...)				\
	struct nlx_ucore_domain *ud;				\
	struct nlx_ucore_transfer_mc *utm;			\
	struct nlx_ucore_buffer *ub;				\
	struct c2_lnet_dev_buf_queue_params dbqp;		\
	int rc = 0;						\
								\
	C2_PRE(cd != NULL);					\
	ud = cd->cd_upvt;					\
	C2_PRE(nlx_ucore_domain_invariant(ud));			\
								\
	C2_PRE(nlx_core_tm_invariant(ctm));			\
	utm = ctm->ctm_upvt;					\
	C2_PRE(nlx_ucore_tm_invariant(utm));			\
								\
	C2_PRE(nlx_core_buffer_invariant(cb));			\
	ub = cb->cb_upvt;					\
	C2_PRE(nlx_ucore_buffer_invariant(ub));			\
								\
	__VA_ARGS__;						\
								\
	dbqp.dbq_ktm = ctm->ctm_kpvt;				\
	dbqp.dbq_kb  = cb->cb_kpvt;				\
	rc = nlx_ucore_ioctl(ud->ud_fd, op, &dbqp);		\
	if (rc < 0)						\
		LNET_ADDB_FUNCFAIL_ADD(ub->ub_addb, rc)

int nlx_core_buf_msg_recv(struct nlx_core_domain *cd,
			  struct nlx_core_transfer_mc *ctm,
			  struct nlx_core_buffer *cb)
{
	NLX_UCORE_BUF_OP(C2_LNET_BUF_MSG_RECV,
			 C2_PRE(cb->cb_qtype == C2_NET_QT_MSG_RECV);
			 C2_PRE(cb->cb_length > 0);
			 C2_PRE(cb->cb_min_receive_size <= cb->cb_length);
			 C2_PRE(cb->cb_max_operations > 0);
			 );
	return rc;
}

int nlx_core_buf_msg_send(struct nlx_core_domain *cd,
			  struct nlx_core_transfer_mc *ctm,
			  struct nlx_core_buffer *cb)
{
	NLX_UCORE_BUF_OP(C2_LNET_BUF_MSG_SEND,
			 C2_PRE(cb->cb_qtype == C2_NET_QT_MSG_SEND);
			 C2_PRE(cb->cb_length > 0);
			 C2_PRE(cb->cb_max_operations == 1);
			 );
	return rc;
}

int nlx_core_buf_active_recv(struct nlx_core_domain *cd,
			     struct nlx_core_transfer_mc *ctm,
			     struct nlx_core_buffer *cb)
{
	uint32_t tmid;
	uint64_t counter;
	NLX_UCORE_BUF_OP(C2_LNET_BUF_ACTIVE_RECV,
			 C2_PRE(cb->cb_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
			 C2_PRE(cb->cb_length > 0);
			 C2_PRE(cb->cb_max_operations == 1);
			 C2_PRE(cb->cb_match_bits > 0);
			 nlx_core_match_bits_decode(cb->cb_match_bits,
						    &tmid, &counter);
			 C2_PRE(tmid == cb->cb_addr.cepa_tmid);
			 C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
			 C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);
			 );
	return rc;
}

int nlx_core_buf_active_send(struct nlx_core_domain *cd,
			     struct nlx_core_transfer_mc *ctm,
			     struct nlx_core_buffer *cb)
{
	uint32_t tmid;
	uint64_t counter;
	NLX_UCORE_BUF_OP(C2_LNET_BUF_ACTIVE_SEND,
			 C2_PRE(cb->cb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
			 C2_PRE(cb->cb_length > 0);
			 C2_PRE(cb->cb_max_operations == 1);
			 C2_PRE(cb->cb_match_bits > 0);
			 nlx_core_match_bits_decode(cb->cb_match_bits,
						    &tmid, &counter);
			 C2_PRE(tmid == cb->cb_addr.cepa_tmid);
			 C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
			 C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);
			 );
	return rc;
}

int nlx_core_buf_passive_recv(struct nlx_core_domain *cd,
			      struct nlx_core_transfer_mc *ctm,
			      struct nlx_core_buffer *cb)
{
	uint32_t tmid;
	uint64_t counter;
	NLX_UCORE_BUF_OP(C2_LNET_BUF_PASSIVE_RECV,
			 C2_PRE(cb->cb_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
			 C2_PRE(cb->cb_length > 0);
			 C2_PRE(cb->cb_max_operations == 1);
			 C2_PRE(cb->cb_match_bits > 0);
			 nlx_core_match_bits_decode(cb->cb_match_bits,
						    &tmid, &counter);
			 C2_PRE(tmid == ctm->ctm_addr.cepa_tmid);
			 C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
			 C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);
			 );
	return rc;
}

int nlx_core_buf_passive_send(struct nlx_core_domain *cd,
			      struct nlx_core_transfer_mc *ctm,
			      struct nlx_core_buffer *cb)
{
	uint32_t tmid;
	uint64_t counter;
	NLX_UCORE_BUF_OP(C2_LNET_BUF_PASSIVE_SEND,
			 C2_PRE(cb->cb_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
			 C2_PRE(cb->cb_length > 0);
			 C2_PRE(cb->cb_max_operations == 1);
			 C2_PRE(cb->cb_match_bits > 0);
			 nlx_core_match_bits_decode(cb->cb_match_bits,
						    &tmid, &counter);
			 C2_PRE(tmid == ctm->ctm_addr.cepa_tmid);
			 C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
			 C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);
			 );
	return rc;
}

int nlx_core_buf_del(struct nlx_core_domain *cd,
		     struct nlx_core_transfer_mc *ctm,
		     struct nlx_core_buffer *cb)
{
	NLX_UCORE_BUF_OP(C2_LNET_BUF_DEL, );
	return rc;
}

#undef NLX_UCORE_BUF_OP

int nlx_core_buf_event_wait(struct nlx_core_domain *cd,
			    struct nlx_core_transfer_mc *ctm,
			    c2_time_t timeout)
{
	struct nlx_ucore_domain *ud;
	struct nlx_ucore_transfer_mc *utm;
	struct c2_lnet_dev_buf_event_wait_params bewp;
	int rc;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(ctm->ctm_kpvt != NULL);
	utm = ctm->ctm_upvt;
	C2_PRE(nlx_ucore_tm_invariant(utm));

	bewp.dbw_ktm = ctm->ctm_kpvt;
	bewp.dbw_timeout = timeout;
	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_BUF_EVENT_WAIT, &bewp);
	if (rc < 0) {
		if (rc != -ETIMEDOUT) /* valid return value */
			LNET_ADDB_FUNCFAIL_ADD(utm->utm_addb, rc);
		return rc;
	}
	return 0;
}

int nlx_core_nidstr_decode(struct nlx_core_domain *cd,
			   const char *nidstr,
			   uint64_t *nid)
{
	struct c2_lnet_dev_nid_encdec_params dnep;
	struct nlx_ucore_domain *ud;
	int rc;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	strncpy(dnep.dn_buf, nidstr, ARRAY_SIZE(dnep.dn_buf) - 1);
	dnep.dn_buf[ARRAY_SIZE(dnep.dn_buf) - 1] = '\0';
	dnep.dn_nid = 0;

	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_NIDSTR_DECODE, &dnep);
	if (rc < 0) {
		LNET_ADDB_FUNCFAIL_ADD(ud->ud_addb, rc);
		return rc;
	}

	*nid = dnep.dn_nid;
	return 0;
}

int nlx_core_nidstr_encode(struct nlx_core_domain *cd,
			   uint64_t nid,
			   char nidstr[C2_NET_LNET_NIDSTR_SIZE])
{
	struct c2_lnet_dev_nid_encdec_params dnep;
	struct nlx_ucore_domain *ud;
	int rc;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	dnep.dn_nid = nid;
	C2_SET_ARR0(dnep.dn_buf);

	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_NIDSTR_ENCODE, &dnep);
	if (rc < 0) {
		LNET_ADDB_FUNCFAIL_ADD(ud->ud_addb, rc);
		return rc;
	}
	C2_POST(dnep.dn_buf[0] != '\0');

	strncpy(nidstr, dnep.dn_buf, C2_NET_LNET_NIDSTR_SIZE - 1);
	nidstr[C2_NET_LNET_NIDSTR_SIZE - 1] = '\0';
	return 0;
}

/*
   This call is not protected by any mutex. It relies on the
   application to not invoke it during domain finalization.
 */
int nlx_core_nidstrs_get(struct nlx_core_domain *cd, char * const **nidary)
{
	struct nlx_ucore_domain *ud;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	c2_atomic64_inc(&ud->ud_nidstrs_refcount);
	*nidary = ud->ud_nidstrs;
	return 0;
}

void nlx_core_nidstrs_put(struct nlx_core_domain *cd, char * const **nidary)
{
	struct nlx_ucore_domain *ud;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	C2_ASSERT(ud->ud_nidstrs == *nidary);
	C2_ASSERT(c2_atomic64_get(&ud->ud_nidstrs_refcount) > 0);
	c2_atomic64_dec(&ud->ud_nidstrs_refcount);
	*nidary = NULL;
	return;
}

/**
   Subroutine to stop the TM in the kernel.
 */
static void nlx_ucore_tm_stop(struct nlx_core_domain *cd,
			      struct nlx_core_transfer_mc *ctm)
{
	struct nlx_ucore_domain *ud;
	struct c2_lnet_dev_tm_stop_params tpp;
	int rc;

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));
	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(ctm->ctm_kpvt != NULL);
	tpp.dts_ktm = ctm->ctm_kpvt;

	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_TM_STOP, &tpp);
	C2_ASSERT(rc == 0);
	return;
}

int nlx_core_tm_start(struct nlx_core_domain *cd,
		      struct c2_net_transfer_mc *tm,
		      struct nlx_core_transfer_mc *ctm)
{
	struct nlx_ucore_domain *ud;
	struct nlx_ucore_transfer_mc *utm;
	struct nlx_core_buffer_event *e1 = NULL;
	struct nlx_core_buffer_event *e2 = NULL;
	struct c2_lnet_dev_tm_start_params tsp = {
		.dts_ctm = ctm,
	};
	int rc;

	C2_PRE(tm != NULL);
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(nlx_tm_invariant(tm));

	C2_PRE(cd != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));

	C2_PRE(ctm != NULL);
	C2_PRE(ctm->ctm_kpvt == NULL);
	C2_PRE(ctm->ctm_magic == 0);

	C2_ALLOC_PTR_ADDB(utm, &ud->ud_addb, &nlx_addb_loc);
	if (utm == NULL) {
		rc = -ENOMEM;
		goto fail_utm;
	}
	c2_addb_ctx_init(&utm->utm_addb, &nlx_core_tm_addb_ctx, &ud->ud_addb);
	utm->utm_magic = C2_NET_LNET_UCORE_TM_MAGIC;
	C2_POST(nlx_ucore_tm_invariant(utm));
	ctm->ctm_upvt = utm;

	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_TM_START, &tsp);
	if (rc < 0)
		goto fail_start;
	C2_ASSERT(ctm->ctm_kpvt != NULL);
	C2_ASSERT(ctm->ctm_upvt == utm);

	rc = nlx_core_new_blessed_bev(cd, ctm, &e1);
	if (rc == 0)
		rc = nlx_core_new_blessed_bev(cd, ctm, &e2);
	if (rc != 0)
		goto fail_blessed_bev;
	C2_ASSERT(e1 != NULL && e2 != NULL);
	bev_cqueue_init(&ctm->ctm_bevq, &e1->cbe_tm_link, &e2->cbe_tm_link);
	C2_ASSERT(bev_cqueue_is_empty(&ctm->ctm_bevq));

	C2_POST(nlx_ucore_tm_invariant(utm));
	C2_POST(ctm->ctm_kpvt != NULL);
	C2_POST(ctm->ctm_upvt == utm);
	C2_POST(nlx_core_tm_invariant(ctm));
	return 0;

 fail_blessed_bev:
	C2_ASSERT(e2 == NULL);
	if (e1 != NULL)
		nlx_core_bev_free_cb(&e1->cbe_tm_link);
	nlx_ucore_tm_stop(cd, ctm);
 fail_start:
	ctm->ctm_upvt = NULL;
	c2_addb_ctx_fini(&utm->utm_addb);
	utm->utm_magic = 0;
	c2_free(utm);
 fail_utm:
	LNET_ADDB_FUNCFAIL_ADD(ud->ud_addb, rc);
	return rc;
}

void nlx_core_tm_stop(struct nlx_core_domain *cd,
		      struct nlx_core_transfer_mc *ctm)
{
	struct nlx_ucore_transfer_mc *utm;

	utm = ctm->ctm_upvt;
	C2_PRE(nlx_ucore_tm_invariant(utm));

	nlx_ucore_tm_stop(cd, ctm); /* other invariants checked */
	bev_cqueue_fini(&ctm->ctm_bevq, nlx_core_bev_free_cb);
	c2_addb_ctx_fini(&utm->utm_addb);

	ctm->ctm_upvt = NULL;
	utm->utm_magic = 0;
	c2_free(utm);
	return;
}

int nlx_core_new_blessed_bev(struct nlx_core_domain *cd,
			     struct nlx_core_transfer_mc *ctm,
			     struct nlx_core_buffer_event **bevp)
{
	struct nlx_core_buffer_event *bev;
	struct c2_lnet_dev_bev_bless_params bp;
	struct nlx_ucore_domain *ud;
	struct nlx_ucore_transfer_mc *utm;
	int rc;

	C2_PRE(cd != NULL);
	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(ctm->ctm_kpvt != NULL);
	ud = cd->cd_upvt;
	C2_PRE(nlx_ucore_domain_invariant(ud));
	utm = ctm->ctm_upvt;
	C2_PRE(nlx_ucore_tm_invariant(utm));

	NLX_ALLOC_PTR_ADDB(bev, &utm->utm_addb, &nlx_addb_loc);
	if (bev == NULL) {
		*bevp = NULL;
		return -ENOMEM;
	}

	bp.dbb_ktm = ctm->ctm_kpvt;
	bp.dbb_bev = bev;
	rc = nlx_ucore_ioctl(ud->ud_fd, C2_LNET_BEV_BLESS, &bp);
	if (rc < 0) {
		NLX_FREE_PTR(bev);
		LNET_ADDB_FUNCFAIL_ADD(utm->utm_addb, rc);
		return rc;
	}
	C2_ASSERT(bev->cbe_kpvt != NULL);

	*bevp = bev;
	return 0;
}

static void nlx_core_fini(void)
{
	return;
}

static int nlx_core_init(void)
{
	return 0;
}

/** @} */ /* ULNetCore */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
