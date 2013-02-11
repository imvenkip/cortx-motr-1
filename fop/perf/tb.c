/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 15/01/2013
 */

#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/mutex.h"
#include "lib/time.h"
#include "lib/trace.h"
#include "reqh/reqh.h"

static int ub_fom_mem_tick(struct m0_fom *fom);
static int ub_fom_mutex_tick(struct m0_fom *fom);
static int ub_fom_long_tick(struct m0_fom *fom);
static int ub_fom_block_tick(struct m0_fom *fom);

static int (*ticks[UB_FOM_NR])(struct m0_fom *fom) = {
	[UB_FOM_MEM_B]     = ub_fom_mem_tick,
	[UB_FOM_MEM_KB]    = ub_fom_mem_tick,
	[UB_FOM_MEM_MB]    = ub_fom_mem_tick,
	[UB_FOM_MUTEX]     = ub_fom_mutex_tick,
	[UB_FOM_LONG_LOCK] = ub_fom_long_tick,
	[UB_FOM_BLOCK]     = ub_fom_block_tick
};

static void ssleep(uint64_t secs)
{
	m0_time_t req;

	if (secs == 0)
		return;

	m0_time_set(&req, secs, 0);
	m0_nanosleep(req, NULL);
}

static void cpu_utilize(struct ub_fom_ctx *ctx, size_t rq_seqn, size_t rq_type,
			size_t cycles)
{
	volatile char read;
	size_t        off1;
	size_t        off2;
	size_t        i;
	size_t        j;

	off1 = rq_seqn % ctx->fc_mem_sz;

	if (rq_type == UB_FOM_MEM_B)        /* 256 bytes max */
		off2 = min_check(ctx->fc_mem_sz - off1, off1 & 0xFF);
	else if (rq_type == UB_FOM_MEM_KB)  /* 64 kbytes max */
		off2 = min_check(ctx->fc_mem_sz - off1, off1 & 0xFFFF);
	else if (rq_type == UB_FOM_MEM_MB)  /* 1 mbyte max */
		off2 = min_check(ctx->fc_mem_sz - off1, off1 & 0xFFFFF);
	else {
		off2 = 0; /* pacify gcc with 'uninitialized variable warning' */
		M0_IMPOSSIBLE("Wrong ub_fom_ctx::fc_type arrived with FOM!");
	}

	M0_ASSERT(off1 < off2 && off2 + off1 < ctx->fc_mem_sz);

	for (j = 0; j < cycles; ++j) {
		for (i = off1; i < off2; ++i) {
			switch (rq_seqn % 3) {
			case 0: /* write */
				ctx->fc_mem[i] = i;
				break;
			case 1: /* read */
				read = ctx->fc_mem[i];
				break;
			case 2: /* read and write */
			default:
				ctx->fc_mem[i]++;
			}
		}
	}
}

/* ub_fom_mem_tick() function is written respectively to show how performance
   is decreased in different memory addressing scenarios. */
static int ub_fom_mem_tick(struct m0_fom *fom)
{
	struct ub_fom_ctx *ctx;
	size_t             rq_seqn;
	size_t             rq_type;

	ctx = container_of(fom, struct ub_fom_ctx, fc_gen);
	rq_seqn = ctx->fc_seqn;
	rq_type = ctx->fc_type;
	M0_LOG(M0_DEBUG, "fom:seqn=%zu", rq_seqn);

	cpu_utilize(ctx, rq_seqn, rq_type, UB_FOM_CYCLES);

	m0_fom_phase_set(fom, M0_FOM_PHASE_FINISH);
	return M0_FSO_WAIT;
}


/* ub_fom_mutex_tick() function is written respectively to show how performance
   is decreased in case when shared mutex synchronisation is used. */
static int ub_fom_mutex_tick(struct m0_fom *fom)
{
	struct ub_fom_ctx *ctx;
	size_t             rq_seqn;

	ctx = container_of(fom, struct ub_fom_ctx, fc_gen);
	rq_seqn = ctx->fc_seqn;

	m0_mutex_lock(ctx->fc_lock);

	M0_LOG(M0_DEBUG, "fom:seqn=%zu", rq_seqn);
	cpu_utilize(ctx, rq_seqn, UB_FOM_MEM_KB, UB_FOM_CYCLES);

	m0_mutex_unlock(ctx->fc_lock);
	m0_fom_phase_set(fom, M0_FOM_PHASE_FINISH);

	return M0_FSO_WAIT;
}

enum ub_fom_long_lock_phase {
	PH_REQ_LOCK = M0_FOM_PHASE_INIT,
	PH_GOT_LOCK = M0_FOPH_NR + 1,
};

/* ub_fom_long_tick() function is written respectively to show how performance
   is decreased in case when shared non-blocking synchronisation primitive long
   lock is used. */
static int ub_fom_long_tick(struct m0_fom *fom)
{
	struct ub_fom_ctx *ctx;
	size_t             rq_seqn;
	bool               is_writer;
	bool               (*lock)(struct m0_long_lock *lock,
				   struct m0_long_lock_link *link, int phase);
	void               (*unlock)(struct m0_long_lock *lock,
				     struct m0_long_lock_link *link);

	ctx = container_of(fom, struct ub_fom_ctx, fc_gen);
	rq_seqn = ctx->fc_seqn;

	/* Every 4th lock is a write lock, the rest are read-locks. */
	is_writer = rq_seqn % 4 == 0;
	lock = is_writer ? m0_long_write_lock : m0_long_read_lock;
	unlock = is_writer ? m0_long_write_unlock : m0_long_read_unlock;

	M0_LOG(M0_DEBUG, "fom->seqn=%zu, is_writer=%d", rq_seqn, is_writer);

	if (m0_fom_phase(fom) == PH_REQ_LOCK) {
		return M0_FOM_LONG_LOCK_RETURN(lock(ctx->fc_long_lock,
						    &ctx->fc_link,
						    PH_GOT_LOCK));
	} else if (m0_fom_phase(fom) == PH_GOT_LOCK) {
		cpu_utilize(ctx, rq_seqn, UB_FOM_MEM_KB, UB_FOM_CYCLES);
		unlock(ctx->fc_long_lock, &ctx->fc_link);
		m0_fom_phase_set(fom, M0_FOM_PHASE_FINISH);
	} else {
		M0_IMPOSSIBLE("Unexpected FOM phase");
	}

	return M0_FSO_WAIT;
}

/* ub_fom_block_tick() function is written respectively to show how performance
   is decreased in cases when additional threads are created in REQH. */
static int ub_fom_block_tick(struct m0_fom *fom)
{
	struct ub_fom_ctx *ctx;
	size_t             rq_seqn;

	ctx = container_of(fom, struct ub_fom_ctx, fc_gen);
	rq_seqn = ctx->fc_seqn;

	m0_fom_block_enter(fom);
	m0_mutex_lock(ctx->fc_lock);

	M0_LOG(M0_DEBUG, "fom:seqn=%zu", rq_seqn);
	cpu_utilize(ctx, rq_seqn, UB_FOM_MEM_KB, UB_FOM_BLOCK_CYCLES);

	m0_mutex_unlock(ctx->fc_lock);
	m0_fom_block_leave(fom);

	m0_fom_phase_set(fom, M0_FOM_PHASE_FINISH);

	return M0_FSO_WAIT;
}

static int ub_fom_tick(struct m0_fom *fom)
{
	struct ub_fom_ctx *ctx;

	M0_PRE(fom != NULL);
	ctx = container_of(fom, struct ub_fom_ctx, fc_gen);

	IS_IN_ARRAY(ctx->fc_type, ticks);
	return ticks[ctx->fc_type](fom);
}

static void reqh_fop_handle(struct m0_reqh *reqh, struct m0_fom *fom)
{
	M0_PRE(reqh != NULL);

	m0_rwlock_read_lock(&reqh->rh_rwlock);
	M0_ASSERT(!reqh->rh_shutdown);
	m0_fom_queue(fom, reqh);
	m0_rwlock_read_unlock(&reqh->rh_rwlock);
}

static void req_handle(struct m0_reqh *reqh, size_t seqn,
		       enum ub_fom_type test_type)
{
	struct m0_fom     *fom;
	struct ub_fom_ctx *ctx;
	int                rc;

	rc = ub_fom_create(&fom, reqh);
	M0_UB_ASSERT(rc == 0);

	ctx = container_of(fom, struct ub_fom_ctx, fc_gen);
	ctx->fc_seqn = seqn;
	ctx->fc_type = test_type;

	reqh_fop_handle(reqh, fom);
}

/**
 * Fills queue(s) of given request handler(s) with a dummy FOMs.
 *
 * @param reqh    an array of request handlers.
 * @param reqh_nr count of used request handlers in reqh.
 */
static void reqh_test_run(struct m0_reqh **reqh, size_t reqh_nr,
			  enum ub_fom_type test_type)
{
	size_t i;
	size_t hit;

	for (hit = 0, i = 0; hit < 10; ++i) {
		req_handle(reqh[i % reqh_nr], i, test_type);

		/* limit memory usage: req_handle() may load reqh queues faster
		   than reqh can process */
		if (i % 5000 == 0) {
			hit++;
			ssleep(1);
		}
	}
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
