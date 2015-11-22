/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy.Bilenko@seagate.com>
 * Original creation date: 21-Nov-2015
 */


/**
 * @addtogroup thread_pool
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_LIB
#include "lib/trace.h"
#include "lib/thread_pool.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"

enum  m0_parallel_pool_state {
	PPS_IDLE = 1,
	PPS_BUSY,
	PPS_TERMINATING,
	PPS_TERMINATED,
};

/* queue */


struct m0_parallel_queue_link {
	struct m0_queue_link  ql_link;
	void                 *ql_item;
};

struct m0_parallel_queue {
	struct m0_queue pq_queue;
	struct m0_mutex pq_lock;
};

static struct m0_parallel_queue_link *
parallel_queue_get(struct m0_parallel_queue *queue)
{
	struct m0_parallel_queue_link *link;

	m0_mutex_lock(&queue->pq_lock);
	link = (struct m0_parallel_queue_link *)m0_queue_get(&queue->pq_queue);
	m0_mutex_unlock(&queue->pq_lock);

	return link;
}

static void parallel_queue_add(struct m0_parallel_queue *queue,
			       struct m0_parallel_queue_link *link)
{
	m0_mutex_lock(&queue->pq_lock);
	m0_queue_put(&queue->pq_queue, &link->ql_link);
	m0_mutex_unlock(&queue->pq_lock);
}

static void parallel_queue_init(struct m0_parallel_queue *queue)
{
	m0_queue_init(&queue->pq_queue);
	m0_mutex_init(&queue->pq_lock);
}

static void parallel_queue_fini(struct m0_parallel_queue *queue)
{
	m0_mutex_fini(&queue->pq_lock);
	m0_queue_fini(&queue->pq_queue);
}

static void parallel_queue_link_init(struct m0_parallel_queue_link *l)
{
	m0_queue_link_init(&l->ql_link);
	l->ql_item = NULL;
}

/* pool */


static void pool_thread(struct m0_parallel_pool *pool)
{
	struct m0_parallel_queue_link *qlink;
	void                          *item;
	int                            rc;

	while (true) {
		m0_semaphore_down(&pool->pp_ready);

		M0_PRE(M0_IN(pool->pp_state, (PPS_BUSY, PPS_TERMINATING)));
		if (pool->pp_done)
			break;

		while (true) {
			qlink = parallel_queue_get(pool->pp_queue);
			if (qlink == NULL)
				break;

			item = qlink->ql_item;
			rc = pool->pp_process(item);

			/* @TODO: Currenly pool doesn't support error codes
			 * returned by the user but it's easy to fix this. Just
			 * add corresponding .ql_rc field into struct
			 * m0_parallel_queue_link, and introduce interface
			 * function like m0_parallel_pool_get_next_error().
			 */
			M0_ASSERT(rc == 0);
		}

		m0_semaphore_up(&pool->pp_sync);
	}
}

static void pool_threads_fini(struct m0_parallel_pool *pool, bool join)
{
	int i;

	for (i = 0; i < pool->pp_thread_nr; ++i) {
		if (join)
			m0_thread_join(&pool->pp_threads[i]);
		m0_thread_fini(&pool->pp_threads[i]);
	}

	parallel_queue_fini(pool->pp_queue);
	m0_free(pool->pp_threads);
	m0_free(pool->pp_qlinks);
	m0_free(pool->pp_queue);
}

static void parallel_pool_fini(struct m0_parallel_pool *pool, bool join)
{
	pool_threads_fini(pool, join);
	m0_semaphore_fini(&pool->pp_sync);
	m0_semaphore_fini(&pool->pp_ready);
}

static int pool_threads_init(struct m0_parallel_pool *pool,
			     int thread_nr, int qlink_nr)
{
	int i;
	int result = thread_nr > 0 && qlink_nr > 0 ? 0 : -EINVAL;

	if (result != 0)
		return result;

	M0_ALLOC_ARR(pool->pp_threads, thread_nr);
	M0_ALLOC_ARR(pool->pp_qlinks,  qlink_nr);
	M0_ALLOC_PTR(pool->pp_queue);
	if (pool->pp_threads == NULL || pool->pp_qlinks == NULL ||
	    pool->pp_queue == NULL) {
		m0_free(pool->pp_threads);
		m0_free(pool->pp_qlinks);
		m0_free(pool->pp_queue);
		return -ENOMEM;
	}

	parallel_queue_init(pool->pp_queue);

	for (i = 0; i < qlink_nr; ++i)
		parallel_queue_link_init(&pool->pp_qlinks[i]);

	for (i = 0; i < thread_nr; ++i) {
		result = M0_THREAD_INIT(&pool->pp_threads[pool->pp_thread_nr],
					struct m0_parallel_pool*, NULL,
					&pool_thread, pool,
					"pool_thread%d", pool->pp_thread_nr);
		if (result != 0) {
			parallel_pool_fini(pool, true);
			break;
		}
		pool->pp_thread_nr++;
	}

	return result;
}

M0_INTERNAL int m0_parallel_pool_init(struct m0_parallel_pool *pool,
				      int thread_nr, int qlinks_nr)
{
	pool->pp_state     = PPS_IDLE;
	pool->pp_done      = false;
	pool->pp_process   = NULL;
	pool->pp_thread_nr = 0;
	pool->pp_qlinks_nr = qlinks_nr;

	m0_semaphore_init(&pool->pp_ready, 0);
	m0_semaphore_init(&pool->pp_sync, 0);

	return pool_threads_init(pool, thread_nr, qlinks_nr);
}

M0_INTERNAL void m0_parallel_pool_fini(struct m0_parallel_pool *pool)
{
	M0_PRE(pool->pp_state == PPS_TERMINATED);
	parallel_pool_fini(pool, false);
}

M0_INTERNAL void m0_parallel_pool_start(struct m0_parallel_pool *pool,
					int (*process)(void *item))
{
	int i;

	M0_PRE(pool->pp_state == PPS_IDLE);
	M0_PRE(pool->pp_process == NULL);

	pool->pp_state = PPS_BUSY;
	pool->pp_process = process;

	for (i = 0; i < pool->pp_thread_nr; ++i)
		m0_semaphore_up(&pool->pp_ready);
}

M0_INTERNAL int m0_parallel_pool_wait(struct m0_parallel_pool *pool)
{
	int i;

	M0_PRE(pool->pp_state == PPS_BUSY);
	M0_PRE(pool->pp_process != NULL);

	for (i = 0; i < pool->pp_thread_nr; ++i)
		m0_semaphore_down(&pool->pp_sync);

	M0_ASSERT(pool->pp_process != NULL);
	pool->pp_state = PPS_IDLE;
	pool->pp_process = NULL;

	return 0;
}

M0_INTERNAL void m0_parallel_pool_terminate_wait(struct m0_parallel_pool *pool)
{
	int i;

	M0_PRE(pool->pp_state == PPS_IDLE);
	pool->pp_state = PPS_TERMINATING;

	pool->pp_done = true;
	for (i = 0; i < pool->pp_thread_nr; ++i)
		m0_semaphore_up(&pool->pp_ready);

	for (i = 0; i < pool->pp_thread_nr; ++i)
		m0_thread_join(&pool->pp_threads[i]);

	pool->pp_state = PPS_TERMINATED;
}

M0_INTERNAL int m0_parallel_pool_job_add(struct m0_parallel_pool *pool,
					 void *item)
{
	int pos;

	M0_PRE(pool->pp_state == PPS_IDLE);

	pos = m0_queue_length(&pool->pp_queue->pq_queue);
	if (pos < pool->pp_qlinks_nr) {
		pool->pp_qlinks[pos].ql_item = item;
		parallel_queue_add(pool->pp_queue, &pool->pp_qlinks[pos]);
		return 0;
	}

	return -ENOSPC;
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of thread_pool group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
