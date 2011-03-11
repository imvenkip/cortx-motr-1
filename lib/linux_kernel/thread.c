/* -*- C -*- */

#include "lib/thread.h"
#include "lib/arith.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

#include <linux/pid_namespace.h>

/**
   @addtogroup thread Thread

   Implementation of c2_thread on top of struct task_struct.

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

	if (q->t_h.h_id != current->pid) {
		return -EINVAL;
	}
	p = current;

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

	rcu_read_lock();
	p = pid_task(find_pid_ns(q->t_h.h_id, current->nsproxy->pid_ns), PIDTYPE_PID);
	if (p == NULL) {
		rcu_read_unlock();
		free_cpumask_var(cpuset);
		return -ESRCH;
	}

	get_task_struct(p);
	rcu_read_unlock();

	...

	put_task_struct(p);
	*/

	result = set_cpus_allowed_ptr(p, cpuset);
	free_cpumask_var(cpuset);
	return result;
#endif
}
C2_EXPORTED(c2_thread_confine);

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
