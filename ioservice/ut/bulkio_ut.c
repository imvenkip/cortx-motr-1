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
#include "net/net.h"		/* C2_NET_QT_PASSIVE_BULK_SEND */
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
	IO_FID_SINGLE		= 1,
	IO_FOP_SINGLE		= 1,
	IO_XPRT_NR		= 1,
	IO_ADDR_LEN		= 32,
	IO_SEG_STEP		= 64,
	IO_RPC_ITEM_TIMEOUT	= 300,
	IO_SEG_START_OFFSET	= C2_0VEC_ALIGN * IO_SEGS_NR * IO_FOPS_NR,
	IO_CLIENT_COBDOM_ID	= 21,
	IO_SERVER_COBDOM_ID	= 29,
	IO_RPC_SESSION_SLOTS	= 8,
	IO_RPC_MAX_IN_FLIGHT	= 32,
	IO_RPC_CONN_TIMEOUT	= 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);
extern const struct c2_net_buffer_callbacks rpc_bulk_cb;
extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;
static void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
		      c2_bcount_t seg_size);

/* Structure containing data needed for UT. */
struct bulkio_params {
	/* Fids of global files. */
	struct c2_fop_file_fid		  bp_fids[IO_FIDS_NR];

	/* Tracks offsets for global fids. */
	uint64_t			  bp_offsets[IO_FIDS_NR];

	/* In-memory fops for read IO. */
	struct c2_io_fop		**bp_rfops;

	/* In-memory fops for write IO. */
	struct c2_io_fop		**bp_wfops;

	/* Read buffers to which data will be transferred. */
	struct c2_net_buffer		**bp_iobuf;

	/* Threads to post rpc items to rpc layer. */
	struct c2_thread		**bp_threads;

	/*
	 * Standard buffers containing a data pattern.
	 * Primarily used for data verification in read and write IO.
	 */
	char				 *bp_readbuf;
	char				 *bp_writebuf;

	/* Structures used by client-side rpc code. */
	struct c2_dbenv			  bp_cdbenv;
	struct c2_cob_domain		  bp_ccbdom;
	char				  bp_cendpaddr[IO_ADDR_LEN];
	char				  bp_cdbname[IO_ADDR_LEN];
	struct c2_net_domain		  bp_cnetdom;
	struct c2_rpc_client_ctx	 *bp_cctx;

	/* Structures used by server-side rpc code. */
	char				  bp_sdbfile[IO_ADDR_LEN];
	char				  bp_sstobfile[IO_ADDR_LEN]; 
	char				  bp_slogfile[IO_ADDR_LEN];
	struct c2_rpc_server_ctx	 *bp_sctx;

	struct c2_net_xprt		 *bp_xprt;
};

/* Pointer to global structure bulkio_params. */
struct bulkio_params *bp;

/* A structure used to pass as argument to io threads. */
struct thrd_arg {
	/* Index in fops array to be posted to rpc layer. */
	int			ta_index;
	/* Type of fop to be sent (read/write). */
	enum C2_RPC_OPCODES	ta_op;
};

/*
static char			  bp_cendpaddr[] = "127.0.0.1:23134:2";
static char			  bp_cdbname[]	= "bulk_c_db";
static char			  bp_sdbfile[]	= "bulkio_ut.db";
static char			  bp_sstobfile[]	= "bulkio_ut_stob";
static char			  bp_slogfile[]	= "bulkio_ut.log";
*/

#define S_ENDP_ADDR		  "127.0.0.1:23134:1"
#define S_ENDPOINT		  "bulk-sunrpc:"S_ENDP_ADDR
#define S_DBFILE		  "bulkio_ut.db"
#define S_STOBFILE		  "bulkio_ut_stob"

static struct c2_rpc_client_ctx c_rctx = {
	.rcx_remote_addr	= S_ENDP_ADDR,
	.rcx_cob_dom_id		= IO_CLIENT_COBDOM_ID,
	.rcx_nr_slots		= IO_RPC_SESSION_SLOTS,
	.rcx_max_rpcs_in_flight	= IO_RPC_MAX_IN_FLIGHT,
	.rcx_timeout_s		= IO_RPC_CONN_TIMEOUT,
};

/* Input arguments for colibri server setup. */
static char *server_args[]	= {"bulkio_ut", "-r", "-T", "AD", "-D",
				   S_DBFILE, "-S", S_STOBFILE, "-e",
				   S_ENDPOINT, "-s", "ds1", "-s", "ds2"};

/* Array of services to be started by colibri server. */
static struct c2_reqh_service_type *stypes[] = {
	&ds1_service_type,
	&ds2_service_type,
};

/*
 * Colibri server rpc context. Can't use C2_RPC_SERVER_CTX_DECLARE_SIMPLE()
 * since it limits the scope of struct c2_rpc_server_ctx to the function
 * where it is declared.
 */
static struct c2_rpc_server_ctx s_rctx = {
	.rsx_xprts_nr		= IO_XPRT_NR,
	.rsx_argv		= server_args,
	.rsx_argc		= ARRAY_SIZE(server_args),
	.rsx_service_types	= stypes,
	.rsx_service_types_nr	= ARRAY_SIZE(stypes),
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
			     ci_count == C2_0VEC_ALIGN);

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
			rc = memcmp(bp->bp_writebuf,
				    netbufs[i]->nb_buffer.ov_buf[j],
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
	/* Deallocates net buffers and c2_bufvec structures. */
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
		bp->bp_fids[i].f_seq = i;
		bp->bp_fids[i].f_oid = i;
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
		bvec->ov_buf[i] = c2_alloc_aligned(C2_0VEC_ALIGN,
						   C2_0VEC_SHIFT);
		C2_UT_ASSERT(bvec->ov_buf[i] != NULL);
		bvec->ov_vec.v_count[i] = C2_0VEC_ALIGN;
	}
}

static void io_buffers_allocate(void)
{
	int i;

	/* Initialized the standard buffer with a data pattern for read IO. */
	memset(bp->bp_readbuf, 'b', C2_0VEC_ALIGN);
	memset(bp->bp_writebuf, 'a', C2_0VEC_ALIGN);

	for (i = 0; i < IO_FOPS_NR; ++i)
		vec_alloc(&bp->bp_iobuf[i]->nb_buffer, IO_SEGS_NR,
			  C2_0VEC_ALIGN);
}

static void io_buffers_deallocate(void)
{
	int i;

	for (i = 0; i < IO_FOPS_NR; ++i)
		c2_bufvec_free(&bp->bp_iobuf[i]->nb_buffer);
}

static void io_fop_populate(int index, uint64_t off_index,
			    struct c2_io_fop **io_fops, int segs_nr)
{
	int			 i;
	int			 rc;
	struct c2_io_fop	*iofop;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*rw;

	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;

	/*
	 * Adds a c2_rpc_bulk_buf structure to list of such structures
	 * in c2_rpc_bulk.
	 */
	rc = c2_rpc_bulk_buf_add(rbulk, segs_nr, &bp->bp_cnetdom, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	rw = io_rw_get(&iofop->if_fop);
	rw->crw_fid = bp->bp_fids[off_index];

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < segs_nr; ++i) {
		rc = c2_rpc_bulk_buf_databuf_add(rbuf,
				bp->bp_iobuf[index]->nb_buffer.ov_buf[i],
				bp->bp_iobuf[index]->nb_buffer.ov_vec.
				v_count[i],
				bp->bp_offsets[off_index], &bp->bp_cnetdom);
		C2_UT_ASSERT(rc == 0);

		bp->bp_offsets[off_index] -=
			bp->bp_iobuf[index]->nb_buffer.ov_vec.v_count[i];
	}

	/*
	 * Allocates memory for array of net buf descriptors and array of
	 * index vectors from io fop.
	 */
	rc = c2_io_fop_prepare(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rw->crw_desc.id_nr ==
		     c2_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist));
	C2_UT_ASSERT(rw->crw_desc.id_descs != NULL);

	/*
	 * Stores the net buf desc/s after adding the corresponding
	 * net buffers to transfer machine to io fop wire format.
	 */
	rc = c2_rpc_bulk_store(rbulk, &c_rctx.rcx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Temporary! Should be removed once bulk server UT code is merged
	 * with this code. Old IO fops were based on sunrpc which had inline
	 * data buffers, which is not the case with bulk IO.
	 * Some deprecated members of fop can not be removed since it would
	 * need changes to IO foms in order for build to be successful.
	 * These will be removed in iointegration branch.
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

	if (op == C2_IOSERVICE_WRITEV_OPCODE) {
		C2_UT_ASSERT(bp->bp_wfops == NULL);
		C2_ALLOC_ARR(bp->bp_wfops, fops_nr);
		fopt = &c2_fop_cob_writev_fopt;
		io_fops = bp->bp_wfops;
	} else {
		C2_UT_ASSERT(bp->bp_rfops == NULL);
		C2_ALLOC_ARR(bp->bp_rfops, fops_nr);
		fopt = &c2_fop_cob_readv_fopt;
		io_fops = bp->bp_rfops;
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

		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
			   bp->bp_rfops;
		io_fop_populate(i, rnd, io_fops, segs_nr);
	}
}

static void io_fops_destroy(void)
{
	c2_free(bp->bp_rfops);
	c2_free(bp->bp_wfops);
	bp->bp_rfops = NULL;
	bp->bp_wfops = NULL;
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
	io_fops = (t->ta_op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
		  bp->bp_rfops;
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
		for (j = 0; j < bp->bp_iobuf[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(bp->bp_iobuf[i]->nb_buffer.ov_buf[j],
				    bp->bp_readbuf,
				    bp->bp_iobuf[i]->nb_buffer.ov_vec.
				    	v_count[j]);
			C2_UT_ASSERT(rc == 0);
			memset(bp->bp_iobuf[i]->nb_buffer.ov_buf[j], 'a',
			       C2_0VEC_ALIGN);
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
	struct thrd_arg	   *targ;

	C2_ALLOC_ARR(targ, fops_nr);
	C2_UT_ASSERT(targ != NULL);

	for (op = C2_IOSERVICE_READV_OPCODE; op <= C2_IOSERVICE_WRITEV_OPCODE;
	     ++op) {
		C2_UT_ASSERT(op == C2_IOSERVICE_READV_OPCODE ||
			     op == C2_IOSERVICE_WRITEV_OPCODE);


		/*
		 * IO fops are deallocated by an rpc item type op on receiving
		 * the reply fop. See io_item_free().
		 */
		io_fops_create(op, fids_nr, fops_nr, segs_nr);
		for (i = 0; i < fops_nr; ++i) {
			targ[i].ta_index = i;
			targ[i].ta_op = op;
			C2_SET0(bp->bp_threads[i]);
			rc = C2_THREAD_INIT(bp->bp_threads[i],
					    struct thrd_arg *,
					    NULL, &io_fops_rpc_submit,
					    &targ[i], "io_thrd");
			C2_UT_ASSERT(rc == 0);
		}

		/* Waits till all threads finish their job. */
		for (i = 0; i < fops_nr; ++i)
			c2_thread_join(bp->bp_threads[i]);
	}
	c2_free(targ);
	io_fops_destroy();
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

static void bulkio_params_init(struct bulkio_params *b)
{
	int  i;
	int  rc;
	char addr[]	 = "127.0.0.1:23134:2";
	char cdbname[]	 = "bulk_c_db";
	char sdbfile[]	 = "bulkio_ut.db";
	char slogfile[]  = "bulkio_ut.log";
	char sstobfile[] = "bulkio_ut_stob";

	C2_UT_ASSERT(b != NULL);

	/* Initialize fids and allocate buffers used for bulk transfer. */
	io_fids_init();

	C2_UT_ASSERT(b->bp_iobuf == NULL);
	C2_ALLOC_ARR(b->bp_iobuf, IO_FOPS_NR);
	C2_UT_ASSERT(b->bp_iobuf != NULL);

	C2_UT_ASSERT(b->bp_threads == NULL);
	C2_ALLOC_ARR(b->bp_threads, IO_FOPS_NR);
	C2_UT_ASSERT(b->bp_threads != NULL);
	for (i = 0; i < IO_FOPS_NR; ++i) {
		C2_ALLOC_PTR(b->bp_iobuf[i]);
		C2_UT_ASSERT(b->bp_iobuf[i] != NULL);
		C2_ALLOC_PTR(b->bp_threads[i]);
		C2_UT_ASSERT(b->bp_threads[i] != NULL);
	}

	C2_UT_ASSERT(b->bp_readbuf == NULL);
	C2_ALLOC_ARR(b->bp_readbuf, C2_0VEC_ALIGN);
	C2_UT_ASSERT(b->bp_readbuf != NULL);

	C2_UT_ASSERT(b->bp_writebuf == NULL);
	C2_ALLOC_ARR(b->bp_writebuf, C2_0VEC_ALIGN);
	C2_UT_ASSERT(b->bp_writebuf != NULL);

	io_buffers_allocate();

	b->bp_xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_domain_init(&b->bp_cnetdom, b->bp_xprt);
	C2_UT_ASSERT(rc == 0);

	memcpy(b->bp_sdbfile, sdbfile, sizeof sdbfile);
	memcpy(b->bp_sstobfile, sstobfile, sizeof sstobfile);
	memcpy(b->bp_slogfile, slogfile, sizeof slogfile);
	memcpy(b->bp_cendpaddr, addr, sizeof addr);
	memcpy(b->bp_cdbname, cdbname, sizeof cdbname);

	/* Starts a colibri server. */
	s_rctx.rsx_xprts = &bp->bp_xprt;
	s_rctx.rsx_log_file_name = bp->bp_slogfile;
	b->bp_sctx = &s_rctx;
	rc = c2_rpc_server_start(b->bp_sctx);
	C2_UT_ASSERT(rc == 0);

	/* Starts an rpc client. */
	c_rctx.rcx_net_dom = &bp->bp_cnetdom;
	c_rctx.rcx_local_addr = bp->bp_cendpaddr;
	c_rctx.rcx_db_name = bp->bp_cdbname;
	c_rctx.rcx_dbenv = &bp->bp_cdbenv;
	c_rctx.rcx_cob_dom = &bp->bp_ccbdom;

	b->bp_cctx = &c_rctx;
	rc = c2_rpc_client_init(b->bp_cctx);
	C2_UT_ASSERT(rc == 0);

	for (i = 0; i < IO_FIDS_NR; ++i)
		b->bp_offsets[i] = IO_SEG_START_OFFSET;

	b->bp_rfops = NULL;
	b->bp_wfops = NULL;
}

static void bulkio_params_fini()
{
	int i;
	int rc;

	C2_UT_ASSERT(bp != NULL);

	rc = c2_rpc_client_fini(bp->bp_cctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_stop(bp->bp_sctx);
	c2_net_domain_fini(&bp->bp_cnetdom);
	C2_UT_ASSERT(bp->bp_iobuf != NULL);
	io_buffers_deallocate();
	for (i = 0; i < IO_FOPS_NR; ++i) {
		c2_free(bp->bp_iobuf[i]);
		c2_free(bp->bp_threads[i]);
	}
	c2_free(bp->bp_iobuf);
	c2_free(bp->bp_threads);

	C2_UT_ASSERT(bp->bp_readbuf != NULL);
	c2_free(bp->bp_readbuf);
	C2_UT_ASSERT(bp->bp_writebuf != NULL);
	c2_free(bp->bp_writebuf);

	C2_UT_ASSERT(bp->bp_rfops == NULL);
	C2_UT_ASSERT(bp->bp_wfops == NULL);
}

static void bulkio_init(void)
{
	struct bulkio_params *bulkp;

	C2_ALLOC_PTR(bulkp);
	C2_UT_ASSERT(bulkp != NULL);

	bp = bulkp;

	bulkio_params_init(bp);

	c2_addb_choose_default_level(AEL_NONE);
}

static void bulkio_fini(void)
{
	bulkio_params_fini();
	c2_free(bp);
}

static void bulkioapi_test(void)
{
	int			 rc;
	char			*sbuf;
	//char			*dbuf;
	//uint32_t		 i;
	int32_t			 max_segs;
	c2_bcount_t		 max_seg_size;
	c2_bcount_t		 max_buf_size;
	//struct c2_clink		 clink;
	struct c2_io_fop	 iofop;
	//struct c2_io_fop	 iofop1;
	struct c2_net_xprt	*xprt;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_net_domain	 nd;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_rpc_bulk_buf	*rbuf1;
	//struct c2_net_buf_desc	 desc;

	C2_SET0(&iofop);
	C2_SET0(&nd);

	xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_domain_init(&nd, xprt);
	C2_UT_ASSERT(rc == 0);

	/* Test : c2_io_fop_init() */
	rc = c2_io_fop_init(&iofop, &c2_fop_cob_writev_fopt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(iofop.if_magic == C2_IO_FOP_MAGIC);
	C2_UT_ASSERT(iofop.if_fop.f_type != NULL);
	C2_UT_ASSERT(iofop.if_fop.f_item.ri_type != NULL);
	C2_UT_ASSERT(iofop.if_fop.f_item.ri_ops  != NULL);

	C2_UT_ASSERT(iofop.if_rbulk.rb_buflist.t_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop.if_rbulk.rb_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop.if_rbulk.rb_bytes == 0);
	C2_UT_ASSERT(iofop.if_rbulk.rb_rc    == 0);

	/* Test : c2_fop_to_rpcbulk() */
	rbulk = c2_fop_to_rpcbulk(&iofop.if_fop);
	C2_UT_ASSERT(rbulk != NULL);
	C2_UT_ASSERT(rbulk == &iofop.if_rbulk);

	/* Test : c2_rpc_bulk_buf_add() */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	/* Test : c2_rpc_bulk_buf structure. */
	C2_UT_ASSERT(c2_tlink_is_in(&rpcbulk_tl, rbuf));
	C2_UT_ASSERT(rbuf->bb_magic == C2_RPC_BULK_BUF_MAGIC);
	C2_UT_ASSERT(rbuf->bb_rbulk == rbulk);
	C2_UT_ASSERT(rbuf->bb_nbuf  != NULL);

	/*
	 * Since no external net buffer was passed to c2_rpc_bulk_buf_add(),
	 * it should allocate a net buffer internally and c2_rpc_bulk_buf::
	 * bb_flags should be C2_RPC_BULK_NETBUF_ALLOCATED.
	 */
	C2_UT_ASSERT(rbuf->bb_flags == C2_RPC_BULK_NETBUF_ALLOCATED);

	/* Test : c2_rpc_bulk_buf_add() - Error case. */
	max_segs = c2_net_domain_get_max_buffer_segments(&nd);
	rc = c2_rpc_bulk_buf_add(rbulk, max_segs + 1, &nd, NULL, &rbuf1);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	/* Test : c2_rpc_bulk_buf_databuf_add(). */
	sbuf = c2_alloc_aligned(C2_0VEC_ALIGN, C2_0VEC_SHIFT);
	C2_UT_ASSERT(sbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, C2_0VEC_ALIGN, 0, &nd);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) ==
		     C2_0VEC_ALIGN);
	C2_UT_ASSERT(c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) ==
		     c2_vec_count(&rbuf->bb_nbuf->nb_buffer.ov_vec));

	/* Test : c2_rpc_bulk_buf_databuf_add() - Error case. */
	max_seg_size = c2_net_domain_get_max_buffer_segment_size(&nd);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, max_seg_size + 1, 0, &nd);
	/* Segment size bigger than permitted segment size. */
	C2_UT_ASSERT(rc == -EMSGSIZE);

	max_buf_size = c2_net_domain_get_max_buffer_size(&nd);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, max_buf_size + 1, 0, &nd);
	/* Max buffer size greater than permitted max buffer size. */
	C2_UT_ASSERT(rc == -EMSGSIZE);

	/* Test : c2_rpc_bulk_buflist_empty() */
	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	/* Test : c2_rpc_bulk_store() */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, C2_0VEC_ALIGN, 0, &nd);
	C2_UT_ASSERT(rc == 0);

	/*
	 * There is no ACTIVE side to start the bulk transfer and hence the
	 * buffer is guaranteed to stay put in PASSIVE_BULK_SEND queue of TM.
	 */
	C2_UT_ASSERT(rbuf->bb_nbuf != NULL);
	rbuf->bb_nbuf->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	rc = c2_io_fop_prepare(&iofop.if_fop);
	C2_UT_ASSERT(rc == 0);
	rw = io_rw_get(&iofop.if_fop);
	C2_UT_ASSERT(rw != NULL);

	rw = io_rw_get(&iofop.if_fop);
	c2_io_fop_destroy(&iofop.if_fop);
	C2_UT_ASSERT(rw->crw_desc.id_descs   == NULL);
	C2_UT_ASSERT(rw->crw_desc.id_nr      == 0);
	C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs == NULL);
	C2_UT_ASSERT(rw->crw_ivecs.cis_nr    == 0);

	/*
	 * Removes net buffers added for data transfer.
	 * Since there is no ACTIVE side to trigger the bulk transfer,
	 * it is safe to remove buffers manually from TM.
	 */

	c2_mutex_lock(&rbulk->rb_mutex);
	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
		c2_net_buffer_del(rbuf->bb_nbuf, &c_rctx.rcx_rpc_machine.cr_tm);
	} c2_tlist_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);

	/* Waits till list of buffers is empty. */
	while (1) {
		c2_mutex_lock(&rbulk->rb_mutex);
		if (c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist)) {
			c2_mutex_unlock(&rbulk->rb_mutex);
			break;
		}
		c2_mutex_unlock(&rbulk->rb_mutex);
	}

#if 0
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

	/* IO fop should contain the copied net buf descriptors. */
	i = 0;
	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
		rc = memcmp(rbuf->bb_nbuf->nb_desc.nbd_data,
			    rw->crw_desc.id_descs[i].nbd_data,
			    rbuf->bb_nbuf->nb_desc.nbd_len);
		C2_UT_ASSERT(rc == 0);
		++i;
	} c2_tlist_endfor;

	c2_mutex_unlock(&rbulk->rb_mutex);

	c2_mutex_lock(&rbulk->rb_mutex);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);

	/* Test : c2_io_fop_destroy() */
	C2_UT_ASSERT(rw->crw_desc.id_descs   != NULL);
	C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs != NULL);

	/* Test : c2_rpc_bulk_load() */
	rc = c2_io_fop_init(&iofop1, &c2_fop_cob_writev_fopt);
	C2_UT_ASSERT(rc == 0);
	rbulk = c2_fop_to_rpcbulk(&iofop1.if_fop);
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf1);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf1 != NULL);

	dbuf = c2_alloc_aligned(C2_0VEC_ALIGN, C2_0VEC_SHIFT);
	C2_UT_ASSERT(dbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf1, dbuf, C2_0VEC_ALIGN, 0, &nd);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf1->bb_nbuf != NULL);
	C2_UT_ASSERT(rbuf1->bb_zerovec.z_bvec.ov_buf[2] == dbuf);
	rw = io_rw_get(&iofop1.if_fop);

	rbuf1->bb_nbuf->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	c2_clink_init(&clink, NULL);
	c2_clink_add(&rbulk->rb_chan, &clink);

	rc = c2_io_fop_prepare(&iofop1.if_fop);
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
#endif

	//c2_io_fop_destroy(&iofop1.if_fop);

	/* Cleanup. */
	//c2_free(dbuf);
	c2_free(sbuf);

	c2_io_fop_fini(&iofop);
	//c2_io_fop_fini(&iofop1);
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
		{ "bulkio_init",	  bulkio_init},
		{ "bulkioapi_test",	  bulkioapi_test},
		{ "bulkio_single_fop_rw", bulkio_single_rw},
		{ "bulkio_vectored_rw",   bulkio_rwv},
		{ "bulkio_fini",	  bulkio_fini},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
