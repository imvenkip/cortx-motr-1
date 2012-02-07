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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 *                  Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 1/4/2012
 */

#include "net/lnet/lnet_main.c"
#include "lib/arith.h" /* max64u */
#include "lib/ut.h"

static int ut_verbose = 0;

static int ut_subs_saved;
static struct nlx_xo_interceptable_subs saved_xo_subs;
#ifdef __KERNEL__
static struct nlx_kcore_interceptable_subs saved_kcore_subs;
#endif

static void ut_save_subs(void)
{
	if (ut_subs_saved > 0)
		return;
	ut_subs_saved = 1;
	saved_xo_subs = nlx_xo_iv;
#ifdef __KERNEL__
	saved_kcore_subs = nlx_kcore_iv;
#endif
}

static void ut_restore_subs(void)
{
	C2_ASSERT(ut_subs_saved > 0);
	nlx_xo_iv = saved_xo_subs;
#ifdef __KERNEL__
	nlx_kcore_iv = saved_kcore_subs;
#endif
}

static bool ut_chan_timedwait(struct c2_clink *link, uint32_t secs)
{
	c2_time_t timeout = c2_time_from_now(secs,0);
	return c2_chan_timedwait(link, timeout);
}

/* write a pattern to a buffer */
static void ut_net_buffer_sign(struct c2_net_buffer *nb,
			       c2_bcount_t len,
			       unsigned char seed)
{
	struct c2_bufvec_cursor cur;
	c2_bcount_t i;
	c2_bcount_t step;
	unsigned char val;
	unsigned char *p;

	val = (c2_bcount_t) seed + ((c2_bcount_t) seed - 1) * len;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	i = 0;
	do {
		c2_bcount_t bytes = 0;
		step = c2_bufvec_cursor_step(&cur);
		p = c2_bufvec_cursor_addr(&cur);
		for ( ; i < len && bytes < step; ++bytes, ++i, ++p, ++val) {
			*p = val;
		}
	} while (i < len && !c2_bufvec_cursor_move(&cur, step));
	C2_UT_ASSERT(i == len);
	return;
}

/* check the pattern in the buffer */
static bool ut_net_buffer_authenticate(struct c2_net_buffer *nb,
				       c2_bcount_t len,
				       c2_bcount_t offset,
				       unsigned char seed)
{
	struct c2_bufvec_cursor cur;
	c2_bcount_t i;
	c2_bcount_t step;
	unsigned char val;
	unsigned char *p;

	if (nb == NULL)
		return false;

	val = (c2_bcount_t) seed + ((c2_bcount_t) seed - 1) * len;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	i = 0;
	len += offset; /* range: offset <= i < len */
	do {
		c2_bcount_t bytes;
		step = c2_bufvec_cursor_step(&cur);
		if (i + step < offset) {
			i += step;
			continue;
		}
		p = c2_bufvec_cursor_addr(&cur);
		if (i < offset) {
			bytes = offset - i;
			p += bytes;
			i += bytes;
		} else
			bytes = 0;
		for ( ; i < len && bytes < step; ++i, ++p, ++bytes, ++val) {
			if (*p != val)
				return false;
		}
	} while (i < len && !c2_bufvec_cursor_move(&cur, step));
	if (i != len)
		return false;
	return true;
}

static enum c2_net_tm_ev_type ecb_evt;
static enum c2_net_tm_state ecb_tms;
static int32_t ecb_status;
static int ecb_count;
static void ecb_reset(void)
{
	ecb_evt = C2_NET_TEV_NR;
	ecb_tms = C2_NET_TM_UNDEFINED;
	ecb_status = 1;
	ecb_count = 0;
}

static void ut_tm_ecb(const struct c2_net_tm_event *ev)
{
	ecb_evt    = ev->nte_type;
	ecb_tms    = ev->nte_next_state;
	ecb_status = ev->nte_status;
	ecb_count++;
}

enum {
	STARTSTOP_DOM_NR = 3,
	STARTSTOP_PID = 12345,	/* same as LUSTRE_SRV_LNET_PID */
	STARTSTOP_PORTAL = 30,
};
#ifdef __KERNEL__
/* LUSTRE_SRV_LNET_PID macro is not available in user space */
C2_BASSERT(STARTSTOP_PID == LUSTRE_SRV_LNET_PID);
#endif

static enum c2_net_queue_type cb_qt1;
static struct c2_net_buffer *cb_nb1;
static int32_t cb_status1;
static c2_bcount_t cb_length1;
static c2_bcount_t cb_offset1;
static bool cb_save_ep1; /* save ep next call only */
static struct c2_net_end_point *cb_ep1; /* QT_MSG_RECV only */
static unsigned cb_called1;
enum { UT_CB_INVALID_STATUS = 9999999 };

static void ut_buf_cb1(const struct c2_net_buffer_event *ev)
{
	/* nlx_print_net_buffer_event("ut_buf_cb1", ev);*/
	cb_nb1     = ev->nbe_buffer;
	cb_qt1     = cb_nb1->nb_qtype;
	cb_status1 = ev->nbe_status;
	cb_length1 = ev->nbe_length;
	cb_offset1 = ev->nbe_offset;
	if (cb_qt1 == C2_NET_QT_MSG_RECV && cb_save_ep1 && ev->nbe_ep != NULL) {
		cb_ep1 = ev->nbe_ep;
		c2_net_end_point_get(cb_ep1);
	} else
		cb_ep1 = NULL;
	cb_save_ep1 = false;
	cb_called1++;
}

static void ut_cbreset1(void)
{
	cb_nb1     = NULL;
	cb_qt1     = C2_NET_QT_NR;
	cb_status1 = UT_CB_INVALID_STATUS;
	cb_length1 = 0;
	cb_offset1 = 0;
	C2_ASSERT(cb_ep1 == NULL); /* be harsh */
	cb_save_ep1 = false;
	cb_called1 = 0;
}

static enum c2_net_queue_type cb_qt2;
static struct c2_net_buffer *cb_nb2;
static int32_t cb_status2;
static c2_bcount_t cb_length2;
static c2_bcount_t cb_offset2;
static bool cb_save_ep2; /* save ep next call only */
static struct c2_net_end_point *cb_ep2; /* QT_MSG_RECV only */
static unsigned cb_called2;

static void ut_buf_cb2(const struct c2_net_buffer_event *ev)
{
	/* nlx_print_net_buffer_event("ut_buf_cb2", ev); */
	cb_nb2     = ev->nbe_buffer;
	cb_qt2     = cb_nb2->nb_qtype;
	cb_status2 = ev->nbe_status;
	cb_length2 = ev->nbe_length;
	cb_offset2 = ev->nbe_offset;
	if (cb_qt2 == C2_NET_QT_MSG_RECV && cb_save_ep2 && ev->nbe_ep != NULL) {
		cb_ep2 = ev->nbe_ep;
		c2_net_end_point_get(cb_ep2);
	} else
		cb_ep2 = NULL;
	cb_save_ep2 = false;
	cb_called2++;
}

static void ut_cbreset2(void)
{
	cb_nb2     = NULL;
	cb_qt2     = C2_NET_QT_NR;
	cb_status2 = UT_CB_INVALID_STATUS;
	cb_length2 = 0;
	cb_offset2 = 0;
	C2_ASSERT(cb_ep2 == NULL); /* be harsh */
	cb_save_ep2 = false;
	cb_called2 = 0;
}

static void ut_cbreset(void)
{
	ut_cbreset1();
	ut_cbreset2();
}

static char ut_line_buf[1024];
#define zUT(x,label)							\
do {									\
	int rc = x;							\
	if (rc != 0) {							\
		snprintf(ut_line_buf, sizeof(ut_line_buf),		\
			 "%s %d: %s: %d",__FILE__,__LINE__, #x, rc);	\
		C2_UT_FAIL(ut_line_buf);				\
		goto label;						\
	}								\
} while (0)

enum {
	UT_BUFS1    = 2,
	UT_BUFSEGS1 = 4,
	UT_BUFS2    = 1,
	UT_BUFSEGS2 = 2,
	UT_MSG_SIZE = 2048,
};
struct ut_data {
	int                            _debug_;
	struct c2_net_tm_callbacks     tmcb;
	struct c2_net_domain           dom1;
	struct c2_net_transfer_mc      tm1;
	struct c2_clink                tmwait1;
	struct c2_net_buffer_callbacks buf_cb1;
	struct c2_net_buffer           bufs1[UT_BUFS1];
	size_t                         buf_size1;
	c2_bcount_t                    buf_seg_size1;
	struct c2_net_domain           dom2;
	struct c2_net_transfer_mc      tm2;
	struct c2_clink                tmwait2;
	struct c2_net_buffer_callbacks buf_cb2;
	struct c2_net_buffer           bufs2[UT_BUFS2];
	size_t                         buf_size2;
	c2_bcount_t                    buf_seg_size2;
	struct c2_net_qstats           qs;
	char * const                  *nidstrs;
};

#define DOM1 (&td->dom1)
#define DOM2 (&td->dom2)
#define TM1  (&td->tm1)
#define TM2  (&td->tm2)

typedef void (*ut_test_fw_body_t)(struct ut_data *td);

static void ut_test_framework_dom_cleanup(struct ut_data *td,
					  struct c2_net_domain *dom)
{
	struct c2_clink cl;
	struct c2_net_buffer *nb;
	struct c2_net_transfer_mc *tm;
	size_t len;
	int qt;
	int i;

	c2_clink_init(&cl, NULL);

	c2_list_for_each_entry(&dom->nd_tms, tm,
			       struct c2_net_transfer_mc, ntm_dom_linkage) {
		/* iterate over buffers in each queue */
		for (qt = C2_NET_QT_MSG_RECV; qt < C2_NET_QT_NR; ++qt) {
			len = c2_tlist_length(&tm_tl, &tm->ntm_q[qt]);
			/* best effort; can't say if this will always work */
			for (i = 0; i < len; ++i) {
				nb = tm_tlist_head(&tm->ntm_q[qt]);
				c2_clink_add(&tm->ntm_chan, &cl);
				NLXDBGP(td,2,"Cleanup/DEL D:%p T:%p Q:%d B:%p\n",
					dom, tm, qt, nb);
				c2_net_buffer_del(nb, tm);
				ut_chan_timedwait(&cl, 10);
				c2_clink_del(&cl);
			}
			len = c2_tlist_length(&tm_tl, &tm->ntm_q[qt]);
			if (len != 0) {
				NLXDBGP(td,0,"Cleanup D:%p T:%p Q:%d B failed\n",
					dom, tm, qt);
			}
		}
		/* iterate over end points */
		if (c2_list_length(&tm->ntm_end_points) > 1) {
			struct c2_net_end_point *ep;
			struct c2_net_end_point *ep_next;
			c2_list_for_each_entry_safe(&tm->ntm_end_points, ep,
						    ep_next,
						    struct c2_net_end_point,
						    nep_tm_linkage) {
				if (ep == tm->ntm_ep)
					continue;
				while(c2_atomic64_get(&ep->nep_ref.ref_cnt)>= 1){
					NLXDBGP(td,2,"Cleanup/PUT D:%p T:%p "
						"E:%p\n", dom, tm, ep);
					c2_net_end_point_put(ep);
				}
			}
		}
		if (c2_list_length(&tm->ntm_end_points) > 1)
			NLXDBGP(td,0,"Cleanup D:%p T:%p E failed\n", dom, tm);
	}
}

static void ut_test_framework(ut_test_fw_body_t body, int dbg)
{
	struct ut_data *td;
	char * const *nidstrs = NULL;
	int i;
	int rc;

	/*
	  Setup.
	 */
	C2_ALLOC_PTR(td);
	C2_UT_ASSERT(td != NULL);
	if (td == NULL)
		return;
	td->_debug_ = dbg;

	c2_clink_init(&td->tmwait1, NULL);
	c2_clink_init(&td->tmwait2, NULL);
	td->tmcb.ntc_event_cb = ut_tm_ecb;
	TM1->ntm_callbacks = &td->tmcb;
	TM2->ntm_callbacks = &td->tmcb;
	for (i = C2_NET_QT_MSG_RECV; i < C2_NET_QT_NR; ++i) {
		td->buf_cb1.nbc_cb[i] = ut_buf_cb1;
		td->buf_cb2.nbc_cb[i] = ut_buf_cb2;
	}

	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(&nidstrs));
	C2_UT_ASSERT(nidstrs != NULL && nidstrs[0] != NULL);

#define SETUP_DOM(which)						\
do {									\
        struct c2_net_domain *dom = &td->dom ## which;			\
	struct c2_net_transfer_mc *tm = &td->tm ## which;		\
        C2_UT_ASSERT(!c2_net_domain_init(dom, &c2_net_lnet_xprt));	\
	{								\
		char epstr[C2_NET_LNET_XEP_ADDR_LEN];			\
		c2_bcount_t max_seg_size;				\
		struct c2_net_buffer      *nb;				\
									\
		max_seg_size = c2_net_domain_get_max_buffer_segment_size(dom); \
		C2_UT_ASSERT(max_seg_size > 0);				\
		C2_UT_ASSERT(max_seg_size >= UT_MSG_SIZE);		\
		td->buf_size ## which = max_seg_size * UT_BUFSEGS ## which; \
		td->buf_seg_size ## which = max_seg_size;		\
		for (i = 0; i < UT_BUFS ## which; ++i) {		\
			nb = &td->bufs ## which [i];			\
			rc = c2_bufvec_alloc(&nb->nb_buffer,		\
					     UT_BUFSEGS ## which,	\
					     max_seg_size);		\
			C2_UT_ASSERT(rc == 0);				\
			if (rc != 0) {					\
				C2_UT_FAIL("aborting: buf alloc failed"); \
				goto dereg ## which;			\
			}						\
			rc = c2_net_buffer_register(nb, dom);		\
			if (rc != 0) {					\
				C2_UT_FAIL("aborting: buf reg failed");	\
				goto dereg ## which;			\
			}						\
			C2_UT_ASSERT(nb->nb_flags & C2_NET_BUF_REGISTERED); \
			nb->nb_callbacks = &td->buf_cb ## which;	\
			NLXDBGPnl(td, 1, "D:%p T:%p B:%p [%u,%d]=%lu\n", \
				  dom, tm, nb, (unsigned) max_seg_size,	\
				  UT_BUFSEGS ## which,			\
				  (unsigned long) td->buf_size ## which); \
		}							\
									\
		C2_UT_ASSERT(!c2_net_tm_init(tm, dom));			\
									\
		sprintf(epstr, "%s:%d:%d:*",				\
			nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);	\
		c2_clink_add(&tm->ntm_chan, &td->tmwait ## which);	\
		C2_UT_ASSERT(!c2_net_tm_start(tm, epstr));		\
		c2_chan_wait(&td->tmwait ## which);			\
		c2_clink_del(&td->tmwait ## which);			\
		C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);	\
		if (tm->ntm_state == C2_NET_TM_FAILED) {		\
			C2_UT_FAIL("aborting: tm" #which " startup failed"); \
			goto fini ## which;				\
		}							\
		NLXDBGPnl(td, 1, "D:%p T:%p E:%s\n", dom, tm,		\
			  tm->ntm_ep->nep_addr);			\
	}								\
} while (0)

#define TEARDOWN_DOM(which)					\
do {								\
        struct c2_net_domain *dom = &td->dom ## which;		\
	struct c2_net_transfer_mc *tm = &td->tm ## which;	\
	c2_clink_add(&tm->ntm_chan, &td->tmwait ## which);	\
	C2_UT_ASSERT(!c2_net_tm_stop(tm, false));		\
	c2_chan_wait(&td->tmwait ## which);			\
	c2_clink_del(&td->tmwait ## which);			\
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STOPPED);	\
 fini ## which:							\
	c2_net_tm_fini(tm);					\
 dereg ## which:						\
	for (i = 0; i < UT_BUFS ## which; ++i) {		\
		struct c2_net_buffer      *nb;			\
		nb = &td->bufs ## which [i];			\
		if (nb->nb_buffer.ov_vec.v_nr == 0)		\
			continue;				\
		c2_net_buffer_deregister(nb, dom);		\
		c2_bufvec_free(&nb->nb_buffer);			\
	}							\
	c2_net_domain_fini(dom);				\
} while (0)

	SETUP_DOM(1);
	SETUP_DOM(2);

	td->nidstrs = nidstrs;
	(*body)(td);

	ut_test_framework_dom_cleanup(td, DOM2);
	ut_test_framework_dom_cleanup(td, DOM1);

	TEARDOWN_DOM(2);
	TEARDOWN_DOM(1);

	if (nidstrs != NULL)
		c2_net_lnet_ifaces_put(&nidstrs);
	C2_UT_ASSERT(nidstrs == NULL);
	c2_clink_fini(&td->tmwait1);
	c2_clink_fini(&td->tmwait2);
	c2_free(td);

#undef TEARDOWN_DOM
#undef SETUP_DOM

	return;
}

/* ############################################################## */

#ifdef __KERNEL__
#include "net/lnet/ut/linux_kernel/klnet_ut.c"
#endif

static int test_lnet_init(void)
{
	ut_save_subs();
	return c2_net_xprt_init(&c2_net_lnet_xprt);
}

static int test_lnet_fini(void)
{
	ut_restore_subs();
	c2_net_xprt_fini(&c2_net_lnet_xprt);
	return 0;
}

static void test_tm_initfini(void)
{
	static struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = LAMBDA(void,(const struct c2_net_tm_event *ev) {
				       }),
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_lnet_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	/* should be able to fini it immediately */
	c2_net_tm_fini(&d1tm1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_UNDEFINED);

	/* should be able to init it again */
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom1.nd_tms, &d1tm1.ntm_dom_linkage));

	/* fini */
	c2_net_tm_fini(&d1tm1);
	c2_net_domain_fini(&dom1);
}

static void test_tm_startstop(void)
{
	struct c2_net_domain *dom;
	struct c2_net_transfer_mc *tm;
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = ut_tm_ecb,
	};
	static struct c2_clink tmwait1;
	char * const *nidstrs;
	char epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char dyn_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char save_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	struct c2_bitmap procs;
	int i;

	C2_ALLOC_PTR(dom);
	C2_ALLOC_PTR(tm);
	C2_UT_ASSERT(dom != NULL && tm != NULL);
	tm->ntm_callbacks = &cbs1;
	ecb_reset();

	C2_UT_ASSERT(!c2_net_domain_init(dom, &c2_net_lnet_xprt));
	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(&nidstrs));
	C2_UT_ASSERT(nidstrs != NULL && nidstrs[0] != NULL);
	sprintf(epstr, "%s:%d:%d:101",
		nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);
	sprintf(dyn_epstr, "%s:%d:%d:*",
		nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL);
	c2_net_lnet_ifaces_put(&nidstrs);
	C2_UT_ASSERT(nidstrs == NULL);
	C2_UT_ASSERT(!c2_net_tm_init(tm, dom));

	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(tm, epstr));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_count == 1);
	C2_UT_ASSERT(ecb_evt == C2_NET_TEV_STATE_CHANGE);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
	C2_UT_ASSERT(ecb_status == 0);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);
	if (tm->ntm_state == C2_NET_TM_FAILED) {
		/* skip rest of this test, else C2_ASSERT will occur */
		c2_net_tm_fini(tm);
		c2_net_domain_fini(dom);
		c2_free(tm);
		c2_free(dom);
		C2_UT_FAIL("aborting test case, endpoint in-use?");
		return;
	}
	C2_UT_ASSERT(strcmp(tm->ntm_ep->nep_addr, epstr) == 0);

	ecb_reset();
	c2_clink_add(&tm->ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(tm, false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_count == 1);
	C2_UT_ASSERT(ecb_evt == C2_NET_TEV_STATE_CHANGE);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STOPPED);
	C2_UT_ASSERT(ecb_status == 0);
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(tm);
	c2_net_domain_fini(dom);
	c2_free(tm);
	c2_free(dom);

	/* test combination of dynamic endpoint, start with confine,
	 * and multiple domains and TMs
	 */
	C2_ALLOC_ARR(dom, STARTSTOP_DOM_NR);
	C2_ALLOC_ARR(tm, STARTSTOP_DOM_NR);
	C2_UT_ASSERT(dom != NULL && tm != NULL);

	for (i = 0; i < STARTSTOP_DOM_NR; ++i) {
		tm[i].ntm_callbacks = &cbs1;
		C2_UT_ASSERT(!c2_net_domain_init(&dom[i], &c2_net_lnet_xprt));
		C2_UT_ASSERT(!c2_net_tm_init(&tm[i], &dom[i]));
		C2_UT_ASSERT(c2_bitmap_init(&procs, 1) == 0);
		c2_bitmap_set(&procs, 0, true);
		C2_UT_ASSERT(c2_net_tm_confine(&tm[i], &procs) == 0);

		ecb_reset();
		c2_clink_add(&tm[i].ntm_chan, &tmwait1);
		C2_UT_ASSERT(!c2_net_tm_start(&tm[i], dyn_epstr));
		c2_chan_wait(&tmwait1);
		c2_clink_del(&tmwait1);
		C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
		C2_UT_ASSERT(tm[i].ntm_state == C2_NET_TM_STARTED);
		C2_UT_ASSERT(strcmp(tm[i].ntm_ep->nep_addr, dyn_epstr) != 0);
		if (i > 0)
			C2_UT_ASSERT(strcmp(tm[i].ntm_ep->nep_addr,
					    tm[i-1].ntm_ep->nep_addr) < 0);
	}

	/* subtest: dynamic TMID reuse using middle TM */
	strcpy(save_epstr, tm[1].ntm_ep->nep_addr);
	c2_clink_add(&tm[1].ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(&tm[1], false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(tm[1].ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(&tm[1]);
	C2_UT_ASSERT(!c2_net_tm_init(&tm[1], &dom[1]));

	c2_clink_add(&tm[1].ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&tm[1], dyn_epstr));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(ecb_tms == C2_NET_TM_STARTED);
	C2_UT_ASSERT(tm[1].ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(strcmp(tm[1].ntm_ep->nep_addr, save_epstr) == 0);

	for (i = 0; i < STARTSTOP_DOM_NR; ++i) {
		c2_clink_add(&tm[i].ntm_chan, &tmwait1);
		C2_UT_ASSERT(!c2_net_tm_stop(&tm[i], false));
		c2_chan_wait(&tmwait1);
		c2_clink_del(&tmwait1);
		C2_UT_ASSERT(ecb_tms == C2_NET_TM_STOPPED);
		C2_UT_ASSERT(tm[i].ntm_state == C2_NET_TM_STOPPED);
		c2_net_tm_fini(&tm[i]);
		c2_net_domain_fini(&dom[i]);
	}
	c2_free(tm);
	c2_free(dom);
}

/* test_msg_body */
enum {
	UT_MSG_OPS  = 4,
};

/* Sub to send messages from TM2 to TM1 until the latter's buffer
   is expected to fill.
   TM1 is primed with the specified number of receive buffers.
 */
static bool test_msg_send_loop(struct ut_data          *td,
			       uint32_t                 num_recv_bufs,
			       uint32_t                 recv_max_msgs,
			       struct c2_net_end_point *ep2,
			       c2_bcount_t              send_len_first,
			       c2_bcount_t              send_len_rest,
			       bool                     space_exhausted)
{
	struct c2_net_buffer *nb1;
	struct c2_net_buffer *nb2;
	c2_bcount_t msg_size;
	c2_bcount_t offset;
	c2_bcount_t space_left;
	unsigned bevs_left;
	unsigned char seed;
	int msg_num;
	bool rc = false;
	uint32_t rb_num;
	c2_bcount_t total_bytes_sent;

	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	ut_cbreset();
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, C2_NET_QT_MSG_RECV,
					  &td->qs, true));
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_MSG_SEND,
					  &td->qs, true));

	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	c2_clink_add(&TM2->ntm_chan, &td->tmwait2);

	/* UT sanity check: messages within buffer bounds */
	if (send_len_first > td->buf_size1 || send_len_first > td->buf_size2 ||
	    send_len_rest  > td->buf_size1 || send_len_rest  > td->buf_size2) {
		C2_UT_ASSERT(!(send_len_first > td->buf_size1 ||
			       send_len_first > td->buf_size2 ||
			       send_len_rest  > td->buf_size1 ||
			       send_len_rest  > td->buf_size2));
		goto aborted;
	}

	for (rb_num = 0; rb_num < num_recv_bufs && rb_num < UT_BUFS1; ++rb_num) {
		nb1 = &td->bufs1[rb_num];
		nb1->nb_min_receive_size = max64u(send_len_first, send_len_rest);
		nb1->nb_max_receive_msgs = recv_max_msgs;
		nb1->nb_qtype = C2_NET_QT_MSG_RECV;
		zUT(c2_net_buffer_add(nb1, TM1), aborted);
	}
	if (rb_num != num_recv_bufs) {
		C2_UT_ASSERT(rb_num == num_recv_bufs);
		goto aborted;
	}

#define RESET_RECV_COUNTERS()					\
	do {							\
		offset = 0;					\
		bevs_left = recv_max_msgs;			\
		/* 1 buf only as all recv space not used */	\
		space_left = td->buf_size1;			\
	} while (0)

	RESET_RECV_COUNTERS();
	rb_num = 1;

	total_bytes_sent = 0;
	msg_size = send_len_first;
	msg_num = 0;
	seed = 'a';
	nb2 = &td->bufs2[0];
	while (msg_size <= space_left && bevs_left > 0) {
		msg_num++;
		nb1 = &td->bufs1[rb_num-1];

		ut_net_buffer_sign(nb2, msg_size, seed);
		C2_UT_ASSERT(ut_net_buffer_authenticate(nb2, msg_size, 0, seed));
		nb2->nb_qtype = C2_NET_QT_MSG_SEND;
		nb2->nb_length = msg_size;
		nb2->nb_ep = ep2;

		NLXDBGPnl(td, 2, "\t%s S%d %lu bytes -> %s\n",
			  TM2->ntm_ep->nep_addr, msg_num,
			  (unsigned long) msg_size, ep2->nep_addr);
		cb_save_ep1 = true;
		zUT(c2_net_buffer_add(nb2, TM2), aborted);

		c2_chan_wait(&td->tmwait2);
		C2_UT_ASSERT(cb_called2 == msg_num);
		C2_UT_ASSERT(cb_qt2 == C2_NET_QT_MSG_SEND);
		C2_UT_ASSERT(cb_nb2 == nb2);
		C2_UT_ASSERT(!(cb_nb2->nb_flags & C2_NET_BUF_QUEUED));
		C2_UT_ASSERT(cb_status2 == 0);
		C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_MSG_SEND,
						  &td->qs, false));
		C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
		C2_UT_ASSERT(td->qs.nqs_num_s_events == msg_num);
		C2_UT_ASSERT(td->qs.nqs_num_adds == msg_num);
		C2_UT_ASSERT(td->qs.nqs_num_dels == 0);
		total_bytes_sent += msg_size;

		c2_chan_wait(&td->tmwait1);
		space_left -= cb_length1;
		NLXDBGPnl(td, 2,
			  "\t%s R%d %lu bytes <- %s off %lu left %lu/%d\n",
			  cb_nb1->nb_tm->ntm_ep->nep_addr, (unsigned) rb_num,
			  (unsigned long) cb_length1,
			  cb_ep1 != NULL ? cb_ep1->nep_addr : "<null>",
			  (unsigned long) offset,
			  (unsigned long) space_left, bevs_left);
		C2_UT_ASSERT(cb_called1 == msg_num);
		C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
		C2_UT_ASSERT(cb_nb1 == nb1);
		C2_UT_ASSERT(cb_status1 == 0);
		C2_UT_ASSERT(cb_offset1 == offset);
		offset += cb_length1;
		C2_UT_ASSERT(cb_length1 == msg_size);
		C2_UT_ASSERT(ut_net_buffer_authenticate(nb1, msg_size,
							cb_offset1, seed));
		C2_UT_ASSERT(cb_ep1 != NULL &&
			     strcmp(TM2->ntm_ep->nep_addr,cb_ep1->nep_addr)==0);
		C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, C2_NET_QT_MSG_RECV,
						  &td->qs, false));
		C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
		C2_UT_ASSERT(td->qs.nqs_num_s_events == msg_num);
		C2_UT_ASSERT(td->qs.nqs_num_adds == num_recv_bufs);
		C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

		msg_size = send_len_rest;
		++seed;
		--bevs_left;

		if (!(cb_nb1->nb_flags & C2_NET_BUF_QUEUED)) {
			/* next receive buffer */
			++rb_num;
			if (rb_num <= num_recv_bufs)
				RESET_RECV_COUNTERS();
			else
				break;
		}
	}
	if (space_exhausted) {
		C2_UT_ASSERT(msg_size > space_left);
	} else
		C2_UT_ASSERT(bevs_left == 0);

	C2_UT_ASSERT(total_bytes_sent >= (num_recv_bufs - 1) * td->buf_size1);

	C2_UT_ASSERT(rb_num == num_recv_bufs + 1);
	C2_UT_ASSERT(cb_nb1 == &td->bufs1[num_recv_bufs - 1]);
	for (rb_num = 0; rb_num < num_recv_bufs; ++rb_num) {
		nb1 = &td->bufs1[rb_num];
		C2_UT_ASSERT(!(nb1->nb_flags & C2_NET_BUF_QUEUED));
	}

	C2_UT_ASSERT(c2_atomic64_get(&cb_ep1->nep_ref.ref_cnt) == msg_num);
	while (msg_num-- > 0)
		zUT(c2_net_end_point_put(cb_ep1), aborted);
	cb_ep1 = NULL;

	rc = true;
 aborted:
	c2_clink_del(&td->tmwait2);
	c2_clink_del(&td->tmwait1);
	return rc;

#undef RESET_RECV_COUNTERS
}

static void test_msg_body(struct ut_data *td)
{
	struct c2_net_buffer    *nb1;
	struct c2_net_buffer    *nb2;
	struct c2_net_end_point *ep2;
	c2_bcount_t msg_size;
	unsigned char seed;

	nb1 = &td->bufs1[0];
	nb2 = &td->bufs2[0];

	/* TEST
	   Add a buffer for message receive then cancel it.
	 */
	nb1->nb_min_receive_size = UT_MSG_SIZE;
	nb1->nb_max_receive_msgs = 1;
	nb1->nb_qtype = C2_NET_QT_MSG_RECV;

	NLXDBGPnl(td, 1, "TEST: add/del on the receive queue\n");

	ut_cbreset();
	zUT(c2_net_buffer_add(nb1, TM1), done);

	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	c2_net_buffer_del(nb1, TM1);
	ut_chan_timedwait(&td->tmwait1, 10);
	c2_clink_del(&td->tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_status1 == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, C2_NET_QT_MSG_RECV,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 1);

	/* TEST
	   Add a buffer for receive in TM1 and send multiple messages from TM2.
	 */

	/* check that the sign/authenticate work */
	msg_size = UT_MSG_SIZE;
	seed = 'a';
	ut_net_buffer_sign(nb2, msg_size, seed);
	C2_UT_ASSERT(ut_net_buffer_authenticate(nb2, msg_size, 0, seed));
	C2_UT_ASSERT(!ut_net_buffer_authenticate(nb2, msg_size - 1, 0, seed));
	C2_UT_ASSERT(!ut_net_buffer_authenticate(nb2, msg_size, 0, seed + 1));

	/* sanity check */
	if (UT_MSG_SIZE >= td->buf_seg_size1) {
		C2_UT_ASSERT(UT_MSG_SIZE < td->buf_seg_size1);
		goto done;
	}

	/* get the destination TM address */
	zUT(c2_net_end_point_create(&ep2, TM2, TM1->ntm_ep->nep_addr), done);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 1);

	/* send until max receive messages is reached */
	NLXDBGPnl(td, 1, "TEST: send until max receive messages reached "
		  "(1 receive buffer)\n");
	C2_UT_ASSERT(test_msg_send_loop(td, 1, UT_MSG_OPS, ep2,
					UT_MSG_SIZE / 3, UT_MSG_SIZE, false));

	NLXDBGPnl(td, 1, "TEST: send until max receive messages reached "
		  "(2 receive buffers, > 1 seg)\n");
	C2_UT_ASSERT(test_msg_send_loop(td, 2, UT_MSG_OPS, ep2,
					td->buf_seg_size1 + UT_MSG_SIZE,
					UT_MSG_SIZE, false));

	/* send until space is exhausted */
	NLXDBGPnl(td, 1, "TEST: send until receive space exhausted "
		  "(1 receive buffer)\n");
	C2_UT_ASSERT(test_msg_send_loop(td, 1, UT_MSG_OPS * 2, ep2,
					UT_MSG_SIZE, UT_MSG_SIZE, true));

	NLXDBGPnl(td, 1, "TEST: send until receive space exhausted "
		  "(2 receive buffers, > 1 seg)\n");
	C2_UT_ASSERT(test_msg_send_loop(td, 2, UT_MSG_OPS * 2, ep2,
					td->buf_seg_size1 + UT_MSG_SIZE,
					td->buf_seg_size1 + UT_MSG_SIZE,
					true));

	/* TEST
	   Send a message when there is no receive buffer
	*/
	NLXDBGPnl(td, 1, "TEST: send / no receive buffer - no error expected\n");

	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	C2_UT_ASSERT(!(cb_nb1->nb_flags & C2_NET_BUF_QUEUED));

	ut_cbreset();
	c2_clink_add(&TM2->ntm_chan, &td->tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_MSG_SEND,
					  &td->qs, true));

	nb2->nb_qtype = C2_NET_QT_MSG_SEND;
	nb2->nb_length = UT_MSG_SIZE;
	nb2->nb_ep = ep2;
	NLXDBGPnl(td, 2, "\t%s S%d %lu bytes -> %s\n",
		  TM2->ntm_ep->nep_addr, 1,
		  (unsigned long) UT_MSG_SIZE, ep2->nep_addr);
	zUT(c2_net_buffer_add(nb2, TM2), aborted);

	c2_chan_wait(&td->tmwait2);
	C2_UT_ASSERT(cb_called2 == 1);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb2 == nb2);
	C2_UT_ASSERT(!(cb_nb2->nb_flags & C2_NET_BUF_QUEUED));
	C2_UT_ASSERT(cb_status2 != 1); /* send doesn't see the error */
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_MSG_SEND,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 1);
	zUT(c2_net_end_point_put(ep2), aborted);
	ep2 = NULL;

	/* TEST
	   Send a message to a non-existent TM address.
	*/
	NLXDBGPnl(td, 1, "TEST: send / non-existent TM - no error expected\n");
	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	{       /* create a destination end point */
		char epstr[C2_NET_LNET_XEP_ADDR_LEN];
		sprintf(epstr, "%s:%d:%d:1024",
			td->nidstrs[0], STARTSTOP_PID, STARTSTOP_PORTAL+1);
		zUT(c2_net_end_point_create(&ep2, TM2, epstr), aborted);
	}
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 1);

	ut_cbreset();

	nb2->nb_qtype = C2_NET_QT_MSG_SEND;
	nb2->nb_length = UT_MSG_SIZE;
	nb2->nb_ep = ep2;
	NLXDBGPnl(td, 2, "\t%s S%d %lu bytes -> %s\n",
		  TM2->ntm_ep->nep_addr, 1,
		  (unsigned long) UT_MSG_SIZE, ep2->nep_addr);
	zUT(c2_net_buffer_add(nb2, TM2), aborted);

	c2_chan_wait(&td->tmwait2);
	C2_UT_ASSERT(cb_called2 == 1);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb2 == nb2);
	C2_UT_ASSERT(!(cb_nb2->nb_flags & C2_NET_BUF_QUEUED));
	C2_UT_ASSERT(cb_status2 == 0); /* send doesn't see the error */
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_MSG_SEND,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 1);
	zUT(c2_net_end_point_put(ep2), aborted);
	ep2 = NULL;

 aborted:
	c2_clink_del(&td->tmwait2);
 done:
	return;
}

static void test_msg(void) {
	ut_test_framework(&test_msg_body, ut_verbose);
}

const struct c2_test_suite c2_net_lnet_ut = {
        .ts_name = "net-lnet",
        .ts_init = test_lnet_init,
        .ts_fini = test_lnet_fini,
        .ts_tests = {
#ifdef __KERNEL__
		{ "net_lnet_buf_shape (K)", ktest_buf_shape },
		{ "net_lnet_buf_reg (K)",   ktest_buf_reg },
		{ "net_lnet_ep_addr (K)",   ktest_core_ep_addr },
		{ "net_lnet_enc_dec (K)",   ktest_enc_dec },
		{ "net_lnet_msg (K)",       ktest_msg },
#endif
		{ "net_lnet_tm_initfini",   test_tm_initfini },
		{ "net_lnet_tm_startstop",  test_tm_startstop },
		{ "net_lnet_msg",           test_msg },
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_lnet_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
