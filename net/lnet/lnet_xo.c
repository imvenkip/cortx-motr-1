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

   This document describes the Colibri Network transport for LNet.

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

   - @ref LNetCore "LNet Transport Core Interface"
   - The @ref Processor API and control over thread affinity to processor.

   <hr>
   @section LNetDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - Common user and kernel space implementation over an underlying "Core" I/O
     layer.
   - Handles I/O flow control
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

   @subsection LNetDLD-lspec-ep End Point Address Structure

   The transport defines the following structure to encode an end point address:
   @code
   struct nlx_xo_ep {
       struct c2_net_end_point nxe_ep;
       nlx_core_ep_addr        nxe_core;
       char                    nxe_addr[1];
   };
   @endcode
   The length of the structure depends on the length of the string
   representation of the address, which must be saved in the @c nxe_addr array.


   @subsection LNetDLD-lspec-tm-start Transfer Machine Startup

   When starting a transfer machine, the transport must call the
   nlx_core_tm_start() subroutine to create the internal LNet EQ associated
   with the transfer machine.  This call also validates the transfer machine's
   address, and assigns a dynamic transfer machine identifier if needed.

   Note that while the transport supports the dynamic allocation of a transfer
   machine identifier, it still requires the rest of the fields of the end point
   address.


   @subsection LNetDLD-lspec-tm-stop Transfer Machine Termination

   When terminating a transfer machine the application has a choice of draining
   current operations or aborting such activity.  If the latter choice is made,
   then the transport must first cancel all operations.  In either case, the
   transfer machine's event handler thread must deliver the completion or
   cancellation events of such operations before stopping, so the
   c2_net_tm_stop() subroutine call that invokes the transport's
   c2_net_xprt_ops::xo_tm_stop() method is an inherently asynchronous operation.

   @subsection LNetDLD-lspec-tm-thread Transfer Machine Event Handler Thread

   Each transfer machine processes buffer events from the core API's event
   queue.  The core API guarantees that LNet operation completion events will
   result in buffer events being enqueued in the order it receives them, and,
   in particular, that multiple buffer events for any given receive buffer will
   be ordered.  This is very important for the transport, because it has to
   ensure that a receive buffer operation is not prematurely flagged as
   dequeued.

   The transport uses exactly one event handler thread to process buffer events
   from the core API.  This has the following advantages:
   - The implementation is simple.
   - It implicitly race-free with respect to receive buffer events.

   Applications are not expected to spend much time in the event callback, so
   this simple approach is acceptable.

   In addition to event processing, the event handler thread has the following
   functions:
   - Buffer operation timeout processing
   - Transfer machine termination

   The event handler thread body is illustrated by the following pseudo-code:
   @code
   while (1) {
      timeout = ...; // compute next timeout
      rc = nlx_core_buf_event_wait(&lctm, &timeout);
      // event processing
      if (rc == 0) {
          do {
             struct nlx_core_buffer_event lcbe;
	     struct c2_net_buffer_event nbev;
             c2_mutex_lock(&tm->ntm_mutex);
             rc = nlx_core_buf_event_get(&lctm, &lcbe);
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
            c2_mutex_lock(&tm->ntm_mutex);
            if (all_tm_queues_are_empty(tm)) {
	       nlx_core_tm_stop(&lctm);
               tm->ntm_state = C2_NET_TM_STOPPED;
            }
            c2_mutex_unlock(&tm->ntm_mutex);
            if (tm->ntm_state == C2_NET_TM_STOPPED) {
	       struct c2_net_tm_event tmev;
	       c2_net_tm_event_post(&tmev);
               break;
            }
      }
   }
   @endcode
   A few points to note on the above pseudo-code:
   - The transfer machine mutex is obtained across the call to dequeue buffer
     events to serialize with the "other" consumer of the buffer event queue,
     the @c xo_buf_add() subroutine that invokes the core API buffer operation
     initiation subroutines.  This is because these subroutines may allocate
     additional buffer event structures to the queue.
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
        uint64_t                 nlxbd_match_bits;
        struct nlx_core_ep_addr  nlxbd_active_ep;
        struct nlx_core_ep_addr  nlxbd_passive_ep;
        enum c2_net_queue_type   nlxbd_qtype;
        c2_bcount_t              nlxbd_total;
   };
   @endcode

   All the fields are integer fields and the structure is of fixed length.
   It is encoded into its opaque over-the-wire format with dedicated encoding
   routines that do not use the XDR support library. @todo Use XDR in Lnet NBD?


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
   framework defined by the Colibri Networking Module.  The state of the
   following objects are particularly called out:

   - c2_net_buffer
   - c2_net_transfer_mc
   - c2_net_domain

   Enqueued network buffers represent operations in progress.  Until they get
   dequeued, the buffers are associated with underlying LNet kernel module
   resources.

   The transfer machine is associated with an LNet event queue (EQ).  The EQ
   must be created when the transfer machine is started, and destroyed when the
   transfer machine stops.

   Buffers registered with a domain object are potentially associated with LNet
   kernel module resources and, if the transport is in user space, kernel
   memory resources as they get pinned in memory. De-registration of the
   buffers releases this memory.  The domain object of a user space transport
   is also associated with an open file descriptor to the device driver used to
   communicate with the kernel Core API.


   @subsection LNetDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   @subsection LNetDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

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
   LNetCoreDLD-fspec "LNet Transport Core Functional Specfication".

   - <b>i.c2.net.xprt.lnet.multiple-messages-in-buffer</b> Fields are provided
   in the c2_net_buffer to support multiple message delivery, and the event
   delivery model includes the delivery of buffer events for receive buffers
   that do not always dequeue the buffer.

   - <b>i.c2.net.xprt.lnet.dynamic-address-assignment</b> Dynamic address assignment is provided in the nlx_core_tm_start().

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

   <hr>
   @section LNetDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
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

/**
   @addtogroup LNetXODFS
   @{
*/

static int nlx_xo_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
}

static void nlx_xo_dom_fini(struct c2_net_domain *dom)
{
}

static c2_bcount_t nlx_xo_get_max_buffer_size(const struct c2_net_domain *dom)
{
}

static c2_bcount_t nlx_xo_get_max_buffer_segment_size(const struct
						       c2_net_domain *dom)
{
}

static int32_t nlx_xo_get_max_buffer_segments(const struct c2_net_domain *dom)
{
}

static int nlx_xo_end_point_create(struct c2_net_end_point **epp,
				   struct c2_net_transfer_mc *tm,
				   const char *addr)
{
}

static int nlx_xo_buf_register(struct c2_net_buffer *nb)
{
}

static void nlx_xo_buf_deregister(struct c2_net_buffer *nb)
{
}

static int nlx_xo_buf_add(struct c2_net_buffer *nb)
{
}

static void nlx_xo_buf_del(struct c2_net_buffer *nb)
{
}

static int nlx_xo_tm_init(struct c2_net_transfer_mc *tm)
{
}

static void nlx_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
}

static int nlx_xo_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
}

static int nlx_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
}

static int nlx_xo_tm_confine(struct c2_net_transfer_mc *tm,
			     const struct c2_bitmap *processors)
{
}

static const struct c2_net_xprt_ops nlx_xo_xprt_ops = {
	.xo_dom_init                    = nlx_xo_dom_init,
	.xo_dom_fini                    = nlx_xo_dom_fini,
	.xo_get_max_buffer_size         = nlx_xo_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = nlx_xo_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = nlx_xo_get_max_buffer_segments,
	.xo_end_point_create            = nlx_xo_end_point_create,
	.xo_buf_register                = nlx_xo_buf_register,
	.xo_buf_deregister              = nlx_xo_buf_deregister,
	.xo_buf_add                     = nlx_xo_buf_add,
	.xo_buf_del                     = nlx_xo_buf_del,
	.xo_tm_init                     = nlx_xo_tm_init,
	.xo_tm_fini                     = nlx_xo_tm_fini,
	.xo_tm_start                    = nlx_xo_tm_start,
	.xo_tm_stop                     = nlx_xo_tm_stop,
	.xo_tm_confine                  = nlx_xo_tm_confine,
};

/**
   @} LNetXODFS
*/

struct c2_net_xprt c2_net_lnet_xprt = {
	.nx_name = "lnet",
	.nx_ops  = &nlx_xo_xprt_ops
};
C2_EXPORTED(c2_net_lnet_xprt);

bool c2_net_lnet_ep_addr_net_compare(const char *addr1, const char *addr2)
{
	return false;
}
C2_EXPORTED(c2_net_lnet_ep_addr_net_compare);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

