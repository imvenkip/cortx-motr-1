/* -*- C -*- */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/thread.h"
#include "lib/assert.h"

/**
   @addtogroup thread Thread

   Implementation of c2_thread on top of pthread_t.

   <b>Implementation notes</b>

   Instead of creating a new POSIX thread executing user-supplied function, all
   threads start executing the same trampoline function pthread_trampoline()
   that performs some generic book-keeping.

   Threads are created with a PTHREAD_CREATE_JOINABLE attribute.

   @{
 */

static pthread_attr_t pthread_attr_default;

static void *pthread_trampoline(void *arg)
{
	struct c2_thread *t = arg;

	C2_ASSERT(t->t_state == TS_RUNNING);
	C2_ASSERT(t->t_initrc == 0);

	if (t->t_init != NULL) {
		t->t_initrc = t->t_init(t->t_arg);
		c2_chan_signal(&t->t_initwait);
	}
	if (t->t_initrc == 0)
		t->t_func(t->t_arg);
	return NULL;
}

int c2_thread_init(struct c2_thread *q, int (*init)(void *),
		   void (*func)(void *), void *arg)
{
	int             result;
	struct c2_clink wait;

	C2_PRE(q->t_func == NULL);
	C2_PRE(q->t_state == TS_PARKED);

	q->t_state = TS_RUNNING;
	q->t_init  = init;
	q->t_func  = func;
	q->t_arg   = arg;
	if (init != NULL) {
		c2_clink_init(&wait, NULL);
		c2_chan_init(&q->t_initwait);
		c2_clink_add(&q->t_initwait, &wait);
	}
	result = pthread_create(&q->t_h.h_id, &pthread_attr_default,
				pthread_trampoline, q);
	if (init != NULL) {
		if (result == 0) {
			c2_chan_wait(&wait);
			result = q->t_initrc;
			if (result != 0)
				c2_thread_join(q);
		}
		c2_clink_del(&wait);
		c2_clink_fini(&wait);
		c2_chan_fini(&q->t_initwait);
	}
	if (result != 0)
		q->t_state = TS_PARKED;
	return result;
}

void c2_thread_fini(struct c2_thread *q)
{
	C2_PRE(q->t_state == TS_PARKED);
	C2_SET0(q);
}

int c2_thread_join(struct c2_thread *q)
{
	int result;

	C2_PRE(q->t_state == TS_RUNNING);
	C2_PRE(!pthread_equal(q->t_h.h_id, pthread_self()));
	result = pthread_join(q->t_h.h_id, NULL);
	if (result == 0)
		q->t_state = TS_PARKED;
	return result;
}

int c2_thread_signal(struct c2_thread *q, int sig)
{
	return pthread_kill(q->t_h.h_id, sig);
}

int c2_threads_init(void)
{
	int result;

	result = pthread_attr_init(&pthread_attr_default);
	if (result == 0)
		result = pthread_attr_setdetachstate(&pthread_attr_default,
						     PTHREAD_CREATE_JOINABLE);
	return result;
}

void c2_threads_fini(void)
{
	pthread_attr_destroy(&pthread_attr_default);
}

/** @} end of vec group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
