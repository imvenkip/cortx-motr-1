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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 07/02/2012
 */

#ifndef __NET_TEST_RINGBUF_H__
#define __NET_TEST_RINGBUF_H__

#include "lib/types.h"	/* size_t */
#include "lib/atomic.h"	/* c2_atomic64 */

/**
   Circular FIFO buffer with size_t elements.
   @note c2_net_test_ringbuf.ntr_start and c2_net_test_ringbuf.ntr_end are
   absolute indices.
 */
struct c2_net_test_ringbuf {
	size_t		    ntr_size;	/**< Number of elements in ringbuf */
	size_t		   *ntr_buf;	/**< Ringbuf array */
	struct c2_atomic64  ntr_start;	/**< Start pointer */
	struct c2_atomic64  ntr_end;	/**< End pointer */
};

/**
   Initialize ring buffer.
   @param rb ring buffer
   @param size maximum number of elements.
 */
int c2_net_test_ringbuf_init(struct c2_net_test_ringbuf *rb, size_t size);
/**
   Finalize ring buffer.
   @pre c2_net_test_ringbuf_invariant(rb)
 */
void c2_net_test_ringbuf_fini(struct c2_net_test_ringbuf *rb);
bool c2_net_test_ringbuf_invariant(const struct c2_net_test_ringbuf *rb);

/**
   Put item to the ring buffer.
   @pre c2_net_test_ringbuf_invariant(rb)
   @post c2_net_test_ringbuf_invariant(rb)
 */
void c2_net_test_ringbuf_put(struct c2_net_test_ringbuf *rb, size_t value);
/**
   Get item from the ring buffer.
   @pre c2_net_test_ringbuf_invariant(rb)
   @post c2_net_test_ringbuf_invariant(rb)
 */
size_t c2_net_test_ringbuf_get(struct c2_net_test_ringbuf *rb);

#endif /*  __NET_TEST_RINGBUF_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
