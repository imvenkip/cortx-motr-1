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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 05/01/2010
 */

#ifndef __KERNEL__
#include <stdarg.h>
#include <stdio.h>	/* vsnprintf */
#endif

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/thread.h"

/**
   @addtogroup thread Thread

   Common m0_thread implementation.

   @{
 */

int m0_thread_init(struct m0_thread *q, int (*init)(void *),
		   void (*func)(void *), void *arg, const char *name, ...)
{
	int     result;
	char    namebuf[M0_THREAD_NAME_LEN];
	va_list varargs;

	M0_PRE(q->t_func == NULL);
	M0_PRE(q->t_state == TS_PARKED);

	q->t_state = TS_RUNNING;
	q->t_init  = init;
	q->t_func  = func;
	q->t_arg   = arg;
	m0_semaphore_init(&q->t_wait, 0);
	va_start(varargs, name);
	vsnprintf(namebuf, sizeof namebuf, name, varargs);
	va_end(varargs);

	result = m0_thread_init_impl(q, namebuf);
	if (result == 0 && q->t_init != NULL) {
		m0_semaphore_down(&q->t_wait);
		result = q->t_initrc;
		if (result != 0)
			m0_thread_join(q);
	}
	if (result != 0)
		q->t_state = TS_PARKED;
	return result;
}
M0_EXPORTED(m0_thread_init);

M0_INTERNAL void *m0_thread_trampoline(void *arg)
{
	struct m0_thread *t = arg;

	M0_ASSERT(t->t_state == TS_RUNNING);
	M0_ASSERT(t->t_initrc == 0);

	if (t->t_init != NULL) {
		t->t_initrc = t->t_init(t->t_arg);
		m0_semaphore_up(&t->t_wait);
	}
	if (t->t_initrc == 0)
		t->t_func(t->t_arg);
	return NULL;
}

void m0_thread_fini(struct m0_thread *q)
{
	M0_PRE(q->t_state == TS_PARKED);
	m0_semaphore_fini(&q->t_wait);
	M0_SET0(q);
}
M0_EXPORTED(m0_thread_fini);

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
