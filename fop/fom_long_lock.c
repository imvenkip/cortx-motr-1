/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 18-Jun-2012
 */

/**
 * @addtogroup c2_longlock
 * @{
 */

#include "fop/fom_long_lock.h"
#include "lib/arith.h"

static void fom_read_ready_cb(struct c2_fom_callback *cb)
{
	struct c2_list_link *pos;
	struct c2_longlock  *lock = cb->fc_lock;
	struct c2_fom       *fom  = cb->fc_fom;

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(fom->fo_transitions == lock->l_transitions);
	C2_PRE(lock->l_state == C2_LONG_LOCK_UNLOCKED ||
	       lock->l_state == C2_LONG_LOCK_RD_LOCKED);

	C2_PRE(c2_list_contains(lock->l_rd_in_processing,
				&fom->fo_lock_linkage));

	if (lock->l_state == C2_LONG_LOCK_UNLOCKED) {
		lock->l_state = C2_LONG_LOCK_RD_LOCKED;
		C2_CNT_INC(fom->fo_locks);

		c2_list_for_each(lock->l_rd_in_processing, pos) {
			if (&fom->fo_lock_linkage != pos) {
				c2_chan_signal(&lock->l_wait);
			}
		}
	}

	c2_fom_ready(fom);
	c2_mutex_unlock(&lock->l_lock);
}

static void fom_write_ready_cb(struct c2_fom_callback *cb)
{
	struct c2_longlock  *lock = cb->fc_lock;
	struct c2_fom       *fom  = cb->fc_fom;

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(fom->fo_transitions == lock->l_transitions);
	C2_PRE(lock->l_state == C2_LONG_LOCK_UNLOCKED);
	lock->l_state = C2_LONG_LOCK_WR_LOCKED;
	C2_CNT_INC(fom->fo_locks);
	c2_fom_ready(fom);

	c2_mutex_unlock(&lock->l_lock);
}

static void c2_fom_wait_on_lock(struct c2_fom *fom, struct c2_chan *chan,
				struct c2_fom_callback *cb,
				struct c2_longlock *lock, bool read)
{
	cb->fc_bottom = read ? fom_read_ready_cb : fom_write_ready_cb;
	cb->fc_lock = lock;
	lock->l_transitions = fom->fo_transitions;
	c2_fom_callback_arm(fom, chan, cb);
}

static bool lock_invariant(struct c2_longlock *lock, struct c2_fom *fom)
{
	return fom->fo_cb.fc_state == C2_FCS_INIT &&
		fom->fo_cb.fc_ast.sa_cb == NULL &&
		!c2_list_contains(&lock->l_readers[0], &fom->fo_lock_linkage) &&
		!c2_list_contains(&lock->l_readers[1], &fom->fo_lock_linkage) &&
		!c2_list_contains(&lock->l_writers, &fom->fo_lock_linkage);
}

static bool write_lock(struct c2_longlock *lock, struct c2_fom *fom)
{
	bool wr_locked;

	c2_mutex_lock(&lock->l_lock);

	c2_list_add_tail(&lock->l_writers, &fom->fo_lock_linkage);

	if (lock->l_state == C2_LONG_LOCK_UNLOCKED) {
		lock->l_state = C2_LONG_LOCK_WR_LOCKED;
		C2_CNT_INC(fom->fo_locks);
	}

	wr_locked = lock->l_state == C2_LONG_LOCK_WR_LOCKED;
	c2_mutex_unlock(&lock->l_lock);

	return wr_locked;
}

static void write_unlock(struct c2_longlock *lock, struct c2_fom *fom)
{
	c2_mutex_lock(&lock->l_lock);

	C2_PRE(lock->l_state == C2_LONG_LOCK_WR_LOCKED);
	C2_PRE(c2_list_contains(&lock->l_writers,
				&fom->fo_lock_linkage));

	c2_list_del(&fom->fo_lock_linkage);

	lock->l_state = C2_LONG_LOCK_UNLOCKED;
	C2_CNT_DEC(fom->fo_locks);

	c2_mutex_unlock(&lock->l_lock);
}

bool c2_long_write_lock(struct c2_longlock *lock,
			struct c2_fom *fom, int next_phase)
{
	C2_PRE(lock_invariant(lock, fom));

	if (write_lock(lock, fom))
		return true;

	fom->fo_phase = next_phase;
	c2_fom_wait_on_lock(fom, &lock->l_wait, &fom->fo_cb, lock, false);

	return false;
}

void c2_long_write_unlock(struct c2_longlock *lock, struct c2_fom *fom)
{
	write_unlock(lock, fom);
	c2_chan_signal(&lock->l_wait);
}

static bool read_lock(struct c2_longlock *lock, struct c2_fom *fom)
{
	bool rd_locked;
	bool writers_pending;

	c2_mutex_lock(&lock->l_lock);

	writers_pending = c2_list_length(&lock->l_writers) > 0;
	c2_list_add_tail(writers_pending
			 ? lock->l_rd_pending
			 : lock->l_rd_in_processing,
			 &fom->fo_lock_linkage);

	if (lock->l_state == C2_LONG_LOCK_UNLOCKED) {
		lock->l_state = C2_LONG_LOCK_RD_LOCKED;
		C2_CNT_INC(fom->fo_locks);
	}

	rd_locked = lock->l_state == C2_LONG_LOCK_RD_LOCKED;

	c2_mutex_unlock(&lock->l_lock);

	return rd_locked;
}

bool c2_long_read_lock(struct c2_longlock *lock,
		       struct c2_fom *fom, int next_phase)
{
	C2_PRE(lock_invariant(lock, fom));

	if (read_lock(lock, fom))
		return true;

	fom->fo_phase = next_phase;
	c2_fom_wait_on_lock(fom, &lock->l_wait, &fom->fo_cb, lock, true);

	return false;
}

static bool read_unlock(struct c2_longlock *lock, struct c2_fom *fom)
{
	bool unlocked;

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(lock->l_state == C2_LONG_LOCK_RD_LOCKED);
	C2_PRE(c2_list_contains(lock->l_rd_in_processing,
				&fom->fo_lock_linkage));

	c2_list_del(&fom->fo_lock_linkage);

	if (c2_list_length(lock->l_rd_in_processing) == 0) {
		lock->l_state = C2_LONG_LOCK_UNLOCKED;
		C2_CNT_DEC(fom->fo_locks);
		C2_SWAP(lock->l_rd_in_processing, lock->l_rd_pending);
	}

	unlocked = (lock->l_state == C2_LONG_LOCK_UNLOCKED);
	c2_mutex_unlock(&lock->l_lock);

	return unlocked;
}

void c2_long_read_unlock(struct c2_longlock *lock, struct c2_fom *fom)
{
	if (read_unlock(lock, fom))
		c2_chan_signal(&lock->l_wait);
}

bool c2_long_is_read_locked(struct c2_longlock *lock)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);
	ret = lock->l_state == C2_LONG_LOCK_RD_LOCKED;
	c2_mutex_unlock(&lock->l_lock);

	return ret;
}

bool c2_long_is_write_locked(struct c2_longlock *lock)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);
	ret = lock->l_state == C2_LONG_LOCK_WR_LOCKED;
	c2_mutex_unlock(&lock->l_lock);

	return ret;
}

void c2_long_lock_init(struct c2_longlock *lock)
{
	c2_mutex_init(&lock->l_lock);

	c2_list_init(&lock->l_readers[0]);
	c2_list_init(&lock->l_readers[1]);
	c2_list_init(&lock->l_writers);

	lock->l_rd_in_processing = &lock->l_readers[0];
	lock->l_rd_pending = &lock->l_readers[1];

	c2_chan_init(&lock->l_wait);

	lock->l_state = C2_LONG_LOCK_UNLOCKED;
}

void c2_long_lock_fini(struct c2_longlock *lock)
{
	C2_ASSERT(lock->l_state == C2_LONG_LOCK_UNLOCKED);
	C2_ASSERT(c2_list_length(&lock->l_writers) == 0);

	C2_ASSERT(c2_list_length(&lock->l_readers[0]) == 0);
	C2_ASSERT(c2_list_length(&lock->l_readers[1]) == 0);

	c2_chan_fini(&lock->l_wait);

	c2_list_fini(&lock->l_writers);
	c2_list_fini(&lock->l_readers[0]);
	c2_list_fini(&lock->l_readers[1]);

	lock->l_rd_in_processing = NULL;
	lock->l_rd_pending = NULL;

	c2_mutex_fini(&lock->l_lock);
}

/** @} end of c2_longlock group */
