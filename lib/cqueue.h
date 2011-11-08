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

#ifndef __COLIBRI_LIB_CQUEUE_H__
#define __COLIBRI_LIB_CQUEUE_H__

#include "atomic.h"

/**
   @page cqueueDLD-fspec Circular Queue Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref cqueueDLD-fspec-ds
   - @ref cqueueDLD-fspec-sub
   - @ref cqueueDLD-fspec-usecases
   - @ref cqueue "Detailed Functional Specification" <!-- Note link -->

   @section cqueueDLD-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   The circular queue is defined by the @a c2_cqueue data structure.

   @section cqueueDLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Subroutines are provided to:
   - initialise and finalise the c2_cqueue
   - produce and consume slots in the queue

   @see @ref cqueue "Detailed Functional Specification"

   @section cqueueDLD-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   The c2_cqueue provides atomic access to the producer and consumer
   slots in the circular queue.  It does not include the array of queue elements
   itself.  The intended use is to embed the c2_cqueue within another
   data structure that contains the array of slots, for example:

   @code
   struct event {
           ...
   };
   struct event_queue {
           struct c2_cqueue eq_header;
	   struct event eq_event[EVENT_QUEUE_NR];
   };
   @endcode

   In addition, semaphores or other synchronization mechanisms can be used to
   notify the producer or consumer when the queue changes, eg. when it becomes
   not empty or not full.

   @subsection cq-init Initialisation

   The circular queue is initialised as follows:

   @code
   struct event_queue myqueue;

   c2_cqueue_init(&myqueue.eq_header, EVENT_QUEUE_NR);
   @endcode

   @subsection cq-producer Producer

   A producer works in a loop, adding data to the queue:

   @code
   bool done;
   ssize_t i;
   while (!done) {
       i = c2_cqueue_pnext(&myqueue.eq_header);
       if (i == -ENOENT) {
           // block until space is available
       } else {
           myqueue.eq_event[i] = ...;
	   c2_cqueue_produce(&myqueue.eq_header);
	   // notify blocked consumer that data is available
       }
   }
   @endcode

   @subsection cq-consumer Consumer

   A consumer works in a loop, consuming data from the queue:

   @code
   bool done;
   ssize_t i;
   while (!done) {
       i = c2_cqueue_consume(&myqueue.eq_header);
       if (i == -ENOENT) {
           // block until data is available
       } else {
           ... = myqueue.eq_event[i];
	   // notify blocked producer that space is available
       }
   }
   @endcode

   @see @ref cqueue "Detailed Functional Specification"
 */

/**
   @defgroup cqueue Circular Queue
   @brief Detailed functional specification for a circular queue.

   @see @ref cqueueDLD Circular Queue DLD

   @{
*/

enum {
	C2_CQUEUE_MAGIC    = 0x2d85d3689fb204b3ULL,
};
/**
   The circular queue.  Only the size of the queue and the producer and consumer
   slots are tracked here.  The queue itself is maintained by the application in
   an associated array.
   @see @ref cqueueDLD-fspec-usecases Recipes
 */
struct c2_cqueue {
	/** Magic constant to validate object */
	uint64_t cq_magic;
	/** The number of slots in the queue */
	size_t cq_size;
	/** Current consumer slot */
	struct c2_atomic64 cq_divider;
	/** Next producer slot */
	struct c2_atomic64 cq_last;
};

/**
   Initialise the circular queue.

   @param q queue to initialise
   @param nr_slots number of slots in the queue
   @pre nr_slots >= 2
 */
void c2_cqueue_init(struct c2_cqueue *q, size_t nr_slots);

/**
   Finalise the circular queue.
 */
void c2_cqueue_fini(struct c2_cqueue *q);

/**
   Test if the circular queue is empty.
 */
bool c2_cqueue_is_empty(struct c2_cqueue *q);

/**
   Test if the circular queue is full.
 */
bool c2_cqueue_is_full(struct c2_cqueue *q);

/**
   Consume the next available slot in the queue.

   @return The slot number (in the range 0..cq_size-1) if there is a slot to
   consume. Otherwise, -ENOENT if there are none currently available to consume.
 */
ssize_t c2_cqueue_consume(struct c2_cqueue *q);

/**
   Get the slot index of the next slot which can be used by the producer.

   @return The slot number (in the range 0..cq_size-1) if there is a producer
   slot available.  Otherwise, -ENOENT if none currently available.
 */
ssize_t c2_cqueue_pnext(struct c2_cqueue *q);

/**
   Produce a slot in the queue.

   @pre (q->cq_last < q->cq_divider) ? (q->cq_last + 1 < q->cq_divider) :
   (q->cq_last + 1 < q->cq_divider + q->cq_size)
 */
void c2_cqueue_produce(struct c2_cqueue *q);

/** @} end of cqueue group */

/* __COLIBRI_LIB_CQUEUE_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
