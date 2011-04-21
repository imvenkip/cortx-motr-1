/* -*- C -*- */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "lib/thread.h"
#include "lib/ut.h"

#include "net/bulk_emulation/mem_xprt_xo.c"
#include "net/bulk_emulation/st/ping.c"

void test_buf_copy(void)
{
	/* Create buffers with different shapes but same total size.
	   Also create identical buffers for exact shape testing.
	 */
	enum { NR_BUFS = 10 };
	static struct {
		uint32_t    num_segs;
		c2_bcount_t seg_size;
	} shapes[NR_BUFS] = {
		[0] = { 1, 48 },
		[1] = { 1, 48 },
		[2] = { 2, 24 },
		[3] = { 2, 24 },
		[4] = { 3, 16 },
		[5] = { 3, 16 },
		[6] = { 4, 12 },
		[7] = { 4, 12 },
		[8] = { 6,  8 },
		[9] = { 6,  8 },
	};
	static const char *msg = "abcdefghijklmnopqrstuvwxyz0123456789"
		"ABCDEFGHIJK";
	size_t msglen = strlen(msg)+1;
	struct c2_net_buffer bufs[NR_BUFS];
	int i;
	struct c2_net_buffer *nb;

	C2_SET_ARR0(bufs);
	for (i=0; i < NR_BUFS; i++) {
		C2_UT_ASSERT(msglen == shapes[i].num_segs * shapes[i].seg_size);
		C2_UT_ASSERT(c2_bufvec_alloc(&bufs[i].nb_buffer,
					     shapes[i].num_segs,
					     shapes[i].seg_size) == 0);
	}
	nb = &bufs[0]; /* single buffer */
	C2_UT_ASSERT(nb->nb_buffer.ov_vec.v_nr == 1);
	memcpy(nb->nb_buffer.ov_buf[0], msg, msglen);
	C2_UT_ASSERT(memcmp(nb->nb_buffer.ov_buf[0], msg, msglen) == 0);
	for (i=1; i < NR_BUFS; i++) {
		int j;
		const char *p = msg;
		C2_UT_ASSERT(mem_copy_buffer(&bufs[i],&bufs[i-1],msglen) == 0);
		C2_UT_ASSERT(bufs[i].nb_length == msglen);
		for (j=0; j<bufs[i].nb_buffer.ov_vec.v_nr; j++) {
			int k;
			char *q;
			for (k=0; k<bufs[i].nb_buffer.ov_vec.v_count[j]; k++){
				q = bufs[i].nb_buffer.ov_buf[j] + k;
				C2_UT_ASSERT(*p++ == *q);
			}
		}

	}
}

void test_failure(void)
{
	/* dom1 */
	struct c2_net_domain dom1;
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
	struct c2_net_buffer d1nb1;
	struct c2_net_buffer d1nb2;
	struct c2_clink tmwait1;

	/* dom 2 */
 	struct c2_net_domain dom2;
	enum c2_net_queue_type cb_qt2;
	struct c2_net_buffer *cb_nb2;
	struct c2_net_tm_callbacks cbs2 = {
		.ntc_event_cb = LAMBDA(void,(struct c2_net_transfer_mc *tm,
					     struct c2_net_event *ev){
					       cb_qt2 = ev->nev_qtype;
					       cb_nb2 = ev->nev_buffer;
				       }),
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

	/* setup the first dom */
	C2_UT_ASSERT(!c2_net_domain_init(&dom1, &c2_net_bulk_mem_xprt));
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 10, 0));
	C2_UT_ASSERT(!c2_net_tm_init(&d1tm1, &dom1));
	c2_clink_init(&tmwait1, NULL);
	c2_clink_add(&d1tm1.ntm_chan, &tmwait1);
	C2_UT_ASSERT(!c2_net_tm_start(&d1tm1, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait1);
	c2_clink_del(&tmwait1);
	C2_UT_ASSERT(d1tm1.ntm_state == C2_NET_TM_STARTED);
	C2_SET0(&d1nb1);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d1nb1.nb_buffer, 4, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d1nb1, &dom1));
	C2_SET0(&d1nb2);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d1nb2.nb_buffer, 1, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d1nb2, &dom1));

	/* setup the second dom */
	C2_UT_ASSERT(!c2_net_domain_init(&dom2, &c2_net_bulk_mem_xprt));
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm1, &dom2));
	/* don't start the TM on port 20 yet */
	C2_UT_ASSERT(!c2_net_tm_init(&d2tm2, &dom2));
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 21, 0));
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm2, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm2.ntm_state == C2_NET_TM_STARTED);

	C2_SET0(&d2nb1);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d2nb1.nb_buffer, 4, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d2nb1, &dom2));
	C2_SET0(&d2nb2);
	C2_UT_ASSERT(!c2_bufvec_alloc(&d2nb2.nb_buffer, 1, 10));
	C2_UT_ASSERT(!c2_net_buffer_register(&d2nb2, &dom2));

	/* test failure situations */

	/* TEST
	   Send a message from d1tm1 to d2tm2 - should fail because
	   the destination TM not started.
	*/
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 20, 0));
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
	C2_UT_ASSERT(d1nb1.nb_status == -ENETUNREACH);

	/* start the TM on port 20 in the second dom */
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 20, 0));
	C2_UT_ASSERT(!c2_net_tm_start(&d2tm1, ep));
	C2_UT_ASSERT(!c2_net_end_point_put(ep));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm1.ntm_state == C2_NET_TM_STARTED);


	/* TEST
	   Send a message from d1tm1 to d2tm2 - should fail because
	   no receive buffers available.
	*/
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom1,
					      "127.0.0.1", 20, 0));
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

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to actively send from an unauthorized dom
	*/
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 30, 0));
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

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

	C2_UT_ASSERT(!c2_net_buffer_del(&d2nb1, &d2tm1));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to actively receive from it.
	*/
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10, 0));
	d2nb1.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb1.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb1, &d2tm1));
	C2_UT_ASSERT(d2nb1.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

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

	C2_UT_ASSERT(!c2_net_buffer_del(&d2nb1, &d2tm1));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);

	/* TEST
	   Set up a passive receive buffer in one dom, and
	   try to send a larger message from the other dom.
	*/
	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10, 0));
	d2nb2.nb_qtype = C2_NET_QT_PASSIVE_BULK_RECV;
	d2nb2.nb_ep = ep;
	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm1.ntm_chan, &tmwait2);
	cb_qt2 = C2_NET_QT_NR;
	cb_nb2 = NULL;
	C2_UT_ASSERT(!c2_net_buffer_add(&d2nb2, &d2tm1));
	C2_UT_ASSERT(d2nb2.nb_desc.nbd_len != 0);
	C2_UT_ASSERT(!c2_net_end_point_put(ep));

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

	C2_UT_ASSERT(!c2_net_buffer_del(&d2nb2, &d2tm1));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb2);
	C2_UT_ASSERT(d2nb2.nb_status == -EFBIG);

	/* TEST
	   Setup a passive send buffer and add it. Save the descriptor in the
	   active buffer of the other dom.  Do not start the active operation
	   yet. Del the passive buffer. Re-submit the same buffer for the same
	   passive operation. Try the active operation in the other dom, using
	   the original desc. Should fail because buffer id changes per add.
	 */

	C2_UT_ASSERT(!c2_net_end_point_create(&ep, &dom2,
					      "127.0.0.1", 10, 0));
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
	C2_UT_ASSERT(!c2_net_buffer_del(&d2nb1, &d2tm1));
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

	C2_UT_ASSERT(!c2_net_buffer_del(&d2nb1, &d2tm1));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(cb_qt2 == C2_NET_QT_PASSIVE_BULK_RECV);
	C2_UT_ASSERT(cb_nb2 == &d2nb1);
	C2_UT_ASSERT(d2nb1.nb_status == -ECANCELED);

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

	c2_clink_init(&tmwait2, NULL);
	c2_clink_add(&d2tm2.ntm_chan, &tmwait2);
	C2_UT_ASSERT(!c2_net_tm_stop(&d2tm2, false));
	c2_chan_wait(&tmwait2);
	c2_clink_del(&tmwait2);
	C2_UT_ASSERT(d2tm2.ntm_state == C2_NET_TM_STOPPED);

	C2_UT_ASSERT(!c2_net_tm_fini(&d1tm1));
	C2_UT_ASSERT(!c2_net_tm_fini(&d2tm1));
	C2_UT_ASSERT(!c2_net_tm_fini(&d2tm2));

	c2_net_domain_fini(&dom1);
	c2_net_domain_fini(&dom2);
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

void test_ping(void)
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
	C2_UT_ASSERT(ping_client_passive_send(&cctx, server_ep) == 0);

	/* test sending/receiving a bigger payload */
	int i;
	char *data = c2_alloc(PING_CLIENT_SEGMENTS * PING_CLIENT_SEGMENT_SIZE);
	int len = (PING_CLIENT_SEGMENTS-1) * PING_CLIENT_SEGMENT_SIZE + 1;

	for (i = 0; i < len; ++i)
		data[i] = "abcdefghi"[i % 9];
	C2_UT_ASSERT(ping_client_msg_send_recv(&cctx, server_ep, data) == 0);

	C2_UT_ASSERT(ping_client_fini(&cctx, server_ep) == 0);

	ping_server_should_stop(&sctx);
	C2_UT_ASSERT(c2_thread_join(&server_thread) == 0);

	c2_cond_fini(&cctx.pc_cond);
	c2_mutex_fini(&cctx.pc_mutex);
	c2_cond_fini(&sctx.pc_cond);
	c2_mutex_fini(&sctx.pc_mutex);
	c2_net_xprt_fini(&c2_net_bulk_mem_xprt);
}

const struct c2_test_suite net_bulk_mem_ut = {
        .ts_name = "net-bulk-mem",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_mem_buf_copy_test", test_buf_copy },
                { "net_bulk_mem_failure_tests", test_failure },
                { "net_bulk_mem_ping_tests", test_ping },
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
