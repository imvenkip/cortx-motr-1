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
 *		    Madhavrao Vemuri <madhav_vemuri@xyratec.com>
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

#include "ioservice/io_foms.h"
#include "ioservice/io_service.h"

#include "ioservice/io_fops.c"	/* To access static apis for testing. */
#include "ioservice/io_foms.c"
enum IO_UT_VALUES {
	IO_KERN_PAGES		= 1,
	IO_FIDS_NR		= 4,
	IO_SEGS_NR		= 128,
	IO_SEQ_LEN		= 8,
	IO_FOPS_NR		= 32,
	IO_SEG_SIZE		= 4096,
	IO_RPC_ITEM_TIMEOUT	= 300,
	IO_SEG_START_OFFSET	= IO_SEG_SIZE,
	IO_CLIENT_COBDOM_ID	= 21,
	IO_SERVER_COBDOM_ID	= 29,
	IO_RPC_SESSION_SLOTS	= 8,
	IO_RPC_MAX_IN_FLIGHT	= 32,
	IO_RPC_CONN_TIMEOUT	= 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);

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

static char			  c_endp_addr[] = "127.0.0.1:23123:2";
static char			  c_db_name[]	= "bulk_c_db";
static char			  s_db_file[]	= "bulkio_ut.db";
static char			  s_stob_file[]	= "bulkio_ut_stob";
static char			  s_log_file[]	= "bulkio_ut.log";

#define S_ENDP_ADDR		  "127.0.0.1:23123:1"
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

static int io_fop_dummy_fom_init(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_server_write_fom_init(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_server_read_fom_init(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_stob_create_fom_init(struct c2_fop *fop, struct c2_fom **m);

struct c2_fop_type_ops bulkio_stob_create_ops = {
	.fto_fom_init = io_fop_stob_create_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct c2_fop_type_ops bulkio_server_write_fop_ut_ops = {
	.fto_fom_init = io_fop_server_write_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct c2_fop_type_ops bulkio_server_read_fop_ut_ops = {
	.fto_fom_init = io_fop_server_read_fom_init,
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

/*
 * An alternate io fop type op vector to test bulk client functionality only.
 * Only .fto_fom_init is pointed to a UT function which tests the received
 * io fop is sane and bulk io transfer is taking place properly using data
 * from io fop. Rest all ops are same as io_fop_rwv_ops.
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

static void bulkio_stob_fom_fini(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rw   *fom_obj = NULL;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        c2_stob_put(fom_obj->fcrw_stob);
	c2_fom_fini(fom);
	c2_free(fom);
}

static void bulkio_server_fom_fini(struct c2_fom *fom)
{
	c2_io_fom_cob_rw_fini(fom);
}

static void bulkio_fom_fini(struct c2_fom *fom)
{
	c2_fom_fini(fom);
	c2_free(fom);
}

static int bulkio_server_write_fom_state(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rw   *fom_obj = NULL;
	int rc;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);

	switch(fom->fo_phase) {
		case FOPH_IO_FOM_BUFFER_ACQUIRE :
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);
			break;
		case FOPH_IO_ZERO_COPY_INIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);
			break;
		case FOPH_IO_ZERO_COPY_WAIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_INIT);
			break;
		case FOPH_IO_STOB_INIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_WAIT);
			break;
		case FOPH_IO_STOB_WAIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_BUFFER_RELEASE);
			break;
		case FOPH_IO_BUFFER_RELEASE:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_SUCCESS);
			break;
		default :
			rc = c2_io_fom_cob_rw_state(fom);
	}
	return rc;
}

static int bulkio_server_read_fom_state(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rw   *fom_obj = NULL;
	int rc;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);

	switch(fom->fo_phase) {
		case FOPH_IO_FOM_BUFFER_ACQUIRE :
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_INIT);
			break;
		case FOPH_IO_ZERO_COPY_INIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);
			break;
		case FOPH_IO_ZERO_COPY_WAIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_BUFFER_RELEASE);
			break;
		case FOPH_IO_STOB_INIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_WAIT);
			break;
		case FOPH_IO_STOB_WAIT:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);
			break;
		case FOPH_IO_BUFFER_RELEASE:
			rc = c2_io_fom_cob_rw_state(fom);
	 		C2_UT_ASSERT(fom->fo_phase == FOPH_SUCCESS);
			break;
		default :
			rc = c2_io_fom_cob_rw_state(fom);
	}
	return rc;
}

static int bulkio_stob_create_fom_state(struct c2_fom *fom)
{
   	struct c2_fop_cob_rw            *rwfop;
        struct c2_stob_domain           *fom_stdom;
        struct c2_fop_file_fid          *ffid;
        struct c2_fid                    fid;
        struct c2_stob_id                stobid;
        int				 rc;
	struct c2_fop			*fop;
	struct c2_fop_cob_writev_rep	*wrep;

        struct c2_io_fom_cob_rw  *fom_obj;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        rwfop = io_rw_get(fom->fo_fop);

	C2_UT_ASSERT(rwfop->crw_desc.id_nr == rwfop->crw_ivecs.cis_nr);
        ffid = &rwfop->crw_fid;
        io_fom_cob_rw_fid_wire2mem(ffid, &fid);
        io_fom_cob_rw_fid2stob_map(&fid, &stobid);
        fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

        rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
        C2_UT_ASSERT(rc == 0);
        C2_UT_ASSERT(fom_obj->fcrw_stob->so_state == CSS_UNKNOWN);

        rc = c2_stob_create(fom_obj->fcrw_stob, &fom->fo_tx);
        C2_UT_ASSERT(rc == 0);

	fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
	wrep = c2_fop_data(fop);
	wrep->c_rep.rwr_rc = 0;
	wrep->c_rep.rwr_count = rwfop->crw_ivecs.cis_nr;
	fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fop->f_item);
	C2_UT_ASSERT(rc == 0);
	fom->fo_phase = FOPH_FINISH;
	return rc;
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
	struct c2_net_buf_desc		*desc;
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
		desc = &rw->crw_desc.id_descs[i];

		C2_ALLOC_PTR(netbufs[i]);
		C2_UT_ASSERT(netbufs[i] != NULL);

		vec_alloc(&netbufs[i]->nb_buffer, ivec->ci_nr,
			  ivec->ci_iosegs[0].ci_count);

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 ivec->ci_iosegs[0].ci_count,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 netbufs[i], &rbuf);

		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf != NULL);

		rbuf->bb_nbuf->nb_qtype = c2_is_write_fop(fom->fo_fop) ?
					 C2_NET_QT_ACTIVE_BULK_RECV :
					 C2_NET_QT_ACTIVE_BULK_SEND;

		for (k = 0; k < ivec->ci_nr; ++k)
			tc += ivec->ci_iosegs[k].ci_count;

		if (c2_is_read_fop(fom->fo_fop)) {
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
	for (i = 0; i < rw->crw_desc.id_nr && c2_is_write_fop(fom->fo_fop); ++i) {
		for (j = 0; j < netbufs[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(writebuf, netbufs[i]->nb_buffer.ov_buf[j],
				    netbufs[i]->nb_buffer.ov_vec.v_count[j]);
			C2_UT_ASSERT(rc == 0);
		}
	}

	if (c2_is_write_fop(fom->fo_fop)) {
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

static struct c2_fom_ops bulkio_stob_create_fom_ops = {
	.fo_fini = bulkio_stob_fom_fini,
	.fo_state = bulkio_stob_create_fom_state,
	.fo_home_locality = bulkio_fom_locality,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

static struct c2_fom_ops bulkio_server_write_fom_ops = {
	.fo_fini = bulkio_server_fom_fini,
	.fo_state = bulkio_server_write_fom_state,
	.fo_home_locality = bulkio_fom_locality,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

static struct c2_fom_ops bulkio_server_read_fom_ops = {
	.fo_fini = bulkio_server_fom_fini,
	.fo_state = bulkio_server_read_fom_state,
	.fo_home_locality = bulkio_fom_locality,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};
static struct c2_fom_ops bulkio_fom_ops = {
	.fo_fini = bulkio_fom_fini,
	.fo_state = bulkio_fom_state,
	.fo_home_locality = bulkio_fom_locality,
};

static int io_fop_stob_create_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	 rc = c2_io_fom_cob_rw_init(fop, &fom);
        C2_UT_ASSERT(rc == 0);
	fop->f_type->ft_fom_type.ft_ops = &bulkio_fom_type_ops;
	fom->fo_type = &bulkio_fom_type;
	fom->fo_ops = &bulkio_stob_create_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

static int io_fop_server_write_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	 rc = c2_io_fom_cob_rw_init(fop, &fom);
        C2_UT_ASSERT(rc == 0);
	fop->f_type->ft_fom_type.ft_ops = &bulkio_fom_type_ops;
	fom->fo_type = &bulkio_fom_type;
	fom->fo_ops = &bulkio_server_write_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

static int io_fop_server_read_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	 rc = c2_io_fom_cob_rw_init(fop, &fom);
        C2_UT_ASSERT(rc == 0);
	fop->f_type->ft_fom_type.ft_ops = &bulkio_fom_type_ops;
	fom->fo_type = &bulkio_fom_type;
	fom->fo_ops = &bulkio_server_read_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

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
			    enum C2_RPC_OPCODES op)
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
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR, IO_SEG_SIZE, &c_netdom,
				 NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	rw = io_rw_get(&iofop->if_fop);
	rw->crw_fid = io_fids[index];

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < IO_SEGS_NR; ++i) {
		rc = c2_rpc_bulk_buf_databuf_add(rbuf,
				io_buf[index].nb_buffer.ov_buf[i],
				io_buf[index].nb_buffer.ov_vec.v_count[i],
				io_offsets[off_index]);
		C2_UT_ASSERT(rc == 0);
		io_offsets[off_index] +=
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

static void io_fops_create(enum C2_RPC_OPCODES op)
{
	int			  i;
	int			  rc;
	uint64_t		  seed;
	uint64_t		  rnd;
	struct c2_fop_type	 *fopt;
	struct c2_io_fop	**io_fops;

	seed = 0;
	for (i = 0; i < IO_FIDS_NR; ++i)
		io_offsets[i] = IO_SEG_START_OFFSET;

	if (op == C2_IOSERVICE_WRITEV_OPCODE) {
		C2_ALLOC_ARR(wfops, IO_FOPS_NR);
		fopt = &c2_fop_cob_writev_fopt;
		io_fops = wfops;
	} else {
		C2_ALLOC_ARR(rfops, IO_FOPS_NR);
		fopt = &c2_fop_cob_readv_fopt;
		io_fops = rfops;
	}
	C2_UT_ASSERT(io_fops != NULL);

	/* Allocates io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		C2_ALLOC_PTR(io_fops[i]);
		C2_UT_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], fopt);
		C2_UT_ASSERT(rc == 0);
		/* removed this actual integration */
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
	C2_UT_ASSERT(rc == 0);

	if (rc == 0 && c2_is_read_fop(&io_fops[i]->if_fop)) {
		for (j = 0; j < io_buf[i].nb_buffer.ov_vec.v_nr;
		     ++j) {
			rc = memcmp(io_buf[i].nb_buffer.ov_buf[j], readbuf,
				    io_buf[i].nb_buffer.ov_vec.v_count[j]);
			memset(io_buf[i].nb_buffer.ov_buf[j], 'a', IO_SEG_SIZE);
			C2_UT_ASSERT(rc == 0);
		}
		c2_mutex_lock(&rbulk->rb_mutex);
		C2_UT_ASSERT(rbulk->rb_rc == 0);
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

char	*server_args[] =
		{"bulkio_ut", "-r", "-T", "AD", "-D", s_db_file,
		 "-S", s_stob_file, "-e", S_ENDPOINT, "-s", "ioservice"};

struct c2_reqh_service_type *stypes[] = {
		&ds1_service_type,
		&ds2_service_type,
};

struct c2_rpc_server_ctx s_rctx =  {
		.rsx_xprts            = &xprt,
		.rsx_xprts_nr         = 1,
		.rsx_argv             = server_args,
		.rsx_argc             = ARRAY_SIZE(server_args),
		.rsx_service_types    = stypes,
		.rsx_service_types_nr = ARRAY_SIZE(stypes),
		.rsx_log_file_name    = s_log_file,
};


void bulkio_test_init(void)
{
	int rc;

	rc = c2_net_domain_init(&c_netdom, xprt);
	C2_UT_ASSERT(rc == 0);

	/* Starts a colibri server. */
	rc = c2_rpc_server_start(&s_rctx);
	C2_UT_ASSERT(rc == 0);
	rc = c2_rpc_client_init(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	io_fids_init();
	io_buffers_allocate();

}

void bulkio_stobe_create(void)
{
	struct c2_fop_cob_rw	*rw;
	enum C2_RPC_OPCODES	 op;
	struct thrd_arg		 targ[IO_FIDS_NR];
	int			 i;
	int			 rc;

	op = C2_IOSERVICE_WRITEV_OPCODE;
	C2_ALLOC_ARR(wfops, IO_FIDS_NR);
	for (i = 0; i < IO_FIDS_NR; ++i) {
		C2_ALLOC_PTR(wfops[i]);
 	 	rc = c2_io_fop_init(wfops[i], &c2_fop_cob_writev_fopt);
		rw = io_rw_get(&wfops[i]->if_fop);
		wfops[i]->if_fop.f_type->ft_ops = &bulkio_stob_create_ops;
		rw->crw_fid = io_fids[i];
		targ[i].ta_index = i;
		targ[i].ta_op = op;
		io_fops_rpc_submit(&targ[i]);
	}

}

void bulkio_server_single_read_write(void)
{
	int		    j;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ;
	struct c2_bufvec   *buf;

	buf = &io_buf[0].nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_WRITEV_OPCODE;
	io_fops_create(op);
	wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);

	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(op);
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);
}

void bulkio_server_read_write_state_test(void)
{
	int		    j;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ;
	struct c2_bufvec   *buf;

	buf = &io_buf[0].nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_WRITEV_OPCODE;
	io_fops_create(op);
	wfops[0]->if_fop.f_type->ft_ops = &bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);

	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(op);
	rfops[0]->if_fop.f_type->ft_ops = &bulkio_server_read_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);
}

void bulkio_test_client(void)
{
	int		     rc;
	int		     i;
	enum C2_RPC_OPCODES  op;
	struct thrd_arg      targ[IO_FOPS_NR];
	struct c2_io_fop   **io_fops;

	for (op = C2_IOSERVICE_READV_OPCODE; op <= C2_IOSERVICE_WRITEV_OPCODE;
	  ++op) {
		io_fops_create(op);
		memset(&io_threads, 0, ARRAY_SIZE(io_threads) *
		       sizeof(struct c2_thread));
		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? wfops : rfops;
		for (i = 0; i < IO_FOPS_NR; ++i)
			io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ut_ops;

		/*
		 * IO fops are deallocated by an rpc item type op on receiving
		 * the reply fop. See io_item_free().
		 */
		io_fops_create(op);
		for (i = 0; i < ARRAY_SIZE(io_threads); ++i) {
			targ[i].ta_index = i;
			targ[i].ta_op = op;
			rc = C2_THREAD_INIT(&io_threads[i], struct thrd_arg *,
					    NULL, &io_fops_rpc_submit,
					    &targ[i], "io_thrd");
			C2_UT_ASSERT(rc == 0);
		}
		for (i = 0; i < ARRAY_SIZE(io_threads); ++i)
			c2_thread_join(&io_threads[i]);
	}
}

void bulkio_test_fini(void)
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

const struct c2_test_suite bulkio_ut = {
	.ts_name = "bulkio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bulkio_init                  ", bulkio_test_init},
		{ "bulkio_stob_create           ", bulkio_stobe_create},
		{ "bulkio_single_read_write     ", bulkio_server_single_read_write},
		{ "bulkio_read_write_state_test ", bulkio_server_read_write_state_test},
		{ "bulkio_client                ", bulkio_test_client},
		{ "bulkio_fini                  ", bulkio_test_fini},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
