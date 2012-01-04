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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 09/29/2011
 */

#include "lib/ut.h"
#include "lib/list.h"
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "ioservice/io_fops.h"	/* c2_io_fop */

#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "rpc/rpc2.h"	/* c2_rpc_bulk, c2_rpc_bulk_buf */
#include "rpc/rpclib.h"	/* c2_rpc_ctx */
#include "reqh/reqh.h"	/* c2_reqh */
#include "net/net.h"	/* C2_NET_QT_PASSICE_BULK_SEND */
#include "ut/rpc.h"	/* c2_rpc_client_init, c2_rpc_server_init */
#include "lib/processor.h"	/* c2_processors_init */
#include "fop/fop.h"
#include "fop/fop_base.h"
#include "lib/thread.h"	/* C2_THREAD_INIT */
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */

#include "ioservice/io_fops.c"	/* To access static apis for testing. */

enum IO_UT_VALUES {
	IO_KERN_PAGES = 1,
	IO_FIDS_NR = 4,
	IO_SEGS_NR = 128,
	IO_FOPS_NR = 32,
	IO_SEG_SIZE = 4096,
	IO_SEG_START_OFFSET = IO_SEG_SIZE * IO_SEGS_NR * IO_FOPS_NR,
	IO_CLIENT_COBDOM_ID = 21,
	IO_SERVER_COBDOM_ID = 29,
	IO_RPC_SESSION_SLOTS = 8,
	IO_RPC_MAX_IN_FLIGHT = 32,
	IO_RPC_CONN_TIMEOUT = 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);

/* Fids of global files. */
static struct c2_fop_file_fid	  io_fids[IO_FIDS_NR];

/* Tracks offsets for global fids. */
static uint64_t			  io_offsets[IO_FIDS_NR];

/* In-memory io fops. */
static struct c2_io_fop		**io_fops;

/* Buffers that will be part of io vectors in io fops. */
static struct c2_buf		  io_cbufs[IO_FIDS_NR];

/* Threads to post rpc items to rpc layer. */
static struct c2_thread		  io_threads[IO_FOPS_NR];

/* Request handler for io service on server side. */
static struct c2_reqh		  io_reqh;

static struct c2_dbenv		  c_dbenv;
static struct c2_dbenv		  s_dbenv;

static struct c2_cob_domain	  c_cbdom;
static struct c2_cob_domain	  s_cbdom;

static char			  s_endp_addr[] = "127.0.0.1:23123:1";
static char			  c_endp_addr[] = "127.0.0.1:23123:2";
static char			  s_db_name[]	= "bulk_s_db";
static char			  c_db_name[]	= "bulk_c_db";

extern struct c2_net_xprt 	  c2_net_bulk_sunrpc_xprt;
static struct c2_net_domain	  io_netdom;
static struct c2_net_xprt	 *xprt = &c2_net_bulk_sunrpc_xprt;

/* Rpc context structures for client and server. */
struct c2_rpc_ctx		s_rctx = {
	.rx_net_dom		= &io_netdom,
	.rx_reqh		= &io_reqh,
	.rx_local_addr		= s_endp_addr,
	.rx_remote_addr		= c_endp_addr,
	.rx_db_name		= s_db_name,
	.rx_dbenv		= &s_dbenv,
	.rx_cob_dom_id		= IO_SERVER_COBDOM_ID,
	.rx_cob_dom		= &s_cbdom,
	.rx_nr_slots		= IO_RPC_SESSION_SLOTS,
	.rx_max_rpcs_in_flight	= IO_RPC_MAX_IN_FLIGHT,
	.rx_timeout_s		= IO_RPC_CONN_TIMEOUT,
};

struct c2_rpc_ctx		c_rctx = {
	.rx_net_dom		= &io_netdom,
	.rx_reqh		= NULL,
	.rx_local_addr		= c_endp_addr,
	.rx_remote_addr		= s_endp_addr,
	.rx_db_name		= c_db_name,
	.rx_dbenv		= &c_dbenv,
	.rx_cob_dom_id		= IO_CLIENT_COBDOM_ID,
	.rx_cob_dom		= &c_cbdom,
	.rx_nr_slots		= IO_RPC_SESSION_SLOTS,
	.rx_max_rpcs_in_flight	= IO_RPC_MAX_IN_FLIGHT,
	.rx_timeout_s		= IO_RPC_CONN_TIMEOUT,
};


/* Dummy io fom object. Used for bulk IO client UT.
   !!This should be removed after bulk IO server UT code is in place!! */
struct io_fop_dummy_fom {
	struct c2_fom df_fom;
};

static int io_fop_dummy_fom_init(struct c2_fop *fop, struct c2_fom **m);

/* An alternate io fop type op vector to test bulk client functionality only.
   Only .fto_fom_init is pointed to a UT function which tests the received
   io fop is sane and bulk io transfer is taking place properly using data
   from io fop. Rest all ops are same as io_fop_rwv_ops.
   !! This whole block of code should be removed after bulk IO server UT
   code is in place!! */
struct c2_fop_type_ops bulkio_fop_ut_ops = {
	.fto_fom_init = io_fop_dummy_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

static struct c2_fom_type_ops bulkio_fom_type_ops = {
	.fto_create = NULL,
};

static struct c2_fom_type bulkio_fom_type = {
	.ft_ops = &bulkio_fom_type_ops,
};

static void bulkio_fom_fini(struct c2_fom *fom)
{
	struct io_fop_dummy_fom *fom_ctx;

	fom_ctx = container_of(fom, struct io_fop_dummy_fom, df_fom);
	c2_free(fom_ctx);
}

static int bulkio_fom_state(struct c2_fom *fom)
{
	int			  rc;
	uint32_t		  i;
	uint32_t		  j;
	uint64_t		  cmp;
	c2_bcount_t		  tc;
	struct c2_fop		 *fop;
	struct c2_clink		  clink;
	struct c2_fop_cob_writev *wfop;
	struct c2_fop_cob_rw	 *rw;
	struct c2_net_buffer	**nbufs;
	struct c2_io_indexvec	 *ivec;
	struct c2_net_buf_desc	 *desc;
	struct c2_rpc_bulk	 *rbulk;
	struct c2_rpc_bulk_buf	 *rbuf;
	struct c2_rpc_conn	 *conn;
	struct c2_fop_cob_writev_rep *fop_rep;

	printf("Entering fom state\n");
	conn = fom->fo_fop->f_item.ri_session->s_conn;
	wfop = c2_fop_data(fom->fo_fop);
	rw = &wfop->c_rwv;
	C2_UT_ASSERT(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);

	C2_ALLOC_ARR(nbufs, rw->crw_desc.id_nr);
	C2_UT_ASSERT(nbufs != NULL);

	C2_ALLOC_PTR(rbulk);
	C2_UT_ASSERT(rbulk != NULL);
	c2_rpc_bulk_init(rbulk);
	printf("No of descriptors = %d\n", rw->crw_desc.id_nr);
	C2_UT_ASSERT(rw->crw_desc.id_nr != 0);

	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i)
		C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs[0].ci_iosegs[i].
				ci_count == 4096);

	for (tc = 0, i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];
		desc = &rw->crw_desc.id_descs[i];

		C2_ALLOC_PTR(nbufs[i]);
		C2_UT_ASSERT(nbufs[i] != NULL);

		rc = c2_bufvec_alloc(&nbufs[i]->nb_buffer, ivec->ci_nr,
				     ivec->ci_iosegs[0].ci_count);
		C2_UT_ASSERT(rc == 0);

		nbufs[i]->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 ivec->ci_iosegs[0].ci_count,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 &rbuf);

		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf != NULL);

		rbuf->bb_nbuf = *nbufs[i];
		tc += c2_vec_count(&nbufs[i]->nb_buffer.ov_vec);
	}
	c2_clink_init(&clink, NULL);
	c2_clink_add(&rbulk->rb_chan, &clink);
	rc = c2_rpc_bulk_load(rbulk, conn, rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&clink);

	/* Makes sure that list of buffers in c2_rpc_bulk is empty. */
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);

	rc = rbulk->rb_rc;
	C2_UT_ASSERT(rc == 0);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	/* Checks if the bulk data is received as is. */
	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		cmp = wfop->c_rwv.crw_flags;
		for (j = 0; j < nbufs[i]->nb_buffer.ov_vec.v_nr; ++j) { 
			rc = memcmp(io_cbufs[cmp].b_addr,
				    nbufs[i]->nb_buffer.ov_buf[j],
				    nbufs[i]->nb_buffer.ov_vec.v_count[j]);
			C2_UT_ASSERT(rc == 0);
		}
	}

	fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);

	fop_rep = c2_fop_data(fop);
	fop_rep->c_rep.rwr_rc = rbulk->rb_rc;
	fop_rep->c_rep.rwr_count = tc;
	fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fop->f_item);
	C2_UT_ASSERT(rc == 0);

	fom->fo_phase = FOPH_FINISH;
	/* Deallocates net buffers and c2_buvec structures. */
	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		c2_bufvec_free(&nbufs[i]->nb_buffer);
		c2_free(nbufs[i]);
	}
	c2_free(nbufs);
	c2_rpc_bulk_fini(rbulk);
	c2_free(rbulk);

	printf("Exiting fom state\n");
	return rc;
}

static size_t bulkio_fom_locality(const struct c2_fom *fom)
{
	return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static struct c2_fom_ops bulkio_fom_ops = {
	.fo_fini = bulkio_fom_fini,
	.fo_state = bulkio_fom_state,
	.fo_home_locality = bulkio_fom_locality,
};

static int io_fop_dummy_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom		*fom;
	struct io_fop_dummy_fom *fom_ctx;

	C2_ALLOC_PTR(fom_ctx);
	C2_UT_ASSERT(fom_ctx != NULL);

	fom = &fom_ctx->df_fom;
	fom->fo_fop = fop;
	c2_fom_init(fom);
	fop->f_type->ft_fom_type.ft_ops = &bulkio_fom_type_ops;
	fom->fo_type = &bulkio_fom_type;
	fom->fo_ops = &bulkio_fom_ops;

	*m = fom;
	return 0;
}

static void io_fids_init(void)
{
	int i;

	/* Populates fids. */
	for (i = 0; i < IO_FIDS_NR; ++i) {
		io_fids[i].f_seq = i;
		io_fids[i].f_oid = i;
	}
}

/* A fixed set of buffers are used to populate io fops. */
static void io_buffers_allocate(void)
{
	int i;

	for (i = 0; i < IO_FIDS_NR; ++i) {
		io_cbufs[i].b_addr = c2_alloc_aligned(IO_SEG_SIZE,
						      C2_0VEC_SHIFT);
		C2_UT_ASSERT(io_cbufs[i].b_addr != NULL);
		io_cbufs[i].b_nob = IO_SEG_SIZE;
		memset(io_cbufs[i].b_addr, 'a', io_cbufs[i].b_nob);
	}
}

static void io_buffers_deallocate(void)
{
	int i;

	for (i = 0; i < IO_FIDS_NR; ++i) {
		c2_free(io_cbufs[i].b_addr);
	}
}

static void io_fop_populate(struct c2_io_fop *iofop, int index)
{
	int			 i;
	int			 rc;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*rw;

	rbulk = &iofop->if_rbulk;
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR, IO_SEG_SIZE, &io_netdom,
				 &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);
	rw = io_rw_get(&iofop->if_fop);

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < IO_SEGS_NR; ++i) {
		rc = c2_rpc_bulk_buf_usrbuf_add(rbuf, io_cbufs[index].b_addr,
						io_cbufs[index].b_nob,
						io_offsets[index]);
		C2_UT_ASSERT(rc == 0);
		io_offsets[index] -= io_cbufs[index].b_nob;
	}
	rbuf->bb_nbuf.nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;

	/* Allocates memory for array of net buf descriptors and array of
	   index vectors from io fop. */
	rc = io_fop_ivec_alloc(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);
	rc = io_fop_desc_alloc(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);
	io_fop_ivec_prepare(&iofop->if_fop);

	rc = c2_rpc_bulk_store(rbulk, &c_rctx.rx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);

	rw->crw_flags = index;
	
	/* Temporary! Should be removed once bulk server UT code is merged
	   with this code. */
	rw->crw_iovec.iv_count = 1;
	C2_ALLOC_ARR(rw->crw_iovec.iv_segs, rw->crw_iovec.iv_count);
	C2_UT_ASSERT(rw->crw_iovec.iv_segs != NULL);
	rw->crw_iovec.iv_segs[0].is_offset = 0;
	rw->crw_iovec.iv_segs[0].is_buf.ib_count = 8;
	C2_ALLOC_ARR(rw->crw_iovec.iv_segs[0].is_buf.ib_buf,
		     rw->crw_iovec.iv_segs[0].is_buf.ib_count);
}

static void io_fops_create(void)
{
	int			  i;
	int			  rc;
	uint64_t		  seed;
	uint64_t		  rnd;
	struct c2_fop_cob_writev *iofop;

	for (i = 0; i < IO_FIDS_NR; ++i)
		io_offsets[i] = IO_SEG_START_OFFSET;

	C2_ALLOC_ARR(io_fops, IO_FOPS_NR);
	C2_UT_ASSERT(io_fops != NULL);

	/* Allocates io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		/* Since read and write fops are similar and the io coalescing
		   code is same for read and write fops, it doesn't
		   matter what type of fop is created. */

		C2_ALLOC_PTR(io_fops[i]);
		C2_UT_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], &c2_fop_cob_writev_fopt);
		C2_UT_ASSERT(rc == 0);
		io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ut_ops;
	}

	/* Populates io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		iofop = c2_fop_data(&io_fops[i]->if_fop);
		rnd = c2_rnd(IO_FIDS_NR, &seed);
		C2_UT_ASSERT(rnd < IO_FIDS_NR);
		iofop->c_rwv.crw_fid = io_fids[rnd];

		io_fop_populate(io_fops[i], rnd);
	}
}

static void io_fops_destroy(void)
{
	int i;
	struct c2_fop_cob_rw *rw;

	for (i = 0; i < IO_FOPS_NR; ++i) {
		rw = io_rw_get(&io_fops[i]->if_fop);
		io_fop_ivec_dealloc(&io_fops[i]->if_fop);
		io_fop_desc_dealloc(&io_fops[i]->if_fop);
		c2_free(rw->crw_iovec.iv_segs[0].is_buf.ib_buf);
		c2_free(rw->crw_iovec.iv_segs);
		/* XXX Can not deallocate fops from here since sessions code
		   tries to free fops and faults in c2_rpc_slot_fini.
		   And in cases, the program will run in an endless loop
		   in nr_active_items_count() which is called from
		   c2_rpc_session_terminate() if fops are deallocated here. */
		//c2_io_fop_fini(io_fops[i]);
		//c2_free(io_fops[i]);
		C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &io_fops[i]->
			     if_rbulk.rb_buflist));
	}
	c2_free(io_fops);
}

static void io_fops_rpc_submit(int i)
{
	int				 rc;
	c2_time_t			 timeout;
	struct c2_clink			 clink;
	struct c2_rpc_item		*item;
	struct c2_fop_cob_writev	*wfop;

	item = &io_fops[i]->if_fop.f_item;
	item->ri_session = &c_rctx.rx_session;
	c2_time_set(&timeout, 300, 0);
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	timeout = c2_time_add(timeout, c2_time_now());
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	wfop = c2_fop_data(&io_fops[i]->if_fop);
	//wfop->c_rwv.crw_flags = i;

	/* Posts the rpc item and waits until reply is received. */
	rc = c2_rpc_post(item);
	printf("Item %d posted to rpc.\n", i);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_reply_timedwait(&clink, timeout);
	if (rc != 0)
		printf("Rpc item post failed. rc = %d\n", rc);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

void bulkio_test(void)
{
	int rc;
	int i;

	C2_SET0(&io_reqh);
	rc = c2_processors_init();
	C2_UT_ASSERT(rc == 0);

	/* Start an rpc server and an rpc client. */
	rc = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&io_netdom, xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_reqh_init(&io_reqh, NULL, NULL, s_rctx.rx_dbenv,
			  s_rctx.rx_cob_dom, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_server_init(&s_rctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_client_init(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	memset(&io_threads, 0, ARRAY_SIZE(io_threads) *
	       sizeof(struct c2_thread));

	io_fids_init();
	io_buffers_allocate();
	io_fops_create();
	for (i = 0; i < ARRAY_SIZE(io_threads); ++i) {
		rc = C2_THREAD_INIT(&io_threads[i], int, NULL,
				    &io_fops_rpc_submit, i, "io_thrd%d", i);
		C2_UT_ASSERT(rc == 0);
	}

	/* Waits till all threads finish their job. */
	for (i = 0; i < ARRAY_SIZE(io_threads); ++i)
		c2_thread_join(&io_threads[i]);

	io_buffers_deallocate();
	io_fops_destroy();

	rc = c2_rpc_client_fini(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_fini(&s_rctx);

	c2_reqh_fini(&io_reqh);

	c2_net_domain_fini(&io_netdom);

	c2_net_xprt_fini(xprt);
}

const struct c2_test_suite bulkio_ut = {
	.ts_name = "bulkio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bulkio", bulkio_test},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
