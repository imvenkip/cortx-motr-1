/* -*- C -*- */
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
 * @addtogroup c2_long_lock
 *
 * @{
 */

#include "fop/fom_long_lock.h"
#include "fop/fom_long_lock_bob.h"
#include "lib/arith.h"

/**
 * Descriptor of typed list used in c2_long_lock with c2_fom::fo_lock_linkage.
 */
C2_TL_DESCR_DEFINE(c2_fom_ll, "list of foms in longlock", ,
                   struct c2_fom, fo_lock_linkage, fo_magix,
                   C2_FOM_MAGIX, C2_FOM_MAGIX);

C2_TL_DEFINE(c2_fom_ll, , struct c2_fom);

static bool prelock_check(struct c2_long_lock *lock, struct c2_fom *fom)
{
	bool ret;

	C2_PRE(c2_mutex_is_locked(&lock->l_lock));

	ret =   !c2_fom_ll_tlist_contains(&lock->l_readers[0], fom) &&
		!c2_fom_ll_tlist_contains(&lock->l_readers[1], fom) &&
		!c2_fom_ll_tlist_contains(&lock->l_writers, fom);

	return ret;
}

bool c2_long_read_lock(struct c2_long_lock *lock,
		       struct c2_fom *fom, int next_phase)
{
	bool got_lock;
	bool writers_pending;

	c2_mutex_lock(&lock->l_lock);
	C2_PRE(prelock_check(lock, fom));

	writers_pending = !c2_fom_ll_tlist_is_empty(&lock->l_writers);
	c2_fom_ll_tlist_add_tail(writers_pending
				 ? lock->l_rd_pending
				 : lock->l_rd_in_processing,
				 fom);

	if (lock->l_state == C2_LONG_LOCK_UNLOCKED)
		lock->l_state = C2_LONG_LOCK_RD_LOCKED;

	got_lock = lock->l_state == C2_LONG_LOCK_RD_LOCKED;

	if (!got_lock)
		fom->fo_transitions_saved = fom->fo_transitions;
	else
		C2_CNT_INC(fom->fo_locks);


	fom->fo_phase = next_phase;

	c2_mutex_unlock(&lock->l_lock);

	return got_lock;
}

bool c2_long_write_lock(struct c2_long_lock *lock,
			struct c2_fom *fom, int next_phase)
{
	bool got_lock;

	c2_mutex_lock(&lock->l_lock);
	C2_PRE(prelock_check(lock, fom));
	c2_fom_ll_tlist_add_tail(&lock->l_writers, fom);

	got_lock = lock->l_state == C2_LONG_LOCK_UNLOCKED;

	if (!got_lock)
		fom->fo_transitions_saved = fom->fo_transitions;
	else {
		C2_CNT_INC(fom->fo_locks);
		lock->l_state = C2_LONG_LOCK_WR_LOCKED;
	}

	fom->fo_phase = next_phase;
	c2_mutex_unlock(&lock->l_lock);

	return got_lock;
}

void c2_long_write_unlock(struct c2_long_lock *lock, struct c2_fom *fom)
{
	struct c2_fom *grantee; /* the fom to grant the lock */

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(lock->l_state == C2_LONG_LOCK_WR_LOCKED);
	C2_PRE(c2_fom_ll_tlist_contains(&lock->l_writers, fom));

	c2_fom_ll_tlist_del(fom);
	lock->l_state = C2_LONG_LOCK_UNLOCKED;
	C2_CNT_DEC(fom->fo_locks);

	if (!c2_fom_ll_tlist_is_empty(&lock->l_writers)) {
		grantee = c2_fom_ll_tlist_head(&lock->l_writers);

		C2_ASSERT(grantee->fo_transitions_saved + 1
			  == grantee->fo_transitions);

		lock->l_state = C2_LONG_LOCK_WR_LOCKED;
		c2_fom_ready_remote(grantee);
	} else if (!c2_fom_ll_tlist_is_empty(lock->l_rd_in_processing)) {
		lock->l_state = C2_LONG_LOCK_RD_LOCKED;

		c2_tl_for(c2_fom_ll, lock->l_rd_in_processing, grantee) {
			C2_ASSERT(grantee->fo_transitions_saved + 1
				  == grantee->fo_transitions);

			c2_fom_ready_remote(grantee);
		} c2_tl_endfor;
	} else
		;

	c2_mutex_unlock(&lock->l_lock);
}

void c2_long_read_unlock(struct c2_long_lock *lock, struct c2_fom *fom)
{
	struct c2_fom *grantee; /* the fom to grant the lock */

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(lock->l_state == C2_LONG_LOCK_RD_LOCKED);
	C2_PRE(c2_fom_ll_tlist_contains(lock->l_rd_in_processing, fom));

	c2_fom_ll_tlist_del(fom);

	if (c2_fom_ll_tlist_is_empty(lock->l_rd_in_processing)) {
		lock->l_state = C2_LONG_LOCK_UNLOCKED;
		C2_SWAP(lock->l_rd_in_processing, lock->l_rd_pending);
	}

	C2_CNT_DEC(fom->fo_locks);

	if (lock->l_state == C2_LONG_LOCK_UNLOCKED) {
		C2_ASSERT(ergo(!c2_fom_ll_tlist_is_empty(lock->l_rd_in_processing),
			       !c2_fom_ll_tlist_is_empty(&lock->l_writers)));

		if (!c2_fom_ll_tlist_is_empty(&lock->l_writers)) {
			grantee = c2_fom_ll_tlist_head(&lock->l_writers);
			lock->l_state = C2_LONG_LOCK_WR_LOCKED;
			C2_ASSERT(grantee->fo_transitions_saved + 1
				  == grantee->fo_transitions);

			c2_fom_ready_remote(grantee);
		} else
			;
	}

	c2_mutex_unlock(&lock->l_lock);
}

bool c2_long_is_read_locked(struct c2_long_lock *lock, struct c2_fom *fom)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);

	ret = lock->l_state == C2_LONG_LOCK_RD_LOCKED &&
		c2_fom_ll_tlist_contains(lock->l_rd_in_processing, fom);

	c2_mutex_unlock(&lock->l_lock);

	return ret;
}

bool c2_long_is_write_locked(struct c2_long_lock *lock, struct c2_fom *fom)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);

	ret = lock->l_state == C2_LONG_LOCK_WR_LOCKED &&
		c2_fom_ll_tlist_head(&lock->l_writers) == fom;

	c2_mutex_unlock(&lock->l_lock);

	return ret;
}

void c2_long_lock_init(struct c2_long_lock *lock)
{
	c2_mutex_init(&lock->l_lock);

	c2_fom_ll_tlist_init(&lock->l_readers[0]);
	c2_fom_ll_tlist_init(&lock->l_readers[1]);
	c2_fom_ll_tlist_init(&lock->l_writers);

	lock->l_rd_in_processing = &lock->l_readers[0];
	lock->l_rd_pending = &lock->l_readers[1];

	lock->l_state = C2_LONG_LOCK_UNLOCKED;

	c2_long_lock_bob_init(lock);
}

void c2_long_lock_fini(struct c2_long_lock *lock)
{
	C2_ASSERT(lock->l_state == C2_LONG_LOCK_UNLOCKED);

	c2_long_lock_bob_fini(lock);

	c2_fom_ll_tlist_fini(&lock->l_writers);
	c2_fom_ll_tlist_fini(&lock->l_readers[0]);
	c2_fom_ll_tlist_fini(&lock->l_readers[1]);

	lock->l_rd_in_processing = NULL;
	lock->l_rd_pending = NULL;

	c2_mutex_fini(&lock->l_lock);
}

/** @} end of c2_long_lock group */
