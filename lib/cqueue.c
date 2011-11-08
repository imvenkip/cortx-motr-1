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
 * Original creation date: 11/03/2011
 */

/**
   @page cqueueDLD Circular Queue for Single Producer and Consumer DLD

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
   fixed sized, lock-free queue for a single producer and consumer.

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

   <hr>
   @section cqueueDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - The @ref  "atomic API".

   <hr>
   @section cqueueDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - A data structure representing a fixed size queue.
   - Memory for the queue itself is maintained by the application.
   - Handles atomic access to slots in the queue for a single producer and
   consumer.

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

   The circular queue is a FIFO queue of a fixed size.  The implementation
   maintains slot indexes for the consumer and producer, and operations for
   accessing and incrementing these indexes.  The application manages the
   memory containing the queue itself (an array, given that the queue is
   slot index based).

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
   struct1:f0 -> array1:f2;
   struct1:f1 -> array1:f6;
   }
   @enddot

   The slots starting from @c cq_divider up to but not including @c cq_last
   contain data to be consumed (those slots marked with "x" in the
   diagram).  So, @c cq_divider follows @c cq_last around the circular
   queue.  When @c cq_divider is the same as @c cq_last, the queue is
   empty.  Because the queue is circular, the index value of @c cq_last can
   be less than the value of @c cq_divider (i.e. the index values wrap
   around).  Note that there must always be one slot before @c cq_divider and
   after @c cq_last in the queue. The producer cannot use the final slot
   between @c cq_last (wrapped around) and @c cq_divider, because if it
   did, producing that slot would result in incrementing @c cq_last so that
   it would be equal to @c cq_divider, which denotes an empty, not a full,
   queue.  Also, this slot, slot "y" in the diagram, is the index of the slot
   most recently consumed by the consumer, as discussed further below.

   The slot index denoted by @c cq_last is returned by
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

   None.

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

#include "cqueue.h"

/**
   @defgroup cqueueIFS Circular Queue Internal Interfaces
   @ingroup cqueue
   @{
*/

/**
   Invariant for circular queue.
 */
static bool cqueue_invariant(struct c2_cqueue *q);

/**
   @} cqueueIFS
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
