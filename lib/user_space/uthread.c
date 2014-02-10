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

#include <stdlib.h>      /* getenv */
#include <unistd.h>      /* getpid */
#include <errno.h>       /* program_invocation_name */
#include <stdio.h>       /* snprinf */

#include "lib/misc.h"    /* M0_SET0 */
#include "lib/string.h"  /* m0_strdup */
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

static pthread_key_t  tls_key;
static pthread_attr_t pthread_attr_default;
static struct m0     *instance_0;

M0_INTERNAL struct m0_thread_tls *m0_thread_tls(void)
{
	return pthread_getspecific(tls_key);
}

M0_INTERNAL void m0_threads_set_instance(struct m0 *instance)
{
	M0_PRE(instance_0 == NULL);
	M0_PRE(instance != NULL);

	instance_0 = instance;
}

static void *uthread_trampoline(void *arg)
{
	struct m0_thread *t = arg;

	M0_PRE(pthread_getspecific(tls_key) == NULL);

	t->t_initrc = -pthread_setspecific(tls_key, &t->t_tls);
	if (t->t_initrc == 0) {
		m0_thread_trampoline(arg);
		(void)pthread_setspecific(tls_key, NULL);
	}
	return NULL;
}

M0_INTERNAL int m0_thread_init_impl(struct m0_thread *q, const char *_)
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
	static struct m0_thread main_thread;
	static char             pidbuf[20];

	char *env_ptr;
	int   result;

	env_ptr = getenv("MERO_DEBUGGER");
	if (env_ptr != NULL)
		m0_debugger_args[0] = m0_strdup(env_ptr);
	/*
	 * Note: program_invocation_name requires _GNU_SOURCE.
	 */
	m0_debugger_args[1] = program_invocation_name;
	m0_debugger_args[2] = pidbuf;
	result = snprintf(pidbuf, ARRAY_SIZE(pidbuf), "%i", getpid());
	M0_ASSERT(result < ARRAY_SIZE(pidbuf));

	result = -pthread_attr_init(&pthread_attr_default) ?:
		 -pthread_attr_setdetachstate(&pthread_attr_default,
					      PTHREAD_CREATE_JOINABLE);
	if (result != 0)
		goto err_0;

	result = -pthread_key_create(&tls_key, NULL);
	if (result != 0)
		goto err_1;

	result = -pthread_setspecific(tls_key, &main_thread.t_tls);
	if (result == 0) {
		M0_ASSERT(instance_0 != NULL);
		main_thread.t_tls.tls_m0_instance = instance_0;
		return 0;
	}

	(void)pthread_key_delete(tls_key);
err_1:
	(void)pthread_attr_destroy(&pthread_attr_default);
err_0:
	m0_free0(&m0_debugger_args[0]); /* harmless even if env_ptr == NULL */
	return result;
}

M0_INTERNAL void m0_threads_fini(void)
{
	(void)pthread_key_delete(tls_key);
	(void)pthread_attr_destroy(&pthread_attr_default);
	m0_free0(&m0_debugger_args[0]);
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
	m0_thread_tls()->tls_is_awkward = true;
}

M0_INTERNAL void m0_exit_awkward(void)
{
	m0_thread_tls()->tls_is_awkward = false;
}

M0_INTERNAL bool m0_is_awkward(void)
{
	return m0_thread_tls()->tls_is_awkward;
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
