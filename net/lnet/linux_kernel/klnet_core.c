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
 */

/**
   @page KLNetCoreDLD LNet Transport Kernel Core DLD

   - @ref KLNetCoreDLD-ovw
   - @ref KLNetCoreDLD-def
   - @ref KLNetCoreDLD-req
   - @ref KLNetCoreDLD-depends
   - @ref KLNetCoreDLD-highlights
   - @subpage LNetCoreDLD-fspec "Functional Specification" <!--
                                                             ../lnet_core.h -->
        - @ref LNetCore "LNet Transport Core Interface" <!-- ../lnet_core.h -->
        - @ref KLNetCore "Core Kernel Interface"        <!-- ./klnet_core.h -->
   - @ref KLNetCoreDLD-lspec
      - @ref KLNetCoreDLD-lspec-comps
      - @ref KLNetCoreDLD-lspec-userspace
      - @ref KLNetCoreDLD-lspec-match-bits
      - @ref KLNetCoreDLD-lspec-tm-list
      - @ref KLNetCoreDLD-lspec-bevq
      - @ref KLNetCoreDLD-lspec-lnet-init
      - @ref KLNetCoreDLD-lspec-reg
      - @ref KLNetCoreDLD-lspec-tm-res
      - @ref KLNetCoreDLD-lspec-buf-res
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
   The LNet Transport is built over an address space agnostic "core" I/O
   interface.  This document describes the kernel implementation of this
   interface, which directly interacts with the Lustre LNet kernel module.

   <hr>
   @section KLNetCoreDLD-def Definitions
   Refer to <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>

   <hr>
   @section KLNetCoreDLD-req Requirements
   - @b r.c2.net.lnet.buffer-registration Provide support for
     hardware optimization through buffer pre-registration.

   - @b r.c2.net.xprt.lnet.end-point-address The implementation
     should support the mapping of end point address to LNet address
     as described in the Refinement section of the HLD.

   - @b r.c2.net.xprt.lnet.multiple-messages-in-buffer Provide
     support for this feature as described in the HLD.

   - @b r.c2.net.xprt.lnet.dynamic-address-assignment Provide
     support for dynamic address assignment as described in the HLD.

   - @b r.c2.net.xprt.lnet.user-space The implementation must
     accommodate the needs of the user space LNet transport.

   - @b r.c2.net.xprt.lnet.user.no-gpl The implementation must not expose
     the user space transport to GPL interfaces.


   <hr>
   @section KLNetCoreDLD-depends Dependencies
   - <b>LNet API</b> headers are required to build the module.
   The Xyratex Lustre source package must be installed on the build
   machine (RPM @c lustre-source version 2.0 or greater).
   - <b>Xyratex Lustre run time</b>
   - @b r.c2.lib.atomic.interoperable-kernel-user-support The @ref LNetcqueueDLD
   "Buffer Event Circular Queue" <!-- ../bev_cqueue.c -->
   provides a shared data structure for efficiently passing event notifications
   from the Core layer to the LNet transport layer.
   - @b r.net.xprt.lnet.growable-event-queue The @ref LNetcqueueDLD
   "Buffer Event Circular Queue" <!-- ../bev_cqueue.c -->
   provides a way to expand the event queue as new buffers are queued with a
   transfer machine, ensuring no events are lost.

   <hr>
   @section KLNetCoreDLD-highlights Design Highlights
   - The Core API is an address space agnostic I/O interface intended for use
     by the Colibri Networking LNet transport operation layer in either user
     space or kernel space.

   - Efficient support for the user space transports is provided by use of
     cross-address space tolerant data structures in shared memory.

   - The Core API does not expose any LNet symbols.

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

   - @ref KLNetCoreDLD-lspec-comps
   - @ref KLNetCoreDLD-lspec-userspace
   - @ref KLNetCoreDLD-lspec-match-bits
   - @ref KLNetCoreDLD-lspec-tm-list
   - @ref KLNetCoreDLD-lspec-bevq
   - @ref KLNetCoreDLD-lspec-lnet-init
   - @ref KLNetCoreDLD-lspec-reg
   - @ref KLNetCoreDLD-lspec-tm-res
   - @ref KLNetCoreDLD-lspec-buf-res
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
   nlx_core_buf_desc_encode() subroutine to generate new match bits for
   the passive buffer.  The match bit counter will repeat over time, though
   after a very long while.  It is the transport's responsibility to ensure
   that all of the passive buffers associated with a given transfer machine
   have unique match bits.  The match bits should be encoded into the network
   buffer descriptor associated with the passive buffer.


   @subsection KLNetCoreDLD-lspec-tm-list Transfer Machine Uniqueness

   The kernel Core module must ensure that all transfer machines on the host
   have unique transfer machine identifiers for a given NID/PID/Portal,
   regardless of the transport instance or network domain context in which
   these transfer machines are created.  To support this, the ::nlx_kcore_tms
   list threads through all the kernel Core's per-TM private data
   structures. This list is private to the kernel Core, and is protected by the
   ::nlx_kcore_mutex.

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

   Instead, the kernel Core module decouples the delivery of buffer operation
   completion to the transport from the LNet callback context by copying the
   result to an intermediate buffer event queue.  The Core API provides the
   nlx_core_buf_event_wait() subroutine that the transport can use to poll for
   the presence of buffer events, and the nlx_core_buf_event_get() subroutine
   to recover the payload of the next available buffer event. See @ref
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
   delivered and will be lost.  As the event queue is in shared memory, it is
   not possible, let alone desirable, to allocate a new buffer event structure
   in the callback context.

   The Core API guarantees the delivery of buffer operation completion status
   by maintaining a "pool" of free buffer event structures for this purpose.
   It does so by keeping count of the total number of buffer event structures
   required to satisfy all outstanding operations, and adding additional such
   structures to the "pool" if necessary, when a new buffer operation is
   initiated.  Likewise, the count is decremented for each buffer event
   delivered to the transport.  Most buffers operations only need a single
   buffer event structure in which to return their operation result, but
   receive buffers may need more, depending on the individually
   configurable maximum number of messages that could be received in each
   receive buffer.

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

   No hardware optimization support is defined in the LNet API at this time but
   the nlx_core_buf_register() subroutine serves as a placeholder where any
   such optimizations could be made in the future.  The
   nlx_core_buf_deregister() subroutine would be used to release any allocated
   resources.

   During buffer registration, the kernel Core API will translate the
   c2_net_bufvec into the nlx_kcore_buffer::kb_kiov field of the buffer private
   data.

   The kernel implementation of the Core API does not increment the page count
   of the buffer pages.  The supposition here is that the buffers are allocated
   by Colibri file system clients, and the Core API has no business imposing
   memory management policy beneath such a client.


   @subsection KLNetCoreDLD-lspec-tm-res LNet Transfer Machine Resources

   A transfer machine is associated with the following LNet resources:
   - An Event Queue (EQ).  This is represented by the
   nlx_kcore_transfer_mc::ktm_eqh handle.

   The nlx_core_tm_start() subroutine creates the event handle.  The
   nlx_core_tm_stop() subroutine releases the handle.
   See @ref KLNetCoreDLD-lspec-ev for more details on event processing.


   @subsection KLNetCoreDLD-lspec-buf-res LNet Buffer Resources

   A network buffer is associated with a Match Descriptor (MD).  This is
   represented by the nlx_kcore_buffer::kb_mdh handle.  There may be a Match
   Entry (ME) associated with this MD for some operations, but when created, it
   is set up to unlink automatically when the MD is unlinked so it is not
   explicitly tracked.

   All the buffer operation initiation subroutines of the kernel Core API
   create such MDs.  Although an MD is set up to explicitly unlink upon
   completion, the value is saved in case an operation needs to be
   cancelled.

   All MDs are associated with the EQ of the transfer machine
   (nlx_kcore_transfer_mc::ktm_eqh).


   @subsection KLNetCoreDLD-lspec-ev LNet Event Callback Processing

   LNet event queues are used with an event callback subroutine to avoid event
   loss.  The callback subroutine overhead is fairly minimal, as it only copies
   out the event payload and arranges for subsequent asynchronous delivery.
   This, coupled with the fact that the circular buffer used works optimally
   with a single producer and single consumer resulted in the decision to use
   just one LNet EQ per transfer machine (nlx_kcore_transfer_mc::ktm_eqh).

   The EQ is created in the call to the nlx_core_tm_start() subroutine, and
   is freed in the call to the nlx_core_tm_stop() subroutine.

   LNet requires that the callback subroutine be re-entrant and non-blocking,
   and not make any LNet API calls. Given that the circular queue assumes a
   single producer and single consumer, a spin lock is used to serialize access
   of the two EQs to the circular queue.

   The event callback requires that the MD @c user_ptr field be set up to the
   address of the nlx_kcore_buffer data structure.  Note that if an event
   has the @c unlinked field set then this will be the last event that LNet will
   post for the related operation, and the @c user_ptr field will be valid, so
   the callback can safely de-reference the field to determine the correct
   queue.

   The callback subroutine does the following:

   -# It will ignore @c LNET_EVENT_SEND events delivered as a result of a @c
      LNetGet() call if the @c unlinked field of the event is not set. If the
      @c unlinked field is set, the event could either be an out-of-order SEND
      (terminating a REPLY/SEND sequence), or the piggy-backed UNLINK on an
      in-order SEND.  The two cases are distinguished by explicitly tracking
      the receipt of an out-of-order REPLY (in nlx_kcore_buffer::kb_ooo_reply).
      An out-of-order SEND will be treated as though it is the terminating @c
      LNET_EVENT_REPLY event of a SEND/REPLY sequence.
   -# It will not create an event in the circular queue for @c LNET_EVENT_REPLY
      events that do not have their @c unlinked field set.  They indicate an
      out-of-sequence REPLY/SEND combination, and LNet will issue a valid SEND
      event subsequently.  However, the receipt of such an REPLY will be
      remembered in nlx_kcore_buffer::kb_ooo_reply, and its payload in the
      other "ooo" fields, so that when the out-of-order SEND arrives, this data
      can be used to generate the circular queue event.
   -# It will ignore @c LNET_EVENT_ACK events.
   -# It obtains the nlx_kcore_transfer_mc::ktm_bevq_lock spin lock.
   -# The bev_cqueue_pnext() subroutine is then used to locate the next buffer
      event structure in the circular buffer event queue which will be used to
      return the result.
   -# It copies the event payload from the LNet event to the buffer event
      structure.  This includes the value of the @c unlinked field of the
      event, which must be copied to the nlx_core_buffer_event::cbe_unlinked
      field.  For @c LNET_EVENT_UNLINK events, a @c -ECANCELED value is
      written to the nlx_core_buffer_event::cbe_status field and the
      nlx_core_buffer_event::cbe_unlinked field set to true.
      For @c LNET_EVENT_PUT events corresponding to unsolicited message
      delivery, the sender's TMID and Portal are encoded
      in the hdr_data.  These values are decoded into the
      nlx_core_buffer_event::cbe_sender, along with the initiator's NID and PID.
      The nlx_core_buffer_event::cbe_sender is not set for other events.
   -# It invokes the bev_cqueue_put() subroutine to "produce" the event in the
      circular queue.
   -# It releases the nlx_kcore_transfer_mc::ktm_bevq_lock spin lock.
   -# It signals the nlx_kcore_transfer_mc::ktm_sem semaphore with the
      c2_semaphore_up() subroutine.

   The (single) transport layer event handler thread blocks on the Core
   transfer machine semaphore in the Core API nlx_core_buf_event_wait()
   subroutine which uses the c2_semaphore_timeddown() subroutine internally to
   wait on the semaphore.  When the Core API subroutine returns with an
   indication of the presence of events, the event handler thread consumes all
   the pending events with multiple calls to the Core API
   nlx_core_buf_event_get() subroutine, which uses the bev_cqueue_get()
   subroutine internally to get the next buffer event.  Then the event handler
   thread repeats the call to the nlx_core_buf_event_wait() subroutine to once
   again block for additional events.

   In the case of the user space transport, the blocking on the semaphore is
   done indirectly by the user space Core API's device driver in the kernel.
   It is required by the HLD that as many events as possible be consumed before
   the next context switch to the kernel must be made.  To support this, the
   kernel Core nlx_core_buf_event_wait() subroutine takes a few additional
   steps to minimize the chance of returning when the queue is empty.  After it
   obtains the semaphore with the c2_semaphore_timeddown() subroutine (i.e. the
   @em P operation succeeds), it attempts to clear the semaphore count by
   repeatedly calling the c2_semaphore_trydown() subroutine until it fails.  It
   then checks the circular queue, and only if not empty will it return.  This
   is illustrated with the following pseudo-code:
   @code
       do {
          rc = c2_semaphore_timeddown(&sem, &timeout);
          if (rc < 0)
              break; // timed out
          while (c2_semaphore_trydown(&sem))
              ; // exhaust the semaphore
       } while (bev_cqueue_is_empty(&q)); // loop if empty
   @endcode
   (The C++ style comments are used because of doxygen only - they are not
   permitted by the Colibri style guide.)

   @subsection KLNetCoreDLD-lspec-recv LNet Receiving Unsolicited Messages

   -# Create an ME with @c LNetMEAttach() for the transfer machine and specify
      the portal, match and ignore bits. All receive buffers for a given TM
      will use a match bit value equal to the TM identifier in the higher order
      bits and zeros for the other bits.  No ignore bits are set. The ME should
      be set up to unlink automatically as it will be used for all receive
      buffers of this transfer machine.  The ME entry should be positioned at
      the end of the portal match list.  There is no need to retain the ME
      handle beyond the subsequent @c LNetMDAttach() call.
   -# Create and attach an MD to the ME using @c LNetMDAttach().
      The MD is set up to unlink automatically.
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the address of the nlx_kcore_buffer in the @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
      - Set the @c threshold value to the nlx_kcore_buffer::kb_max_recv_msgs
        value.
      - Set the @c max_size value to the nlx_kcore_buffer::kb_min_recv_size
        value.
      - Set the @c LNET_MD_OP_PUT, @c LNET_MD_MAX_SIZE and @c LNET_MD_KIOV
        flags in the @c options field.
   -# When a message arrives, an @c LNET_EVENT_PUT event will be delivered to
      the event queue, and will be processed as described in
      @ref KLNetCoreDLD-lspec-ev.


   @subsection KLNetCoreDLD-lspec-send LNet Sending Messages

   -# Create an MD using @c LNetMDBind() with each invocation of the
      nlx_core_buf_msg_send() subroutine.
      The MD is set up to unlink automatically.
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the address of the nlx_kcore_buffer in the @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
        The number of entries in the KIOV and the length field in the last
        element of the vector must be adjusted to reflect the desired byte
        count.
      - Set the @c LNET_MD_KIOV flag in the @c options field.
   -# Use the @c LNetPut() subroutine to send the MD to the destination.  The
      match bits must set to the destination TM identifier in the higher order
      bits and zeros for the other bits. The hdr_data must be set to a value
      encoding the TMID (in the upper bits, like the match bits) and the portal
      (in the lower bits). No acknowledgment should be requested.
   -# When the message is sent, an @c LNET_EVENT_SEND event will be delivered
      to the event queue, and processed as described in
      @ref KLNetCoreDLD-lspec-ev.
      @note The event does not indicate if the recipient was able to save the
      data, but merely that it left the host.

   @subsection KLNetCoreDLD-lspec-passive LNet Staging Passive Bulk Buffers

   -# Prior to invoking the nlx_core_buf_passive_recv() or the
      nlx_core_buf_passive_send() subroutines, the transport should use the
      nlx_core_buf_desc_encode() subroutine to assign unique match bits to
      the passive buffer. See @ref KLNetCoreDLD-lspec-match-bits for details.
      The match bits should be encoded into the network buffer descriptor and
      independently conveyed to the remote active transport.
      The network descriptor also encodes the number of bytes to be transferred.
   -# Create an ME using @c LNetMEAttach(). Specify the portal and match_id
      fields as appropriate for the transfer machine.  The buffer's match bits
      are obtained from the nlx_core_buffer::cb_match_bits field.  No ignore
      bits are set. The ME should be set up to unlink automatically, so there
      is no need to save the handle for later use.  The ME should be positioned
      at the end of the portal match list.
   -# Create and attach an MD to the ME using @c LNetMDAttach() with each
      invocation of the nlx_core_buf_passive_recv() or the
      nlx_core_buf_passive_send() subroutines.
      The MD is set up to unlink automatically.
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the address of the nlx_kcore_buffer in the @c user_ptr field.
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
      nlx_core_buffer::cb_addr field.
   -# Create an MD using @c LNetMDBind() with each invocation of the
      nlx_core_buf_active_recv() or nlx_core_buf_active_send() subroutines.
      The MD is set up to unlink automatically.
      Save the MD handle in the nlx_kcore_buffer::kb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with
        the transfer machine (nlx_kcore_transfer_mc::ktm_eqh).
      - Set the address of the nlx_kcore_buffer in the @c user_ptr field.
      - Pass in the KIOV from the nlx_kcore_buffer::kb_kiov.
        The number of entries in the KIOV and the length field in the last
        element of the vector must be adjusted to reflect the desired byte
        count.
      - Set the @c LNET_MD_KIOV flag in the @c options field.
      - In case of an active read, which uses @c LNetGet(), set the threshold
        value to 2 to accommodate both the SEND and the REPLY events. Otherwise
	set it to 1.
   -# Use the @c LNetGet() subroutine to initiate the active read or the @c
      LNetPut() subroutine to initiate the active write. The @c hdr_data is set
      to 0 in the case of @c LNetPut(). No acknowledgment should be requested.
      In the case of an @c LNetGet(), the field used to track out-of-order
      REPLY events (nlx_kcore_buffer::kb_ooo_reply) should be cleared before
      the operation is initiated.
   -# When a response to the @c LNetGet() or @c LNetPut() call completes, an @c
      LNET_EVENT_SEND event will be delivered to the event queue and should
      typically be ignored in the case of @c LNetGet().
      See @ref KLNetCoreDLD-lspec-ev for details.
   -# When the bulk data transfer for @c LNetGet() completes, an
      @c LNET_EVENT_REPLY event will be delivered to the event queue, and will
      be processed as described in @ref KLNetCoreDLD-lspec-ev.

      @note LNet does not guarantee the order of the SEND and REPLY events
      associated with the @c LNetGet() operation.  Also note that in the case
      of an @c LNetGet() operation, the SEND event does not indicate if the
      recipient was able to save the data, but merely that the request left the
      host.


   @subsection KLNetCoreDLD-lspec-lnet-cancel LNet Canceling Operations

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

   LNet properly handles the race condition between the automatic unlink of the
   MD and a call to @c LNetMDUnlink().


   @subsection KLNetCoreDLD-lspec-state State Specification
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
   run time. This dependency is captured by the Linux kernel module support that
   reference counts the usage of dependent modules.

   - The kernel Core layer modules explicitly tracks the events received for @c
   LNetGet() calls, in the nlx_kcore_buffer data structure associated with the
   call.  This is because there are two events (SEND and REPLY) that are
   returned for this operation, and LNet does not guarantee their order of
   arrival, and the event processing logic is set up such that a circular
   buffer event must be created only upon receipt of the last operation event.
   Complicating the issue is that a cancellation response could be piggy-backed
   onto an in-order SEND.  See @ref KLNetCoreDLD-lspec-ev and @ref
   KLNetCoreDLD-lspec-active for details.


   @subsection KLNetCoreDLD-lspec-thread Threading and Concurrency Model
   -# Generally speaking, API calls within the transport address space
      are protected by the serialization of the Colibri Networking layer,
      typically the transfer machine mutex or the domain mutex.
      The nlx_core_buf_desc_encode() subroutine, for example, is fully
      protected by the transfer machine mutex held across the
      c2_net_buffer_add() subroutine call, so implicitly protects the match bit
      counter in the kernel Core's per TM private data.
   -# The Colibri Networking layer serialization does not always suffice, as
      the kernel Core module has to support concurrent multiple transport
      instances in kernel and user space.  Fortunately, the LNet API
      intrinsically provides considerable serialization support to the Core, as
      transfer machines are defined by the HLD to have disjoint addresses.
   -# Enforcement of the disjoint address semantics are protected by the
      kernel Core's ::nlx_kcore_mutex lock.  The nlx_core_tm_start() and
      nlx_core_tm_stop() subroutines use this mutex internally for serialization
      and operation on the ::nlx_kcore_tms list threaded through the kernel
      Core's per-TM private data.
   -# The kernel Core module registers a callback subroutine with the
      LNet EQ defined per transfer machine. LNet requires that this subroutine
      be reentrant and non-blocking.  The circular buffer event queue accessed
      from the callback requires a single producer, so the
      nlx_kcore_transfer_mc::ktm_bevq_lock spin lock is used to serialize its
      use across possible concurrent invocations.  The time spent in the lock
      is minimal.
   -# The Core API does not support callbacks to indicate completion of an
      asynchronous buffer operation.  Instead, the transport application must
      invoke the nlx_core_buf_event_wait() subroutine to block waiting for
      buffer events.  Internally this call waits on the
      nlx_kcore_transfer_mc::ktm_sem semaphore.  The semaphore is
      incremented each time an event is added to the buffer event queue.
   -# The event payload is actually delivered via a per transfer machine
      single producer, single consumer, lock-free circular buffer event queue.
      The only requirement for failure free operation is to ensure that there
      are sufficient event structures pre-allocated to the queue, plus one more
      to support the circular semantics.  Multiple events may be dequeued
      between each call to the nlx_core_buf_event_wait() subroutine.  Each such
      event is fetched by a call to the nlx_core_buf_event_get() subroutine,
      until the queue is exhausted.  Note that the queue exists in memory
      shared between the transport and the kernel Core; the transport could be
      in the kernel or in user space.
   -# The API assumes that only a single transport thread will handle event
      processing.  This is a critical assumption in the support for multiple
      messages in a single receive buffer, as it implicitly serializes the
      delivery of the events associated with any given receive buffer, thus the
      last event which unlinks the buffer is guaranteed to be delivered last.
   -# LNet properly handles the race condition between the automatic unlink
      of the MD and a call to @c LNetMDUnlink().

   @subsection KLNetCoreDLD-lspec-numa NUMA optimizations
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
   - @b i.c2.net.lnet.buffer-registration See @ref KLNetCoreDLD-lspec-reg.

   - @b i.c2.net.xprt.lnet.end-point-address The nlx_core_ep_addr_encode()
     and nlx_core_ep_addr_decode() provide this functionality.

   - @b i.c2.net.xprt.lnet.multiple-messages-in-buffer See @ref
     KLNetCoreDLD-lspec-recv.

   - @b i.c2.net.xprt.lnet.dynamic-address-assignment See @ref
     KLNetCoreDLD-lspec-tm-list.

   - @b i.c2.net.xprt.lnet.user-space See @ref KLNetCoreDLD-lspec-userspace.

   - @b i.c2.net.xprt.lnet.user.no-gpl See the @ref LNetCoreDLD-fspec
     "Functional Specification"; no LNet headers are exposed by the Core API.

   <hr>
   @section KLNetCoreDLD-ut Unit Tests
   The testing strategy is 2 pronged:
   - Tests with a fake LNet API.  These tests will intercept the LNet
     subroutine calls.  The real LNet data structures will be used by the Core
     API.
   - Tests with the real LNet API using the TCP loop back address.  These tests
     will use the TCP loop back address.  LNet on the test machine must be
     configured with the @c "tcp" network.

   @test The correct sequence of LNet operations are issued for each type
         of buffer operation with a fake LNet API.

   @test The callback subroutine properly delivers events to the buffer
         event queue, including single and multiple events for receive buffers
         with a fake LNet API.

   @test The dynamic assignment of transfer machine identifiers with a fake LNet
         API.

   @test Test the parsing of LNet addresses with the real LNet API.

   @test Test each type of buffer operation, including single and multiple
         events for receive buffers with the real LNet API.

   <hr>
   @section KLNetCoreDLD-st System Tests
   System testing will be performed as part of the transport operation system
   test.

   <hr>
   @section KLNetCoreDLD-O Analysis
   - Dynamic transfer machine identifier assignment is proportional to the
   number of transfer machines defined on the server, including kernel and all
   process space LNet transport instances.
   - The time taken to process an LNet event callback is in constant time.
   - The time taken for the transport to dequeue a pending buffer event
   depends upon the operating system scheduler.  The algorithmic
   processing involved is in constant time.
   - The time taken to register a buffer is in constant time.  The reference
     count of the buffer pages is not incremented, so there are no VM subsystem
     imposed delays.
   - The time taken to process outbound buffer operations is unpredictable,
   and depends, at the minimum, on current system load, other LNet users,
   and on the network load.

   <hr>
   @section KLNetCoreDLD-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   - The LNet API.

 */

/*
 ******************************************************************************
 End of DLD
 ******************************************************************************
 */

#include "lib/mutex.h"

#include <lnet/lnet.h> /* LNet API, LNET_NIDSTR_SIZE */

/* include local files */
#include "net/lnet/linux_kernel/klnet_vec.c"
#include "net/lnet/linux_kernel/klnet_utils.c"

/**
   @addtogroup KLNetCore
   @{
 */

/**
   Kernel core lock.
   Provides serialization across the nlx_kcore_tms list.
 */
static struct c2_mutex nlx_kcore_mutex;

/** List of all transfer machines. Protected by nlx_kcore_mutex. */
static struct c2_tl nlx_kcore_tms;

/** NID strings of LNIs. */
static char **nlx_kcore_lni_nidstrs;
/** The count of non-NULL entries in nlx_kcore_lni_nidstrs.
    @note Caching this count and the string list will no longer be possible
    if and when dynamic NI addition/removal is supported by LNet.
 */
static unsigned int nlx_kcore_lni_nr;
/** Reference counter for nlx_kcore_lni_nidstrs. */
static struct c2_atomic64 nlx_kcore_lni_refcount;

C2_TL_DESCR_DEFINE(tms, "nlx tms", static, struct nlx_kcore_transfer_mc,
		   ktm_tm_linkage, ktm_magic, C2_NET_LNET_KCORE_TM_MAGIC,
		   C2_NET_LNET_KCORE_TMS_MAGIC);
C2_TL_DEFINE(tms, static, struct nlx_kcore_transfer_mc);

C2_TL_DESCR_DEFINE(drv_tms, "drv tms", static, struct nlx_kcore_transfer_mc,
		   ktm_drv_linkage, ktm_magic, C2_NET_LNET_KCORE_TM_MAGIC,
		   C2_NET_LNET_DEV_TMS_MAGIC);
C2_TL_DEFINE(drv_tms, static, struct nlx_kcore_transfer_mc);

C2_TL_DESCR_DEFINE(drv_bufs, "drv bufs", static, struct nlx_kcore_buffer,
		   kb_drv_linkage, kb_magic, C2_NET_LNET_KCORE_BUF_MAGIC,
		   C2_NET_LNET_DEV_BUFS_MAGIC);
C2_TL_DEFINE(drv_bufs, static, struct nlx_kcore_transfer_mc);

C2_TL_DESCR_DEFINE(drv_bevs, "drv bevs", static, struct nlx_kcore_buffer_event,
		   kbe_drv_linkage, kbe_magic, C2_NET_LNET_KCORE_BEV_MAGIC,
		   C2_NET_LNET_DEV_BEVS_MAGIC);
C2_TL_DEFINE(drv_bevs, static, struct nlx_kcore_transfer_mc);

/* assert the equivalence of LNet and Colibri data types */
C2_BASSERT(sizeof(__u64) == sizeof(uint64_t));

/** Unit test intercept support.
   Conventions to use:
   - All such subs must be declared in headers.
   - A macro named for the subroutine, but with the "NLX" portion of the prefix
   in capitals, should be used to call the subroutine via this intercept
   vector.
   - UT should restore the vector upon completion. It is not declared
   const so that the UTs can modify it.
 */
struct nlx_kcore_interceptable_subs {
	int (*_nlx_kcore_LNetMDAttach)(struct nlx_core_transfer_mc *lctm,
				       struct nlx_kcore_transfer_mc *kctm,
				       struct nlx_core_buffer *lcbuf,
				       struct nlx_kcore_buffer *kcb,
				       lnet_md_t *umd);
	int (*_nlx_kcore_LNetPut)(struct nlx_core_transfer_mc *lctm,
				  struct nlx_kcore_transfer_mc *kctm,
				  struct nlx_core_buffer *lcbuf,
				  struct nlx_kcore_buffer *kcb,
				  lnet_md_t *umd);
	int (*_nlx_kcore_LNetGet)(struct nlx_core_transfer_mc *lctm,
				  struct nlx_kcore_transfer_mc *kctm,
				  struct nlx_core_buffer *lcbuf,
				  struct nlx_kcore_buffer *kcb,
				  lnet_md_t *umd);
};
static struct nlx_kcore_interceptable_subs nlx_kcore_iv = {
#define _NLXIS(s) ._##s = s

	_NLXIS(nlx_kcore_LNetMDAttach),
	_NLXIS(nlx_kcore_LNetPut),
	_NLXIS(nlx_kcore_LNetGet),

#undef _NLXI
};

#define NLX_kcore_LNetMDAttach(lctm, ktm, lcbuf, kb, umd)	\
	(*nlx_kcore_iv._nlx_kcore_LNetMDAttach)(lctm, ktm, lcbuf, kb, umd)
#define NLX_kcore_LNetPut(lctm, ktm, lcbuf, kb, umd)		\
	(*nlx_kcore_iv._nlx_kcore_LNetPut)(lctm, ktm, lcbuf, kb, umd)
#define NLX_kcore_LNetGet(lctm, ktm, lcbuf, kb, umd)		\
	(*nlx_kcore_iv._nlx_kcore_LNetGet)(lctm, ktm, lcbuf, kb, umd)

/**
   KCore domain invariant.
   @note Unlike other kernel core object invariants, the reference to the
   nlx_core_domain is allowed to be NULL, because initialization of the
   nlx_kcore_domain in the driver is split between the open and C2_LNET_DOM_INIT
   ioctl request.
 */
static bool nlx_kcore_domain_invariant(const struct nlx_kcore_domain *kd)
{
	if (kd == NULL || kd->kd_magic != C2_NET_LNET_KCORE_DOM_MAGIC)
		return false;
	if (!nlx_core_kmem_loc_is_empty(&kd->kd_cd_loc) &&
	    !nlx_core_kmem_loc_invariant(&kd->kd_cd_loc))
		return false;
	return true;
}

/**
   KCore buffer invariant.
   @note Shouldn't require the mutex as it is called from nlx_kcore_eq_cb.
 */
static bool nlx_kcore_buffer_invariant(const struct nlx_kcore_buffer *kcb)
{
	if (kcb == NULL || kcb->kb_magic != C2_NET_LNET_KCORE_BUF_MAGIC)
		return false;
	if (!nlx_core_kmem_loc_invariant(&kcb->kb_cb_loc))
		return false;
	return true;
}

/**
   KCore tm invariant.
   @note Shouldn't require the mutex as it is called from nlx_kcore_eq_cb.
 */
static bool nlx_kcore_tm_invariant(const struct nlx_kcore_transfer_mc *kctm)
{
	if (kctm == NULL || kctm->ktm_magic != C2_NET_LNET_KCORE_TM_MAGIC)
		return false;
	if (!nlx_core_kmem_loc_invariant(&kctm->ktm_ctm_loc))
		return false;
	return true;
}

/**
   Tests if the specified address is in use by a running TM.
   @note the nlx_kcore_mutex must be locked by the caller
 */
static bool nlx_kcore_addr_in_use(struct nlx_core_ep_addr *cepa)
{
	bool matched = false;
	struct nlx_kcore_transfer_mc *scan;
	struct nlx_core_ep_addr *scanaddr;
	C2_PRE(c2_mutex_is_locked(&nlx_kcore_mutex));

	c2_tlist_for(&tms_tl, &nlx_kcore_tms, scan) {
		scanaddr = &scan->ktm_addr;
		if (nlx_core_ep_eq(scanaddr, cepa)) {
			matched = true;
			break;
		}
	} c2_tlist_endfor;
	return matched;
}

/**
   Find an unused tmid.
   @note The nlx_kcore_mutex must be locked by the caller
   @param cepa The NID, PID and Portal are used to filter the ::nlx_kcore_tms
   @return The largest available tmid, or -EADDRNOTAVAIL if none exists.
 */
static int nlx_kcore_max_tmid_find(struct nlx_core_ep_addr *cepa)
{
	int tmid = C2_NET_LNET_TMID_MAX;
	struct nlx_kcore_transfer_mc *scan;
	struct nlx_core_ep_addr *scanaddr;
	C2_PRE(c2_mutex_is_locked(&nlx_kcore_mutex));

	/* list is in descending order by tmid */
	c2_tlist_for(&tms_tl, &nlx_kcore_tms, scan) {
		scanaddr = &scan->ktm_addr;
		if (scanaddr->cepa_nid == cepa->cepa_nid &&
		    scanaddr->cepa_pid == cepa->cepa_pid &&
		    scanaddr->cepa_portal == cepa->cepa_portal) {
			if (scanaddr->cepa_tmid == tmid)
				--tmid;
			else if (scanaddr->cepa_tmid < tmid)
				break;
		}
	} c2_tlist_endfor;
	return tmid >= 0 ? tmid : -EADDRNOTAVAIL;
}

/**
   Add the transfer machine to the ::nlx_kcore_tms.  The list is kept in
   descending order sorted by nlx_core_ep_addr::cepa_tmid.
   @note the nlx_kcore_mutex must be locked by the caller
 */
static void nlx_kcore_tms_list_add(struct nlx_kcore_transfer_mc *kctm)
{
	struct nlx_kcore_transfer_mc *scan;
	struct nlx_core_ep_addr *scanaddr;
	struct nlx_core_ep_addr *cepa = &kctm->ktm_addr;
	C2_PRE(c2_mutex_is_locked(&nlx_kcore_mutex));

	c2_tlist_for(&tms_tl, &nlx_kcore_tms, scan) {
		scanaddr = &scan->ktm_addr;
		if (scanaddr->cepa_tmid <= cepa->cepa_tmid) {
			tms_tlist_add_before(scan, kctm);
			return;
		}
	} c2_tlist_endfor;
	tms_tlist_add_tail(&nlx_kcore_tms, kctm);
}

/**
   Callback for the LNet Event Queue.
   It must be re-entrant, and not make any calls to the LNet API.
   It must not perform non-atomic operations, such as locking a mutex.
   An atomic spin lock is used to synchronize access to the
   nlx_core_transfer_mc::nlx_core_bev_cqueue.
 */
static void nlx_kcore_eq_cb(lnet_event_t *event)
{
	struct nlx_kcore_buffer *kbp;
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_core_transfer_mc *lctm;
	struct nlx_core_bev_link *ql;
	struct nlx_core_buffer_event *bev;
	c2_time_t now = c2_time_now();
	bool is_unlinked = false;
	unsigned mlength;
	unsigned offset;
	int status;

	C2_PRE(event != NULL);
	if (event->type == LNET_EVENT_ACK) {
		/* we do not use ACK */
		NLXDBG(&nlx_debug, 1,
		       nlx_kprint_lnet_event("nlx_kcore_eq_cb: filtered ACK",
					     event));
		LNET_ADDB_FUNCFAIL_ADD(c2_net_addb, -EPROTO);
		return;
	}
	kbp = event->md.user_ptr;
	C2_ASSERT(nlx_kcore_buffer_invariant(kbp));
	ktm = kbp->kb_ktm;
	C2_ASSERT(nlx_kcore_tm_invariant(ktm));
	lctm = nlx_kcore_core_tm_map_atomic(ktm);

	NLXDBGP(lctm, 1, "\t%p: eq_cb: %p %s U:%d S:%d T:%d buf:%lx\n",
		lctm, event, nlx_kcore_lnet_event_type_to_string(event->type),
		event->unlinked, event->status, event->md.threshold,
		(unsigned long) kbp->kb_buffer_id);
	NLXDBG(lctm, 2, nlx_kprint_lnet_event("eq_cb", event));
	NLXDBG(lctm, 3, nlx_kprint_kcore_tm("eq_cb", ktm));

	if (event->unlinked != 0) {
		LNetInvalidateHandle(&kbp->kb_mdh); /* Invalid use, but safe */
		kbp->kb_ktm = NULL;
		is_unlinked = true;
	}
	status  = event->status;
	mlength = event->mlength;
	offset  = event->offset;

	if (event->type == LNET_EVENT_SEND &&
	    kbp->kb_qtype == C2_NET_QT_ACTIVE_BULK_RECV) {
		/* An LNetGet related event, normally ignored */
		if (!is_unlinked) {
			NLXDBGP(lctm, 1, "\t%p: ignored LNetGet() SEND\n",lctm);
			goto done;
		}
		/* An out-of-order SEND, or
		   cancellation notification piggy-backed onto an in-order SEND.
		   The only way to distinguish is from the value of the
		   event->kb_ooo_reply field.
		 */
		NLXDBGP(lctm, 1,
			"\t%p: LNetGet() SEND with unlinked: thr:%d ooo:%d\n",
			lctm, event->md.threshold, (int) kbp->kb_ooo_reply);
		if (status == 0) {
			if (!kbp->kb_ooo_reply)
				status = -ECANCELED;
			else {  /* from earlier REPLY */
				mlength = kbp->kb_ooo_mlength;
				offset  = kbp->kb_ooo_offset;
				status  = kbp->kb_ooo_status;
			}
		}
	} else if (event->type == LNET_EVENT_UNLINK) {/* see nlx_core_buf_del */
		C2_ASSERT(is_unlinked);
		status = -ECANCELED;
	} else if (!is_unlinked) {
		NLXDBGP(lctm, 1, "\t%p: eq_cb: %p %s !unlinked Q=%d\n", lctm,
			event, nlx_kcore_lnet_event_type_to_string(event->type),
			kbp->kb_qtype);
		/* We may get REPLY before SEND, so ignore such events,
		   but save the significant values for when the SEND arrives.
		 */
		if (kbp->kb_qtype == C2_NET_QT_ACTIVE_BULK_RECV) {
			kbp->kb_ooo_reply   = true;
			kbp->kb_ooo_mlength = mlength;
			kbp->kb_ooo_offset  = offset;
			kbp->kb_ooo_status  = status;
			goto done;
		}
		/* we don't expect anything other than receive messages */
		C2_ASSERT(kbp->kb_qtype == C2_NET_QT_MSG_RECV);
	}

	spin_lock(&ktm->ktm_bevq_lock);
	ql = bev_cqueue_pnext(&lctm->ctm_bevq);
	bev = container_of(ql, struct nlx_core_buffer_event, cbe_tm_link);
	bev->cbe_buffer_id = kbp->kb_buffer_id;
	bev->cbe_time      = now;
	bev->cbe_status    = status;
	bev->cbe_length    = mlength;
	bev->cbe_offset    = offset;
	bev->cbe_unlinked  = is_unlinked;
	if (event->hdr_data != 0) {
		bev->cbe_sender.cepa_nid = event->initiator.nid;
		bev->cbe_sender.cepa_pid = event->initiator.pid;
		nlx_kcore_hdr_data_decode(event->hdr_data,
					  &bev->cbe_sender.cepa_portal,
					  &bev->cbe_sender.cepa_tmid);
	} else
		C2_SET0(&bev->cbe_sender);

	bev_cqueue_put(&lctm->ctm_bevq, ql);
	spin_unlock(&ktm->ktm_bevq_lock);
	c2_semaphore_up(&ktm->ktm_sem);
done:
	nlx_kcore_core_tm_unmap_atomic(lctm);
}

/**
   Set a memory location reference, including checksum.
   @pre off < PAGE_SIZE && ergo(pg == NULL, off == 0)
   @param loc Location to set.
   @param pg Pointer to page object.
   @param off Offset within the page.
 */
static void nlx_core_kmem_loc_set(struct nlx_core_kmem_loc *loc,
				  struct page *pg, uint32_t off)
{
	C2_PRE(off < PAGE_SIZE && ergo(pg == NULL, off == 0));
	C2_PRE(loc != NULL);

	loc->kl_page = pg;
	loc->kl_offset = off;
	loc->kl_checksum = nlx_core_kmem_loc_checksum(loc);
}

void *nlx_core_mem_alloc(size_t size, unsigned shift)
{
	return c2_alloc(size);
}

void nlx_core_mem_free(void *data, unsigned shift)
{
	c2_free(data);
}

/**
   Initializes the core private data given a previously initialized
   kernel core private data object.
   @param kd Kernel core private data pointer.
   @param cd Core private data pointer.
 */
static int nlx_kcore_core_dom_init(struct nlx_kcore_domain *kd,
				   struct nlx_core_domain *cd)
{
	C2_PRE(kd != NULL && cd != NULL);
	C2_PRE(nlx_kcore_domain_invariant(kd));
	cd->cd_kpvt = kd;
	return 0;
}

int nlx_core_dom_init(struct c2_net_domain *dom, struct nlx_core_domain *cd)
{
	struct nlx_kcore_domain *kd;
	int rc;

	C2_PRE(dom != NULL && cd != NULL);
	C2_ALLOC_PTR_ADDB(kd, &dom->nd_addb, &nlx_addb_loc);
	if (kd == NULL)
		return -ENOMEM;
	rc = nlx_kcore_kcore_dom_init(kd);
	if (rc != 0)
		goto fail_free_kd;
	nlx_core_kmem_loc_set(&kd->kd_cd_loc, virt_to_page(cd),
			      PAGE_OFFSET((unsigned long) cd));
	rc = nlx_kcore_core_dom_init(kd, cd);
	if (rc != 0)
		goto fail_dom_inited;
	return rc;
fail_dom_inited:
	nlx_kcore_kcore_dom_fini(kd);
fail_free_kd:
	c2_free(kd);
	C2_ASSERT(rc != 0);
	LNET_ADDB_FUNCFAIL_ADD(dom->nd_addb, rc);
	return rc;
}

/**
   Finilizes the core private data associated with a kernel core
   private data object.
   @param kd Kernel core private data pointer.
   @param cd Core private data pointer.
 */
static void nlx_kcore_core_dom_fini(struct nlx_kcore_domain *kd,
				    struct nlx_core_domain *cd)
{
	C2_PRE(cd != NULL && cd->cd_kpvt == kd);
	cd->cd_kpvt = NULL;
}

void nlx_core_dom_fini(struct nlx_core_domain *cd)
{
	struct nlx_kcore_domain *kd;

	C2_PRE(cd != NULL);
	kd = cd->cd_kpvt;
	C2_PRE(nlx_kcore_domain_invariant(kd));
	nlx_kcore_core_dom_fini(kd, cd);
	nlx_kcore_kcore_dom_fini(kd);
	c2_free(kd);
}

c2_bcount_t nlx_core_get_max_buffer_size(struct nlx_core_domain *lcdom)
{
	return LNET_MAX_PAYLOAD;
}

c2_bcount_t nlx_core_get_max_buffer_segment_size(struct nlx_core_domain *lcdom)
{
	/* PAGE_SIZE limit applies only when LNET_MD_KIOV has been set in
	 * lnet_md_t::options. There's no such limit in MD fragment size when
	 * LNET_MD_IOVEC is set.  DLD calls for only LNET_MD_KIOV to be used.
	 */
	return PAGE_SIZE;
}

int32_t nlx_core_get_max_buffer_segments(struct nlx_core_domain *lcdom)
{
	return LNET_MAX_IOV;
}

/**
   Performs common kernel core tasks related to registering a network
   buffer.  The nlx_kcore_buffer::kb_kiov is @b not set.
   @param kd Kernel core private domain pointer.
   @param buffer_id Value to set in the cb_buffer_id field.
   @param cb The core private data pointer for the buffer.
   @param kb Kernel core private buffer pointer.
 */
static int nlx_kcore_buf_register(struct nlx_kcore_domain *kd,
				  nlx_core_opaque_ptr_t buffer_id,
				  struct nlx_core_buffer *cb,
				  struct nlx_kcore_buffer *kb)
{
	C2_PRE(nlx_kcore_domain_invariant(kd));
	kb->kb_magic         = C2_NET_LNET_KCORE_BUF_MAGIC;
	kb->kb_ktm           = NULL;
	kb->kb_buffer_id     = buffer_id;
	kb->kb_kiov          = NULL;
	kb->kb_kiov_len      = 0;
	kb->kb_kiov_orig_len = 0;
	LNetInvalidateHandle(&kb->kb_mdh);
	kb->kb_ooo_reply     = false;
	kb->kb_ooo_mlength   = 0;
	kb->kb_ooo_status    = 0;
	kb->kb_ooo_offset    = 0;
	c2_addb_ctx_init(&kb->kb_addb, &nlx_core_buffer_addb_ctx, &kd->kd_addb);

	cb->cb_kpvt          = kb;
	cb->cb_buffer_id     = buffer_id;
	cb->cb_magic         = C2_NET_LNET_CORE_BUF_MAGIC;

	C2_POST(nlx_kcore_buffer_invariant(kb));
	return 0;
}

/**
   Performs common kernel core tasks related to de-registering a buffer.
   @param cb The core private data pointer for the buffer.
   @param kb Kernel core private buffer pointer.
 */
static void nlx_kcore_buf_deregister(struct nlx_core_buffer *cb,
				     struct nlx_kcore_buffer *kb)
{
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(LNetHandleIsInvalid(kb->kb_mdh));
	kb->kb_magic = 0;
	c2_addb_ctx_fini(&kb->kb_addb);
	c2_free(kb->kb_kiov);
	cb->cb_buffer_id = 0;
	cb->cb_kpvt = NULL;
	cb->cb_magic = 0;
}

int nlx_core_buf_register(struct nlx_core_domain *cd,
			  nlx_core_opaque_ptr_t buffer_id,
			  const struct c2_bufvec *bvec,
			  struct nlx_core_buffer *cb)
{
	int rc;
	struct nlx_kcore_buffer *kb;
	struct nlx_kcore_domain *kd;

	C2_PRE(cb != NULL && cb->cb_kpvt == NULL);
	C2_PRE(cd != NULL);
	kd = cd->cd_kpvt;
	C2_ALLOC_PTR_ADDB(kb, &kd->kd_addb, &nlx_addb_loc);
	if (kb == NULL)
		return -ENOMEM;
	nlx_core_kmem_loc_set(&kb->kb_cb_loc, virt_to_page(cb),
			      PAGE_OFFSET((unsigned long) cb));
	rc = nlx_kcore_buf_register(kd, buffer_id, cb, kb);
	if (rc != 0)
		goto fail_free_kb;
	rc = nlx_kcore_buffer_kla_to_kiov(kb, bvec);
	if (rc != 0)
		goto fail_buf_registered;
	C2_ASSERT(kb->kb_kiov != NULL && kb->kb_kiov_len > 0);
	C2_POST(nlx_kcore_buffer_invariant(cb->cb_kpvt));
	return 0;

fail_buf_registered:
	nlx_kcore_buf_deregister(cb, kb);
fail_free_kb:
	c2_free(kb);
	C2_ASSERT(rc != 0);
	LNET_ADDB_FUNCFAIL_ADD(kd->kd_addb, rc);
	return rc;
}

void nlx_core_buf_deregister(struct nlx_core_domain *cd,
			     struct nlx_core_buffer *cb)
{
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	nlx_kcore_buf_deregister(cb, kb);
	c2_free(kb);
}

/**
   Performs kernel core tasks relating to adding a buffer to
   the message receive queue.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
static int nlx_kcore_buf_msg_recv(struct nlx_core_transfer_mc *ctm,
				  struct nlx_kcore_transfer_mc *ktm,
				  struct nlx_core_buffer *cb,
				  struct nlx_kcore_buffer *kb)
{
	lnet_md_t umd;
	int rc;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(nlx_core_buffer_invariant(cb));
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(cb->cb_qtype == C2_NET_QT_MSG_RECV);
	C2_PRE(cb->cb_length > 0);
	C2_PRE(cb->cb_min_receive_size <= cb->cb_length);
	C2_PRE(cb->cb_max_operations > 0);

	nlx_kcore_umd_init(ctm, ktm, cb, kb, cb->cb_max_operations,
			   cb->cb_min_receive_size, LNET_MD_OP_PUT,
			   false, &umd);
	cb->cb_match_bits =
		nlx_core_match_bits_encode(ktm->ktm_addr.cepa_tmid, 0);
	cb->cb_addr = ktm->ktm_addr;
	rc = NLX_kcore_LNetMDAttach(ctm, ktm, cb, kb, &umd);
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(ktm->ktm_addb, rc);
	return rc;
}

int nlx_core_buf_msg_recv(struct nlx_core_domain *cd, /* not used */
			  struct nlx_core_transfer_mc *ctm,
			  struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	return nlx_kcore_buf_msg_recv(ctm, ktm, cb, kb);
}

/**
   Performs kernel core tasks relating to adding a buffer to
   the message receive queue.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
static int nlx_kcore_buf_msg_send(struct nlx_core_transfer_mc *ctm,
				  struct nlx_kcore_transfer_mc *ktm,
				  struct nlx_core_buffer *cb,
				  struct nlx_kcore_buffer *kb)
{
	lnet_md_t umd;
	int rc;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(nlx_core_buffer_invariant(cb));
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(cb->cb_qtype == C2_NET_QT_MSG_SEND);
	C2_PRE(cb->cb_length > 0);
	C2_PRE(cb->cb_max_operations == 1);

	nlx_kcore_umd_init(ctm, ktm, cb, kb, 1, 0, 0, false, &umd);
	nlx_kcore_kiov_adjust_length(ctm, kb, &umd, cb->cb_length);
	cb->cb_match_bits =
		nlx_core_match_bits_encode(cb->cb_addr.cepa_tmid, 0);
	rc = NLX_kcore_LNetPut(ctm, ktm, cb, kb, &umd);
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(ktm->ktm_addb, rc);
	nlx_kcore_kiov_restore_length(ctm, kb);
	return rc;
}

int nlx_core_buf_msg_send(struct nlx_core_domain *cd, /* not used */
			  struct nlx_core_transfer_mc *ctm,
			  struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	return nlx_kcore_buf_msg_send(ctm, ktm, cb, kb);
}

/**
   Performs kernel core tasks relating to adding a buffer to
   the bulk active receive queue.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
static int nlx_kcore_buf_active_recv(struct nlx_core_transfer_mc *ctm,
				     struct nlx_kcore_transfer_mc *ktm,
				     struct nlx_core_buffer *cb,
				     struct nlx_kcore_buffer *kb)
{
	uint32_t tmid;
	uint64_t counter;
	lnet_md_t umd;
	int rc;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(nlx_core_buffer_invariant(cb));
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(cb->cb_qtype == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_PRE(cb->cb_length > 0);
	C2_PRE(cb->cb_max_operations == 1);

	C2_PRE(cb->cb_match_bits > 0);
	nlx_core_match_bits_decode(cb->cb_match_bits, &tmid, &counter);
	C2_PRE(tmid == cb->cb_addr.cepa_tmid);
	C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
	C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);

	nlx_kcore_umd_init(ctm, ktm, cb, kb, 1, 0, 0, true, &umd);
	nlx_kcore_kiov_adjust_length(ctm, kb, &umd, cb->cb_length);
	rc = NLX_kcore_LNetGet(ctm, ktm, cb, kb, &umd);
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(ktm->ktm_addb, rc);
	nlx_kcore_kiov_restore_length(ctm, kb);
	return rc;
}

int nlx_core_buf_active_recv(struct nlx_core_domain *cd, /* not used */
			     struct nlx_core_transfer_mc *ctm,
			     struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	return nlx_kcore_buf_active_recv(ctm, ktm, cb, kb);
}

/**
   Performs kernel core tasks relating to adding a buffer to
   the bulk active send queue.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
static int nlx_kcore_buf_active_send(struct nlx_core_transfer_mc *ctm,
				     struct nlx_kcore_transfer_mc *ktm,
				     struct nlx_core_buffer *cb,
				     struct nlx_kcore_buffer *kb)
{
	uint32_t tmid;
	uint64_t counter;
	lnet_md_t umd;
	int rc;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(nlx_core_buffer_invariant(cb));
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(cb->cb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_PRE(cb->cb_length > 0);
	C2_PRE(cb->cb_max_operations == 1);

	C2_PRE(cb->cb_match_bits > 0);
	nlx_core_match_bits_decode(cb->cb_match_bits, &tmid, &counter);
	C2_PRE(tmid == cb->cb_addr.cepa_tmid);
	C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
	C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);

	nlx_kcore_umd_init(ctm, ktm, cb, kb, 1, 0, 0, false, &umd);
	nlx_kcore_kiov_adjust_length(ctm, kb, &umd, cb->cb_length);
	rc = NLX_kcore_LNetPut(ctm, ktm, cb, kb, &umd);
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(ktm->ktm_addb, rc);
	nlx_kcore_kiov_restore_length(ctm, kb);
	return rc;
}

int nlx_core_buf_active_send(struct nlx_core_domain *cd, /* not used */
			     struct nlx_core_transfer_mc *ctm,
			     struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	return nlx_kcore_buf_active_send(ctm, ktm, cb, kb);
}

/**
   Performs kernel core tasks relating to adding a buffer to
   the bulk passive receive queue.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
static int nlx_kcore_buf_passive_recv(struct nlx_core_transfer_mc *ctm,
				      struct nlx_kcore_transfer_mc *ktm,
				      struct nlx_core_buffer *cb,
				      struct nlx_kcore_buffer *kb)
{
	uint32_t tmid;
	uint64_t counter;
	lnet_md_t umd;
	int rc;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(nlx_core_buffer_invariant(cb));
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(cb->cb_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_PRE(cb->cb_length > 0);
	C2_PRE(cb->cb_max_operations == 1);

	nlx_core_match_bits_decode(cb->cb_match_bits, &tmid, &counter);
	C2_PRE(tmid == ktm->ktm_addr.cepa_tmid);
	C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
	C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);

	nlx_kcore_umd_init(ctm, ktm, cb, kb, 1, 0, LNET_MD_OP_PUT, false, &umd);
	cb->cb_addr = ktm->ktm_addr;
	rc = NLX_kcore_LNetMDAttach(ctm, ktm, cb, kb, &umd);
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(ktm->ktm_addb, rc);
	return rc;
}

int nlx_core_buf_passive_recv(struct nlx_core_domain *cd, /* not used */
			      struct nlx_core_transfer_mc *ctm,
			      struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	return nlx_kcore_buf_passive_recv(ctm, ktm, cb, kb);
}

/**
   Performs kernel core tasks relating to adding a buffer to
   the bulk passive send queue.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param cb The buffer private data.
   @param kb The kernel buffer private data.
 */
static int nlx_kcore_buf_passive_send(struct nlx_core_transfer_mc *ctm,
				      struct nlx_kcore_transfer_mc *ktm,
				      struct nlx_core_buffer *cb,
				      struct nlx_kcore_buffer *kb)
{
	uint32_t tmid;
	uint64_t counter;
	lnet_md_t umd;
	int rc;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(nlx_core_buffer_invariant(cb));
	C2_PRE(nlx_kcore_buffer_invariant(kb));
	C2_PRE(cb->cb_qtype == C2_NET_QT_PASSIVE_BULK_SEND);
	C2_PRE(cb->cb_length > 0);
	C2_PRE(cb->cb_max_operations == 1);

	nlx_core_match_bits_decode(cb->cb_match_bits, &tmid, &counter);
	C2_PRE(tmid == ktm->ktm_addr.cepa_tmid);
	C2_PRE(counter >= C2_NET_LNET_BUFFER_ID_MIN);
	C2_PRE(counter <= C2_NET_LNET_BUFFER_ID_MAX);

	nlx_kcore_umd_init(ctm, ktm, cb, kb, 1, 0, LNET_MD_OP_GET, false, &umd);
	nlx_kcore_kiov_adjust_length(ctm, kb, &umd, cb->cb_length);
	cb->cb_addr = ktm->ktm_addr;
	rc = NLX_kcore_LNetMDAttach(ctm, ktm, cb, kb, &umd);
	if (rc != 0)
		LNET_ADDB_FUNCFAIL_ADD(ktm->ktm_addb, rc);
	nlx_kcore_kiov_restore_length(ctm, kb);
	return rc;
}

int nlx_core_buf_passive_send(struct nlx_core_domain *cd, /* not used */
			      struct nlx_core_transfer_mc *ctm,
			      struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;
	return nlx_kcore_buf_passive_send(ctm, ktm, cb, kb);
}

int nlx_core_buf_del(struct nlx_core_domain *cd, /* not used */
		     struct nlx_core_transfer_mc *ctm,
		     struct nlx_core_buffer *cb)
{
	struct nlx_kcore_transfer_mc *ktm;
	struct nlx_kcore_buffer *kb;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	C2_PRE(nlx_core_buffer_invariant(cb));
	kb = cb->cb_kpvt;

	/* Subtle: Cancelling the MD associated with the buffer
	   could result in a LNet UNLINK event if the buffer operation is
	   terminated by LNet.
	   The unlink bit is also set in other LNet events but does not
	   signify cancel in those cases.
	 */
	return nlx_kcore_LNetMDUnlink(ctm, ktm, kb);
}

/**
   Performs common kernel core tasks to wait for buffer events.
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
   @param timeout Absolute time at which to stop waiting.
 */
static int nlx_kcore_buf_event_wait(struct nlx_core_transfer_mc *ctm,
				    struct nlx_kcore_transfer_mc *ktm,
				    c2_time_t timeout)
{
	bool any;

	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));

	do {
		any = c2_semaphore_timeddown(&ktm->ktm_sem, timeout);
		if (!any)
			break;
		while (c2_semaphore_trydown(&ktm->ktm_sem))
			; /* exhaust the semaphore */
	} while (bev_cqueue_is_empty(&ctm->ctm_bevq)); /* loop if empty */

	return any ? 0 : -ETIMEDOUT;
}

int nlx_core_buf_event_wait(struct nlx_core_transfer_mc *ctm, c2_time_t timeout)
{
	struct nlx_kcore_transfer_mc *ktm;

	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	return nlx_kcore_buf_event_wait(ctm, ktm, timeout);
}

/**
   Decodes a NID string into a NID.
   @param nidstr the string to be decoded.
   @param nid On success, the resulting NID is returned here.
   @retval -EINVAL the NID string could not be decoded
 */
static int nlx_kcore_nidstr_decode(const char *nidstr, uint64_t *nid)
{
	*nid = libcfs_str2nid(nidstr);
	if (*nid == LNET_NID_ANY)
		return -EINVAL;
	return 0;
}

int nlx_core_nidstr_decode(struct nlx_core_domain *lcdom, /* not used */
			   const char *nidstr, uint64_t *nid)
{
	return nlx_kcore_nidstr_decode(nidstr, nid);
}

/**
   Encode a NID into its string representation.
   @param nid The NID to be converted.
   @param nidstr On success, the string form is set here.
 */
static int nlx_kcore_nidstr_encode(uint64_t nid,
				   char nidstr[C2_NET_LNET_NIDSTR_SIZE])
{
	const char *cp = libcfs_nid2str(nid);

	C2_ASSERT(cp != NULL && *cp != 0);
	strncpy(nidstr, cp, C2_NET_LNET_NIDSTR_SIZE - 1);
	nidstr[C2_NET_LNET_NIDSTR_SIZE - 1] = 0;
	return 0;
}

int nlx_core_nidstr_encode(struct nlx_core_domain *lcdom, /* not used */
			   uint64_t nid, char nidstr[C2_NET_LNET_NIDSTR_SIZE])
{
	return nlx_kcore_nidstr_encode(nid, nidstr);
}

/**
   Gets a list of strings corresponding to the local LNET network interfaces.
   The returned array must be released using nlx_core_nidstrs_put().
   @param nidary A NULL-terminated (like argv) array of NID strings is returned.
 */
static int nlx_kcore_nidstrs_get(char * const **nidary)
{
	C2_PRE(nlx_kcore_lni_nidstrs != NULL);
	*nidary = nlx_kcore_lni_nidstrs;
	c2_atomic64_inc(&nlx_kcore_lni_refcount);
	return 0;
}

int nlx_core_nidstrs_get(struct nlx_core_domain *lcdom, char * const **nidary)
{
	return nlx_kcore_nidstrs_get(nidary);
}

/**
   Kernel helper to release the strings returned by nlx_kcore_nidstrs_get().
 */
void nlx_kcore_nidstrs_put(char * const **nidary)
{
	C2_PRE(*nidary == nlx_kcore_lni_nidstrs);
	C2_PRE(c2_atomic64_get(&nlx_kcore_lni_refcount) > 0);
	c2_atomic64_dec(&nlx_kcore_lni_refcount);
	*nidary = NULL;
}

void nlx_core_nidstrs_put(struct nlx_core_domain *lcdom, char * const **nidary)
{
	nlx_kcore_nidstrs_put(nidary);
}

int nlx_core_new_blessed_bev(struct nlx_core_transfer_mc *ctm, /* not used */
			     struct nlx_core_buffer_event **bevp)
{
	struct nlx_core_buffer_event *bev;

	NLX_ALLOC_PTR_ADDB(bev, &c2_net_addb, &nlx_addb_loc);
	if (bev == NULL) {
		*bevp = NULL;
		return -ENOMEM;
	}
	bev_link_bless(&bev->cbe_tm_link, virt_to_page(&bev->cbe_tm_link));
	*bevp = bev;
	return 0;
}

/**
   Performs kernel core tasks relating to stopping a transfer machine.
   Kernel resources are released.
   @param kd kernel domain for this transfer machine
   @param ctm The transfer machine private data.
   @param ktm The kernel transfer machine private data.
 */
static void nlx_kcore_tm_stop(struct nlx_kcore_domain *kd,
			      struct nlx_core_transfer_mc *ctm,
			      struct nlx_kcore_transfer_mc *ktm)
{
	int rc;

	C2_PRE(nlx_kcore_domain_invariant(kd));
	C2_PRE(nlx_core_tm_invariant(ctm));
	C2_PRE(nlx_kcore_tm_invariant(ktm));
	C2_PRE(drv_bevs_tlist_is_empty(&ktm->ktm_drv_bevs));

	rc = LNetEQFree(ktm->ktm_eqh);
	C2_ASSERT(rc == 0);
	c2_semaphore_fini(&ktm->ktm_sem);

	c2_mutex_lock(&nlx_kcore_mutex);
	tms_tlist_del(ktm);
	c2_mutex_unlock(&nlx_kcore_mutex);
	c2_addb_ctx_fini(&ktm->ktm_addb);
	ktm->ktm_magic = 0;
	ctm->ctm_kpvt = NULL;
}

void nlx_core_tm_stop(struct nlx_core_domain *cd,
		      struct nlx_core_transfer_mc *ctm)
{
	struct nlx_kcore_domain *kd;
	struct nlx_kcore_transfer_mc *ktm;

	C2_PRE(cd != NULL);
	kd = cd->cd_kpvt;
	C2_PRE(nlx_kcore_domain_invariant(kd));
	C2_PRE(nlx_core_tm_invariant(ctm));
	ktm = ctm->ctm_kpvt;
	nlx_kcore_tm_stop(kd, ctm, ktm);
	bev_cqueue_fini(&ctm->ctm_bevq, nlx_core_bev_free_cb);
	c2_free(ktm);
}

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
static int nlx_kcore_tm_start(struct nlx_kcore_domain *kd,
			      struct nlx_core_transfer_mc *ctm,
			      struct nlx_kcore_transfer_mc *ktm)
{
	struct nlx_core_ep_addr *cepa;
	lnet_process_id_t id;
	int rc;
	int i;

	C2_PRE(kd != NULL && ctm != NULL && ktm != NULL);
	cepa = &ctm->ctm_addr;

	/*
	 * cepa_nid/cepa_pid must match a local NID/PID.
	 * cepa_portal must be in range.  cepa_tmid is checked below.
	 */
	if (cepa->cepa_portal == LNET_RESERVED_PORTAL ||
	    cepa->cepa_portal >= C2_NET_LNET_MAX_PORTALS) {
		rc = -EINVAL;
		goto fail;
	}
	for (i = 0; i < nlx_kcore_lni_nr; ++i) {
		rc = LNetGetId(i, &id);
		C2_ASSERT(rc == 0);
		if (id.nid == cepa->cepa_nid && id.pid == cepa->cepa_pid)
			break;
	}
	if (i == nlx_kcore_lni_nr) {
		rc = -ENOENT;
		goto fail;
	}

	tms_tlink_init(ktm);
	drv_tms_tlink_init(ktm);
	drv_bevs_tlist_init(&ktm->ktm_drv_bevs);
	ctm->ctm_mb_counter = C2_NET_LNET_BUFFER_ID_MIN;
	spin_lock_init(&ktm->ktm_bevq_lock);
	c2_semaphore_init(&ktm->ktm_sem, 0);
	rc = LNetEQAlloc(C2_NET_LNET_EQ_SIZE, nlx_kcore_eq_cb, &ktm->ktm_eqh);
	if (rc < 0)
		goto fail;

	c2_mutex_lock(&nlx_kcore_mutex);
	if (cepa->cepa_tmid == C2_NET_LNET_TMID_INVALID) {
		rc = nlx_kcore_max_tmid_find(cepa);
		if (rc < 0) {
			c2_mutex_unlock(&nlx_kcore_mutex);
			goto fail_with_eq;
		}
		cepa->cepa_tmid = rc;
	} else if (cepa->cepa_tmid > C2_NET_LNET_TMID_MAX) {
		c2_mutex_unlock(&nlx_kcore_mutex);
		rc = -EINVAL;
		goto fail_with_eq;
	} else if (nlx_kcore_addr_in_use(cepa)) {
		c2_mutex_unlock(&nlx_kcore_mutex);
		rc = -EADDRINUSE;
		goto fail_with_eq;
	}
	ktm->ktm_addr = *cepa;
	nlx_kcore_tms_list_add(ktm);
	c2_mutex_unlock(&nlx_kcore_mutex);

	c2_addb_ctx_init(&ktm->ktm_addb, &nlx_core_tm_addb_ctx, &kd->kd_addb);
	ctm->ctm_kpvt = ktm;
	ctm->ctm_magic = C2_NET_LNET_CORE_TM_MAGIC;
	C2_POST(nlx_kcore_tm_invariant(ktm));
	return 0;

fail_with_eq:
	i = LNetEQFree(ktm->ktm_eqh);
	C2_ASSERT(i == 0);
fail:
	C2_ASSERT(rc != 0);
	LNET_ADDB_FUNCFAIL_ADD(kd->kd_addb, rc);
	return rc;
}

int nlx_core_tm_start(struct nlx_core_domain *cd,
		      struct c2_net_transfer_mc *tm,
		      struct nlx_core_transfer_mc *ctm)
{
	struct nlx_kcore_domain *kd;
	struct nlx_core_buffer_event *e1;
	struct nlx_core_buffer_event *e2;
	struct nlx_kcore_transfer_mc *ktm;
	int rc;

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(nlx_tm_invariant(tm));
	C2_PRE(cd != NULL);
	kd = cd->cd_kpvt;
	C2_PRE(nlx_kcore_domain_invariant(kd));

	C2_ALLOC_PTR_ADDB(ktm, &tm->ntm_addb, &nlx_addb_loc);
	if (ktm == NULL) {
		rc = -ENOMEM;
		goto fail_ktm;
	}

	nlx_core_kmem_loc_set(&ktm->ktm_ctm_loc, virt_to_page(ctm),
			      PAGE_OFFSET((unsigned long) ctm));
	rc = nlx_kcore_tm_start(kd, ctm, ktm);
	if (rc != 0)
		goto fail_ktm;

	ctm->ctm_upvt = NULL;
	ctm->ctm_user_space_xo = false;
	nlx_core_new_blessed_bev(ctm, &e1);
	nlx_core_new_blessed_bev(ctm, &e2);
	if (e1 == NULL || e2 == NULL) {
		rc = -ENOMEM;
		goto fail;
	}

	bev_cqueue_init(&ctm->ctm_bevq, &e1->cbe_tm_link, &e2->cbe_tm_link);
	C2_ASSERT(bev_cqueue_is_empty(&ctm->ctm_bevq));
	return 0;

fail:
	nlx_kcore_tm_stop(kd, ctm, ktm);
fail_ktm:
	c2_free(ktm);
	C2_ASSERT(rc != 0);
	LNET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	return rc;
}

static void nlx_core_fini(void)
{
	int rc;
	int i;

	C2_ASSERT(c2_atomic64_get(&nlx_kcore_lni_refcount) == 0);
	nlx_dev_fini();
	if (nlx_kcore_lni_nidstrs != NULL) {
		for (i = 0; nlx_kcore_lni_nidstrs[i] != NULL; ++i)
			c2_free(nlx_kcore_lni_nidstrs[i]);
		c2_free(nlx_kcore_lni_nidstrs);
		nlx_kcore_lni_nidstrs = NULL;
	}
	nlx_kcore_lni_nr = 0;
	tms_tlist_fini(&nlx_kcore_tms);
	c2_mutex_fini(&nlx_kcore_mutex);
	rc = LNetNIFini();
	C2_ASSERT(rc == 0);
}

static int nlx_core_init(void)
{
	int rc;
	int i;
	lnet_process_id_t id;
	const char *nidstr;

	/* Init LNet with same PID as Lustre would use in case we are first. */
	rc = LNetNIInit(LUSTRE_SRV_LNET_PID);
	C2_ASSERT(rc >= 0);
	c2_mutex_init(&nlx_kcore_mutex);
	tms_tlist_init(&nlx_kcore_tms);

	c2_atomic64_set(&nlx_kcore_lni_refcount, 0);
	for (i = 0, rc = 0; rc != -ENOENT; ++i)
		rc = LNetGetId(i, &id);
	C2_ALLOC_ARR_ADDB(nlx_kcore_lni_nidstrs, i,
			  &c2_net_addb, &nlx_addb_loc);
	if (nlx_kcore_lni_nidstrs == NULL) {
		nlx_core_fini();
		return -ENOMEM;
	}
	nlx_kcore_lni_nr = i - 1;
	for (i = 0; i < nlx_kcore_lni_nr; ++i) {
		rc = LNetGetId(i, &id);
		C2_ASSERT(rc == 0);
		nidstr = libcfs_nid2str(id.nid);
		C2_ASSERT(nidstr != NULL);
		nlx_kcore_lni_nidstrs[i] = c2_alloc(strlen(nidstr) + 1);
		if (nlx_kcore_lni_nidstrs[i] == NULL) {
			C2_ADDB_ADD(&c2_net_addb, &nlx_addb_loc, c2_addb_oom);
			nlx_core_fini();
			return -ENOMEM;
		}
		strcpy(nlx_kcore_lni_nidstrs[i], nidstr);
	}

	rc = nlx_dev_init();
	if (rc != 0)
		nlx_core_fini();

	return rc;
}

/** @todo make static */
struct nlx_kcore_ops nlx_kcore_def_ops = {
	.ko_dom_init = nlx_kcore_core_dom_init,
	.ko_dom_fini = nlx_kcore_core_dom_fini,
	.ko_buf_register = nlx_kcore_buf_register,
	.ko_buf_deregister = nlx_kcore_buf_deregister,
	.ko_tm_start = nlx_kcore_tm_start,
	.ko_tm_stop = nlx_kcore_tm_stop,
	.ko_buf_msg_recv = nlx_kcore_buf_msg_recv,
	.ko_buf_msg_send = nlx_kcore_buf_msg_send,
	.ko_buf_active_recv = nlx_kcore_buf_active_recv,
	.ko_buf_active_send = nlx_kcore_buf_active_send,
	.ko_buf_passive_recv = nlx_kcore_buf_passive_recv,
	.ko_buf_passive_send = nlx_kcore_buf_passive_send,
	.ko_buf_del = nlx_kcore_LNetMDUnlink,
	.ko_buf_event_wait = nlx_kcore_buf_event_wait,
};

/**
   Initializes the kernel core domain private data object.
   @param kd kernel core private data pointer for the domain to be initialized.
 */
static int nlx_kcore_kcore_dom_init(struct nlx_kcore_domain *kd)
{
	C2_PRE(kd != NULL);
	kd->kd_magic = C2_NET_LNET_KCORE_DOM_MAGIC;
	nlx_core_kmem_loc_set(&kd->kd_cd_loc, NULL, 0);
	c2_mutex_init(&kd->kd_drv_mutex);
	kd->kd_drv_ops = &nlx_kcore_def_ops;
	drv_tms_tlist_init(&kd->kd_drv_tms);
	drv_bufs_tlist_init(&kd->kd_drv_bufs);
	c2_addb_ctx_init(&kd->kd_addb, &nlx_core_domain_addb_ctx, &c2_net_addb);
	C2_POST(nlx_kcore_domain_invariant(kd));
	return 0;
}

/**
   Finalizes the kernel core domain private data object.
   @param kd kernel core private data pointer for the domain to be finalized.
 */
static void nlx_kcore_kcore_dom_fini(struct nlx_kcore_domain *kd)
{
	C2_PRE(nlx_kcore_domain_invariant(kd));
	C2_PRE(nlx_core_kmem_loc_is_empty(&kd->kd_cd_loc));

	c2_addb_ctx_fini(&kd->kd_addb);
	drv_bufs_tlist_fini(&kd->kd_drv_bufs);
	drv_tms_tlist_fini(&kd->kd_drv_tms);
	kd->kd_drv_ops = NULL;
	c2_mutex_fini(&kd->kd_drv_mutex);
}

/** @} */ /* KLNetCore */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
