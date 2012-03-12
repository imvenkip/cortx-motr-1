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
   - @ref LNetCoreDLD-fspec "Functional Specification"   <!-- ./lnet_core.h -->
        - @ref LNetCore "LNet Transport Core Interface"  <!-- ./lnet_core.h -->
        - @ref ULNetCore "Core Userspace Interface"     <!-- ./ulnet_core.h -->
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

   Refer to <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>.

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

   @see <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>,
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
   - It shares the @c nlx_core_domain object via the @c #C2_LNET_DOM_INIT
     ioctl request.  Note that a side effect of this request is that the
     @c nlx_core_domain::cd_kpvt is set.
   - It completes user space initialization of the @c nlx_core_domain object and
     the @c nlx_ucore_domain object.

   @see @ref LNetDRVDLD-lspec-dominit "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-domfini Domain Finalization

   During domain finalization, @c nlx_core_dom_fini(), the user space core
   performs the following steps.

   - It completes pre-checks of the @c nlx_ucore_domain
     and @c nlx_core_domain objects.
   - It performs the @c #C2_LNET_DOM_FINI ioctl request to cause the
     kernel to finalize its private data and release resources.
   - It calls @c close() to release the file descriptor.
   - It completes any post-finalization steps, such as freeing its
     @c nlx_ucore_domain object.

   @see @ref LNetDRVDLD-lspec-domfini "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-reg Buffer Registration and De-registration

   The following ioctl requests are available for use by the user space
   core to obtain kernel parameters controlling buffer size.
   - @c #C2_LNET_MAX_BUFFER_SIZE
   - @c #C2_LNET_MAX_BUFFER_SEGMENT_SIZE
   - @c #C2_LNET_MAX_BUFFER_SEGMENTS

   The user space core performs these ioctl requests to obtain the
   corresponding values.  The value (when positive) is the return value of the
   ioctl request.

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
   - Loop in the case that a timeout did not occur but the buffer event queue
     was already empty again after the ioctl request returns.

   @see @ref LNetDRVDLD-lspec-event "Corresponding device layer behavior"

   @subsection ULNetCoreDLD-lspec-nids Node Identifier Support

   Operations involving NID strings require ioctl requests to access
   kernel-only functions.

   Most of the @c nlx_core_ep_addr_decode() and
   @c nlx_core_ep_addr_encode() functions can be implemented common
   in user and kernel space code.  However, converting a NID to a string or
   visa versa requires access to functions which exists only in the kernel.
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
   - @b i.c2.net.xprt.lnet.aligned-objects THe @ref ULNetCoreDLD-lspec-malloc
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
   <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD</a>.

   In general, the User Core layer simply routes parameters to and from
   the Kernel Core via the LNet driver.  The complexity of this routing
   is analyzed in @ref LNetDRVDLD-O "LNet Driver Analysis".

   The user core requires a small structure for each shared core structure.
   These user core private structures, e.g. @c nlx_ucore_domain are of
   fixed size and their number is directly proportional to the number of
   core objects allocated by the transport layer.

   <hr>
   @section ULNetCoreDLD-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   - @ref KLNetCoreDLD "LNet Transport Kernel Core DLD" <!--
     ./linux_kernel/klnet_core.c -->
   - @ref LNetDRVDLD "LNet Transport Device DLD" <!--
     ./linux_kernel/klnet_drv.c -->
 */

void *nlx_core_mem_alloc(size_t size)
{
	/** @todo implement */
	return NULL;
}

void nlx_core_mem_free(void *data)
{
	/** @todo implement */
}

int nlx_core_dom_init(struct c2_net_domain *dom, struct nlx_core_domain *lcdom)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

void nlx_core_dom_fini(struct nlx_core_domain *lcdom)
{
	/** @todo XXX implement */
}

c2_bcount_t nlx_core_get_max_buffer_size(struct nlx_core_domain *lcdom)
{
	/** @todo XXX implement */
	return 0;
}

c2_bcount_t nlx_core_get_max_buffer_segment_size(struct nlx_core_domain *lcdom)
{
	/** @todo XXX implement */
	return 0;
}

int32_t nlx_core_get_max_buffer_segments(struct nlx_core_domain *lcdom)
{
	/** @todo XXX implement */
	return 0;
}

int nlx_core_buf_register(struct nlx_core_domain *lcdom,
			  nlx_core_opaque_ptr_t buffer_id,
			  const struct c2_bufvec *bvec,
			  struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	return -ENOSYS;
}

void nlx_core_buf_deregister(struct nlx_core_domain *lcdom,
			     struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
}

int nlx_core_buf_msg_recv(struct nlx_core_transfer_mc *lctm,
			  struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX temp: just to compile in user space */
	nlx_core_bevq_provision(lctm, lcbuf->cb_max_operations);
	nlx_core_bevq_release(lctm, lcbuf->cb_max_operations);

	return -ENOSYS;
}

int nlx_core_buf_msg_send(struct nlx_core_transfer_mc *lctm,
			  struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_active_recv(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_active_send(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_passive_recv(struct nlx_core_transfer_mc *lctm,
			      struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_passive_send(struct nlx_core_transfer_mc *lctm,
			      struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_del(struct nlx_core_transfer_mc *lctm,
		     struct nlx_core_buffer *lcbuf)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_event_wait(struct nlx_core_transfer_mc *lctm,
			    c2_time_t timeout)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

bool nlx_core_buf_event_get(struct nlx_core_transfer_mc *lctm,
			    struct nlx_core_buffer_event *lcbe)
{
	struct nlx_core_bev_link *link;
	struct nlx_core_buffer_event *bev;

	C2_PRE(lctm != NULL);
	C2_PRE(lcbe != NULL);

	/** @todo XXX temp code to cause APIs to be used */
	if (!bev_cqueue_is_empty(&lctm->ctm_bevq)) {
		link = bev_cqueue_get(&lctm->ctm_bevq);
		if (link != NULL) {
			bev = container_of(link, struct nlx_core_buffer_event,
					   cbe_tm_link);
			*lcbe = *bev;
			C2_SET0(&lcbe->cbe_tm_link); /* copy is not in queue */
			return true;
		}
	}
	return false;
}

int nlx_core_ep_addr_decode(struct nlx_core_domain *lcdom,
			    const char *ep_addr,
			    struct nlx_core_ep_addr *cepa)
{
	/** @todo XXX implement */
	uint64_t nid;
	(void) nlx_core_nidstr_decode(lcdom, ep_addr, &nid);
	return -ENOSYS;
}

void nlx_core_ep_addr_encode(struct nlx_core_domain *lcdom,
			     const struct nlx_core_ep_addr *cepa,
			     char buf[C2_NET_LNET_XEP_ADDR_LEN])
{
	/** @todo XXX implement */
	(void) nlx_core_nidstr_encode(lcdom, 0, buf);
}

int nlx_core_nidstr_decode(struct nlx_core_domain *lcdom,
			   const char *nidstr,
			   uint64_t *nid)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_nidstr_encode(struct nlx_core_domain *lcdom,
			   uint64_t nid,
			   char nidstr[C2_NET_LNET_XEP_ADDR_LEN])
{
	/** @todo XXX implement */
	return -ENOSYS;
}

int nlx_core_nidstrs_get(char * const **nidary)
{
	/** @todo XXX implement */
	return -ENOSYS;
}

void nlx_core_nidstrs_put(char * const **nidary)
{
	/** @todo XXX implement */
}

int nlx_core_tm_start(struct c2_net_transfer_mc *tm,
		      struct nlx_core_transfer_mc *lctm)
{
	struct nlx_core_buffer_event *e1;
	struct nlx_core_buffer_event *e2;

	C2_PRE(lctm != NULL);
	/** @todo XXX: temp, really belongs in async and/or kernel code */
	e1 = nlx_core_mem_alloc(sizeof *e1);
	e1->cbe_tm_link.cbl_c_self = (nlx_core_opaque_ptr_t) &e1->cbe_tm_link;
	/* ioctl call: bev_link_bless(&e1->cbe_tm_link); */
	e2 = nlx_core_mem_alloc(sizeof *e2);
	e2->cbe_tm_link.cbl_c_self = (nlx_core_opaque_ptr_t) &e2->cbe_tm_link;
	/* ioctl call: bev_link_bless(&e2->cbe_tm_link); */
	bev_cqueue_init(&lctm->ctm_bevq, &e1->cbe_tm_link, &e2->cbe_tm_link);
	C2_ASSERT(bev_cqueue_size(&lctm->ctm_bevq) == 2);

	lctm->ctm_mb_counter = C2_NET_LNET_BUFFER_ID_MIN;

	C2_POST(nlx_core_tm_invariant(lctm));
	return -ENOSYS;
}

/** @todo XXX duplicate code, see klnet_core.c, refactor during ulnet task */
static void nlx_core_bev_free_cb(struct nlx_core_bev_link *ql)
{
	struct nlx_core_buffer_event *bev;
	if (ql != NULL) {
		bev = container_of(ql, struct nlx_core_buffer_event,
				   cbe_tm_link);
		nlx_core_mem_free(bev);
	}
}

void nlx_core_tm_stop(struct nlx_core_transfer_mc *lctm)
{
	/** @todo XXX: temp, really belongs in async code */
	bev_cqueue_fini(&lctm->ctm_bevq, nlx_core_bev_free_cb);
}

int nlx_core_new_blessed_bev(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer_event **bevp)
{
	return -ENOSYS;
}

static void nlx_core_fini(void)
{
}

static int nlx_core_init(void)
{
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
