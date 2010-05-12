/* -*- C -*- */

#include <errno.h>

#include "lib/chan.h"
#include "lib/c2list.h"
#include "lib/assert.h"

/**
   @addtogroup chan
   @{
 */


void c2_chan_init(struct c2_chan *chan)
{
	c2_list_init(&chan->ch_links);
	c2_mutex_init(&chan->ch_guard);
}

void c2_chan_fini(struct c2_chan *chan)
{
	c2_mutex_lock(&chan->ch_guard);
	c2_mutex_unlock(&chan->ch_guard);

	c2_mutex_fini(&chan->ch_guard);
	c2_list_fini(&chan->ch_links);
}

static void clink_lock(struct c2_clink *clink)
{
	c2_mutex_lock(&clink->cl_chan->ch_guard);
}

static void clink_unlock(struct c2_clink *clink)
{
	c2_mutex_unlock(&clink->cl_chan->ch_guard);
}

static struct c2_clink *chan_top(struct c2_chan *chan)
{
	struct c2_clink *clink;
	C2_ASSERT(c2_mutex_is_not_locked(&chan->ch_guard));

	c2_mutex_lock(&chan->ch_guard);
	if (!c2_list_is_empty(&chan->ch_links)) {
		clink = container_of(chan->ch_links.first, struct c2_clink,
				     cl_linkage);
		c2_list_del_init(&clink->cl_linkage);
	} else
		clink = NULL;
	c2_mutex_unlock(&chan->ch_guard);
	return clink;
}

static void clink_signal(struct c2_clink *clink)
{
	C2_ASSERT(clink->cl_chan != NULL);
	C2_ASSERT(c2_mutex_is_not_locked(&clink->cl_chan->ch_guard));

	if (clink->cl_cb != NULL)
		clink->cl_cb(clink);
	else {
		int rc;

		rc = sem_post(&clink->cl_wait);
		C2_ASSERT(rc == 0);
	}
}

void c2_chan_signal(struct c2_chan *chan)
{
	struct c2_clink *clink;

	clink = chan_top(chan);
	if (clink != NULL)
		clink_signal(clink);
}

void c2_chan_broadcast(struct c2_chan *chan)
{
	struct c2_clink *clink;

	while((clink = chan_top(chan)) != NULL)
		clink_signal(clink);
}

bool c2_chan_has_waiters(struct c2_chan *chan)
{
	bool isempty;

	c2_mutex_lock(&chan->ch_guard);
	isempty = c2_list_is_empty(&chan->ch_links);
	c2_mutex_unlock(&chan->ch_guard);

	return !isempty;
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
	c2_list_add_tail(&chan->ch_links, &link->cl_linkage);
	rc = sem_init(&link->cl_wait, 0, 0);
	C2_ASSERT(rc == 0);
	clink_unlock(link);

	C2_POST(c2_clink_is_armed(link));
}

/**
   @pre   c2_clink_is_armed(link)
   @post !c2_clink_is_armed(link)
 */
void c2_clink_del(struct c2_clink *link)
{
	int rc;

	C2_PRE(c2_clink_is_armed(link));

	clink_lock(link);
	c2_list_del_init(&link->cl_linkage);
	rc = sem_destroy(&link->cl_wait);
	C2_ASSERT(rc == 0);
	link->cl_chan = NULL;
	clink_unlock(link);

	C2_POST(!c2_clink_is_armed(link));
}

bool c2_clink_is_armed(const struct c2_clink *link)
{
	return link->cl_chan != NULL;
}

bool c2_chan_trywait(struct c2_clink *link)
{
	int rc;

	C2_ASSERT(link->cl_cb == NULL);
	rc = sem_trywait(&link->cl_wait);
	C2_ASSERT(rc == 0 || rc == EAGAIN);
	return rc == 0;
}

void c2_chan_wait(struct c2_clink *link)
{
	int rc;

	C2_ASSERT(link->cl_cb == NULL);
	rc = sem_wait(&link->cl_wait);
	C2_ASSERT(rc == 0);
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
