/* -*- C -*- */

#include "lib/misc.h"
#include "lib/ut.h"

#include "net/bulk_emulation/sunrpc_xprt_xo.c"
#include "net/bulk_emulation/st/ping.h"

void test_sunrpc_ep(void)
{
	/* dom1 */
	struct c2_net_domain dom1;
	struct c2_net_end_point *ep1;
	struct c2_net_end_point *ep2;
	struct c2_net_end_point *ep3;

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_sunrpc_xprt));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep1, &dom1,
					      "255.255.255.255", 65535, 0));
	C2_UT_ASSERT(strcmp(ep1->nep_addr,"255.255.255.255:65535")==0);
	C2_UT_ASSERT(c2_atomic64_get(&ep1->nep_ref.ref_cnt) == 1);

	C2_UT_ASSERT(!c2_net_end_point_create(&ep2, &dom1,
					      "255.255.255.255", 65535, 0));
	C2_UT_ASSERT(strcmp(ep2->nep_addr,"255.255.255.255:65535")==0);
	C2_UT_ASSERT(c2_atomic64_get(&ep2->nep_ref.ref_cnt) == 2);
	C2_UT_ASSERT(ep1 == ep2);

	C2_UT_ASSERT(!c2_net_end_point_create(&ep3, &dom1,
					      "255.255.255.255:65535", 0));
	C2_UT_ASSERT(strcmp(ep3->nep_addr,"255.255.255.255:65535")==0);
	C2_UT_ASSERT(c2_atomic64_get(&ep3->nep_ref.ref_cnt) == 3);
	C2_UT_ASSERT(ep1 == ep3);

	C2_UT_ASSERT(!c2_net_end_point_put(ep1));
	C2_UT_ASSERT(!c2_net_end_point_put(ep2));
	C2_UT_ASSERT(!c2_net_end_point_put(ep3));

	c2_net_domain_fini(&dom1);
}

void test_sunrpc_desc(void)
{
	struct c2_net_domain dom1;
	struct c2_net_end_point *ep1, *ep2;

	enum c2_net_queue_type cb_qt1;
	struct c2_net_buffer *cb_nb1;
	struct c2_net_tm_callbacks cbs1 = {
		.ntc_event_cb = LAMBDA(void,(struct c2_net_transfer_mc *tm,
					     struct c2_net_event *ev){
					       cb_qt1 = ev->nev_qtype;
					       cb_nb1 = ev->nev_buffer;
				       }),
	};
	struct c2_net_transfer_mc d1tm1 = {
		.ntm_callbacks = &cbs1,
		.ntm_state = C2_NET_TM_UNDEFINED
	};

	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_sunrpc_xprt));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep1, &dom1,
					      "127.0.0.1", 31111, 0));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep2, &dom1,
					      "127.0.0.1", 31112, 0));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));

	/* start tm and wait for tm to notify it has started */
	struct c2_clink tmwait;
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, ep1));
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);

	struct c2_net_buf_desc desc1;
	struct sunrpc_buf_desc sd;

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
	C2_UT_ASSERT(sd.sbd_active_ep.sep_port == htons(31112));
	C2_UT_ASSERT(sd.sbd_passive_ep.sep_addr == htonl(0x7f000001));
	C2_UT_ASSERT(sd.sbd_passive_ep.sep_port == htons(31111));
	c2_net_desc_free(&desc1);

	c2_clink_add(&d1tm1.ntm_chan, &tmwait);
	C2_UT_ASSERT(!c2_net_tm_stop(&d1tm1, false));
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STOPPED);
	C2_UT_ASSERT(!c2_net_tm_fini(&d1tm1));

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

void test_sunrpc_ping(void)
{
	struct ping_ctx cctx = {
		.pc_ops = &quiet_ops,
		.pc_xprt = &c2_net_bulk_mem_xprt,
		.pc_nr_bufs = PING_NR_BUFS,
		.pc_segments = PING_CLIENT_SEGMENTS,
		.pc_seg_size = PING_CLIENT_SEGMENT_SIZE,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};
	struct ping_ctx sctx = {
		.pc_ops = &quiet_ops,
		.pc_xprt = &c2_net_bulk_mem_xprt,
		.pc_nr_bufs = PING_NR_BUFS,
		.pc_segments = PING_SERVER_SEGMENTS,
		.pc_seg_size = PING_SERVER_SEGMENT_SIZE,
		.pc_tm = {
			.ntm_state     = C2_NET_TM_UNDEFINED
		}
	};

	c2_mutex_init(&sctx.pc_mutex);
	c2_cond_init(&sctx.pc_cond);
	c2_mutex_init(&cctx.pc_mutex);
	c2_cond_init(&cctx.pc_cond);

	C2_UT_ASSERT(c2_net_xprt_init(&c2_net_bulk_mem_xprt) == 0);

	int                      rc;
	struct c2_net_end_point *server_ep;
	struct c2_thread	 server_thread;

	C2_UT_ASSERT(ping_client_init(&cctx, &server_ep) == 0);
	/* client times out because server is not ready */
	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, NULL) != 0);
	/* server runs in background thread */
	C2_SET0(&server_thread);
	rc = C2_THREAD_INIT(&server_thread, struct ping_ctx *, NULL,
			    &ping_server, &sctx);
	if (rc != 0) {
		C2_UT_FAIL("failed to start ping server");
		return;
	} else
		C2_UT_PASS("started ping server");

	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, NULL) == 0);
	C2_UT_ASSERT(ping_client_passive_recv(&cctx, server_ep) == 0);
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep, NULL) == 0);

	/* test sending/receiving a bigger payload */
	int i;
	char *data = c2_alloc(PING_CLIENT_SEGMENTS * PING_CLIENT_SEGMENT_SIZE);
	int len = (PING_CLIENT_SEGMENTS-1) * PING_CLIENT_SEGMENT_SIZE + 1;

	for (i = 0; i < len; ++i)
		data[i] = "abcdefghi"[i % 9];
	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, data) == 0);
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep, data) == 0);

	C2_UT_ASSERT(ping_client_fini(&cctx, server_ep) == 0);

	ping_server_should_stop(&sctx);
	C2_UT_ASSERT(c2_thread_join(&server_thread) == 0);

	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
	c2_cond_fini(&sctx.pc_cond);
	c2_mutex_fini(&sctx.pc_mutex);
	c2_net_xprt_fini(&c2_net_bulk_mem_xprt);
}

const struct c2_test_suite net_bulk_sunrpc_ut = {
        .ts_name = "net-bulk-sunrpc",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_sunrpc_ep",         test_sunrpc_ep },
                { "net_bulk_sunrpc_desc",       test_sunrpc_desc },
                { "net_bulk_sunrpc_ping_tests", test_sunrpc_ping },
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
