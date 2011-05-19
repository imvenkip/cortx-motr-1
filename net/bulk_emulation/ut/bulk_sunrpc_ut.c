/* -*- C -*- */

#include "lib/misc.h"
#include "lib/ut.h"

#include "net/bulk_emulation/sunrpc_xprt_xo.c"
#include "net/bulk_emulation/st/ping.h"

static void test_sunrpc_ep(void)
{
	/* dom1 */
	struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	struct c2_net_end_point *ep1;
	struct c2_net_end_point *ep2;
	struct c2_net_end_point *ep3;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_sunrpc_xprt));
	C2_UT_ASSERT(c2_net_end_point_create(&ep1, &dom1,
					     "255.255.255.255", 65535, 0)
		     == -EINVAL);
	C2_UT_ASSERT(!c2_net_end_point_create(&ep1, &dom1,
					      "255.255.255.255", 65535,
					      4294967295U, 0));
	C2_UT_ASSERT(strcmp(ep1->nep_addr,"255.255.255.255:65535:4294967295")
		     ==0);
	C2_UT_ASSERT(c2_atomic64_get(&ep1->nep_ref.ref_cnt) == 1);

	C2_UT_ASSERT(!c2_net_end_point_create(&ep2, &dom1,
					      "255.255.255.255", 65535,
					      4294967295U, 0));
	C2_UT_ASSERT(strcmp(ep2->nep_addr,"255.255.255.255:65535:4294967295")
		     ==0);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);
	C2_UT_ASSERT(ep1 == ep2);

	C2_UT_ASSERT(!c2_net_end_point_create(&ep3, &dom1,
		     "255.255.255.255:65535:4294967295", 0));
	C2_UT_ASSERT(strcmp(ep3->nep_addr,"255.255.255.255:65535:4294967295")
		     ==0);
	C2_UT_ASSERT(c2_atomic64_get(&ep3->nep_ref.ref_cnt) == 3);
	C2_UT_ASSERT(ep1 == ep3);

	C2_UT_ASSERT(!c2_net_end_point_put(ep1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep2));
	C2_UT_ASSERT(!c2_net_end_point_put(ep3));

	c2_net_domain_fini(&dom1);
}

static enum c2_net_ev_type cb_evt1;
static enum c2_net_queue_type cb_qt1;
static struct c2_net_buffer *cb_nb1;
static enum c2_net_tm_state cb_tms1;
static int32_t cb_status1;
void sunrpc_tf_cb1(const struct c2_net_event *ev)
{
	cb_evt1 = ev->nev_type;
	if (ev->nev_type == C2_NET_EV_BUFFER_RELEASE){
		cb_nb1 = ev->nev_buffer;
		cb_qt1 = cb_nb1->nb_qtype;
	} else {
		cb_nb1 = NULL;
		cb_qt1 = C2_NET_QT_NR;
	}
	cb_tms1 = ev->nev_next_state;
	cb_status1 = ev->nev_status;
}

static enum c2_net_ev_type cb_evt2;
static enum c2_net_queue_type cb_qt2;
static struct c2_net_buffer *cb_nb2;
static enum c2_net_tm_state cb_tms2;
static int32_t cb_status2;
void sunrpc_tf_cb2(const struct c2_net_event *ev)
{
	cb_evt2 = ev->nev_type;
	if (ev->nev_type == C2_NET_EV_BUFFER_RELEASE){
		cb_nb2 = ev->nev_buffer;
		cb_qt2 = cb_nb2->nb_qtype;
	} else {
		cb_nb2 = NULL;
		cb_qt2 = C2_NET_QT_NR;
	}
	cb_tms2 = ev->nev_next_state;
	cb_status2 = ev->nev_status;
}

static void test_sunrpc_desc(void)
{
	struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	struct c2_net_end_point *ep1, *ep2;

	struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = sunrpc_tf_cb1
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_clink tmwait;
	struct c2_net_buf_desc desc1;
	struct sunrpc_buf_desc sd;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_sunrpc_xprt));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep1, &dom1,
					      "127.0.0.1", 31111, 1));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep2, &dom1,
					      "127.0.0.1", 31111, 2));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	/* start tm and wait for tm to notify it has started */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, ep1));
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);

	C2_SET0(&desc1);
	C2_SET0(&sd);

	C2_UT_ASSERT(!sunrpc_desc_create(&desc1, ep2, &d1tm1,
					 C2_NET_QT_PASSIVE_BULK_RECV,
					 2345, 34));
	C2_UT_ASSERT(desc1.nbd_len == sizeof(struct sunrpc_buf_desc));

	C2_UT_ASSERT(!sunrpc_desc_decode(&desc1, &sd));
	C2_UT_ASSERT(sd.sbd_id == 34);
	C2_UT_ASSERT(sd.sbd_qtype == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(sd.sbd_total == 2345);
	C2_UT_ASSERT(sd.sbd_active_ep.sep_addr == htonl(0x7f000001));
	C2_UT_ASSERT(sd.sbd_active_ep.sep_port == htons(31111));
	C2_UT_ASSERT(sd.sbd_active_ep.sep_id == 2);
	C2_UT_ASSERT(sd.sbd_passive_ep.sep_addr == htonl(0x7f000001));
	C2_UT_ASSERT(sd.sbd_passive_ep.sep_port == htons(31111));
	C2_UT_ASSERT(sd.sbd_passive_ep.sep_id == 1);
	c2_net_desc_free(&desc1);

	c2_clink_add(&d1tm1.ntm_chan, &tmwait);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);
	c2_net_tm_fini(&d1tm1);

	C2_UT_ASSERT(!c2_net_end_point_put(ep2));
	C2_UT_ASSERT(!c2_net_end_point_put(ep1));

	c2_net_domain_fini(&dom1);
}

enum {
	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 512,
	PING_SERVER_SEGMENTS = 4,
	PING_SERVER_SEGMENT_SIZE = 1024,
	PING_NR_BUFS = 20
};
static int quiet_printf(const char *fmt, ...)
{
	return 0;
}

static struct ping_ops quiet_ops = {
	.pf = quiet_printf
};

static void test_sunrpc_ping(void)
{
	struct ping_ctx cctx = {
		.pc_ops = &quiet_ops,
		.pc_xprt = &c2_net_bulk_sunrpc_xprt,
		.pc_port = PING_PORT1,
		.pc_id = 1,
		.pc_rid = PART3_SERVER_ID,
		.pc_nr_bufs = PING_NR_BUFS,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};
	struct ping_ctx sctx = {
		.pc_ops = &quiet_ops,
		.pc_xprt = &c2_net_bulk_sunrpc_xprt,
		.pc_port = PING_PORT1,
		.pc_id = PART3_SERVER_ID,
		.pc_nr_bufs = PING_NR_BUFS,
		.pc_segments = PING_SERVER_SEGMENTS,
		.pc_seg_size = PING_SERVER_SEGMENT_SIZE,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};
	int rc;
	struct c2_net_end_point *server_ep;
	struct c2_thread server_thread;
	int i;
	char *data;
	int len;

	c2_mutex_init(&sctx.pc_mutex);
	c2_cond_init(&sctx.pc_cond);
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);

	C2_UT_ASSERT(c2_net_xprt_init(&c2_net_bulk_sunrpc_xprt) == 0);

	C2_UT_ASSERT(ping_client_init(&cctx, &server_ep) == 0);
	/* client times out because server is not ready */
	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, NULL) != 0);
	/* server runs in background thread */
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &ping_server, &sctx, "ping_server");
	if (rc != 0) {
		C2_UT_FAIL("failed to start ping server");
		return;
	} else
		C2_UT_PASS("started ping server");

	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, NULL) == 0);
	C2_UT_ASSERT(ping_client_passive_recv(&cctx, server_ep) == 0);
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep, NULL) == 0);

	/* test sending/receiving a bigger payload */
	data = c2_alloc(PING_CLIENT_SEGMENTS * PING_CLIENT_SEGMENT_SIZE);
	C2_UT_ASSERT(data != NULL);
	len = (PING_CLIENT_SEGMENTS-1) * PING_CLIENT_SEGMENT_SIZE + 1;
	for (i = 0; i < len; ++i)
		data[i] = "abcdefghi"[i % 9];
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep, data) == 0);

	C2_UT_ASSERT(ping_client_fini(&cctx, server_ep) == 0);

	ping_server_should_stop(&sctx);
	C2_UT_ASSERT(c2_thread_join(&server_thread) == 0);

	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
	c2_cond_fini(&sctx.pc_cond);
	c2_mutex_fini(&sctx.pc_mutex);
	c2_net_xprt_fini(&c2_net_bulk_sunrpc_xprt);
}

static void test_sunrpc_failure(void)
{
	/* dom1 */
	struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = sunrpc_tf_cb1
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_net_buffer d1nb1;
	struct c2_net_buffer d1nb2;
	struct c2_clink tmwait1;

	/* dom 2 */
 	struct c2_net_domain dom2 = {
		.nd_xprt = NULL
	};
	struct c2_net_tm_callbacks cbs2 = {
		.ntc_event_cb = sunrpc_tf_cb2
	};
	struct c2_net_transfer_mc d2tm1 = {
		.ntm_callbacks = &cbs2,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_net_transfer_mc d2tm2 = {
		.ntm_callbacks = &cbs2,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_net_buffer d2nb1;
	struct c2_net_buffer d2nb2;
	struct c2_clink tmwait2;

	struct c2_net_end_point *ep;
	struct c2_net_qstats qs;

	/* setup the first dom - use non-reserved port numbers */
	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_sunrpc_xprt));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 10000, 1, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:1")==0);
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);
	C2_SET0(&d1nb1);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d1nb1.nb_buffer, 4, 10000));
	C2_UT_ASSERT(!c2_net_buffer_register(&d1nb1, &dom1));
	C2_SET0(&d1nb2);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d1nb2.nb_buffer, 1, 10000));
	C2_UT_ASSERT(!c2_net_buffer_register(&d1nb2, &dom1));

	/* setup the second dom */
	C2_UT_ASSERT(!c2_net_domain_init(&dom2, &c2_net_bulk_sunrpc_xprt));
	C2_SET0(&d2nb1);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d2nb1.nb_buffer, 4, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d2nb1, &dom2));
	C2_SET0(&d2nb2);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d2nb2.nb_buffer, 1, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d2nb2, &dom2));

 	/* TEST
	   Start a TM in the second domain using a different port number.
	   Bulksunrpc requires a single port number for all TMs, per process,
	   regardless of domain.
	*/
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm2, &dom2));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1:21000:3", 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:21000:3")==0);
	cb_status2 = 0;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm2, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_status2 == -EADDRNOTAVAIL);
	C2_UT_ASSERT(d2tm2.ntm_state == C2_NET_TM_FAILED);

	/* start a TM with id 3 in the second domain */
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm1, &dom2));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1:10000:3", 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:3")==0);
	cb_status2 = 0;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm1, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_status2 == 0);
	C2_UT_ASSERT(d2tm1.ntm_state == C2_NET_TM_STARTED);

 	/* TEST
	   Start a second TM in the second domain, using the same port
	   number and the same service id.
	*/
	c2_net_tm_fini(&d2tm2);
	d2tm2.ntm_state = C2_NET_TM_UNDEFINED;
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm2, &dom2));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1:10000:3", 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:3")==0);
	cb_status2 = 0;
	C2_UT_ASSERT(c2_net_tm_start(&d2tm2, ep) == -EADDRINUSE);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

 	/* TEST
	   Start a second TM in the second domain, using a different port
	   number.
	   Bulksunrpc requires a single port number for all TMs, per process,
	   regardless of domain.
	*/
	c2_net_tm_fini(&d2tm2);
	d2tm2.ntm_state = C2_NET_TM_UNDEFINED;
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm2, &dom2));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1:10000:1", 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:1")==0);
	cb_status2 = 0;
	C2_UT_ASSERT(c2_net_tm_start(&d2tm2, ep) == -EADDRINUSE);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	/* TEST
	   Send a message from d1tm1 to d2tm2 - should fail because
	   the destination TM not started.
	*/
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 10000, 4, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:4")==0);
	d1nb1.nb_qtype = C2_NET_QT_MSG_SEND;
	d1nb1.nb_ep = ep;
	d1nb1.nb_length = 10; /* don't care */
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -ENXIO);

	/* start the TM with id 4 in the second dom */
	c2_net_tm_fini(&d2tm2);
	d2tm2.ntm_state = C2_NET_TM_UNDEFINED;
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm2, &dom2));
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10000, 4, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:4")==0);
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm2, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm1.ntm_state == C2_NET_TM_STARTED);

	/* TEST
	   Send a message from d1tm1 to d2tm1 - should fail because
	   no receive buffers available.
	   The failure count on the receive queue of d2tm1 should
	   be bumped, and an -ENOBUFS error callback delivered.
	*/
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	cb_status2 = 0;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 10000, 3, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:3")==0);
	d1nb1.nb_qtype = C2_NET_QT_MSG_SEND;
	d1nb1.nb_ep = ep;
	d1nb1.nb_length = 10; /* don't care */
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -ENOBUFS);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 0);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);
	C2_UT_ASSERT(cb_nb2 == NULL);
	C2_UT_ASSERT(cb_tms2 == C2_NET_TM_UNDEFINED);
	C2_UT_ASSERT(cb_status2 == -ENOBUFS);

	/* TEST
	   Add a receive buffer in d2tm1.
	   Send a larger message from d1tm1 to d2tm1.
	   Both buffers should fail with -EMSGSIZE.
	*/
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	d2nb2.nb_qtype = C2_NET_QT_MSG_RECV;
	d2nb2.nb_ep = NULL;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb2, &d2tm1));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 10000, 3, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:3")==0);
	d1nb1.nb_qtype = C2_NET_QT_MSG_SEND;
	d1nb1.nb_ep = ep;
	d1nb1.nb_length = 40;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_MSG_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -EMSGSIZE);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_MSG_SEND,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_MSG_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb2);
	C2_UT_ASSERT(d2nb2.nb_status == -EMSGSIZE);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_MSG_RECV,&qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to actively send from an unauthorized dom
	*/
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10000, 9, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:9")==0);
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb1.nb_desc, &d1nb1.nb_desc));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	d1nb1.nb_length = 10;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -EACCES);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 1);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to actively receive from it.
	*/
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10000, 1, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:1")==0);
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb1.nb_desc, &d1nb1.nb_desc));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	d1nb1.nb_length = 10;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -EPERM);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 1);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to send a larger message from the other dom.
	*/
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10000, 1, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:1")==0);
	d2nb2.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb2.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb2, &d2tm1));
	C2_UT_ASSERT(d2nb2.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb2.nb_desc, &d1nb1.nb_desc));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	d1nb1.nb_length = 40; /* larger than d2nb2 */
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -EFBIG);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb2);
	C2_UT_ASSERT(d2nb2.nb_status == -EFBIG);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	/* TEST
	   Setup a passive send buffer and add it. Save the descriptor in the
	   active buffer of the other dom.  Do not start the active operation
	   yet. Del the passive buffer. Re-submit the same buffer for the same
	   passive operation. Try the active operation in the other dom, using
	   the original desc. Should fail because buffer id changes per add.
	 */
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10000, 1, 0));
	C2_UT_ASSERT(strcmp(ep->nep_addr,"127.0.0.1:10000:1")==0);
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	/* C2_UT_ASSERT(!c2_net_end_point_put(ep)); reuse it on resubmit */

	/* copy the desc but don't start the active operation yet */
	C2_UT_ASSERT(!c2_net_desc_copy(&d2nb1.nb_desc, &d1nb1.nb_desc));

	/* cancel the original passive operation */
	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);

	/* resubmit */
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

	/* descriptors should have changed */
	C2_UT_ASSERT(d1nb1.nb_desc.nbd_len != d2nb1.nb_desc.nbd_len ||
		     memcmp(d1nb1.nb_desc.nbd_data, d2nb1.nb_desc.nbd_data,
			    d1nb1.nb_desc.nbd_len) != 0);

	/* start the active operation */
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	d1nb1.nb_qtype = C2_NET_QT_ACTIVE_BULK_SEND;
	d1nb1.nb_length = 10;
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	cb_qt1 = C2_NET_QT_NR;
	cb_nb1 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d1nb1, &d1tm1));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(cb_qt1 == C2_NET_QT_ACTIVE_BULK_SEND);
	C2_UT_ASSERT(cb_nb1 == &d1nb1);
	C2_UT_ASSERT(d1nb1.nb_status == -ENOENT);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d1tm1,C2_NET_QT_ACTIVE_BULK_SEND,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 1);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 1);
	C2_UT_ASSERT(qs.nqs_num_dels == 0);

	c2_net_buffer_del(&d2nb1, &d2tm1);
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);
	C2_UT_ASSERT(!c2_net_tm_stats_get(&d2tm1,C2_NET_QT_PASSIVE_BULK_RECV,
					  &qs,true));
	C2_UT_ASSERT(qs.nqs_num_f_events == 2);
	C2_UT_ASSERT(qs.nqs_num_s_events == 0);
	C2_UT_ASSERT(qs.nqs_num_adds == 2);
	C2_UT_ASSERT(qs.nqs_num_dels == 2);

	/* fini */
	C2_UT_ASSERT(!c2_net_buffer_deregister(&d1nb1, &dom1));
	C2_UT_ASSERT(!c2_net_buffer_deregister(&d1nb2, &dom1));
	C2_UT_ASSERT(!c2_net_buffer_deregister(&d2nb1, &dom2));
	C2_UT_ASSERT(!c2_net_buffer_deregister(&d2nb2, &dom2));

	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);

	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stop(&d2tm1, false));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm1.ntm_state == C2_NET_TM_STOPPED);

	if (d2tm2.ntm_state != C2_NET_TM_FAILED) {
		c2_clink_init(&tmwait2, NULL);
		c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
		C2_UT_ASSERT(!c2_net_tm_stop(&d2tm2, false));
		c2_chan_wait(&tmwait2);
		c2_clink_del(&tmwait2);
		C2_UT_ASSERT(d2tm2.ntm_state == C2_NET_TM_STOPPED);
	}

	c2_net_tm_fini(&d1tm1);
	c2_net_tm_fini(&d2tm1);
	c2_net_tm_fini(&d2tm2);

	c2_net_domain_fini(&dom1);
	c2_net_domain_fini(&dom2);
}

static void test_sunrpc_tm(void)
{
	struct c2_net_domain dom1 = {
		.nd_xprt = NULL
	};
	struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = LAMBDA(void,(const struct c2_net_event *ev) {
				       }),
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};
	struct c2_clink tmwait1;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_sunrpc_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	/* should be able to fini it immediately */
	c2_net_tm_fini(&d1tm1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_UNDEFINED);

	/* should be able to init it again */
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_INITIALIZED);
	C2_UT_ASSERT(c2_list_contains(&dom1.nd_tms, &d1tm1.ntm_dom_linkage));

	/* check thread counts */
	C2_UT_ASSERT(c2_net_bulk_mem_tm_get_num_threads(&d1tm1)
		     == C2_NET_BULK_SUNRPC_TM_THREADS);
	c2_net_bulk_mem_tm_set_num_threads(&d1tm1, 2);
	C2_UT_ASSERT(c2_net_bulk_mem_tm_get_num_threads(&d1tm1) == 2);

	/* fini */
	if (d1tm1.ntm_state > C2_NET_TM_INITIALIZED) {
		c2_clink_init(&tmwait1, NULL);
		c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
		C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
		c2_chan_wait(&tmwait1);
		c2_clink_del(&tmwait1);
		C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);
	}
	c2_net_tm_fini(&d1tm1);
	c2_net_domain_fini(&dom1);
}

const struct c2_test_suite net_bulk_sunrpc_ut = {
        .ts_name = "net-bulk-sunrpc",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_sunrpc_ep",         test_sunrpc_ep },
                { "net_bulk_sunrpc_desc",       test_sunrpc_desc },
                { "net_bulk_sunrpc_failure",    test_sunrpc_failure },
		{ "net_bulk_sunrpc_tm_test",    test_sunrpc_tm},
                { "net_bulk_sunrpc_ping_tests", test_sunrpc_ping },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
