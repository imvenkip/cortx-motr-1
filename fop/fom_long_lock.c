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
 * @addtogroup c2_long_lock_API
 *
 * @{
 */

#include "fop/fom_long_lock.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/bob.h"
#include "colibri/magic.h"

/**
 * Descriptor of typed list used in c2_long_lock with
 * c2_long_lock_link::lll_lock_linkage.
 */
C2_TL_DESCR_DEFINE(c2_lll, "list of lock-links in longlock", ,
                   struct c2_long_lock_link, lll_lock_linkage, lll_magix,
                   C2_FOM_LL_LINK_MAGIC, C2_FOM_LL_LINK_MAGIC);

C2_TL_DEFINE(c2_lll, , struct c2_long_lock_link);

static const struct c2_bob_type long_lock_bob = {
	.bt_name         = "LONG_LOCK_BOB",
	.bt_magix        = C2_FOM_LL_MAGIC,
	.bt_magix_offset = offsetof(struct c2_long_lock, l_magix)
};

C2_BOB_DEFINE(, &long_lock_bob, c2_long_lock);

static struct c2_bob_type long_lock_link_bob;
C2_BOB_DEFINE(, &long_lock_link_bob, c2_long_lock_link);

void c2_fom_ll_global_init(void)
{
	c2_bob_type_tlist_init(&long_lock_link_bob, &c2_lll_tl);
}
C2_EXPORTED(c2_fom_ll_global_init);

void c2_long_lock_link_init(struct c2_long_lock_link *link, struct c2_fom *fom)
{
	C2_PRE(fom != NULL);
	c2_lll_tlink_init(link);
	link->lll_fom = fom;
}

void c2_long_lock_link_fini(struct c2_long_lock_link *link)
{
	C2_PRE(!c2_lll_tlink_is_in(link));
	link->lll_fom = NULL;
	c2_lll_tlink_fini(link);
}

static bool link_invariant(const struct c2_long_lock_link *link)
{
	return
		c2_long_lock_link_bob_check(link) &&
		link->lll_fom != NULL &&
		C2_IN(link->lll_lock_type, (C2_LONG_LOCK_READER,
					    C2_LONG_LOCK_WRITER));
}

/**
 * This invariant is established by c2_long_lock_init(). Every top-level long
 * lock entry point assumes that this invariant holds right after the lock's
 * mutex is taken and restores the invariant before releasing the mutex.
 */
static bool lock_invariant(const struct c2_long_lock *lock)
{
	struct c2_long_lock_link *last;
	struct c2_long_lock_link *first;

	last  = c2_lll_tlist_tail(&lock->l_owners);
	first = c2_lll_tlist_head(&lock->l_waiters);

	return
		c2_long_lock_bob_check(lock) &&
		c2_mutex_is_locked(&lock->l_lock) &&
		C2_IN(lock->l_state, (C2_LONG_LOCK_UNLOCKED,
				      C2_LONG_LOCK_RD_LOCKED,
				      C2_LONG_LOCK_WR_LOCKED)) &&
		c2_tl_forall(c2_lll, l, &lock->l_owners, link_invariant(l)) &&
		c2_tl_forall(c2_lll, l, &lock->l_waiters, link_invariant(l)) &&

		(lock->l_state == C2_LONG_LOCK_UNLOCKED) ==
			(c2_lll_tlist_is_empty(&lock->l_owners) &&
			 c2_lll_tlist_is_empty(&lock->l_waiters)) &&

		(lock->l_state == C2_LONG_LOCK_RD_LOCKED) ==
			(!c2_lll_tlist_is_empty(&lock->l_owners) &&
			 c2_tl_forall(c2_lll, l, &lock->l_owners,
				      l->lll_lock_type == C2_LONG_LOCK_READER))&&

		ergo((lock->l_state == C2_LONG_LOCK_WR_LOCKED),
		     (c2_lll_tlist_length(&lock->l_owners) == 1)) &&

		ergo(first != NULL, last != NULL) &&

		ergo(last != NULL && first != NULL,
		     ergo(last->lll_lock_type == C2_LONG_LOCK_READER,
			  first->lll_lock_type == C2_LONG_LOCK_WRITER));
}

/**
 * True, iff "link" can acquire "lock", provided "link" is at the head of
 * waiters queue.
 */
static bool can_lock(const struct c2_long_lock *lock,
		     const struct c2_long_lock_link *link)
{
	return link->lll_lock_type == C2_LONG_LOCK_READER ?
		lock->l_state != C2_LONG_LOCK_WR_LOCKED :
		lock->l_state == C2_LONG_LOCK_UNLOCKED;
}

static void grant(struct c2_long_lock *lock, struct c2_long_lock_link *link)
{
	lock->l_state = link->lll_lock_type == C2_LONG_LOCK_READER ?
		C2_LONG_LOCK_RD_LOCKED : C2_LONG_LOCK_WR_LOCKED;

	c2_lll_tlist_move_tail(&lock->l_owners, link);
}

static bool lock(struct c2_long_lock *lock, struct c2_long_lock_link *link,
		 int next_phase)
{
	bool got_lock;
	struct c2_fom *fom;

	c2_mutex_lock(&lock->l_lock);
	C2_PRE(lock_invariant(lock));
	C2_PRE(!c2_lll_tlink_is_in(link));

	fom = link->lll_fom;
	C2_PRE(fom != NULL);

	got_lock = c2_lll_tlist_is_empty(&lock->l_waiters) &&
		can_lock(lock, link);
	if (got_lock) {
		grant(lock, link);
	} else {
		fom->fo_transitions_saved = fom->fo_transitions;
		c2_lll_tlist_add_tail(&lock->l_waiters, link);
	}
	c2_fom_phase_set(fom, next_phase);
	C2_POST(lock_invariant(lock));
	c2_mutex_unlock(&lock->l_lock);
	return got_lock;
}

bool c2_long_write_lock(struct c2_long_lock *lk,
			struct c2_long_lock_link *link, int next_phase)
{
	link->lll_lock_type = C2_LONG_LOCK_WRITER;
	return lock(lk, link, next_phase);
}

bool c2_long_read_lock(struct c2_long_lock *lk,
		       struct c2_long_lock_link *link, int next_phase)
{
	link->lll_lock_type = C2_LONG_LOCK_READER;
	return lock(lk, link, next_phase);
}

static void unlock(struct c2_long_lock *lock, struct c2_long_lock_link *link)
{
	struct c2_fom            *fom;
	struct c2_long_lock_link *next;

	fom = link->lll_fom;

	c2_mutex_lock(&lock->l_lock);
	C2_PRE(lock_invariant(lock));
	C2_PRE(c2_lll_tlist_contains(&lock->l_owners, link));
	C2_PRE(c2_fom_group_is_locked(fom));

	c2_lll_tlist_del(link);
	lock->l_state =
		link->lll_lock_type == C2_LONG_LOCK_WRITER ||
		c2_lll_tlist_is_empty(&lock->l_owners) ?
		C2_LONG_LOCK_UNLOCKED : C2_LONG_LOCK_RD_LOCKED;

	while ((next = c2_lll_tlist_head(&lock->l_waiters)) != NULL &&
	       can_lock(lock, next)) {
		grant(lock, next);

		C2_ASSERT(next->lll_fom->fo_transitions_saved + 1
			  == next->lll_fom->fo_transitions);
		c2_fom_wakeup(next->lll_fom);
	}

	C2_POST(lock_invariant(lock));
	c2_mutex_unlock(&lock->l_lock);
}

void c2_long_write_unlock(struct c2_long_lock *lock,
			  struct c2_long_lock_link *link)
{
	unlock(lock, link);
}

void c2_long_read_unlock(struct c2_long_lock *lock,
			 struct c2_long_lock_link *link)
{
	unlock(lock, link);
}

bool c2_long_is_read_locked(struct c2_long_lock *lock,
			    const struct c2_fom *fom)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);
	C2_ASSERT(lock_invariant(lock));
	ret = lock->l_state == C2_LONG_LOCK_RD_LOCKED &&
		!c2_tl_forall(c2_lll, link, &lock->l_owners,
			      link->lll_fom != fom);
	c2_mutex_unlock(&lock->l_lock);
	return ret;
}

bool c2_long_is_write_locked(struct c2_long_lock *lock,
			     const struct c2_fom *fom)
{
	bool ret;

	c2_mutex_lock(&lock->l_lock);
	C2_ASSERT(lock_invariant(lock));

	ret = lock->l_state == C2_LONG_LOCK_WR_LOCKED &&
		c2_lll_tlist_head(&lock->l_owners)->lll_fom == fom;

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
