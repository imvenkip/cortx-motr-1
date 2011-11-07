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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 10/29/2011
 */

#ifndef __COLIBRI_LIB_AQUEUE_H__
#define __COLIBRI_LIB_AQUEUE_H__

#include "lib/aptr.h"

struct c2_aqueue_link {
	struct c2_aptr aql_next;
};

/**
   Lock-free thread-safe async-signal-safe FIFO MPMC queue.
   Using cmpxchg16b for atomicity (c2_aptr).
 */
struct c2_aqueue {
	struct c2_aptr aq_head;
	struct c2_aptr aq_tail;
	struct c2_aqueue_link aq_stub;
};

void c2_aqueue_init(struct c2_aqueue *q);
void c2_aqueue_fini(struct c2_aqueue *q);

void c2_aqueue_link_init(struct c2_aqueue_link *ql);
void c2_aqueue_link_fini(struct c2_aqueue_link *ql);

/**
   Returns queue head or NULL if queue is empty.
   Removes item from queue.
 */
struct c2_aqueue_link *c2_aqueue_get(struct c2_aqueue *q);

/**
   Add item to queue.
 */
void c2_aqueue_put(struct c2_aqueue *q, struct c2_aqueue_link *node);

/* __COLIBRI_LIB_AQUEUE_H__ */
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
