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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/07/2011
 */

#pragma once

#ifndef __COLIBRI_LIB_QUEUE_H__
#define __COLIBRI_LIB_QUEUE_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup queue Queue

   FIFO queue. Should be pretty self-explanatory.

   @{
 */

struct c2_queue_link;

/**
   A queue of elements.
 */
struct c2_queue {
	/** Oldest element in the queue (first to be returned). */
	struct c2_queue_link *q_head;
	/** Youngest (last added) element in the queue. */
	struct c2_queue_link *q_tail;
};

/**
   An element in a queue.
 */
struct c2_queue_link {
	struct c2_queue_link *ql_next;
};

/**
   Static queue initializer. Assign this to a variable of type struct c2_queue
   to initialize empty queue.
 */
extern const struct c2_queue C2_QUEUE_INIT;

void c2_queue_init(struct c2_queue *q);
void c2_queue_fini(struct c2_queue *q);
bool c2_queue_is_empty(const struct c2_queue *q);

void c2_queue_link_init (struct c2_queue_link *ql);
void c2_queue_link_fini (struct c2_queue_link *ql);
bool c2_queue_link_is_in(const struct c2_queue_link *ql);
bool c2_queue_contains  (const struct c2_queue *q,
			 const struct c2_queue_link *ql);
size_t c2_queue_length(const struct c2_queue *q);

/**
   Returns queue head or NULL if queue is empty.
 */
struct c2_queue_link *c2_queue_get(struct c2_queue *q);
void c2_queue_put(struct c2_queue *q, struct c2_queue_link *ql);

bool c2_queue_invariant(const struct c2_queue *q);

/** @} end of queue group */


/* __COLIBRI_LIB_QUEUE_H__ */
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
