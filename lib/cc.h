/* -*- C -*- */

#ifndef __COLIBRI_CC_H__
#define __COLIBRI_CC_H__

#include "lib/cdefs.h"

/**
   @defgroup cc Concurrency control
   @{
*/

/** waiting channel */
struct c2_chan {
};

struct c2_chan_link {
};

void c2_chan_init(struct c2_chan *chan);
void c2_chan_fini(struct c2_chan *chan);

void c2_chan_link_init(struct c2_chan_link *link);
void c2_chan_link_fini(struct c2_chan_link *link);

void c2_chan_link_add     (struct c2_chan *chan, struct c2_chan_link *link);
void c2_chan_link_del     (struct c2_chan_link *link);
bool c2_chan_link_is_armed(const struct c2_chan_link *link);

int  c2_chan_wait(struct c2_chan_link *link);

struct c2_mutex {
};

/**
   Blocking read-write lock.
*/
struct c2_rw_lock {
};

void c2_rwlock_init(struct c2_rw_lock *lock);
void c2_rwlock_fini(struct c2_rw_lock *lock);

void c2_rwlock_write_lock(struct c2_rw_lock *lock);
void c2_rwlock_write_unlock(struct c2_rw_lock *lock);

void c2_rwlock_read_lock(struct c2_rw_lock *lock);
void c2_rwlock_read_unlock(struct c2_rw_lock *lock);


struct c2_semaphore {
};

struct c2_cond {
};

/** @} end of cc group */


/* __COLIBRI_CC_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
