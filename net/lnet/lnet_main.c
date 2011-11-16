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
   @page LNetDLD LNet Transport DLD

   - @ref LNetDLD-ovw
   - @ref LNetDLD-def
   - @ref LNetDLD-req
   - @ref LNetDLD-depends
   - @ref LNetDLD-highlights
   - @subpage LNetDLD-fspec "Functional Specification" <!-- ext link -->
      - @ref LNetDFS "LNet Transport Interface"       <!-- ext link -->
      - @ref LNetXODFS "XO Interface"            <!-- int link -->
   - @ref LNetDLD-lspec
      - @ref LNetDLD-lspec-comps
      - @ref LNetDLD-lspec-ep
      - @ref LNetDLD-lspec-tm-stop
      - @ref LNetDLD-lspec-tm-thread
      - @ref LNetDLD-lspec-buf-nbd
      - @ref LNetDLD-lspec-buf-op
      - @ref LNetDLD-lspec-state
      - @ref LNetDLD-lspec-thread
      - @ref LNetDLD-lspec-numa
   - @ref LNetDLD-conformance
   - @ref LNetDLD-ut
   - @ref LNetDLD-st
   - @ref LNetDLD-O
   - @ref LNetDLD-ref

   <hr>
   @section LNetDLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   This document describes the Colibri Network transport for LNet. The
   transport is composed of multiple layers.  The document describes the
   layering and then focuses mainly on the transport operations layer.

   The design of the other layers can be found here:
   - @ref LNetcqueueDLD "LNet Buffer Event Circular Queue DLD"
   - @ref KLNetCoreDLD "LNet Transport Kernel Core DLD"
   - @ref ULNetCoreDLD "LNet Transport User Space Core DLD"

   <hr>
   @section LNetDLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:
   - Refer to <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>

   New terms:

   <hr>
   @section LNetDLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - <b>r.c2.net.xprt.lnet.transport-variable</b> The implementation
     shall name the transport variable as specified in the HLD.

   - <b>r.c2.net.lnet.buffer-registration</b> Provide support for
     hardware optimization through buffer pre-registration.

   - <b>r.c2.net.xprt.lnet.end-point-address</b> The implementation
     should support the mapping of end point address to LNet address
     as described in the Refinement section of the HLD.

   - <b>r.c2.net.xprt.lnet.multiple-messages-in-buffer</b> Provide
     support for this feature as described in the HLD.

   - <b>r.c2.net.xprt.lnet.dynamic-address-assignment</b> Provide
     support for dynamic address assignment as described in the HLD.

   - <b>r.c2.net.xprt.lnet.processor-affinity</b> The implementation
     must support processor affinity as described in the HLD.

   - <b>r.c2.net.xprt.lnet.user-space</b> The implementation must
     accommodate the needs of the user space LNet transport.

   <hr>
   @section LNetDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   <ul>

   <li>@ref LNetCore "LNet Transport Core Interface" </li>

   <li>The @ref net "Networking Module".  Some modifications are required:

     The design adds two additional fields to the c2_net_buffer structure:
     @code
     struct c2_net_buffer {
        ...
	c2_bcount_t   nb_min_receive_size;
	uint32_t      nb_max_receive_msgs;
     };
     @endcode
     These fields are required to be set to non-zero values in receive buffers,
     and control the reception of multiple messages into a single receive buffer.

     Additionally, the semantics of the @c nb_ep field is modified to not
     require the end point of the active transfer machine when enqueuing a
     passive buffer.  This effectively says that there will be no constraint on
     which transfer machine performs the active operation, and the application
     with the passive buffer is not required to know the address of this active
     transfer machine in advance. This enables the conveyance of the network
     buffer descriptor to the active transfer machine through intermediate
     proxies, and the use of load balancing algorithms to spread the I/O
     traffic across multiple servers.

     The c2_net_tm_confine() subroutine is added to set the processor
     affinity for transfer machine thread if desired.  This results in an
     additional operation being added to the @c c2_net_xo_ops structure:

     @code
     struct c2_net_xo_ops {
        ...
        int  (*xo_tm_confine)(struct c2_net_transfer_mc *tm,
	                      const struct c2_bitmap *processors);
     };
     @endcode

     The behavior of the c2_net_buffer_event_post() subroutine is modified
     slightly to allow for multiple buffer events to be delivered for a single
     receive buffer, without removing it from a transfer machine queue.
     This is indicated by the C2_NET_BUF_RETAIN flag.

   </li> <!-- end net module changes -->

   <li>The @ref bitmap "Bitmap Module".  New subroutines to copy a bitmap and
   to compare bitmaps are required. The copy subroutine should be refactored
   out of the processors_copy_c2bitmap() subroutine. </li>

   <li>The @ref Processor API for the application to determine processor
   bitmaps with which to specify thread affinity.</li>

   <li>The @ref thread "Thread Module".  Modifications are required in
   c2_thread_init() subroutine or a variant should be provided to support
   thread creation with processor affinity set.  This is essential for the
   kernel implementation where processor affinity can only be set during thread
   creation.</li>

   </ul>

   <hr>
   @section LNetDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - Common user and kernel space implementation over an underlying "Core" I/O
     layer.
   - Provides processor affinity


   <hr>
   @section LNetDLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref LNetDLD-lspec-comps
   - @ref LNetDLD-lspec-ep
   - @ref LNetDLD-lspec-tm-start
   - @ref LNetDLD-lspec-tm-stop
   - @ref LNetDLD-lspec-tm-thread
   - @ref LNetDLD-lspec-buf-nbd
   - @ref LNetDLD-lspec-buf-op
   - @ref LNetDLD-lspec-state
   - @ref LNetDLD-lspec-thread
   - @ref LNetDLD-lspec-numa

   @subsection LNetDLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The focus of the LNet transport is the implementation of the asynchronous
   semantics required by the Colibri Networking layer.  I/O is performed by an
   underlying "core" layer, which does the actual interaction with the Lustre
   LNet kernel module.  The core layer permits the LNet transport code to be
   written in an address space agnostic fashion, as it offers the same
   interface in both user and kernel space.

   The relationship between the various components of the LNet transport and
   the networking layer is illustrated in the following UML diagram.
   @image html "../../net/lnet/lnet_xo.png" "LNet Transport Objects"
   <!-- PNG image width is 800 -->

   @subsection LNetDLD-lspec-ep End Point Support

   The transport defines the following structure for the internal
   representation of a struct c2_net_end_point.
   @code
   struct nlx_xo_ep {
       struct c2_net_end_point xe_ep;
       nlx_core_ep_addr        xe_core;
       char                    xe_addr[1];
   };
   @endcode
   The length of the structure depends on the length of the string
   representation of the address, which must be saved in the @c xe_addr array.
   The address of the @c xe_ep field is returned as the external representation.

   The transport does not support dynamic addressing: i.e. the @c addr field
   can never be NULL in the c2_net_end_point_create() subroutine.  However, it
   supports the dynamic assignment of transfer machine identifiers as described
   in the HLD, but only for the @c addr parameter of the c2_net_tm_start()
   subroutine.

   A link list of all end point objects created for a transfer machine is
   maintained in the c2_net_transfer_mc::ntm_end_points list.  Objects are
   added to this list as a result of the application invoking the
   c2_net_end_point_create() subroutine, or as a side effect of receiving a
   message.  Access to this list is protected by the transfer machine mutex.


   @subsection LNetDLD-lspec-tm-start Transfer Machine Start

   The c2_net_tm_start() subroutine is used to start a transfer machine, which
   results in a call to nlx_xo_tm_start().  The subroutine decodes the end
   point address using the nlx_core_ep_addr_decode() subroutine. It then starts
   the background event thread with the desired processor affinity. The thread
   will complete the transfer machine start up and deliver its state change
   event.

   The event processing thread will call the nlx_core_tm_start() subroutine to
   create the internal LNet EQ associated with the transfer machine.  This call
   also validates the transfer machine's address, and assigns a dynamic
   transfer machine identifier if needed.  It will then post a state change
   callback to transition the transfer machine to its normal operational state,
   or fail it if any error is encountered.


   @subsection LNetDLD-lspec-tm-stop Transfer Machine Termination

   Termination of a transfer machine is requested through the c2_net_tm_stop()
   subroutine, which results in a call to nlx_xo_tm_stop().

   When terminating a transfer machine the application has a choice of draining
   current operations or aborting such activity.  If the latter choice is made,
   then the transport must first cancel all operations.

   Regardless, the transfer machine's event handler thread completes the
   termination process.  It waits until all buffer queues are empty, then
   invokes the nlx_core_tm_stop() subroutine to free the LNet EQ and other
   resources associated with the transfer machine.  It then posts the transfer
   machine state change event and terminates itself.


   @subsection LNetDLD-lspec-tm-thread Transfer Machine Event Handler Thread

   Each transfer machine processes buffer events from the core API's event
   queue.  The core API guarantees that LNet operation completion events will
   result in buffer events being enqueued in the order the API receives them,
   and, in particular, that multiple buffer events for any given receive buffer
   will be ordered.  This is very important for the transport, because it has
   to ensure that a receive buffer operation is not prematurely flagged as
   dequeued.

   The transport uses exactly one event handler thread to process buffer events
   from the core API.  This has the following advantages:
   - The implementation is simple.
   - It implicitly race-free with respect to receive buffer events.

   Applications are not expected to spend much time in the event callback, so
   this simple approach is acceptable.

   The application can establish specific processor affiliation for the event
   handler thread with the c2_net_tm_confine() subroutine @em prior to starting
   the transfer machine. This results in a call to the nlx_xo_tm_confine()
   subroutine, which makes a copy of the desired processor affinity bitmask in
   nlx_xo_transfer_mc::xtm_processors.

   In addition to buffer event processing, the event handler thread performs the
   following functions:
   - Transfer machine state change event posting
   - Buffer operation timeout processing
   - Logging of statistical data

   The functionality of the event handler thread is illustrated by
   the following pseudo-code:
   @code
   // start the transfer machine in the Core
   rc = nlx_core_tm_start(&tm, &lctm, &cepa);
   // deliver a C2_NET_TEV_STATE_CHANGE event to transition the TM to
   // the C2_NET_TM_STARTED or C2_NET_TM_FAILED states
   // Set the transfer machine's end point on success
   c2_net_tm_event_post(&tmev);
   // loop forever
   while (1) {
      timeout = ...; // compute next timeout
      rc = nlx_core_buf_event_wait(&lctm, &timeout);
      // buffer event processing
      if (rc == 0) { // did not time out - events pending
          do { // consume all pending events
             struct nlx_core_buffer_event lcbe;
	     struct c2_net_buffer_event nbev;
             c2_mutex_lock(&tm->ntm_mutex);
             rc = nlx_core_buf_event_get(&lctm, &lcbe);
             // may need to create/lookup msg sender end point objects here.
	     c2_mutex_unlock(&tm->ntm_mutex);
	     if (rc == 0) {
	        nbe = ... // convert the event
	        c2_net_buffer_event_post(&nbev);
             }
          } while (rc == 0);
      }
      // do buffer operation timeout processing
      ...
      // termination processing
      if (tm->ntm_state == C2_NET_TM_STOPPING) {
            bool must_stop = false;
            c2_mutex_lock(&tm->ntm_mutex);
            if (all_tm_queues_are_empty(tm)) {
	       nlx_core_tm_stop(&lctm);
	       must_stop = true;
            }
            c2_mutex_unlock(&tm->ntm_mutex);
            if (must_stop) {
	       struct c2_net_tm_event tmev;
               // construct a C2_NET_TEV_STATE_CHANGE event to transition
	       // to the C2_NET_TM_STOPPED state.
	       c2_net_tm_event_post(&tmev);
               break;
            }
      }
      // Log statistical data periodically using ADDB
   }
   @endcode
   (The C++ style comments above are used only because the example is
   embedded in a Doxygen C comment.  C++ comments are not permitted by the
   Colibri coding style.)

   A few points to note on the above pseudo-code:
   - The transfer machine mutex is obtained across the call to dequeue buffer
     events to serialize with the "other" consumer of the buffer event queue,
     the @c xo_buf_add() subroutine that invokes the core API buffer operation
     initiation subroutines.  This is because these subroutines may allocate
     additional buffer event structures to the queue.
   - The transfer machine mutex may also be needed to create end point objects
     to identify the sender of messages received by the transfer machine.
   - The thread attempts to process as many events as it can each time around
     the loop.  The call to the nlx_core_buf_event_wait() subroutine in the
     user space transport is expensive as it makes a device driver @c ioctl
     call internally.
   - The thread is responsible for terminating the transfer machine and
     delivering its termination event.


   @subsection LNetDLD-lspec-buf-nbd Network Buffer Descriptor

   The transport has to define the format of the opaque network buffer
   descriptor returned to the application, to encode the identity of the
   passive buffers.

   The following internal format is used:
   @code
   struct nlx_xo_buf_desc {
        uint64_t                 xbd_match_bits;
        struct nlx_core_ep_addr  xbd_passive_ep;
        enum c2_net_queue_type   xbd_qtype;
        c2_bcount_t              xbd_size;
   };
   @endcode

   All the fields are integer fields and the structure is of fixed length.  It
   is encoded and decoded into its opaque over-the-wire struct c2_net_buf_desc
   format with dedicated encoding routines.


   @subsection LNetDLD-lspec-buf-op Buffer operations

   Buffer operations are initiated through the c2_net_xprt_ops::xo_buf_add()
   subroutine. The transport must invoke one of the relevant core API buffer
   initiation operations.

   In passive buffer operations, the transport must first obtain suitable match
   bits for the buffer using the nlx_core_buf_match_bits_set() subroutine.  The
   transport is responsible for ensuring that the assigned match bits are not
   in use currently; however this step can be ignored with relative safety as
   the match bit space is very large and the match bit counter will only wrap
   around after a very long while.  These match bits should also be encoded in
   the network buffer descriptor that the transport must return.

   In active buffer operations, the size of the active buffer should be
   validated against the size of the passive buffer as given in its network
   buffer descriptor.


   @subsection LNetDLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   The transport does not introduce its own state models but operates within
   the framework defined by the Colibri Networking Module.  The state of the
   following objects are particularly called out:

   - c2_net_buffer
   - c2_net_transfer_mc
   - c2_net_domain
   - c2_net_end_point

   Enqueued network buffers represent operations in progress.  Until they get
   dequeued, the buffers are associated with underlying LNet kernel module
   resources.

   The transfer machine is associated with an LNet event queue (EQ).  The EQ
   must be created when the transfer machine is started, and destroyed when the
   transfer machine stops.

   Buffers registered with a domain object are potentially associated with LNet
   kernel module resources and, if the transport is in user space, kernel
   memory resources as the buffer vector gets pinned in memory. De-registration
   of the buffers releases these resources.  The domain object of a user space
   transport is also associated with an open file descriptor to the device
   driver used to communicate with the kernel Core API.

   End point structures are exposed externally as struct c2_net_end_point, but
   are allocated and managed internally by the transport using struct
   nlx_xo_ep.  They are reference counted, and the application must release all
   references before attempting to finalize a transfer machine.


   @subsection LNetDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   The transport inherits the concurrency model of the Colibri Networking
   Module. All transport operations are protected by some lock or object state,
   as described in the <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/view">RPC Bulk Transfer Task Plan</a>.  The Core API is designed to work with this same locking model.
   The locking order figure is repeated here for convenience:
   @dot
   digraph {
      node [shape=plaintext];
      subgraph cluster_m1 { // represents mutex scope
         // sorted R-L so put mutex name last to align on the left
         rank = same;
	 n1_2 [label="dom_fini()"];  // procedure using mutex
	 n1_1 [label="dom_init()"];
         n1_0 [label="c2_net_mutex"];// mutex name
      }
      subgraph cluster_m2 {
         rank = same;
	 n2_2 [label="tm_fini()"];
         n2_1 [label="tm_init()"];
         n2_4 [label="buf_deregister()"];
	 n2_3 [label="buf_register()"];
         n2_0 [label="nd_mutex"];
      }
      subgraph cluster_m3 {
         rank = same;
	 n3_2 [label="tm_stop()"];
         n3_1 [label="tm_start()"];
	 n3_6 [label="ep_put()"];
	 n3_5 [label="ep_create()"];
	 n3_4 [label="buf_del()"];
	 n3_3 [label="buf_add()"];
         n3_0 [label="ntm_mutex"];
      }
      label="Mutex usage and locking order in the Network Layer";
      n1_0 -> n2_0;  // locking order
      n2_0 -> n3_0;
   }
   @enddot

   The transport only has one thread, its event processing thread.  This thread
   uses the transfer machine lock when serialization is required by the Core
   API, and also when creating or looking up end point objects when processing
   receive buffer events. See @ref LNetDLD-lspec-tm-thread for details.


   @subsection LNetDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   The application can establish specific processor affiliation for the event
   handler thread with the c2_net_tm_confine() subroutine prior to starting the
   transfer machine.  Buffer completion events and transfer machine state
   change events will be delivered through callbacks made from this thread.

   <hr>
   @section LNetDLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref LNetDLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   - <b>i.c2.net.xprt.lnet.transport-variable</b> The transport variable
   @c c2_net_lnet_xprt is provided.

   - <b>i.c2.net.lnet.buffer-registration</b> Buffer registration is required
   in the network API and has a corresponding nlx_xo_buf_register() at the LNet
   transport layer.  This can be extended for hardware optimization once LNet
   provides such APIs.

   - <b>i.c2.net.xprt.lnet.end-point-address</b> Mapping of LNet end point
   address is handled in the Core API as described in the @ref
   LNetCoreDLD-fspec "LNet Transport Core Functional Specification".

   - <b>i.c2.net.xprt.lnet.multiple-messages-in-buffer</b> Fields are provided
   in the c2_net_buffer to support multiple message delivery, and the event
   delivery model includes the delivery of buffer events for receive buffers
   that do not always dequeue the buffer.

   - <b>i.c2.net.xprt.lnet.dynamic-address-assignment</b> Dynamic transfer
     machine identifier assignment is provided by nlx_core_tm_start().

   - <b>i.c2.net.xprt.lnet.processor-affinity</b> The c2_net_tm_confine() API
   is provided and the LNet transport provides the corresponding
   nlx_xo_tm_confine() function.

   - <b>i.c2.net.xprt.lnet.user-space</b> The API provides "core" functionality
   for both user and kernel space and reduces context switches required for
   user-space event processing, especially through the use of a circular queue
   using atomic operations.

   <hr>
   @section LNetDLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   To control symbol exposure, the transport code is compiled using a single C
   file that includes other C files with static symbols.  Unit testing will
   take advantage of this setup and use conditional renaming of symbols to
   intercept specific internal interfaces.

   The following tests will be performed for the transport operation (xo) layer
   with a fake Core API.  Tests involving the fake Core API ensure that the
   transport operation layer makes the correct calls to the Core API.

   - Multiple domain creation will be tested.
   - Buffer registration and deregistration will be tested.
   - Multiple transfer machine creation will be tested.
   - Test that the processor affinity bitmask is set in the TM.
   - The transfer machine state change functionality.
   - Initiation of buffer operations will be tested.
   - Delivery of synthetic buffer events will be tested, including multiple
     receive buffer events for a single receive buffer.
   - Management of the reference counted end point objects; the addresses
     themselves don't have to valid for these tests.
   - Encoding and Decoding of the network buffer descriptor will be tested.
   - Orderly finalization will be tested.

   <hr>
   @section LNetDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   The @c bulkping system test program will be updated to include support for
   the LNet transport.  This program will be used to test communication between
   end points on the same system and between remote systems.

   <hr>
   @section LNetDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   In general, the transport operational layer simply routes data too and from
   the Core API; this behavior is analyzed in
   @ref KLNetCoreDLD "LNet Transport Kernel Core DLD".

   An area of concern specific to the transport operations layer is the
   management of end point objects.  In particular, the time taken to search
   the list of end point objects is of O(N) - i.e. a linear search through the
   list, which is proportional to the number of list items.  This may become
   expensive if the list grows large - items on the list are reference counted
   and it is up to the application to release them.

   The internal end point address fields are all numeric and easily lend
   themselves to a hash based strategy (the NID value is the best candidate
   key).  The tricky part of an implementation, were we to address this, would
   be to determine what hash strategy would result in a reasonably even
   distribution, but worst case, it degenerates to the linear search we have at
   present.  The choice of whether to use hashing depends on what the expected
   behavior of a Colibri server will be like in steady state.


   <hr>
   @section LNetDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/view">RPC Bulk Transfer Task Plan</a>
   - @subpage LNetcqueueDLD "LNet Buffer Event Circular Queue DLD"
   - @subpage KLNetCoreDLD "LNet Transport Kernel Core DLD"
   - @subpage ULNetCoreDLD "LNet Transport User Space Core DLD"

 */

/*
 ******************************************************************************
 End of DLD
 ******************************************************************************
 */

#include "net/lnet/lnet_core.h"

/* To reduce global symbols, yet make the code readable, we
   include other .c files with static symbols into this file.
   Dependency information must be captured in Makefile.am.

   Static functions should be declared in the private header file
   so that the order of their definition does not matter.
*/
#include "net/lnet/bev_cqueue.c"
#ifdef __KERNEL__
#include "net/lnet/linux_kernel/klnet_core.c"
#else
#include "net/lnet/ulnet_core.c"
#endif
#include "net/lnet/lnet_xo.c"

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

