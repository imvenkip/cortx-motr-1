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

#ifndef __COLIBRI_LIB_CIRCULAR_QUEUE_H__
#define __COLIBRI_LIB_CIRCULAR_QUEUE_H__

#include "atomic.h"

/**
   @page circular_queueDLD-fspec Circular Queue Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref circular_queueDLD-fspec-ds
   - @ref circular_queueDLD-fspec-sub
   - @ref circular_queueDLD-fspec-usecases
   - @ref circular_queue "Detailed Functional Specification" <!-- Note link -->

   @section circular_queueDLD-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   The circular queue is defined by the @a c2_circular_queue data structure.

   @section circular_queueDLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Subroutines are provided to:
   - initialise and finalise the c2_circular_queue
   - produce and consume slots in the queue

   @see @ref circular_queue "Detailed Functional Specification"

   @section circular_queueDLD-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   The c2_circular_queue provides atomic access to the producer and consumer
   slots in the circular queue.  It does not include the array of queue elements
   itself.  The intended use is to embed the c2_circular_queue within another
   data structure that contains the array of slots, for example:

   @code
   struct event {
           ...
   };
   struct event_queue {
           struct c2_circular_queue eq_header;
	   struct event eq_event[EVENT_QUEUE_NR];
   };
   @endcode

   @see @ref circular_queue "Detailed Functional Specification"
 */

/**
   @defgroup circular_queue Circular Queue
   @brief Detailed functional specification for a circular queue.

   @see @ref circular_queueDLD Circular Queue DLD

   @{
*/

/**
   The circular queue.  Only the size of the queue and the producer and consumer
   slots are tracked here.  The queue itself is maintained by the application in
   an associated array.
   @see @ref circular_queueDLD-fspec-usecases Recipes
 */
struct c2_circular_queue {
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
void c2_circular_queue_init(struct c2_circular_queue *q, size_t nr_slots);

/**
   Finalise the circular queue.
 */
void c2_circular_queue_fini(struct c2_circular_queue *q);

/**
   Consume the next available slot in the queue.

   @return the slot number (in the range 0 - cq_size) if there is a slot to
   consume. Otherwise, -ENOENT if there are none currently available to consume.
 */
ssize_t c2_circular_queue_consume(struct c2_circular_queue *q);

/**
   Get the slot index of the next slot which can be used by the producer.

   @return the slot number (in the range 0 - cq_size) if there is a producer
   slot available.  Otherwise, -ENOENT if none currently available.
 */
ssize_t c2_circular_queue_pnext(struct c2_circular_queue *q);

/**
   Produce a slot in the queue.

   @pre (q->cq_last < q->cq_divider) ?
   (q->cq_last + 1 < q->cq_divider) : (q->cq_last + 1 < q->cq_size)
 */
void c2_circular_queue_produce(struct c2_circular_queue *q);

/** @} end of circular_queue group */

/* __COLIBRI_LIB_CIRCULAR_QUEUE_H__ */
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
