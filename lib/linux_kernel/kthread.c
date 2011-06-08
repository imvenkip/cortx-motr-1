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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 02/24/2011
 */

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
	c2_thread_trampoline(arg);

	/* Required for correct c2_thread_join() behavior in kernel:
	   kthread_stop() will not stop if the thread has been created but has
	   not yet started executing.  So, c2_thread_join() blocks on the
	   semaphore to ensure the thread can be stopped. kthread_stop(), in
	   turn, requires that the thread not exit until kthread_stop() is
	   called, so we must loop on kthread_should_stop() to satisfy that API
	   requirement.
	 */
	c2_semaphore_up(&t->t_wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);

	return 0;
}

int c2_thread_init_impl(struct c2_thread *q, const char *namebuf)
{
	int result;

	C2_PRE(q->t_state == TS_RUNNING);

	q->t_h.h_t = kthread_create(kthread_trampoline, q, "%s", namebuf);
	if (IS_ERR(q->t_h.h_t)) {
		result = PTR_ERR(q->t_h.h_t);
	} else {
		result = 0;
		wake_up_process(q->t_h.h_t);
	}
	return result;
}

int c2_thread_join(struct c2_thread *q)
{
	int result;

	C2_PRE(q->t_state == TS_RUNNING);
	C2_PRE(q->t_h.h_t != current);

	/* see comment in kthread_trampoline */
	c2_semaphore_down(&q->t_wait);
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
