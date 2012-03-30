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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/06/2010
 */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/mutex.h"
#include "lib/ut.h"

#include "net/net_internal.h"

static struct c2_net_domain utdom;
static struct c2_net_transfer_mc ut_tm;

static void make_desc(struct c2_net_buf_desc *desc);

#if 0
#define KPRN(fmt,...) printk(KERN_ERR fmt, ## __VA_ARGS__)
#define PRId64 "lld" /* from <inttypes.h> */
#else
#define KPRN(fmt,...)
#endif

#define DELAY_MS(ms)				\
{       c2_time_t rem;				\
	c2_time_t del;				\
        c2_time_set(&del, 0, ms * 1000000ULL);	\
        c2_nanosleep(del, &rem);		\
}

/*
 *****************************************************
   Fake transport for the UT
 *****************************************************
*/
static struct {
	int num;
} ut_xprt_pvt;

static bool ut_dom_init_called=false;
static int ut_dom_init(struct c2_net_xprt *xprt,
		       struct c2_net_domain *dom)
{
	C2_ASSERT(c2_mutex_is_locked(&c2_net_mutex));
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	ut_dom_init_called = true;
	dom->nd_xprt_private = &ut_xprt_pvt;
	return 0;
}

static bool ut_dom_fini_called = false;
static void ut_dom_fini(struct c2_net_domain *dom)
{
	C2_ASSERT(c2_mutex_is_locked(&c2_net_mutex));
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	ut_dom_fini_called = true;
}

/* params */
enum {
	UT_MAX_BUF_SIZE = 8192,
	UT_MAX_BUF_SEGMENT_SIZE = 2048,
	UT_MAX_BUF_SEGMENTS = 4,
};
static bool ut_get_max_buffer_size_called = false;
static c2_bcount_t ut_get_max_buffer_size(const struct c2_net_domain *dom)
{
	ut_get_max_buffer_size_called = true;
	return UT_MAX_BUF_SIZE;
}
static bool ut_get_max_buffer_segment_size_called = false;
static c2_bcount_t ut_get_max_buffer_segment_size(const struct c2_net_domain
						  *dom)
{
	ut_get_max_buffer_segment_size_called = true;
	return UT_MAX_BUF_SEGMENT_SIZE;
}
static bool ut_get_max_buffer_segments_called = false;
static int32_t ut_get_max_buffer_segments(const struct c2_net_domain *dom)
{
	ut_get_max_buffer_segments_called = true;
	return UT_MAX_BUF_SEGMENTS;
}

struct ut_ep {
	char *addr;
	struct c2_net_end_point uep;
};
static bool ut_end_point_release_called = false;
static struct c2_net_end_point *ut_last_ep_released;
static void ut_end_point_release(struct c2_ref *ref)
{
	struct c2_net_end_point *ep;
	struct ut_ep *utep;
	struct c2_net_transfer_mc *tm;
	ut_end_point_release_called = true;
	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	ut_last_ep_released = ep;
	tm = ep->nep_tm;
	C2_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	c2_list_del(&ep->nep_tm_linkage);
	ep->nep_tm = NULL;
	utep = container_of(ep, struct ut_ep, uep);
	c2_free(utep);
}
static bool ut_end_point_create_called = false;
static int ut_end_point_create(struct c2_net_end_point **epp,
			       struct c2_net_transfer_mc *tm,
			       const char *addr)
{
	char *ap;
	struct ut_ep *utep;
	struct c2_net_end_point *ep;

	C2_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_end_point_create_called = true;
	if (addr == NULL) {
		/* don't support dynamic */
		return -ENOSYS;
	}
	ap = (char *)addr;  /* avoid strdup; this is a ut! */
	/* check if its already on the domain list */
	c2_list_for_each_entry(&tm->ntm_end_points, ep,
			       struct c2_net_end_point,
			       nep_tm_linkage) {
		utep = container_of(ep, struct ut_ep, uep);
		if (strcmp(utep->addr, ap) == 0) {
			c2_ref_get(&ep->nep_ref); /* refcnt++ */
			*epp = ep;
			return 0;
		}
	}
	/* allocate a new end point */
	C2_ALLOC_PTR(utep);
	utep->addr = ap;
	utep->uep.nep_addr = ap;
	c2_ref_init(&utep->uep.nep_ref, 1, ut_end_point_release);
	utep->uep.nep_tm = tm;
	c2_list_add_tail(&tm->ntm_end_points, &utep->uep.nep_tm_linkage);
	*epp = &utep->uep;
	return 0;
}

static bool ut_buf_register_called = false;
static int ut_buf_register(struct c2_net_buffer *nb)
{
	C2_ASSERT(c2_mutex_is_locked(&nb->nb_dom->nd_mutex));
	ut_buf_register_called = true;
	return 0;
}

static bool ut_buf_deregister_called = false;
static void ut_buf_deregister(struct c2_net_buffer *nb)
{
	C2_ASSERT(c2_mutex_is_locked(&nb->nb_dom->nd_mutex));
	ut_buf_deregister_called = true;
	return;
}

static bool ut_buf_add_called = false;
static int ut_buf_add(struct c2_net_buffer *nb)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&nb->nb_tm->ntm_mutex));
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_IN_USE));
	switch (nb->nb_qtype) {
	case C2_NET_QT_PASSIVE_BULK_RECV:
	case C2_NET_QT_PASSIVE_BULK_SEND:
		/* passive bulk ops required to set nb_desc */
		make_desc(&nb->nb_desc);
		break;
	default:
		break;
	}
	ut_buf_add_called = true;
	return 0;
}

struct c2_thread ut_del_thread;
static void ut_post_del_thread(struct c2_net_buffer *nb)
{
	struct c2_net_buffer_event ev = {
		.nbe_buffer = nb,
		.nbe_status = 0,
	};
	if (nb->nb_flags & C2_NET_BUF_CANCELLED)
		ev.nbe_status = -ECANCELED; /* required behavior */
	DELAY_MS(1);
	ev.nbe_time = c2_time_now();

	/* post requested event */
	c2_net_buffer_event_post(&ev);
}

static bool ut_buf_del_called = false;
static void ut_buf_del(struct c2_net_buffer *nb)
{
	int rc;
	C2_SET0(&ut_del_thread);
	C2_UT_ASSERT(c2_mutex_is_locked(&nb->nb_tm->ntm_mutex));
	ut_buf_del_called = true;
	if (!(nb->nb_flags & C2_NET_BUF_IN_USE))
		nb->nb_flags |= C2_NET_BUF_CANCELLED;
	rc = C2_THREAD_INIT(&ut_del_thread, struct c2_net_buffer *, NULL,
			    &ut_post_del_thread, nb, "ut_post_del");
	C2_UT_ASSERT(rc == 0);
	return;
}

struct ut_tm_pvt {
	struct c2_net_transfer_mc *tm;
};
static bool ut_tm_init_called = false;
static int ut_tm_init(struct c2_net_transfer_mc *tm)
{
	struct ut_tm_pvt *tmp;
	C2_ASSERT(c2_mutex_is_locked(&tm->ntm_dom->nd_mutex));
	C2_ALLOC_PTR(tmp);
	tmp->tm = tm;
	tm->ntm_xprt_private = tmp;
	ut_tm_init_called = true;
	return 0;
}

static bool ut_tm_fini_called = false;
static void ut_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct ut_tm_pvt *tmp;
	C2_ASSERT(c2_mutex_is_locked(&tm->ntm_dom->nd_mutex));
	tmp = tm->ntm_xprt_private;
	C2_UT_ASSERT(tmp->tm == tm);
	c2_free(tmp);
	ut_tm_fini_called = true;
	return;
}

struct c2_thread ut_tm_thread;
static void ut_post_tm_started_ev_thread(struct c2_net_end_point *ep)
{
	struct c2_net_tm_event ev = {
		.nte_type = C2_NET_TEV_STATE_CHANGE,
		.nte_tm = &ut_tm,
		.nte_ep = ep,
		.nte_status = 0,
		.nte_next_state = C2_NET_TM_STARTED
	};
	DELAY_MS(1);
	ev.nte_time = c2_time_now();

	/* post state change event */
	c2_net_tm_event_post(&ev);
}
static void ut_post_state_change_ev_thread(int n)
{
	struct c2_net_tm_event ev = {
		.nte_type = C2_NET_TEV_STATE_CHANGE,
		.nte_tm = &ut_tm,
		.nte_status = 0,
		.nte_next_state = (enum c2_net_tm_state) n
	};
	DELAY_MS(1);
	ev.nte_time = c2_time_now();

	/* post state change event */
	c2_net_tm_event_post(&ev);
}

static bool ut_tm_start_called = false;
static int ut_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
	int rc;
	struct c2_net_xprt *xprt;
	struct c2_net_end_point *ep;

	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_tm_start_called = true;

	/* create the end point (indirectly via the transport ops vector) */
	xprt = tm->ntm_dom->nd_xprt;
	rc = (*xprt->nx_ops->xo_end_point_create)(&ep, tm, addr);
	if (rc != 0)
		return rc;

	/* create bg thread to post start state change event.
	   cannot do it here: we are in tm lock, post would assert.
	 */
	rc = C2_THREAD_INIT(&ut_tm_thread, struct c2_net_end_point *, NULL,
			    &ut_post_tm_started_ev_thread, ep,
			    "state_change%d", C2_NET_TM_STARTED);
	C2_UT_ASSERT(rc == 0);
	return rc;
}

static bool ut_tm_stop_called = false;
static int ut_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	int rc;

	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_tm_stop_called = true;
	rc = C2_THREAD_INIT(&ut_tm_thread, int, NULL,
			    &ut_post_state_change_ev_thread, C2_NET_TM_STOPPED,
			    "state_change%d", C2_NET_TM_STOPPED);
	C2_UT_ASSERT(rc == 0);
	return rc;
}

static bool ut_tm_confine_called = false;
static const struct c2_bitmap *ut_tm_confine_bm;
static int ut_tm_confine(struct c2_net_transfer_mc *tm,
			 const struct c2_bitmap *processors)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_tm_confine_called = true;
	ut_tm_confine_bm = processors;
	return 0;
}

static bool ut_bev_deliver_sync_called = false;
static int ut_bev_deliver_sync(struct c2_net_transfer_mc *tm)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_bev_deliver_sync_called = true;
	return 0;
}

static bool ut_bev_deliver_all_called = false;
static void ut_bev_deliver_all(struct c2_net_transfer_mc *tm)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_bev_deliver_all_called = true;
	return;
}

static bool ut_bev_pending_called = false;
static int ut_bev_pending_last = 1;
static bool ut_bev_pending(struct c2_net_transfer_mc *tm)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_bev_pending_called = true;
	ut_bev_pending_last = 1 - ut_bev_pending_last;
	return (bool)ut_bev_pending_last;
}

static bool ut_bev_notify_called = false;
static void ut_bev_notify(struct c2_net_transfer_mc *tm, struct c2_chan *chan)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_UT_ASSERT(chan == &tm->ntm_chan);
	ut_bev_notify_called = true;
	return;
}

static struct c2_net_xprt_ops ut_xprt_ops = {
	.xo_dom_init                    = ut_dom_init,
	.xo_dom_fini                    = ut_dom_fini,
	.xo_get_max_buffer_size         = ut_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = ut_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = ut_get_max_buffer_segments,
	.xo_end_point_create            = ut_end_point_create,
	.xo_buf_register                = ut_buf_register,
	.xo_buf_deregister              = ut_buf_deregister,
	.xo_buf_add                     = ut_buf_add,
	.xo_buf_del                     = ut_buf_del,
	.xo_tm_init                     = ut_tm_init,
	.xo_tm_fini                     = ut_tm_fini,
	.xo_tm_start                    = ut_tm_start,
	.xo_tm_stop                     = ut_tm_stop,
	// define at runtime .xo_tm_confine
	// define at runtime .xo_bev_deliver_sync
        .xo_bev_deliver_all             = ut_bev_deliver_all,
	.xo_bev_pending                 = ut_bev_pending,
	.xo_bev_notify                  = ut_bev_notify
};

static struct c2_net_xprt ut_xprt = {
	.nx_name = "ut/bulk",
	.nx_ops  = &ut_xprt_ops
};

/* utility subs */
static struct c2_net_buffer *
allocate_buffers(c2_bcount_t buf_size,
		 c2_bcount_t buf_seg_size,
		 int32_t buf_segs)
{
	int rc;
	int i;
	struct c2_net_buffer *nbs;
	struct c2_net_buffer *nb;
	c2_bcount_t sz;
	int32_t nr;

	C2_ALLOC_ARR(nbs, C2_NET_QT_NR);
	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		C2_SET0(nb);
		nr = buf_segs;
		if ((buf_size / buf_segs) > buf_seg_size) {
			sz = buf_seg_size;
			C2_ASSERT((sz * nr) <= buf_size);
		} else {
			sz = buf_size/buf_segs;
		}
		rc = c2_bufvec_alloc(&nb->nb_buffer, nr, sz);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == nr);
		C2_UT_ASSERT(c2_vec_count(&nb->nb_buffer.ov_vec) == (sz * nr));
	}

	return nbs;
}

static void make_desc(struct c2_net_buf_desc *desc)
{
	static const char *p = "descriptor";
	size_t len = strlen(p)+1;
	desc->nbd_data = c2_alloc(len);
	desc->nbd_len = len;
	strcpy((char *)desc->nbd_data, p);
}

/* callback subs */
static int ut_cb_calls[C2_NET_QT_NR];
static uint64_t num_adds[C2_NET_QT_NR];
static uint64_t num_dels[C2_NET_QT_NR];
static c2_bcount_t total_bytes[C2_NET_QT_NR];
static c2_bcount_t max_bytes[C2_NET_QT_NR];

static void ut_buffer_event_callback(const struct c2_net_buffer_event *ev,
				     enum c2_net_queue_type qt,
				     bool queue_check)
{
	c2_bcount_t len = 0;
	C2_UT_ASSERT(ev->nbe_buffer != NULL);
	C2_UT_ASSERT(ev->nbe_buffer->nb_qtype == qt);
	ut_cb_calls[qt]++;
	if (queue_check)
		C2_UT_ASSERT(!(ev->nbe_buffer->nb_flags & C2_NET_BUF_QUEUED));
	/* Collect stats to test the q stats.
	   Length counted only on success.
	   Receive buffer lengths are in the event.
	*/
	if (qt == C2_NET_QT_MSG_RECV ||
	    qt == C2_NET_QT_PASSIVE_BULK_RECV ||
	    qt == C2_NET_QT_ACTIVE_BULK_RECV) {
		/* assert that the buffer length not set by the API */
		C2_UT_ASSERT(ev->nbe_buffer->nb_length == 0);
		if (ev->nbe_status == 0) {
			len = ev->nbe_length;
			C2_UT_ASSERT(len != 0);
			ev->nbe_buffer->nb_length = ev->nbe_length;
		}
	} else {
		if (ev->nbe_status == 0) {
			len = ev->nbe_buffer->nb_length;
		}
	}
	total_bytes[qt] += len;
	max_bytes[qt] = max64u(ev->nbe_buffer->nb_length,max_bytes[qt]);
}

#define UT_CB_CALL(_qt) ut_buffer_event_callback(ev, _qt, true)
static void ut_msg_recv_cb(const struct c2_net_buffer_event *ev)
{
	UT_CB_CALL(C2_NET_QT_MSG_RECV);
}

static void ut_msg_send_cb(const struct c2_net_buffer_event *ev)
{
	UT_CB_CALL(C2_NET_QT_MSG_SEND);
}

static void ut_passive_bulk_recv_cb(const struct c2_net_buffer_event *ev)
{
	UT_CB_CALL(C2_NET_QT_PASSIVE_BULK_RECV);
}

static void ut_passive_bulk_send_cb(const struct c2_net_buffer_event *ev)
{
	UT_CB_CALL(C2_NET_QT_PASSIVE_BULK_SEND);
}

static void ut_active_bulk_recv_cb(const struct c2_net_buffer_event *ev)
{
	UT_CB_CALL(C2_NET_QT_ACTIVE_BULK_RECV);
}

static void ut_active_bulk_send_cb(const struct c2_net_buffer_event *ev)
{
	UT_CB_CALL(C2_NET_QT_ACTIVE_BULK_SEND);
}

static bool ut_multi_use_expect_queued;
static struct c2_net_buffer *ut_multi_use_got_buf;
static void ut_multi_use_cb(const struct c2_net_buffer_event *ev)
{
	C2_UT_ASSERT(ev->nbe_buffer != NULL);
	if (ut_multi_use_expect_queued) {
		C2_UT_ASSERT(ev->nbe_buffer->nb_flags & C2_NET_BUF_QUEUED);
	} else {
		C2_UT_ASSERT(!(ev->nbe_buffer->nb_flags & C2_NET_BUF_QUEUED));
	}
	ut_multi_use_got_buf = ev->nbe_buffer;
	ut_buffer_event_callback(ev, ev->nbe_buffer->nb_qtype, false);
}

static int ut_tm_event_cb_calls = 0;
void ut_tm_event_cb(const struct c2_net_tm_event *ev)
{
	ut_tm_event_cb_calls++;
}

/* UT transfer machine */
struct c2_net_tm_callbacks ut_tm_cb = {
	.ntc_event_cb = ut_tm_event_cb
};

struct c2_net_buffer_callbacks ut_buf_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = ut_msg_recv_cb,
		[C2_NET_QT_MSG_SEND]          = ut_msg_send_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = ut_passive_bulk_recv_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = ut_passive_bulk_send_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = ut_active_bulk_recv_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = ut_active_bulk_send_cb
	},
};

struct c2_net_buffer_callbacks ut_buf_multi_use_cb = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]          = ut_multi_use_cb,
		[C2_NET_QT_MSG_SEND]          = ut_multi_use_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = ut_multi_use_cb,
		[C2_NET_QT_PASSIVE_BULK_SEND] = ut_multi_use_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = ut_multi_use_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = ut_multi_use_cb,
	},
};

static struct c2_net_transfer_mc ut_tm = {
	.ntm_callbacks = &ut_tm_cb,
	.ntm_state = C2_NET_TM_UNDEFINED
};

/*
  Unit test starts
 */
static void test_net_bulk_if(void)
{
	int rc, i, reuse_cnt;
	bool brc;
	c2_bcount_t buf_size, buf_seg_size;
	int32_t   buf_segs;
	struct c2_net_domain *dom = &utdom;
	struct c2_net_transfer_mc *tm = &ut_tm;
	struct c2_net_buffer *nbs;
	struct c2_net_buffer *nb;
	struct c2_net_end_point *ep1, *ep2, *ep;
	struct c2_net_buf_desc d1, d2;
	struct c2_clink tmwait;
	struct c2_net_qstats qs[C2_NET_QT_NR];
	c2_time_t c2tt_to_period;
	struct c2_bitmap *procmask = (void *) -1; /* fake not null UT value */
	enum { NUM_REUSES = 2 };

	C2_SET0(&d1);
	C2_SET0(&d2);
	make_desc(&d1);
	C2_UT_ASSERT(d1.nbd_data != NULL);
	C2_UT_ASSERT(d1.nbd_len > 0);
	rc = c2_net_desc_copy(&d1, &d2);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(d2.nbd_data != NULL);
	C2_UT_ASSERT(d2.nbd_len > 0);
	C2_UT_ASSERT(d1.nbd_data != d2.nbd_data);
	C2_UT_ASSERT(d1.nbd_len == d2.nbd_len);
	C2_UT_ASSERT(memcmp(d1.nbd_data, d2.nbd_data, d1.nbd_len) == 0);
	c2_net_desc_free(&d2);
	C2_UT_ASSERT(d2.nbd_data == NULL);
	C2_UT_ASSERT(d2.nbd_len == 0);
	c2_net_desc_free(&d1);

	/* initialize the domain */
	C2_UT_ASSERT(ut_dom_init_called == false);
	rc = c2_net_domain_init(dom, &ut_xprt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_dom_init_called);
	C2_UT_ASSERT(dom->nd_xprt == &ut_xprt);
	C2_UT_ASSERT(dom->nd_xprt_private == &ut_xprt_pvt);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));

	/* get max buffer size */
	C2_UT_ASSERT(ut_get_max_buffer_size_called == false);
	buf_size = 0;
	buf_size = c2_net_domain_get_max_buffer_size(dom);
	C2_UT_ASSERT(ut_get_max_buffer_size_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_size == UT_MAX_BUF_SIZE);

	/* get max buffer segment size */
	C2_UT_ASSERT(ut_get_max_buffer_segment_size_called == false);
	buf_seg_size = 0;
	buf_seg_size = c2_net_domain_get_max_buffer_segment_size(dom);
	C2_UT_ASSERT(ut_get_max_buffer_segment_size_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_seg_size == UT_MAX_BUF_SEGMENT_SIZE);

	/* get max buffer segments */
	C2_UT_ASSERT(ut_get_max_buffer_segments_called == false);
	buf_segs = 0;
	buf_segs = c2_net_domain_get_max_buffer_segments(dom);
	C2_UT_ASSERT(ut_get_max_buffer_segments_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_segs == UT_MAX_BUF_SEGMENTS);

	/* allocate buffers for testing */
	nbs = allocate_buffers(buf_size, buf_seg_size, buf_segs);

	/* register the buffers */
	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		nb->nb_flags = 0;
		nb->nb_timeout = 0;
		ut_buf_register_called = false;
		rc = c2_net_buffer_register(nb, dom);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(ut_buf_register_called);
		C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_REGISTERED);
		C2_UT_ASSERT(nb->nb_timeout == C2_TIME_NEVER);
		num_adds[i] = 0;
		num_dels[i] = 0;
		total_bytes[i] = 0;
	}
	c2_time_set(&c2tt_to_period, 120, 0); /* 2 min */

	/* TM init with callbacks */
	rc = c2_net_tm_init(tm, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm->ntm_dom_linkage));

	/* should be able to fini it immediately */
	ut_tm_fini_called = false;
	c2_net_tm_fini(tm);
	C2_UT_ASSERT(ut_tm_fini_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_UNDEFINED);

	/* should be able to init it again */
	ut_tm_init_called = false;
	ut_tm_fini_called = false;
	rc = c2_net_tm_init(tm, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm->ntm_dom_linkage));

	/* check the confine API */
	C2_UT_ASSERT(dom->nd_xprt->nx_ops->xo_tm_confine == NULL);
	rc = c2_net_tm_confine(tm, procmask);
	C2_UT_ASSERT(rc == -ENOSYS); /* optional support */
	C2_UT_ASSERT(!ut_tm_confine_called);
	ut_xprt_ops.xo_tm_confine = ut_tm_confine; /* provide the operation */
	C2_UT_ASSERT(dom->nd_xprt->nx_ops->xo_tm_confine != NULL);
	rc = c2_net_tm_confine(tm, procmask);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_confine_called);
	C2_UT_ASSERT(ut_tm_confine_bm == procmask);

	/* TM start */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	C2_UT_ASSERT(ut_end_point_create_called == false);
	rc = c2_net_tm_start(tm, "addr2");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTING ||
		     tm->ntm_state == C2_NET_TM_STARTED);

	/* wait on channel for started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(ut_tm_event_cb_calls == 1);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ut_end_point_create_called);

	C2_UT_ASSERT(tm->ntm_ep != NULL);
	C2_UT_ASSERT(c2_atomic64_get(&tm->ntm_ep->nep_ref.ref_cnt) == 1);

	/* Test desired end point behavior
	   A real transport isn't actually forced to maintain
	   reference counts this way, but ought to do so.
	 */
	ut_end_point_create_called = false;
	rc = c2_net_end_point_create(&ep1, tm, NULL);
	C2_UT_ASSERT(rc != 0); /* no dynamic */
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_ASSERT(c2_mutex_is_not_locked(&tm->ntm_mutex));
	C2_ASSERT(c2_list_length(&tm->ntm_end_points) == 1);

	ut_end_point_create_called = false;
	rc = c2_net_end_point_create(&ep1, tm, "addr1");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_ASSERT(c2_mutex_is_not_locked(&tm->ntm_mutex));
	C2_ASSERT(!c2_list_is_empty(&tm->ntm_end_points));
	C2_UT_ASSERT(c2_atomic64_get(&ep1->nep_ref.ref_cnt) == 1);

	rc = c2_net_end_point_create(&ep2, tm, "addr2");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ep2 != ep1);
	C2_UT_ASSERT(ep2 == tm->ntm_ep);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);

	rc = c2_net_end_point_create(&ep, tm, "addr1");
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ep == ep1);
	C2_UT_ASSERT(c2_atomic64_get(&ep->nep_ref.ref_cnt) == 2);

	C2_UT_ASSERT(ut_end_point_release_called == false);
	c2_net_end_point_get(ep); /* refcnt=3 */
	C2_UT_ASSERT(c2_atomic64_get(&ep->nep_ref.ref_cnt) == 3);

	C2_UT_ASSERT(ut_end_point_release_called == false);
	rc = c2_net_end_point_put(ep); /* refcnt=2 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_release_called == false);
	C2_UT_ASSERT(c2_atomic64_get(&ep->nep_ref.ref_cnt) == 2);

	rc = c2_net_end_point_put(ep); /* refcnt=1 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_release_called == false);
	C2_UT_ASSERT(c2_atomic64_get(&ep->nep_ref.ref_cnt) == 1);

	rc = c2_net_end_point_put(ep); /* refcnt=0 */
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_release_called);
	C2_UT_ASSERT(ut_last_ep_released == ep);
	ep1 = NULL; /* not valid! */

	/* add MSG_RECV buf with a timeout in the past - should fail */
	nb = &nbs[C2_NET_QT_MSG_RECV];
	nb->nb_callbacks = &ut_buf_cb;
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_timeout = c2_time_sub(c2_time_now(), c2tt_to_period);
	nb->nb_min_receive_size = buf_size;
	nb->nb_max_receive_msgs = 1;
	rc = c2_net_buffer_add(nb, tm);
	C2_UT_ASSERT(rc == -ETIME);

	/* add MSG_RECV buf - should succeeded as now started */
	nb = &nbs[C2_NET_QT_MSG_RECV];
	nb->nb_callbacks = &ut_buf_cb;
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_timeout = c2_time_add(c2_time_now(), c2tt_to_period);
	nb->nb_min_receive_size = buf_size;
	nb->nb_max_receive_msgs = 1;
	rc = c2_net_buffer_add(nb, tm);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_buf_add_called);
	C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_QUEUED);
	C2_UT_ASSERT(nb->nb_tm == tm);
	num_adds[nb->nb_qtype]++;
	max_bytes[nb->nb_qtype] = max64u(nb->nb_length,
					 max_bytes[nb->nb_qtype]);

	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/* initialize and add remaining types of buffers
	   use buffer private callbacks for the bulk
	 */
	for (i = C2_NET_QT_MSG_SEND; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
		nb->nb_qtype = i;
		nb->nb_callbacks = &ut_buf_cb;
		/* NB: real code sets nb_ep to server ep */
		switch (i) {
		case C2_NET_QT_MSG_SEND:
			nb->nb_ep = ep2;
			nb->nb_length = buf_size;
			break;
		case C2_NET_QT_PASSIVE_BULK_RECV:
			C2_UT_ASSERT(nb->nb_length == 0);
			nb->nb_ep = ep2;
			break;
		case C2_NET_QT_PASSIVE_BULK_SEND:
			nb->nb_ep = ep2;
			nb->nb_length = buf_size;
			break;
		case C2_NET_QT_ACTIVE_BULK_RECV:
			C2_UT_ASSERT(nb->nb_length == 0);
			make_desc(&nb->nb_desc);
			break;
		case C2_NET_QT_ACTIVE_BULK_SEND:
			nb->nb_length = buf_size;
			make_desc(&nb->nb_desc);
			break;
		}
		nb->nb_timeout = c2_time_add(c2_time_now(),
					     c2tt_to_period);
		rc = c2_net_buffer_add(nb, tm);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_QUEUED);
		C2_UT_ASSERT(nb->nb_tm == tm);
		num_adds[nb->nb_qtype]++;
		max_bytes[nb->nb_qtype] = max64u(buf_size,
						 max_bytes[nb->nb_qtype]);
	}
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 3);

	/* fake each type of buffer "post" response.
	   xprt normally does this
	 */
	for (i = C2_NET_QT_MSG_RECV; i < C2_NET_QT_NR; ++i) {
		struct c2_net_buffer_event ev = {
			.nbe_buffer = &nbs[i],
			.nbe_status = 0,
		};
		DELAY_MS(1);
		ev.nbe_time = c2_time_now();
		nb = &nbs[i];

		if (i == C2_NET_QT_MSG_RECV) {
			/* simulate transport ep in recv msg */
			ev.nbe_ep = ep2;
			c2_net_end_point_get(ep2);
		}

		if (i == C2_NET_QT_MSG_RECV ||
		    i == C2_NET_QT_PASSIVE_BULK_RECV ||
		    i == C2_NET_QT_ACTIVE_BULK_RECV) {
			/* fake the length in the event */
			ev.nbe_length = buf_size;
		}

		nb->nb_flags |= C2_NET_BUF_IN_USE;
		c2_net_buffer_event_post(&ev);
		C2_UT_ASSERT(ut_cb_calls[i] == 1);
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_IN_USE));
		C2_UT_ASSERT(nb->nb_timeout == C2_TIME_NEVER);
	}
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);

	/* add a buffer and fake del - check callback */
	nb = &nbs[C2_NET_QT_PASSIVE_BULK_SEND];
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	c2_net_desc_free(&nb->nb_desc);
	rc = c2_net_buffer_add(nb, tm);
	C2_UT_ASSERT(rc == 0);
	num_adds[nb->nb_qtype]++;
	max_bytes[nb->nb_qtype] = max64u(nb->nb_length,
					 max_bytes[nb->nb_qtype]);

	ut_buf_del_called = false;
	c2_clink_add(&tm->ntm_chan, &tmwait);
	c2_net_buffer_del(nb, tm);
	C2_UT_ASSERT(ut_buf_del_called);
	num_dels[nb->nb_qtype]++;

	/* wait on channel for post (and consume UT thread) */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	rc = c2_thread_join(&ut_del_thread);
	C2_UT_ASSERT(rc == 0);

	/* Initialize and add buffers for multi-use.
	   Note: the net API does not restrict multi-use to the recv queue.
	 */
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);
	for (i = C2_NET_QT_MSG_RECV; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
		nb->nb_qtype = i;
		nb->nb_callbacks = &ut_buf_multi_use_cb;
		nb->nb_length = 0;
		/* NB: real code sets nb_ep to server ep */
		switch (i) {
		case C2_NET_QT_MSG_RECV:
			nb->nb_min_receive_size = buf_size;
			nb->nb_max_receive_msgs = 2;
			break;
		case C2_NET_QT_MSG_SEND:
			nb->nb_ep = ep2;
			nb->nb_length = buf_size;
			break;
		case C2_NET_QT_PASSIVE_BULK_RECV:
			nb->nb_ep = ep2;
			break;
		case C2_NET_QT_PASSIVE_BULK_SEND:
			nb->nb_ep = ep2;
			nb->nb_length = buf_size;
			break;
		case C2_NET_QT_ACTIVE_BULK_RECV:
			make_desc(&nb->nb_desc);
			break;
		case C2_NET_QT_ACTIVE_BULK_SEND:
			nb->nb_length = buf_size;
			make_desc(&nb->nb_desc);
			break;
		}
		nb->nb_timeout = c2_time_add(c2_time_now(),
					     c2tt_to_period);
		nb->nb_tm = NULL;
		rc = c2_net_buffer_add(nb, tm);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_QUEUED);
		C2_UT_ASSERT(nb->nb_tm == tm);
		num_adds[nb->nb_qtype]++;
		max_bytes[nb->nb_qtype] = max64u(buf_size,
						 max_bytes[nb->nb_qtype]);
	}
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 3);

	/* Issue multiple fake buffer "post" with the RETAIN flag. */
	for (reuse_cnt = 0; reuse_cnt < NUM_REUSES; ++reuse_cnt) {
		bool retain = true;
		if (reuse_cnt == NUM_REUSES - 1)
			retain = false;
		for (i = C2_NET_QT_MSG_RECV; i < C2_NET_QT_NR; ++i) {
			c2_time_t to_before;
			struct c2_net_buffer_event ev = {
				.nbe_buffer = &nbs[i],
				.nbe_status = 0,
			};
			DELAY_MS(1);
			ev.nbe_time = c2_time_now();
			nb = &nbs[i];

			if (i == C2_NET_QT_MSG_RECV) {
				/* simulate transport ep in recv msg */
				ev.nbe_ep = ep2;
				c2_net_end_point_get(ep2);
			}

			if (i == C2_NET_QT_MSG_RECV ||
			    i == C2_NET_QT_PASSIVE_BULK_RECV ||
			    i == C2_NET_QT_ACTIVE_BULK_RECV) {
				/* fake the length in the event */
				ev.nbe_length = buf_size;
				nb->nb_length = 0; /* the tests expect this */
			}

			nb->nb_flags |= C2_NET_BUF_IN_USE;
			if (retain) {
				nb->nb_flags |= C2_NET_BUF_RETAIN;
				to_before = nb->nb_timeout;
				ut_multi_use_expect_queued = true;
				if (i == C2_NET_QT_MSG_SEND)
					c2_net_end_point_get(ep2); /* adjust */
			} else {
				ut_multi_use_expect_queued = false;
			}
			c2_net_buffer_event_post(&ev);
			C2_UT_ASSERT(ut_multi_use_got_buf == nb);
			C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_RETAIN));
			if (retain) {
				C2_UT_ASSERT(to_before == nb->nb_timeout);
				C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_QUEUED);
				C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_IN_USE);
			} else {
				C2_UT_ASSERT(nb->nb_timeout == C2_TIME_NEVER);
				C2_UT_ASSERT(!(nb->nb_flags &
					       C2_NET_BUF_QUEUED));
				C2_UT_ASSERT(!(nb->nb_flags &
					       C2_NET_BUF_IN_USE));
			}
		}
	}

	/* free end point */
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);
	rc = c2_net_end_point_put(ep2);
	C2_UT_ASSERT(rc == 0);

	/* TM stop */
	c2_clink_add(&tm->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm, false);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_stop_called);

	/* wait on channel for stopped */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(ut_tm_event_cb_calls == 2);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STOPPED);

	/* clean up; real xprt would handle this itself */
	c2_thread_join(&ut_tm_thread);
	c2_thread_fini(&ut_tm_thread);

	/* de-register channel waiter */
	c2_clink_fini(&tmwait);

	/* get stats (specific queue, then all queues) */
	i = C2_NET_QT_PASSIVE_BULK_SEND;
	rc = c2_net_tm_stats_get(tm, i, &qs[0], false);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(qs[0].nqs_num_adds == num_adds[i]);
	C2_UT_ASSERT(qs[0].nqs_num_dels == num_dels[i]);
	C2_UT_ASSERT(qs[0].nqs_total_bytes == total_bytes[i]);
	C2_UT_ASSERT(qs[0].nqs_max_bytes == max_bytes[i]);
	C2_UT_ASSERT((qs[0].nqs_num_f_events + qs[0].nqs_num_s_events)
		     == num_adds[i] + reuse_cnt - 1);
	C2_UT_ASSERT(qs[0].nqs_num_f_events + qs[0].nqs_num_s_events > 0 &&
		     qs[0].nqs_time_in_queue > 0);

	rc = c2_net_tm_stats_get(tm, C2_NET_QT_NR, qs, true);
	C2_UT_ASSERT(rc == 0);
	for (i = 0; i < C2_NET_QT_NR; i++) {
		KPRN("i=%d\n", i);
#define QS(x)  KPRN("\t" #x "=%"PRId64"\n", qs[i].nqs_##x)
#define QS2(x) KPRN("\t" #x "=%"PRId64" [%"PRId64"]\n", qs[i].nqs_##x, x[i])
		QS2(num_adds);
		QS2(num_dels);
		QS2(total_bytes);
		QS(max_bytes);
		QS(num_f_events);
		QS(num_s_events);
		QS(time_in_queue);
		C2_UT_ASSERT(qs[i].nqs_num_adds == num_adds[i]);
		C2_UT_ASSERT(qs[i].nqs_num_dels == num_dels[i]);
		C2_UT_ASSERT(qs[i].nqs_total_bytes == total_bytes[i]);
		C2_UT_ASSERT(qs[i].nqs_total_bytes >= qs[i].nqs_max_bytes);
		C2_UT_ASSERT(qs[i].nqs_max_bytes == max_bytes[i]);
		C2_UT_ASSERT((qs[i].nqs_num_f_events + qs[i].nqs_num_s_events)
			     == num_adds[i] + reuse_cnt - 1);
		C2_UT_ASSERT(qs[i].nqs_num_f_events +
			     qs[i].nqs_num_s_events > 0 &&
			     qs[i].nqs_time_in_queue > 0);
	}

	rc = c2_net_tm_stats_get(tm, C2_NET_QT_NR, qs, false);
	C2_UT_ASSERT(rc == 0);
	for (i = 0; i < C2_NET_QT_NR; i++) {
		C2_UT_ASSERT(qs[i].nqs_num_adds == 0);
		C2_UT_ASSERT(qs[i].nqs_num_dels == 0);
		C2_UT_ASSERT(qs[i].nqs_num_f_events == 0);
		C2_UT_ASSERT(qs[i].nqs_num_s_events == 0);
		C2_UT_ASSERT(qs[i].nqs_total_bytes == 0);
		C2_UT_ASSERT(qs[i].nqs_max_bytes == 0);
		C2_UT_ASSERT(qs[i].nqs_time_in_queue == 0);
	}

	/* fini the TM */
	ut_tm_fini_called = false;
	c2_net_tm_fini(tm);
	C2_UT_ASSERT(ut_tm_fini_called);

	/* TM fini releases final end point */
	C2_UT_ASSERT(ut_last_ep_released == ep2);

	/*
	 * Test APIs for synchronous buffer event delivery
	 */

	/* restart the TM */
	ut_tm_init_called = false;
	ut_tm_fini_called = false;
	rc = c2_net_tm_init(tm, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm->ntm_dom_linkage));
	C2_UT_ASSERT(tm->ntm_bev_auto_deliver);

	/* request synchronous buffer event delivery */
	C2_UT_ASSERT(dom->nd_xprt->nx_ops->xo_bev_deliver_sync == NULL);
	rc = c2_net_buffer_event_deliver_synchronously(tm);
	C2_UT_ASSERT(rc == -ENOSYS); /* optional support */
	C2_UT_ASSERT(tm->ntm_bev_auto_deliver);
	C2_UT_ASSERT(!ut_bev_deliver_sync_called);
	ut_xprt_ops.xo_bev_deliver_sync = ut_bev_deliver_sync; /* set op */
	C2_UT_ASSERT(dom->nd_xprt->nx_ops->xo_bev_deliver_sync != NULL);
	rc = c2_net_buffer_event_deliver_synchronously(tm);
	C2_UT_ASSERT(ut_bev_deliver_sync_called);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(!tm->ntm_bev_auto_deliver);

	/* start the TM */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);
	rc = c2_net_tm_start(tm, "addr3");
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);

	/* test the synchronous buffer event delivery APIs */
	C2_UT_ASSERT(!ut_bev_pending_called);
	brc = c2_net_buffer_event_pending(tm);
	C2_UT_ASSERT(ut_bev_pending_called);
	C2_UT_ASSERT(!brc);

	C2_UT_ASSERT(!ut_bev_notify_called);
	c2_net_buffer_event_notify(tm, &tm->ntm_chan);
	C2_UT_ASSERT(ut_bev_notify_called);

	ut_bev_pending_called = false;
	brc = c2_net_buffer_event_pending(tm);
	C2_UT_ASSERT(ut_bev_pending_called);
	C2_UT_ASSERT(brc);

	C2_UT_ASSERT(!ut_bev_deliver_all_called);
	c2_net_buffer_event_deliver_all(tm);
	C2_UT_ASSERT(ut_bev_deliver_all_called);

	/* TM stop and fini */
	c2_clink_add(&tm->ntm_chan, &tmwait);
	rc = c2_net_tm_stop(tm, false);
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_thread_join(&ut_tm_thread); /* cleanup thread */
	c2_thread_fini(&ut_tm_thread);
	c2_clink_fini(&tmwait);
	c2_net_tm_fini(tm);

	/* de-register and free buffers */
	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		ut_buf_deregister_called = false;
		c2_net_desc_free(&nb->nb_desc);
		c2_net_buffer_deregister(nb, dom);
		C2_UT_ASSERT(ut_buf_deregister_called);
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_REGISTERED));
		c2_bufvec_free(&nb->nb_buffer);
	}
	c2_free(nbs);

	/* fini the domain */
	C2_UT_ASSERT(ut_dom_fini_called == false);
	c2_net_domain_fini(dom);
	C2_UT_ASSERT(ut_dom_fini_called);
}

#include "tm_provision_ut.c"

const struct c2_test_suite c2_net_bulk_if_ut = {
        .ts_name = "net-bulk-if",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_if", test_net_bulk_if },
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_bulk_if_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
