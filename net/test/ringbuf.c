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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 07/02/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/errno.h"	/* ENOMEM */
#include "lib/misc.h"	/* C2_SET0 */
#include "lib/memory.h"	/* C2_ALLOC_ARR */

#include "net/test/ringbuf.h"

int c2_net_test_ringbuf_init(struct c2_net_test_ringbuf *rb, size_t size)
{
	C2_PRE(rb != NULL);
	C2_PRE(size != 0);

	rb->ntr_size = size;
	c2_atomic64_set(&rb->ntr_start, 0);
	c2_atomic64_set(&rb->ntr_end, 0);
	C2_ALLOC_ARR(rb->ntr_buf, rb->ntr_size);

	if (rb->ntr_buf != NULL)
		C2_ASSERT(c2_net_test_ringbuf_invariant(rb));

	return rb->ntr_buf == NULL ? -ENOMEM : 0;
}

void c2_net_test_ringbuf_fini(struct c2_net_test_ringbuf *rb)
{
	C2_PRE(c2_net_test_ringbuf_invariant(rb));

	c2_free(rb->ntr_buf);
	C2_SET0(rb);
}

bool c2_net_test_ringbuf_invariant(const struct c2_net_test_ringbuf *rb)
{
	int64_t start;
	int64_t end;

	if (rb == NULL || rb->ntr_buf == NULL)
		return false;

	start = c2_atomic64_get(&rb->ntr_start);
	end   = c2_atomic64_get(&rb->ntr_end);
	if (start > end)
		return false;
	if (end - start > rb->ntr_size)
		return false;
	return true;
}

void c2_net_test_ringbuf_push(struct c2_net_test_ringbuf *rb, size_t value)
{
	int64_t index;

	C2_PRE(c2_net_test_ringbuf_invariant(rb));
	index = c2_atomic64_add_return(&rb->ntr_end, 1) - 1;
	C2_ASSERT(c2_net_test_ringbuf_invariant(rb));

	rb->ntr_buf[index % rb->ntr_size] = value;
}

size_t c2_net_test_ringbuf_pop(struct c2_net_test_ringbuf *rb)
{
	int64_t index;

	C2_PRE(c2_net_test_ringbuf_invariant(rb));
	index = c2_atomic64_add_return(&rb->ntr_start, 1) - 1;
	C2_ASSERT(c2_net_test_ringbuf_invariant(rb));

	return rb->ntr_buf[index % rb->ntr_size];
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
