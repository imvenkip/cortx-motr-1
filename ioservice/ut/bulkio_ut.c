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

#include "ioservice/io_fops.c"	/* To access static apis for testing. */
#include "ioservice/io_foms.c"
#include "bulkio_common.c"
#include "bulkio_server_ut.c"

static int io_fop_dummy_fom_create(struct c2_fop *fop, struct c2_fom **m);

/*
 * An alternate io fop type op vector to test bulk client functionality only.
 * Only .fto_fom_init is pointed to a UT function which tests the received
 * io fop is sane and bulk io transfer is taking place properly using data
 * from io fop. Rest all ops are same as io_fop_rwv_ops.
 */
struct c2_fop_type_ops bulkio_fop_ut_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

static struct c2_fom_type_ops bulkio_dummy_fom_type_ops = {
	.fto_create = io_fop_dummy_fom_create,
};

static struct c2_fom_type bulkio_dummy_fom_type = {
	.ft_ops = &bulkio_dummy_fom_type_ops,
};

static void bulkio_fom_fini(struct c2_fom *fom)
{
	c2_fom_fini(fom);
	c2_free(fom);
}

/*
 * It is the dummy fom state used by the bulk client to check the
 *  functionality.
 */
static int bulkio_fom_state(struct c2_fom *fom)
{
	int				 rc;
	uint32_t			 i;
	uint32_t			 j;
	uint32_t			 k;
	c2_bcount_t			 tc;
        struct c2_fop                   *fop;
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
	for (i = 0; i < rw->crw_desc.id_nr &&
                c2_is_write_fop(fom->fo_fop); ++i) {
		for (j = 0; j < netbufs[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(bp->bp_writebuf,
				    netbufs[i]->nb_buffer.ov_buf[j],
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

static int io_fop_dummy_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom *fom;

        C2_ALLOC_PTR(fom);
	C2_UT_ASSERT(fom != NULL);

	fom->fo_fop = fop;
	c2_fom_init(fom);
	fop->f_type->ft_fom_type = bulkio_dummy_fom_type;
	fom->fo_ops = &bulkio_fom_ops;
        fom->fo_type = &bulkio_dummy_fom_type;

	*m = fom;
	return 0;

}

static void bulkio_test(int fids_nr, int fops_nr, int segs_nr)
{
	int		    rc;
	int		    i;
	enum C2_RPC_OPCODES op;
	struct thrd_arg	   *targ;
	struct c2_io_fop  **io_fops;

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
		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops : bp->bp_rfops;
		for (i = 0; i < fops_nr; ++i) {
			io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ut_ops;
                        io_fops[i]->if_fop.f_type->ft_fom_type =
			bulkio_dummy_fom_type;
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

	bulkio_stob_create();
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
	int32_t			 max_segs;
	c2_bcount_t		 max_seg_size;
	c2_bcount_t		 max_buf_size;
	struct c2_io_fop	 iofop;
	struct c2_net_xprt	*xprt;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_net_domain	 nd;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_rpc_bulk_buf	*rbuf1;

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

	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	/* Cleanup. */
	c2_free(sbuf);

	c2_io_fop_fini(&iofop);
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
		{ "bulkio_server_single_read_write",
		   bulkio_server_single_read_write},
		{ "bulkio_server_read_write_state_test",
		   bulkio_server_read_write_state_test},
		{ "bulkio_server_vectored_read_write",
		   bulkio_server_multiple_read_write},
		{ "bulkio_server_rw_multiple_nb_server",
		   bulkio_server_read_write_multiple_nb},
		{ "bulkio_server_rw_state_transition_test",
		   bulkio_server_rw_state_transition_test},
		{ "bulkio_fini",	  bulkio_fini},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
