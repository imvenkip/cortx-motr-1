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
   - @ref cqueueDLD "Circular Queue for Single Producer and Consumer DLD"

   <hr>
   @section KLNetCoreDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - Each transfer machine uses one LNet event queue for all LNet operations.

   - The number of messages that can be delivered into a single receive buffer
   is bounded to support pre-allocation of memory to hold the event payload to
   support asynchronous delivery and consumption.

   - Event delivery is decoupled from the LNet callback.

   - Support for user space transports.

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

   The relationship between the various components of the LNet transport and
   the networking layer is illustrated in the following UML diagram.
   @image html "../../net/lnet/lnet_xo.png" "LNet Transport Objects"

   The Core layer in the kernel has no sub-components but interfaces directly
   with the Lustre LNet module in the kernel.

   @subsection KLNetCoreDLD-lspec-userspace Support for User Space Transports

   The kernel Core layer is designed to support user space transports with the
   use of shared memory.  It does not directly provide a mechanism to
   communicate with the user space transport, but the Core data structures are
   organized with a distinction between the common directly shareable portions,
   and private areas for kernel and user space data.

   @subsection KLNetCoreDLD-lspec-match-bits Match Bits for Buffer Identification

   The kernel Core module will maintain a unsigned integer counter per transfer
   machine, to generate unique match bits for passive bulk buffers associated
   with that transfer machine.  The upper 12 match bits are reserved by the HLD
   to represent the transfer machine identifier. Therefore the counter is
   (64-12)=52 bits wide. The value of 0 is reserved for unsolicited
   receive messages, so the counter range is [1,0xffffffffffff]. It is
   initialized to 1 and will wrap back to 1 when it reaches its upper bound.

   The transport should use the c2_lnet_core_tm_match_bits_set() subroutine to
   obtain new match bits for passive buffers in the context of the
   c2_net_buffer_add() subroutine itself.  The match bits should be encoded
   into the network buffer descriptor associated with the passive buffer.  It
   is the transport's responsibility to ensure that all of the passive buffers
   associated with a given transfer machine have unique match bits. The match
   bit counter will repeat over time, though after a very long while.

   @subsection KLNetCoreDLD-lspec-tm-list Transfer Machine Uniqueness

   The kernel Core module must ensure that all transfer machines have unique
   addresses, regardless of the transport instance or network domain in which
   they originate.  To support this, the ::klnc_tms list threads through all the
   kernel Core's per-TM private data structures. This list is private to the
   kernel Core, and is protected by the ::klnc_mutex.

   The same list helps in assigning dynamic transfer machine identifiers.  The
   highest available value at the upper bound of the transfer machine
   identifier space is assigned dynamically.  The logic takes into account the
   NID, PID and portal number of the new transfer machine when looking for an
   available transfer machine identifier.  A single pass over the list is
   required to search for an available transfer machine identifier.

   @subsection KLNetCoreDLD-lspec-lnet-init LNet Initialization and Finalization

   No initialization and finalization logic is required for LNet in the kernel
   for the following reasons:

   - Use of the LNet kernel module is reference counted by the kernel.
   - The LNetInit() subroutine is automatically called by the LNet kernel module,    and cannot be called multiple times.

   @todo Check if this is always done or only done if Lustre is used.

   @subsection KLNetCoreDLD-lspec-reg LNet Buffer Registration

   No hardware optimization support is defined in the LNet API at this time.

   @subsection KLNetCoreDLD-lspec-ev LNet Event Processing

   LNet event queues are used with an event callback subroutine to avoid event
   loss.  The callback subroutine overhead is fairly minimal, as it only copies
   out the event payload and arranges for subsequent asynchronous delivery.
   This, coupled with the fact that the circular buffer used works optimally
   with a single producer and single consumer resulted in the decision to use
   just one LNet EQ per transfer machine (c2_klnet_core_transfer_mc::klctm_eqh).

   The event callback requires that the MD @c user_ptr field be set up to point
   to the c2_lnet_core_buffer data structure.  The callback does the following:

   -# @c LNET_EVENT_SEND and @c LNET_EVENT_ACK events are ignored.
   -# If the event is @c LNET_EVENT_PUT and the buffer is a receive message
      buffer, and multiple messages are permitted in a single buffer, then
      determine the next available event payload structure for the receive
      buffer from the c2_lnet_core_buffer::lcb_ev_cq circular queue.  (Note:
      the circular buffer data structure is used here because of its implicit
      serialization for a single consumer and producer, but the circularity is
      otherwise not important).  The queue indicies de-reference the
      c2_lnet_core_buffer::lcb_ev array.
   -# Otherwise, for all other cases and event types, the event payload
      structure used is c2_lnet_core_buffer::lcb_ev[0].
   -# Copy the event payload from the LNet event to the event payload structure
      in the buffer.  This includes the value of the @c unlinked field of the
      event, which must be copied to the c2_lnet_core_buffer_event::lcbe_unlinked
      field.
      For @c LNET_EVENT_UNLINK events, a @c -ECANCELLED value is written to the
      c2_lnet_core_buffer_event::lcbe_status field and
      the c2_lnet_core_buffer_event::lcbe_unlinked field set to true.
   -# The buffer identifier, c2_lnet_core_buffer::lcb_nbid, is put into the
      next event slot of the circular event queue anchored in the core transfer
      machine structure, returned by the c2_cqueue_pnext() subroutine.  This
      buffer identifier is the address of the buffer in the address space of
      the transport (it could be a user space address).  (Note that as there is
      only one LNet EQ used per transfer machine, no serialization is required
      to do this operation.  If multiple EQs are ever used, then this step
      needs to be serialized.)
   -# The circular queue c2_cqueue_produce() subroutine is invoked.
   -# The c2_klnet_core_transfer_mc::klctm_sem count is incremented with the
      c2_semaphore_up() subroutine to notify any waiters.

   The transport layer event handler thread blocks on the core transfer machine
   event semaphore through the c2_lnet_core_buf_event_wait() subroutine.  This
   call is aware of the position of the last event consumed in the transfer
   machine circular buffer; it loops on a c2_semaphore_down() call on the
   c2_klnet_core_transfer_mc::klctm_sem as long as the event queue is empty.

   When the subroutine returns with an indication of the presence of events,
   the thread consumes all the pending events with multiple calls to the
   c2_lnet_core_buf_event_get() subroutine, which invokes the
   c2_cqueue_consume() subroutine to get the index of the next queue slot to
   read.  This must be repeated until the event queue is drained.

   @subsection KLNetCoreDLD-lspec-recv LNet Receiving Unsolicited Messages

   -# An EQ is created for each transfer machine by the c2_lnet_core_tm_start()
   subroutine, and the common callback handler registered.
   -# For each receive buffer, create an ME with @c LNetMEAlloc() and specify
      the portal, match and ignore bits. All receive buffers for a given TM will
      use a match bit value equal to the TM identifier in the higher order
      bits and zeros for the other bits.  No ignore bits are set.
      Save the ME handle in the c2_klnet_core_buffer::klcb_meh field.
   -# Create and attach an MD to the ME using @c LNetMDAttach().
      Save the MD handle in the c2_klnet_core_buffer::klcb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (c2_klnet_core_transfer_mc::klctm_eqh).
      - Set the kernel logical address of the c2_klnet_core_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the c2_klnet_core_buffer::klcb_kiov.
      - Set the @c threshold value to the maximum number of messages that
        are to be received in the buffer.
      - Set the @c LNET_MD_OP_PUT and @c LNET_MD_KIOV flags in the
        @c options field.
        @todo What about @c LNET_MD_MAX_SIZE?
   -# When a message arrives, an @c LNET_EVENT_PUT event will be delivered to
      the event queue, and will be processed as described in
      @ref KLNetCoreDLD-lspec-ev.

   @subsection KLNetCoreDLD-lspec-send LNet Sending Messages

   -# Create an MD using @c LNetMDBind().
      Save the MD handle in the c2_klnet_core_buffer::klcb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (c2_klnet_core_transfer_mc::klctm_eqh).
      - Set the kernel logical address of the c2_klnet_core_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the c2_klnet_core_buffer::klcb_kiov.
   -# Use the @c LNetPut() subroutine to send the MD to the destination.
      The match bits must set to the destination TM identifier in the higher
      order bits and zeros for the other bits.
   -# When the message is sent, an @c LNET_EVENT_SEND event will be delivered
      to the event queue, and processed as described in
      @ref KLNetCoreDLD-lspec-ev.

   @subsection KLNetCoreDLD-lspec-passive LNet Staging Passive Bulk Buffers

   -# Prior to invoking the c2_lnet_core_buf_passive_recv() or the
      c2_lnet_core_buf_passive_send() subroutines, the transport should use the
      c2_lnet_core_tm_match_bit_set() subroutine to assign unique match bits to
      the passive buffer. See @ref KLNETCoreDLD-lspec-match-bits for details.
      The match bits should be encoded into the network buffer descriptor and
      independently conveyed to the remote active transport.
   -# Create an ME using @c LNetMEAlloc(). Specify the portal and match_id fields
      as appropriate for the transfer machine.  The buffer's match bits are
      obtained from the c2_lnet_core_buffer::lcb_match_bits field.  No ignore
      bits are set. The ME should be set up to unlink automatically.
   -# Create and attach an MD to the ME using @c LNetMDAttach().
      Save the MD handle in the c2_klnet_core_buffer::klcb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (c2_klnet_core_transfer_mc::klctm_eqh).
      - Set the kernel logical address of the c2_klnet_core_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the c2_klnet_core_buffer::klcb_kiov.
      - Set the @c LNET_MD_OP_PUT or the @c LNET_MD_OP_GET flag in the
        @c options field according to the direction of data transfer.
   -# When the bulk data transfer completes, either an @c LNET_EVENT_PUT or an
      @c LNET_EVENT_GET event will be delivered to the event queue, and will be
      processed as described in @ref KLNetCoreDLD-lspec-ev.

   @subsection KLNetCoreDLD-lspec-active LNet Active Bulk Read or Write

   -# Prior to invoking the c2_lnet_core_buf_active_recv() or
   c2_lnet_core_buf_active_send() subroutines, the
   transport should put the match bits of the remote passive buffer into the
   c2_lnet_core_buffer::lcb_match_bits field. The destination address of the
   remote transfer machine with the passive buffer should be set in the
   c2_lnet_core_buffer::lcb_passive_addr field.
   -# Create an MD using @c LNetMDBind().
      Save the MD handle in the c2_klnet_core_buffer::klcb_mdh field.
      Set up the fields of the @c lnet_md_t argument as follows:
      - Set the @c eq_handle to identify the EQ associated with the transfer
        machine (c2_klnet_core_transfer_mc::klctm_eqh).
      - Set the kernel logical address of the c2_klnet_core_buffer in the
        @c user_ptr field.
      - Pass in the KIOV from the c2_klnet_core_buffer::klcb_kiov.
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
   initiate a cancel operation using the c2_lnet_core_buf_del() subroutine.

   This will result in an @c LNetMDUnlink() subroutine call being issued for
   the buffer MD saved in the c2_klnet_core_buffer::klcb_mdh field.
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

   The kernel Core layer, for the most part, relies on the upper transport
   layer to maintain the linkage between the data structures used by the core
   layer. The only exception is the link, c2_klnet_core_buffer::klcb_tm, from
   the kernel private buffer data to the transfer machine shared data, which is
   maintained for efficiency during event processing.  The kernel Core layer
   maintains no lists of data structures or ongoing operations.

   The kernel Core layer module depends on the LNet module in the kernel at
   runtime. This dependency is captured by the kernel's module support that
   reference counts the usage of dependent modules.

   @subsection KLNetCoreDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   Only one instance of the API can be opened per network domain.  All the API
   calls are synchronous, except for the buffer operation calls which initiate
   asynchronous activity.  Generally speaking, API calls within the transport
   address space itself, are protected by the serialization of the Colibri
   Networking layer.  The c2_lnet_core_tm_match_bits_set() subroutine, for
   example, is fully protected by the transfer machine mutex held across the
   c2_net_buffer_add() subroutine call, so implicitly protects the match bit
   counter in the kernel Core's per TM private data.

   However, this serialization does not always suffice, as the kernel Core
   module has to support concurrent multiple transport instances in kernel and
   user space.  Fortunately, the LNet API intrinsically provides considerable
   serialization support to the Core, as each transfer machine is set up by the
   HLD to be a disjoint LNet consumer.  The boundary condition of enforcing
   this "disjointedness" of transfer machines is protected by the Core's static
   global ::klnc_mutex.  The c2_lnet_core_tm_start() and c2_lnet_core_tm_stop()
   subroutines use this mutex internally for serialization and operation on the
   ::klnc_tms list threaded through the kernel Core's per-TM private data.

   The API does not define any callback to indicate completion of an
   asynchronous buffer operation.  Instead, the transport application must
   invoke the c2_lnet_core_buf_event_wait() subroutine to block waiting for
   buffer events.  Internally this call waits on the
   c2_klnet_core_transfer_mc::klctm_sem semaphore.

   The event payload is actually delivered via a per transfer machine
   circular buffer event queue, and multiple events may be delivered between
   each call to this subroutine.  Each such event is fetched by a call to the
   c2_lnet_core_buf_event_get() subroutine, until the queue is exhausted.

   The number of event payload structures is finite. The core API requires that
   the transport application not issue more operation requests than there are
   free c2_lnet_core_buffer_event structures in the transfer machine buffer
   event circular queue to return the operation result.  i.e. the transport is
   responsible for flow control.

   The API assumes that only a single transport thread will handle event
   processing; if this is not the case then the transport should serialize its
   use of these two subroutines.


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
   the presence of the event. (See @ref KLNetCoreDLD-lspec-ev above). The
   transport event processing thread can be constrained to have any desired
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

   - Dynamic transfer machine identifier assignment is proportional to the number
   of transfer machines defined on the server, including kernel and all process
   space LNet transport instances.
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
   - @ref cqueueDLD "Circular Queue for Single Producer and Consumer DLD"

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

/** Kernel core lock */
static c2_mutex klnc_mutex;

/** List of all transfer machines. Protected by klnc_mutex. */
static struct c2_tl klnc_tms;


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

