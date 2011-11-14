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
   @page LNetcqueueDLD LNet Buffer Event Circular Queue DLD

   - @ref cqueueDLD-ovw
   - @ref cqueueDLD-def
   - @ref cqueueDLD-req
   - @ref cqueueDLD-depends
   - @ref cqueueDLD-highlights
   - @subpage cqueueDLD-fspec "Functional Specification"
      - @ref bevcqueue "External Interfaces"        <!-- int link -->
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

   - The @ref atomic API.

   <hr>
   @section cqueueDLD-highlights Design Highlights
   <i>Mandatory. This section briefly summarizes the key design
   decisions that are important for understanding the functional and
   logical specifications, and enumerates topics that need special
   attention.</i>

   - A data structure representing a circular queue.
   - Handles atomic access to elements in the queue for a single producer and
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
   queue when the size of the queue needs to grow.  In this discussion of the
   logic, the pointers are named @c consumer, @c producer and @c next for
   brevity.

   @dot
   digraph {
   {
       rank=same;
       node [shape=plaintext];
       c2_lnet_core_bev_cqueue;
       node [shape=record];
       struct1 [label="<f0> consumer|<f1> producer"];
   }
   {
       rank=same;
       ordering=out;
       node [shape=plaintext];
       "element list";
       node [shape=record];
       list1 [label="<f0> |<f1> y|<f2> x|<f3> x|<f4> x|<f5> x|<f6> |<f7> "];
       "element list" -> list1 [style=invis];
   }
   c2_lnet_core_bev_cqueue -> "element list" [style=invis];
   struct1:f0 -> list1:f1;
   struct1:f1 -> list1:f6;
   }
   @enddot

   The elements starting after @c consumer up to but not including @c producer
   contain data to be consumed (those elements marked with "x" in the diagram).
   So, @c consumer follows @c producer around the circular queue.  When
   @c consumer->next is the same as @c producer, the queue is empty
   (requiring that the queue be initialised with at least 2 elements).  The
   element pointed to by @c consumer (element "y" in the diagram) is the
   element most recently consumed by the consumer.  The producer cannot use this
   element, because if it did, producing that element would result in moving
   @c producer so that it would pass @c consumer.

   In the context of the LNet Buffer Event Queue, the transport should add
   enough elements to the queue strictly before it enqueues buffer operations
   requiring subsequent completion notifications.  The number required is the
   total number of possible events generated by queued buffers, plus one extra
   element for the most recently consumed event notification.  The circular
   queue does not enforce this requirement, but does provide APIs that the
   transport can use to determine the current number of elements in the queue
   and to add new elements.

   The element denoted by @c producer is returned by bev_cqueue_pnext() as
   long as the queue is not full.  This allows the producer to determine the
   next available element and populate it with the data to be produced.  Once
   the element contains the data, the producer then calls bev_cqueue_put()
   to make that element available to the consumer.  This call also moves the
   @c producer pointer to the next element.

   The consumer uses bev_cqueue_get() to get the next available element
   containing data in FIFO order.  Consuming an element causes @c consumer to
   be pointed at the next element in the queue.  After this call returns, the
   consumer "owns" the element returned, element "y" in the diagram.  The
   consumer owns this element until it calls bev_cqueue_get() again, at which
   time ownership reverts to the queue and can be reused by the producer.

   The pointers themselves are more complex than the brief description above.
   The @c consumer pointer refers to the element just consumed in the consumer's
   (the transport) address space.  The @c producer pointer refers to the element
   in the producer's (the core) address space.  The @c next link is actually
   represented by a data structure, c2_lnet_core_bev_link.
   @code
   struct c2_lnet_core_bev_link {
            c2_lnet_core_opaque_ptr_t lcbevl_c_self;
                   // Self pointer in the transport address space.
            c2_lnet_core_opaque_ptr_t lcbevl_p_self;
                   // Self pointer in the kernel address space.
            c2_lnet_core_opaque_ptr_t lcbevl_c_next;
                   // Pointer to the next element in the consumer address space.
            c2_lnet_core_opaque_ptr_t lcbevl_p_next;
                   // Pointer to the next element in the producer address space.
   };
   @endcode

   When the producer performs a bev_cqueue_put() call, internally, this call
   uses c2_lnet_core_bev_link::lcbevl_p_next to refer to the next element.
   Similarly, when the consumer performs a bev_cqueue_get() call, internall,
   this call uses c2_lnet_core_bev_link::lcbevl_c_next.  Note that only
   allocation, discussed below, modifies any of these pointers.  Steady-state
   operations on the queue only modify the @c consumer and @c producer pointers.

   @subsection cqueueDLD-lspec-qalloc Circular Queue Allocation

   The circular queue must contain at least 2 elements, as discussed above.
   Additional elements can be added to maintain the requirement that the number
   of elements in the queue equals or exceeds the number of pending buffer
   operations, plus one element for the most recently consumed operation.

   The initial condition is shown below.  In this diagram, the queue is empty
   (see the state discussion, below).  There is room in the queue for one
   pending buffer event and one completed/consumed event.

   @dot
   digraph {
   {
       rank=same;
       node [shape=plaintext];
       c2_lnet_core_bev_cqueue;
       node [shape=record];
       struct1 [label="<f0> consumer|<f1> producer"];
   }
   {
       rank=same;
       ordering=out;
       node [shape=plaintext];
       "element list";
       node1 [shape=box];
       node2 [shape=box];
       "element list" -> node1 [style=invis];
       node1 -> node2 [label=next];
       node2 -> node1 [label=next];
   }
   c2_lnet_core_bev_cqueue -> "element list" [style=invis];
   struct1:f0 -> node1;
   struct1:f1 -> node2;
   }
   @enddot

   Before adding additional elements, the following are true:
   - The number of elements in the queue, N, equals the number of pending
   operations plus one for the most recently consumed operation completion
   event.
   - The producer produces one event per pending operation.
   - The producer will never catch up with the consumer.  Given the required
   number of elements, the producer will run out of work to do when it has
   generated one event for each buffer operation, resulting in a state where
   producer == consumer.

   This means the queue can be expanded at the location of the consumer without
   affecting the producer.  Elements are added as follows:

   -# allocate and initialise a new queue element (referred to as newnode)
   -# Set newnode->next = consumer->next
   -# Set consumer->next = newnode
   -# set consumer = newnode

   Steps 2-4 are performed in bev_cqueue_add().  Because several pointers need
   to be updated, simple atomic operations are insufficent.  Thus, the transport
   layer must synchronise calls to bev_cqueue_add() and bev_cqueue_get(),
   because both calls affect the consumer.  Given that bev_cqueue_add()
   completes its three operations before returning, and bev_cqueue_add() is
   called before the new buffer is added to the queue, there is no way the
   producer will try to generate an event and move its pointer forward until
   bev_cqueue_add() completes.  This allows the transport layer and core layer
   to continue interact only using atomic operations.

   A dragramatic view of these steps is shown below.  The dotted arrows signify
   the pointers before the new node is added.  The Step numbers correspond to
   steps 2-4 above.
   @dot
   digraph {
   {
       rank=same;
       node [shape=plaintext];
       c2_lnet_core_bev_cqueue;
       node [shape=record];
       struct1 [label="<f0> consumer|<f1> producer"];
   }
   newnode [shape=box];
   struct1:f0 -> newnode [label="(4)"];
   node1 -> newnode [label="next (3)"]
   newnode -> node2 [label="next (2)"]
   {
       rank=same;
       ordering=out;
       "element list" [shape=plaintext];
       node1 [shape=box];
       node2 [shape=box];
       node1 -> node2 [style=dotted];
       node2 -> node1 [label=next];
   }
   c2_lnet_core_bev_cqueue -> "element list" [style=invis];
   struct1:f0 -> node1 [style=dotted];
   struct1:f1 -> node2;
   }
   @enddot

   Once again, updating the @c next pointer is less straight forward than the
   diagram suggests.  In step 1, the node is allocated by the transport layer.
   Once allocated, initialisation includes the transport layer setting the
   c2_lnet_core_bev_link::lcbevl_c_self pointer to point at the node and having
   the core layer "bless" the node by setting the
   c2_lnet_core_bev_link::lcbevl_p_self link.  After the self pointers are set,
   the next pointers can be set by using these self pointers.  Since allocation
   occurs in the transport address space, the allocation logic uses the
   c2_lnet_core_bev_link::lcbevl_c_next pointers of the existing nodes for
   navigation, and sets both the @c lcbevl_c_next and
   c2_lnet_core_bev_link::lcbevl_p_next pointers.  The @c lcbevl_p_next pointer
   is set by using the @c lcbevl_c_next->lcbevl_p_self value, which is treated
   opaquely by the transport layer.  So, steps 2 and 3 update both pairs of
   pointers.  Allocation has no affect on the @c producer pointer itself, only
   the @c consumer pointer.

   @subsection cqueueDLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   The circular queue can be in one of 3 states:
   - empty: This is the initial state and the queue returns to this state
   whenever @code consumer->next == producer @endcode
   - full: The queue contains elements and has no room for more. In this state,
   the producer should not attempt to put any more elements into the queue.
   This state can be expressed as @code producer == consumer @endcode
   - partial: In this state, the queue contains elements to be consumed and
   still has room for additional element production. This can be expressed as
   @code consumer->next != producer && consumer != producer @endcode

   @subsection cqueueDLD-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   A single producer and consumer are supported.  Atomic variables,
   @c consumer and @c producer, represent the range of elements in the queue
   containing data.  Because these pointers are atomic, no locking is needed
   to access them by a single producer and consumer.  Multiple producers
   and/or consumers must synchronize externally.

   The transport layer acts both as the consumer and the allocator, and both
   operations use and modify the @c consumer variable and related pointers.  As
   such, calls to bev_cqueue_add() and bev_cqueue_get() must be synchronised.
   The transport layer holds the transfer machine c2_net_transfer_mc::ntm_mutex
   when it calls bev_cqueue_add().  The transport layer will also hold this
   mutex when it calls bev_cqueue_get().

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

   - <b>i.c2.lib.atomic.interoperable-kernel-user-support</b> The
   c2_lnet_core_bev_link data structure allows for tracking the pointers to the
   link in both address spaces.  The atomic operations allow the FIFO to be
   produced and consumed simultaneously in both spaces without synchronization
   or context switches.

   - <b>i.net.xprt.lnet.growable-event-queue</b> The implementation supports
   an event queue to which new elements can be added over time.

   <hr>
   @section cqueueDLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   The following cases will be tested by unit tests:
   - initialising a queue of minimum size 2
   - successfully producing an element in a slot
   - successfully consuming an element from a slot
   - failing to consume a slot because the queue is empty
   - initialising a queue of larger size
   - repeating the producing and consuming tests
   - concurrently producing and consuming elements

   <hr>
   @section cqueueDLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   System testing will include tests where the producer and consumer are
   in separate address spaces.

   <hr>
   @section cqueueDLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   The circular queue (the struct c2_lnet_core_bev_cqueue) consumes fixed size
   memory, independent of the size of the elements contains the queue's data.
   The number of elements can grow over time, where the number of elements is
   proportional to the number of current and outstanding buffer operations.
   This number of elements will reach some maximum based on the peak activity in
   the application layer.  Operations on the queue are O(1) complexity.

   <hr>
   @section cqueueDLD-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>

 */

/**
   @page cqueueDLD-fspec LNet Buffer Event Queue Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref cqueueDLD-fspec-ds
   - @ref cqueueDLD-fspec-sub
   - @ref cqueueDLD-fspec-usecases
   - @ref bevcqueue "Detailed Functional Specification" <!-- Note link -->

   @section cqueueDLD-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   The circular queue is defined by the @a c2_lnet_core_bev_cqueue data
   structure.

   @section cqueueDLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Subroutines are provided to:
   - initialise and finalise the c2_lnet_core_bev_cqueue
   - produce and consume slots in the queue

   @see @ref cqueue "Detailed Functional Specification"

   @section cqueueDLD-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   The c2_lnet_core_bev_cqueue provides atomic access to the producer and
   consumer elements in the circular queue.

   In addition, semaphores or other synchronization mechanisms can be used to
   notify the producer or consumer when the queue changes, eg. when it becomes
   not empty.

   @subsection cq-init Initialisation

   The circular queue is initialised as follows:

   @code
   struct c2_lnet_core_buffer_event *e1;
   struct c2_lnet_core_buffer_event *e2;
   struct c2_lnet_core_bev_cqueue myqueue;

   C2_ALLOC_PTR(e1);
   C2_ALLOC_PTR(e2);
   bev_cqueue_init(&myqueue, &e1->lcbe_tm_link, &e2->lcbe_tm_link);
   @endcode

   @subsection cq-allocator Allocator

   The event queue can be expanded to make room for additional buffer events.
   This should be performed before buffers are queued.  One element should exist
   on the event queue for each expected buffer operation, plus one additional
   element for the "current" buffer operation.

   @code
   size_t needed;
   struct c2_lnet_core_buffer_event *el;

   while (needed > bev_cqueue_size(&myqueue)) {
       C2_ALLOC_PTR(el);
       ... ; // initialize the new element for both address spaces
       bev_cqueue_add(&myqueue, el);
   }
   @endcode

   @subsection cq-producer Producer

   A producer works in a loop, putting event notifications in the queue:

   @code
   bool done;
   struct c2_lnet_core_bev_link *ql;
   struct c2_lnet_core_buffer_event *el;

   while (!done) {
       ql = bev_cqueue_pnext(&myqueue);
       el = container_of(ql, struct c2_lnet_core_buffer_event, lcbe_tm_link);
       ... ; // initialize the element
       bev_cqueue_put(&myqueue);
       ... ; // notify blocked consumer that data is available
   }
   @endcode

   @subsection cq-consumer Consumer

   A consumer works in a loop, consuming data from the queue:

   @code
   bool done;
   struct c2_lnet_core_bev_link *ql;
   struct c2_lnet_core_buffer_event *el;

   while (!done) {
       ql = bev_cqueue_get(&myqueue);
       if (ql == NULL) {
           ... ; // block until data is available
           continue;
       }

       el = container_of(ql, struct c2_lnet_core_buffer_event, lcbe_tm_link);
       ... ; // operate on the current element
   }
   @endcode

   @see @ref bevcqueue "Detailed Functional Specification"
 */

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
static bool bev_cqueue_invariant(const struct c2_lnet_core_bev_cqueue *q);

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
   @note this operation is to be used only by the consumer.  The data structures
   do not provide a pointer to the consumer element from the producer's
   perspective.
 */
static bool bev_cqueue_is_empty(const struct c2_lnet_core_bev_cqueue *q);

/**
   Return total size of the event queue, including in-use and free elements.
 */
static size_t bev_cqueue_size(const struct c2_lnet_core_bev_cqueue *q);

/**
   Get the oldest element in the FIFO circular queue, advancing the divider.
   @param q the queue
   @returns the link to the element in the consumer context,
   NULL when the queue is empty
 */
static struct c2_lnet_core_bev_link *bev_cqueue_get(
					    struct c2_lnet_core_bev_cqueue *q);

/**
   Determine the next element in the queue that can be used by the producer.
   @param q the queue
   @returns a pointer to the next available element in the producer context
   @pre q->lcbevq_producer->lcbevl_c_self != q->lcbevq_consumer
 */
static struct c2_lnet_core_bev_link* bev_cqueue_pnext(
				      const struct c2_lnet_core_bev_cqueue *q);

/**
   Put (produce) an element so it can be consumed.  The caller must first
   call bev_cqueue_pnext() to ensure such an element exists.
   @param q the queue
   @pre q->lcbevq_producer->lcbevl_c_self != q->lcbevq_consumer
 */
static void bev_cqueue_put(struct c2_lnet_core_bev_cqueue *q);

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
