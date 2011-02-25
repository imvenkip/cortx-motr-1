/* -*- C -*- */

#include "lib/thread.h"
#include "lib/arith.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

#include <linux/pid_namespace.h>

/**
   @addtogroup thread Thread

   Implementation of c2_thread on top of struct task_struct.

   @{
 */

int c2_thread_confine(struct c2_thread *q, const struct c2_bitmap *processors)
{
	int                 result;
	size_t              idx;
	size_t              nr_bits = min64u(processors->b_nr, nr_cpu_ids);
	cpumask_var_t       cpuset;
	struct task_struct *p;

	rcu_read_lock();
	p = pid_task(find_pid_ns(q->t_h.h_id, current->nsproxy->pid_ns), PIDTYPE_PID);
	if (p == NULL) {
		rcu_read_unlock();
		return -ESRCH;
	}

	/* ensure p will remain valid until done */
	get_task_struct(p);
	rcu_read_unlock();

	if (!alloc_cpumask_var(&cpuset, GFP_KERNEL)) {
		put_task_struct(p);
		return -ENOMEM;
	}
	cpumask_clear(cpuset);

	for (idx = 0; idx < nr_bits; ++idx) {
		if (c2_bitmap_get(processors, idx))
			cpumask_set_cpu(idx, cpuset);
	}

	result = set_cpus_allowed_ptr(p, cpuset);
	free_cpumask_var(cpuset);
	put_task_struct(p);
	return result;
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
