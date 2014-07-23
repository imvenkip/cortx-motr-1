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

#include "lib/thread.h"
#include "lib/errno.h"
#include "lib/chan.h"
#include "lib/assert.h"
#include "mero/magic.h"

/**
   @addtogroup chan

   A simplistic user space implementation of m0_chan and m0_clink interfaces
   based on POSIX semaphores.

   A list of registered clinks is maintained for each channel. Each clink has a
   semaphore, used to wait for pending events. When an event is declared on the
   channel, a number (depending on whether event is signalled or broadcast) of
   clinks on the channel list is scanned and for each of them either call-back
   is called or semaphore is upped.

   To wait for an event, a user downs clink semaphore.

   Semaphore is initialized every time when the clink is registered with a
   channel (m0_clink_add()) and destroyed every time the clink is deleted from a
   channel (m0_clink_del()). This guarantees that semaphore counter is exactly
   equal to the number of pending events declared on the channel.

   @note that a version of m0_chan_wait() with a timeout would induce some
   changes to the design, because in this case it is waiter who has to unlink a
   clink from a channel.

   @{
 */

M0_TL_DESCR_DEFINE(clink,
		   "chan clinks", static, struct m0_clink, cl_linkage,	cl_magic,
		   M0_LIB_CHAN_MAGIC, M0_LIB_CHAN_HEAD_MAGIC);

M0_TL_DEFINE(clink, static, struct m0_clink);

static bool clink_is_head(const struct m0_clink *clink)
{
	return clink->cl_group == clink;
}

static void chan_lock(struct m0_chan *ch)
{
	m0_mutex_lock(ch->ch_guard);
}

static void chan_unlock(struct m0_chan *ch)
{
	m0_mutex_unlock(ch->ch_guard);
}

static bool chan_is_locked(const struct m0_chan *ch)
{
	return m0_mutex_is_locked(ch->ch_guard);
}

/**
   Channel invariant: all clinks on the list are clinks for this channel and
   number of waiters matches list length.
 */
static bool m0_chan_invariant(struct m0_chan *chan)
{
	return chan->ch_waiters == clink_tlist_length(&chan->ch_links) &&
		m0_tl_forall(clink, scan, &chan->ch_links,
			     scan->cl_chan == chan &&
			     scan->cl_group != NULL &&
			     clink_is_head(scan->cl_group));
}

M0_INTERNAL void m0_chan_init(struct m0_chan *chan, struct m0_mutex *ch_guard)
{
	M0_PRE(ch_guard != NULL);
	clink_tlist_init(&chan->ch_links);
	chan->ch_guard = ch_guard;
	chan->ch_waiters = 0;
}
M0_EXPORTED(m0_chan_init);

M0_INTERNAL void m0_chan_fini(struct m0_chan *chan)
{
	M0_PRE(chan_is_locked(chan));
	M0_ASSERT(chan->ch_waiters == 0);
	clink_tlist_fini(&chan->ch_links);
}
M0_EXPORTED(m0_chan_fini);

M0_INTERNAL void m0_chan_fini_lock(struct m0_chan *chan)
{
	/*
	 * This seemingly useless lock-unlock pair is to synchronize with
	 * m0_chan_{signal,broadcast}() that might be still using chan.
	 */
	chan_lock(chan);
	m0_chan_fini(chan);
	chan_unlock(chan);
}
M0_EXPORTED(m0_chan_fini_lock);

static void clink_signal(struct m0_clink *clink)
{
	struct m0_clink *grp = clink->cl_group;
	int              rc    = 0;

	if (clink->cl_is_oneshot)
		m0_clink_del(clink);

	if (clink->cl_cb != NULL) {
		m0_enter_awkward();
		rc = clink->cl_cb(clink);
		m0_exit_awkward();
	}
	if (rc == 0)
		m0_semaphore_up(&grp->cl_wait);
}

static void chan_signal_nr(struct m0_chan *chan, uint32_t nr)
{
	uint32_t i;

	M0_PRE(chan_is_locked(chan));
	M0_ASSERT(m0_chan_invariant(chan));
	for (i = 0; i < nr; ++i) {
		struct m0_clink *clink = clink_tlist_head(&chan->ch_links);

		if (clink != NULL) {
			clink_tlist_move_tail(&chan->ch_links, clink);
			clink_signal(clink);
		} else {
			break;
		}
	}
	M0_ASSERT(m0_chan_invariant(chan));
}

M0_INTERNAL void m0_chan_signal(struct m0_chan *chan)
{
	chan_signal_nr(chan, 1);
}
M0_EXPORTED(m0_chan_signal);

M0_INTERNAL void m0_chan_signal_lock(struct m0_chan *chan)
{
	chan_lock(chan);
	m0_chan_signal(chan);
	chan_unlock(chan);
}

M0_INTERNAL void m0_chan_broadcast(struct m0_chan *chan)
{
	chan_signal_nr(chan, chan->ch_waiters);
}
M0_EXPORTED(m0_chan_broadcast);

M0_INTERNAL void m0_chan_broadcast_lock(struct m0_chan *chan)
{
	chan_lock(chan);
	m0_chan_broadcast(chan);
	chan_unlock(chan);
}

M0_INTERNAL bool m0_chan_has_waiters(struct m0_chan *chan)
{
	return chan->ch_waiters > 0;
}

static void clink_init(struct m0_clink *link,
		       struct m0_clink *group, m0_chan_cb_t cb)
{
	link->cl_group      = group;
	link->cl_chan       = NULL;
	link->cl_cb         = cb;
	link->cl_is_oneshot = false;
	clink_tlink_init(link);
	M0_POST(clink_is_head(group));
}

M0_INTERNAL void m0_clink_init(struct m0_clink *link, m0_chan_cb_t cb)
{
	clink_init(link, link, cb);
	/* do NOT initialise the semaphore here */
}
M0_EXPORTED(m0_clink_init);

M0_INTERNAL void m0_clink_fini(struct m0_clink *link)
{
	/* do NOT finalise the semaphore here */
	clink_tlink_fini(link);
}
M0_EXPORTED(m0_clink_fini);

M0_INTERNAL void m0_clink_attach(struct m0_clink *link,
				 struct m0_clink *group, m0_chan_cb_t cb)
{
	M0_PRE(clink_is_head(group));

	clink_init(link, group, cb);
}
M0_EXPORTED(m0_clink_attach);

/**
   @pre  !m0_clink_is_armed(link)
   @post  m0_clink_is_armed(link)
 */
M0_INTERNAL void m0_clink_add(struct m0_chan *chan, struct m0_clink *link)
{
	int rc;

	M0_PRE(chan_is_locked(chan));
	M0_PRE(!m0_clink_is_armed(link));
	/* head is registered first */
	M0_PRE(ergo(!clink_is_head(link), m0_clink_is_armed(link->cl_group)));

	link->cl_chan = chan;
	if (clink_is_head(link)) {
		rc = m0_semaphore_init(&link->cl_wait, 0);
		M0_ASSERT(rc == 0);
	}

	M0_ASSERT(m0_chan_invariant(chan));
	chan->ch_waiters++;
	clink_tlist_add_tail(&chan->ch_links, link);
	M0_ASSERT(m0_chan_invariant(chan));

	M0_POST(m0_clink_is_armed(link));
}
M0_EXPORTED(m0_clink_add);

void m0_clink_add_lock(struct m0_chan *chan, struct m0_clink *link)
{
	chan_lock(chan);
	m0_clink_add(chan, link);
	chan_unlock(chan);
}
M0_EXPORTED(m0_clink_add_lock);

/**
   @pre   m0_clink_is_armed(link)
   @post !m0_clink_is_armed(link)
 */
M0_INTERNAL void m0_clink_del(struct m0_clink *link)
{
	struct m0_chan *chan = link->cl_chan;

	M0_PRE(m0_clink_is_armed(link));
	M0_PRE(chan_is_locked(chan));
	/* head is de-registered last */
	M0_PRE(ergo(!clink_is_head(link), m0_clink_is_armed(link->cl_group)));

	M0_ASSERT(m0_chan_invariant(chan));
	M0_ASSERT(chan->ch_waiters > 0);
	chan->ch_waiters--;
	clink_tlist_del(link);
	M0_ASSERT(m0_chan_invariant(chan));

	link->cl_chan = NULL;
	if (clink_is_head(link))
		m0_semaphore_fini(&link->cl_wait);

	M0_POST(!m0_clink_is_armed(link));
}
M0_EXPORTED(m0_clink_del);

M0_INTERNAL void m0_clink_del_lock(struct m0_clink *link)
{
	struct m0_chan *chan = link->cl_chan;

	chan_lock(chan);
	m0_clink_del(link);
	chan_unlock(chan);
}
M0_EXPORTED(m0_clink_del_lock);

M0_INTERNAL bool m0_clink_is_armed(const struct m0_clink *link)
{
	return link->cl_chan != NULL;
}

M0_INTERNAL void m0_clink_signal(struct m0_clink *clink)
{
	clink_signal(clink);
}

M0_INTERNAL bool m0_chan_trywait(struct m0_clink *link)
{
	return m0_semaphore_trydown(&link->cl_group->cl_wait);
}

M0_INTERNAL void m0_chan_wait(struct m0_clink *link)
{
	m0_semaphore_down(&link->cl_group->cl_wait);
}
M0_EXPORTED(m0_chan_wait);

M0_INTERNAL bool m0_chan_timedwait(struct m0_clink *link,
				   const m0_time_t abs_timeout)
{
	return m0_semaphore_timeddown(&link->cl_group->cl_wait, abs_timeout);
}
M0_EXPORTED(m0_chan_timedwait);

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
