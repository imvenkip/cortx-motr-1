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

#ifndef __COLIBRI_LONG_LOCK_H__
#define __COLIBRI_LONG_LOCK_H__

/**
 * @defgroup c2_longlock "FOM Long lock"
 *
 * <b>FOM Long lock</b>
 *
 * @{
 */

#include "fop/fop.h"
#include "lib/list.h"
#include "lib/chan.h"
#include "lib/mutex.h"

enum c2_longlock_state {
	C2_LONG_LOCK_UNLOCKED,
	C2_LONG_LOCK_RD_LOCKED,
	C2_LONG_LOCK_WR_LOCKED
};

/**
 * Long lock is a non-blocking synchronization primitive which has to
 * be used in FOM state handlers c2_fom_ops::fo_state().
 * The following code example demonstrates the usage example of the
 * lock:
 * @code
 * static int fom_state_handler(struct c2_fom *fom)
 * {
 *	//...
 *	if (fom->fo_phase == PH_CURRENT) {
 *		// try to obtain a lock, when it's obtained FOM is
 *		// transitted into PH_NEXT
 *
 *		if (!c2_long_read_lock(lock, fom, PH_NEXT))
 *			return C2_FSO_WAIT;
 *
 *              C2_ASSERT(c2_long_is_read_locked());
 *		fom->fo_phase = PH_NEXT;
 *		return C2_FSO_AGAIN;
 *	} else if (fom->fo_phase == PH_NEXT) {
 *		// ...
 *	}
 *	//...
 * }
 * @endcode
 * The lock is considered to be obtained when FOM is transited into
 * the PH_NEXT phase.
 */
struct c2_longlock {
	/** Lists of peding and processing readers of the lock */
	struct c2_list  l_readers[2];
	struct c2_list *l_rd_pending;
	struct c2_list *l_rd_in_processing;

	/** List of writers pending the lock, or writers which have
	 * already taken it */
	struct c2_list  l_writers;
	/** Channel, signalled by callback, when the fom-lock is taken */
	struct c2_chan  l_wait;
	/** Mutex used to protect the structure from concurrent access */
	struct c2_mutex l_lock;
	/** State of the lock */
	enum c2_longlock_state l_state;
	/** Transition counter since lock was obtained */
	int l_transitions;
};

/**
 * @post lock->l_state == C2_LONG_LOCK_UNLOCKED
 * @post c2_mutex_is_not_locked(&lock->l_lock)
 * @post !c2_chan_has_waiters(&lock->l_wait)
 */
void c2_long_lock_init(struct c2_longlock *lock);

/**
 * @pre !c2_long_is_read_locked(lock)
 * @pre !c2_long_is_write_locked(lock)
 */
void c2_long_lock_fini(struct c2_longlock *lock);

/**
 * Obtains given lock for reading for given fom.
 *
 * @param fom - FOM which has to obtain the lock.
 *
 * @return true iff the lock is taken. Arms the callback that will
 * wake this fom when the lock should be re-tried and returns false
 * otherwise.
 */
bool c2_long_read_lock(struct c2_longlock *lock,
		       struct c2_fom *fom, int next_phase);

/**
 * Obtains given lock for writing for given fom.
 *
 * @param fom - FOM which has to obtain the lock.
 *
 * @return true iff the lock is taken. Arms the callback that will
 * wake this fom when the lock should be re-tried and returns false
 * otherwise.
 */
bool c2_long_write_lock(struct c2_longlock *lock,
			struct c2_fom *fom, int next_phase);

/**
 * Unlocks given read-lock.
 *
 * @param fom - FOM which has obtained the lock.
 * @pre lock has been already taken by given fom.
 */
void c2_long_read_unlock(struct c2_longlock *lock, struct c2_fom *fom);

/**
 * Unlocks given write-lock.
 *
 * @param fom - FOM which has obtained the lock.
 * @pre lock has been already taken by given fom.
 */
void c2_long_write_unlock(struct c2_longlock *lock, struct c2_fom *fom);

/**
 * @return true iff the lock is taken as a read-lock
 */
bool c2_long_is_read_locked(struct c2_longlock *lock);

/**
 * @return true iff the lock is taken as a write-lock
 */
bool c2_long_is_write_locked(struct c2_longlock *lock);

/** @} end of c2_longlock group */

#endif /* __COLIBRI_LONG_LOCK_H__ */
