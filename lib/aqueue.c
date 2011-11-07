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

#include "lib/aqueue.h"
#include "lib/assert.h"

#include <stdlib.h>
#include <stdio.h>

/**
   Implemented algorithm from PODC'96.
 */

#define EOQ ((struct c2_aqueue_link *)7)

void aptr_printf(char *descr, struct c2_aptr *ap)
{
	printf("%s, ptr = %p, count = %lu\n", descr, c2_aptr_ptr(ap), c2_aptr_count(ap));
}

void c2_aqueue_init(struct c2_aqueue *q)
{
	c2_aptr_init(&q->aq_head);
	c2_aptr_init(&q->aq_tail);
	c2_aqueue_link_init(&q->aq_stub);
	c2_aptr_set(&q->aq_head, &q->aq_stub, 0);
	c2_aptr_set(&q->aq_tail, &q->aq_stub, 0);
}

void c2_aqueue_fini(struct c2_aqueue *q)
{
	c2_aptr_fini(&q->aq_head);
	c2_aptr_fini(&q->aq_tail);
	c2_aqueue_link_fini(&q->aq_stub);
}

void c2_aqueue_link_init(struct c2_aqueue_link *ql)
{
	c2_aptr_init(&ql->aql_next);
	c2_aptr_set(&ql->aql_next, EOQ, 0);
}

void c2_aqueue_link_fini(struct c2_aqueue_link *ql)
{
	c2_aptr_fini(&ql->aql_next);
}

struct c2_aqueue_link *c2_aqueue_get(struct c2_aqueue *q)
{
	struct c2_aptr head;
	struct c2_aptr tail;
	struct c2_aptr next;
	struct c2_aqueue_link *head_ptr;
	struct c2_aqueue_link *tail_ptr;
	struct c2_aqueue_link *next_ptr;

	while (1) {
		c2_aptr_copy(&head, &q->aq_head);
		head_ptr = c2_aptr_ptr(&head);
		c2_aptr_copy(&tail, &q->aq_tail);
		tail_ptr = c2_aptr_ptr(&tail);
		c2_aptr_copy(&next, &head_ptr->aql_next);
		next_ptr = c2_aptr_ptr(&next);

		if (!c2_aptr_eq(&head, &q->aq_head))
			continue;
		if (head_ptr == tail_ptr) {
			if (next_ptr == EOQ)
				return NULL;
			c2_aptr_cas(&q->aq_tail, &tail,
					next_ptr,
					c2_aptr_count(&tail) + 1);
		} else {
			if (c2_aptr_cas(&q->aq_head, &head,
					next_ptr,
					c2_aptr_count(&head) + 1))
				break;
		}
	}

	return next_ptr;
}

void c2_aqueue_put(struct c2_aqueue *q, struct c2_aqueue_link *node)
{
	struct c2_aptr tail;
	struct c2_aptr next;
	struct c2_aqueue_link *tail_ptr;

	C2_ASSERT(c2_aptr_ptr(&node->aql_next) == EOQ);

	while (1) {
		c2_aptr_copy(&tail, &q->aq_tail);
		tail_ptr = c2_aptr_ptr(&tail);
		c2_aptr_copy(&next, &tail_ptr->aql_next);
		if (!c2_aptr_eq(&tail, &q->aq_tail))
			continue;
		if (c2_aptr_ptr(&next) == EOQ) {
			if (c2_aptr_cas(&tail_ptr->aql_next, &next,
					&node->aql_next,
					c2_aptr_count(&next) + 1))
				break;
		} else {
			c2_aptr_cas(&q->aq_tail, &tail,
					c2_aptr_ptr(&next),
					c2_aptr_count(&tail) + 1);
		}
	}
	c2_aptr_cas(&q->aq_tail, &tail,
			&node->aql_next,
			c2_aptr_count(&tail) + 1);
}


/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
