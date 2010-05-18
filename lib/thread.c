/* -*- C -*- */

#include "lib/thread.h"
#include "lib/assert.h"


/**
   @addtogroup thread Thread
   @{
 */

static pthread_attr_t pthread_attr_default;

void *pthread_trampoline(void *arg)
{
	struct c2_thread *t = arg;

	t->t_func(t->t_arg);
	return NULL;
}

int c2_thread_init(struct c2_thread *q, void (*func)(void *), void *arg)
{
	int result;

	C2_ASSERT(q->t_func == NULL);

	q->t_func = func;
	q->t_arg  = arg;
	result = pthread_create(&q->t_id, &pthread_attr_default, 
				pthread_trampoline, q);
	return result;
}

void c2_thread_fini(struct c2_thread *q)
{
	q->t_func = NULL;
}

int c2_thread_kill(struct c2_thread *q, int signal)
{
	return pthread_kill(q->t_id, signal);
}

int c2_thread_join(struct c2_thread *q)
{
	return pthread_join(q->t_id, NULL);
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
