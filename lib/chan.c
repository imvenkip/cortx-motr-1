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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/13/2010
 */

#include "lib/errno.h"
#include "lib/chan.h"
#include "lib/assert.h"

/**
   @addtogroup chan

   A simplistic user space implementation of c2_chan and c2_clink interfaces
   based on POSIX semaphores.

   A list of registered clinks is maintained for each channel. Each clink has a
   semaphore, used to wait for pending events. When an event is declared on the
   channel, a number (depending on whether event is signalled or broadcast) of
   clinks on the channel list is scanned and for each of them either call-back
   is called or semaphore is upped.

   To wait for an event, a user downs clink semaphore.

   Semaphore is initialized every time when the clink is registered with a
   channel (c2_clink_add()) and destroyed every time the clink is deleted from a
   channel (c2_clink_del()). This guarantees that semaphore counter is exactly
   equal to the number of pending events declared on the channel.

   @note that a version of c2_chan_wait() with a timeout would induce some
   changes to the design, because in this case it is waiter who has to unlink a
   clink from a channel.

   @{
 */

C2_TL_DEFINE(clink, "chan clinks", static, struct c2_clink, cl_linkage,	cl_magic,
	     0x6368616e636c696e /* "chanclin" */,
	     0x4348414e57414954 /* "CHANWAIT" */);

/**
   Channel invariant: all clinks on the list are clinks for this channel and
   number of waiters matches list length.
 */
static bool c2_chan_invariant_locked(struct c2_chan *chan)
{
	struct c2_clink *scan;

	if (chan->ch_waiters != clink_tlist_length(&chan->ch_links))
		return false;

	c2_tlist_for(&clink_tl, &chan->ch_links, scan) {
		if (scan->cl_chan != chan)
			return false;
	} c2_tlist_endfor;
	return true;
}

static bool c2_chan_invariant(struct c2_chan *chan)
{
	bool holds;

	c2_mutex_lock(&chan->ch_guard);
	holds = c2_chan_invariant_locked(chan);
	c2_mutex_unlock(&chan->ch_guard);
	return holds;
}

void c2_chan_init(struct c2_chan *chan)
{
	clink_tlist_init(&chan->ch_links);
	c2_mutex_init(&chan->ch_guard);
	chan->ch_waiters = 0;
	C2_ASSERT(c2_chan_invariant(chan));
}
C2_EXPORTED(c2_chan_init);

void c2_chan_fini(struct c2_chan *chan)
{
	C2_ASSERT(c2_chan_invariant(chan));
	C2_ASSERT(chan->ch_waiters == 0);

	c2_mutex_lock(&chan->ch_guard);
	/*
	 * This seemingly useless lock-unlock pair is to synchronize with
	 * c2_chan_{signal,broadcast}() that might be still using chan.
	 */
	c2_mutex_unlock(&chan->ch_guard);

	c2_mutex_fini(&chan->ch_guard);
	clink_tlist_fini(&chan->ch_links);
}
C2_EXPORTED(c2_chan_fini);

static struct c2_clink *chan_head(struct c2_chan *chan)
{
	struct c2_clink *clink;

	clink = clink_tlist_head(&chan->ch_links);
	if (clink != NULL)
		clink_tlist_move_tail(&chan->ch_links, clink);
	C2_ASSERT((chan->ch_waiters > 0) == (clink != NULL));
	return clink;
}

static void clink_signal(struct c2_clink *clink)
{
	C2_ASSERT(clink->cl_chan != NULL);

	if (clink->cl_cb != NULL)
		clink->cl_cb(clink);
	else
		c2_semaphore_up(&clink->cl_wait);
}

static void chan_signal_nr(struct c2_chan *chan, uint32_t nr)
{
	uint32_t i;

	c2_mutex_lock(&chan->ch_guard);
	C2_ASSERT(c2_chan_invariant_locked(chan));
	for (i = 0; i < nr; ++i) {
		struct c2_clink *clink;

		clink = chan_head(chan);
		if (clink != NULL)
			clink_signal(clink);
		else
			break;
	}
	C2_ASSERT(c2_chan_invariant_locked(chan));
	c2_mutex_unlock(&chan->ch_guard);
}

void c2_chan_signal(struct c2_chan *chan)
{
	chan_signal_nr(chan, 1);
}
C2_EXPORTED(c2_chan_signal);

void c2_chan_broadcast(struct c2_chan *chan)
{
	chan_signal_nr(chan, chan->ch_waiters);
}
C2_EXPORTED(c2_chan_broadcast);

bool c2_chan_has_waiters(struct c2_chan *chan)
{
	C2_ASSERT(c2_chan_invariant(chan));
	return chan->ch_waiters > 0;
}
C2_EXPORTED(c2_chan_has_waiters);

void c2_clink_init(struct c2_clink *link, c2_chan_cb_t cb)
{
	link->cl_chan = NULL;
	link->cl_cb = cb;
	clink_tlink_init(link);
	/* do NOT initialise the semaphore here */
}
C2_EXPORTED(c2_clink_init);

void c2_clink_fini(struct c2_clink *link)
{
	/* do NOT finalise the semaphore here */
	clink_tlink_fini(link);
}
C2_EXPORTED(c2_clink_fini);

static void clink_lock(struct c2_clink *clink)
{
	c2_mutex_lock(&clink->cl_chan->ch_guard);
}

static void clink_unlock(struct c2_clink *clink)
{
	c2_mutex_unlock(&clink->cl_chan->ch_guard);
}

/**
   @pre  !c2_clink_is_armed(link)
   @post  c2_clink_is_armed(link)
 */
void c2_clink_add(struct c2_chan *chan, struct c2_clink *link)
{
	int rc;

	C2_PRE(!c2_clink_is_armed(link));

	link->cl_chan = chan;
	clink_lock(link);
	C2_ASSERT(c2_chan_invariant_locked(chan));
	chan->ch_waiters++;
	clink_tlist_add_tail(&chan->ch_links, link);
	rc = c2_semaphore_init(&link->cl_wait, 0);
	C2_ASSERT(rc == 0);
	C2_ASSERT(c2_chan_invariant_locked(chan));
	clink_unlock(link);

	C2_POST(c2_clink_is_armed(link));
}
C2_EXPORTED(c2_clink_add);

/**
   @pre   c2_clink_is_armed(link)
   @post !c2_clink_is_armed(link)
 */
void c2_clink_del(struct c2_clink *link)
{
	struct c2_chan *chan;

	C2_PRE(c2_clink_is_armed(link));

	clink_lock(link);
	chan = link->cl_chan;
	C2_ASSERT(c2_chan_invariant_locked(chan));
	C2_ASSERT(chan->ch_waiters > 0);
	chan->ch_waiters--;
	clink_tlist_del(link);
	C2_ASSERT(c2_chan_invariant_locked(chan));
	clink_unlock(link);

	link->cl_chan = NULL;
	c2_semaphore_fini(&link->cl_wait);

	C2_POST(!c2_clink_is_armed(link));
}
C2_EXPORTED(c2_clink_del);

bool c2_clink_is_armed(const struct c2_clink *link)
{
	return link->cl_chan != NULL;
}
C2_EXPORTED(c2_clink_is_armed);

bool c2_chan_trywait(struct c2_clink *link)
{
	bool result;

	C2_ASSERT(link->cl_cb == NULL);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	result = c2_semaphore_trydown(&link->cl_wait);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	return result;
}
C2_EXPORTED(c2_chan_trywait);

void c2_chan_wait(struct c2_clink *link)
{
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	C2_ASSERT(link->cl_cb == NULL);
	c2_semaphore_down(&link->cl_wait);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
}
C2_EXPORTED(c2_chan_wait);

bool c2_chan_timedwait(struct c2_clink *link, const c2_time_t abs_timeout)
{
	bool result;

	C2_ASSERT(link->cl_cb == NULL);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));

	result = c2_semaphore_timeddown(&link->cl_wait, abs_timeout);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	return result;
}
C2_EXPORTED(c2_chan_timedwait);

/** @} end of chan group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
