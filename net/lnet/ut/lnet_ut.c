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
#include "lib/thread.h" /* c2_thread_self, c2_thread_handle_eq */
#include "lib/ut.h"

#include "db/db.h" /* c2_dbenv, c2_table */

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
	STARTSTOP_STAT_SECS = 3,
	STARTSTOP_STAT_PER_PERIOD = 1,
	STARTSTOP_STAT_BUF_NR = 4,
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
#define zvUT(x,expRC,label)					\
do {								\
	int rc = (x);						\
	int erc = (expRC);					\
	if (rc != erc) {					\
		snprintf(ut_line_buf, sizeof(ut_line_buf),	\
			 "%s %d: %s: %d != %d",			\
			 __FILE__,__LINE__, #x, rc, erc);	\
		C2_UT_FAIL(ut_line_buf);			\
		goto label;					\
	}							\
} while (0)
#define zUT(x,label) zvUT(x,0,label)

enum {
	UT_BUFS1     = 2,
	UT_BUFSEGS1  = 4,
	UT_BUFS2     = 1,
	UT_BUFSEGS2  = 2,
	UT_MSG_SIZE  = 2048,
	UT_BULK_SIZE = 2 * 4096,
	UT_PAGE_SHIFT = 12
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
	char * const                  *nidstrs1;
	char * const                  *nidstrs2;
	struct nlx_core_buf_desc       cbd1;
	struct nlx_core_buf_desc       cbd2;
};

#ifdef __KERNEL__
C2_BASSERT(UT_PAGE_SHIFT == PAGE_SHIFT);
#endif

#define DOM1 (&td->dom1)
#define DOM2 (&td->dom2)
#define TM1  (&td->tm1)
#define TM2  (&td->tm2)
#define CBD1 (&td->cbd1)
#define CBD2 (&td->cbd2)

typedef void (*ut_test_fw_body_t)(struct ut_data *td);
typedef void (*ut_test_fw_prestart_cb_t)(struct ut_data *td, int which);

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
			len = c2_tlist_length(&c2_net_tm_tl, &tm->ntm_q[qt]);
			/* best effort; can't say if this will always work */
			for (i = 0; i < len; ++i) {
				nb = c2_net_tm_tlist_head(&tm->ntm_q[qt]);
				c2_clink_add(&tm->ntm_chan, &cl);
				NLXDBGP(td, 2,
					"Cleanup/DEL D:%p T:%p Q:%d B:%p\n",
					dom, tm, qt, nb);
				c2_net_buffer_del(nb, tm);
				if (tm->ntm_bev_auto_deliver)
					ut_chan_timedwait(&cl, 10);
				else {
					int j;
					c2_net_buffer_event_notify(tm,
								 &tm->ntm_chan);
					for (j = 0; j < 10; ++j) {
						ut_chan_timedwait(&cl, 1);
						c2_net_buffer_event_deliver_all
							(tm);
					}
				}
				c2_clink_del(&cl);
			}
			len = c2_tlist_length(&c2_net_tm_tl, &tm->ntm_q[qt]);
			if (len != 0) {
				NLXDBGP(td, 0,
					"Cleanup D:%p T:%p Q:%d B failed\n",
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
				while (c2_atomic64_get(&ep->nep_ref.ref_cnt) >=
				       1) {
					NLXDBGP(td, 2,
						"Cleanup/PUT D:%p T:%p E:%p\n",
						dom, tm, ep);
					c2_net_end_point_put(ep);
				}
			}
		}
		if (c2_list_length(&tm->ntm_end_points) > 1)
			NLXDBGP(td,0,"Cleanup D:%p T:%p E failed\n", dom, tm);
	}
}

#ifdef NLX_DEBUG
static void ut_describe_buf(const struct c2_net_buffer *nb)
{
	struct nlx_xo_buffer       *bp = nb->nb_xprt_private;
	struct nlx_core_buffer  *lcbuf = &bp->xb_core;
#ifdef __KERNEL__
	struct nlx_kcore_buffer   *kcb = lcbuf->cb_kpvt;

	NLXP("\txo:%p lcbuf:%p kcb:%p\n",
	     (void *) bp, (void *) lcbuf, (void *) kcb);
#endif
}

static void ut_describe_tm(const struct c2_net_transfer_mc *tm)
{
	struct nlx_xo_transfer_mc      *tp = tm->ntm_xprt_private;
	struct nlx_core_transfer_mc  *lctm = &tp->xtm_core;
#ifdef __KERNEL__
	struct nlx_kcore_transfer_mc *kctm = lctm->ctm_kpvt;

	NLXP("\txo:%p lctm:%p kctm:%p\n",
	     (void *) tp, (void *) lctm, (void *) kctm);
	if (kctm != NULL) {
		nlx_kprint_lnet_handle("\tEQ1", kctm->ktm_eqh);
	}
#endif
}
#endif /* NLX_DEBUG */

static void ut_test_framework(ut_test_fw_body_t body,
			      ut_test_fw_prestart_cb_t ps_cb,
			      int dbg)
{
	struct ut_data *td;
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

#define SETUP_DOM(which)						\
do {									\
        struct c2_net_domain *dom = &td->dom ## which;			\
	struct c2_net_transfer_mc *tm = &td->tm ## which;		\
	char * const **nidstrs = &td->nidstrs ## which;			\
        C2_UT_ASSERT(!c2_net_domain_init(dom, &c2_net_lnet_xprt));	\
	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(dom, nidstrs));		\
	C2_UT_ASSERT(*nidstrs != NULL && **nidstrs != NULL);		\
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
			rc = c2_bufvec_alloc_aligned(&nb->nb_buffer,	\
						     UT_BUFSEGS ## which, \
						     max_seg_size,	\
						     UT_PAGE_SHIFT);	\
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
			NLXDBGPnl(td, 2, "[%d] D:%p T:%p B:%p [%u,%d]=%lu\n", \
				  which, dom, tm, nb, (unsigned) max_seg_size, \
				  UT_BUFSEGS ## which,			\
				  (unsigned long) td->buf_size ## which); \
			NLXDBGnl(td, 2, ut_describe_buf(nb));           \
		}							\
									\
		C2_UT_ASSERT(!c2_net_tm_init(tm, dom));			\
		if (ps_cb != NULL)                                      \
			(*ps_cb)(td, which);                            \
									\
		sprintf(epstr, "%s:%d:%d:*",				\
			**nidstrs, STARTSTOP_PID, STARTSTOP_PORTAL);	\
		c2_clink_add(&tm->ntm_chan, &td->tmwait ## which);	\
		C2_UT_ASSERT(!c2_net_tm_start(tm, epstr));		\
		c2_chan_wait(&td->tmwait ## which);			\
		c2_clink_del(&td->tmwait ## which);			\
		C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STARTED);	\
		if (tm->ntm_state == C2_NET_TM_FAILED) {		\
			C2_UT_FAIL("aborting: tm" #which " startup failed"); \
			goto fini ## which;				\
		}							\
		NLXDBGPnl(td, 2, "[%d] D:%p T:%p E:%s\n", which, dom, tm, \
			  tm->ntm_ep->nep_addr);			\
		NLXDBGnl(td, 2, ut_describe_tm(tm));			\
	}								\
} while (0)

#define TEARDOWN_DOM(which)						\
do {									\
        struct c2_net_domain *dom;					\
	struct c2_net_transfer_mc *tm = &td->tm ## which;		\
	c2_clink_add(&tm->ntm_chan, &td->tmwait ## which);		\
	C2_UT_ASSERT(!c2_net_tm_stop(tm, false));			\
	c2_chan_wait(&td->tmwait ## which);				\
	c2_clink_del(&td->tmwait ## which);				\
	C2_UT_ASSERT(tm->ntm_state == C2_NET_TM_STOPPED);		\
 fini ## which:								\
	tm = &td->tm ## which;						\
	c2_net_tm_fini(tm);						\
 dereg ## which:							\
	dom = &td->dom ## which;					\
	for (i = 0; i < UT_BUFS ## which; ++i) {			\
		struct c2_net_buffer      *nb;				\
		nb = &td->bufs ## which [i];				\
		if (nb->nb_buffer.ov_vec.v_nr == 0)			\
			continue;					\
		c2_net_buffer_deregister(nb, dom);			\
		c2_bufvec_free_aligned(&nb->nb_buffer, UT_PAGE_SHIFT);	\
	}								\
	if (td->nidstrs ## which != NULL)				\
		c2_net_lnet_ifaces_put(dom, &td->nidstrs ## which);	\
	C2_UT_ASSERT(td->nidstrs ## which == NULL);			\
	c2_net_domain_fini(dom);					\
} while (0)

	SETUP_DOM(1);
	SETUP_DOM(2);

	(*body)(td);

	ut_test_framework_dom_cleanup(td, DOM2);
	ut_test_framework_dom_cleanup(td, DOM1);

	TEARDOWN_DOM(2);
	TEARDOWN_DOM(1);

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
	int rc;

	rc = c2_net_xprt_init(&c2_net_lnet_xprt);
#ifdef __KERNEL__
	if (rc != 0)
		return rc;
	rc = ktest_lnet_init();
#else
	{
		struct stat st;

		/* fail entire suite if LNet device is not present */
		rc = stat("/dev/" C2_LNET_DEV, &st);

		if (rc != 0)
			rc = -errno;
		else if (!S_ISCHR(st.st_mode))
			rc = -ENODEV;
	}
#endif
	if (rc != 0)
		c2_net_xprt_fini(&c2_net_lnet_xprt);
	else
		ut_save_subs();
	return rc;
}

static int test_lnet_fini(void)
{
	ut_restore_subs();
#ifdef __KERNEL__
	ktest_lnet_fini();
#endif
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
	static char *n1t0 = "10.72.49.14@o2ib0:12345:31:0";
	static char *n1t1 = "10.72.49.14@o2ib0:12345:31:1";
	static char *n1ts = "10.72.49.14@o2ib0:12345:31:*";
	static char *n2t0 = "192.168.96.128@tcp1:12345:31:0";
	static char *n2t1 = "192.168.96.128@tcp1:12345:31:1";
	static char *n2ts = "192.168.96.128@tcp1:12345:31:*";

	/* TEST
	   Network name comparsion.
	*/
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t0, n1t0) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t1, n1t1) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1ts, n1ts) == 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t0, n1t1) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t0, n1ts) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t1, n1ts) == 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t0, n2t0) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t1, n2t1) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2ts, n2ts) == 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t0, n2t1) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t0, n2ts) == 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t1, n2ts) == 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t0, n2t0) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t1, n2t0) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1ts, n2t0) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t0, n1t0) > 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t0, n1t1) > 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t0, n1ts) > 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t0, n2t1) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t1, n2t1) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1ts, n2t1) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t1, n1t0) > 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t1, n1t1) > 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2t1, n1ts) > 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t0, n2ts) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1t1, n2ts) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1ts, n2ts) < 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2ts, n1t0) > 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2ts, n1t1) > 0);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n2ts, n1ts) > 0);

	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp(n1ts, "foo") == -1);
	C2_UT_ASSERT(c2_net_lnet_ep_addr_net_cmp("foo", n1ts) == -1);

	/* TEST
	   Domain setup.
	*/
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

static struct c2_table mock_table;
static struct c2_dbenv mock_dbenv;
static struct c2_semaphore mock_sem;
static int lnet_stat_ev_count;

int mock_db_add(struct c2_addb_dp *dp, struct c2_dbenv *dbenv,
		struct c2_table *db)
{
	if (dp->ad_ev == &nlx_qstat) {
		const struct c2_addb_ev_ops *ops = dp->ad_ev->ae_ops;
		struct nlx_qstat_body *body;
		struct c2_addb_record rec;
		struct nlx_addb_dp *ndp;

		ndp = container_of(dp, struct nlx_addb_dp, ad_dp);
		C2_UT_ASSERT(dp->ad_name != NULL);
		C2_UT_ASSERT(strncmp(dp->ad_name, "nlx_tm_stats:", 13) == 0);
		C2_UT_ASSERT(ndp->ad_qs != NULL);

		/* test the pack and getsize ops */
		C2_SET0(&rec);
		C2_UT_ASSERT(ops == &nlx_addb_qstats);
		rec.ar_data.cmb_count = ops->aeo_getsize(dp);
		C2_UT_ASSERT(rec.ar_data.cmb_count != 0);
		rec.ar_data.cmb_value = c2_alloc(rec.ar_data.cmb_count);
		C2_ASSERT(rec.ar_data.cmb_value != NULL);
		C2_UT_ASSERT(ops->aeo_pack(dp, &rec) == 0);
		body = (struct nlx_qstat_body *)rec.ar_data.cmb_value;
		C2_UT_ASSERT(body->sb_qid == dp->ad_rc);
		C2_UT_ASSERT(body->sb_qstats.nqs_num_adds ==
			     STARTSTOP_STAT_BUF_NR);
		C2_UT_ASSERT(strcmp(dp->ad_name, body->sb_name) == 0);
		c2_free(rec.ar_data.cmb_value);

		lnet_stat_ev_count++;
		c2_semaphore_up(&mock_sem);
	}
	return 0;
}

static void test_tm_startstop(void)
{
	struct c2_net_domain *dom;
	struct c2_net_transfer_mc *tm;
	const struct c2_net_qstats fake_stats = {
		.nqs_num_adds = STARTSTOP_STAT_BUF_NR,
		.nqs_num_dels = STARTSTOP_STAT_BUF_NR,
		.nqs_num_s_events = STARTSTOP_STAT_BUF_NR,
	};
	const struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = ut_tm_ecb,
	};
	static struct c2_clink tmwait1;
	char * const *nidstrs;
	const char *nid_to_use;
	char epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char dyn_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	char save_epstr[C2_NET_LNET_XEP_ADDR_LEN];
	struct c2_bitmap procs;
	int rc;
	int i;

	C2_ALLOC_PTR(dom);
	C2_ALLOC_PTR(tm);
	C2_UT_ASSERT(dom != NULL && tm != NULL);
	tm->ntm_callbacks = &cbs1;
	ecb_reset();

	/* mock addb db store */
	c2_semaphore_init(&mock_sem, 0);
	rc = c2_addb_choose_store_media(C2_ADDB_REC_STORE_DB,
					mock_db_add, &mock_table, &mock_dbenv);
	C2_UT_ASSERT(rc == 0);

	C2_UT_ASSERT(!c2_net_domain_init(dom, &c2_net_lnet_xprt));
	C2_UT_ASSERT(!c2_net_lnet_ifaces_get(dom, &nidstrs));
	C2_UT_ASSERT(nidstrs != NULL && nidstrs[0] != NULL);
	nid_to_use = nidstrs[0];
	for (i = 0; nidstrs[i] != NULL; ++i) {
		if (strstr(nidstrs[i], "@lo") != NULL)
			continue;
		nid_to_use = nidstrs[i];
		break;
	}
	sprintf(epstr, "%s:%d:%d:101",
		nid_to_use, STARTSTOP_PID, STARTSTOP_PORTAL);
	sprintf(dyn_epstr, "%s:%d:%d:*",
		nid_to_use, STARTSTOP_PID, STARTSTOP_PORTAL);
	c2_net_lnet_ifaces_put(dom, &nidstrs);
	C2_UT_ASSERT(nidstrs == NULL);
	C2_UT_ASSERT(!c2_net_tm_init(tm, dom));
	c2_net_lnet_tm_stat_interval_set(tm, STARTSTOP_STAT_SECS);

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
		rc = c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);
		c2_semaphore_fini(&mock_sem);
		C2_UT_ASSERT(rc == 0);
		C2_UT_FAIL("aborting test case, endpoint in-use?");
		return;
	}
	C2_UT_ASSERT(strcmp(tm->ntm_ep->nep_addr, epstr) == 0);

	/* also test periodic statistics */
	C2_UT_ASSERT(lnet_stat_ev_count == 0);
	tm->ntm_qstats[C2_NET_QT_MSG_RECV] = fake_stats;
	c2_semaphore_down(&mock_sem);
	C2_UT_ASSERT(lnet_stat_ev_count == STARTSTOP_STAT_PER_PERIOD);
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
	C2_UT_ASSERT(lnet_stat_ev_count == 2 * STARTSTOP_STAT_PER_PERIOD);
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
		c2_bitmap_fini(&procs);

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
	rc = c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);
	c2_semaphore_fini(&mock_sem);
	C2_UT_ASSERT(rc == 0);
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

	for (rb_num = 0; rb_num < num_recv_bufs && rb_num < UT_BUFS1; ++rb_num){
		nb1 = &td->bufs1[rb_num];
		nb1->nb_min_receive_size = max64u(send_len_first,send_len_rest);
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
		C2_UT_ASSERT(ut_net_buffer_authenticate(nb2, msg_size, 0,seed));
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
	NLXDBGPnl(td, 1, "TEST: send/no receive buffer - no error expected\n");

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
			td->nidstrs2[0], STARTSTOP_PID, STARTSTOP_PORTAL+1);
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

static void test_msg(void)
{
	ut_test_framework(&test_msg_body, NULL, ut_verbose);
}

static void test_buf_desc_body(struct ut_data *td)
{
	struct nlx_xo_transfer_mc      *tp1 = TM1->ntm_xprt_private;
	struct nlx_core_transfer_mc  *lctm1 = &tp1->xtm_core;
	struct c2_net_buffer           *nb1 = &td->bufs1[0];
	struct nlx_xo_buffer           *bp1 = nb1->nb_xprt_private;
	struct nlx_core_buffer      *lcbuf1 = &bp1->xb_core;
	struct nlx_xo_transfer_mc      *tp2 = TM2->ntm_xprt_private;
	struct nlx_core_transfer_mc  *lctm2 = &tp2->xtm_core;
	struct c2_net_buffer           *nb2 = &td->bufs2[0];
	struct nlx_xo_buffer           *bp2 = nb2->nb_xprt_private;
	struct nlx_core_buffer      *lcbuf2 = &bp2->xb_core;
	uint32_t tmid;
	uint64_t counter;
	int rc;

	/* TEST
	   Check that match bit decode reverses encode.
	*/
	NLXDBGPnl(td, 1, "TEST: match bit encoding\n");
#define TEST_MATCH_BIT_ENCODE(_t, _c)					\
	nlx_core_match_bits_decode(nlx_core_match_bits_encode((_t),(_c)), \
				   &tmid, &counter);			\
	C2_UT_ASSERT(tmid == (_t));					\
	C2_UT_ASSERT(counter == (_c))

	TEST_MATCH_BIT_ENCODE(0, 0);
	TEST_MATCH_BIT_ENCODE(C2_NET_LNET_TMID_MAX, 0);
	TEST_MATCH_BIT_ENCODE(C2_NET_LNET_TMID_MAX, C2_NET_LNET_BUFFER_ID_MIN);
	TEST_MATCH_BIT_ENCODE(C2_NET_LNET_TMID_MAX, C2_NET_LNET_BUFFER_ID_MAX);

#undef TEST_MATCH_BIT_ENCODE


	/* TEST
	   Check that conversion to and from the external opaque descriptor
	   form preserve the internal descriptor.
	*/
	NLXDBGPnl(td, 1, "TEST: buffer descriptor S(export, import)\n");
	memset(CBD1, 0xf7, sizeof *CBD1); /* arbitrary pattern */
	memset(CBD2, 0xab, sizeof *CBD2); /* different arbitrary pattern */
	C2_UT_ASSERT(sizeof *CBD1 == sizeof *CBD2);
	C2_UT_ASSERT(memcmp(CBD1, CBD2, sizeof *CBD1) != 0);

	C2_UT_ASSERT(!nlx_xo__nbd_allocate(TM1, CBD1, &nb1->nb_desc));
	C2_UT_ASSERT(nb1->nb_desc.nbd_len == sizeof *CBD1);
	C2_UT_ASSERT(!nlx_xo__nbd_recover(TM1, &nb1->nb_desc, CBD2));
	C2_UT_ASSERT(memcmp(CBD1, CBD2, sizeof *CBD1) == 0);

	/* TEST
	   Check that an invalid descriptor length will fail to convert to
	   internal form, and will not modify the supplied internal desc.
	*/
	NLXDBGPnl(td, 1, "TEST: buffer descriptor F(import invalid size)\n");
	nb1->nb_desc.nbd_len++;
	memset(CBD1, 0x35, sizeof *CBD1); /* aribtrary pattern, not the desc */
	memcpy(CBD2, CBD1, sizeof *CBD2); /* same pattern */
	C2_UT_ASSERT(nlx_xo__nbd_recover(TM1, &nb1->nb_desc, CBD2) == -EINVAL);
	C2_UT_ASSERT(memcmp(CBD1, CBD2, sizeof *CBD1) == 0); /* unchanged */

	c2_net_desc_free(&nb1->nb_desc);

#define VALIDATE_MATCH_BITS(mb, s_lctm)				\
	nlx_core_match_bits_decode(mb, &tmid, &counter);	\
	C2_UT_ASSERT(tmid == s_lctm->ctm_addr.cepa_tmid);	\
	C2_UT_ASSERT(counter == s_lctm->ctm_mb_counter - 1)

	/* TEST
	   Passive send buffer descriptor.
	   Ensure that match bits get properly set, and that the
	   descriptor properly encodes buffer and end point
	   data, and that the data can be properly decoded.
	   Test with an exact size receive buffer and a larger one.
	 */
	NLXDBGPnl(td, 1, "TEST: encode buffer descriptor S(PS1)\n");
	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	C2_UT_ASSERT(lctm2->ctm_mb_counter == C2_NET_LNET_BUFFER_ID_MIN);
	lcbuf1->cb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	lcbuf1->cb_length = td->buf_size1; /* Arbitrary */
	C2_SET0(&lcbuf1->cb_addr);
	c2_mutex_lock(&TM1->ntm_mutex);
	nlx_core_buf_desc_encode(lctm1, lcbuf1, CBD1);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(lctm1->ctm_mb_counter == C2_NET_LNET_BUFFER_ID_MIN + 1);
	VALIDATE_MATCH_BITS(lcbuf1->cb_match_bits, lctm1);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor S(AR2 == PS1)\n");
	C2_SET0(&lcbuf2->cb_addr); /* clear target areas of buf2 */
	C2_UT_ASSERT(!nlx_core_ep_eq(&lctm1->ctm_addr, &lcbuf2->cb_addr));
	lcbuf2->cb_match_bits = 0;
	C2_UT_ASSERT(lcbuf2->cb_match_bits != lcbuf1->cb_match_bits);
	/* decode - buf2 on right queue and have adequate size */
	lcbuf2->cb_length = lcbuf1->cb_length; /* same size as send buffer */
	lcbuf2->cb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	c2_mutex_lock(&TM2->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm2, lcbuf2, CBD1);
	c2_mutex_unlock(&TM2->ntm_mutex);
	C2_UT_ASSERT(rc == 0);
	/* buf2 target address set to TM1, and size/bits set to buf1 */
	C2_UT_ASSERT(nlx_core_ep_eq(&lctm1->ctm_addr, &lcbuf2->cb_addr));
	C2_UT_ASSERT(lcbuf2->cb_length == lcbuf1->cb_length);
	C2_UT_ASSERT(lcbuf2->cb_match_bits == lcbuf1->cb_match_bits);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor F(corrupt)\n");
	/* decode - everything correct, like above, but corrupt descriptor */
	counter = CBD1->cbd_data[2]; /* save some location */
	CBD1->cbd_data[2]++; /* modify, arbitrarily different */
	c2_mutex_lock(&TM2->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm2, lcbuf2, CBD1);
	c2_mutex_unlock(&TM2->ntm_mutex);
	C2_UT_ASSERT(rc == -EINVAL);
	CBD1->cbd_data[2] = counter; /* restore */

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor S(AR2 > PS1)\n");
	C2_SET0(&lcbuf2->cb_addr); /* clear target areas of buf2 */
	C2_UT_ASSERT(!nlx_core_ep_eq(&lctm1->ctm_addr, &lcbuf2->cb_addr));
	lcbuf2->cb_match_bits = 0;
	C2_UT_ASSERT(lcbuf2->cb_match_bits != lcbuf1->cb_match_bits);
	/* decode - buf2 on right queue and > send buffer size */
	lcbuf2->cb_length = lcbuf1->cb_length + 1;
	lcbuf2->cb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	c2_mutex_lock(&TM2->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm2, lcbuf2, CBD1);
	c2_mutex_unlock(&TM2->ntm_mutex);
	C2_UT_ASSERT(rc == 0);
	/* buf2 target address set to TM1, and size/bits set to buf1 */
	C2_UT_ASSERT(nlx_core_ep_eq(&lctm1->ctm_addr, &lcbuf2->cb_addr));
	C2_UT_ASSERT(lcbuf2->cb_length == lcbuf1->cb_length); /* passive size */
	C2_UT_ASSERT(lcbuf2->cb_match_bits == lcbuf1->cb_match_bits);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor F(AR2 < PS1)\n");
	lcbuf2->cb_length = lcbuf1->cb_length - 1; /* < send buffer size */
	lcbuf2->cb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	c2_mutex_lock(&TM2->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm2, lcbuf2, CBD1);
	c2_mutex_unlock(&TM2->ntm_mutex);
	C2_UT_ASSERT(rc == -EFBIG);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor F(AS2 == PS1)\n");
	lcbuf1->cb_length = lcbuf1->cb_length; /* same size as send buffer */
	lcbuf1->cb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	c2_mutex_lock(&TM1->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm1, lcbuf1, CBD1);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(rc == -EPERM);

	/* TEST
	   Passive receive buffer descriptor.
	   Ensure that match bits get properly set, and that the
	   descriptor properly encodes buffer and end point
	   data, and that the data can be properly decoded.
	   Test with an exact size send buffer and a smaller one.
	 */
	C2_UT_ASSERT(td->buf_size1 >= td->buf_size2); /* sanity check */
	NLXDBGPnl(td, 1, "TEST: encode buffer descriptor S(PR2)\n");

	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	C2_UT_ASSERT(lctm2->ctm_mb_counter == C2_NET_LNET_BUFFER_ID_MIN);
	lcbuf2->cb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	lcbuf2->cb_length = td->buf_size2; /* Arbitrary */
	c2_mutex_lock(&TM2->ntm_mutex);
	nlx_core_buf_desc_encode(lctm2, lcbuf2, CBD2);
	c2_mutex_unlock(&TM2->ntm_mutex);
	C2_UT_ASSERT(lctm2->ctm_mb_counter == C2_NET_LNET_BUFFER_ID_MIN + 1);
	VALIDATE_MATCH_BITS(lcbuf2->cb_match_bits, lctm2);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor S(AS1 == PR2)\n");
	C2_SET0(&lcbuf1->cb_addr); /* clear target areas of buf1 */
	C2_UT_ASSERT(!nlx_core_ep_eq(&lctm2->ctm_addr, &lcbuf1->cb_addr));
	lcbuf1->cb_match_bits = 0;
	C2_UT_ASSERT(lcbuf1->cb_match_bits != lcbuf2->cb_match_bits);
	/* decode - buf1 on right queue and have adequate size */
	lcbuf1->cb_length = lcbuf2->cb_length; /* same size as receive buffer */
	lcbuf1->cb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	c2_mutex_lock(&TM1->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm1, lcbuf1, CBD2);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(rc == 0);
	/* buf1 target address set to TM2, and size/bits set to buf2 */
	C2_UT_ASSERT(nlx_core_ep_eq(&lctm2->ctm_addr, &lcbuf1->cb_addr));
	C2_UT_ASSERT(lcbuf1->cb_length == lcbuf2->cb_length);
	C2_UT_ASSERT(lcbuf1->cb_match_bits == lcbuf2->cb_match_bits);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor S(AS1 < PR2)\n");
	C2_SET0(&lcbuf1->cb_addr); /* clear target areas of buf1 */
	C2_UT_ASSERT(!nlx_core_ep_eq(&lctm2->ctm_addr, &lcbuf1->cb_addr));
	lcbuf1->cb_match_bits = 0;
	C2_UT_ASSERT(lcbuf1->cb_match_bits != lcbuf2->cb_match_bits);
	/* decode - buf1 on right queue and < receive buffer size */
	lcbuf1->cb_length = lcbuf2->cb_length - 1;
	lcbuf1->cb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	c2_mutex_lock(&TM1->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm1, lcbuf1, CBD2);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(rc == 0);
	/* buf1 target address set to TM2, and size/bits set to buf2 */
	C2_UT_ASSERT(nlx_core_ep_eq(&lctm2->ctm_addr, &lcbuf1->cb_addr));
	C2_UT_ASSERT(lcbuf1->cb_length == lcbuf2->cb_length - 1);/* active sz */
	C2_UT_ASSERT(lcbuf1->cb_match_bits == lcbuf2->cb_match_bits);

#undef VALIDATE_MATCH_BITS

	/* TEST
	   Failure tests for this setup: invalid usage, wrong sizes
	 */
	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor F(AS1 > PR2)\n");
	lcbuf1->cb_length = lcbuf2->cb_length + 1; /* > receive buffer size */
	lcbuf1->cb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	c2_mutex_lock(&TM1->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm1, lcbuf1, CBD2);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(rc == -EFBIG);

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor F(AR1 == PR2)\n");
	lcbuf1->cb_length = lcbuf2->cb_length; /* same size as receive buffer */
	lcbuf1->cb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	c2_mutex_lock(&TM1->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm1, lcbuf1, CBD2);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(rc == -EPERM);

	/* TEST
	   Invalid descriptor cases
	*/
	*CBD1 = *CBD2;

	NLXDBGPnl(td, 1, "TEST: decode buffer descriptor F(corrupt)\n");
	CBD2->cbd_match_bits++; /* invalidates checksum */
	lcbuf1->cb_length = lcbuf2->cb_length; /* same size as receive buffer */
	lcbuf1->cb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	c2_mutex_lock(&TM1->ntm_mutex);
	rc = nlx_core_buf_desc_decode(lctm1, lcbuf1, CBD2);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(rc == -EINVAL);

	/* TEST
	   Check that the match bit counter wraps.
	*/
	NLXDBGPnl(td, 1, "TEST: match bit counter wraps\n");
	lctm1->ctm_mb_counter = C2_NET_LNET_BUFFER_ID_MAX - 1;
	lcbuf1->cb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	lcbuf1->cb_length = td->buf_size1; /* Arbitrary */
	c2_mutex_lock(&TM1->ntm_mutex);
	nlx_core_buf_desc_encode(lctm1, lcbuf1, CBD1);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(lctm1->ctm_mb_counter == C2_NET_LNET_BUFFER_ID_MAX);
	nlx_core_match_bits_decode(lcbuf1->cb_match_bits, &tmid, &counter);
	C2_UT_ASSERT(tmid == lctm1->ctm_addr.cepa_tmid);
	C2_UT_ASSERT(counter == C2_NET_LNET_BUFFER_ID_MAX - 1);

	lcbuf1->cb_qtype = C2_NET_QT_PASSIVE_BULK_RECV; /* qt doesn't matter */
	c2_mutex_lock(&TM1->ntm_mutex);
	nlx_core_buf_desc_encode(lctm1, lcbuf1, CBD1);
	c2_mutex_unlock(&TM1->ntm_mutex);
	C2_UT_ASSERT(lctm1->ctm_mb_counter == C2_NET_LNET_BUFFER_ID_MIN);
	nlx_core_match_bits_decode(lcbuf1->cb_match_bits, &tmid, &counter);
	C2_UT_ASSERT(tmid == lctm1->ctm_addr.cepa_tmid);
	C2_UT_ASSERT(counter == C2_NET_LNET_BUFFER_ID_MAX);

	return;
}

static void test_buf_desc(void)
{
	ut_test_framework(&test_buf_desc_body, NULL, ut_verbose);
}

static int test_bulk_passive_send(struct ut_data *td)
{
	struct c2_net_buffer *nb1  = &td->bufs1[0]; /* passive */
	struct c2_net_buffer *nb2  = &td->bufs2[0]; /* active */
	struct c2_net_buffer *nb2s = NULL;
	unsigned char seed = 's';
	c2_bcount_t pBytes = UT_BULK_SIZE;
	int rc = -1;

	ut_cbreset();

	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	c2_clink_add(&TM2->ntm_chan, &td->tmwait2);

	/* stage passive send buffer */
	C2_UT_ASSERT(td->buf_size1 >= pBytes);
	if (td->buf_size1 < pBytes)
		goto failed;
	nb1->nb_length = pBytes;
	ut_net_buffer_sign(nb1, nb1->nb_length, seed);
	nb1->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	zUT(c2_net_buffer_add(nb1, TM1), failed);
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);
	C2_UT_ASSERT(nb1->nb_desc.nbd_len != 0);
	C2_UT_ASSERT(nb1->nb_desc.nbd_data != NULL);

	zUT(c2_net_desc_copy(&nb1->nb_desc, &nb2->nb_desc), failed);

	/* ensure that an active send fails */
	NLXDBGPnl(td, 1, "TEST: bulk transfer F(PS !~ AS)\n");
	nb2->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	nb2->nb_length = pBytes;
	zvUT(c2_net_buffer_add(nb2, TM2), -EPERM, failed);

	/* try to receive into a smaller buffer */
	NLXDBGPnl(td, 1, "TEST: bulk transfer F(PS >  AR)\n");
	C2_UT_ASSERT(pBytes > td->buf_seg_size2); /* sanity */
	zUT((C2_ALLOC_PTR(nb2s) == NULL), failed);
	zUT(c2_bufvec_alloc_aligned(&nb2s->nb_buffer, 1, td->buf_seg_size2,
				    UT_PAGE_SHIFT), failed);
	zUT(c2_net_buffer_register(nb2s, DOM2), failed);
	zUT(c2_net_desc_copy(&nb1->nb_desc, &nb2s->nb_desc), failed);
	nb2s->nb_length = td->buf_seg_size2;
	nb2s->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	zvUT(c2_net_buffer_add(nb2s, TM2), -EFBIG, failed);

	/* success case */
	NLXDBGPnl(td, 1, "TEST: bulk transfer S(PS ~  AR)\n");
	nb2->nb_length = td->buf_size2;
	nb2->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	zUT(c2_net_buffer_add(nb2, TM2), failed);
	C2_UT_ASSERT(nb2->nb_flags & C2_NET_BUF_QUEUED);

	ut_chan_timedwait(&td->tmwait2, 10);
	C2_UT_ASSERT(cb_called2 == 1);
	C2_UT_ASSERT(cb_status2 == 0);
	C2_UT_ASSERT(cb_nb2 == nb2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_UT_ASSERT(cb_length2 == pBytes);
	C2_UT_ASSERT(ut_net_buffer_authenticate(nb2, cb_length2, 0, seed));
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_ACTIVE_BULK_RECV,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

	ut_chan_timedwait(&td->tmwait1, 10);
	C2_UT_ASSERT(cb_called1 == 1);
	C2_UT_ASSERT(cb_status1 == 0);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_PASSIVE_BULK_SEND);
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, C2_NET_QT_PASSIVE_BULK_SEND,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

	rc = 0;
 failed:
	c2_net_desc_free(&nb1->nb_desc);
	c2_net_desc_free(&nb2->nb_desc);
	if (nb2s != NULL) {
		if (nb2s->nb_flags & C2_NET_BUF_QUEUED) {
			NLXDBGP(td, 3, "\tcancelling nb2s\n");
			c2_net_buffer_del(nb2s, nb2s->nb_tm);
			ut_chan_timedwait(&td->tmwait2, 10);
			if (nb2s->nb_flags & C2_NET_BUF_QUEUED)
				NLXP("Unable to cancel nb2s\n");
		}
		if (nb2s->nb_flags & C2_NET_BUF_REGISTERED) {
			NLXDBGP(td, 3, "\tderegistering nb2s\n");
			c2_net_buffer_deregister(nb2s, DOM2);
		}
		if (nb2s->nb_buffer.ov_vec.v_nr != 0)
			c2_bufvec_free_aligned(&nb2s->nb_buffer, UT_PAGE_SHIFT);
		c2_net_desc_free(&nb2s->nb_desc);
		c2_free(nb2s);
	}
	c2_clink_del(&td->tmwait1);
	c2_clink_del(&td->tmwait2);
	return rc;
}

static int test_bulk_passive_recv(struct ut_data *td)
{
	struct c2_net_buffer *nb1 = &td->bufs1[0]; /* passive */
	struct c2_net_buffer *nb2 = &td->bufs2[0]; /* active */
	struct c2_net_buffer *nb2l = NULL;
	unsigned char seed = 'r';
	c2_bcount_t aBytes = UT_BULK_SIZE;
	int rc = -1;

	ut_cbreset();

	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	c2_clink_add(&TM2->ntm_chan, &td->tmwait2);

	/* stage passive recv buffer */
	nb1->nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	nb1->nb_length = td->buf_size1;
	zUT(c2_net_buffer_add(nb1, TM1), failed);
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);
	C2_UT_ASSERT(nb1->nb_desc.nbd_len != 0);
	C2_UT_ASSERT(nb1->nb_desc.nbd_data != NULL);

	zUT(c2_net_desc_copy(&nb1->nb_desc, &nb2->nb_desc), failed);
	C2_UT_ASSERT(td->buf_size2 >= aBytes);
	if (td->buf_size2 < aBytes)
		goto failed;
	nb2->nb_length = aBytes;
	ut_net_buffer_sign(nb2, nb2->nb_length, seed);

	/* ensure that an active receive fails */
	NLXDBGPnl(td, 1, "TEST: bulk transfer F(PR !~ AR)\n");
	nb2->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	zvUT(c2_net_buffer_add(nb2, TM2), -EPERM, failed);

	/* try to send a larger buffer */
	NLXDBGPnl(td, 1, "TEST: bulk transfer F(PR <  AS)\n");
	zUT((C2_ALLOC_PTR(nb2l) == NULL), failed);
	zUT(c2_bufvec_alloc_aligned(&nb2l->nb_buffer, UT_BUFSEGS1 + 1,
				    td->buf_seg_size1, UT_PAGE_SHIFT), failed);
	zUT(c2_net_buffer_register(nb2l, DOM2), failed);
	zUT(c2_net_desc_copy(&nb1->nb_desc, &nb2l->nb_desc), failed);
	nb2l->nb_length = td->buf_seg_size1 * (UT_BUFSEGS1 + 1);
	nb2l->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	zvUT(c2_net_buffer_add(nb2l, TM2), -EFBIG, failed);

	/* now try the success case */
	NLXDBGPnl(td, 1, "TEST: bulk transfer S(PR ~  AS)\n");
	nb2->nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	zUT(c2_net_buffer_add(nb2, TM2), failed);
	C2_UT_ASSERT(nb2->nb_flags & C2_NET_BUF_QUEUED);

	ut_chan_timedwait(&td->tmwait2, 10);
	C2_UT_ASSERT(cb_called2 == 1);
	C2_UT_ASSERT(cb_status2 == 0);
	C2_UT_ASSERT(cb_nb2 == nb2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_ACTIVE_BULK_SEND,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

	ut_chan_timedwait(&td->tmwait1, 10);
	C2_UT_ASSERT(cb_called1 == 1);
	C2_UT_ASSERT(cb_status1 == 0);
	C2_UT_ASSERT(cb_nb1 == nb1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_length1 == aBytes);
	C2_UT_ASSERT(ut_net_buffer_authenticate(nb1, cb_length1, 0, seed));
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, C2_NET_QT_PASSIVE_BULK_RECV,
					  &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

	rc = 0;
 failed:
	c2_net_desc_free(&nb1->nb_desc);
	c2_net_desc_free(&nb2->nb_desc);
	if (nb2l != NULL) {
		if (nb2l->nb_flags & C2_NET_BUF_QUEUED) {
			NLXDBGP(td, 3, "\tcancelling nb2l\n");
			c2_net_buffer_del(nb2l, nb2l->nb_tm);
			ut_chan_timedwait(&td->tmwait2, 10);
			if (nb2l->nb_flags & C2_NET_BUF_QUEUED)
				NLXP("Unable to cancel nb2l\n");
		}
		if (nb2l->nb_flags & C2_NET_BUF_REGISTERED) {
			NLXDBGP(td, 3, "\tderegistering nb2l\n");
			c2_net_buffer_deregister(nb2l, DOM2);
		}
		if (nb2l->nb_buffer.ov_vec.v_nr != 0)
			c2_bufvec_free_aligned(&nb2l->nb_buffer, UT_PAGE_SHIFT);
		c2_net_desc_free(&nb2l->nb_desc);
		c2_free(nb2l);
	}
	c2_clink_del(&td->tmwait1);
	c2_clink_del(&td->tmwait2);
	return rc;
}

static void test_bulk_body(struct ut_data *td)
{
	struct c2_net_buffer *nb1 = &td->bufs1[0];
	int i;

	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	/* TEST
	   Add buffers on the passive queues and then cancel them.
	   Check that descriptors are present after enqueuing.
	*/
	NLXDBGPnl(td, 1, "TEST: add/del on the passive queues\n");
	for (i = C2_NET_QT_PASSIVE_BULK_RECV;
	     i < C2_NET_QT_PASSIVE_BULK_SEND; ++i) {
		nb1->nb_length = td->buf_size1;
		nb1->nb_qtype  = i;

		ut_cbreset();
		C2_SET0(&nb1->nb_desc);
		zUT(c2_net_buffer_add(nb1, TM1), done);
		C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);
		C2_UT_ASSERT(nb1->nb_desc.nbd_len == sizeof *CBD1);
		C2_UT_ASSERT(nb1->nb_desc.nbd_data != NULL);

		c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
		c2_net_buffer_del(nb1, TM1);
		ut_chan_timedwait(&td->tmwait1, 10);
		c2_clink_del(&td->tmwait1);
		C2_UT_ASSERT(cb_called1 == 1);
		C2_UT_ASSERT(cb_nb1 == nb1);
		C2_UT_ASSERT(!(nb1->nb_flags & C2_NET_BUF_QUEUED));
		C2_UT_ASSERT(cb_qt1 == i);
		C2_UT_ASSERT(cb_status1 == -ECANCELED);
		C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, i, &td->qs, true));
		C2_UT_ASSERT(td->qs.nqs_num_f_events == 1);
		C2_UT_ASSERT(td->qs.nqs_num_s_events == 0);
		C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
		C2_UT_ASSERT(td->qs.nqs_num_dels == 1);

		/* explicitly free the descriptor */
		C2_UT_ASSERT(nb1->nb_desc.nbd_data != NULL);
		c2_net_desc_free(&nb1->nb_desc);
		C2_UT_ASSERT(nb1->nb_desc.nbd_data == NULL);
		C2_UT_ASSERT(nb1->nb_desc.nbd_len == 0);
	}
	c2_net_lnet_tm_set_debug(TM1, 0);
	c2_net_lnet_tm_set_debug(TM2, 0);

	/* sanity check */
	C2_UT_ASSERT(td->buf_size1 >= UT_BULK_SIZE);
	C2_UT_ASSERT(td->buf_size2 >= UT_BULK_SIZE);

	/* TEST
	   Test a passive send. Ensure that an active send cannot be
	   issued for the network buffer descriptor.
	   Also test size issues.
	*/
	zUT(test_bulk_passive_send(td), done);

	/* TEST
	   Test a passive receive. Ensure that an active receive cannot be
	   issued for the network buffer descriptor.
	   Also test size issues.
	 */
	zUT(test_bulk_passive_recv(td), done);

 done:
	return;
}

static void test_bulk(void)
{
#ifdef NLX_DEBUG
	nlx_debug._debug_ = 0;
#endif
	ut_test_framework(&test_bulk_body, NULL, ut_verbose);
#ifdef NLX_DEBUG
	nlx_debug._debug_ = 0;
#endif
}

static struct c2_thread_handle test_sync_ut_handle;

/* replacement for ut_buf_cb2 for this test */
static void test_sync_msg_send_cb2(const struct c2_net_buffer_event *ev)
{
	struct c2_thread_handle self;
	c2_thread_self(&self);
	/* async callback on background thread */
	C2_UT_ASSERT(!c2_thread_handle_eq(&self, &test_sync_ut_handle));

	ut_buf_cb2(ev);
}

/* replacement for ut_buf_cb1 for this test */
static void test_sync_msg_recv_cb1(const struct c2_net_buffer_event *ev)
{
	struct c2_thread_handle self;
	c2_thread_self(&self);
	/* synchronous callback on application thread */
	C2_UT_ASSERT(c2_thread_handle_eq(&self, &test_sync_ut_handle));

	cb_save_ep1 = true;
	ut_buf_cb1(ev);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_status1 == 0);
}

static void test_sync_body(struct ut_data *td)
{
	struct c2_net_buffer *nb1      = &td->bufs1[0];
	struct c2_net_buffer *nb2      = &td->bufs2[0];
	struct nlx_xo_transfer_mc *tp1 = TM1->ntm_xprt_private;
	struct c2_net_end_point *ep2 = NULL;
	int num_msgs;
	int initial_len;
	int len;
	int offset;
	int i;

	c2_net_lnet_tm_set_debug(TM1, 0);

	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);
	c2_clink_add(&TM2->ntm_chan, &td->tmwait2);

	/* TEST
	   No-op calls
	 */
	NLXDBGPnl(td, 1, "TEST: no-op sync calls\n");
	ut_cbreset();
	C2_UT_ASSERT(tp1->xtm_ev_chan == NULL);
	C2_UT_ASSERT(!c2_net_buffer_event_pending(TM1));
	c2_net_buffer_event_deliver_all(TM1);
	C2_UT_ASSERT(cb_called1 == 0);

	/* TEST
	   Test synchronous delivery of buffer events under control of the
	   application.
	   No events must be delivered until fetched.
	   Normal event delivery guarantees the use of a separate thread.
	   Synchronous event delivery guarantees the use of the application
	   thread.

	   Note that this test is not about the content of the event (tested
	   elsewhere) but about the control over delivery.
	   I use the C2_NET_QT_MSG_RECV queue for this test to make it easier
	   to generate multiple events, but any queue would have sufficied.
	*/
	NLXDBGPnl(td, 1, "TEST: sync delivery of buffer events\n");

	ut_cbreset();

	c2_thread_self(&test_sync_ut_handle); /* save thread handle */

	num_msgs = 4;
	initial_len = 256;
	nb1->nb_length = td->buf_size1;
	ut_net_buffer_sign(nb1, nb1->nb_length, 0);
	nb1->nb_min_receive_size = UT_MSG_SIZE;
	nb1->nb_max_receive_msgs = num_msgs;
	nb1->nb_qtype = C2_NET_QT_MSG_RECV;
	td->buf_cb1.nbc_cb[C2_NET_QT_MSG_RECV] = test_sync_msg_recv_cb1;
	zUT(c2_net_buffer_add(nb1, TM1), done);
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

	C2_UT_ASSERT(!c2_net_buffer_event_pending(TM1));
	C2_UT_ASSERT(tp1->xtm_ev_chan == NULL);
	c2_net_buffer_event_notify(TM1, &TM1->ntm_chan);
	C2_UT_ASSERT(tp1->xtm_ev_chan == &TM1->ntm_chan);

	/* get a TM2 end point for TM1's address */
	zUT(c2_net_end_point_create(&ep2, TM2, TM1->ntm_ep->nep_addr), done);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 1);

	td->buf_cb2.nbc_cb[C2_NET_QT_MSG_SEND] = test_sync_msg_send_cb2;
	for (i = 1; i <= num_msgs; ++i) {
		len = initial_len * i;
		C2_UT_ASSERT(len < td->buf_size2);
		ut_net_buffer_sign(nb2, len, i);
		nb2->nb_qtype = C2_NET_QT_MSG_SEND;
		nb2->nb_length = len;
		nb2->nb_ep = ep2;
		zUT(c2_net_buffer_add(nb2, TM2), done);
		C2_UT_ASSERT(nb2->nb_flags & C2_NET_BUF_QUEUED);

		ut_chan_timedwait(&td->tmwait2, 10);
		C2_UT_ASSERT(cb_called2 == i);
		C2_UT_ASSERT(cb_status2 == 0);
		C2_UT_ASSERT(cb_nb2 == nb2);
		C2_UT_ASSERT(cb_qt2 == C2_NET_QT_MSG_SEND);
		C2_UT_ASSERT(!c2_net_tm_stats_get(TM2, C2_NET_QT_MSG_SEND,
						  &td->qs, false));
		C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
		C2_UT_ASSERT(td->qs.nqs_num_s_events == i);
		C2_UT_ASSERT(td->qs.nqs_num_adds == i);
		C2_UT_ASSERT(td->qs.nqs_num_dels == 0);
	}

	C2_UT_ASSERT(cb_called1 == 0);
	C2_UT_ASSERT(ut_chan_timedwait(&td->tmwait1, 10));/* got notification */
	C2_UT_ASSERT(tp1->xtm_ev_chan == NULL);
	C2_UT_ASSERT(c2_net_buffer_event_pending(TM1));
	C2_UT_ASSERT(cb_called1 == 0);
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

	/* event is still pending */
	C2_UT_ASSERT(c2_net_buffer_event_pending(TM1));

	c2_net_buffer_event_deliver_all(TM1); /* get events */

	C2_UT_ASSERT(cb_called1 == num_msgs);
	C2_UT_ASSERT(!(nb1->nb_flags & C2_NET_BUF_QUEUED));
	C2_UT_ASSERT(cb_ep1 != NULL &&
		     strcmp(TM2->ntm_ep->nep_addr, cb_ep1->nep_addr) == 0);
	C2_UT_ASSERT(c2_atomic64_get(&cb_ep1->nep_ref.ref_cnt) == num_msgs);
	for (i = 1, len = 0, offset = 0; i <= num_msgs; ++i) {
		offset += len;
		len = initial_len * i;
		C2_UT_ASSERT(ut_net_buffer_authenticate(nb1, len, offset, i));
		c2_net_end_point_put(cb_ep1);
	}
	cb_ep1 = NULL;
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, C2_NET_QT_MSG_RECV,
					  &td->qs, false));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == num_msgs);
	C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);

 done:
	c2_clink_del(&td->tmwait2);
	c2_clink_del(&td->tmwait1);
	if (ep2 != NULL)
		c2_net_end_point_put(ep2);
	return;
}

static void test_sync_prestart(struct ut_data *td, int which)
{
	if (which == 2)
		return;
	/* use synchronous event delivery in TM1 */
	C2_UT_ASSERT(!c2_net_buffer_event_deliver_synchronously(TM1));
}

static void test_sync(void)
{
	ut_test_framework(&test_sync_body, &test_sync_prestart, ut_verbose);
}


/* replacement for ut_buf_cb1 used in this test */
static void test_timeout_msg_recv_cb1(const struct c2_net_buffer_event *ev)
{
	ut_buf_cb1(ev);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_status1 == -ETIMEDOUT);
}

/* intercepted sub */
static c2_time_t test_timeout_tm_get_buffer_timeout_tick(const struct
							 c2_net_transfer_mc *tm)
{
	c2_time_t tick;
	c2_time_set(&tick, 1, 0);
	return tick >> 1; /* 500ms */
}

/* intercepted sub */
static struct c2_atomic64 test_timeout_ttb_called;
static struct c2_atomic64 test_timeout_ttb_retval; /* non zero */
static int test_timeout_tm_timeout_buffers(struct c2_net_transfer_mc *tm,
					   c2_time_t now)
{
	int rc;
	c2_atomic64_inc(&test_timeout_ttb_called);
	rc = nlx_tm_timeout_buffers(tm, now);
	if (rc)
		c2_atomic64_set(&test_timeout_ttb_retval, rc);
	return rc;
}

static void test_timeout_body(struct ut_data *td)
{
	struct c2_net_buffer *nb1 = &td->bufs1[0];
	int qts[3] = {
		C2_NET_QT_MSG_RECV,
		C2_NET_QT_PASSIVE_BULK_RECV,
		C2_NET_QT_PASSIVE_BULK_SEND
	};
	int qt;
	int i;
	c2_time_t abs_timeout;
	c2_time_t rel_timeout;
	uint64_t timeout_secs = 1;

	c2_clink_add(&TM1->ntm_chan, &td->tmwait1);

	/* TEST
	   Enqueue non-active buffers one at a time on different queues,
	   and let them timeout.
	   Cannot test with active buffers.
	*/
	c2_net_lnet_tm_set_debug(TM1, 0);
	nb1->nb_length = td->buf_size1;
	c2_time_set(&rel_timeout, timeout_secs, 0);
	for (i = 0; i < ARRAY_SIZE(qts); ++i) {
		qt = qts[i];
		NLXDBGPnl(td, 1, "TEST: buffer single timeout: %d\n", (int) qt);
		ut_cbreset();
		c2_atomic64_set(&test_timeout_ttb_called, 0);
		if (qt == C2_NET_QT_MSG_RECV) {
			nb1->nb_min_receive_size = UT_MSG_SIZE;
			nb1->nb_max_receive_msgs = 1;
		} else {
			nb1->nb_min_receive_size = 0;
			nb1->nb_max_receive_msgs = 0;
		}
		nb1->nb_qtype = qt;
		abs_timeout = c2_time_from_now(timeout_secs, 0);
		C2_UT_ASSERT(nb1->nb_timeout == C2_TIME_NEVER);
		nb1->nb_timeout = abs_timeout;
		zUT(c2_net_buffer_add(nb1, TM1), done);
		C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

		ut_chan_timedwait(&td->tmwait1, 2 * timeout_secs);
		C2_UT_ASSERT(c2_atomic64_get(&test_timeout_ttb_called) >=
					     2 * timeout_secs); /* 0.5s tick */
		C2_UT_ASSERT(c2_atomic64_get(&test_timeout_ttb_retval) == 1);
		C2_UT_ASSERT(cb_called1 == 1);
		C2_UT_ASSERT(cb_status1 == -ETIMEDOUT);
		C2_UT_ASSERT(!(nb1->nb_flags & C2_NET_BUF_QUEUED));
		C2_UT_ASSERT(nb1->nb_timeout == C2_TIME_NEVER);
		C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, qt, &td->qs, true));
		C2_UT_ASSERT(td->qs.nqs_num_f_events == 1);
		C2_UT_ASSERT(td->qs.nqs_num_s_events == 0);
		C2_UT_ASSERT(td->qs.nqs_num_adds == 1);
		C2_UT_ASSERT(td->qs.nqs_num_dels == 0);
		C2_UT_ASSERT(td->qs.nqs_time_in_queue >= rel_timeout);

		if (qt != C2_NET_QT_MSG_RECV)
			c2_net_desc_free(&nb1->nb_desc);
	}

	/* TEST
	   Enqueue multiple non-active buffers on a single queue and let
	   them timeout at the same time.
	   Cannot test with active buffers.
	*/
	c2_net_lnet_tm_set_debug(TM1, 0);
	qt = C2_NET_QT_MSG_RECV;
	NLXDBGPnl(td, 1, "TEST: buffer multiple timeout\n");
	td->buf_cb1.nbc_cb[qt] = test_timeout_msg_recv_cb1;
	abs_timeout = c2_time_from_now(timeout_secs, 0);
	ut_cbreset();
	c2_atomic64_set(&test_timeout_ttb_called, 0);
	for (i = 0; i < UT_BUFS1; ++i) {
		nb1 = &td->bufs1[i];
		nb1->nb_qtype = qt;
		nb1->nb_min_receive_size = UT_MSG_SIZE;
		nb1->nb_max_receive_msgs = 1;
		C2_UT_ASSERT(nb1->nb_timeout == C2_TIME_NEVER);
		nb1->nb_timeout = abs_timeout;
		zUT(c2_net_buffer_add(nb1, TM1), done);
		C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);
	}

	i = 0;
	while (cb_called1 != UT_BUFS1 && i <= UT_BUFS1) {
		ut_chan_timedwait(&td->tmwait1, 2 * timeout_secs);
		++i;
	}
	C2_UT_ASSERT(cb_called1 == UT_BUFS1);
	C2_UT_ASSERT(c2_atomic64_get(&test_timeout_ttb_called) >=
				     2 * timeout_secs); /* 0.5s tick */
	C2_UT_ASSERT(c2_atomic64_get(&test_timeout_ttb_retval) == UT_BUFS1);
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, qt, &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == UT_BUFS1);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_adds == UT_BUFS1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);
	C2_UT_ASSERT(td->qs.nqs_time_in_queue >= UT_BUFS1 * rel_timeout);

	for (i = 0; i < UT_BUFS1; ++i) {
		nb1 = &td->bufs1[i];
		C2_UT_ASSERT(nb1->nb_timeout == C2_TIME_NEVER);
	}

	/* TEST
	   Enqueue multiple non-active buffers onto a single queue.
	   Set a timeout only on one buffer.
	   Cannot test with active buffers.
	*/
	c2_net_lnet_tm_set_debug(TM1, 0);
	qt = C2_NET_QT_MSG_RECV;
	NLXDBGPnl(td, 1, "TEST: buffer mixed timeout\n");
	td->buf_cb1.nbc_cb[qt] = test_timeout_msg_recv_cb1;
	abs_timeout = c2_time_from_now(timeout_secs, 0);
	ut_cbreset();
	c2_atomic64_set(&test_timeout_ttb_called, 0);
	for (i = 0; i < UT_BUFS1; ++i) {
		nb1 = &td->bufs1[i];
		nb1->nb_qtype = qt;
		nb1->nb_min_receive_size = UT_MSG_SIZE;
		nb1->nb_max_receive_msgs = 1;
		C2_UT_ASSERT(nb1->nb_timeout == C2_TIME_NEVER);
		if (i == 0)
			nb1->nb_timeout = abs_timeout;
		zUT(c2_net_buffer_add(nb1, TM1), done);
		C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);
	}

	i = 0;
	while (cb_called1 != 1 && i <= UT_BUFS1) {
		ut_chan_timedwait(&td->tmwait1, 2 * timeout_secs);
		++i;
	}
	C2_UT_ASSERT(cb_called1 == 1);
	C2_UT_ASSERT(c2_atomic64_get(&test_timeout_ttb_called) >=
				     2 * timeout_secs); /* 0.5s tick */
	C2_UT_ASSERT(c2_atomic64_get(&test_timeout_ttb_retval) == 1);
	C2_UT_ASSERT(!c2_net_tm_stats_get(TM1, qt, &td->qs, true));
	C2_UT_ASSERT(td->qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(td->qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(td->qs.nqs_num_adds == UT_BUFS1);
	C2_UT_ASSERT(td->qs.nqs_num_dels == 0);
	C2_UT_ASSERT(td->qs.nqs_time_in_queue >= rel_timeout);

	/* restore the callback sub and then cancel the other buffer */
	td->buf_cb1.nbc_cb[qt] = ut_buf_cb1;
	c2_net_buffer_del(nb1, TM1);
	ut_chan_timedwait(&td->tmwait1, 3);
	C2_UT_ASSERT(cb_called1 == 2);
	C2_UT_ASSERT(cb_status1 == -ECANCELED);

	/* TEST
	   Enqueue a buffer, no timeout.
	   Force set the C2_NET_BUF_TIMED_OUT flag in the buffer.
	   Construct a core buffer event structure and set the status to 0
           in the structure, indicating that the buffer operation completed
	   successfully and co-incidentally with the time out.
	   Call nlx_xo_core_bev_to_net_bev() to construct the net buffer
	   event structure.
	   The status should be 0 in the buffer event structure and the
	   C2_NET_BUF_TIMED_OUT flag should be cleared.

	   This situation is quite probable when using synchronous buffer event
	   delivery when the cancel is issued while the completion event is
	   already present in the circular buffer but not yet harvested.
	 */
	c2_net_lnet_tm_set_debug(TM1, 0);
	nb1 = &td->bufs1[0];
	nb1->nb_length = td->buf_size1;
	nb1->nb_min_receive_size = UT_MSG_SIZE;
	nb1->nb_max_receive_msgs = 1;
	nb1->nb_timeout = C2_TIME_NEVER;
	nb1->nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV; /* doesn't involve EPs */
	zUT(c2_net_buffer_add(nb1, TM1), done);
	C2_UT_ASSERT(nb1->nb_flags & C2_NET_BUF_QUEUED);

	{
		struct nlx_core_buffer_event lcbev = {
			.cbe_buffer_id = (nlx_core_opaque_ptr_t) nb1,
			.cbe_status    = 0,
			.cbe_length    = 10,
			.cbe_unlinked  = true,
		};
		struct c2_net_buffer_event nbev;

		nb1->nb_flags |= C2_NET_BUF_TIMED_OUT;

		c2_mutex_lock(&TM1->ntm_mutex);
		zUT(nlx_xo_core_bev_to_net_bev(TM1, &lcbev, &nbev), done);
		c2_mutex_unlock(&TM1->ntm_mutex);
		C2_UT_ASSERT(!(nb1->nb_flags & C2_NET_BUF_TIMED_OUT));
		C2_UT_ASSERT(nbev.nbe_status == 0);
		C2_UT_ASSERT(nbev.nbe_length == lcbev.cbe_length);
	}

	/* cancel the buffer */
	c2_net_buffer_del(nb1, TM1);
	ut_chan_timedwait(&td->tmwait1, 3);
	C2_UT_ASSERT(cb_called1 == 3);
	C2_UT_ASSERT(cb_status1 == -ECANCELED);
	c2_net_desc_free(&nb1->nb_desc);

 done:
	c2_clink_del(&td->tmwait1);
}

static void test_timeout(void)
{
	ut_save_subs();

	nlx_xo_iv._nlx_tm_get_buffer_timeout_tick =
		test_timeout_tm_get_buffer_timeout_tick;
	nlx_xo_iv._nlx_tm_timeout_buffers =
		test_timeout_tm_timeout_buffers;
	c2_atomic64_set(&test_timeout_ttb_called, 0);
	c2_atomic64_set(&test_timeout_ttb_retval, 0);

	ut_test_framework(&test_timeout_body, NULL, ut_verbose);

	ut_restore_subs();
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
		{ "net_lnet_bulk (K)",      ktest_bulk },
		{ "net_lnet_device",        ktest_dev },
#endif
		{ "net_lnet_tm_initfini",   test_tm_initfini },
		{ "net_lnet_tm_startstop",  test_tm_startstop },
		{ "net_lnet_msg",           test_msg },
		{ "net_lnet_buf_desc",      test_buf_desc },
		{ "net_lnet_bulk",          test_bulk },
		{ "net_lnet_sync",          test_sync },
		{ "net_lnet_timeout",       test_timeout },
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
