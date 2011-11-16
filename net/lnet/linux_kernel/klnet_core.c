/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/01/2011
 */

/**
   @page KLNetCoreDLD LNet Transport Kernel Core DLD

   - @ref KLNetCoreDLD-ovw
   - @ref KLNetCoreDLD-def
   - @ref KLNetCoreDLD-req
   - @ref KLNetCoreDLD-depends
   - @ref KLNetCoreDLD-highlights
   - @subpage LNetCoreDLD-fspec "Functional Specification" <!-- ext link -->
        - @ref LNetCore "LNet Transport Core Interface"
        - @ref KLNetCore "Core Kernel Interface"      <!-- ext link -->
        - @ref ULNetCore "Core User Space Interface"  <!-- ext link -->
   - @ref KLNetCoreDLD-lspec
      - @ref KLNetCoreDLD-lspec-comps
      - @ref KLNetCoreDLD-lspec-userspace
      - @ref KLNetCoreDLD-lspec-match-bits
      - @ref KLNetCoreDLD-lspec-tm-list
      - @ref KLNetCoreDLD-lspec-bevq
      - @ref KLNetCoreDLD-lspec-lnet-init
      - @ref KLNetCoreDLD-lspec-reg
      - @ref KLNetCoreDLD-lspec-ev
      - @ref KLNetCoreDLD-lspec-recv
      - @ref KLNetCoreDLD-lspec-send
      - @ref KLNetCoreDLD-lspec-passive
      - @ref KLNetCoreDLD-lspec-active
      - @ref KLNetCoreDLD-lspec-lnet-cancel
      - @ref KLNetCoreDLD-lspec-state
      - @ref KLNetCoreDLD-lspec-thread
      - @ref KLNetCoreDLD-lspec-numa
   - @ref KLNetCoreDLD-conformance
   - @ref KLNetCoreDLD-ut
   - @ref KLNetCoreDLD-st
   - @ref KLNetCoreDLD-O
   - @ref KLNetCoreDLD-ref

   <hr>
   @section KLNetCoreDLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   The LNet Transport is built over an address space agnostic "core" I/O
   interface.  This document describes the kernel implementation of this
   interface, which directly interacts with the LNet kernel module.

   <hr>
   @section KLNetCoreDLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:

   New terms:

   <hr>
   @section KLNetCoreDLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - <b>r.c2.net.lnet.buffer-registration</b> Provide support for
     hardware optimization through buffer pre-registration.

   - <b>r.c2.net.xprt.lnet.end-point-address</b> The implementation
     should support the mapping of end point address to LNet address
     as described in the Refinement section of the HLD.

   - <b>r.c2.net.xprt.lnet.multiple-messages-in-buffer</b> Provide
     support for this feature as described in the HLD.

   - <b>r.c2.net.xprt.lnet.dynamic-address-assignment</b> Provide
     support for dynamic address assignment as described in the HLD.

   - <b>r.c2.net.xprt.lnet.user-space</b> The implementation must
     accommodate the needs of the user space LNet transport.

   - <b>r.c2.net.xprt.lnet.user.no-gpl</b> The implementation must not expose
     the user space transport to GPL interfaces.


   <hr>
   @section KLNetCoreDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - <b>LNet API</b> headers are required to build the module.
   The Xyratex Lustre source package must be installed on the build
   machine (RPM @c lustre-source version 2.0 or greater).
   - <b>Xyratex Lustre runtime</b>

   <hr>
   @section KLNetCoreDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - The core API is an address space agnostic I/O interface intended for use
     by the Colibri Networking LNet transport operation layer in either user
     space or kernel space.

   - Efficient support for the user space transports is provided by use of
     cross-address space tolerant data structures in shared memory.

   - The core API does not expose any LNet symbols.

   - Each transfer machine is internally assigned one LNet event queue for all
     its LNet buffer operations.

   - Pre-allocation of buffer event space to guarantee that buffer operation
     results can be returned.

   - The notification of the completion of a buffer operation to the transport
     layer is decoupled from the LNet callback that provided this notification
     to the core module.

   - The number of messages that can be delivered into a single receive buffer
     is bounded to support pre-allocation of memory to hold the buffer event
     payload.

   - Buffer completion event notification is provided via a semaphore.  The
     design guarantees delivery of events in the order received from LNet.  In
     particular, the multiple possible events delivered for a single receive
     buffer will be ordered.

   <hr>
   @section KLNetCoreDLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref KLNetCoreDLD-lspec-comps
   - @ref KLNetCoreDLD-lspec-userspace
   - @ref KLNetCoreDLD-lspec-match-bits
   - @ref KLNetCoreDLD-lspec-tm-list
   - @ref KLNetCoreDLD-lspec-bevq
   - @ref KLNetCoreDLD-lspec-lnet-init
   - @ref KLNetCoreDLD-lspec-reg
   - @ref KLNetCoreDLD-lspec-ev
   - @ref KLNetCoreDLD-lspec-recv
   - @ref KLNetCoreDLD-lspec-send
   - @ref KLNetCoreDLD-lspec-passive
   - @ref KLNetCoreDLD-lspec-active
   - @ref KLNetCoreDLD-lspec-lnet-cancel
   - @ref KLNetCoreDLD-lspec-state
   - @ref KLNetCoreDLD-lspec-thread
   - @ref KLNetCoreDLD-lspec-numa


   @subsection KLNetCoreDLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The relationship between the various objects in the components of the LNet
   transport and the networking layer is illustrated in the following UML
   diagram.  @image html "../../net/lnet/lnet_xo.png" "LNet Transport Objects"

   The Core layer in the kernel has no sub-components but interfaces directly
   with the Lustre LNet module in the kernel.


   @subsection KLNetCoreDLD-lspec-userspace Support for User Space Transports

   The kernel Core module is designed to support user space transports with the
   use of shared memory.  It does not directly provide a mechanism to
   communicate with the user space transport, but expects that the user space
   Core module will provide a device driver to communicate between user and
   kernel space, manage the sharing of core data structures, and interface
   between the kernel and user space implementations of the Core API.

   The common Core data structures are designed to support such communication
   efficiently:

   - The core data structures are organized with a distinction between the
     common directly shareable portions, and private areas for kernel and user
     space data.  This allows each address space to place pointer values of its
     address space in private regions associated with the shared data
     structures.

   - An address space opaque pointer type is provided to safely save pointer
     values in shared memory locations where necessary.

   - The single producer, single consumer circular buffer event queue shared
     between the transport and the core layer in the kernel is designed to work
     with the producer and consumer potentially in different address spaces.
     This is described in further detail in @ref KLNetCoreDLD-lspec-bevq.


   @subsection KLNetCoreDLD-lspec-match-bits Match Bits for Buffer Identification

   The kernel Core module will maintain a unsigned integer counter per transfer
   machine, to generate unique match bits for passive bulk buffers associated
   with that transfer machine.  The upper 12 match bits are reserved by the HLD
   to represent the transfer machine identifier. Therefore the counter is
   (64-12)=52 bits wide. The value of 0 is reserved for unsolicited
   receive messages, so the counter range is [1,0xfffffffffffff]. It is
   initialized to 1 and will wrap back to 1 when it reaches its upper bound.

   The transport uses the nlx_core_buf_passive_recv() or the
   nlx_core_buf_passive_send() subroutines to stage passive buffers.  Prior
   to initiating these operations, the transport should use the
   nlx_core_buf_match_bits_set() subroutine to generate new match bits for
   the passive buffer.  The match bit counter will repeat over time, though
   after a very long while.  It is the transport's responsibility to ensure
   that all of the passive buffers associated with a given transfer machine
   have unique match bits.  The match bits should be encoded into the network
   buffer descriptor associated with the passive buffer.


   @subsection KLNetCoreDLD-lspec-tm-list Transfer Machine Uniqueness

   The kernel Core module must ensure that all transfer machines have unique
   addresses, regardless of the transport instance or network domain in which
   they originate.  To support this, the ::nlx_kcore_tms list threads through
   all the kernel Core's per-TM private data structures. This list is private
   to the kernel Core, and is protected by the ::nlx_kcore_mutex.

   The same list helps in assigning dynamic transfer machine identifiers.  The
   highest available value at the upper bound of the transfer machine
   identifier space is assigned dynamically.  The logic takes into account the
   NID, PID and portal number of the new transfer machine when looking for an
   available transfer machine identifier.  A single pass over the list is
   required to search for an available transfer machine identifier.


   @subsection KLNetCoreDLD-lspec-bevq The Buffer Event Queue

   The kernel Core receives notification of the completion of a buffer
   operation through an LNet callback.  The completion status is not directly
   conveyed to the transport, because the transport layer may have processor
   affinity constraints that are not met by the LNet callback thread; indeed,
   LNet does not even state if this callback is in a schedulable context.

   Instead, the kernel Core module de-couples the delivery of buffer operation
   completion to the transport from the LNet callback context by copying the
   result to an intermediate buffer event queue.  The Core API provides the
   nlx_core_buf_event_wait() subroutine that the transport can use to poll for
   the presence of buffer events, and the nlx_core_buf_event_get() to recover
   the payload of the next available buffer event. See @ref
   KLNetCoreDLD-lspec-ev for further details on these subroutines.

   There is another advantage to this indirect delivery: to address the
   requirement to efficiently support a user space transport, the Core module
   keeps this queue in memory shared between the transport and the Core,
   eliminating the need for a user space transport to make an @c ioctl call to
   fetch the buffer event payload.  The only @c ioctl call needed for a user
   space transport is to block waiting for buffer events to appear in the
   shared memory queue.

   It is critical for proper operation, that there be an available buffer event
   structure when the LNet callback is invoked, or else the event cannot be
   delivered and will be lost.  As the event queue is in shared memory, is not
   possible, let alone desirable, to allocate a new buffer event structure in
   the callback context.

   The Core API guarantees the delivery of buffer operation completion status
   by maintaining a "pool" of free buffer event structures for this purpose.
   It does so by keeping count of the total number of buffer event structures
   required to satisfy all outstanding operations, and adding additional such
   structures to the "pool" if necessary when a new buffer operation is
   initiated.  Likewise, the count is decremented for each buffer event
   delivered to the transport.  Most buffers operations only need a single
   buffer event structure in which to return their operation result, but
   receive buffers, however, may need more, depending on the individually
   configurable maximum number of messages that could be received in each
   buffer.

   The pool and queue potentially span the kernel and user address spaces.
   There are two cases around the use of these data structures:

   - Normal queue operation involves a single @a producer, in the kernel Core
     callback subroutine, and a single @a consumer, in the Core API
     nlx_core_buf_event_get() subroutine, which may be invoked either in
     the kernel or in user space.

   - The allocation of new buffer event structures to the "pool" is always done
     by the Core API buffer operation initiation subroutines invoked by the
     transport.  The user space implementation of the Core API would have to
     arrange for these new structures to get mapped into the kernel at this
     time.

   The kernel Core module combines both the free "pool" and the result queue
   into a single data structure: a circular, single producer, single consumer
   buffer event queue.  Details on this event queue are covered in the
   @ref LNetcqueueDLD "LNet Buffer Event Circular Queue DLD."

   The design makes a critical simplifying assumption, in that the transport
   will use exactly one thread to process events.  This assumption implicitly
   serializes the delivery of the events associated with any given receive
   buffer, thus the last event which unlinks the buffer is guaranteed to be
   delivered after other events associated with that same buffer operation.


   @subsection KLNetCoreDLD-lspec-lnet-init LNet Initialization and Finalization

   No initialization and finalization logic is required for LNet in the kernel
   for the following reasons:

   - Use of the LNet kernel module is reference counted by the kernel.
   - The LNetInit() subroutine is automatically called when then LNet kernel
     module is loaded, and cannot be called multiple times.


   @subsection KLNetCoreDLD-lspec-reg LNet Buffer Registration

   No hardware optimization support is defined in the LNet API at this time.

   During buffer registration, the core API will translate the c2_net_bufvec
   into the nlx_kcore_buffer::kb_kiov field of the buffer private data.


   @subsection KLNetCoreDLD-lspec-ev LNet Event Callback Processing

   LNet event queues are used with an event callback subroutine to avoid event
   loss.  The callback subroutine overhead is fairly minimal, as it only copies
   out the event payload and arranges for subsequent asynchronous delivery.
   This, coupled with the fact that the circular buffer used works optimally
   with a single producer and single consumer resulted in the decision to use
   just one LNet EQ per transfer machine (nlx_kcore_transfer_mc::ktm_eqh).

   LNet requires that the callback subroutine be re-entrant and
   non-blocking. Given that the circular queue assumes a single producer and
   single consumer, a spin lock is used to serialize access to the queue.

   The event callback requires that the MD @c user_ptr field be set up to point
   to the nlx_core_buffer data structure.  The callback does the following:

   -# @c LNET_EVENT_SEND and @c LNET_EVENT_ACK events are ignored.
   -# Obtain the nlx_kcore_transfer_mc::ktm_bevq_lock spinlock.
   -# The bev_cqueue_pnext() subroutine is used to locate the next buffer event
      structure in the circular buffer event queue which will be used to return
      the result.
   -# Copy the event payload from the LNet event to the buffer event structure.
      This includes the value of the @c unlinked field of the event, which must
      be copied to the nlx_core_buffer_event::cbe_unlinked field.  For @c
      LNET_EVENT_UNLINK events, a @c -ECANCELLED value is written to the
      nlx_core_buffer_event::cbe_status field and the
      nlx_core_buffer_event::cbe_unlinked field set to true.
   -# The bev_cqueue_put() subroutine is invoked.
   -# Release the nlx_kcore_transfer_mc::ktm_bevq_lock spinlock.
   -# The nlx_kcore_transfer_mc::ktm_sem is signalled with the
      c2_semaphore_up() subroutine.

   The (single) transport layer event handler thread blocks on the Core
   transfer machine semaphore in the nlx_core_buf_event_wait() subroutine using
   the c2_semaphore_timeddown() subroutine.  When the subroutine returns with
   an indication of the presence of events, the event handler thread consumes
   all the pending events with multiple calls to the nlx_core_buf_event_get()
   subroutine, which invokes the bev_cqueue_get() subroutine to get the next
   buffer event.  Then the event handler thread repeats the call to the
   nlx_core_buf_event_wait() subroutine to once again block for additional
   events.

   In the case of the user space transport, the blocking on the semaphore is
   done indirectly by the device driver in the kernel.  It is required by the
   HLD that as many events as possible be consumed before the context switch to
   the kernel is made.  To support this, the nlx_core_buf_event_wait()
   subroutine takes a few additional steps to minimize the chance of returning
   when the queue is empty.  After it obtains the semaphore with the
   c2_semaphore_timeddown() subroutine (i.e. the @em P operation succeeds), it
   attempts to clear the semaphore count by repeatedly calling the
   c2_semaphore_trydown() subroutine until it fails.  It then checks the
   circular queue, and only if not empty will it return.  This is illustrated
   with the following pseudo-code:
   @code
       do {
          rc = c2_net_semaphore_timed_down(&sem, &timeout);
 	  if (rc < 0)
 	      break; // timed out
          while (c2_semaphore_trydown(&sem))
              ; // exhaust the semaphore
          if (!bev_cqueue_is_empty(&q))
              break; // loop if empty
       } while (1);
   @endcode
   (The C++ style comments are used because of doxygen only - they are not
   permitted by the Colibri style guide.)

   @subsection KLNetCoreDLD-lspec-recv LNet Receiving Unsolicited Messages

   -# For each receive buffer, create an ME with @c LNetMEAlloc() and specify
      the portal, match and ignore bits. All receive buffers for a given TM will
      use a match bit value equal to the TM identifier in the higher order
      bits and zeros for the other bits.  No ignore bits are set.
      Save the ME handle in the nlx_kcore_buffer::kb_meh field.
   -# Create and attach an MD to the ME using @c LNetMDAttach().
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the kernel logical address of the nlx_kcore_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
      - Set the @c threshold value to the maximum number of messages that
        are to be received in the buffer.
      - Set the @c max_size value to the
        nlx_kcore_buffer::kb_min_receive_size value.
      - Set the @c LNET_MD_OP_PUT, @c LNET_MD_MAX_SIZE and @c LNET_MD_KIOV
        flags in the @c options field.
   -# When a message arrives, an @c LNET_EVENT_PUT event will be delivered to
      the event queue, and will be processed as described in
      @ref KLNetCoreDLD-lspec-ev.


   @subsection KLNetCoreDLD-lspec-send LNet Sending Messages

   -# Create an MD using @c LNetMDBind().
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the kernel logical address of the nlx_kcore_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
      - Set the @c LNET_MD_KIOV flag in the @c options field.
   -# Use the @c LNetPut() subroutine to send the MD to the destination.
      The match bits must set to the destination TM identifier in the higher
      order bits and zeros for the other bits.
   -# When the message is sent, an @c LNET_EVENT_SEND event will be delivered
      to the event queue, and processed as described in
      @ref KLNetCoreDLD-lspec-ev.


   @subsection KLNetCoreDLD-lspec-passive LNet Staging Passive Bulk Buffers

   -# Prior to invoking the nlx_core_buf_passive_recv() or the
      nlx_core_buf_passive_send() subroutines, the transport should use the
      nlx_core_buf_match_bits_set() subroutine to assign unique match bits to
      the passive buffer. See @ref KLNetCoreDLD-lspec-match-bits for details.
      The match bits should be encoded into the network buffer descriptor and
      independently conveyed to the remote active transport.
   -# Create an ME using @c LNetMEAlloc(). Specify the portal and match_id
      fields as appropriate for the transfer machine.  The buffer's match bits
      are obtained from the nlx_core_buffer::cb_match_bits field.  No ignore
      bits are set. The ME should be set up to unlink automatically.
   -# Create and attach an MD to the ME using @c LNetMDAttach().
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the kernel logical address of the nlx_kcore_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
      - Set the @c LNET_MD_KIOV flag in the @c options field, along with either
        the @c LNET_MD_OP_PUT or the @c LNET_MD_OP_GET flag according to the
	direction of data transfer.
   -# When the bulk data transfer completes, either an @c LNET_EVENT_PUT or an
      @c LNET_EVENT_GET event will be delivered to the event queue, and will be
      processed as described in @ref KLNetCoreDLD-lspec-ev.


   @subsection KLNetCoreDLD-lspec-active LNet Active Bulk Read or Write

   -# Prior to invoking the nlx_core_buf_active_recv() or
   nlx_core_buf_active_send() subroutines, the
   transport should put the match bits of the remote passive buffer into the
   nlx_core_buffer::cb_match_bits field. The destination address of the
   remote transfer machine with the passive buffer should be set in the
   nlx_core_buffer::cb_passive_addr field.
   -# Create an MD using @c LNetMDBind().
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the kernel logical address of the nlx_kcore_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
      - Set the @c LNET_MD_KIOV flag in the @c options field.
   -# Use the @c LNetGet() subroutine to initate the active read or the
      @c LNetPut() subroutine to initiate the active write.
   -# When a response to the @c LNetGet() or @c LNetPut() call completes, an @c
      LNET_EVENT_SEND event will be delivered to the event queue and should be
      ignored.  See @ref KLNetCoreDLD-lspec-ev for details.
   -# When the bulk data transfer completes, an @c LNET_EVENT_REPLY event will
      be delivered to the event queue, and will be processed as described in
      @ref KLNetCoreDLD-lspec-ev.


   @subsection KLNetCoreDLD-lspec-lnet-cancel LNet Cancelling Operations

   The kernel Core module provides no timeout capability.  The transport may
   initiate a cancel operation using the nlx_core_buf_del() subroutine.

   This will result in an @c LNetMDUnlink() subroutine call being issued for
   the buffer MD saved in the nlx_kcore_buffer::kb_mdh field.
   Cancellation may or may not take place - it depends upon whether the
   operation has started, and there is a race condition in making this call and
   concurrent delivery of an event associated with the MD.

   Assuming success, the next event delivered for the buffer concerned will
   either be a @c LNET_EVENT_UNLINK event or the @c unlinked field will be set
   in the next completion event for the buffer.  The events will be processed
   as described in @ref KLNetCoreDLD-lspec-ev.


   @subsection KLNetCoreDLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   - The kernel Core module relies on the networking data structures to maintain
   the linkage between the data structures used by the Core module. It
   maintains no lists through data structures itself.  As such, these lists can
   only be navigated by the Core API subroutines invoked by the transport (the
   "upper" layer) and not by the Core module's LNet callback subroutine (the
   "lower" layer).

   - The kernel Core API maintains a count of the total number of buffer event
   structures needed.  This should be tested by the Core API's transfer machine
   invariant subroutine before returning from any buffer operation initiation
   call, and before returning from the nlx_core_buf_event_get() subroutine.

   - The kernel Core layer module depends on the LNet module in the kernel at
   runtime. This dependency is captured by the Linux kernel module support that
   reference counts the usage of dependent modules.


   @subsection KLNetCoreDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   <ol>

   <li>Generally speaking, API calls within the transport address space
   are protected by the serialization of the Colibri Networking layer,
   typically the transfer machine mutex or the domain mutex.
   The nlx_core_buf_match_bits_set() subroutine, for example, is fully
   protected by the transfer machine mutex held across the c2_net_buffer_add()
   subroutine call, so implicitly protects the match bit counter in the kernel
   Core's per TM private data.</li>

   <li>The Colibri Networking layer serialization does not always suffice, as
   the kernel Core module has to support concurrent multiple transport
   instances in kernel and user space.  Fortunately, the LNet API intrinsically
   provides considerable serialization support to the Core, as transfer
   machines are defined by the HLD to have disjoint addresses.</li>

   <li>Enforcement of the disjoint address semantics are protected by the
   kernel Core's ::nlx_kcore_mutex lock.  The nlx_core_tm_start() and
   nlx_core_tm_stop() subroutines use this mutex internally for serialization
   and operation on the ::nlx_kcore_tms list threaded through the kernel Core's
   per-TM private data.</li>

   <li> The kernel Core module registers a single callback subroutine with the
   LNet EQ defined per transfer machine. LNet requires that this subroutine be
   reentrant and non-blocking.  The circular buffer event queue accessed from
   the callback requires a single producer, so the
   nlx_kcore_transfer_mc::ktm_bevq_lock spinlock is used to serialize its
   use across possible concurrent invocations.  The time spent in the lock is
   minimal.</li>

   <li>The Core API does not support callbacks to indicate completion of an
   asynchronous buffer operation.  Instead, the transport application must
   invoke the nlx_core_buf_event_wait() subroutine to block waiting for buffer
   events.  Internally this call waits on the
   nlx_kcore_transfer_mc::ktm_sem semaphore.  The semaphore is
   incremented each time an event is added to the buffer event queue.</li>

   <li>The event payload is actually delivered via a per transfer machine
   single producer, single consumer, lock-free circular buffer event queue.
   The only requirement for failure free operation is to ensure that there are
   sufficient event structures pre-allocated to the queue, plus one more to
   support the circular semantics.  Multiple events may be dequeued between
   each call to the nlx_core_buf_event_wait() subroutine.  Each such event is
   fetched by a call to the nlx_core_buf_event_get() subroutine, until the
   queue is exhausted.  Note that the queue exists in memory shared between the
   transport and the kernel Core; the transport could be in the kernel or in
   user space.</li>

   <li>The API assumes that only a single transport thread will handle event
   processing.  This is a critical assumption in the support for multiple
   messages in a single receive buffer, as it implicitly serializes the
   delivery of the events associated with any given receive buffer, thus the
   last event which unlinks the buffer is guaranteed to be delivered last.</li>

   </ol>


   @subsection KLNetCoreDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   The LNet transport will initiate calls to the API on threads that may have
   specific process affinity assigned.

   LNet offers no direct NUMA optimizations.  In particular, event callbacks
   cannot be constrained to have any specific processor affinity.  The API
   compensates for this lack of support by providing a level of indirection in
   event delivery: its callback handler simply copies the LNet event payload to
   an event delivery queue and notifies a transport event processing thread of
   the presence of the event. (See @ref KLNetCoreDLD-lspec-bevq above). The
   transport event processing threads can be constrained to have any desired
   processor affinity.

   <hr>
   @section KLNetCoreDLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref KLNetCoreDLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section KLNetCoreDLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>


   <hr>
   @section KLNetCoreDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section KLNetCoreDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   - Dynamic transfer machine identifier assignment is proportional to the
   number of transfer machines defined on the server, including kernel and all
   process space LNet transport instances.
   - The time taken to process an LNet event callback is in constant time.
   - The time taken for the transport to dequeue a pending buffer event
   depends upon the operating system scheduler.  The algorithmic
   processing involved is in constant time.
   - The time taken to register a buffer is in constant time.
   - The time taken to process outbound buffer operations is unpredictable,
   and depends, at the minimum, on current system load, other LNet users,
   and on the network load.

   <hr>
   @section KLNetCoreDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>

*/

/*
 ******************************************************************************
 End of DLD
 ******************************************************************************
 */

#include "lib/mutex.h"
#include "net/lnet/linux_kernel/klnet_core.h"

/**
   @addtogroup KLNetCore
   @{
*/

/**
   Kernel core lock.
   Provides serialization across the nlx_kcore_tms list.
 */
static c2_mutex nlx_kcore_mutex;

/** List of all transfer machines. Protected by nlx_kcore_mutex. */
static struct c2_tl nlx_kcore_tms;


/**
   @}
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

