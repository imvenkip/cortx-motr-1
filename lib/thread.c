/* -*- C -*- */

#ifndef __KERNEL__
#include <stdarg.h>
#include <stdio.h>	/* vsnprintf */
#endif

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/thread.h"

/**
   @addtogroup thread Thread

   Common c2_thread implementation.

   @{
 */

int c2_thread_init(struct c2_thread *q, int (*init)(void *),
		   void (*func)(void *), void *arg, const char *name, ...)
{
	int     result;
	char    namebuf[C2_THREAD_NAME_LEN];
	va_list varargs;

	C2_PRE(q->t_func == NULL);
	C2_PRE(q->t_state == TS_PARKED);

	q->t_state = TS_RUNNING;
	q->t_init  = init;
	q->t_func  = func;
	q->t_arg   = arg;
	c2_semaphore_init(&q->t_wait, 0);
	va_start(varargs, name);
	vsnprintf(namebuf, sizeof namebuf, name, varargs);
	va_end(varargs);

	result = c2_thread_init_impl(q, namebuf);
	if (result == 0 && q->t_init != NULL) {
		c2_semaphore_down(&q->t_wait);
		result = q->t_initrc;
		if (result != 0)
			c2_thread_join(q);
	}
	if (result != 0)
		q->t_state = TS_PARKED;
	return result;
}
C2_EXPORTED(c2_thread_init);

void *c2_thread_trampoline(void *arg)
{
	struct c2_thread *t = arg;

	C2_ASSERT(t->t_state == TS_RUNNING);
	C2_ASSERT(t->t_initrc == 0);

	if (t->t_init != NULL) {
		t->t_initrc = t->t_init(t->t_arg);
		c2_semaphore_up(&t->t_wait);
	}
	if (t->t_initrc == 0)
		t->t_func(t->t_arg);
	return NULL;
}

void c2_thread_fini(struct c2_thread *q)
{
	C2_PRE(q->t_state == TS_PARKED);
	c2_semaphore_fini(&q->t_wait);
	C2_SET0(q);
}
C2_EXPORTED(c2_thread_fini);

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
