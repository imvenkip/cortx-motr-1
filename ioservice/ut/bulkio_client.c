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
 * Original creation date: 12/27/2011
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "ioservice/io_fops.h"	/* c2_io_fop */

#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "rpc/rpc2.h"		/* c2_rpc_bulk, c2_rpc_bulk_buf */
#include "net/net.h"		/* C2_NET_QT_PASSIVE_BULK_SEND */
#include "reqh/reqh.h"		/* c2_reqh */
#include "rpc/rpclib.h"		/* c2_rpc_client_ctx */
#include "cob/cob.h"		/* c2_cob_domain_id */
#include "ut/rpc.h"		/* c2_rpc_client_init() */
#include "lib/processor.h"	/* c2_processors_init() */

enum {
	IO_SINGLE_BUFFER	= 1,
	IO_RPC_SEG_NR		= 1,
	IO_SERVER_BUF_SEG_NR	= 8,
	IO_ADDR_LEN		= 32,
	IO_STR_LEN		= 16,
	IO_CCOBDOMAIN_ID	= 21,
	IO_SCOBDOMAIN_ID	= 22,
	IO_SESSION_SLOT_NR	= 8,
	IO_RPC_CONN_TIMEOUT	= 60,
	IO_RPC_MAX_IN_FLIGHT	= 32,
	IO_RPC_ITEM_TIMEOUT	= 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);
extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
extern const struct c2_net_buffer_callbacks rpc_bulk_cb;
extern int c2_reqh_init(struct c2_reqh *reqh, struct c2_dtm *dtm,
                struct c2_stob_domain *stdom, struct c2_dbenv *db,
                struct c2_cob_domain *cdom, struct c2_fol *fol);
extern void c2_reqh_fini(struct c2_reqh *reqh);

struct bulkio_values {
	char *bds_sdbname;
	char *bds_cdbname;
	char *bds_saddr;
	char *bds_caddr;
};

/* Mutex used to wait between bulk transfer. */
struct c2_mutex bulkio_mutex;

struct bulkio_values bio_val = {
	.bds_sdbname = "bulkio_rpcsrv_db",
	.bds_cdbname = "bulkio_rpccli_db",
	.bds_caddr   = "127.0.0.1:12345:12",
	.bds_saddr   = "127.0.0.1:12345:11",
};

struct bulkio_rpc_server {
	struct c2_rpcmachine    brs_mach;
	struct c2_dbenv         brs_dbenv;
	struct c2_cob_domain    brs_cobdomain;
	struct c2_cob_domain_id brs_cobdomain_id;
	struct c2_reqh          brs_reqh;
	char                    brs_laddr[IO_ADDR_LEN];
};

struct bulkio_rpc_client {
	struct c2_dbenv           brc_dbenv;
	struct c2_cob_domain	  brc_cobdomain;
	struct c2_rpc_client_ctx  brc_cctx;
};

void bulkio_fom_fini(struct c2_fom *fom)
{
}

int bulkio_fom_state(struct c2_fom *fom)
{
	int rc;
	uint32_t i;
	struct c2_clink clink;
	struct c2_net_buffer **netbufs;
	struct c2_fop_cob_rw *rw;
	struct c2_rpc_conn   *conn;
	struct c2_rpc_bulk   *rbulk;
	struct c2_io_indexvec *ivec;
	struct c2_rpc_bulk_buf *rbuf;
	struct c2_fop_cob_writev_rep *wrep;

	c2_mutex_lock(&bulkio_mutex);

	rw = io_rw_get(fom->fo_fop);
	C2_UT_ASSERT(rw->crw_desc.id_nr != 0);
	C2_UT_ASSERT(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);
	conn = fom->fo_fop->f_item.ri_session->s_conn;

	C2_ALLOC_ARR(netbufs, rw->crw_desc.id_nr);
	C2_UT_ASSERT(netbufs != NULL);

	C2_ALLOC_PTR(rbulk);
	C2_UT_ASSERT(rbulk != NULL);

	c2_rpc_bulk_init(rbulk);

	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];

		C2_ALLOC_PTR(netbufs[i]);
		C2_UT_ASSERT(netbufs[i] != NULL);

		rc = c2_bufvec_alloc_aligned(&netbufs[i]->nb_buffer,
					     ivec->ci_nr,
					     ivec->ci_iosegs[0].ci_count,
					     C2_0VEC_SHIFT);

		C2_UT_ASSERT(rc == 0);

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 netbufs[i], &rbuf);

		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf != NULL);

		rbuf->bb_nbuf->nb_qtype = C2_NET_QT_ACTIVE_BULK_RECV;
	}

	c2_clink_init(&clink, NULL);
	c2_clink_add(&rbulk->rb_chan, &clink);
	rc = c2_rpc_bulk_load(rbulk, conn, rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&clink);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_mutex_lock(&rbulk->rb_mutex);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	C2_UT_ASSERT(rbulk->rb_rc == 0);
	c2_mutex_unlock(&rbulk->rb_mutex);

	wrep = c2_fop_data(fom->fo_rep_fop);
	wrep->c_rep.rwr_count = rbulk->rb_bytes;
	wrep->c_rep.rwr_rc = rbulk->rb_rc;

	fom->fo_rep_fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
	C2_UT_ASSERT(rc == 0);

	fom->fo_phase = FOPH_FINISH;

	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		c2_bufvec_free_aligned(&netbufs[i]->nb_buffer, C2_0VEC_SHIFT);
		c2_free(netbufs[i]);
	}
	c2_free(netbufs);
	c2_rpc_bulk_fini(rbulk);
	c2_free(rbulk);
	c2_mutex_unlock(&bulkio_mutex);

	return rc;
}

size_t bulkio_fom_locality_get(const struct c2_fom *fom)
{
	return 0;
}

static const struct c2_fom_ops bulkio_fom_ops = {
	.fo_fini = bulkio_fom_fini,
	.fo_state = bulkio_fom_state,
	.fo_home_locality = bulkio_fom_locality_get,
	.fo_service_name = NULL,
};

int bulkio_ut_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	struct c2_fom *fom;

	C2_PRE(fop != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(fom);
	C2_UT_ASSERT(fom != NULL);
	c2_fom_init(fom);

	fom->fo_type = &fop->f_type->ft_fom_type;
	fom->fo_ops = &bulkio_fom_ops;
	fom->fo_fop = fop;
	fom->fo_rep_fop = c2_is_read_fop(fop) ?
			  c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL) :
			  c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
	C2_UT_ASSERT(fom->fo_rep_fop != NULL);
	*out = fom;
	return 0;
}

static const struct c2_fom_type_ops bulkio_fom_type_ops = {
	.fto_create = bulkio_ut_fom_create,
};

/* Original fom type for io fop. */
static struct c2_fom_type bulkio_fop_orig_fom;

static struct c2_fom_type bulkio_fom_type  = {
	.ft_ops = &bulkio_fom_type_ops,
};

static void bulkio_rpcserver_start(struct bulkio_rpc_server *srpc,
				   struct c2_net_domain *nd)
{
	int rc;

	C2_UT_ASSERT(srpc != NULL);

	rc = c2_dbenv_init(&srpc->brs_dbenv, bio_val.bds_sdbname, 0);
	C2_UT_ASSERT(rc == 0);

	srpc->brs_cobdomain_id.id = IO_CCOBDOMAIN_ID;
	rc = c2_cob_domain_init(&srpc->brs_cobdomain, &srpc->brs_dbenv,
				&srpc->brs_cobdomain_id);
	C2_UT_ASSERT(rc == 0);

	rc = c2_processors_init();
	C2_UT_ASSERT(rc == 0);
	rc = c2_reqh_init(&srpc->brs_reqh, NULL, NULL, &srpc->brs_dbenv,
			  &srpc->brs_cobdomain, NULL);
	C2_UT_ASSERT(rc == 0);

	memcpy(srpc->brs_laddr, bio_val.bds_saddr, strlen(bio_val.bds_saddr));
	rc = c2_rpcmachine_init(&srpc->brs_mach, &srpc->brs_cobdomain, nd,
				srpc->brs_laddr, &srpc->brs_reqh);
	C2_UT_ASSERT(rc == 0);
}

static void bulkio_rpcserver_stop(struct bulkio_rpc_server *srpc)
{
	c2_rpcmachine_fini(&srpc->brs_mach);
	c2_reqh_fini(&srpc->brs_reqh);
	c2_cob_domain_fini(&srpc->brs_cobdomain);
	c2_dbenv_fini(&srpc->brs_dbenv);
	c2_processors_fini();
}

static void bulkio_rpcclient_start(struct bulkio_rpc_client *crpc,
				   struct c2_net_domain *nd)
{
	int                      rc;
	struct c2_rpc_client_ctx *cctx;

	C2_UT_ASSERT(crpc != NULL);

	cctx = &crpc->brc_cctx;
	cctx->rcx_remote_addr = bio_val.bds_saddr;
	cctx->rcx_cob_dom_id  = IO_SCOBDOMAIN_ID;
	cctx->rcx_nr_slots    = IO_SESSION_SLOT_NR;
	cctx->rcx_timeout_s   = IO_RPC_CONN_TIMEOUT;
	cctx->rcx_local_addr  = bio_val.bds_caddr;
	cctx->rcx_net_dom     = nd;
	cctx->rcx_db_name     = bio_val.bds_cdbname;
	cctx->rcx_dbenv       = &crpc->brc_dbenv;
	cctx->rcx_cob_dom     = &crpc->brc_cobdomain;
	cctx->rcx_max_rpcs_in_flight = IO_RPC_MAX_IN_FLIGHT;

	rc = c2_rpc_client_init(cctx);
	C2_UT_ASSERT(rc == 0);
}

static void bulkio_rpcclient_stop(struct bulkio_rpc_client *crpc)
{
	int rc;

	C2_UT_ASSERT(crpc != NULL);
	rc = c2_rpc_client_fini(&crpc->brc_cctx);
	C2_UT_ASSERT(rc == 0);
}

static void bulkio_rpcitem_send_wait(struct c2_io_fop *iofop,
				     struct c2_rpc_session *session)
{
	int                 rc;
	c2_time_t           timeout;
	struct c2_clink     clink;
	struct c2_rpc_item *item;

	C2_UT_ASSERT(iofop != NULL);
	C2_UT_ASSERT(session != NULL);

	item = &iofop->if_fop.f_item;
	item->ri_session = session;
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	c2_time_set(&timeout, IO_RPC_ITEM_TIMEOUT, 0);
	timeout = c2_time_add(timeout, c2_time_now());
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	rc = c2_rpc_post(item);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_reply_timedwait(&clink, timeout);
	C2_UT_ASSERT(rc == 0);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

void bulkioapi_test(void)
{
	int			   rc;
	int			   i = 0;
	char			  *sbuf;
	int32_t			   max_segs;
	c2_bcount_t		   max_seg_size;
	c2_bcount_t		   max_buf_size;
	struct c2_io_fop	  *iofop;
	struct c2_net_xprt	  *xprt;
	struct c2_rpc_bulk	  *rbulk;
	struct c2_fop_cob_rw	  *rw;
	struct c2_net_domain	   nd;
	struct c2_fop_type	  *ftype;
	struct c2_net_buffer	  *nb;
	struct c2_rpc_bulk_buf	  *rbuf;
	struct c2_rpc_bulk_buf	  *rbuf1;
	struct bulkio_rpc_server  *srpc;
	struct bulkio_rpc_client  *crpc;

	C2_SET0(&nd);

	C2_ALLOC_PTR(iofop);
	C2_UT_ASSERT(iofop != NULL);

	c2_mutex_init(&bulkio_mutex);
	c2_addb_choose_default_level_console(AEL_ERROR);
	xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(rc == 0);
	rc = c2_net_domain_init(&nd, xprt);
	C2_UT_ASSERT(rc == 0);

	/* Test : c2_io_fop_init() */
	rc = c2_io_fop_init(iofop, &c2_fop_cob_writev_fopt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(iofop->if_magic == C2_IO_FOP_MAGIC);
	C2_UT_ASSERT(iofop->if_fop.f_type != NULL);
	C2_UT_ASSERT(iofop->if_fop.f_item.ri_type != NULL);
	C2_UT_ASSERT(iofop->if_fop.f_item.ri_ops  != NULL);

	C2_UT_ASSERT(iofop->if_rbulk.rb_buflist.t_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop->if_rbulk.rb_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop->if_rbulk.rb_bytes == 0);
	C2_UT_ASSERT(iofop->if_rbulk.rb_rc    == 0);

	/* Test : c2_fop_to_rpcbulk() */
	rbulk = c2_fop_to_rpcbulk(&iofop->if_fop);
	C2_UT_ASSERT(rbulk != NULL);
	C2_UT_ASSERT(rbulk == &iofop->if_rbulk);

	/* Test : c2_rpc_bulk_buf_add() */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SINGLE_BUFFER, &nd, NULL, &rbuf);
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

	rc = c2_rpc_bulk_buf_add(rbulk, IO_SINGLE_BUFFER, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, C2_0VEC_ALIGN, 0, &nd);
	C2_UT_ASSERT(rc == 0);

	/* Test : c2_rpc_bulk_buf_add(nb != NULL)*/
	C2_ALLOC_PTR(nb);
	C2_UT_ASSERT(nb != NULL);
	rc = c2_bufvec_alloc_aligned(&nb->nb_buffer, IO_SINGLE_BUFFER,
				     C2_0VEC_ALIGN, C2_0VEC_SHIFT);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_bulk_buf_add(rbulk, IO_SINGLE_BUFFER, &nd, nb, &rbuf1);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf1 != NULL);
	C2_UT_ASSERT(rbuf1->bb_nbuf == nb);
	C2_UT_ASSERT(!rbuf1->bb_flags & C2_RPC_BULK_NETBUF_ALLOCATED);

	C2_UT_ASSERT(rbuf->bb_nbuf != NULL);
	rc = c2_io_fop_prepare(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);
	rw = io_rw_get(&iofop->if_fop);
	C2_UT_ASSERT(rw != NULL);

	C2_ALLOC_PTR(srpc);
	C2_UT_ASSERT(srpc != NULL);
	bulkio_rpcserver_start(srpc, &nd);
#ifdef __KERNEL__
	printk(KERN_ERR "rpc server started.\n");
#endif

	C2_ALLOC_PTR(crpc);
	C2_UT_ASSERT(crpc != NULL);
	bulkio_rpcclient_start(crpc, &nd);
#ifdef __KERNEL__
	printk(KERN_ERR "rpc client started and connected with server.\n");
#endif

	bulkio_fop_orig_fom = iofop->if_fop.f_type->ft_fom_type;
	ftype = iofop->if_fop.f_type;
	iofop->if_fop.f_type->ft_fom_type = bulkio_fom_type;

	/*
	 * Receiving side also tries to take the same lock and blocks for
	 * the mutex. Meanwhile, all data in c2_rpc_bulk structures can
	 * be asserted.
	 */
	c2_mutex_lock(&bulkio_mutex);

	rc = c2_rpc_bulk_store(&iofop->if_rbulk,
			       &crpc->brc_cctx.rcx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);
#ifdef __KERNEL__
	printk(KERN_ERR "bulk_store() successful.\n");
#endif

	c2_mutex_lock(&rbulk->rb_mutex);
	c2_tlist_for(&rpcbulk_tl, &rbulk->rb_buflist, rbuf) {
		C2_UT_ASSERT(rbuf->bb_nbuf->nb_callbacks == &rpc_bulk_cb);
		C2_UT_ASSERT(rbuf->bb_nbuf != NULL);
		C2_UT_ASSERT(rbuf->bb_nbuf->nb_app_private == rbuf);
		C2_UT_ASSERT(rbuf->bb_nbuf->nb_flags & C2_NET_BUF_REGISTERED);
		C2_UT_ASSERT(rbuf->bb_nbuf->nb_flags & C2_NET_BUF_QUEUED);
		rc = memcmp(rbuf->bb_nbuf->nb_desc.nbd_data,
			    rw->crw_desc.id_descs[i].nbd_data,
			    rbuf->bb_nbuf->nb_desc.nbd_len);
		C2_UT_ASSERT(rc == 0);
		++i;
	} c2_tlist_endfor;
	C2_UT_ASSERT(rbulk->rb_bytes ==  2 * C2_0VEC_ALIGN);
	c2_mutex_unlock(&rbulk->rb_mutex);

	c2_mutex_unlock(&bulkio_mutex);

	bulkio_rpcitem_send_wait(iofop, &crpc->brc_cctx.rcx_session);
#ifdef __KERNEL__
	printk(KERN_ERR "fop send successful.\n");
#endif
	ftype->ft_fom_type = bulkio_fop_orig_fom;

	bulkio_rpcclient_stop(crpc);
	bulkio_rpcserver_stop(srpc);

	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	/* Cleanup. */
	c2_bufvec_free_aligned(&nb->nb_buffer, C2_0VEC_SHIFT);
	c2_free(nb);
	c2_free(srpc);
	c2_free(crpc);
	c2_free_aligned(sbuf, C2_0VEC_ALIGN, C2_0VEC_SHIFT);

	c2_net_domain_fini(&nd);
}

const struct c2_test_suite bulkio_client_ut = {
	.ts_name = "bulk-client-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bulkio_apitest",	  bulkioapi_test},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_client_ut);
