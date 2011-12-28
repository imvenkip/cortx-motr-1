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

/* Structure to be used as in assigning values to a fop sequence like
   c2_net_buf_desc and c2_io_indexvec. */
static char			  io_tmp_char;
static char			  s_endp_addr[] = "127.0.0.1:23123:1";
static char			  c_endp_addr[] = "127.0.0.1:23123:2";
static char			  s_db_name[]	= "bulk_s_db";
static char			  c_db_name[]	= "bulk_c_db";

extern struct c2_net_xprt 	  c2_net_bulk_sunrpc_xprt;
static struct c2_net_domain	  io_netdom;
static struct c2_net_xprt	 *xprt = &c2_net_bulk_sunrpc_xprt;

static void io_fids_init(void)
{
	int i;

	/* Populate fids. */
	for (i = 0; i < IO_FIDS_NR; ++i) {
		io_fids[i].f_seq = i;
		io_fids[i].f_oid = i;
	}
}

static void io_buffers_populate()
{
	int i;

	for (i = 0; i < IO_FIDS_NR; ++i) {
		io_cbufs[i].b_addr = c2_alloc_aligned(IO_SEG_SIZE,
						      C2_0VEC_SHIFT);
		C2_UT_ASSERT(io_cbufs[i].b_addr != NULL);
		io_cbufs[i].b_nob = IO_SEG_SIZE;
	}
}

static void io_fop_populate(struct c2_io_fop *iofop, int index)
{
	int			 i;
	int			 rc;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;

	rbulk = &iofop->if_rbulk;
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR, IO_SEG_SIZE, &io_netdom,
				 &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < IO_SEGS_NR; ++i) {
		rc = c2_rpc_bulk_buf_usrbuf_add(rbuf, io_cbufs[index].b_addr,
						io_cbufs[index].b_nob,
						io_offsets[index]);
		C2_UT_ASSERT(rc == 0);
		rbuf->bb_nbuf.nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
		io_offsets[index] -= io_cbufs[index].b_nob;
	}
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

	/* Allocate io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		/* Since read and write fops are similar and the io coalescing
		   code is same for read and write fops, it doesn't
		   matter what type of fop is created. */

		C2_ALLOC_PTR(io_fops[i]);
		C2_UT_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], &c2_fop_cob_writev_fopt);
		C2_UT_ASSERT(rc == 0);
	}

	/* Populate io fops. */
	for (i = 0; i < IO_FOPS_NR; ++i) {
		iofop = c2_fop_data(&io_fops[i]->if_fop);
		rnd = c2_rnd(IO_FIDS_NR, &seed);
		C2_UT_ASSERT(rnd < IO_FIDS_NR);
		iofop->c_rwv.crw_fid = io_fids[rnd];
		/* Allocates memory for net buf descriptor array and index
		   vector array. */
		C2_ALLOC_PTR(iofop->c_rwv.crw_desc.id_descs);
		C2_UT_ASSERT(iofop->c_rwv.crw_desc.id_descs != NULL);
		iofop->c_rwv.crw_desc.id_nr = 1;

		C2_ALLOC_PTR(iofop->c_rwv.crw_ivecs.cis_ivecs);
		C2_UT_ASSERT(iofop->c_rwv.crw_ivecs.cis_ivecs != NULL);
		iofop->c_rwv.crw_ivecs.cis_nr = 1;
		iofop->c_rwv.crw_ivecs.cis_ivecs->ci_nr = 1;
		C2_ALLOC_PTR(iofop->c_rwv.crw_ivecs.cis_ivecs->ci_iosegs);
		C2_UT_ASSERT(iofop->c_rwv.crw_ivecs.cis_ivecs->ci_iosegs !=
				NULL);

		/* Need to put some values in c2_net_buf_desc and the
		   c2_io_indexvec since both of them are SEQUENCE fops
		   and count of a SEQUENCE in a fop can not be zero.
		   They need to point to a valid memory address. */
		iofop->c_rwv.crw_desc.id_descs->nbd_len = 1;
		iofop->c_rwv.crw_desc.id_descs->nbd_data = &io_tmp_char;

		io_fop_populate(io_fops[i], rnd);
	}
}

static void io_fops_rpc_submit(struct c2_rpc_ctx *ctx)
{
	int i;
	int rc;

	for (i = 0; i < IO_FOPS_NR; ++i) {
		io_fops[i]->if_fop.f_item.ri_session = &ctx->rx_session;
		rc = c2_rpc_post(&io_fops[i]->if_fop.f_item);
		C2_UT_ASSERT(rc == 0);
	}
}

void test_iocoalesce(void)
{
	int				 rc;
	struct c2_reqh			reqh;
	struct c2_dbenv			c_dbenv;
	struct c2_dbenv			s_dbenv;
	struct c2_cob_domain		c_cbdom;
	struct c2_cob_domain		s_cbdom;

	struct c2_rpc_ctx		s_rctx = {
		.rx_net_dom		= &io_netdom,
		.rx_reqh		= &reqh,
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

	C2_SET0(&reqh);
	rc = c2_processors_init();
	C2_UT_ASSERT(rc == 0);

	/* Start an rpc server and an rpc client. */
	rc = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&io_netdom, xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_reqh_init(&reqh, NULL, NULL, NULL, NULL, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_server_init(&s_rctx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_client_init(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	io_fids_init();
	io_buffers_populate();
	io_fops_create();
	io_fops_rpc_submit(&c_rctx);

	rc = c2_rpc_client_fini(&c_rctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_fini(&s_rctx);

	c2_reqh_fini(&reqh);

	c2_net_domain_fini(&io_netdom);

	c2_net_xprt_fini(xprt);
}

const struct c2_test_suite bulkio_ut = {
	.ts_name = "bulkio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bulkio", test_iocoalesce },
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
