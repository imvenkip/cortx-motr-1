/* -*- C -*- */

#include "lib/errno.h"
#include "lib/chan.h"
#include "lib/list.h"
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

/**
   Channel invariant: all clinks on the list are clinks for this channel and
   number of waiters matches list length.
 */
static bool c2_chan_invariant_locked(struct c2_chan *chan)
{
	struct c2_clink *scan;

	if (chan->ch_waiters != c2_list_length(&chan->ch_links))
		return false;

	c2_list_for_each_entry(&chan->ch_links, scan,
			       struct c2_clink, cl_linkage) {
		if (scan->cl_chan != chan)
			return false;
	}
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
	c2_list_init(&chan->ch_links);
	c2_mutex_init(&chan->ch_guard);
	chan->ch_waiters = 0;
	C2_ASSERT(c2_chan_invariant(chan));
}

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
	c2_list_fini(&chan->ch_links);
}

static struct c2_clink *chan_head(struct c2_chan *chan)
{
	struct c2_clink *clink;

	if (!c2_list_is_empty(&chan->ch_links)) {
		clink = container_of(chan->ch_links.l_head, struct c2_clink,
				     cl_linkage);
		c2_list_move_tail(&chan->ch_links, &clink->cl_linkage);
	} else
		clink = NULL;
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

void c2_chan_broadcast(struct c2_chan *chan)
{
	chan_signal_nr(chan, chan->ch_waiters);
}

bool c2_chan_has_waiters(struct c2_chan *chan)
{
	C2_ASSERT(c2_chan_invariant(chan));
	return chan->ch_waiters > 0;
}

void c2_clink_init(struct c2_clink *link, c2_chan_cb_t cb)
{
	link->cl_chan = NULL;
	link->cl_cb = cb;
	c2_list_link_init(&link->cl_linkage);
	/* do NOT initialise the semaphore here */
}

void c2_clink_fini(struct c2_clink *link)
{
	/* do NOT finalise the semaphore here */
	c2_list_link_fini(&link->cl_linkage);
}

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
	c2_list_add_tail(&chan->ch_links, &link->cl_linkage);
	rc = c2_semaphore_init(&link->cl_wait, 0);
	C2_ASSERT(rc == 0);
	C2_ASSERT(c2_chan_invariant_locked(chan));
	clink_unlock(link);

	C2_POST(c2_clink_is_armed(link));
}

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
	c2_list_del(&link->cl_linkage);
	C2_ASSERT(c2_chan_invariant_locked(chan));
	clink_unlock(link);

	link->cl_chan = NULL;
	c2_semaphore_fini(&link->cl_wait);

	C2_POST(!c2_clink_is_armed(link));
}

bool c2_clink_is_armed(const struct c2_clink *link)
{
	return link->cl_chan != NULL;
}

bool c2_chan_trywait(struct c2_clink *link)
{
	bool result;

	C2_ASSERT(link->cl_cb == NULL);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	result = c2_semaphore_trydown(&link->cl_wait);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	return result;
}

void c2_chan_wait(struct c2_clink *link)
{
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	C2_ASSERT(link->cl_cb == NULL);
	c2_semaphore_down(&link->cl_wait);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
}

bool c2_chan_timedwait(struct c2_clink *link, const c2_time_t abs_timeout)
{
	bool result;

	C2_ASSERT(link->cl_cb == NULL);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));

	result = c2_semaphore_timeddown(&link->cl_wait, abs_timeout);
	C2_ASSERT(c2_chan_invariant(link->cl_chan));
	return result;
}


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
