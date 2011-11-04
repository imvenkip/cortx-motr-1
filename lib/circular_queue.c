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
 * Original creation date: 09/26/2011
 */

/**
   @page circular_queueDLD Circular Queue DLD

   - @ref circular_queueDLD-ovw
   - @ref circular_queueDLD-def
   - @ref circular_queueDLD-req
   - @ref circular_queueDLD-depends
   - @ref circular_queueDLD-highlights
   - @subpage circular_queueDLD-fspec "Functional Specification"
   - @ref circular_queueDLD-lspec
      - @ref circular_queueDLD-lspec-comps
      - @ref circular_queueDLD-lspec-state
      - @ref circular_queueDLD-lspec-thread
      - @ref circular_queueDLD-lspec-numa
   - @ref circular_queueDLD-conformance
   - @ref circular_queueDLD-ut
   - @ref circular_queueDLD-st
   - @ref circular_queueDLD-O
   - @ref circular_queueDLD-ref


   <hr>
   @section circular_queueDLD-ovw Overview
   <i>All specifications must start with an Overview section that
   briefly describes the document and provides any additional
   instructions or hints on how to best read the specification.</i>

   The circular queue provides a data structure and interfaces to manage a
   fixed sized, lock-free queue for a single producer and consumer.

   <hr>
   @section circular_queueDLD-def Definitions
   <i>Mandatory.
   The DLD shall provide definitions of the terms and concepts
   introduced by the design, as well as the relevant terms used by the
   specification but described elsewhere.  References to the
   C2 Glossary are permitted and encouraged.  Agreed upon terminology
   should be incorporated in the glossary.</i>

   Previously defined terms:

   New terms:

   <hr>
   @section circular_queueDLD-req Requirements
   <i>Mandatory.
   The DLD shall state the requirements that it attempts to meet.</i>

   They should be expressed in a list, thusly:

   <hr>
   @section circular_queueDLD-depends Dependencies
   <i>Mandatory. Identify other components on which this specification
   depends.</i>

   - The atomic API.

   <hr>
   @section circular_queueDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - A data structure representing a fixed size queue.
   - Memory for the queue itself is maintained by the application.
   - Handles atomic access to slots in the queue for a single producer and
   consumer.

   <hr>
   @section circular_queueDLD-lspec Logical Specification
   <i>Mandatory.  This section describes the internal design of the component,
   explaining how the functional specification is met.  Sub-components and
   diagrams of their interaction should go into this section.  The section has
   mandatory subsections created using the Doxygen @@subsection command.  The
   designer should feel free to use additional sub-sectioning if needed, though
   if there is significant additional sub-sectioning, provide a table of
   contents here.</i>

   - @ref circular_queueDLD-lspec-comps
   - @ref circular_queueDLD-lspec-state
   - @ref circular_queueDLD-lspec-thread
   - @ref circular_queueDLD-lspec-numa


   @subsection circular_queueDLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The circular queue is a FIFO queue of a fixed size.  The implementation
   maintains slot indexes for the consumer and producer, and operations for
   incrementing these indexes.  The application manages the memory containing
   the queue itself (an array, given that the queue is slot index based).

   @subsection circular_queueDLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   @subsection circular_queueDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   A single producer and consumer are supported.  Atomic variables,
   cq_divider and cq_last, represent the range of slots in the queue
   containing data.  Because these indexes are atomic, no locking is needed
   to access them by a single producer and consumer.  Multiple producers
   and/or consumers must synchronize externally.

   @subsection circular_queueDLD-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   <hr>
   @section circular_queueDLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref circular_queueDLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section circular_queueDLD-ut Unit Tests
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
   @section circular_queueDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section circular_queueDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section circular_queueDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

 */

#include "circular_queue.h"

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
