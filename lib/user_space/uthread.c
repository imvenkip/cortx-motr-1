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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>,
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 02/18/2011
 */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/thread.h"
#include "lib/arith.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

/**
   @addtogroup thread Thread

   Implementation of c2_thread on top of pthread_t.

   <b>Implementation notes</b>

   Instead of creating a new POSIX thread executing user-supplied function, all
   threads start executing the same trampoline function c2_thread_trampoline()
   that performs some generic book-keeping.

   Threads are created with a PTHREAD_CREATE_JOINABLE attribute.

   @{
 */

static pthread_attr_t pthread_attr_default;

int c2_thread_init_impl(struct c2_thread *q, const char *namebuf)
{
	C2_PRE(q->t_state == TS_RUNNING);

	return -pthread_create(&q->t_h.h_id, &pthread_attr_default,
			       c2_thread_trampoline, q);
}

int c2_thread_join(struct c2_thread *q)
{
	int result;

	C2_PRE(q->t_state == TS_RUNNING);
	C2_PRE(!pthread_equal(q->t_h.h_id, pthread_self()));
	result = -pthread_join(q->t_h.h_id, NULL);
	if (result == 0)
		q->t_state = TS_PARKED;
	return result;
}

int c2_thread_signal(struct c2_thread *q, int sig)
{
	return pthread_kill(q->t_h.h_id, sig);
}

int c2_thread_confine(struct c2_thread *q, const struct c2_bitmap *processors)
{
	size_t    idx;
	size_t    nr_bits = min64u(processors->b_nr, CPU_SETSIZE);
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);

	for (idx = 0; idx < nr_bits; ++idx) {
		if (c2_bitmap_get(processors, idx))
			CPU_SET(idx, &cpuset);
	}

	return -pthread_setaffinity_np(q->t_h.h_id, sizeof cpuset, &cpuset);
}

int c2_threads_init(void)
{
	int result;

	result = -pthread_attr_init(&pthread_attr_default);
	if (result == 0)
		result = -pthread_attr_setdetachstate(&pthread_attr_default,
						      PTHREAD_CREATE_JOINABLE);
	return result;
}

void c2_threads_fini(void)
{
	pthread_attr_destroy(&pthread_attr_default);
}

void c2_thread_self(struct c2_thread_handle *id)
{
	id->h_id = pthread_self();
}

bool c2_thread_handle_eq(struct c2_thread_handle *h1,
			 struct c2_thread_handle *h2)
{
	return h1->h_id == h2->h_id;
}

/** @} end of thread group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
