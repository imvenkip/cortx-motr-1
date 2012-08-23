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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/06/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/mutex.h"
#include "reqh/reqh.h"
#include "fop/fom_long_lock.h"

C2_TL_DESCR_DECLARE(c2_lll, extern);
C2_TL_DECLARE(c2_lll, extern, struct c2_long_lock_link);

enum tb_request_type {
	RQ_READ,
	RQ_WRITE,
	RQ_WAKE_UP,
	RQ_LAST
};

enum tb_request_phase {
	/* See comment on PH_REQ_LOCK value in fom_rdwr_state() function */
	PH_REQ_LOCK = C2_FOM_PHASE_INIT,
	PH_GOT_LOCK = C2_FOPH_NR + 1,
};

struct test_min_max {
	size_t min;
	size_t max;
};

struct test_request {
	enum tb_request_type tr_type;
	/* Expected count of waiters */
	size_t tr_waiters;
	/**
	 * Expected count of owners. As far, test is being run in multithreaded
	 * environment, concurrent FOMs can pop an owner from a queue in an
	 * arbitary order. That's the reason why struct test_min_max is used.
	 */
	struct test_min_max tr_owners;
};

static struct c2_fom      *sleeper;
static struct c2_chan      chan[RDWR_REQUEST_MAX];
static struct c2_clink	   clink[RDWR_REQUEST_MAX];
static struct c2_long_lock long_lock;

/**
 * a. Checks that multiple readers can hold the read lock concurrently, but
 * writers (more than one) get blocked.
 */
static bool readers_check(struct c2_long_lock *lock)
{
	struct c2_long_lock_link *head;
	bool result;

	c2_mutex_lock(&lock->l_lock);

	head = c2_lll_tlist_head(&lock->l_waiters);
	result = c2_tl_forall(c2_lll, l, &lock->l_owners,
			      l->lll_lock_type == C2_LONG_LOCK_READER) &&
		ergo(head != NULL, head->lll_lock_type == C2_LONG_LOCK_WRITER);

	c2_mutex_unlock(&lock->l_lock);

	return result;
}

/**
 * b. Only one writer at a time can hold the write lock. All other contenders
 * wait.
 */
static bool writer_check(struct c2_long_lock *lock)
{
	struct c2_long_lock_link *head;
	bool result;

	c2_mutex_lock(&lock->l_lock);

	head = c2_lll_tlist_head(&lock->l_owners);
	result = head != NULL &&
		head->lll_lock_type == C2_LONG_LOCK_WRITER &&
		c2_lll_tlist_length(&lock->l_owners) == 1;

	c2_mutex_unlock(&lock->l_lock);

	return result;
}

/**
 * Checks expected readers and writers against actual.
 */
static bool lock_check(struct c2_long_lock *lock, enum tb_request_type type,
		       size_t owners_min, size_t owners_max, size_t waiters)
{
	bool result;
	size_t owners_len;

	c2_mutex_lock(&lock->l_lock);

	owners_len = c2_lll_tlist_length(&lock->l_owners);
	result = owners_min <= owners_len && owners_len <= owners_max &&
		c2_lll_tlist_length(&lock->l_waiters) == waiters &&

		(type == RQ_WRITE) ? lock->l_state == C2_LONG_LOCK_WR_LOCKED :
		(type == RQ_READ)  ? lock->l_state == C2_LONG_LOCK_RD_LOCKED :
		false;

	c2_mutex_unlock(&lock->l_lock);

	return result;
}

static int fom_rdwr_tick(struct c2_fom *fom)
{
	struct fom_rdwr	*request;
	int		 rq_type;
	int		 rq_seqn;
	int		 result;

	request = container_of(fom, struct fom_rdwr, fr_gen);
	C2_UT_ASSERT(request != NULL);
	rq_type = request->fr_req->tr_type;
	rq_seqn = request->fr_seqn;

	/*
	 * To pacify C2_PRE(C2_IN(c2_fom_phase(fom), (C2_FOPH_INIT,
	 * C2_FOPH_FAILURE))) precondition in c2_fom_queue(), special processing
	 * order of FOM phases is used.
	 *
	 * Do NOT use this code as a template for the general purpose. It's
	 * designed for tesing of c2_long_lock ONLY!
	 */
	if (c2_fom_phase(fom) == PH_REQ_LOCK) {
		if (rq_seqn == 0)
			sleeper = fom;

		switch (rq_type) {
		case RQ_READ:
			result = C2_FOM_LONG_LOCK_RETURN(
					c2_long_read_lock(&long_lock,
							  &request->fr_link,
							  PH_GOT_LOCK));
			C2_UT_ASSERT((result == C2_FSO_AGAIN)
				     == (rq_seqn == 0));
			result = C2_FSO_WAIT;
			break;
		case RQ_WRITE:
			result = C2_FOM_LONG_LOCK_RETURN(
					c2_long_write_lock(&long_lock,
							   &request->fr_link,
							   PH_GOT_LOCK));
			C2_UT_ASSERT((result == C2_FSO_AGAIN)
				     == (rq_seqn == 0));
			result = C2_FSO_WAIT;
			break;
		case RQ_WAKE_UP:
		default:
			c2_fom_wakeup(sleeper);
			c2_fom_phase_set(fom, PH_GOT_LOCK);
			result = C2_FSO_AGAIN;
		}

		/* notify, fom ready */
		c2_chan_signal(&chan[rq_seqn]);
	} else if (c2_fom_phase(fom) == PH_GOT_LOCK) {
		C2_UT_ASSERT(ergo(C2_IN(rq_type, (RQ_READ, RQ_WRITE)),
				  lock_check(&long_lock, rq_type,
					     request->fr_req->tr_owners.min,
					     request->fr_req->tr_owners.max,
					     request->fr_req->tr_waiters)));

		switch (rq_type) {
		case RQ_READ:
			C2_UT_ASSERT(readers_check(&long_lock));
			C2_UT_ASSERT(c2_long_is_read_locked(&long_lock, fom));
			c2_long_read_unlock(&long_lock, &request->fr_link);
			break;
		case RQ_WRITE:
			C2_UT_ASSERT(writer_check(&long_lock));
			C2_UT_ASSERT(c2_long_is_write_locked(&long_lock, fom));
			c2_long_write_unlock(&long_lock, &request->fr_link);
			break;
		case RQ_WAKE_UP:
		default:
			;
		}

		/* notify, fom ready */
		c2_chan_signal(&chan[rq_seqn]);
		c2_fom_phase_set(fom, C2_FOM_PHASE_FINISH);
		result = C2_FSO_WAIT;
        } else
		C2_IMPOSSIBLE("");

	return result;
}

static void reqh_fop_handle(struct c2_reqh *reqh,  struct c2_fom *fom)
{
	C2_PRE(reqh != NULL);
	c2_rwlock_read_lock(&reqh->rh_rwlock);
	C2_PRE(!reqh->rh_shutdown);
	c2_fom_queue(fom, reqh);
	c2_rwlock_read_unlock(&reqh->rh_rwlock);
}

static void test_req_handle(struct c2_reqh *reqh,
			    struct test_request *rq, int seqn)
{
	struct c2_fom   *fom;
	struct fom_rdwr *obj;
	int rc;

	rc = rdwr_fom_create(&fom);
	C2_UT_ASSERT(rc == 0);

	obj = container_of(fom, struct fom_rdwr, fr_gen);
	obj->fr_req  = rq;
	obj->fr_seqn = seqn;

	reqh_fop_handle(reqh, fom);
}

/* c. To make sure that the fairness queue works, lock should sequentially
 * transit from "state to state" listed in the following structure: */

static struct test_request test[3][RDWR_REQUEST_MAX] = {
	[0] = {
		{.tr_type = RQ_READ,  .tr_owners = {1, 1}, .tr_waiters = 8},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 7},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 4},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 4},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 4},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 3},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},

		{.tr_type = RQ_WAKE_UP, .tr_owners = {0, 0}, .tr_waiters = 0},
		{.tr_type = RQ_LAST,    .tr_owners = {0, 0}, .tr_waiters = 0},
	},
	[1] = {
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 9},
		{.tr_type = RQ_READ,  .tr_owners = {1, 1}, .tr_waiters = 8},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 7},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 4},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 4},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 4},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 3},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},

		{.tr_type = RQ_WAKE_UP, .tr_owners = {0, 0}, .tr_waiters = 0},
		{.tr_type = RQ_LAST,    .tr_owners = {0, 0}, .tr_waiters = 0},
	},
	[2] = {
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 9},
		{.tr_type = RQ_READ,  .tr_owners = {1, 1}, .tr_waiters = 8},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 7},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 6},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 5},
		{.tr_type = RQ_READ,  .tr_owners = {1, 1}, .tr_waiters = 4},
		{.tr_type = RQ_WRITE, .tr_owners = {1, 1}, .tr_waiters = 3},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},
		{.tr_type = RQ_READ,  .tr_owners = {1, 3}, .tr_waiters = 0},

		{.tr_type = RQ_WAKE_UP, .tr_owners = {0, 0}, .tr_waiters = 0},
		{.tr_type = RQ_LAST,    .tr_owners = {0, 0}, .tr_waiters = 0},
	},
};

static void rdwr_send_fop(struct c2_reqh **reqh, size_t reqh_nr)
{
	int i;
	int j;

	for (j = 0; j < ARRAY_SIZE(test); ++j) {
		c2_long_lock_init(&long_lock);

		for (i = 0; test[j][i].tr_type != RQ_LAST; ++i) {
			c2_chan_init(&chan[i]);
			c2_clink_init(&clink[i], NULL);
			c2_clink_add(&chan[i], &clink[i]);

			/* d. Send FOMs from multiple request handlers, where
			 * they can contend for the lock. 'reqh[i % reqh_nr]'
			 * expression allows to send FOMs one by one into each
			 * request handler */
			test_req_handle(reqh[i % reqh_nr], &test[j][i], i);

			/* Wait until the fom completes the first state
			 * transition. This is needed to achieve deterministic
			 * lock acquisition order. */
			c2_chan_wait(&clink[i]);
		}

		/* Wait until all queued foms are processed. */
		for (i = 0; test[j][i].tr_type != RQ_LAST; ++i) {
			c2_chan_wait(&clink[i]);
		}

		/* Cleanup */
		for (i = 0; test[j][i].tr_type != RQ_LAST; ++i) {
			c2_clink_del(&clink[i]);
			c2_chan_fini(&chan[i]);
			c2_clink_fini(&clink[i]);
		}

		c2_long_lock_fini(&long_lock);
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
