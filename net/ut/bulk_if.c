/* -*- C -*- */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/mutex.h"
#include "lib/ut.h"

#include "net/net.h"

static struct c2_net_domain utdom;
static struct c2_net_transfer_mc ut_tm;

void make_desc(struct c2_net_buf_desc *desc);

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
	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_dom_init_called = true;
	dom->nd_xprt_private = &ut_xprt_pvt;
	return 0;
}

static bool ut_dom_fini_called=false;
static void ut_dom_fini(struct c2_net_domain *dom)
{
	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_dom_fini_called = true;
}

/* params */
enum {
	UT_MAX_BUF_SIZE=4096,
	UT_MAX_BUF_SEGMENT_SIZE=2048,
	UT_MAX_BUF_SEGMENTS=4,
};
static bool ut_get_max_buffer_size_called = false;
static int ut_get_max_buffer_size(struct c2_net_domain *dom, c2_bcount_t *size)
{
	ut_get_max_buffer_size_called = true;
	*size = UT_MAX_BUF_SIZE;
	return 0;
}
static bool ut_get_max_buffer_segment_size_called = false;
static int ut_get_max_buffer_segment_size(struct c2_net_domain *dom,
					  c2_bcount_t *size)
{
	ut_get_max_buffer_segment_size_called = true;
	*size = UT_MAX_BUF_SEGMENT_SIZE;
	return 0;
}
static bool ut_get_max_buffer_segments_called = false;
static int ut_get_max_buffer_segments(struct c2_net_domain *dom,
				      int32_t *num_segs)
{
	ut_get_max_buffer_segments_called = true;
	*num_segs = UT_MAX_BUF_SEGMENTS;
	return 0;
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
	struct c2_net_domain *dom;
	ut_end_point_release_called = true;
	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	ut_last_ep_released = ep;
	dom = ep->nep_dom;
	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	c2_list_del(&ep->nep_dom_linkage);
	ep->nep_dom = NULL;
	utep = container_of(ep, struct ut_ep, uep);
	c2_free(utep);
}
static bool ut_end_point_create_called = false;
static int ut_end_point_create(struct c2_net_end_point **epp,
			       struct c2_net_domain *dom,
			       va_list varargs)
{
	char *ap;
	struct ut_ep *utep;
	struct c2_net_end_point *ep;

	C2_ASSERT(c2_mutex_is_locked(&dom->nd_mutex));
	ut_end_point_create_called = true;
	ap = va_arg(varargs, char *);
	if (ap == NULL) {
		/* end of args - don't support dynamic */
		return -ENOSYS;
	}
	/* check if its already on the domain list */
	c2_list_for_each_entry(&dom->nd_end_points, ep,
			       struct c2_net_end_point,
			       nep_dom_linkage) {
		utep = container_of(ep, struct ut_ep, uep);
		if (strcmp(utep->addr, ap) == 0) {
			c2_ref_get(&ep->nep_ref); /* refcnt++ */
			*epp = ep;
			return 0;
		}
	}
	/* allocate a new end point */
	C2_ALLOC_PTR(utep);
	utep->addr = ap; /* avoid strdup; this is a ut! */
	c2_ref_init(&utep->uep.nep_ref, 1, ut_end_point_release);
	utep->uep.nep_dom = dom;
	c2_list_link_init(&utep->uep.nep_dom_linkage);
	c2_list_add_tail(&dom->nd_end_points, &utep->uep.nep_dom_linkage);
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
static int ut_buf_deregister(struct c2_net_buffer *nb)
{
	C2_ASSERT(c2_mutex_is_locked(&nb->nb_dom->nd_mutex));
	ut_buf_deregister_called = true;
	return 0;
}

static bool ut_buf_add_called = false;
static int ut_buf_add(struct c2_net_buffer *nb)
{
	C2_UT_ASSERT(c2_mutex_is_locked(&nb->nb_tm->ntm_mutex));
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
	int rc;
	struct c2_net_event ev = {
		.nev_qtype = nb->nb_qtype,
		.nev_tm = nb->nb_tm,
		.nev_buffer = nb,
		.nev_status = 0,
		.nev_payload = NULL
	};
	c2_time_now(&ev.nev_time);

	/* post requested event */
	rc = c2_net_tm_event_post(ev.nev_tm, &ev);
	C2_UT_ASSERT(rc == 0);
}

static bool ut_buf_del_called = false;
static int ut_buf_del(struct c2_net_buffer *nb)
{
	int rc;
	C2_UT_ASSERT(c2_mutex_is_locked(&nb->nb_tm->ntm_mutex));
	ut_buf_del_called = true;
	if (!(nb->nb_flags & C2_NET_BUF_IN_USE))
		nb->nb_flags |= C2_NET_BUF_CANCELLED;
	rc = C2_THREAD_INIT(&ut_del_thread, struct c2_net_buffer *, NULL,
			    &ut_post_del_thread, nb);
	C2_UT_ASSERT(rc == 0);
	return rc;
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
static int ut_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct ut_tm_pvt *tmp;
	C2_ASSERT(c2_mutex_is_locked(&tm->ntm_dom->nd_mutex));
	tmp = tm->ntm_xprt_private;
	C2_UT_ASSERT(tmp->tm == tm);
	c2_free(tmp);
	ut_tm_fini_called = true;
	return 0;
}

struct c2_thread ut_tm_thread;
static void ut_post_ev_thread(int n)
{
	int rc;
	struct c2_net_event ev = {
		.nev_qtype = C2_NET_QT_NR,
		.nev_tm = &ut_tm,
		.nev_buffer = NULL,
		.nev_status = 0,
		.nev_payload = (void *) (enum c2_net_tm_state) n
	};
	c2_time_now(&ev.nev_time);

	/* post requested event */
	rc = c2_net_tm_event_post(ev.nev_tm, &ev);
	C2_UT_ASSERT(rc == 0);
}

static bool ut_tm_start_called = false;
static int ut_tm_start(struct c2_net_transfer_mc *tm)
{
	int rc;

	C2_UT_ASSERT(c2_mutex_is_locked(&tm->ntm_mutex));
	ut_tm_start_called = true;
	/* create bg thread to post start state change event.
	   cannot do it here: we are in dom lock, post would assert.
	 */
	rc = C2_THREAD_INIT(&ut_tm_thread, int, NULL,
			    &ut_post_ev_thread, C2_NET_TM_STARTED);
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
			    &ut_post_ev_thread, C2_NET_TM_STOPPED);
	C2_UT_ASSERT(rc == 0);
	return rc;
}

static const struct c2_net_xprt_ops ut_xprt_ops = {
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
		if (i == C2_NET_QT_MSG_RECV) {
			sz = min64(256, buf_seg_size);
			nr = 1;
		} else {
			nr = buf_segs;
			if ((buf_size / buf_segs) > buf_seg_size) {
				sz = buf_seg_size;
				C2_ASSERT((sz * nr) <= buf_size);
			} else {
				sz = buf_size/buf_segs;
			}
		}
		rc = c2_bufvec_alloc(&nb->nb_buffer, nr, sz);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == nr);
		C2_UT_ASSERT(c2_vec_count(&nb->nb_buffer.ov_vec) == (sz * nr));
	}

	return nbs;
}

void make_desc(struct c2_net_buf_desc *desc)
{
	static const char *p = "descriptor";
	size_t len = strlen(p)+1;
	desc->nbd_data = c2_alloc(len);
	desc->nbd_len = len;
	strcpy(desc->nbd_data, p);
}

/* callback subs */
#define UT_CB_CALL(qt)							 \
({									 \
	C2_UT_ASSERT(ev->nev_qtype == qt);				 \
	C2_UT_ASSERT(ev->nev_buffer != NULL);				 \
	C2_UT_ASSERT(ev->nev_buffer->nb_qtype == ev->nev_qtype);	 \
	ut_cb_calls[qt]++;						 \
	total_bytes[qt] += ev->nev_buffer->nb_length;			 \
	max_bytes[qt] = max64u(ev->nev_buffer->nb_length,max_bytes[qt]); \
})

static int ut_cb_calls[C2_NET_QT_NR];
static int num_adds[C2_NET_QT_NR];
static int num_dels[C2_NET_QT_NR];
static c2_bcount_t total_bytes[C2_NET_QT_NR];
static c2_bcount_t max_bytes[C2_NET_QT_NR];
void ut_msg_recv_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	UT_CB_CALL(C2_NET_QT_MSG_RECV);
}

void ut_msg_send_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	UT_CB_CALL(C2_NET_QT_MSG_SEND);
}

void ut_passive_bulk_recv_cb(struct c2_net_transfer_mc *tm,
			     struct c2_net_event *ev)
{
	UT_CB_CALL(C2_NET_QT_PASSIVE_BULK_RECV);
}

void ut_passive_bulk_send_cb(struct c2_net_transfer_mc *tm,
			     struct c2_net_event *ev)
{
	UT_CB_CALL(C2_NET_QT_PASSIVE_BULK_SEND);
}

void ut_active_bulk_recv_cb(struct c2_net_transfer_mc *tm,
			    struct c2_net_event *ev)
{
	UT_CB_CALL(C2_NET_QT_ACTIVE_BULK_RECV);
}

void ut_active_bulk_send_cb(struct c2_net_transfer_mc *tm,
			    struct c2_net_event *ev)
{
	UT_CB_CALL(C2_NET_QT_ACTIVE_BULK_SEND);
}

static int ut_event_cb_calls = 0;
void ut_event_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	ut_event_cb_calls++;
	if (ev->nev_qtype != C2_NET_QT_NR) {
		UT_CB_CALL(ev->nev_qtype);
		ut_cb_calls[ev->nev_qtype]--;
	}
}

static int ut_tm_event_cb_calls = 0;
void ut_tm_event_cb(struct c2_net_transfer_mc *tm, struct c2_net_event *ev)
{
	ut_tm_event_cb_calls++;
	if (ev->nev_qtype != C2_NET_QT_NR){
		UT_CB_CALL(ev->nev_qtype);
		ut_cb_calls[ev->nev_qtype]--;
	}
}

/* UT transfer machine */
struct c2_net_tm_callbacks ut_tm_cb = {
	.ntc_msg_recv_cb = ut_msg_recv_cb,
	.ntc_msg_send_cb = ut_msg_send_cb,
	.ntc_event_cb = ut_tm_event_cb
};

struct c2_net_tm_callbacks ut_buf_cb = {
	.ntc_passive_bulk_recv_cb = ut_passive_bulk_recv_cb,
	.ntc_passive_bulk_send_cb = ut_passive_bulk_send_cb,
	.ntc_active_bulk_recv_cb = ut_active_bulk_recv_cb,
	.ntc_active_bulk_send_cb = ut_active_bulk_send_cb,
	.ntc_event_cb = ut_event_cb
};

static struct c2_net_transfer_mc ut_tm = {
	.ntm_callbacks = &ut_tm_cb,
	.ntm_state = C2_NET_TM_UNDEFINED
};

/*
  Unit test starts
 */
void test_net_bulk_if(void)
{
	int rc, i;
	c2_bcount_t buf_size, buf_seg_size;
	int32_t   buf_segs;
	struct c2_net_domain *dom = &utdom;
	struct c2_net_transfer_mc *tm = &ut_tm;
	struct c2_net_buffer *nbs;
	struct c2_net_buffer *nb;
	struct c2_net_end_point *ep1, *ep2, *ep;
	struct c2_net_buf_desc d1, d2;

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
	rc = c2_net_domain_get_max_buffer_size(dom, &buf_size);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_get_max_buffer_size_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_size == UT_MAX_BUF_SIZE);

	/* get max buffer segment size */
	C2_UT_ASSERT(ut_get_max_buffer_segment_size_called == false);
	buf_seg_size = 0;
	rc = c2_net_domain_get_max_buffer_segment_size(dom, &buf_seg_size);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_get_max_buffer_segment_size_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_seg_size == UT_MAX_BUF_SEGMENT_SIZE);

	/* get max buffer segments */
	C2_UT_ASSERT(ut_get_max_buffer_segments_called == false);
	buf_segs = 0;
	rc = c2_net_domain_get_max_buffer_segments(dom, &buf_segs);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_get_max_buffer_segments_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_UT_ASSERT(buf_segs == UT_MAX_BUF_SEGMENTS);

	/* Test desired end point behavior
	   A real transport isn't actually forced to maintain
	   reference counts this way, but ought to do so.
	 */
	C2_UT_ASSERT(ut_end_point_create_called == false);
	rc = c2_net_end_point_create(&ep1, dom, 0);
	C2_UT_ASSERT(rc != 0); /* no dynamic */
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_ASSERT(c2_list_is_empty(&dom->nd_end_points));

	ut_end_point_create_called = false;
	rc = c2_net_end_point_create(&ep1, dom, "addr1", 0);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_end_point_create_called);
	C2_ASSERT(c2_mutex_is_not_locked(&dom->nd_mutex));
	C2_ASSERT(!c2_list_is_empty(&dom->nd_end_points));
	C2_UT_ASSERT(c2_atomic64_get(&ep1->nep_ref.ref_cnt) == 1);

	rc = c2_net_end_point_create(&ep2, dom, "addr2", 0);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ep2 != ep1);

	rc = c2_net_end_point_create(&ep, dom, "addr1", 0);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ep == ep1);
	C2_UT_ASSERT(c2_atomic64_get(&ep->nep_ref.ref_cnt) == 2);

	C2_UT_ASSERT(ut_end_point_release_called == false);
	rc = c2_net_end_point_get(ep); /* refcnt=3 */
	C2_UT_ASSERT(rc == 0);
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

	/* allocate buffers for testing */
	nbs = allocate_buffers(buf_size, buf_seg_size, buf_segs);

	/* register the buffers */
	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		nb->nb_flags = 0;
		ut_buf_register_called = false;
		rc = c2_net_buffer_register(nb, dom);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(ut_buf_register_called);
		C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_REGISTERED);
		num_adds[i] = 0;
		num_dels[i] = 0;
		total_bytes[i] = 0;
	}

	/* TM init with callbacks */
	rc = c2_net_tm_init(tm, dom);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_init_called);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom->nd_tms, &tm->ntm_dom_linkage));

	/* add MSG_RECV buf - should fail as not started */
	nb = &nbs[C2_NET_QT_MSG_RECV];
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	rc = c2_net_buffer_add(nb, tm);
	C2_UT_ASSERT(rc == -EPERM);
	C2_UT_ASSERT(ut_buf_add_called == false);

	/* TM start */
	struct c2_clink tmwait;
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	rc = c2_net_tm_start(tm, ep2);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_start_called);
	C2_UT_ASSERT(tm->ntm_ep == ep2);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTING ||
		     tm->ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);

	/* wait on channel for started */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(ut_tm_event_cb_calls == 1);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);

	/* add MSG_RECV buf - should succeeded as now started */
	nb = &nbs[C2_NET_QT_MSG_RECV];
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
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
		/* NB: real code sets nb_ep to server ep */
		switch (i) {
		case C2_NET_QT_MSG_SEND:
			nb->nb_ep = ep2;
			nb->nb_length = buf_size;
			break;
		case C2_NET_QT_PASSIVE_BULK_RECV:
			nb->nb_ep = ep2;
			nb->nb_callbacks = &ut_buf_cb;
			break;
		case C2_NET_QT_PASSIVE_BULK_SEND:
			nb->nb_ep = ep2;
			nb->nb_callbacks = &ut_buf_cb;
			nb->nb_length = buf_size;
			break;
		case C2_NET_QT_ACTIVE_BULK_RECV:
			nb->nb_callbacks = &ut_buf_cb;
			make_desc(&nb->nb_desc);
			break;
		case C2_NET_QT_ACTIVE_BULK_SEND:
			nb->nb_callbacks = &ut_buf_cb;
			nb->nb_length = buf_size;
			make_desc(&nb->nb_desc);
			break;
		}
		rc = c2_net_buffer_add(nb, tm);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_QUEUED);
		C2_UT_ASSERT(nb->nb_tm == tm);
		num_adds[nb->nb_qtype]++;
		max_bytes[nb->nb_qtype] = max64u(nb->nb_length,
						 max_bytes[nb->nb_qtype]);
	}
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 5);

	/* fake each type of buffer "post" response.
	   xprt normally does this
	 */
	for (i = C2_NET_QT_MSG_RECV; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		struct c2_net_event ev = {
			.nev_qtype = i,
			.nev_tm = tm,
			.nev_buffer = nb,
			.nev_status = 0,
			.nev_payload = NULL
		};
		c2_time_now(&ev.nev_time);

		if (i == C2_NET_QT_MSG_RECV) {
			/* simulate transport adding ep to buf */
			nb->nb_ep = ep2;
			rc = c2_net_end_point_get(ep2);
			C2_UT_ASSERT(rc == 0);
		}

		rc = c2_net_tm_event_post(tm, &ev);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(ut_cb_calls[i] == 1);

		if (i == C2_NET_QT_MSG_RECV) {
			/* simulate transport removing ep from buf */
			nb->nb_ep = NULL;
			rc = c2_net_end_point_put(ep2);
			C2_UT_ASSERT(rc == 0);
		}
	}
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);

	/* add a buffer and fake del - check callback */
	nb = &nbs[C2_NET_QT_PASSIVE_BULK_SEND];
	C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_QUEUED));
	nb->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	rc = c2_net_buffer_add(nb, tm);
	C2_UT_ASSERT(rc == 0);
	num_adds[nb->nb_qtype]++;
	max_bytes[nb->nb_qtype] = max64u(nb->nb_length,
					 max_bytes[nb->nb_qtype]);

	ut_buf_del_called = false;
	c2_clink_add(&tm->ntm_chan, &tmwait);
	rc = c2_net_buffer_del(nb, tm);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_buf_del_called);
	C2_UT_ASSERT(nb->nb_flags | C2_NET_BUF_CANCELLED);
	num_dels[nb->nb_qtype]++;

	/* wait on channel for post */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);

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

	/* get stats */
	struct c2_net_qstats qs[C2_NET_QT_NR];
	i = C2_NET_QT_PASSIVE_BULK_SEND;
	rc = c2_net_tm_stats_get(tm, i, &qs[0], false);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(qs[0].nqs_num_adds == num_adds[i]);
	C2_UT_ASSERT(qs[0].nqs_num_dels == num_dels[i]);
	C2_UT_ASSERT(qs[0].nqs_total_bytes == total_bytes[i]);
	C2_UT_ASSERT(qs[0].nqs_max_bytes == max_bytes[i]);
	C2_UT_ASSERT((qs[0].nqs_num_f_events + qs[0].nqs_num_s_events)
		     == num_adds[i]);
	C2_UT_ASSERT((qs[0].nqs_num_f_events + qs[0].nqs_num_s_events) > 0 &&
		     (qs[0].nqs_time_in_queue.ts.tv_sec +
		      qs[0].nqs_time_in_queue.ts.tv_nsec) > 0);

	rc = c2_net_tm_stats_get(tm,
				 C2_NET_QT_NR,
				 qs,
				 true);
	C2_UT_ASSERT(rc == 0);
	for(i=0; i < C2_NET_QT_NR; i++) {
		C2_UT_ASSERT(qs[i].nqs_num_adds == num_adds[i]);
		C2_UT_ASSERT(qs[i].nqs_num_dels == num_dels[i]);
		C2_UT_ASSERT(qs[i].nqs_total_bytes == total_bytes[i]);		
		C2_UT_ASSERT(qs[i].nqs_total_bytes >= qs[i].nqs_max_bytes);
		C2_UT_ASSERT(qs[i].nqs_max_bytes == max_bytes[i]);
		C2_UT_ASSERT((qs[i].nqs_num_f_events + qs[i].nqs_num_s_events)
			     == num_adds[i]);
		C2_UT_ASSERT((qs[i].nqs_num_f_events + 
			      qs[i].nqs_num_s_events) > 0 &&
			     (qs[i].nqs_time_in_queue.ts.tv_sec +
			      qs[i].nqs_time_in_queue.ts.tv_nsec) > 0);
	}

	rc = c2_net_tm_stats_get(tm,
				 C2_NET_QT_NR,
				 qs,
				 false);
	C2_UT_ASSERT(rc == 0);
	for(i=0; i < C2_NET_QT_NR; i++) {
		C2_UT_ASSERT(qs[i].nqs_num_adds == 0);
		C2_UT_ASSERT(qs[i].nqs_num_dels == 0);
		C2_UT_ASSERT(qs[i].nqs_num_f_events == 0);
		C2_UT_ASSERT(qs[i].nqs_num_s_events == 0);
		C2_UT_ASSERT(qs[i].nqs_total_bytes == 0);
		C2_UT_ASSERT(qs[i].nqs_max_bytes == 0);
		C2_UT_ASSERT(qs[i].nqs_time_in_queue.ts.tv_sec == 0);		
		C2_UT_ASSERT(qs[i].nqs_time_in_queue.ts.tv_nsec == 0);		
	}

	/* fini the TM */
	rc = c2_net_tm_fini(tm);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_tm_fini_called);

	/* free end points */
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 1);
	rc = c2_net_end_point_put(ep2);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(ut_last_ep_released == ep2);

	/* de-register buffers */
	for (i = 0; i < C2_NET_QT_NR; ++i) {
		nb = &nbs[i];
		ut_buf_deregister_called = false;
		rc = c2_net_buffer_deregister(nb, dom);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(ut_buf_deregister_called);
		C2_UT_ASSERT(!(nb->nb_flags & C2_NET_BUF_REGISTERED));
	}

	/* fini the domain */
	C2_UT_ASSERT(ut_dom_fini_called == false);
	c2_net_domain_fini(dom);
	C2_UT_ASSERT(ut_dom_fini_called);
}

const struct c2_test_suite net_bulk_if_ut = {
        .ts_name = "net-bulk-if",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_if", test_net_bulk_if },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
