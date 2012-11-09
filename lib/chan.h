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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/13/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_CHAN_H__
#define __COLIBRI_LIB_CHAN_H__

#include "lib/cdefs.h"
#include "lib/tlist.h"
#include "lib/mutex.h"
#include "lib/time.h"
#include "lib/semaphore.h"

/**
   @defgroup chan Waiting channels

   Waiting channel.

   A channel (c2_chan) is a stream of asynchronous events that a channel user
   can wait or register a call-back for.

   A clink (c2_clink) is a record of interest in events on a particular
   channel. A user adds a clink to a channel and appearance of new events in
   the stream is recorded in the clink.

   There are two interfaces related to channels:

       - producer interface. It consists of c2_chan_signal(), c2_clink_signal()
         and c2_chan_broadcast() functions. These functions are called to
         declare that new asynchronous event happened in the stream.

       - consumer interface. It consists of c2_clink_add(), c2_clink_del(),
         c2_chan_wait() and c2_chan_trywait() functions.

   When a producer declares an event on a channel, this event is delivered. If
   event is a broadcast (c2_chan_broadcast()) it is delivered to all clinks
   registered with the channel. If event is a signal (c2_chan_signal()) it is
   delivered to a single clink (if any) registered with the channel. Clinks for
   delivery of consecutive signals are selected in a round-robin manner.

   A special c2_clink_signal() function is provided to signal a particular
   clink. c2_clink_signal() delivers a signal to its argument clink. This
   function does not take any locks and is designed to be used in "awkward"
   contexts, like interrupt handler or timer call-backs, where blocking on a
   lock is not allowed. Use sparingly.

   The method of delivery depends on the clink interface used (c2_clink). If
   clink has a call-back, the delivery starts with calling this call-back. If a
   clink has no call-back or the call-back returns false, the delivered event
   becomes pending on the clink. Pending events can be consumed by calls to
   c2_chan_wait(), c2_chan_timedwait() and c2_chan_trywait().

   <b>Filtered wake-ups.</b>

   By returning true from a call-back, it is possible to "filter" some events
   out and avoid potentially expensive thread wake-up. A typical use case for
   this is the following:

   @code
   struct wait_state {
           struct c2_clink f_clink;
	   ...
   };

   static bool callback(struct c2_clink *clink)
   {
           struct wait_state *f =
                   container_of(clink, struct wait_state, f_clink);
	   return !condition_is_right(f);
   }

   {
           struct wait_state g;

	   c2_clink_init(&g.f_clink, &callback);
	   c2_clink_add(chan, &g.f_clink);
	   ...
	   while (!condition_is_right(&g)) {
	           c2_chan_wait(&g.f_clink);
	   }
   }
   @endcode

   The idea behind this idiom is that the call-back is called in the same
   context where the event is declared and it is much cheaper to test whether a
   condition is right than to wake up a waiting thread that would check this
   and go back to sleep if it is not.

   <b>Multiple channels.</b>

   It is possible to wait for an event to be announced on a channel from a
   set. To this end, first a clink is created as usual. Then, additional
   (unintialised) clinks are attached to the first by a call to
   c2_clink_attach(), forming a "clink group" consisting of the original clink
   and all clinks attached. Clinks from the group can be registered with
   multiple (or the same) channels. Events announced on any channel are
   delivered to all clinks in the group.

   Groups are used as following:

       - initialise a "group head" clink;

       - attach other clinks to the group, without initialising them;

       - register the group clinks with their channels, starting with the head;

       - to wait for an event on any channel, wait on the group head.

       - call-backs can be used for event filtering on any channel as usual;

       - if N clinks from the group are registered with the same channel, an
         event in this channel will be delivered N times.

       - de-register the clinks, head last.

   @code
   struct c2_clink cl0;
   struct c2_clink cl1;

   c2_clink_init(&cl0, call_back0);
   c2_clink_attach(&cl1, &cl0, call_back1);

   c2_clink_add(chan0, &cl0);
   c2_clink_add(chan1, &cl1);

   // wait for an event on chan0 or chan1
   c2_chan_wait(&cl0);

   // de-register clinks, head last
   c2_clink_del(&cl1);
   c2_clink_del(&cl0);

   // finalise in any order
   c2_clink_fini(&cl0);
   c2_clink_fini(&cl1);
   @endcode

   @note An interface similar to c2_chan was a part of historical UNIX kernel
   implementations. It is where "CHAN" field in ps(1) output comes from.

   @todo The next scalability improvement is to allow c2_chan to use an
   externally specified mutex instead of a built-in one. This would allow
   larger state machines with multiple channels to operate under fewer locks,
   reducing coherency bus traffic.

   @{
*/

struct c2_chan;
struct c2_clink;

/**
   Clink call-back called when event is delivered to the clink. The call-back
   returns true iff the event has been "consumed". Otherwise, the event will
   remain pending on the clink for future consumption by the waiting
   interfaces.
 */
typedef bool (*c2_chan_cb_t)(struct c2_clink *link);

/**
   A stream of asynchronous events.

   <b>Concurrency control</b>

   Implementation serializes producer interface calls, but see
   c2_chan_broadcast() description. Implementation serializes c2_clink_add() and
   c2_clink_del() calls against a given channel.

   <b>Liveness</b>

   A user has to enforce a serialization between event production and channel
   destruction. Implementation guarantees that call to c2_chan_fini() first
   waits until calls to c2_chan_{signal,broadcast}() that started before this
   c2_chan_fini() call complete. This make the following idiomatic usage safe:

   @code
   struct c2_chan *complete;

   producer() {
           ...
           c2_chan_signal(complete);
   }

   consumer() {
           c2_clink_add(complete, &wait);
           for (i = 0; i < nr_producers; ++i)
                   c2_chan_wait(&wait);
           c2_clink_del(&wait);
           c2_chan_fini(complete);
           c2_free(complete);
   }
   @endcode

   <b>Invariants</b>

   c2_chan_invariant()
 */
struct c2_chan {
	/** Lock protecting other fields. */
	struct c2_mutex ch_guard;
	/** List of registered clinks. */
	struct c2_tl    ch_links;
	/** Number of clinks in c2_chan::ch_links. This is used to speed up
	    c2_chan_broadcast(). */
	uint32_t        ch_waiters;
};

/**
   A record of interest in events on a stream.

   A clink records the appearance of events in the stream.

   There are two ways to use a clink:

   @li an asynchronous call-back can be specified as an argument to clink
   constructor c2_clink_init(). This call-back is called when an event happens
   in the channel the clink is registered with. It is guaranteed that a
   call-back is executed in the same context where event producer declared new
   event. A per-channel mutex c2_chan::ch_guard is held while call-backs are
   executed (except the case when c2_clink_signal() is used by producer).

   @li once a clink is registered with a channel, it is possible to wait until
   an event happens by calling c2_chan_wait().

   See the "Filtered wake-ups" section in the top-level comment on how to
   combine call-backs with waiting.

   <b>Concurrency control</b>

   A user must guarantee that at most one thread waits on a
   clink. Synchronization between call-backs, waits and clink destruction is
   also up to user.

   A user owns a clink before call to c2_chan_add() and after return from the
   c2_chan_del() call. At any other time clink can be concurrently accessed by
   the implementation.

   <b>Liveness</b>

   A user is free to dispose a clink whenever it owns the latter.
 */
struct c2_clink {
	/** Channel this clink is registered with. */
	struct c2_chan     *cl_chan;
	/** Call-back to be called when event is declared. */
	c2_chan_cb_t        cl_cb;
	/** The head of the clink group. */
	struct c2_clink    *cl_group;
	/** Linkage into c2_chan::ch_links */
	struct c2_tlink     cl_linkage;
	struct c2_semaphore cl_wait;
	uint64_t            cl_magic;
};

C2_INTERNAL void c2_chan_init(struct c2_chan *chan);
C2_INTERNAL void c2_chan_fini(struct c2_chan *chan);

/**
   Notifies a clink currently registered with the channel that a new event
   happened.

   @see c2_chan_broadcast()
 */
C2_INTERNAL void c2_chan_signal(struct c2_chan *chan);

/**
   Notifies all clinks currently registered with the channel that a new event
   happened.

   No guarantees about behaviour in the case when clinks are added or removed
   while c2_chan_broadcast() is running.

   If clinks with call-backs (c2_clink::cl_cb) are registered with the channel
   at the time of this call, the call-backs are run to completion as part of
   broadcast.

   @see c2_chan_signal()
 */
C2_INTERNAL void c2_chan_broadcast(struct c2_chan *chan);

/**
   Notifies a given clink that a new event happened.

   This function takes no locks.

   c2_chan_signal() should be used instead, unless the event is announced in a
   context where blocking is not allowed.

   @see c2_chan_signal()
   @see c2_chan_broadcast()
 */
C2_INTERNAL void c2_clink_signal(struct c2_clink *clink);

/**
   True iff there are clinks registered with the chan.

   @note the return value of this function can, in general, be obsolete by the
   time it returns. It is up to the user to provide concurrency control
   mechanisms that would make this function useful.
 */
bool c2_chan_has_waiters(struct c2_chan *chan);

C2_INTERNAL void c2_clink_init(struct c2_clink *link, c2_chan_cb_t cb);
C2_INTERNAL void c2_clink_fini(struct c2_clink *link);

/**
   Attaches @link to a clink group. @group is the original clink in the group.
 */
C2_INTERNAL void c2_clink_attach(struct c2_clink *link,
				 struct c2_clink *group, c2_chan_cb_t cb);

/**
   Registers the clink with the channel.

   @pre !c2_clink_is_armed(link)
   @post c2_clink_is_armed(link)
 */
C2_INTERNAL void c2_clink_add(struct c2_chan *chan, struct c2_clink *link);

/**
   Un-registers the clink from the channel.

   @pre   c2_clink_is_armed(link)
   @post !c2_clink_is_armed(link)
 */
C2_INTERNAL void c2_clink_del(struct c2_clink *link);

/**
   True iff the clink is registered with a channel.
 */
C2_INTERNAL bool c2_clink_is_armed(const struct c2_clink *link);

/**
   Returns when there is an event pending in the clink. The event is consumed
   before the call returns.

   Note that this implies that if an event happened after the clink has been
   registered (by a call to c2_clink_add()) and before call to c2_chan_wait(),
   the latter returns immediately.

   User must guarantee that no more than one thread waits on the clink.
 */
C2_INTERNAL void c2_chan_wait(struct c2_clink *link);

/**
   True there is an event pending in the clink. When this function returns true,
   the event is consumed, exactly like if c2_chan_wait() were called instead.
 */
C2_INTERNAL bool c2_chan_trywait(struct c2_clink *link);

/**
   This is the same as c2_chan_wait, except that it has an expire time. If the
   time expires before event is pending, this function will return false.

   @param abs_timeout absolute time since Epoch (00:00:00, 1 January 1970)
   @return true if the there is an event pending before timeout;
   @return false if there is no events pending and timeout expires;
 */
C2_INTERNAL bool c2_chan_timedwait(struct c2_clink *link,
				   const c2_time_t abs_timeout);


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
