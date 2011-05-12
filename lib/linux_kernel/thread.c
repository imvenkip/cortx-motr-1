/* -*- C -*- */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/thread.h"
#include "lib/arith.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

/**
   @addtogroup thread Thread

   Implementation of c2_thread on top of struct task_struct and kthread API.

   Instead of creating a new kthread executing user-supplied function, all
   threads start executing the same trampoline function kthread_trampoline()
   that performs some generic book-keeping.

   Presently, the kernel c2_thread_confine only succeeds when the thread
   referenced is the current thread.  This is because the interfaces required
   to safely reference other threads are not all exported, most notably,
   the matched pair get_task_struct() and put_task_struct() are required to
   ensure a task will not be deallocated while set_cpus_allowed_ptr() is called,
   however, put_task_struct() references __put_task_struct() and the latter
   is not exported.  By restricting to the current thread, we can ensure the
   thread will not be deallocated during the call without using this pair of
   reference counting functions.

   @{
 */

static int kthread_trampoline(void *arg)
{
	struct c2_thread *t = arg;

	C2_ASSERT(t->t_state == TS_RUNNING);
	C2_ASSERT(t->t_initrc == 0);

	if (t->t_init != NULL)
		t->t_initrc = t->t_init(t->t_arg);
	c2_chan_signal(&t->t_initwait);
	if (t->t_initrc == 0)
		t->t_func(t->t_arg);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);

	return 0;
}

int c2_thread_init(struct c2_thread *q, int (*init)(void *),
		   void (*func)(void *), void *arg, const char *name, ...)
{
	int             result;
	struct c2_clink wait;
	char            commbuf[TASK_COMM_LEN];
	va_list         varargs;

	C2_PRE(q->t_func == NULL);
	C2_PRE(q->t_state == TS_PARKED);

	q->t_state = TS_RUNNING;
	q->t_init  = init;
	q->t_func  = func;
	q->t_arg   = arg;

	va_start(varargs, name);
	vsnprintf(commbuf, sizeof(commbuf), name, varargs);
	va_end(varargs);

	/*
	  Always set up and wait on initwait. Ensures thread actually starts
	  before kthread_stop can be called.
	 */
	c2_clink_init(&wait, NULL);
	c2_chan_init(&q->t_initwait);
	c2_clink_add(&q->t_initwait, &wait);
	q->t_h.h_t = kthread_create(kthread_trampoline, q, "%s", commbuf);
	if (IS_ERR(q->t_h.h_t)) {
		result = PTR_ERR(q->t_h.h_t);
	} else {
		result = 0;
		wake_up_process(q->t_h.h_t);
	}
	if (result == 0) {
		c2_chan_wait(&wait);
		result = q->t_initrc;
		if (result != 0)
			c2_thread_join(q);
	}
	c2_clink_del(&wait);
	c2_clink_fini(&wait);
	c2_chan_fini(&q->t_initwait);
	if (result != 0)
		q->t_state = TS_PARKED;
	return result;
}
C2_EXPORTED(c2_thread_init);

void c2_thread_fini(struct c2_thread *q)
{
	C2_PRE(q->t_state == TS_PARKED);
	C2_SET0(q);
}
C2_EXPORTED(c2_thread_fini);

int c2_thread_join(struct c2_thread *q)
{
	int result;

	C2_PRE(q->t_state == TS_RUNNING);
	C2_PRE(q->t_h.h_t != current);

	/*
	  c2_thread provides no wrappers for kthread_should_stop() or
	  do_exit(), so this will block until the thread exits by returning
	  from kthread_trampoline.  kthread_trampoline always returns 0,
	  but kthread_stop can return -errno on failure.
	 */
	result = kthread_stop(q->t_h.h_t);
	if (result == 0)
		q->t_state = TS_PARKED;
	return result;
}
C2_EXPORTED(c2_thread_join);

int c2_thread_signal(struct c2_thread *q, int sig)
{
	return -ENOSYS;
}
C2_EXPORTED(c2_thread_signal);

int c2_thread_confine(struct c2_thread *q, const struct c2_bitmap *processors)
{
#if 1
	/*
	  For now, not implemented because it needs to call GPL functions to
	  succeed.  See also problems with put_task_struct mentioned below.
	 */
	return -ENOSYS;
#else
	int                 result;
	size_t              idx;
	size_t              nr_bits = min64u(processors->b_nr, nr_cpu_ids);
	cpumask_var_t       cpuset;
	struct task_struct *p;

	if (q->t_h.h_t != current) {
		return -EINVAL;
	}
	p = q->t_h.h_t;

	if (!alloc_cpumask_var(&cpuset, GFP_KERNEL)) {
		return -ENOMEM;
	}
	cpumask_clear(cpuset);

	for (idx = 0; idx < nr_bits; ++idx) {
		if (c2_bitmap_get(processors, idx))
			cpumask_set_cpu(idx, cpuset);
	}

	/*
	  The following code would safely find the task_struct and ensure it
	  would not disappear, however, put_task_struct is an inline that
	  references __put_task_struct, and the latter is not exported.

	get_task_struct(p);

	...

	put_task_struct(p);
	*/

	result = set_cpus_allowed_ptr(p, cpuset);
	free_cpumask_var(cpuset);
	return result;
#endif
}
C2_EXPORTED(c2_thread_confine);

void c2_thread_self(struct c2_thread_handle *id)
{
	id->h_t = current;
}
C2_EXPORTED(c2_thread_self);

bool c2_thread_handle_eq(struct c2_thread_handle *h1,
			 struct c2_thread_handle *h2)
{
	return h1->h_t == h2->h_t;
}
C2_EXPORTED(c2_thread_handle_eq);

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
