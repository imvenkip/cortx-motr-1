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
#include "net/lnet/lnet.h"

enum {
	IO_SINGLE_BUFFER	= 1,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);
extern struct c2_net_xprt c2_net_lnet_xprt;
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
extern const struct c2_net_buffer_callbacks rpc_bulk_cb;
extern void c2_lut_lhost_lnet_conv(struct c2_net_domain *ndom, char *ep_addr);

static void bulkio_tm_cb(const struct c2_net_tm_event *ev)
{
}

static struct c2_net_tm_callbacks bulkio_ut_tm_cb = {
	.ntc_event_cb = bulkio_tm_cb
};

/*
 * This structure represents message sending/receiving entity on network
 * for either client or server. Since rpc can not be used as is,
 * this structure just tries to start transfer machines on both ends.
 * Since uiltimately rpc uses transfer machine rpc, message
 * sending/receiving can be achieved easily without having to go
 * through rpc interfaces.
 * The c2_rpc_conn member is just a placeholder. It is needed for
 * c2_rpc_bulk_{store/load} APIs.
 */
struct bulkio_msg_tm {
	struct c2_rpc_machine bmt_mach;
	struct c2_rpc_conn    bmt_conn;
	const char           *bmt_addr;
};

static void bulkio_msg_tm_init(struct bulkio_msg_tm *bmt,
			       struct c2_net_domain *nd)
{
	int                        rc;
	struct c2_clink            clink;
	struct c2_net_transfer_mc *tm;

	C2_UT_ASSERT(bmt != NULL);
	C2_UT_ASSERT(nd != NULL);
	C2_UT_ASSERT(bmt->bmt_addr != NULL);
	C2_UT_ASSERT(bmt->bmt_mach.rm_tm.ntm_state == C2_NET_TM_UNDEFINED);

	tm = &bmt->bmt_mach.rm_tm;
	C2_SET0(&bmt->bmt_conn);
	bmt->bmt_conn.c_rpc_machine = &bmt->bmt_mach;

	tm->ntm_state = C2_NET_TM_UNDEFINED;
	tm->ntm_callbacks = &bulkio_ut_tm_cb;
	rc = c2_net_tm_init(tm, nd);
	C2_UT_ASSERT(rc == 0);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&tm->ntm_chan, &clink);
	rc = c2_net_tm_start(tm, bmt->bmt_addr);
	C2_UT_ASSERT(rc == 0);

	while (tm->ntm_state != C2_NET_TM_STARTED)
		c2_chan_wait(&clink);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

static void bulkio_msg_tm_fini(struct bulkio_msg_tm *bmt)
{
	int rc;
	struct c2_clink clink;

	C2_UT_ASSERT(bmt != NULL);
	C2_UT_ASSERT(bmt->bmt_addr != NULL);
	C2_UT_ASSERT(bmt->bmt_mach.rm_tm.ntm_state == C2_NET_TM_STARTED);
	C2_UT_ASSERT(bmt->bmt_conn.c_rpc_machine == &bmt->bmt_mach);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&bmt->bmt_mach.rm_tm.ntm_chan, &clink);

	rc = c2_net_tm_stop(&bmt->bmt_mach.rm_tm, false);
	C2_UT_ASSERT(rc == 0);

	while(bmt->bmt_mach.rm_tm.ntm_state != C2_NET_TM_STOPPED)
		c2_chan_wait(&clink);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_net_tm_fini(&bmt->bmt_mach.rm_tm);
}

static void bulkclient_test(void)
{
	int			   rc;
	int                        i = 0;
	char			  *sbuf;
	int32_t			   max_segs;
	c2_bcount_t		   max_seg_size;
	c2_bcount_t		   max_buf_size;
	struct c2_clink            clink;
	struct c2_io_fop	   iofop;
	struct c2_net_xprt	  *xprt;
	struct c2_rpc_bulk	  *rbulk;
	struct c2_rpc_bulk	  *sbulk;
	struct c2_fop_cob_rw	  *rw;
	struct c2_net_domain	   nd;
	struct c2_net_buffer	  *nb;
	struct c2_net_buffer     **nbufs;
	struct c2_rpc_bulk_buf	  *rbuf;
	struct c2_rpc_bulk_buf	  *rbuf1;
	struct c2_rpc_bulk_buf	  *rbuf2;
	char                       caddr[C2_NET_LNET_XEP_ADDR_LEN] =
					"127.0.0.1@tcp:12345:34:7";
	char		           saddr[C2_NET_LNET_XEP_ADDR_LEN] =
					"127.0.0.1@tcp:12345:34:8";
	struct c2_io_indexvec	  *ivec;
	enum c2_net_queue_type	   q;
	struct bulkio_msg_tm      *ctm;
	struct bulkio_msg_tm      *stm;

	C2_SET0(&iofop);
	C2_SET0(&nd);

	c2_addb_choose_default_level_console(AEL_ERROR);
	xprt = &c2_net_lnet_xprt;
	rc = c2_net_domain_init(&nd, xprt);
	C2_UT_ASSERT(rc == 0);
	c2_lut_lhost_lnet_conv(&nd, caddr);
	c2_lut_lhost_lnet_conv(&nd, saddr);

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
	memset(sbuf, 'a', C2_0VEC_ALIGN);
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
	memset(nb->nb_buffer.ov_buf[IO_SINGLE_BUFFER - 1], 'a', C2_0VEC_ALIGN);

	rc = c2_rpc_bulk_buf_add(rbulk, IO_SINGLE_BUFFER, &nd, nb, &rbuf1);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf1 != NULL);
	C2_UT_ASSERT(rbuf1->bb_nbuf == nb);
	C2_UT_ASSERT(!rbuf1->bb_flags & C2_RPC_BULK_NETBUF_ALLOCATED);

	C2_UT_ASSERT(rbuf->bb_nbuf != NULL);
	rc = c2_io_fop_prepare(&iofop.if_fop);
	C2_UT_ASSERT(rc == 0);
	rw = io_rw_get(&iofop.if_fop);
	C2_UT_ASSERT(rw != NULL);

	C2_ALLOC_PTR(ctm);
	C2_UT_ASSERT(ctm != NULL);
	ctm->bmt_addr = caddr;
	bulkio_msg_tm_init(ctm, &nd);

	rc = c2_rpc_bulk_store(rbulk, &ctm->bmt_conn, rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);

	/*
	 * There is no ACTIVE side _yet_ to start the bulk transfer and
	 * hence the buffer is guaranteed to stay put in the
	 * PASSIVE_BULK_SEND queue of TM.
	 */
	c2_mutex_lock(&rbulk->rb_mutex);
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
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
	} c2_tl_endfor;
	C2_UT_ASSERT(rbulk->rb_bytes ==  2 * C2_0VEC_ALIGN);
	c2_mutex_unlock(&rbulk->rb_mutex);

	/* Start server side TM. */
	C2_ALLOC_PTR(stm);
	C2_UT_ASSERT(stm != NULL);
	stm->bmt_addr = saddr;
	bulkio_msg_tm_init(stm, &nd);

	/*
	 * Bulk server (receiving side) typically uses c2_rpc_bulk structure
	 * without having to use c2_io_fop.
	 */
	C2_ALLOC_PTR(sbulk);
	C2_UT_ASSERT(sbulk != NULL);

	/*
	 * Pretends that io fop is received and starts zero copy.
	 * Actual fop can not be sent since rpc server hands over any
	 * incoming fop to associated request handler. And request
	 * handler does not work in kernel space at the moment.
	 */
	c2_rpc_bulk_init(sbulk);

	C2_ALLOC_ARR(nbufs, rw->crw_desc.id_nr);
	C2_UT_ASSERT(nbufs != NULL);
	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];
		C2_ALLOC_PTR(nbufs[i]);
		C2_UT_ASSERT(nbufs[i] != NULL);
		rc = c2_bufvec_alloc_aligned(&nbufs[i]->nb_buffer, ivec->ci_nr,
					     C2_0VEC_ALIGN, C2_0VEC_SHIFT);
		C2_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];
		rc = c2_rpc_bulk_buf_add(sbulk, ivec->ci_nr, &nd, nbufs[i],
					 &rbuf2);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf2 != NULL);
	}

	c2_mutex_lock(&sbulk->rb_mutex);
	q = c2_is_read_fop(&iofop.if_fop) ? C2_NET_QT_ACTIVE_BULK_SEND :
	    C2_NET_QT_ACTIVE_BULK_RECV;
	c2_rpc_bulk_qtype(sbulk, q);
	c2_mutex_unlock(&sbulk->rb_mutex);

	c2_clink_init(&clink, NULL);
	c2_clink_add(&sbulk->rb_chan, &clink);
	rc = c2_rpc_bulk_load(sbulk, &stm->bmt_conn, rw->crw_desc.id_descs);

	/*
	 * Buffer completion callbacks also wait to acquire the
	 * c2_rpc_bulk::rb_mutex and in any case asserts inside the loop
	 * are protected from buffer completion callbacks which do some
	 * cleanup due to the lock.
	 */
	c2_mutex_lock(&sbulk->rb_mutex);
	c2_tl_for(rpcbulk, &sbulk->rb_buflist, rbuf2) {
		C2_UT_ASSERT(rbuf2->bb_nbuf != NULL);
		C2_UT_ASSERT(rbuf2->bb_nbuf->nb_flags & C2_NET_BUF_REGISTERED);
		C2_UT_ASSERT(rbuf2->bb_nbuf->nb_app_private == rbuf2);
		C2_UT_ASSERT(rbuf2->bb_nbuf->nb_flags & C2_NET_BUF_QUEUED);
		C2_UT_ASSERT(rbuf2->bb_rbulk == sbulk);
		C2_UT_ASSERT(!(rbuf2->bb_flags & C2_RPC_BULK_NETBUF_ALLOCATED));
		C2_UT_ASSERT(rbuf2->bb_flags & C2_RPC_BULK_NETBUF_REGISTERED);
	} c2_tl_endfor;
	C2_UT_ASSERT(sbulk->rb_bytes == 2 * C2_0VEC_ALIGN);
	c2_mutex_unlock(&sbulk->rb_mutex);

	/* Waits for zero copy to complete. */
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&clink);

	c2_mutex_lock(&sbulk->rb_mutex);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &sbulk->rb_buflist));
	c2_mutex_unlock(&sbulk->rb_mutex);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
	c2_rpc_bulk_fini(sbulk);

	bulkio_msg_tm_fini(ctm);
	bulkio_msg_tm_fini(stm);
	c2_free(ctm);
	c2_free(stm);

	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		rc = memcmp(nbufs[i]->nb_buffer.ov_buf[IO_SINGLE_BUFFER - 1],
			    sbuf, C2_0VEC_ALIGN);
		C2_UT_ASSERT(rc == 0);
		c2_bufvec_free_aligned(&nbufs[i]->nb_buffer, C2_0VEC_SHIFT);
		c2_free(nbufs[i]);
	}
	c2_free(nbufs);
	c2_free(sbulk);

	c2_io_fop_destroy(&iofop.if_fop);
	C2_UT_ASSERT(rw->crw_desc.id_descs   == NULL);
	C2_UT_ASSERT(rw->crw_desc.id_nr      == 0);
	C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs == NULL);
	C2_UT_ASSERT(rw->crw_ivecs.cis_nr    == 0);

	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	c2_bufvec_free_aligned(&nb->nb_buffer, C2_0VEC_SHIFT);
	c2_free(nb);

	/* Cleanup. */
	c2_free_aligned(sbuf, C2_0VEC_ALIGN, C2_0VEC_SHIFT);
	c2_io_fop_fini(&iofop);
	c2_net_domain_fini(&nd);
}

const struct c2_test_suite bulkio_client_ut = {
	.ts_name = "bulk-client-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "bulkclient_test", bulkclient_test},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_client_ut);
