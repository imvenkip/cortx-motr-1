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

#ifndef __COLIBRI_LONG_LOCK_H__
#define __COLIBRI_LONG_LOCK_H__

/**
 * @defgroup c2_long_lock FOM long lock
 * @page c2_long_lock-dld FOM long lock DLD
 * @section c2_long_lock-dld-ovw Overview
 * Long lock is a non-blocking synchronization construct which has to be used in
 * FOM state handlers c2_fom_ops::fo_state(). Long lock provides support for a
 * reader-writer lock for FOMs.
 *
 * @section c2_long_lock-dld-func Functional specification.
 * The following code example demonstrates the usage example of the lock:
 *
 * @code
 * static int fom_state_handler(struct c2_fom *fom)
 * {
 *	//...
 *	if (fom->fo_phase == PH_CURRENT) {
 *		// try to obtain a lock, when it's obtained FOM is
 *		// transitted into PH_GOT_LOCK
 *
 *		return C2_FOM_LONG_LOCK_RETURN(
 *		     c2_long_read_lock(lock, fom, PH_GOT_LOCK));
 *	} else if (fom->fo_phase == PH_GOT_LOCK) {
 *		// ...
 *	}
 *	//...
 * }
 * @endcode
 *
 * @section c2_long_lock-dld-logic Logical specification.
 * The lock is considered to be obtained when FOM transitions to the phase
 * specified by the next_phase parameter to the c2_long_{read|write}_lock
 * subroutine - the PH_GOT_LOCK value in the example above. Eventually, when
 * FOMs state handler does not need to read or write shared data structures,
 * access to which must be synchronized, the lock can be released with
 * c2_long_write_unlock() and c2_long_write_unlock() calls.
 *
 * The long lock uses the fo_lock_linkage field of the c2_fom structure to track
 * the FOMs that actively own the lock, and the FOMs that are queued waiting to
 * acquire the lock.  Note that this implies that a FOM can only acquire one
 * long lock.
 *
 * To avoid starvation of writers in the face of a stream of incoming readers,
 * separate lists of pending and active readers are maintained:
 *
 * - FOM-links are physically stored in c2_long_lock::l_readers[N], where N is
 *   [0, 2). c2_long_lock::l_rd_pending and c2_long_lock::l_rd_active are
 *   the pointers to the first or the second element of c2_long_lock::l_readers.
 *   c2_long_read_lock() adds new links into the list pointed by
 *   c2_long_lock::l_rd_active if the lock is obtained for reading or
 *   unlocked, otherwise into the list pointed by c2_long_lock::l_rd_pending
 *   (the last avoids starvation).
 * - When a write lock is released and there are no active readers, FOMs
 *   pending for processing (if any) are put into
 *   c2_long_lock::l_rd_active queue. This done by swapping the
 *   c2_long_lock::l_rd_pending and c2_long_lock::l_rd_active pointers
 *   (C2_SWAP).
 *
 * When some lock is being unlocked the work of selecting the next lock owner(s)
 * and waking them up is done in the unlock path in c2_long_read_unlock() and
 * c2_long_write_unlock() with respect to c2_long_lock::l_rd_active,
 * c2_long_lock::l_rd_pending and c2_long_lock::l_writers lists.
 * c2_fom_ready_remote() call is used to wake FOMs up.
 *
 * @{
 */

#include "fop/fop.h"
#include "lib/list.h"
#include "lib/chan.h"
#include "lib/mutex.h"
#include "lib/bob.h"
#include "fop/fom_long_lock.h"

/**
 * Long lock states.
 */
enum c2_long_lock_state {
	C2_LONG_LOCK_UNLOCKED,
	C2_LONG_LOCK_RD_LOCKED,
	C2_LONG_LOCK_WR_LOCKED
};

enum c2_long_lock_type {
	C2_LONG_LOCK_READER,
	C2_LONG_LOCK_WRITER
};

struct c2_long_lock_link {
	struct c2_fom		*lll_fom;
	/** Linkage to struct c2_long_lock::l_{owners,waiters} list */
	struct c2_tlink		 lll_lock_linkage;
	/** magic number. C2_FOM_MAGIX */
	uint64_t		 lll_magix;
	/** . */
	enum c2_long_lock_type   lll_lock_type;
};

/**
 * Long lock structure.
 */
struct c2_long_lock {
	/** 
	 *  List of readers pending the lock or holding it. l_rd_pending and
	 *  l_rd_active point into l_readers[].
	 */
	/* struct c2_tl		l_readers[2]; */
	/* struct c2_tl	       *l_rd_pending; */
	/* struct c2_tl	       *l_rd_active; */
	/** List of writers pending the lock, or writer, which holds it */
	/* struct c2_tl		l_writers; */

	struct c2_tl		l_owners;
	struct c2_tl		l_waiters;
	/** Mutex used to protect the structure from concurrent access. */
	struct c2_mutex		l_lock;
	/** State of the lock */
	enum c2_long_lock_state l_state;
	/** Magic number. C2_LONG_LOCK_MAGIX */
	uint64_t		l_magix;
};

C2_TL_DESCR_DECLARE(c2_lll, extern);
C2_TL_DECLARE(c2_lll, extern, struct c2_long_lock_link);

#define C2_FOM_LONG_LOCK_RETURN(rc) ((rc) ? C2_FSO_AGAIN : C2_FSO_WAIT)

/**
 * @post lock->l_state == C2_LONG_LOCK_UNLOCKED
 * @post c2_mutex_is_not_locked(&lock->l_lock)
 * @post !c2_chan_has_waiters(&lock->l_wait)
 */
void c2_long_lock_init(struct c2_long_lock *lock);

/**
 * @pre !c2_long_is_read_locked(lock, *)
 * @pre !c2_long_is_write_locked(lock, *)
 */
void c2_long_lock_fini(struct c2_long_lock *lock);

/**
 * Obtains given lock for reading for given fom. Taking recursive read-lock is
 * not permitted. If the lock is not obtained the invoking FOM should wait; it
 * will eventually be awoken when the lock has been obtained, and will be
 * transitioned to the next_phase state.
 *
 * @param fom - FOM which has to obtain the lock.
 *
 * @pre !c2_long_is_read_locked(lock, fom)
 * @post fom->fo_phase == next_phase
 *
 * @return true iff the lock is taken.
 */
bool c2_long_read_lock(struct c2_long_lock *lock,
		       struct c2_long_lock_link *link, int next_phase);

/**
 * Obtains given lock for writing for given fom. Taking recursive write-lock is
 * not permitted. If the lock is not obtained the invoking FOM should wait; it
 * will eventually be awoken when the lock has been obtained, and will be
 * transitioned to the next_phase state.
 *
 * @param fom - FOM which has to obtain the lock.
 *
 * @pre !c2_long_is_write_locked(lock, fom)
 * @post fom->fo_phase == next_phase
 *
 * @return true iff the lock is taken.
 */
bool c2_long_write_lock(struct c2_long_lock *lock,
			struct c2_long_lock_link *link, int next_phase);

/**
 * Unlocks given read-lock.
 *
 * @param fom - FOM which has obtained the lock.
 *
 * @pre c2_long_is_read_locked(lock, fom);
 * @post !c2_long_is_read_locked(lock, fom);
 */
void c2_long_read_unlock(struct c2_long_lock *lock,
			 struct c2_long_lock_link *link);

/**
 * Unlocks given write-lock.
 *
 * @param fom - FOM which has obtained the lock.
 *
 * @pre c2_long_is_write_locked(lock, fom);
 * @post !c2_long_is_write_locked(lock, fom);
 */
void c2_long_write_unlock(struct c2_long_lock *lock,
			  struct c2_long_lock_link *link);

/**
 * @return true iff the lock is taken as a read-lock by the given fom.
 */
bool c2_long_is_read_locked(struct c2_long_lock *lock,
			    struct c2_long_lock_link *link);

/**
 * @return true iff the lock is taken as a write-lock by the given fom.
 */
bool c2_long_is_write_locked(struct c2_long_lock *lock,
			     struct c2_long_lock_link *link);

/** @} end of c2_long_lock group */

#endif /* __COLIBRI_LONG_LOCK_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
