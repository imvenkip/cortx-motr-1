/* -*- C -*- */

#ifndef __COLIBRI_CC_H__
#define __COLIBRI_CC_H__

#include "lib/cdefs.h"
#include "lib/c2list.h"

/**
   @defgroup cc Concurrency control
   @{
*/

/** waiting channel */
struct c2_chan {
	struct c2_list ch_links;
};

struct c2_clink;

typedef void (*c2_chan_cb_t)(struct c2_clink *link);

struct c2_clink {
	struct c2_chan     *cl_chan;
	c2_chan_cb_t        cl_cb;
	struct c2_list_link cl_linkage;
};

void c2_chan_init(struct c2_chan *chan);
void c2_chan_fini(struct c2_chan *chan);

void c2_clink_init(struct c2_clink *link, c2_chan_cb_t cb);
void c2_clink_fini(struct c2_clink *link);

/**
   @post c2_list_contains(&link->cl_chan.ch_links, &link->cl_linkage)
 */
void c2_clink_add     (struct c2_chan *chan, struct c2_clink *link);
/**
   @pre c2_list_contains(&link->cl_chan.ch_links, &link->cl_linkage)
 */
void c2_clink_del     (struct c2_clink *link);
bool c2_clink_is_armed(const struct c2_clink *link);

int  c2_chan_wait(struct c2_clink *link);

struct c2_mutex {
};

/**
   Blocking read-write lock.
*/
struct c2_rwlock {
};

/**
   read-write lock constructor
 */
void c2_rwlock_init(struct c2_rwlock *lock);

/**
   read-write lock destructor
 */
void c2_rwlock_fini(struct c2_rwlock *lock);

/**
   take exclusive lock
 */
void c2_rwlock_write_lock(struct c2_rwlock *lock);
/**
   release exclusive lock
 */
void c2_rwlock_write_unlock(struct c2_rwlock *lock);

/**
   take shared lock
 */
void c2_rwlock_read_lock(struct c2_rwlock *lock);
/**
   release shared lock
 */
void c2_rwlock_read_unlock(struct c2_rwlock *lock);

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
