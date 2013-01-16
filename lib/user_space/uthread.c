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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>,
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 02/18/2011
 */

#include <stdlib.h>     /* getenv */
#include <unistd.h>     /* getpid */
#include <errno.h>      /* program_invocation_name */
#include <stdio.h>      /* snprinf */

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/thread.h"
#include "lib/arith.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

/**
   @addtogroup thread Thread

   Implementation of m0_thread on top of pthread_t.

   <b>Implementation notes</b>

   Instead of creating a new POSIX thread executing user-supplied function, all
   threads start executing the same trampoline function m0_thread_trampoline()
   that performs some generic book-keeping.

   Threads are created with a PTHREAD_CREATE_JOINABLE attribute.

   @{
 */

/**
 * Thread specific data.
 */
struct uthread_specific_data {
	/** Flag to indicate thread is in awkward context. */
	bool tsd_is_awkward;
};

static pthread_attr_t pthread_attr_default;
static pthread_key_t pthread_data_key;

/**
 * Initialize thread specific data.
 */
M0_INTERNAL int uthread_specific_data_init(void)
{
	struct uthread_specific_data *ptr;

	M0_ALLOC_PTR(ptr);
	if (ptr == NULL)
		return -ENOMEM;

	ptr->tsd_is_awkward = false;
	return -pthread_setspecific(pthread_data_key, ptr);
}

/**
 * Finalise thread specific data.
 */
M0_INTERNAL void uthread_specific_data_fini(void)
{
	struct uthread_specific_data *ptr;

	ptr = pthread_getspecific(pthread_data_key);
	pthread_setspecific(pthread_data_key, NULL);
	m0_free(ptr);
}

/*
 * Used to initialize user thread specific data.
 */
static void *uthread_trampoline(void *arg)
{
	struct m0_thread	     *t = arg;

	t->t_initrc = uthread_specific_data_init();
	if (t->t_initrc == 0) {
		m0_thread_trampoline(arg);
		uthread_specific_data_fini();
	}

	return NULL;
}

M0_INTERNAL int m0_thread_init_impl(struct m0_thread *q, const char *namebuf)
{
	M0_PRE(q->t_state == TS_RUNNING);

	return -pthread_create(&q->t_h.h_id, &pthread_attr_default,
			       uthread_trampoline, q);
}

int m0_thread_join(struct m0_thread *q)
{
	int result;

	M0_PRE(q->t_state == TS_RUNNING);
	M0_PRE(!pthread_equal(q->t_h.h_id, pthread_self()));
	result = -pthread_join(q->t_h.h_id, NULL);
	if (result == 0)
		q->t_state = TS_PARKED;
	return result;
}

M0_INTERNAL int m0_thread_signal(struct m0_thread *q, int sig)
{
	return -pthread_kill(q->t_h.h_id, sig);
}

M0_INTERNAL int m0_thread_confine(struct m0_thread *q,
				  const struct m0_bitmap *processors)
{
	size_t    idx;
	size_t    nr_bits = min64u(processors->b_nr, CPU_SETSIZE);
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);

	for (idx = 0; idx < nr_bits; ++idx) {
		if (m0_bitmap_get(processors, idx))
			CPU_SET(idx, &cpuset);
	}

	return -pthread_setaffinity_np(q->t_h.h_id, sizeof cpuset, &cpuset);
}

M0_INTERNAL char *m0_debugger_args[4] = {
	NULL, /* debugger name */
	NULL, /* our binary name */
	NULL, /* our process id */
	NULL
};

M0_INTERNAL int m0_threads_init(void)
{
	int         result;
	static char pidbuf[20];
	static char debugger_buf[20];

	m0_debugger_args[0] = debugger_buf;
	strcpy(debugger_buf, getenv("MERO_DEBUGGER"));
	/*
	 * Note: program_invocation_name requires _GNU_SOURCE.
	 */
	m0_debugger_args[1] = program_invocation_name;
	m0_debugger_args[2] = pidbuf;
	snprintf(pidbuf, ARRAY_SIZE(pidbuf), "%i", getpid());

	result = -pthread_attr_init(&pthread_attr_default);
	if (result != 0)
		return result;

	result = -pthread_attr_setdetachstate(&pthread_attr_default,
					      PTHREAD_CREATE_JOINABLE);
	if (result != 0)
		return result;

	/* Generate key for thread specific data. */
	result = -pthread_key_create(&pthread_data_key, NULL);
	if (result != 0) {
		pthread_attr_destroy(&pthread_attr_default);
		return result;
	}

	return uthread_specific_data_init();
}

M0_INTERNAL void m0_threads_fini(void)
{
	pthread_attr_destroy(&pthread_attr_default);
	uthread_specific_data_fini();
	pthread_key_delete(pthread_data_key);
}

M0_INTERNAL void m0_thread_self(struct m0_thread_handle *id)
{
	id->h_id = pthread_self();
}

M0_INTERNAL bool m0_thread_handle_eq(struct m0_thread_handle *h1,
				     struct m0_thread_handle *h2)
{
	return h1->h_id == h2->h_id;
}

M0_INTERNAL void m0_enter_awkward(void)
{
	struct uthread_specific_data *ptr;

	ptr = pthread_getspecific(pthread_data_key);
	M0_ASSERT(ptr != NULL);

	ptr->tsd_is_awkward = true;
}

M0_INTERNAL void m0_exit_awkward(void)
{
	struct uthread_specific_data *ptr;

	ptr = pthread_getspecific(pthread_data_key);
	M0_ASSERT(ptr != NULL);

	ptr->tsd_is_awkward = false;
}

M0_INTERNAL bool m0_is_awkward(void)
{
	struct uthread_specific_data *ptr;

	ptr = pthread_getspecific(pthread_data_key);
	M0_ASSERT(ptr != NULL);

	return ptr->tsd_is_awkward;
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
