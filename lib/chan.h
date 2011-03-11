/* -*- C -*- */

#ifndef __COLIBRI_LIB_CHAN_H__
#define __COLIBRI_LIB_CHAN_H__

#include "lib/cdefs.h"
#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/time.h"

/**
   @defgroup chan Waiting channels

   Waiting channel.

   A channel (c2_chan) is a stream of asynchronous events that a channel user
   can wait or register a call-back for.

   A clink (c2_clink) is a record of interest in events on a particular
   channel. A user adds a clink to a channel and appearance of new events in the
   stream is recorded in the clink.

   There are two interfaces related to channels:

   @li producer interface. It consists of c2_chan_signal() and
   c2_chan_broadcast() functions. These functions are called to declare that new
   asynchronous event happened in the stream.

   @li consumer interface. It consists of c2_clink_add(), c2_clink_del(),
   c2_clink_wait() and c2_clink_trywait() functions.

   When a producer declares an event on a channel, this event is delivered. If
   event is a broadcast (c2_chan_broadcast()) it is delivered to all clinks
   registered with the channel. If event is a signal (c2_chan_signal()) it is
   delivered to a single clink (if any) registered with the channel. Clinks for
   delivery of consecutive signals are selected in a round-robin manner.

   The method of delivery depends on the clink interface used (c2_clink). If
   clink has a call-back, the delivery consists in calling this call-back. If a
   clink has no call-back, the delivered event becomes pending on the
   clink. Pending events can be consumed by calls to c2_chan_wait() and
   c2_chan_trywait().

   @note An interface similar to c2_chan was a part of historical UNIX kernel
   implementations. It is where "CHAN" field in ps(1) output comes from.

   @{
*/


#ifndef __KERNEL__
#include "lib/user_space/chan.h"
#else
#include "lib/linux_kernel/chan.h"
#endif

void c2_chan_init(struct c2_chan *chan);
void c2_chan_fini(struct c2_chan *chan);

/**
   Notify a clink currently registered with the channel that a new event
   happened.

   @see c2_chan_broadcast()
 */
void c2_chan_signal(struct c2_chan *chan);

/**
   Notify all clinks currently registered with the channel that a new event
   happened.

   No guarantees about behaviour in the case when clinks are added or removed
   while c2_chan_broadcast() is running.

   If clinks with call-backs (c2_clink::cl_cb) are registered with the channel
   at the time of this call, the call-backs are run to completion as part of
   broadcast.

   @see c2_chan_signal()
 */
void c2_chan_broadcast(struct c2_chan *chan);

/**
   True iff there are clinks registered with the chan.

   @note the return value of this function can, in general, be obsolete by the
   time it returns. It is up to the user to provide concurrency control
   mechanisms that would make this function useful.
 */
bool c2_chan_has_waiters(struct c2_chan *chan);

void c2_clink_init(struct c2_clink *link, c2_chan_cb_t cb);
void c2_clink_fini(struct c2_clink *link);

/**
   Register the clink with the channel.

   @pre !c2_clink_is_armed(link)
   @post c2_clink_is_armed(link)
 */
void c2_clink_add     (struct c2_chan *chan, struct c2_clink *link);

/**
   Un-register the clink from the channel.

   @pre   c2_clink_is_armed(link)
   @post !c2_clink_is_armed(link)
 */
void c2_clink_del     (struct c2_clink *link);

/**
   True iff the clink is registered with a channel.
 */
bool c2_clink_is_armed(const struct c2_clink *link);

/**
   Returns when there is an event pending in the clink. The event is consumed
   before the call returns.

   Note that this implies that if an event happened after the clink has been
   registered (by a call to c2_clink_add()) and before call to c2_chan_wait(),
   the latter returns immediately.

   User must guarantee that no more than one thread waits on the clink.
 */
void c2_chan_wait(struct c2_clink *link);

/**
   True there is an event pending in the clink. When this function returns true,
   the event is consumed, exactly like if c2_chan_wait() were called instead.
 */
bool c2_chan_trywait(struct c2_clink *link);

/**
   This is the same as c2_chan_wait, except that it has an expire time. If the
   time expires before event is pending, this function will return false.

   @param abs_timeout absolute time since Epoch (00:00:00, 1 January 1970)
   @return true if the there is an event pending before timeout.
   @return false if there is no events pending and timeout expires. errno is ETIMEDOUT;
 */
bool c2_chan_timedwait(struct c2_clink *link, const struct c2_time *abs_timeout);


/** @} end of chan group */


/* __COLIBRI_LIB_CHAN_H__ */
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
