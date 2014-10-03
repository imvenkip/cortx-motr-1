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
 * @addtogroup m0_long_lock_API
 *
 * @{
 */

#include "fop/fom_long_lock.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/bob.h"
#include "mero/magic.h"

/**
 * Descriptor of typed list used in m0_long_lock with
 * m0_long_lock_link::lll_lock_linkage.
 */
M0_TL_DESCR_DEFINE(m0_lll, "list of lock-links in longlock", M0_INTERNAL,
                   struct m0_long_lock_link, lll_lock_linkage, lll_magix,
                   M0_FOM_LL_LINK_MAGIC, M0_FOM_LL_LINK_MAGIC);

M0_TL_DEFINE(m0_lll, M0_INTERNAL, struct m0_long_lock_link);

static const struct m0_bob_type long_lock_bob = {
	.bt_name         = "LONG_LOCK_BOB",
	.bt_magix        = M0_FOM_LL_MAGIC,
	.bt_magix_offset = offsetof(struct m0_long_lock, l_magix)
};

M0_BOB_DEFINE(M0_INTERNAL, &long_lock_bob, m0_long_lock);

static struct m0_bob_type long_lock_link_bob;
M0_BOB_DEFINE(M0_INTERNAL, &long_lock_link_bob, m0_long_lock_link);

M0_INTERNAL void m0_fom_ll_global_init(void)
{
	m0_bob_type_tlist_init(&long_lock_link_bob, &m0_lll_tl);
}
M0_EXPORTED(m0_fom_ll_global_init);

M0_INTERNAL void m0_long_lock_link_init(struct m0_long_lock_link *link,
					struct m0_fom *fom)
{
	M0_PRE(fom != NULL);
	m0_lll_tlink_init(link);
	link->lll_fom = fom;
}

M0_INTERNAL void m0_long_lock_link_fini(struct m0_long_lock_link *link)
{
	M0_PRE(!m0_lll_tlink_is_in(link));
	link->lll_fom = NULL;
	m0_lll_tlink_fini(link);
}

static bool link_invariant(const struct m0_long_lock_link *link)
{
	return
		m0_long_lock_link_bob_check(link) &&
		link->lll_fom != NULL &&
		M0_IN(link->lll_lock_type, (M0_LONG_LOCK_READER,
					    M0_LONG_LOCK_WRITER));
}

/**
 * This invariant is established by m0_long_lock_init(). Every top-level long
 * lock entry point assumes that this invariant holds right after the lock's
 * mutex is taken and restores the invariant before releasing the mutex.
 */
static bool lock_invariant(const struct m0_long_lock *lock)
{
	struct m0_long_lock_link *last;
	struct m0_long_lock_link *first;

	last  = m0_lll_tlist_tail(&lock->l_owners);
	first = m0_lll_tlist_head(&lock->l_waiters);

	return
		m0_long_lock_bob_check(lock) &&
		m0_mutex_is_locked(&lock->l_lock) &&
		M0_IN(lock->l_state, (M0_LONG_LOCK_UNLOCKED,
				      M0_LONG_LOCK_RD_LOCKED,
				      M0_LONG_LOCK_WR_LOCKED)) &&
		m0_tl_forall(m0_lll, l, &lock->l_owners, link_invariant(l)) &&
		m0_tl_forall(m0_lll, l, &lock->l_waiters, link_invariant(l)) &&

		(lock->l_state == M0_LONG_LOCK_UNLOCKED) ==
			(m0_lll_tlist_is_empty(&lock->l_owners) &&
			 m0_lll_tlist_is_empty(&lock->l_waiters)) &&

		(lock->l_state == M0_LONG_LOCK_RD_LOCKED) ==
			(!m0_lll_tlist_is_empty(&lock->l_owners) &&
			 m0_tl_forall(m0_lll, l, &lock->l_owners,
				   l->lll_lock_type == M0_LONG_LOCK_READER)) &&

		ergo((lock->l_state == M0_LONG_LOCK_WR_LOCKED),
		     (m0_lll_tlist_length(&lock->l_owners) == 1)) &&

		ergo(first != NULL, last != NULL) &&

		ergo(last != NULL && first != NULL,
		     ergo(last->lll_lock_type == M0_LONG_LOCK_READER,
			  first->lll_lock_type == M0_LONG_LOCK_WRITER));
}

/**
 * True, iff "link" can acquire "lock", provided "link" is at the head of
 * waiters queue.
 */
static bool can_lock(const struct m0_long_lock *lock,
		     const struct m0_long_lock_link *link)
{
	return link->lll_lock_type == M0_LONG_LOCK_READER ?
		lock->l_state != M0_LONG_LOCK_WR_LOCKED :
		lock->l_state == M0_LONG_LOCK_UNLOCKED;
}

static void grant(struct m0_long_lock *lock, struct m0_long_lock_link *link)
{
	lock->l_state = link->lll_lock_type == M0_LONG_LOCK_READER ?
		M0_LONG_LOCK_RD_LOCKED : M0_LONG_LOCK_WR_LOCKED;

	m0_lll_tlist_move_tail(&lock->l_owners, link);
}

static bool lock(struct m0_long_lock *lock, struct m0_long_lock_link *link,
		 int next_phase)
{
	bool got_lock;
	struct m0_fom *fom;

	m0_mutex_lock(&lock->l_lock);
	M0_PRE(lock_invariant(lock));
	M0_PRE(!m0_lll_tlink_is_in(link));

	fom = link->lll_fom;
	M0_PRE(fom != NULL);

	got_lock = m0_lll_tlist_is_empty(&lock->l_waiters) &&
		can_lock(lock, link);
	if (got_lock) {
		grant(lock, link);
	} else {
		fom->fo_transitions_saved = fom->fo_transitions;
		m0_lll_tlist_add_tail(&lock->l_waiters, link);
	}
	m0_fom_phase_set(fom, next_phase);
	M0_POST(lock_invariant(lock));
	m0_mutex_unlock(&lock->l_lock);
	return got_lock;
}

M0_INTERNAL bool m0_long_write_lock(struct m0_long_lock *lk,
				    struct m0_long_lock_link *link,
				    int next_phase)
{
	link->lll_lock_type = M0_LONG_LOCK_WRITER;
	return lock(lk, link, next_phase);
}

M0_INTERNAL bool m0_long_read_lock(struct m0_long_lock *lk,
				   struct m0_long_lock_link *link,
				   int next_phase)
{
	link->lll_lock_type = M0_LONG_LOCK_READER;
	return lock(lk, link, next_phase);
}

static void unlock(struct m0_long_lock *lock, struct m0_long_lock_link *link)
{
	struct m0_fom            *fom;
	struct m0_long_lock_link *next;

	fom = link->lll_fom;

	m0_mutex_lock(&lock->l_lock);
	M0_PRE(lock_invariant(lock));
	M0_PRE(m0_lll_tlist_contains(&lock->l_owners, link));
	M0_PRE(m0_fom_group_is_locked(fom));

	m0_lll_tlist_del(link);
	lock->l_state =
		link->lll_lock_type == M0_LONG_LOCK_WRITER ||
		m0_lll_tlist_is_empty(&lock->l_owners) ?
		M0_LONG_LOCK_UNLOCKED : M0_LONG_LOCK_RD_LOCKED;

	while ((next = m0_lll_tlist_head(&lock->l_waiters)) != NULL &&
	       can_lock(lock, next)) {
		grant(lock, next);

		M0_ASSERT(next->lll_fom->fo_transitions_saved + 1
			  == next->lll_fom->fo_transitions);
		m0_fom_wakeup(next->lll_fom);
	}

	M0_POST(lock_invariant(lock));
	m0_mutex_unlock(&lock->l_lock);
}

M0_INTERNAL void m0_long_write_unlock(struct m0_long_lock *lock,
				      struct m0_long_lock_link *link)
{
	unlock(lock, link);
}

M0_INTERNAL void m0_long_read_unlock(struct m0_long_lock *lock,
				     struct m0_long_lock_link *link)
{
	unlock(lock, link);
}

M0_INTERNAL bool m0_long_is_read_locked(struct m0_long_lock *lock,
					const struct m0_fom *fom)
{
	bool ret;

	m0_mutex_lock(&lock->l_lock);
	M0_ASSERT(lock_invariant(lock));
	ret = lock->l_state == M0_LONG_LOCK_RD_LOCKED &&
		m0_tl_exists(m0_lll, link, &lock->l_owners,
			     link->lll_fom == fom);
	m0_mutex_unlock(&lock->l_lock);
	return ret;
}

M0_INTERNAL bool m0_long_is_write_locked(struct m0_long_lock *lock,
					 const struct m0_fom *fom)
{
	bool ret;

	m0_mutex_lock(&lock->l_lock);
	M0_ASSERT(lock_invariant(lock));

	ret = lock->l_state == M0_LONG_LOCK_WR_LOCKED &&
		m0_lll_tlist_head(&lock->l_owners)->lll_fom == fom;

	m0_mutex_unlock(&lock->l_lock);

	return ret;
}

M0_INTERNAL void m0_long_lock_init(struct m0_long_lock *lock)
{
	m0_mutex_init(&lock->l_lock);

	m0_lll_tlist_init(&lock->l_owners);
	m0_lll_tlist_init(&lock->l_waiters);

	lock->l_state = M0_LONG_LOCK_UNLOCKED;

	m0_long_lock_bob_init(lock);
}

M0_INTERNAL void m0_long_lock_fini(struct m0_long_lock *lock)
{
	M0_ASSERT(lock->l_state == M0_LONG_LOCK_UNLOCKED);

	m0_long_lock_bob_fini(lock);

	m0_lll_tlist_fini(&lock->l_waiters);
	m0_lll_tlist_fini(&lock->l_owners);

	m0_mutex_fini(&lock->l_lock);
}

/** @} end of m0_long_lock group */
