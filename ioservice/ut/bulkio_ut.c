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
#include "ut/cs_service.h"	/* ds1_service_type */
#include "fop/fop.h"
#include "fop/fop_base.h"
#include "lib/thread.h"		/* C2_THREAD_INIT */
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */
#include "lib/misc.h"		/* C2_SET_ARR0 */
#include "reqh/reqh_service.h"	/* c2_reqh_service */

#include "ioservice/io_fops.c"	/* To access static apis for testing. */

enum IO_UT_VALUES {
	IO_FIDS_NR		= 4,
	IO_SEGS_NR		= 128,
	IO_SEQ_LEN		= 8,
	IO_FOPS_NR		= 32,
	IO_SEG_SIZE		= 4096,
	IO_FID_SINGLE		= 1,
	IO_FOP_SINGLE		= 1,
	IO_XPRT_NR		= 1,
	IO_RPC_ITEM_TIMEOUT	= 300,
	IO_SEG_START_OFFSET	= IO_SEG_SIZE * IO_SEGS_NR * IO_FOPS_NR,
	IO_CLIENT_COBDOM_ID	= 21,
	IO_SERVER_COBDOM_ID	= 29,
	IO_RPC_SESSION_SLOTS	= 8,
	IO_RPC_MAX_IN_FLIGHT	= 32,
	IO_RPC_CONN_TIMEOUT	= 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);
extern const struct c2_net_buffer_callbacks rpc_bulk_cb;

static void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
		      c2_bcount_t seg_size);

/* Fids of global files. */
static struct c2_fop_file_fid	  io_fids[IO_FIDS_NR];

/* Tracks offsets for global fids. */
static uint64_t			  io_offsets[IO_FIDS_NR];

/* In-memory fops for read IO. */
static struct c2_io_fop		**rfops;

/* In-memory fops for write IO. */
static struct c2_io_fop		**wfops;

/* Read buffers to which data will be transferred. */
static struct c2_net_buffer	  io_buf[IO_FOPS_NR];

/* Threads to post rpc items to rpc layer. */
static struct c2_thread		  io_threads[IO_FOPS_NR];

/*
 * Standard buffers containing a data pattern.
 * Primarily used for data verification in read and write IO.
 */
static char			  readbuf[IO_SEG_SIZE];
static char			  writebuf[IO_SEG_SIZE];

/* A structure used to pass as argument to io threads. */
struct thrd_arg {
	/* Index in fops array to be posted to rpc layer. */
	int			ta_index;
	/* Type of fop to be sent (read/write). */
	enum C2_RPC_OPCODES	ta_op;
};

static struct c2_dbenv		  c_dbenv;

static struct c2_cob_domain	  c_cbdom;

static char			  c_endp_addr[] = "127.0.0.1:23134:2";
static char			  c_db_name[]	= "bulk_c_db";
static char			  s_db_file[]	= "bulkio_ut.db";
static char			  s_stob_file[]	= "bulkio_ut_stob";
static char			  s_log_file[]	= "bulkio_ut.log";

#define S_ENDP_ADDR		  "127.0.0.1:23134:1"
#define S_ENDPOINT		  "bulk-sunrpc:"S_ENDP_ADDR

extern struct c2_net_xprt	  c2_net_bulk_sunrpc_xprt;

/* Net domain for rpc client. */
static struct c2_net_domain	  c_netdom;
static struct c2_net_xprt	 *xprt = &c2_net_bulk_sunrpc_xprt;

struct c2_rpc_client_ctx c_rctx = {
	.rcx_net_dom		= &c_netdom,
	.rcx_local_addr		= c_endp_addr,
	.rcx_remote_addr	= S_ENDP_ADDR,
	.rcx_db_name		= c_db_name,
	.rcx_dbenv		= &c_dbenv,
	.rcx_cob_dom_id		= IO_CLIENT_COBDOM_ID,
	.rcx_cob_dom		= &c_cbdom,
	.rcx_nr_slots		= IO_RPC_SESSION_SLOTS,
	.rcx_max_rpcs_in_flight	= IO_RPC_MAX_IN_FLIGHT,
	.rcx_timeout_s		= IO_RPC_CONN_TIMEOUT,
};

/* Input arguments for colibri server setup. */
char *server_args[]		= {"bulkio_ut", "-r", "-T", "AD", "-D",
				   s_db_file, "-S", s_stob_file, "-e",
				   S_ENDPOINT, "-s", "ds1", "-s", "ds2"};

/* Array of services to be started by colibri server. */
struct c2_reqh_service_type *stypes[] = {
	&ds1_service_type,
	&ds2_service_type,
};

/*
 * Colibri server rpc context. Can't use C2_RPC_SERVER_CTX_DECLARE_SIMPLE()
 * since it limits the scope of struct c2_rpc_server_ctx to the function
 * where it is declared.
 */
struct c2_rpc_server_ctx s_rctx = {
	.rsx_xprts		= &xprt,
	.rsx_xprts_nr		= IO_XPRT_NR,
	.rsx_argv		= server_args,
	.rsx_argc		= ARRAY_SIZE(server_args),
	.rsx_service_types	= stypes,
	.rsx_service_types_nr	= ARRAY_SIZE(stypes),
	.rsx_log_file_name	= s_log_file,
};

static int io_fop_dummy_fom_init(struct c2_fop *fop, struct c2_fom **m);

/*
 * An alternate io fop type op vector to test bulk client functionality only.
 * Only .fto_fom_init is pointed to a UT function which tests the received
 * io fop is sane and bulk io transfer is taking place properly using data
 * from io fop. Rest all ops are same as io_fop_rwv_ops.
 * !! This whole block of code should be removed after bulk IO server UT
 * code is in place!!
 */
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
	c2_fom_fini(fom);
	c2_free(fom);
}

static int bulkio_fom_state(struct c2_fom *fom)
{
	int				 rc;
	uint32_t			 i;
	uint32_t			 j;
	uint32_t			 k;
	c2_bcount_t			 tc;
	struct c2_fop			*fop;
	struct c2_clink			 clink;
	struct c2_net_buffer		**netbufs;
	struct c2_fop_cob_rw		*rw;
	struct c2_io_indexvec		*ivec;
	struct c2_rpc_bulk		*rbulk;
	struct c2_rpc_bulk_buf		*rbuf;
	struct c2_rpc_conn		*conn;
	struct c2_fop_cob_writev_rep	*wrep;
	struct c2_fop_cob_readv_rep	*rrep;

	conn = fom->fo_fop->f_item.ri_session->s_conn;
	rw = io_rw_get(fom->fo_fop);
	C2_UT_ASSERT(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);

	C2_ALLOC_ARR(netbufs, rw->crw_desc.id_nr);
	C2_UT_ASSERT(netbufs != NULL);

	C2_ALLOC_PTR(rbulk);
	C2_UT_ASSERT(rbulk != NULL);
	c2_rpc_bulk_init(rbulk);
	C2_UT_ASSERT(rw->crw_desc.id_nr != 0);

	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i)
		for (j = 0; j < rw->crw_ivecs.cis_ivecs[i].ci_nr; ++j)
			C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs[i].ci_iosegs[j].
			     ci_count == IO_SEG_SIZE);

	for (tc = 0, i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];

		C2_ALLOC_PTR(netbufs[i]);
		C2_UT_ASSERT(netbufs[i] != NULL);

		vec_alloc(&netbufs[i]->nb_buffer, ivec->ci_nr,
			  ivec->ci_iosegs[0].ci_count);

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 netbufs[i], &rbuf);

		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf != NULL);

		rbuf->bb_nbuf->nb_qtype = is_write(fom->fo_fop) ?
					 C2_NET_QT_ACTIVE_BULK_RECV :
					 C2_NET_QT_ACTIVE_BULK_SEND;

		for (k = 0; k < ivec->ci_nr; ++k)
			tc += ivec->ci_iosegs[k].ci_count;

		if (is_read(fom->fo_fop)) {
			for (j = 0; j < ivec->ci_nr; ++j)
				/*
				 * Sets a pattern in data buffer so that
				 * it can be verified at other side.
				 */
				memset(netbufs[i]->nb_buffer.ov_buf[j], 'b',
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
		for (j = 0; j < netbufs[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(writebuf, netbufs[i]->nb_buffer.ov_buf[j],
				    netbufs[i]->nb_buffer.ov_vec.v_count[j]);
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
		c2_bufvec_free(&netbufs[i]->nb_buffer);
		c2_free(netbufs[i]);
	}
	c2_free(netbufs);
	c2_rpc_bulk_fini(rbulk);
	c2_free(rbulk);

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

/*
 * Zero vector needs buffers aligned on 4k boundary.
 * Hence c2_bufvec_alloc can not be used.
 */
static void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
		      c2_bcount_t seg_size)
{
	uint32_t i;

	bvec->ov_vec.v_nr = segs_nr;
	C2_ALLOC_ARR(bvec->ov_vec.v_count, segs_nr);
	C2_UT_ASSERT(bvec->ov_vec.v_count != NULL);
	C2_ALLOC_ARR(bvec->ov_buf, segs_nr);
	C2_UT_ASSERT(bvec->ov_buf != NULL);

	for (i = 0; i < segs_nr; ++i) {
		bvec->ov_buf[i] = c2_alloc_aligned(IO_SEG_SIZE, C2_0VEC_SHIFT);
		C2_UT_ASSERT(bvec->ov_buf[i] != NULL);
		bvec->ov_vec.v_count[i] = IO_SEG_SIZE;
	}
}

static void io_buffers_allocate(void)
{
	int i;

	/* Initialized the standard buffer with a data pattern for read IO. */
	memset(readbuf, 'b', IO_SEG_SIZE);
	memset(writebuf, 'a', IO_SEG_SIZE);

	C2_SET_ARR0(io_buf);
	for (i = 0; i < IO_FOPS_NR; ++i)
		vec_alloc(&io_buf[i].nb_buffer, IO_SEGS_NR, IO_SEG_SIZE);
}

static void io_buffers_deallocate(void)
{
	int i;

	for (i = 0; i < IO_FOPS_NR; ++i)
		c2_bufvec_free(&io_buf[i].nb_buffer);
}

static void io_fop_populate(int index, uint64_t off_index,
			    enum C2_RPC_OPCODES op, int segs_nr)
{
	int			 i;
	int			 rc;
	struct c2_io_fop	*iofop;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_fop	**io_fops;

	io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? wfops : rfops;
	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;

	/*
	 * Adds a c2_rpc_bulk_buf structure to list of such structures
	 * in c2_rpc_bulk.
	 */
	rc = c2_rpc_bulk_buf_add(rbulk, segs_nr, &c_netdom, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	rw = io_rw_get(&iofop->if_fop);
	rw->crw_fid = io_fids[off_index];

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < segs_nr; ++i) {
		rc = c2_rpc_bulk_buf_databuf_add(rbuf,
				io_buf[index].nb_buffer.ov_buf[i],
				io_buf[index].nb_buffer.ov_vec.v_count[i],
				io_offsets[off_index], &c_netdom);
		C2_UT_ASSERT(rc == 0);

		io_offsets[off_index] -=
			io_buf[index].nb_buffer.ov_vec.v_count[i];
	}

	rbuf->bb_nbuf->nb_qtype = (op == C2_IOSERVICE_WRITEV_OPCODE) ?
		C2_NET_QT_PASSIVE_BULK_SEND : C2_NET_QT_PASSIVE_BULK_RECV;

	/*
	 * Allocates memory for array of net buf descriptors and array of
	 * index vectors from io fop.
	 */
	rc = io_fop_prepare(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Stores the net buf desc/s after adding the corresponding
	 * net buffers to transfer machine to io fop wire format.
	 */
	rc = c2_rpc_bulk_store(rbulk, &c_rctx.rcx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Temporary! Should be removed once bulk server UT code is merged
	 * with this code.
	 */
	rw->crw_iovec.iv_count = 1;
	C2_ALLOC_ARR(rw->crw_iovec.iv_segs, rw->crw_iovec.iv_count);
	C2_UT_ASSERT(rw->crw_iovec.iv_segs != NULL);
	rw->crw_iovec.iv_segs[0].is_offset = 0;
	rw->crw_iovec.iv_segs[0].is_buf.ib_count = IO_SEQ_LEN;
	C2_ALLOC_ARR(rw->crw_iovec.iv_segs[0].is_buf.ib_buf,
		     rw->crw_iovec.iv_segs[0].is_buf.ib_count);
}

static void io_fops_create(enum C2_RPC_OPCODES op, int fids_nr, int fops_nr,
			   int segs_nr)
{
	int			  i;
	int			  rc;
	uint64_t		  seed;
	uint64_t		  rnd;
	struct c2_fop_type	 *fopt;
	struct c2_io_fop	**io_fops;

	seed = 0;
	for (i = 0; i < fids_nr; ++i)
		io_offsets[i] = IO_SEG_START_OFFSET;

	if (op == C2_IOSERVICE_WRITEV_OPCODE) {
		C2_ALLOC_ARR(wfops, fops_nr);
		fopt = &c2_fop_cob_writev_fopt;
		io_fops = wfops;
	} else {
		C2_ALLOC_ARR(rfops, fops_nr);
		fopt = &c2_fop_cob_readv_fopt;
		io_fops = rfops;
	}
	C2_UT_ASSERT(io_fops != NULL);

	/* Allocates io fops. */
	for (i = 0; i < fops_nr; ++i) {
		C2_ALLOC_PTR(io_fops[i]);
		C2_UT_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], fopt);
		C2_UT_ASSERT(rc == 0);
		io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ut_ops;
	}

	/* Populates io fops. */
	for (i = 0; i < fops_nr; ++i) {
		rnd = c2_rnd(fids_nr, &seed);
		C2_UT_ASSERT(rnd < fids_nr);

		io_fop_populate(i, rnd, op, segs_nr);
	}
}

static void io_fops_destroy(void)
{
	c2_free(rfops);
	c2_free(wfops);
}

static void io_fops_rpc_submit(struct thrd_arg *t)
{
	int			  i;
	int			  j;
	int			  rc;
	c2_time_t		  timeout;
	struct c2_clink		  clink;
	struct c2_rpc_item	 *item;
	struct c2_rpc_bulk	 *rbulk;
	struct c2_io_fop	**io_fops;

	i = t->ta_index;
	io_fops = (t->ta_op == C2_IOSERVICE_WRITEV_OPCODE) ? wfops : rfops;
	rbulk = c2_fop_to_rpcbulk(&io_fops[i]->if_fop);
	item = &io_fops[i]->if_fop.f_item;
	item->ri_session = &c_rctx.rcx_session;
	c2_time_set(&timeout, IO_RPC_ITEM_TIMEOUT, 0);

	/*
	 * Initializes and adds a clink to rpc item channel to wait for
	 * reply.
	 */
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	timeout = c2_time_add(timeout, c2_time_now());
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	/* Posts the rpc item and waits until reply is received. */
	rc = c2_rpc_post(item);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_reply_timedwait(&clink, timeout);
	if (is_read(&io_fops[i]->if_fop)) {
		for (j = 0; j < io_buf[i].nb_buffer.ov_vec.v_nr;
		     ++j) {
			rc = memcmp(io_buf[i].nb_buffer.ov_buf[j], readbuf,
				    io_buf[i].nb_buffer.ov_vec.v_count[j]);
			C2_UT_ASSERT(rc == 0);
			memset(io_buf[i].nb_buffer.ov_buf[j], 'a', IO_SEG_SIZE);
		}
		c2_mutex_lock(&rbulk->rb_mutex);
		C2_UT_ASSERT(rbulk->rb_rc == 0);
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

static void bulkio_test(int fids_nr, int fops_nr, int segs_nr)
{
	int		    rc;
	int		    i;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ[fops_nr];

	for (op = C2_IOSERVICE_READV_OPCODE; op <= C2_IOSERVICE_WRITEV_OPCODE;
	     ++op) {
		memset(&io_threads, 0, ARRAY_SIZE(io_threads) *
		       sizeof(struct c2_thread));

		/*
		 * IO fops are deallocated by an rpc item type op on receiving
		 * the reply fop. See io_item_free().
		 */
		io_fops_create(op, fids_nr, fops_nr, segs_nr);
		for (i = 0; i < fops_nr; ++i) {
			targ[i].ta_index = i;
			targ[i].ta_op = op;
			rc = C2_THREAD_INIT(&io_threads[i], struct thrd_arg *,
					    NULL, &io_fops_rpc_submit,
					    &targ[i], "io_thrd");
			C2_UT_ASSERT(rc == 0);
		}

		/* Waits till all threads finish their job. */
		for (i = 0; i < fops_nr; ++i)
			c2_thread_join(&io_threads[i]);
	}
}

static void bulkio_single_rw(void)
{
	/* Sends only one fop for read and write IO. */
	bulkio_test(IO_FID_SINGLE, IO_FOP_SINGLE, IO_SEGS_NR);
}

static void bulkio_rwv(void)
{
	/* Sends multiple fops with multiple segments and multiple fids. */
	bulkio_test(IO_FIDS_NR, IO_FOPS_NR, IO_SEGS_NR);
}

static void bulkio_init(void)
{
	int rc;

	C2_SET0(&c_netdom);
	rc = c2_net_domain_init(&c_netdom, xprt);
	C2_UT_ASSERT(rc == 0);

	/* Starts a colibri server. */
	rc = c2_rpc_server_start(&s_rctx);
	C2_UT_ASSERT(rc == 0);

	/* Starts an rpc client. */
	rc = c2_rpc_client_init(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	/* Initialize fids and allocate buffers used for bulk transfer. */
	io_fids_init();
	io_buffers_allocate();

	c2_addb_choose_default_level(AEL_WARN);
}

static void bulkio_fini(void)
{
	int rc;

	rc = c2_rpc_client_fini(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_stop(&s_rctx);
	c2_net_domain_fini(&c_netdom);
	c2_net_xprt_fini(xprt);
	io_fops_destroy();
	io_buffers_deallocate();
}

static void bulkioapi_test(void)
{
	int			 rc;
	char			*sbuf;
	char			*dbuf;
	struct c2_clink		 clink;
	struct c2_io_fop	 iofop;
	struct c2_io_fop	 iofop1;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_net_domain	 nd;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_rpc_bulk_buf	*rbuf1;
	struct c2_net_buf_desc	 desc;

	C2_SET0(&iofop);
	C2_SET0(&nd);

	/* Test : c2_io_fop_init() */
	rc = c2_io_fop_init(&iofop, &c2_fop_cob_writev_fopt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(iofop.if_magic == C2_IO_FOP_MAGIC);
	C2_UT_ASSERT(iofop.if_fop.f_type != NULL);
	C2_UT_ASSERT(iofop.if_fop.f_item.ri_type != NULL);
	C2_UT_ASSERT(iofop.if_fop.f_item.ri_ops != NULL);

	C2_UT_ASSERT(iofop.if_rbulk.rb_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop.if_rbulk.rb_buflist.t_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop.if_rbulk.rb_bytes == 0);
	C2_UT_ASSERT(iofop.if_rbulk.rb_rc == 0);

	/* Test : c2_fop_to_rpcbulk() */
	rbulk = c2_fop_to_rpcbulk(&iofop.if_fop);
	C2_UT_ASSERT(rbulk != NULL);
	C2_UT_ASSERT(rbulk == &iofop.if_rbulk);

	/* Test : c2_rpc_bulk_buf_add() */
	rc = c2_net_domain_init(&nd, xprt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	/* Test : c2_rpc_bulk_buf structure. */
	C2_UT_ASSERT(c2_tlink_is_in(&rpcbulk_tl, rbuf));
	C2_UT_ASSERT(rbuf->bb_magic == C2_RPC_BULK_BUF_MAGIC);
	C2_UT_ASSERT(rbuf->bb_rbulk == rbulk);
	C2_UT_ASSERT(rbuf->bb_nbuf!= NULL);
	/*
	 * Since no external net buffer was passed to c2_rpc_bulk_buf_add(),
	 * it should allocate a net buffer internally and c2_rpc_bulk_buf::
	 * bb_flags should be C2_RPC_BULK_NETBUF_ALLOCATED.
	 */
	C2_UT_ASSERT(rbuf->bb_flags == C2_RPC_BULK_NETBUF_ALLOCATED);

	/* Test : c2_rpc_bulk_buf_add() - Error case. */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR * IO_FOPS_NR,
				 &nd, NULL, &rbuf1);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	/* Test : c2_rpc_bulk_buf_databuf_add(). */
	sbuf = c2_alloc_aligned(IO_SEG_SIZE, C2_0VEC_SHIFT);
	C2_UT_ASSERT(sbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, IO_SEG_SIZE, 0, &nd);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) ==
		     IO_SEG_SIZE);
	C2_UT_ASSERT(c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) ==
		     c2_vec_count(&rbuf->bb_nbuf->nb_buffer.ov_vec));

	/* Test : c2_rpc_bulk_buf_databuf_add() - Error case. */
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, IO_SEG_SIZE * IO_SEGS_NR *
					 IO_FOPS_NR, 0, &nd);
	/* Segment size bigger than permitted segment size. */
	C2_UT_ASSERT(rc == -EMSGSIZE);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, IO_SEG_SIZE * IO_SEGS_NR *
					 IO_FOPS_NR, 0, &nd);
	/* Max buffer size greater than permitted max buffer size. */
	C2_UT_ASSERT(rc == -EMSGSIZE);

	/* Test : c2_rpc_bulk_buflist_empty() */
	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	/* Test : c2_rpc_bulk_store() */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, IO_SEG_SIZE, 0, &nd);
	C2_UT_ASSERT(rc == 0);

	rbuf->bb_nbuf->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	rc = io_fop_prepare(&iofop.if_fop);
	C2_UT_ASSERT(rc == 0);
	rw = io_rw_get(&iofop.if_fop);

	rc = c2_rpc_bulk_store(rbulk, &c_rctx.rcx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_UT_ASSERT(rbuf->bb_nbuf->nb_callbacks == &rpc_bulk_cb);
	C2_UT_ASSERT(rbuf->bb_nbuf->nb_flags & C2_NET_BUF_REGISTERED);
	C2_UT_ASSERT(rbuf->bb_nbuf->nb_flags & C2_NET_BUF_QUEUED);
	C2_UT_ASSERT(rbulk->rb_bytes ==
		     c2_vec_count(&rbuf->bb_nbuf->nb_buffer.ov_vec));
	C2_UT_ASSERT(rbuf->bb_nbuf->nb_app_private == rbuf);
	C2_UT_ASSERT(rbuf->bb_nbuf->nb_ep ==
		     c_rctx.rcx_connection.c_rpcchan->rc_destep);
	c2_mutex_unlock(&rbulk->rb_mutex);

	/* Removes the net buffer added for data transfer. */
	c2_mutex_lock(&rbulk->rb_mutex);
	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
		c2_net_buffer_del(rbuf->bb_nbuf, &c_rctx.rcx_rpc_machine.cr_tm);
	} c2_tlist_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);

	/* Waits till list of buffers is empty. */
	while (1) {
		c2_mutex_lock(&rbulk->rb_mutex);
		if (c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist))
			break;
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);
	io_fop_destroy(&iofop.if_fop);

	/* Test : c2_rpc_bulk_load() */
	rc = c2_io_fop_init(&iofop1, &c2_fop_cob_writev_fopt);
	C2_UT_ASSERT(rc == 0);
	rbulk = c2_fop_to_rpcbulk(&iofop1.if_fop);
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf1);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf1 != NULL);

	dbuf = c2_alloc_aligned(IO_SEG_SIZE, C2_0VEC_SHIFT);
	C2_UT_ASSERT(dbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf1, dbuf, IO_SEG_SIZE, 0, &nd);
	C2_UT_ASSERT(rc == 0);
	rw = io_rw_get(&iofop1.if_fop);

	rbuf1->bb_nbuf->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	c2_clink_init(&clink, NULL);
	c2_clink_add(&rbulk->rb_chan, &clink);

	rc = io_fop_prepare(&iofop1.if_fop);
	C2_UT_ASSERT(rc == 0);

	/* Populates a fake net buf desc and copies it in io fop wire format. */
	desc.nbd_len = IO_SEQ_LEN;
	desc.nbd_data = sbuf;
	memcpy(rw->crw_desc.id_descs, &desc, sizeof(struct c2_net_buf_desc));
	rc = c2_rpc_bulk_load(rbulk, &c_rctx.rcx_connection,
			      rw->crw_desc.id_descs);

	C2_UT_ASSERT(rc == 0);

	/* Waits till list of buffers is empty. */
	while (1) {
		c2_mutex_lock(&rbulk->rb_mutex);
		if (c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist))
			break;
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	/*
	 * After an invalid net buf desc is supplied, the bulk transfer
	 * should fail with an invalid return code.
	 */
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);

	io_fop_destroy(&iofop1.if_fop);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	/* Cleanup. */
	c2_free(dbuf);
	c2_free(sbuf);

	c2_io_fop_fini(&iofop);
	c2_io_fop_fini(&iofop1);
	c2_net_domain_fini(&nd);
}

const struct c2_test_suite bulkio_ut = {
	.ts_name = "bulkio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		/*
		 * Intentionally kept as first test case. It initializes
		 * all necessary data for sending IO fops. Keeping
		 * bulkio_init() as .ts_init requires changing all
		 * C2_UT_ASSERTS to C2_ASSERTS.
		 */
		{ "bulkio_init",		bulkio_init},
		{ "bulkioapi_test",		bulkioapi_test},
		{ "bulkio_single_fop_rw",	bulkio_single_rw},
		{ "bulkio_vectored_rw",		bulkio_rwv},
		{ "bulkio_fini",		bulkio_fini},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
