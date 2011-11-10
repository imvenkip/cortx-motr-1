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
 * Original creation date: 11/10/2011
 */

/**
   @page cqueueDLD LNet Buffer Event Circular Queue DLD

   - @ref cqueueDLD-ovw
   - @ref cqueueDLD-def
   - @ref cqueueDLD-req
   - @ref cqueueDLD-depends
   - @ref cqueueDLD-highlights
   - @subpage cqueueDLD-fspec "Functional Specification"
      - @ref cqueue "External Interfaces"           <!-- ext link -->
      - @ref cqueueIFS "Internal Interfaces"        <!-- int link -->
   - @ref cqueueDLD-lspec
      - @ref cqueueDLD-lspec-comps
      - @ref cqueueDLD-lspec-q
      - @ref cqueueDLD-lspec-state
      - @ref cqueueDLD-lspec-thread
      - @ref cqueueDLD-lspec-numa
   - @ref cqueueDLD-conformance
   - @ref cqueueDLD-ut
   - @ref cqueueDLD-st
   - @ref cqueueDLD-O
   - @ref cqueueDLD-ref

   <hr>
   @section cqueueDLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   The circular queue provides a data structure and interfaces to manage a
   lock-free queue for a single producer and consumer.  The producer and
   consumer can be in different address spaces with the queue in shared memory.

   <hr>
   @section cqueueDLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:

   New terms:

   <hr>
   @section cqueueDLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   - <b>r.c2.lib.atomic.interoperable-kernel-user-support</b> The
   implementation shall provide a queue that supports atomic,
   interoperable sharing between kernel to user-space.
   - <b>r.net.xprt.lnet.growable-event-queue</b> The implementation that support
   an event queue to which new elements can be added over time.

   <hr>
   @section cqueueDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - The @ref "atomic API".

   <hr>
   @section cqueueDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - A data structure representing a circular queue.
   - Handles atomic access to slots in the queue for a single producer and
   consumer.
   - Handles dynamically adding new elements to the circular queue.

   <hr>
   @section cqueueDLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref cqueueDLD-lspec-comps
   - @ref cqueueDLD-lspec-q
   - @ref cqueueDLD-lspec-state
   - @ref cqueueDLD-lspec-thread
   - @ref cqueueDLD-lspec-numa

   @subsection cqueueDLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The circular queue is a single component.

   @subsection cqueueDLD-lspec-q Logic of the Circular Queue

   The circular queue is a FIFO queue.  The implementation maintains pointers
   for the consumer and producer, and operations for accessing these pointers
   and for moving them around the circular queue elements.  The application
   manages the memory containing the queue itself, and adds new elements to the
   queue when the size of the queue needs to grow.

   @dot
   digraph {
   {
       rank=same;
       node [shape=plaintext];
       c2_cqueue;
       node [shape=record];
       struct1 [label="<f0> cq_divider|<f1> cq_last"];
   }
   {
       rank=same;
       ordering=out;
       node [shape=plaintext];
       "application array";
       node [shape=record];
       array1 [label="<f0> |<f1> y|<f2> x|<f3> x|<f4> x|<f5> x|<f6> |<f7> "];
       "application array" -> array1 [style=invis];
   }
   c2_cqueue -> "application array" [style=invis];
   struct1:f0 -> array1:f1;
   struct1:f1 -> array1:f6;
   }
   @enddot

   The elements starting after @c cq_divider up to and including @c cq_last
   contain data to be consumed (those elements marked with "x" in the diagram).
   So, @c cq_divider follows @c cq_last around the circular queue.  When
   @c cq_divider->next is the same as @c cq_last, the queue is empty
   (necessarily requiring that the queue be initialised with at least 2
   elements).  The element pointed to by @c cq_divider (element "y" in the
   diagram) is the element most recently consumed by the consumer.  The producer
   cannot use this element, because if it did, producing that element would
   result in moving @c cq_last so that it would pass @c cq_divider.

   In the context of the LNet Buffer Event Queue, the transport is required to
   add enough elements to the queue strictly before it enqueues buffer
   operations requiring subsequent completion notifications.  The circular queue
   does not enforce this requirement, but does provide APIs that the transport
   can use to determine the current number of elements in the queue and to add
   new elements.

   The element denoted by @c cq_last is returned by
   @c c2_cqueue_pnext as long as the queue is not full.  This allows
   the producer to determine the next available slot index and populate the
   application array slot itself with the data to be produced.  Once the
   array slot contains the data, the producer then calls
   @c c2_cqueue_produce to make that slot available to the consumer.
   This call also increments @c cq_last.

   The consumer uses @c c2_cqueue_consume to get the next available
   slot containing data in FIFO order.  Consuming a slot increments
   @c cq_divider.  After incrementing, the consumer "owns" the slot returned,
   slot "y" in the diagram.  The consumer owns this slot until it calls
   @c c2_cqueue_consume again, at which time ownership reverts to the
   queue and can be reused by the producer.

   @subsection cqueueDLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   The circular queue can be in one of 3 states:
   - empty: This is the initial state and the queue returns to this state
   whenever @code cq_divider == cq_last @endcode
   - partial: In this state, the queue contains elements to be consumed and
   still has room for additional elements to be added. This can be expressed as
   @code cq_divider != cq_last && (cq_last + 1) % cq_size != cq_divider @endcode
   - full: The queue contains elements and has no room for more. This can be
   expressed as @code (cq_last + 1) % cq_size == cq_divider @endcode

   @subsection cqueueDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   A single producer and consumer are supported.  Atomic variables,
   @c cq_divider and @c cq_last, represent the range of slots in the queue
   containing data.  Because these indexes are atomic, no locking is needed
   to access them by a single producer and consumer.  Multiple producers
   and/or consumers must synchronize externally.

   @subsection cqueueDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   None.

   <hr>
   @section cqueueDLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref cqueueDLD-req
   section, and explains briefly how the DLD meets the requirement.</i>

   - <b>i.c2.lib.atomic.interoperable-kernel-user-support</b> The use of
   indexes instead of pointers allows a queue and its associated memory
   slots to be placed in shared memory, accessible to both a user-space
   process and the kernel.  The atomic operations allow the FIFO to be
   produced and consumed simultaneously in both spaces without
   synchronization or context switches.

   <hr>
   @section cqueueDLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   The following cases will be tested by unit tests:
   - initialising a queue of minimum size 2
   - successfully producing an element in a slot
   - failing to produce a slot because the queue is full
   - successfully consuming an element from a slot
   - failing to consume a slot because the queue is empty
   - initialising a queue of larger size
   - repeating the producing and consuming tests
   - concurrently producing and consuming elements

   <hr>
   @section cqueueDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section cqueueDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   The circular queue (the struct c2_cqueue) consumes fixed size
   memory, independent of the size of the array representing the queue's data.
   Operations on the queue are O(1) complexity.

   <hr>
   @section cqueueDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>

 */

#include "lnet_core.h"

/**
   @defgroup bevcqueue LNet Buffer Event Queue Interface
   @ingroup LNetDFS

   The buffer event FIFO circular queue, used between the LNet Core and LNet
   transport.

   Unlike the standard c2_queue, this queue supports a producer and consumer in
   different address spaces sharing the queue via shared memory.  No locking is
   required by this single producer or consumer.

   @{
 */

/**
   Buffer event queue invariant.
 */
static bool bev_cqueue_invariant(const struct c2_queue *q);

/**
   Initialise the buffer event queue.
   @param q buffer event queue to initialise
   @param ql1 the first element in the new queue
   @param ql2 the second element in the new queue
   @pre q != NULL && ql1 != NULL && ql2 != NULL
   @post bev_cqueue_invariant(q)
*/
static void bev_cqueue_init(struct c2_lnet_core_bev_cqueue *q,
			    struct c2_lnet_core_bev_link *ql1,
			    struct c2_lnet_core_bev_link *ql2);


/**
   Adds a free element to the circular buffer queue.
   @param q the queue
   @param ql the element to add
 */
static void bev_cqueue_add(struct c2_lnet_core_bev_cqueue *q,
			   struct c2_lnet_core_bev_link *ql);
/**
   Finalise the buffer event queue.
 */
static void bev_cqueue_fini(struct c2_lnet_core_bev_cqueue *q);

/**
   Test if the buffer event queue is empty.
 */
static bool bev_cqueue_is_empty(const struct c2_lnet_core_bev_cqueue *q);

/**
   Get the oldest element in the FIFO circular queue, advancing the divider.
   @param q the queue
   @returns the link to the element, NULL when the queue is empty
 */
static struct c2_lnet_core_bev_link *bev_cqueue_get(
					    struct c2_lnet_core_bev_cqueue *q);

/**
   Determine the next element in the queue that can be used by the producer.
   @param q the queue
   @returns a pointer to the next available element, or NULL if none exists
 */
static struct c2_lnet_core_bev_link* bev_cqueue_pnext(
				      const struct c2_lnet_core_bev_cqueue *q);

/**
   Put (produce) an element so it can be consumed.  The caller must first
   call bev_cqueue_pnext() to ensure such an element exists.
   @param q the queue
   @pre q->cq_last != q->cq_divider
 */
static void bev_cqueue bev_cqueue_put(struct c2_lnet_core_bev_cqueue *q);

/**
   @}
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
