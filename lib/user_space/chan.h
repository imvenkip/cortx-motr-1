/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 12/02/2010
 */

#ifndef __COLIBRI_LIB_USER_SPACE_CHAN_H__
#define __COLIBRI_LIB_USER_SPACE_CHAN_H__

#include <semaphore.h>

/**
   @addtogroup chan

   <b>User space chan.</b>
   @{
*/

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
	struct c2_list  ch_links;
	/** Number of clinks in c2_chan::ch_links. This is used to speed up
	    c2_chan_broadcast(). */
	uint32_t        ch_waiters;
};

struct c2_clink;
typedef void (*c2_chan_cb_t)(struct c2_clink *link);
/**
   A record of interest in events on a stream.

   A clink records the appearance of events in the stream.

   There are two mutually exclusive ways to use a clink:

   @li an asynchronous call-back can be specified as an argument to clink
   constructor c2_clink_init(). This call-back is called when an event happens
   in the channel the clink is registered with. It is guaranteed that a
   call-back is executed in the same context where event producer declared new
   event. A per-channel mutex c2_chan::ch_guard is held while call-backs are
   executed.

   @li once a clink is registered with a channel, it is possible to wait until
   an event happens by calling c2_clink_wait().

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
	/** Linkage into c2_chan::ch_links */
	struct c2_list_link cl_linkage;
	/** POSIX.1-2001 semaphore for now. */
	sem_t               cl_wait;
};



/** @} end of chan group */

/* __COLIBRI_LIB_USER_SPACE_CHAN_H__ */
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
