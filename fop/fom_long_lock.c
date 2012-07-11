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

enum {
        /** Value of c2_long_lock_link::lll_magix */
        C2_LONG_LOCK_LINK_MAGIX = 0xB055B1E55EDF01D5
};

/**
 * Descriptor of typed list used in c2_long_lock with
 * c2_long_lock_link::lll_lock_linkage.
 */
C2_TL_DESCR_DEFINE(c2_lll, "list of lock-links in longlock", ,
                   struct c2_long_lock_link, lll_lock_linkage, lll_magix,
                   C2_LONG_LOCK_LINK_MAGIX, C2_LONG_LOCK_LINK_MAGIX);

C2_TL_DEFINE(c2_lll, , struct c2_long_lock_link);

C2_EXPORTED(c2_lll_tlink_init);
C2_EXPORTED(c2_lll_tlink_fini);

static bool prelock_check(struct c2_long_lock *lock,
			  struct c2_long_lock_link *link)
{
	bool ret;

	C2_PRE(c2_mutex_is_locked(&lock->l_lock));

	ret = !c2_lll_tlist_contains(&lock->l_owners, link) &&
	      !c2_lll_tlist_contains(&lock->l_waiters, link);

	return ret;
}

bool c2_long_read_lock(struct c2_long_lock *lock,
		       struct c2_long_lock_link *link, int next_phase)
{
	bool got_lock;
	bool waiters;
	struct c2_fom *fom;

	c2_mutex_lock(&lock->l_lock);
	C2_PRE(prelock_check(lock, link));

	fom = link->lll_fom;
	C2_ASSERT(fom);

	link->lll_lock_type = C2_LONG_LOCK_READER;
	waiters = !c2_lll_tlist_is_empty(&lock->l_waiters);
	got_lock = lock->l_state == C2_LONG_LOCK_UNLOCKED ||
		(!waiters && lock->l_state == C2_LONG_LOCK_RD_LOCKED);
	
	if (got_lock) {
		C2_CNT_INC(fom->fo_locks);
		lock->l_state = C2_LONG_LOCK_RD_LOCKED;
		c2_lll_tlist_add_tail(&lock->l_owners, link);
	} else {
		fom->fo_transitions_saved = fom->fo_transitions;
		c2_lll_tlist_add_tail(&lock->l_waiters, link);
	}

	fom->fo_phase = next_phase;

	c2_mutex_unlock(&lock->l_lock);

	return got_lock;
}

bool c2_long_write_lock(struct c2_long_lock *lock,
			struct c2_long_lock_link *link, int next_phase)
{
	bool got_lock;
	struct c2_fom *fom;

	c2_mutex_lock(&lock->l_lock);
	C2_PRE(prelock_check(lock, link));

	fom = link->lll_fom;
	C2_ASSERT(fom);

	link->lll_lock_type = C2_LONG_LOCK_WRITER;
	got_lock = lock->l_state == C2_LONG_LOCK_UNLOCKED;

	if (got_lock) {
		C2_ASSERT(c2_lll_tlist_is_empty(&lock->l_owners));
		C2_CNT_INC(fom->fo_locks);
		lock->l_state = C2_LONG_LOCK_WR_LOCKED;
		c2_lll_tlist_add_tail(&lock->l_owners, link);
	} else {
		fom->fo_transitions_saved = fom->fo_transitions;
		c2_lll_tlist_add_tail(&lock->l_waiters, link);
	}

	fom->fo_phase = next_phase;
	c2_mutex_unlock(&lock->l_lock);

	return got_lock;
}

void c2_long_write_unlock(struct c2_long_lock *lock,
			  struct c2_long_lock_link *link)
{
	struct c2_long_lock_link *grantee; /* the fom to grant the lock */
	struct c2_long_lock_link *next;
	struct c2_fom *fom;
	int grantee_lock_type;

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(lock->l_state == C2_LONG_LOCK_WR_LOCKED);
	C2_PRE(link == c2_lll_tlist_head(&lock->l_owners));
	C2_PRE(link->lll_lock_type == C2_LONG_LOCK_WRITER);

	fom = link->lll_fom;
	C2_ASSERT(fom);

	c2_lll_tlist_del(link);
	lock->l_state = C2_LONG_LOCK_UNLOCKED;
	C2_CNT_DEC(fom->fo_locks);

	C2_ASSERT(c2_lll_tlist_is_empty(&lock->l_owners));

	if (!c2_lll_tlist_is_empty(&lock->l_waiters)) {
		next = c2_lll_tlist_head(&lock->l_waiters);
		lock->l_state = next->lll_lock_type == C2_LONG_LOCK_WRITER ?
			C2_LONG_LOCK_WR_LOCKED : C2_LONG_LOCK_RD_LOCKED;

		do {
			grantee = next;
			grantee_lock_type = grantee->lll_lock_type;
			c2_lll_tlist_del(grantee);
			c2_lll_tlist_add_tail(&lock->l_owners, grantee);

			C2_CNT_INC(grantee->lll_fom->fo_locks);
			c2_fom_ready_remote(grantee->lll_fom);
			C2_ASSERT(grantee->lll_fom->fo_transitions_saved + 1
				  == grantee->lll_fom->fo_transitions);

			next = c2_lll_tlist_head(&lock->l_waiters);
		} while (next && grantee_lock_type == C2_LONG_LOCK_READER &&
			 next->lll_lock_type == grantee_lock_type);
	}

	c2_mutex_unlock(&lock->l_lock);
}

void c2_long_read_unlock(struct c2_long_lock *lock,
			 struct c2_long_lock_link *link)
{
	struct c2_long_lock_link *next;
	struct c2_long_lock_link *grantee;
	struct c2_fom *fom;
	int grantee_lock_type;

	c2_mutex_lock(&lock->l_lock);

	C2_PRE(lock->l_state == C2_LONG_LOCK_RD_LOCKED);
	C2_PRE(c2_lll_tlist_contains(&lock->l_owners, link));
	C2_PRE(link->lll_lock_type == C2_LONG_LOCK_READER);

	fom = link->lll_fom;
	C2_ASSERT(fom);

	c2_lll_tlist_del(link);
	if (c2_lll_tlist_is_empty(&lock->l_owners))
		lock->l_state = C2_LONG_LOCK_UNLOCKED;
	C2_CNT_DEC(fom->fo_locks);

	if (!c2_lll_tlist_is_empty(&lock->l_waiters) &&
	    lock->l_state == C2_LONG_LOCK_UNLOCKED) {

		next = c2_lll_tlist_head(&lock->l_waiters);
		lock->l_state = next->lll_lock_type == C2_LONG_LOCK_WRITER ?
			C2_LONG_LOCK_WR_LOCKED : C2_LONG_LOCK_RD_LOCKED;

		do {
			grantee = next;
			grantee_lock_type = grantee->lll_lock_type;
			c2_lll_tlist_del(grantee);
			c2_lll_tlist_add_tail(&lock->l_owners, grantee);

			C2_CNT_INC(grantee->lll_fom->fo_locks);
			c2_fom_ready_remote(grantee->lll_fom);
			C2_ASSERT(grantee->lll_fom->fo_transitions_saved + 1
				  == grantee->lll_fom->fo_transitions);

			next = c2_lll_tlist_head(&lock->l_waiters);
		} while (next && grantee_lock_type == C2_LONG_LOCK_READER &&
			 next->lll_lock_type == grantee_lock_type);
	}

	c2_mutex_unlock(&lock->l_lock);

}

bool c2_long_is_read_locked(struct c2_long_lock *lock,
			    struct c2_long_lock_link *link)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);

	ret = lock->l_state == C2_LONG_LOCK_RD_LOCKED &&
		c2_lll_tlist_contains(&lock->l_owners, link);

	c2_mutex_unlock(&lock->l_lock);

	return ret;
}

bool c2_long_is_write_locked(struct c2_long_lock *lock,
			     struct c2_long_lock_link *link)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);

	ret = lock->l_state == C2_LONG_LOCK_WR_LOCKED &&
		c2_lll_tlist_head(&lock->l_owners) == link;

	c2_mutex_unlock(&lock->l_lock);

	return ret;
}

void c2_long_lock_init(struct c2_long_lock *lock)
{
	c2_mutex_init(&lock->l_lock);

	c2_lll_tlist_init(&lock->l_owners);
	c2_lll_tlist_init(&lock->l_waiters);

	lock->l_state = C2_LONG_LOCK_UNLOCKED;

	c2_long_lock_bob_init(lock);
}

void c2_long_lock_fini(struct c2_long_lock *lock)
{
	C2_ASSERT(lock->l_state == C2_LONG_LOCK_UNLOCKED);

	c2_long_lock_bob_fini(lock);

	c2_lll_tlist_fini(&lock->l_waiters);
	c2_lll_tlist_fini(&lock->l_owners);

	c2_mutex_fini(&lock->l_lock);
}

/** @} end of c2_long_lock group */
