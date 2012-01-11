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

#include "rpc/rpc2.h"		/* c2_rpc_bulk, c2_rpc_bulk_buf */
#include "rpc/rpclib.h"		/* c2_rpc_ctx */
#include "reqh/reqh.h"		/* c2_reqh */
#include "net/net.h"		/* C2_NET_QT_PASSICE_BULK_SEND */
#include "ut/rpc.h"		/* c2_rpc_client_init, c2_rpc_server_init */
#include "lib/processor.h"	/* c2_processors_init */
#include "fop/fop.h"
#include "fop/fop_base.h"
#include "lib/thread.h"		/* C2_THREAD_INIT */
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */
#include "lib/misc.h"		/* C2_SET_ARR0 */

#include "ioservice/io_fops.c"	/* To access static apis for testing. */

enum IO_UT_VALUES {
	IO_KERN_PAGES		= 1,
	IO_FIDS_NR		= 4,
	IO_SEGS_NR		= 128,
	IO_SEQ_LEN		= 8,
	IO_FOPS_NR		= 32,
	IO_SEG_SIZE		= 4096,
	IO_RPC_ITEM_TIMEOUT	= 300,
	IO_SEG_START_OFFSET	= IO_SEG_SIZE * IO_SEGS_NR * IO_FOPS_NR,
	IO_CLIENT_COBDOM_ID	= 21,
	IO_SERVER_COBDOM_ID	= 29,
	IO_RPC_SESSION_SLOTS	= 8,
	IO_RPC_MAX_IN_FLIGHT	= 32,
	IO_RPC_CONN_TIMEOUT	= 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);

/* Fids of global files. */
static struct c2_fop_file_fid	  io_fids[IO_FIDS_NR];

/* Tracks offsets for global fids. */
static uint64_t			  io_offsets[IO_FIDS_NR];

/* In-memory io fops. */
static struct c2_io_fop		**io_fops;

/* Read buffers to which data will be transferred. */
static struct c2_net_buffer	  io_buf[IO_FOPS_NR];

/* Threads to post rpc items to rpc layer. */
static struct c2_thread		  io_threads[IO_FOPS_NR];

/* A standard buffer containing a data pattern.
   Primarily used for data verification. */
static char			  io_cbuf[IO_SEG_SIZE];

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

extern struct c2_net_xprt	  c2_net_bulk_sunrpc_xprt;
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
	c2_free(fom);
}

static int bulkio_fom_state(struct c2_fom *fom)
{
	int				 rc;
	uint32_t			 i;
	uint32_t			 j;
	uint64_t			 cmp;
	c2_bcount_t			 tc;
	struct c2_fop			*fop;
	struct c2_clink			 clink;
	struct c2_bufvec		**bvecs;
	struct c2_fop_cob_rw		*rw;
	struct c2_io_indexvec		*ivec;
	struct c2_net_buf_desc		*desc;
	struct c2_rpc_bulk		*rbulk;
	struct c2_rpc_bulk_buf		*rbuf;
	struct c2_rpc_conn		*conn;
	struct c2_fop_cob_writev_rep	*wrep;
	struct c2_fop_cob_readv_rep	*rrep;

	printf("Entering fom state\n");
	conn = fom->fo_fop->f_item.ri_session->s_conn;
	rw = io_rw_get(fom->fo_fop);
	C2_UT_ASSERT(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);

	C2_ALLOC_ARR(bvecs, rw->crw_desc.id_nr);
	C2_UT_ASSERT(bvecs != NULL);

	C2_ALLOC_PTR(rbulk);
	C2_UT_ASSERT(rbulk != NULL);
	c2_rpc_bulk_init(rbulk);
	printf("No of descriptors = %d\n", rw->crw_desc.id_nr);
	C2_UT_ASSERT(rw->crw_desc.id_nr != 0);

	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i)
		C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs[0].ci_iosegs[i].
			     ci_count == IO_SEG_SIZE);

	for (tc = 0, i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];
		desc = &rw->crw_desc.id_descs[i];

		C2_ALLOC_PTR(bvecs[i]);
		C2_UT_ASSERT(bvecs[i] != NULL);

		rc = c2_bufvec_alloc(bvecs[i], ivec->ci_nr,
				     ivec->ci_iosegs[0].ci_count);
		C2_UT_ASSERT(rc == 0);

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 ivec->ci_iosegs[0].ci_count,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 &rbuf);

		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf != NULL);

		rbuf->bb_nbuf.nb_buffer = *bvecs[i];
		rbuf->bb_nbuf.nb_qtype = is_write(fom->fo_fop) ?
					 C2_NET_QT_ACTIVE_BULK_RECV :
					 C2_NET_QT_ACTIVE_BULK_SEND;
		tc += c2_vec_count(&bvecs[i]->ov_vec);

		if (is_read(fom->fo_fop)) {
			for (j = 0; j < ivec->ci_nr; ++j)
				/* Sets a pattern in data buffer so that
				   it can be verified at other side. */
				memset(bvecs[i]->ov_buf[j], 'b',
				       ivec->ci_iosegs[j].ci_count);
		}
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

	/* Checks if the write io bulk data is received as is. */
	for (i = 0; i < rw->crw_desc.id_nr && is_write(fom->fo_fop); ++i) {
		cmp = rw->crw_flags;
		for (j = 0; j < bvecs[i]->ov_vec.v_nr; ++j) {
			rc = memcmp(io_buf[cmp].nb_buffer.ov_buf[j],
				    bvecs[i]->ov_buf[j],
				    bvecs[i]->ov_vec.v_count[j]);
			C2_UT_ASSERT(rc == 0);
		}
	}

	if (is_write(fom->fo_fop)) {
		fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
		wrep = c2_fop_data(fop);
		wrep->c_rep.rwr_rc = rbulk->rb_rc;
		wrep->c_rep.rwr_count = tc;
	} else {
		fop = c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL);
		rrep = c2_fop_data(fop);
		rrep->c_rep.rwr_rc = rbulk->rb_rc;
		rrep->c_rep.rwr_count = tc;
		rrep->c_iobuf.ib_count = IO_SEQ_LEN;
		C2_ALLOC_ARR(rrep->c_iobuf.ib_buf, rrep->c_iobuf.ib_count);
	}
	C2_UT_ASSERT(fop != NULL);

	fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fop->f_item);
	C2_UT_ASSERT(rc == 0);

	fom->fo_phase = FOPH_FINISH;
	/* Deallocates net buffers and c2_buvec structures. */
	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		c2_bufvec_free(bvecs[i]);
		c2_free(bvecs[i]);
	}
	c2_free(bvecs);
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
	struct c2_fom *fom;

	C2_ALLOC_PTR(fom);
	C2_UT_ASSERT(fom != NULL);

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
	int		  i;
	int		  j;
	struct c2_bufvec *buf;

	/* Initialized the standard buffer with a data pattern. */
	memset(io_cbuf, 'b', IO_SEG_SIZE);

	C2_SET_ARR0(io_buf);
	for (i = 0; i < IO_FOPS_NR; ++i) {
		/* Zero vector needs buffers aligned on 4k boundary.
		   Hence c2_bufvec_alloc can not be used. */
		buf = &io_buf[i].nb_buffer;
		buf->ov_vec.v_nr = IO_SEGS_NR;
		C2_ALLOC_ARR(buf->ov_vec.v_count, IO_SEGS_NR);
		C2_UT_ASSERT(buf->ov_vec.v_count != NULL);
		C2_ALLOC_ARR(buf->ov_buf, IO_SEGS_NR);
		C2_UT_ASSERT(buf->ov_buf != NULL);
		for (j = 0; j < IO_SEGS_NR; ++j) {
			buf->ov_buf[j] = c2_alloc_aligned(IO_SEG_SIZE,
							  C2_0VEC_SHIFT);
			C2_UT_ASSERT(buf->ov_buf[j] != NULL);
			buf->ov_vec.v_count[j] = IO_SEG_SIZE;
			memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
		}
	}
}

static void io_buffers_deallocate(void)
{
	int i;

	for (i = 0; i < IO_FOPS_NR; ++i)
		c2_bufvec_free(&io_buf[i].nb_buffer);
}

static void io_fop_populate(int index, uint64_t off_index,
			    enum C2_RPC_OPCODES op)
{
	int			 i;
	int			 rc;
	struct c2_io_fop	*iofop;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*rw;

	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR, IO_SEG_SIZE, &io_netdom,
				 &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	rw = io_rw_get(&iofop->if_fop);
	rw->crw_fid = io_fids[index];

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < IO_SEGS_NR; ++i) {
		rc = c2_rpc_bulk_buf_usrbuf_add(rbuf,
				io_buf[index].nb_buffer.ov_buf[i],
				io_buf[index].nb_buffer.ov_vec.v_count[i],
				io_offsets[off_index]);
		C2_UT_ASSERT(rc == 0);

		io_offsets[off_index] -=
			io_buf[index].nb_buffer.ov_vec.v_count[i];
	}

	rbuf->bb_nbuf.nb_qtype = (op == C2_IOSERVICE_WRITEV_OPCODE) ?
		C2_NET_QT_PASSIVE_BULK_SEND : C2_NET_QT_PASSIVE_BULK_RECV;

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
	rw->crw_iovec.iv_segs[0].is_buf.ib_count = IO_SEQ_LEN;
	C2_ALLOC_ARR(rw->crw_iovec.iv_segs[0].is_buf.ib_buf,
		     rw->crw_iovec.iv_segs[0].is_buf.ib_count);
}

static void io_fops_create(enum C2_RPC_OPCODES op)
{
	int			  i;
	int			  rc;
	uint64_t		  seed;
	uint64_t		  rnd;
	struct c2_fop_type	 *fopt;

	for (i = 0; i < IO_FIDS_NR; ++i)
		io_offsets[i] = IO_SEG_START_OFFSET;

	C2_ALLOC_ARR(io_fops, IO_FOPS_NR);
	C2_UT_ASSERT(io_fops != NULL);

	fopt = (op == C2_IOSERVICE_WRITEV_OPCODE) ? &c2_fop_cob_writev_fopt :
	       &c2_fop_cob_readv_fopt;

	/* Allocates io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		/* Since read and write fops are similar and the io coalescing
		   code is same for read and write fops, it doesn't
		   matter what type of fop is created. */

		C2_ALLOC_PTR(io_fops[i]);
		C2_UT_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], fopt);
		C2_UT_ASSERT(rc == 0);
		io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ut_ops;
	}

	/* Populates io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		rnd = c2_rnd(IO_FIDS_NR, &seed);
		C2_UT_ASSERT(rnd < IO_FIDS_NR);

		io_fop_populate(i, rnd, op);
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
	int			 j;
	int			 rc;
	c2_time_t		 timeout;
	struct c2_clink		 clink;
	struct c2_rpc_item	*item;
	struct c2_rpc_bulk	*rbulk;

	rbulk = c2_fop_to_rpcbulk(&io_fops[i]->if_fop);
	item = &io_fops[i]->if_fop.f_item;
	item->ri_session = &c_rctx.rx_session;
	c2_time_set(&timeout, IO_RPC_ITEM_TIMEOUT, 0);
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	timeout = c2_time_add(timeout, c2_time_now());
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	/* Posts the rpc item and waits until reply is received. */
	rc = c2_rpc_post(item);
	printf("Item %d posted to rpc.\n", i);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_reply_timedwait(&clink, timeout);
	if (rc != 0)
		printf("Rpc item post failed. rc = %d\n", rc);
	else if (is_read(&io_fops[i]->if_fop)) {
		for (j = 0; j < io_buf[i].nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(io_buf[i].nb_buffer.ov_buf[j], io_cbuf,
				    io_buf[i].nb_buffer.ov_vec.v_count[j]);
			C2_UT_ASSERT(rc == 0);
		}
		c2_mutex_lock(&rbulk->rb_mutex);
		C2_UT_ASSERT(rbulk->rb_rc == 0);
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

void bulkio_test(void)
{
	int		    rc;
	int		    i;
	enum C2_RPC_OPCODES op;

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

	io_fids_init();
	io_buffers_allocate();

	for (op = C2_IOSERVICE_READV_OPCODE; op <= C2_IOSERVICE_WRITEV_OPCODE;
	     ++op) {
		memset(&io_threads, 0, ARRAY_SIZE(io_threads) *
		       sizeof(struct c2_thread));

		io_fops_create(op);
		for (i = 0; i < ARRAY_SIZE(io_threads); ++i) {
			rc = C2_THREAD_INIT(&io_threads[i], int, NULL,
					&io_fops_rpc_submit, i, "io_thrd%d", i);
			C2_UT_ASSERT(rc == 0);
		}

		/* Waits till all threads finish their job. */
		for (i = 0; i < ARRAY_SIZE(io_threads); ++i)
			c2_thread_join(&io_threads[i]);

		io_fops_destroy();
	}
	io_buffers_deallocate();

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
