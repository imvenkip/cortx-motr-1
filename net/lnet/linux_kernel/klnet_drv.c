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

/**
   @page LNetDRVDLD LNet Transport Device DLD

   - @ref LNetDRVDLD-ovw
   - @ref LNetDRVDLD-def
   - @ref LNetDRVDLD-req
   - @ref LNetDRVDLD-depends
   - @ref LNetDRVDLD-highlights
   - @subpage LNetDRVDLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref LNetDRVDLD-lspec
      - @ref LNetDRVDLD-lspec-comps
      - @ref LNetDRVDLD-lspec-dev
      - @ref LNetDRVDLD-lspec-ioctl
      - @ref LNetDRVDLD-lspec-dominit
      - @ref LNetDRVDLD-lspec-domfini
      - @ref LNetDRVDLD-lspec-reg
      - @ref LNetDRVDLD-lspec-bev
      - @ref LNetDRVDLD-lspec-tmstart
      - @ref LNetDRVDLD-lspec-tmstop
      - @ref LNetDRVDLD-lspec-buf
      - @ref LNetDRVDLD-lspec-event
      - @ref LNetDRVDLD-lspec-nids
      - @ref LNetDRVDLD-lspec-state
      - @ref LNetDRVDLD-lspec-thread
      - @ref LNetDRVDLD-lspec-numa
   - @ref LNetDRVDLD-conformance
   - @ref LNetDRVDLD-ut
   - @ref LNetDRVDLD-st
   - @ref LNetDRVDLD-O
   - @ref LNetDRVDLD-ref

   <hr>
   @section LNetDRVDLD-ovw Overview

   The Colibri LNet Transport device provides user-space access to the kernel
   @ref LNetDLD "Colibri LNet Transport".  The
   @ref ULNetCoreDLD "User Space Core" implementation uses the device to
   communicate with the @ref KLNetCoreDLD "Kernel Core".  The device
   provides a conduit through which information flows between the user-space and
   kernel core layers, initiated by the user-space layer.  The specific
   operations that can be performed on the device are documented here.  Hooks
   for unit testing the device are also discussed.

   This design also provides a recipe and requirements for structuring each user
   space core operation and restructuring/refactoring the kernel core
   implementation, but it does not include the complete design additions and
   changes to those layers themselves.

   <hr>
   @section LNetDRVDLD-def Definitions

   Refer to <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>.

   <hr>
   @section LNetDRVDLD-req Requirements

   - @b r.c2.net.xprt.lnet.user-space The implementation must
     accommodate the needs of the user space LNet transport.
   - @b r.c2.net.xprt.lnet.dev.pin-objects The implementation must pin
     shared objects in kernel memory to ensure they will not disappear while
     in use.
   - @b r.c2.net.xprt.lnet.dev.resource-tracking The implementation must
     track all shared resources and ensure they are released properly, even
     after a user-space error.
   - @b r.c2.net.xprt.lnet.dev.safe-sharing The implementation must ensure
     that references to shared object are valid.
   - @b r.c2.net.xprt.lnet.dev.assert-free The implementation must ensure
     that the kernel module will not assert due to invalid shared state.
   - @b r.c2.net.xprt.lnet.dev.minimal-mapping The implementation must avoid
     mapping many kernel pages for long periods of time, avoiding excessive
     use of kernel high memory page map.
   - @b r.c2.net.xprt.lnet.dev.addb The implementation must log significant
     events, such as device open, close and failures to ADDB.

   <hr>
   @section LNetDRVDLD-depends Dependencies

   - @ref LNetCore "LNet Transport Core Interface" <!-- ../lnet_core.h --> <br>
     Several modifications are required on the Core interface itself:
     - A new @c nlx_core_buffer_event::cbe_kpvt pointer is required that can be
       set to refer to the new @c nlx_kcore_buffer_event object.
     - The @c nlx_core_tm_start() function is changed to remove the @c cepa and
       @c epp parameters.  The @c cepa parameter is always the same as the
       @c lctm->ctm_addr.  The Core API does not use a @c c2_net_end_point,
       so setting the @c epp at the core layer was inappropriate.  The LNet XO
       layer, which does use the end point, is modified to allocate this
       end point in the @c nlx_tm_ev_worker() logic itself.
     - The user space transport must ensure that shared objects do not
       cross page boundaries.  This applies only to shared core objects such as
       @c nlx_core_transfer_mc, not to buffer pages.  Since object allocation
       is actually done in the LNet XO layer (except for the
       @c nlx_core_buffer_event), this appears to require that an allocation
       wrapper function be added to the Core Interface, implemented separately
       in the kernel and user transports, because the kernel transport has no
       such limitation and the @c c2_alloc_aligned() API, which could be used to
       satisfy this requirement, requires page aligned data or greater in kernel
       space.

   - @ref ULNetCore "LNet Transport Core User Private Interface" <!--
       ../ulnet_core.c --> <br>
     Besides the existence of this interface, the following dependencies
     exist:
     - The user space transport must call the ioctl requests specified in this
       DLD in the manner prescribed in the @ref LNetDRVDLD-lspec below.
     - The user space transport must implement the new object allocator required
       by the Core Interface.

   - @ref KLNetCore "LNet Transport Core Kernel Private Interface" <!--
       ./lnet_core.h --> <br>
     Several modifications are required in this interface:
     - The kernel core interfaces that initialize objects with @c c2_addb_ctx
       members must be changed such that their parent is the
       @c nlx_kcore_domain::kd_addb ADDB context.  The initialization of the
       @c nlx_kcore_domain object must be changed such that its parent is the
       @c ::c2_net_addb context.  The parent of an ADDB context must be in the
       same address space, so kernel core objects cannot depend on c2_net layer
       objects as parents, because such objects can be in user space.
     - The @c bev_cqueue_pnext() and @c bev_cqueue_put() should be modified or
       wrapped such that they can @c kmap() and @c kunmap() the
       @c nlx_core_buffer_event object (preferably, the atomic form of these
       functions can be used).  This may also require storing a page
       pointer and offset in the @c nlx_core_bev_link.
     - Many of the Core APIs implemented in the kernel must be refactored
       such that the portion that can be shared between the kernel-only
       transport and the kernel driver is moved into a new API.  The Core
       API is should be changed to perform pre-checks, call the new shared API
       and complete post-shared operations.  The user transport tasks described
       in the @ref LNetDRVDLD-lspec can be used as a guide for how to refactor
       the Kernel Core implementation.  In addition, the operations on the
       @c nlx_kcore_ops structure should guide the signatures of the
       refactored, shared operations.
     - The @c nlx_kcore_umd_init() function should be changed to set the
       MD @c user_ptr to the @c nlx_kcore_buffer, not the @c nlx_core_buffer.
       This allows the shared buffer to be mapped and unmapped as needed, rather
       that keeping it mapped for long periods.  All other uses of the MD
       @c user_ptr field must be changed accordingly.
     - Various blocks of @c C2_PRE() assertions used to validate shared objects
       before they are referenced should be refactored into new invariant-style
       functions so the driver can perform the checks and return an error
       without causing an kernel assertion.
     - The kernel transport must implement the new object allocator required
       by the Core Interface.

   <hr>
   @section LNetDRVDLD-highlights Design Highlights

   - The device provides ioctl-based access to the Kernel LNet Core Interface.
   - Ioctl requests correspond roughly to the
     @ref LNetCore "LNet Transport Core APIs".
   - Each user-space @c c2_net_domain corresponds to opening a separate
     file descriptor.
   - The device driver tracks all resources associated with the file descriptor.
   - Well-defined patterns are used for sharing new resources between user
     and kernel space, referencing previously shared resources, and releasing
     shared resources.
   - The device driver can clean up a domain's resources in the case that the
     user program terminates prematurely.

   <hr>
   @section LNetDRVDLD-lspec Logical Specification

   - @ref LNetDRVDLD-lspec-comps
   - @ref LNetDRVDLD-lspec-dev
   - @ref LNetDRVDLD-lspec-ioctl
   - @ref LNetDRVDLD-lspec-dominit
   - @ref LNetDRVDLD-lspec-domfini
   - @ref LNetDRVDLD-lspec-reg
   - @ref LNetDRVDLD-lspec-bev
   - @ref LNetDRVDLD-lspec-tmstart
   - @ref LNetDRVDLD-lspec-tmstop
   - @ref LNetDRVDLD-lspec-buf
   - @ref LNetDRVDLD-lspec-event
   - @ref LNetDRVDLD-lspec-nids
   - @ref LNetDRVDLD-lspec-state
   - @ref LNetDRVDLD-lspec-thread
   - @ref LNetDRVDLD-lspec-numa

   @subsection LNetDRVDLD-lspec-comps Component Overview

   The LNet Device Driver is a layer between the user space transport
   core and the kernel space transport core.  The driver layer provides
   a mechanism for the user space to interact with the Lustre LNet kernel
   module.  It uses a subset of the kernel space transport core interface
   to implement this interaction.

   @see <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>,
   specifically the Design Highlights component diagram.

   For reference, the relationship between the various components of the LNet
   transport and the networking layer is illustrated in the following UML
   diagram.
   @image html "../../net/lnet/lnet_xo.png" "LNet Transport Objects"

   The LNet Device Driver has no sub-components.  It has several internal
   functions that interact with the kernel space transport core layer.

   @subsection LNetDRVDLD-lspec-dev Device Setup and Shutdown

   The LNet device is registered with the kernel using the @c nlx_dev_init()
   function when the Colibri Kernel module is loaded.  This function is
   called by the existing @c nlx_core_init() function.  The function performs
   the following tasks.
   - It registers the device with the kernel.  The device is registered as
     a miscellaneous device named "c2lnet".  As such, registration causes the
     device to appear as "/dev/c2lnet" in the device file system.
   - Sets a flag, @c ::nlx_dev_registered, denoting successful device
     registration.

   The LNet device is deregistered with the kernel using the @c nlx_dev_fini()
   function when the Colibri Kernel module is unloaded.  This function is
   called by the existing @c nlx_core_fini() function.  The function performs
   the following task.
   - If device registration was performed successfully, deregisters the
     device and resets the @c ::nlx_dev_registered flag.

   @subsection LNetDRVDLD-lspec-ioctl Ioctl Request Behavior

   The @ref ULNetCore "user space implementation" of the
   @ref LNetCore "LNet Transport Core Interface" interacts with the
   @ref KLNetCore "LNet Transport Kernel Core" via ioctl requests.
   The file descriptor required to make the ioctl requests is obtained during
   the @ref LNetDRVDLD-lspec-dominit operation.

   All further interaction with the device, until the file descriptor is
   closed, is via ioctl requests.  Ioctl requests are served by the
   @c nlx_dev_ioctl() function, an implementation of the kernel @c
   file_operations::unlocked_ioctl() function.  This function performs the
   following steps.

   - It validates the request.
   - It copies in (from user space) the parameter object corresponding to the
     specific request for most _IOW requests.  Note that the requests that take
     pointers instead of parameter objects do not copy in, because the pointers
     are either references to kernel objects or shared objects to be pinned, not
     copied.
   - It calls a helper function to execute the command; specific helper
     functions are called out in the following sections.  The helper function
     will call a kernel core operation to execute the behavior shared between
     user space and kernel transports.  It does this indirectly from operations
     defined on the @c nlx_kcore_domain::kd_drv_ops operation object.
   - It copies out (to user space) the parameter object corresponding to the
     specific request for _IOR and _IOWR requests.
   - It returns the status, generally of the helper function.  This status
     follows the typical 0 for success, -errno for failure, except as
     specified for certain helper functions.

   Some ioctl requests have the side effect of pinning user pages in memory.
   However, the mapping of pages (i.e. @c kmap() or @c kmap_atomic() functions)
   is performed only while the pages are to be used, and then unmapped as soon
   as possible.  The number of mappings available to @c kmap() is documented as
   being limited.  Except as noted, @c kmap_atomic() is used in atomic blocks to
   map the page associated with an object.  Each time a shared object is mapped,
   its invariants are re-checked to ensure the page still contains the shared
   object.  Note that each shared core object is required to fit within a
   single page.  The user space transport must ensure this requirement is met
   when it allocates core objects.  Note that the pages of the @c c2_bufvec
   segments are not part of the shared @c nlx_core_buffer; they are referenced
   by the associated @c nlx_kcore_buffer object and are never mapped by the
   driver or kernel core layers.

   The helper functions verify that the requested operation will not cause an
   assertion in the kernel core.  This is done by performing the same checks the
   Kernel Core APIs would do, but without asserting. Instead, they log an ADDB
   record and return an error status when verification fails.  The user space
   core can detect the error status and assert the user space process.  The
   error code @c -EFAULT is used to report both verification failure and
   failure to pin shared pages.  Specific helper functions may return additional
   well-defined errors.

   @see @ref LNetDevInternal

   @subsection LNetDRVDLD-lspec-dominit Domain Initialization

   The LNet Transport Device Driver is first accessed during domain
   initialization.  In this case, the following sequence of tasks
   is performed by the user space transport.

   - It allocates a @c nlx_core_domain object.
   - It performs upper layer initialization of this object, including allocating
     the @c nlx_ucore_domain object and setting the @c nlx_core_domain::cd_upvt
     field.
   - It opens the device using the @c open() system call.  The device is named
     @c "/dev/c2lnet" and the device is opened with @c O_RDWR flag.  The
     file descriptor is saved in the @c nlx_ucore_domain::ud_fd field.
   - It shares the @c nlx_core_domain object via the @c #C2_LNET_DOM_INIT
     ioctl request.  Note that a side effect of this request is that the
     @c nlx_core_domain::cd_kpvt is set.
   - It completes user space initialization of the @c nlx_core_domain object and
     the @c nlx_ucore_domain object.

   In the kernel, the @c open() and @c ioctl() system calls are handled by
   the @c nlx_dev_open() and @c nlx_dev_ioctl() subroutines, respectively.

   The @c nlx_dev_open() performs the following sequence.
   - It increases the reference count on the module to ensure it will not be
     unloaded while user space references exist.
   - It allocates and initializes a @c nlx_kcore_domain object, using a
     refactored @c nlx_core_dom_init() function, and assigns the object to the
     @c file->private_data field.
   - It logs an ADDB record recording the occurrence of the open operation.

   The @c nlx_dev_ioctl() is described generally
   @ref LNetDRVDLD-lspec-ioctl "above".  It uses the helper function
   @c nlx_dev_ioctl_dom_init() to complete kernel domain initialization.
   The following tasks are performed.
   - The @c nlx_kcore_domain::kd_drv_mutex() is locked.
   - The @c nlx_kcore_domain is verified to ensure the domain is not already
     initialized.
   - The @c nlx_core_domain object is pinned in kernel memory.
   - The @c nlx_core_domain is validated to ensure no assertions will occur.
   - Information required to map the pinned object is saved in the
     @c nlx_kcore_domain object.
   - The @c nlx_core_domain::cd_kpvt field is set.
   - The @c nlx_kcore_domain::kd_drv_mutex() is unlocked.

   @subsection LNetDRVDLD-lspec-domfini Domain Finalization

   During domain finalization, the user space transport will perform
   the following steps.

   - It completes any pre-checks and pre-finalization of the @c nlx_ucore_domain
     and @c nlx_core_domain objects.
   - It performs the @c #C2_LNET_DOM_FINI ioctl request to request that the
     kernel finalize its private data and release resources.
   - It calls @c close() to release the file descriptor.
   - It completes any post-finalization steps, such as freeing its
     @c nlx_ucore_domain object.

   In the kernel, the @c ioctl() and @c close() system calls are handled by
   the @c nlx_dev_ioctl() and @c nlx_dev_close() subroutines, respectively.
   Technically, @c nlx_dev_close() is called once by the kernel when the last
   reference to the file is closed (e.g. if the file descriptor had been
   duplicated or the process forked children and they inherited the open
   file descriptor).

   The @c nlx_dev_ioctl() is described generally
   @ref LNetDRVDLD-lspec-ioctl "above".  It uses the helper function
   @c nlx_dev_ioctl_dom_fini() to complete kernel domain finalization.
   The following tasks are performed.
   - The @c nlx_kcore_domain::kd_drv_mutex() is locked.
   - It verifies that the domain was not previously finalized and can
     be finalized without assertions.
   - It calls the @c nlx_core_dom_fini() function to finalize the domain.
   - It unpins the shared @c nlx_core_domain object, resetting the corresponding
     fields of the @c nlx_kcore_domain object.
   - The @c nlx_kcore_domain::kd_drv_mutex() is unlocked.

   The @c nlx_dev_close() API is called when the last reference to the file goes
   away.  This is either via orderly finalization as described above, or various
   error paths, such as the process aborting, the file descriptor being closed
   erroneously, and so on.  It performs the following sequence.

   - It verifies that the domain was previously finalized, by verifying
     that the @c nlx_kcore_domain::kd_drv_page is NULL.
   - If the domain was not previously finalized, it completes finalization
     itself.
     - Any running transfer machines must be stopped, their pending
       operations aborted.
     - Shared @c nlx_core_transfer_mc objects must be unpinned.
     - Each corresponding @c nlx_kcore_transfer_mc object is freed.
     - All registered buffers must be deregistered.
     - Shared @c nlx_core_buffer objects must be unpinned.
     - Each corresponding @c nlx_kcore_buffer object is freed.
     - Shared @c nlx_core_buffer_event objects must be unpinned.
     - Each corresponding @c nlx_kcore_buffer_event object is freed.
     - Finalize the @c nlx_kcore_domain object, as described for the
       @c nlx_dev_ioctl_dom_fini() above.
     - The improper finalization is logged with ADDB.
   - It resets (to NULL) the @c file->private_data.
   - It frees the @c nlx_kcore_domain object.
   - It decrements the reference count on the module.
   - It logs an ADDB record recording the occurrence of the close operation.

   @subsection LNetDRVDLD-lspec-reg Buffer Registration and De-registration

   The following ioctl requests are available for use by the user space
   transport to obtain kernel parameters controlling buffer size.
   - @c #C2_LNET_MAX_BUFFER_SIZE
   - @c #C2_LNET_MAX_BUFFER_SEGMENT_SIZE
   - @c #C2_LNET_MAX_BUFFER_SEGMENTS

   These ioctl requests are handled by the following helper functions,
   respectively.
   - @c nlx_dev_ioctl_max_buffer_size()
   - @c nlx_dev_ioctl_max_buffer_segment_size()
   - @c nlx_dev_ioctl_max_buffer_segments()

   These helper functions simply return the values of the kernel implementation
   of following core functions, respectively.  As such, the corresponding ioctl
   requests return a positive number on success, not 0.
   - @c nlx_core_get_max_buffer_size()
   - @c nlx_core_get_max_buffer_segment_size()
   - @c nlx_core_get_max_buffer_segments()

   The user space transport completes the following tasks to perform
   buffer registration.

   - It performs upper layer initialization of the @c nlx_core_buffer object.
     This includes allocating and initializing the user space private object and
     setting the @c nlx_core_buffer::cb_upvt field.
   - It declares a @c c2_lnet_dev_buf_register_params object, setting
     the dbr_lcbuf and dbr_buffer_id from the corresponding
     @c nlx_core_buf_register() parameters.
   - It copies the data referenced by the bvec parameter to the dbr_bvec
     field.
   - It performs a @c #C2_LNET_BUF_REGISTER ioctl request to share the buffer
     with the kernel and complete the kernel part of buffer registration.
   - It completes any user-space initialization of the @c nlx_core_buffer and
     user space private objects.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_buf_register() to complete kernel buffer registration.
   The following tasks are performed.
   - The parameters are validated to ensure no assertions will occur.
   - The @c c2_bufvec::ov_buf is copied in, temporarily (to avoid issues of the
     list crossing page boundaries that might occur by mapping the pages
     directly), and the corresponding field of the @c
     c2_lnet_dev_buf_register_params::dbr_bvec is updated to refer to this copy.
   - The @c nlx_core_buffer, @c c2_lnet_dev_buf_register_params::dbr_lcbuf,
     is pinned in kernel memory.
   - The @c nlx_core_buffer is checked to ensure it is not already associated
     with a @c nlx_kcore_buffer object.
   - A refactored @c nlx_core_buf_register() is used to allocate and initialize
     the @c nlx_kcore_buffer object.
   - Information required to map the pinned object is saved in the
     @c nlx_kcore_buffer object.
   - An API similar to @c bufvec_seg_kla_to_kiov() is used to pin the pages
     of the buffer segments and initialize the @c nlx_kcore_buffer::kb_kiov.
   - The @c nlx_kcore_buffer is added to the @c nlx_kcore_domain::kd_drv_buffers
     list.

   The user space transport completes the following tasks to perform
   buffer de-registration.

   - It completes pre-checks of the @c nlx_core_buffer object.
   - It performs a @c #C2_LNET_BUF_DEREGISTER ioctl request, causing
     the kernel to complete the kernel part of buffer de-registration.
   - It completes any user-space de-registration of the @c nlx_core_buffer and
     user space private objects.
   - It frees the user space private object and resets the
     @c nlx_core_buffer::cb_upvt to NULL.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_buf_deregister() to complete kernel buffer de-registration.
   The following tasks are performed.
   - The parameters are validated to ensure no assertions will occur.
   - The pages associated with the buffer, referenced by
     @c nlx_kcore_buffer::kb_kiov, are unpinned.
   - The buffer is removed from the @c nlx_kcore_domain::kd_drv_buffers list.
   - A refactored @c nlx_core_buf_deregister() is used to de-register
     the buffer and free the corresponding @c nlx_kcore_buffer object.
   - The @c nlx_core_buffer is unpinned.

   @subsection LNetDRVDLD-lspec-bev Managing the Buffer Event Queue

   The @c nlx_core_new_blessed_bev() helper allocates and blesses buffer event
   objects.  In user space, blessing the object requires interacting with the
   kernel.  After the object is blessed by the kernel, the user space transport
   can add it to the buffer event queue directly, without further kernel
   interaction.  The following steps are taken by the user space transport.

   - It allocates a new @c nlx_core_buffer_event object.
   - It declares a @c c2_lnet_dev_bev_bless_params object and sets its fields.
   - It performs a @c #C2_LNET_BEV_BLESS ioctl request to share the
     @c nlx_core_buffer_event object with the kernel and complete the kernel
     part of blessing the object.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_bev_bless() to complete blessing the buffer event object.
   The following tasks are performed.
   - The parameters are validated to ensure no assertions will occur.
   - The @c nlx_core_buffer_event is pinned in kernel memory.
   - The @c nlx_core_buffer_event is checked to ensure it is not already
     associated with a @c nlx_kcore_buffer_event object.
   - A @c nlx_kcore_buffer_event object is allocated and initialized.
   - Information required to map the pinned object is saved in the
     @c nlx_kcore_buffer_event object.
   - The @c bev_link_bless() function is called to bless the object.
   - The object is added to the @c nlx_kcore_transfer_mc::ktm_drv_bevs list.

   Buffer event objects are never removed from the buffer event queue until
   the transfer machine is stopped.

   @see @ref LNetDRVDLD-lspec-tmstop

   @subsection LNetDRVDLD-lspec-tmstart Starting a Transfer Machine

   The user space transport completes the following tasks to start a
   transfer machine.  Recall that there is no core API corresponding to the
   @c nlx_xo_tm_init() function.

   - It performs upper layer initialization of the @c nlx_core_transfer_mc
     object.  This includes allocating and initializing the user space private
     object and setting the @c nlx_core_transfer_mc::ctm_upvt field.
   - It performs a @c #C2_LNET_TM_START ioctl request to share the
     @c nlx_core_transfer_mc object with the kernel and complete the kernel
     part of starting the transfer machine.
   - It allocates and initializes two @c nlx_core_buffer_event objects, using
     the user space @c nlx_core_new_blessed_bev() helper.
   - It completes the user-space initialization of the @c nlx_core_buffer and
     user space private objects.  This including initializing the buffer event
     circular queue using the @c bev_cqueue_init() function.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_tm_start() to complete starting the transfer machine.
   The following tasks are performed.
   - The parameters are validated to ensure no assertions will occur.
   - The @c nlx_core_transfer_mc object is pinned in kernel memory.
   - The @c nlx_core_transfer_mc is checked to ensure it is not already
     associated with a @c nlx_kcore_transfer_mc object.
   - The @c nlx_core_transfer_mc is mapped using @c kmap()
     because the core operation cannot be guaranteed to be atomic.
   - A refactored @c nlx_core_tm_start() is used to allocate and initialize
     the @c nlx_kcore_transfer_mc object.
   - Information required to later remap the pinned object is saved in the
     @c nlx_kcore_transfer_mc object.
   - The @c nlx_core_transfer_mc is unmapped.
   - The @c nlx_kcore_transfer_mc is added to the
     @c nlx_kcore_domain::kd_drv_tms list.

   @subsection LNetDRVDLD-lspec-tmstop Stopping a Transfer Machine

   The user space transport completes the following tasks to stop a
   transfer machine.  Recall that there is no core API corresponding to the
   @c nlx_xo_tm_fini() function.

   - It completes pre-checks of the @c nlx_core_transfer_mc object.
   - It performs a @c #C2_LNET_TM_STOP ioctl request, causing
     the kernel to complete the kernel part of stopping the transfer machine.
   - It frees the buffer event queue using the @c bev_cqueue_fini() function.
   - It frees the user space private object and resets the
     @c nlx_core_transfer_mc::ctm_upvt to NULL.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_tm_stop() to complete stopping the transfer machine.
   The following tasks are performed.
   - The parameters are validated to ensure no assertions will occur.
   - The transfer machine is removed from the @c nlx_kcore_domain::kd_drv_tms
     list.
   - The buffer event objects on the @c nlx_kcore_transfer_mc::ktm_drv_bevs list
     are unpinned and their corresponding @c nlx_kcore_buffer_event objects
     freed.
   - A refactored @c nlx_core_tm_stop() is used to stop the transfer machine.
   - The @c nlx_core_transfer_mc is unpinned.

   @subsection LNetDRVDLD-lspec-buf Transfer Machine Buffer Queue Operations

   Several LNet core interfaces operate on buffers and transfer machine queues.
   In all user transport cases, the shared objects, @c nlx_core_buffer and
   @c nlx_core_transfer_mc, must have been previously shared with the kernel,
   through use of the @c #C2_LNET_BUF_REGISTER and @c #C2_LNET_TM_START ioctl
   requests, respectively.

   The ioctl requests available to the user space transport for managing
   buffers and transfer machine buffer queues are as follows.
   - @c #C2_LNET_BUF_MSG_RECV
   - @c #C2_LNET_BUF_MSG_SEND
   - @c #C2_LNET_BUF_ACTIVE_RECV
   - @c #C2_LNET_BUF_ACTIVE_SEND
   - @c #C2_LNET_BUF_PASSIVE_RECV
   - @c #C2_LNET_BUF_PASSIVE_SEND
   - @c #C2_LNET_BUF_DEL

   In each case, the user space transport performs the following steps.
   - Validates the parameters.
   - Declares a @c c2_lnet_dev_buf_queue_params object and sets the two fields.
     In this case, both fields are set to the kernel private pointers of
     the shared object.
   - Performs the appropriate ioctl request from the list above.

   The ioctl requests are handled by the following helper functions,
   respectively.
   - @c nlx_dev_ioctl_buf_msg_recv()
   - @c nlx_dev_ioctl_buf_msg_send()
   - @c nlx_dev_ioctl_buf_active_recv()
   - @c nlx_dev_ioctl_buf_active_send()
   - @c nlx_dev_ioctl_buf_passive_recv()
   - @c nlx_dev_ioctl_buf_passive_send()
   - @c nlx_dev_ioctl_buf_del()

   These helper functions each perform similar tasks.
   - The parameters are validated to ensure no assertions will occur.
   - The @c nlx_core_transfer_mc and @c nlx_core_buffer are mapped using
     @c kmap() because the core operations cannot be guaranteed to be atomic.
   - The corresponding kernel core operation is called.
     - @c nlx_core_buf_msg_recv()
     - @c nlx_core_buf_msg_send()
     - @c nlx_core_buf_active_recv()
     - @c nlx_core_buf_active_send()
     - @c nlx_core_buf_passive_recv()
     - @c nlx_core_buf_passive_send()
     - @c nlx_core_buf_del()
   - The @c nlx_core_transfer_mc and @c nlx_core_buffer are unmapped.

   @subsection LNetDRVDLD-lspec-event Waiting for Buffer Events

   The user space transport completes the following tasks to wait for
   buffer events.
   - It declares a @c c2_lnet_dev_buf_event_wait_params and sets the fields.
   - It performs a @c #C2_LNET_BUF_EVENT_WAIT ioctl request to wait for
     the kernel to generate additional buffer events.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_buf_event_wait() to perform the wait operation.
   The following tasks are performed.

   - The parameters are validated to ensure no assertions will occur.
   - The @c nlx_core_transfer_mc is mapped using @c kmap()
     because the core operation will likely block.
   - The @c nlx_core_buf_event_wait() function is called.
   - The @c nlx_core_transfer_mc is unmapped.

   @subsection LNetDRVDLD-lspec-nids Node Identifier Support

   Operations involving NID strings require ioctl requests to access
   kernel-only functions.

   Most of the refactored @c nlx_core_ep_addr_decode() and
   @c nlx_core_ep_addr_encode() functions can be implemented directly
   in user space.  However, converting a NID to a string or visa versa
   requires access to functions which exists only in the kernel.

   To convert a NID string to a NID, the user space transport performs the
   following tasks.
   - It declares a @c c2_lnet_dev_nid_encdec_params and sets the @c dn_buf to
     the string to be decoded.
   - It calls the @c #C2_LNET_NIDSTR_DECODE ioctl request to cause the kernel
     to decode the string.  On successful return, the @c dn_nid field will be
     set to the corresponding NID.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_nidstr_decode() to decode the string.
   The following tasks are performed.

   - The parameter is validated to ensure no assertions will occur.
   - The @c libcfs_str2nid() function is called to convert the string to a NID.
   - In the case the result is LNET_NID_ANY, -EINVAL is returned,
     otherwise the @c dn_nid field is set.

   To convert a NID into a NID string, the user space transport performs the
   following tasks.
   - It declares a @c c2_lnet_dev_nid_encdec_params and sets the @c dn_nid to
     the value to be converted.
   - It calls the @c #C2_LNET_NIDSTR_ENCODE ioctl request to cause the kernel
     to encode the string.  On successful return, the @c dn_buf field will be
     set to the corresponding NID string.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_nidstr_encode() to decode the string.
   The following tasks are performed.

   - The parameter is validated to ensure no assertions will occur.
   - The @c libcfs_nid2str() function is called to convert the string to a NID.
   - The resulting string is copied to the the @dn_buf field.

   The final operations involving NID strings are the @c nlx_core_nidstrs_get()
   and @c nlx_core_nidstrs_put() operations.  The user space transport obtains
   the strings from the kernel using the @c #C2_LNET_NIDSTRS_GET ioctl request.
   This ioctl request returns a copy of the strings, rather than sharing a
   reference to them.  As such, there is no ioctl request to "put" the strings.
   To get the list of strings, the user space transport performs the following
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

   When the user space transport does this is beyond the scope of this
   specification.  Currently, the kernel implementation caches the NID strings
   once; the user space transport might assume this behavior, but may need to
   change in the future if the kernel implementation changes.

   The @c nlx_dev_ioctl() uses the helper function
   @c nlx_dev_ioctl_nidstrs_get() to decode the string.
   The following tasks are performed.

   - The parameters are validated to ensure no assertions will occur.
   - The @c nlx_core_nidstrs_get() API is called to get the list of NID strings.
   - The user space buffer is temporarily pinned and mapped.
   - The NID strings are copied consecutively into the buffer.  Each NID string
     is nul terminated and an extra nul is written after the final NID string.
   - The @c nlx_core_nidstrs_put() API is called to release the list of NID
     strings.
   - The number of NID strings is returned on success; @c nlx_dev_ioctl()
     returns this positive number instead of the typical 0 for success.
   - The value -EFBIG is returned if the buffer is not big enough.

   @subsection LNetDRVDLD-lspec-state State Specification

   The LNet device driver does not introduce its own state model but operates
   within the frameworks defined by the Colibri Networking Module and the Kernel
   device driver interface.  In general, resources are pinned and allocated when
   an object is first shared with the kernel by the the user space process and
   are freed and unpinned when the user space requests.  To ensure there is no
   resource leakage, remaining resources are freed when the @c nlx_dev_close()
   API is called.

   The resources managed by the driver are tracked by the following lists:
   - @c nlx_kcore_domain::kd_drv_page (a single item)
   - @c nlx_kcore_domain::kd_drv_tms
   - @c nlx_kcore_domain::kd_drv_buffers
   - @c nlx_kcore_transfer_mc::ktm_drv_bevs

   @subsection LNetDRVDLD-lspec-thread Threading and Concurrency Model

   The LNet device driver has no threads of its own.  It operates within
   the context of a user space process and a kernel thread operating on behalf
   of that process.  All operations are invoked through the Linux device
   driver interface, specifically the operations defined on the
   @c ::nlx_dev_file_ops object.  The @c nlx_dev_open() and @c nlx_dev_close()
   are guaranteed to be called once each for each kernel @c file object, and
   calls to these operations are guaranteed to not overlap with calls to the
   @c nlx_dev_ioctl() operation. However, multiple calls to @c nlx_dev_ioctl()
   may occur simultaneously on different threads.

   Synchronization of device driver resources is controlled by a single mutex
   per domain, the @c nlx_kcore_domain::kd_drv_mutex.  This mutex must be
   held while manipulating the resource lists, @c nlx_kcore_domain::kd_drv_tms,
   @c nlx_kcore_domain::kd_drv_buffers and
   @c nlx_kcore_transfer_mc::ktm_drv_bevs.  The mutex must also be held
   while a shared object is temporarily mapped and its corresponding core
   pointer is manipulated, for example, the @c nlx_kcore_transfer_mc::ktm_ctm
   pointer.  This is to ensure that two threads will not attempt to set
   this pointer differently across calls to the Kernel Core APIs.  The mutex
   does not need to be held while mapping a page atomically, if a private
   pointer (e.g. on the stack) is used to refer to the object.  The mutex may
   also be used to serialize driver ioctl requests, such as in the case of
   @c #C2_LNET_DOM_INIT and @c #C2_LNET_DOM_FINI. The driver mutex must be
   obtained before any other Net or Kernel LNet mutex.

   The @c nlx_kcore_domain::kd_drv_mutex may be released, e.g. to perform
   an operation that may block, as long as the
   @c nlx_kcore_domain::kd_drv_inuse is first increment.  After
   re-obtaining the mutex, the counter must be decremented.  This ensures that
   a page will not be unpinned while it is in use, for example.  Note that
   the user space transport is supposed to synchronize itself so that it does
   not attempt to de-register a buffer that is in use.  Attempting to do so
   should result in an assertion; the use of the counter helps to ensure
   such an assertion cannot occur in the kernel on behalf of the user process.

   @subsection LNetDRVDLD-lspec-numa NUMA optimizations

   The LNet device driver does not allocate threads.  The user space application
   can control thread processor affiliation by confining the threads it uses
   to access the device driver via system calls.

   <hr>
   @section LNetDRVDLD-conformance Conformance

   - @b i.c2.net.xprt.lnet.user-space The @ref LNetDRVDLD-lspec covers how
     each LNet Core operation in user space can be implemented using the
     driver ioctl requests.
   - @b i.c2.net.xprt.lnet.dev.pin-objects See @ref LNetDRVDLD-lspec-dominit,
     @ref LNetDRVDLD-lspec-reg, @ref LNetDRVDLD-lspec-bev,
     @ref LNetDRVDLD-lspec-tmstart.
   - @b i.c2.net.xprt.lnet.dev.resource-tracking
     See @ref LNetDRVDLD-lspec-domfini.
   - @b i.c2.net.xprt.lnet.dev.safe-sharing See @ref LNetDRVDLD-lspec-ioctl,
     specifically the paragraph covering the use of pinned pages.
   - @b i.c2.net.xprt.lnet.dev.assert-free See @ref LNetDRVDLD-lspec-ioctl.
   - @b i.c2.net.xprt.lnet.dev.minimal-mapping None of the work flows in the
     @ref LNetDRVDLD-lspec require that objects remain mapped across ioctl
     calls.  The @ref LNetDRVDLD-lspec-ioctl calls for use of @c kmap_atomic()
     when possible.
   - @b i.c2.net.xprt.lnet.dev.addb See @ref LNetDRVDLD-lspec-dominit,
     @ref LNetDRVDLD-lspec-domfini, @ref LNetDRVDLD-lspec-ioctl.

   <hr>
   @section LNetDRVDLD-ut Unit Tests

   LNet Device driver unit tests focus on the covering the common code
   paths.  Code paths involving most Kernel LNet Core operations and
   the device wrappers will be handled as part of testing the user transport.
   Even so, some tests are most easily performed by coordinating user space
   code with kernel unit tests.  The following strategy will be used:
   - When the LNet unit test suite is initialized in the kernel, it creates
     a /proc/c2_lnet_ut file, registering read and write file operations.
   - A user space program is started concurrently with the kernel unit tests.
   - The user space program waits for the /proc/c2_lnet_ut to appear.
   - The user space program writes a message to the /proc/c2_lnet_ut to
     synchronize with the kernel unit test.
   - The user space program loops.
     - The user space program reads the /proc/c2_lnet_ut for instructions.
     - Each instruction tells the user space program which test to perform;
       there is a special instruction to tell the user space program the
       unit test is complete.
     - The user space program writes the test result back.
   - When the LNet unit test suite is finalized in the kernel, the
     /proc/c2_lnet_ut file is removed.

   To enable unit testing of the device layer without requiring full kernel
   core behavior, the device layer accesses kernel core operations indirectly
   via the @c nlx_kcore_domain::kd_drv_ops operation structure.  During unit
   tests, these operations can be changed to call mock operations instead of
   the real kernel core operations.  This allows testing of things such as
   pinning and mapping pages without causing real core behavior to occur.

   @test Initializing the device causes it to be registered and visible
         in the file system.

   @test The device can be opened and closed.

   @test Reading or writing the device fails.

   @test Unsupported ioctl requests fail.

   @test A nlx_core_domain can be initialized and finalized, testing common
         code paths and the strategy of pinning and unpinning pages.

   @test A nlx_core_domain can be initialized and then the device is closed,
         and cleanup occurs.

   @test A nlx_core_domain is initialized, and several nlx_core_transfer_mc
         objects can be started and then stopped, the domain finalized and the
         device is closed.  No cleanup is necessary.

   @test A nlx_core_domain is initialized, and the same nlx_core_transfer_mc
         object is started twice, the error is detected.  The
         device is closed and cleanup occurs.

   @test A nlx_core_domain and a nlx_core_transfer_mc are initialized,
         then the nlx_core_transfer_mc is corrupted and subsequently
         used for another request.  The ioctl handler detects the corrupt
         object.

   @test A nlx_core_domain, and several nlx_core_transfer_mc objects can
         be registered, then the device is closed, and cleanup occurs.

   Buffer and buffer event management tests and more advanced domain and
   transfer machine test will be added as part of testing the user space
   transport.

   <hr>
   @section LNetDRVDLD-st System Tests
   System testing will be performed as part of the transport operation system
   test.

   <hr>
   @section LNetDRVDLD-O Analysis
   - The algorithmic complexity of ioctl requests is constant, except
     - the complexity of pinning a buffer varies with the number of pages in
       the buffer,
     - the complexity of stopping a transfer machine is proportional to the
       number of buffer events pinned.
   - The time to pin or @c kmap() a page is unpredictable, and depends, at the
     minimum, on current system load, memory consumption and other LNet users.
     For this reason, @c kmap_atomic() should be used when a shared page can
     be used without blocking.
   - The driver layer consumes a small amount of additional memory in the form
     of additional fields in the various kernel core objects.
   - The use of stack pointers instead of pointers within kernel core
     objects while mapping shared objects, and the use of
     @c nlx_kcore_domain::kd_drv_inuse avoids holding the
     @c nlx_kcore_domain::kd_drv_mutex which would otherwise reduce concurrency.

   <hr>
   @section LNetDRVDLD-ref References
   - <a href="http://lwn.net/Kernel/LDD3/">Linux Device Drivers, Third Edition By Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, 2005</a>
   - <a href="http://lwn.net/Articles/119652/">The new way of ioctl(), Jonathan Corbet, 2005</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   - @ref LNetDLD "LNet Transport DLD"
   - @ref ULNetCoreDLD "LNet Transport User Space Core DLD"
   - @ref KLNetCoreDLD "LNet Transport Kernel Space Core DLD"
 */

#include "net/lnet/lnet_ioctl.h"
#include "klnet_drv.h"

C2_BASSERT(sizeof(struct nlx_xo_domain) < PAGE_SIZE);
C2_BASSERT(sizeof(struct nlx_xo_transfer_mc) < PAGE_SIZE);
C2_BASSERT(sizeof(struct nlx_xo_buffer) < PAGE_SIZE);
C2_BASSERT(sizeof(struct nlx_core_buffer_event) < PAGE_SIZE);

/**
   @defgroup LNetDevInternal LNet Transport Device Internals
   @ingroup LNetDev
   @brief Detailed functional specification of the internals of the
   LNet Transport Device

   @see @ref LNetDRVDLD "LNet Transport Device DLD" and @ref LNetDRVDLD-lspec

   @{
 */

enum {
	DD_MAGIC = 0x64645f6d61676963ULL, /* dd_magic */
	DD_INITIAL_VALUE = 41,
};

/**
   Track each pinned memory region in the prototype.
   @todo Replace uses of this object with lists rooted on nlx_kcore_domain
 */
struct mock_mem_area {
	/** Opaque user space address corresponding to this memory area. */
	unsigned long ma_user_addr;
	/** Page for the pinned object. Require each object to be < PAGE_SIZE
	    and be aligned such that it fully fits in the page.
	 */
	struct page *ma_page;
	/** link in the prototype_dev_data::dd_mem_area */
	struct c2_list_link ma_link;
};

/**
   Private data for each nlx file
   @todo Replace uses of this object with nlx_kcore_domain
 */
struct prototype_dev_data {
	uint64_t dd_magic;
	struct c2_mutex dd_mutex;
	/** proof-of-concept value to exchange via ioctl */
	unsigned int dd_value;
	/** proof-of-concept offset into shared page */
	unsigned int dd_tm_offset;
	/** proof-of-concept shared data structure */
	struct page *dd_tm_page;
	/** list of struct mock_mem_area */
	struct c2_list dd_mem_areas;
};

/**
   Release (unpin, unlink and free) a memory area.
   @todo Remove this function, it is only part of the prototype
   @param ma the memory area to release
 */
static void mock_mem_area_put(struct mock_mem_area *ma)
{
	printk("%s: unmapping area %lx\n", __func__, ma->ma_user_addr);
	put_page(ma->ma_page);
	c2_list_del(&ma->ma_link);
	c2_free(ma);
}

/**
   Record a memory area in the list of mapped user memory areas.  On success, a
   new struct mock_mem_area object is added to the list of memory areas tracked
   in the struct prototype_dev_data.  The prototype_dev_data::dd_tm_page and
   prototype_dev_data::dd_tm_offset are also set.
   @todo Remove this function, it is only part of the prototype
   @param dd device data structure tracking all resources for this instance
   @param uma memory descriptor, copied in from user space
 */
static int mock_mem_area_map(struct prototype_dev_data *dd,
			     struct prototype_mem_area *uma)
{
	int rc;
	struct mock_mem_area *ma;
	unsigned int offset;

	C2_PRE(c2_mutex_is_locked(&dd->dd_mutex));
	C2_PRE(current->mm != NULL);

	offset = PAGE_OFFSET(uma->nm_user_addr);
	if (offset + uma->nm_size > PAGE_SIZE) {
		printk("%s: user object did not fit in 1 page\n", __func__);
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -EINVAL);
		return -EINVAL;
	}
	C2_ALLOC_PTR_ADDB(ma, &c2_net_addb, &nlx_addb_loc);
	if (ma == NULL)
		return -ENOMEM;

	c2_mutex_unlock(&dd->dd_mutex);
	/* note: down_read and get_use_pages can block */
	down_read(&current->mm->mmap_sem);
	rc = get_user_pages(current, current->mm,
			    uma->nm_user_addr, 1, 1, 0, &ma->ma_page, NULL);
	up_read(&current->mm->mmap_sem);
	c2_mutex_lock(&dd->dd_mutex);

	if (rc != 1) {
		C2_ASSERT(rc < 0);
		c2_free(ma);
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -ENOSPC);
		return rc;
	}

	printk("%s: mapped area %lx size %d\n",
	       __func__, uma->nm_user_addr, uma->nm_size);
	ma->ma_user_addr = uma->nm_user_addr;
	dd->dd_tm_page = ma->ma_page;
	dd->dd_tm_offset = offset;

	c2_list_add(&dd->dd_mem_areas, &ma->ma_link);
	return 0;
}

/**
   Erase a previously recorded memory area from the list
   in the struct prototype_dev_data.  Always clears prototype_dev_data::dd_tm
   on success.
   @todo Remove this function, it is only part of the prototype
   @param dd device data structure tracking all resources for this instance
   @param uma memory descriptor, copied in from user space
 */
static int mock_mem_area_unmap(struct prototype_dev_data *dd,
			      struct prototype_mem_area *uma)
{
	struct mock_mem_area *pos;

	C2_PRE(c2_mutex_is_locked(&dd->dd_mutex));
	C2_PRE(current->mm != NULL);

	c2_list_for_each_entry(&dd->dd_mem_areas,
			       pos, struct mock_mem_area, ma_link) {
		if (pos->ma_user_addr == uma->nm_user_addr) {
			mock_mem_area_put(pos);
			dd->dd_tm_page = NULL;
			dd->dd_tm_offset = 0;
			return 0;
		}
	}
	return -EINVAL;
}

/**
   Completes the kernel initialization of the kernel and shared core domain
   objects.  The user domain object is mapped into kernel space and its
   nlx_core_domain::cd_kpvt field is set.
   @param kd The kernel domain object
   @param udp User space pointer to a nlx_core_domain object
 */
static int nlx_dev_ioctl_dom_init(struct nlx_kcore_domain *kd, void __user *udp)
{
	return -ENOSYS;
}

/**
   Completes the kernel finalization of the kernel and shared core domain
   objects.  This function detects premature finalization returns an error
   for this case, avoiding assertions in the kernel core API.
   @param kd The kernel domain object
   @retval -ENOTEMPTY Domain is not ready to be finalized, some resources
   have not been finalized properly by the caller.
 */
static int nlx_dev_ioctl_dom_fini(struct nlx_kcore_domain *kd)
{
	return -ENOSYS;
}

/**
   Gets the maximum buffer size.
   @post ret < INT_MAX
   @param kd The kernel domain object
   @return the maximum buffer size, a positive number, on success
 */
static int nlx_dev_ioctl_max_buffer_size(struct nlx_kcore_domain *kd)
{
	return -ENOSYS;
}

/**
   Gets the maximum buffer segment size.
   @post ret < INT_MAX
   @param kd The kernel domain object
   @return the maximum buffer segment size, a positive number, on success
 */
static int nlx_dev_ioctl_max_buffer_segment_size(struct nlx_kcore_domain *kd)
{
	return -ENOSYS;
}

/**
   Gets the maximum buffer segment size.
   @post ret < INT_MAX
   @param kd The kernel domain object
   @return the maximum number of buffer segments, a positive number, on success
 */
static int nlx_dev_ioctl_max_buffer_segments(struct nlx_kcore_domain *kd)
{
	return -ENOSYS;
}

/**
   Registers a shared memory buffer with the kernel domain.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_register(struct nlx_kcore_domain *kd,
				      struct c2_lnet_dev_buf_register_params *p)
{
	return -ENOSYS;
}

/**
   Deregisters a shared memory buffer from the kernel domain.
   @param kd The kernel domain object
   @param kb The kernel buffer object
 */
static int nlx_dev_ioctl_buf_deregister(struct nlx_kcore_domain *kd,
					struct nlx_kcore_buffer *kb)
{
	return -ENOSYS;
}

/**
   Enqueues a buffer for message reception.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_msg_recv(struct nlx_kcore_domain *kd,
				      struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Enqueues a buffer for message transmission.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_msg_send(struct nlx_kcore_domain *kd,
				      struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Enqueues a buffer for active bulk receive.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_active_recv(struct nlx_kcore_domain *kd,
					 struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Enqueues a buffer for active bulk send.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_active_send(struct nlx_kcore_domain *kd,
					 struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Enqueues a buffer for passive bulk receive.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_passive_recv(struct nlx_kcore_domain *kd,
					 struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Enqueues a buffer for passive bulk send.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_passive_send(struct nlx_kcore_domain *kd,
					 struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Cancels a buffer operation if possible.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_del(struct nlx_kcore_domain *kd,
				 struct c2_lnet_dev_buf_queue_params *p)
{
	return -ENOSYS;
}

/**
   Waits for buffer events, or the timeout.
   @param kd The kernel domain object
   @param p Ioctl request parameters
 */
static int nlx_dev_ioctl_buf_event_wait(struct nlx_kcore_domain *kd,
				    struct c2_lnet_dev_buf_event_wait_params *p)
{
	return -ENOSYS;
}

/**
   Decodes a NID string into a NID.
   @param kd The kernel domain object
   @param p Ioctl request parameters. The c2_lnet_dev_nid_encdec_params::dn_nid
   field is set on success.
 */
static int nlx_dev_ioctl_nidstr_decode(struct nlx_kcore_domain *kd,
				       struct c2_lnet_dev_nid_encdec_params *p)
{
	return -ENOSYS;
}

/**
   Encodes a NID into a NID string.
   @param kd The kernel domain object
   @param p Ioctl request parameters. The c2_lnet_dev_nid_encdec_params::dn_buf
   field is set on success.
 */
static int nlx_dev_ioctl_nidstr_encode(struct nlx_kcore_domain *kd,
				       struct c2_lnet_dev_nid_encdec_params *p)
{
	return -ENOSYS;
}

/**
   Gets the NID strings of all the local LNet interfaces.
   The NID strings are encoded consecutively in user space buffer denoted by
   the c2_lnet_dev_nidstrs_get_params::dng_buf field as a sequence nul
   terminated strings, with an final nul (string) terminating the list.
   @param kd The kernel domain object
   @param p Ioctl request parameters.
   @retval -EFBIG if the strings do not fit in the provided buffer.
 */
static int nlx_dev_ioctl_nidstrs_get(struct nlx_kcore_domain *kd,
				     struct c2_lnet_dev_nidstrs_get_params *p)
{
	return -ENOSYS;
}

/**
   Completes the kernel portion of the TM start logic.
   The shared transfer machine object is pinned in kernel space and its
   nlx_core_transfer_mc::ctm_kpvt field is set.
   @param kd The kernel domain object
   @param udp User space pointer to a nlx_core_transfer_mc object
 */
static int nlx_dev_ioctl_tm_start(struct nlx_kcore_domain *kd,
				  void __user *utmp)
{
	return -ENOSYS;
}

/**
   Complete the kernel portion of the TM stop logic.
   @param kd The kernel domain object
   @param ktm The kernel transfer machine object
   @retval -ENOTEMPTY The user space transport has not correctly cleaned up all
   of the required resources before stopping the transfer machine.
 */
static int nlx_dev_ioctl_tm_stop(struct nlx_kcore_domain *kd,
				 struct nlx_kcore_transfer_mc *ktm)
{
	return -ENOSYS;
}

/**
   Blesses a shared nlx_core_buffer_event object.
   The shared buffer event object is pinned in kernel space and its
   nlx_core_buffer_event::cbe_kpvt field is set.  The bev_link_bless()
   function is used to bless the nlx_core_buffer_event::cbe_tm_link.
   @param kd The kernel domain object
   @param udp User space pointer to a nlx_core_buffer_event object
 */
static int nlx_dev_ioctl_bev_bless(struct nlx_kcore_domain *kd,
				   struct c2_lnet_dev_bev_bless_params *p)
{
	return -ENOSYS;
}

/**
   Performs an unlocked (BKL is not held) ioctl request on the c2lnet device.

   @pre nlx_kcore_domain_invariant(file->private_data)
   @param file File instance, corresponding to the nlx_kcore_domain.
   @param cmd The request or operation to perform.
   @param arg Argument to the operation, internally treated as a pointer
   whose type depends on the cmd.
 */
static long nlx_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct prototype_dev_data *dd =
	    (struct prototype_dev_data *) file->private_data;
	struct prototype_mem_area uma;
        int rc = -ENOTTY;

	/* This will test nlx_kcore_domain_invariant in the real code */
	C2_PRE(dd != NULL && dd->dd_magic == DD_MAGIC);

        if (_IOC_TYPE(cmd) != C2_LNET_IOC_MAGIC ||
            _IOC_NR(cmd) < C2_LNET_IOC_MIN_NR  ||
            _IOC_NR(cmd) > C2_LNET_IOC_MAX_NR) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
		return rc;
	}

	if (!(file->f_flags & O_RDWR)) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -EBADF);
		return -EBADF;
	}

	/** @todo check capable(CAP_SYS_ADMIN)? */

	rc = 0;
	c2_mutex_lock(&dd->dd_mutex);
	switch (cmd) {
	case PROTOREAD:
		if (put_user(dd->dd_value, (unsigned int __user *) arg))
			rc = -EFAULT;
		break;
	case PROTOWRITE:
		if (get_user(dd->dd_value, (unsigned int __user *) arg))
			rc = -EFAULT;
		/* real code will call a function at this point */
		if (dd->dd_tm_page != NULL) {
			struct nlx_core_transfer_mc *tm;
			bool mod = false;
			char *ptr = kmap_atomic(dd->dd_tm_page, KM_USER0);

			tm = (struct nlx_core_transfer_mc *)
			    (ptr + dd->dd_tm_offset);
			if (tm->ctm_magic == C2_NET_LNET_CORE_TM_MAGIC) {
				tm->_debug_ = dd->dd_value;
				SetPageDirty(dd->dd_tm_page);
				mod = true;
			}
			kunmap_atomic(dd->dd_tm_page, KM_USER0);
			printk("%s: %smodified nlx_core_transfer_mc\n",
			       __func__, mod ? "" : "UN");
		}
		break;
	case PROTOMAP:
		if (copy_from_user(&uma, (void __user *) arg, sizeof uma))
			rc = -EFAULT;
		else
			rc = mock_mem_area_map(dd, &uma);
		break;
	case PROTOUNMAP:
		if (copy_from_user(&uma, (void __user *) arg, sizeof uma))
			rc = -EFAULT;
		else
			rc = mock_mem_area_unmap(dd, &uma);
		break;
	default:
		/** @todo temporary code so this file will compile */
		nlx_dev_ioctl_dom_init(NULL, NULL);
		nlx_dev_ioctl_dom_fini(NULL);
		nlx_dev_ioctl_max_buffer_size(NULL);
		nlx_dev_ioctl_max_buffer_segment_size(NULL);
		nlx_dev_ioctl_max_buffer_segments(NULL);
		nlx_dev_ioctl_buf_register(NULL, NULL);
		nlx_dev_ioctl_buf_deregister(NULL, NULL);
		nlx_dev_ioctl_buf_msg_recv(NULL, NULL);
		nlx_dev_ioctl_buf_msg_send(NULL, NULL);
		nlx_dev_ioctl_buf_active_recv(NULL, NULL);
		nlx_dev_ioctl_buf_active_send(NULL, NULL);
		nlx_dev_ioctl_buf_passive_recv(NULL, NULL);
		nlx_dev_ioctl_buf_passive_send(NULL, NULL);
		nlx_dev_ioctl_buf_del(NULL, NULL);
		nlx_dev_ioctl_buf_event_wait(NULL, NULL);
		nlx_dev_ioctl_nidstr_decode(NULL, NULL);
		nlx_dev_ioctl_nidstr_encode(NULL, NULL);
		nlx_dev_ioctl_nidstrs_get(NULL, NULL);
		nlx_dev_ioctl_tm_start(NULL, NULL);
		nlx_dev_ioctl_tm_stop(NULL, NULL);
		nlx_dev_ioctl_bev_bless(NULL, NULL);
		/* end of temporary code */
		rc = -ENOTTY;
		break;
	}
	c2_mutex_unlock(&dd->dd_mutex);
	return rc;
}

/**
   Open the /dev/c2lnet device.

   There is a 1:1 correspondence between struct file objects and
   nlx_kcore_domain objects.  Thus, user processes will open the c2lnet
   device once for each c2_net_domain.
   @param inode Inode object for the device.
   @param file File object for this instance.
 */
static int nlx_dev_open(struct inode *inode, struct file *file)
{
	int cnt = try_module_get(THIS_MODULE);
	struct prototype_dev_data *dd;

	if (cnt == 0) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -ENODEV);
		return -ENODEV;
	}

	C2_ALLOC_PTR_ADDB(dd, &c2_net_addb, &nlx_addb_loc);
	if (dd == NULL)
		return -ENOMEM;
	dd->dd_magic = DD_MAGIC;
	dd->dd_value = DD_INITIAL_VALUE;
	c2_mutex_init(&dd->dd_mutex);
	c2_list_init(&dd->dd_mem_areas);
	/* real implementation will assign a nlx_kcore_domain object here */
	file->private_data = dd;
	printk("Colibri c2lnet: opened\n");
        return 0;
}

/**
   Releases all resources for the given struct file.

   This operation is called once when the file is being released.  There is a
   1:1 correspondence between struct file objects and nlx_kcore_domain objects,
   so this operation will release all kernel resources for the domain.  That can
   be expensive if the user process failed to successfully call
   nlx_core_dom_fini() before closing the file.  This operation will not
   assert in that case, but will clean up and log the error via ADDB.

   @param inode Device inode object
   @param file File object being released
 */
int nlx_dev_close(struct inode *inode, struct file *file)
{
	struct prototype_dev_data *dd = file->private_data;
	struct mock_mem_area *pos;
	struct mock_mem_area *next;

	C2_PRE(dd != NULL && dd->dd_magic == DD_MAGIC);

	file->private_data = NULL;
	/* user program may not unmap all areas, eg if it was killed */
	c2_list_for_each_entry_safe(&dd->dd_mem_areas,
				    pos, next, struct mock_mem_area, ma_link)
		mock_mem_area_put(pos);
	c2_list_fini(&dd->dd_mem_areas);
	c2_mutex_fini(&dd->dd_mutex);
	c2_free(dd);

	module_put(THIS_MODULE);
	printk("Colibri c2lnet: closed\n");
	return 0;
}

/** File operations for the c2lnet device. */
static const struct file_operations nlx_dev_file_ops = {
        .unlocked_ioctl = nlx_dev_ioctl,
        .open           = nlx_dev_open,
        .release        = nlx_dev_close
};

/**
   Device description.
   The device is named /dev/c2lnet when nlx_dev_init() completes.
   The major number is 10 (misc), and the minor number is assigned dynamically
   when misc_register() is called.
 */
static struct miscdevice nlx_dev = {
        .minor   = MISC_DYNAMIC_MINOR,
        .name    = C2_LNET_DEV,
        .fops    = &nlx_dev_file_ops
};
static bool nlx_dev_registered = false;

/** @} */ /* LNetDevInternal */

/**
   @addtogroup LNetDev
   @{
 */

int nlx_dev_init(void)
{
	int rc;

	rc = misc_register(&nlx_dev);
	if (rc != 0) {
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
		return rc;
	}
	nlx_dev_registered = true;
	printk("Colibri %s registered with minor %d\n",
	       nlx_dev.name, nlx_dev.minor);
	return rc;
}

void nlx_dev_fini(void)
{
	int rc;

	if (nlx_dev_registered) {
		rc = misc_deregister(&nlx_dev);
		if (rc != 0)
			LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, rc);
		nlx_dev_registered = false;
		printk("Colibri %s deregistered\n", nlx_dev.name);
	}
}

/** @} */ /* LNetDev */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
