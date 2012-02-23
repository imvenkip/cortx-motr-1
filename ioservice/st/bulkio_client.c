/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#include "ioservice/st/bulkio_common.h"

C2_TL_DESCR_DECLARE(rpcbulk, extern);
extern const struct c2_net_buffer_callbacks rpc_bulk_cb;
extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
struct c2_fop_type_ops bulkio_fop_ops;
struct c2_fom_type bulkio_fom_type;
extern bool is_read(const struct c2_fop *fop);

/*
static struct c2_rpc_client_ctx c_rctx = {
	.rcx_remote_addr	= S_ENDP_ADDR,
	.rcx_cob_dom_id		= IO_CLIENT_COBDOM_ID,
	.rcx_nr_slots		= IO_RPC_SESSION_SLOTS,
	.rcx_max_rpcs_in_flight	= IO_RPC_MAX_IN_FLIGHT,
	.rcx_timeout_s		= IO_RPC_CONN_TIMEOUT,
};
*/

static void io_fids_init(struct bulkio_params *bp)
{
	int i;

	C2_ASSERT(bp != NULL);
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
void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
	       c2_bcount_t seg_size)
{
	uint32_t i;

	bvec->ov_vec.v_nr = segs_nr;
	C2_ALLOC_ARR(bvec->ov_vec.v_count, segs_nr);
	C2_ASSERT(bvec->ov_vec.v_count != NULL);
	C2_ALLOC_ARR(bvec->ov_buf, segs_nr);
	C2_ASSERT(bvec->ov_buf != NULL);

	for (i = 0; i < segs_nr; ++i) {
		bvec->ov_buf[i] = c2_alloc_aligned(C2_0VEC_ALIGN,
						   C2_0VEC_SHIFT);
		C2_ASSERT(bvec->ov_buf[i] != NULL);
		bvec->ov_vec.v_count[i] = C2_0VEC_ALIGN;
	}
}

static void io_buffers_allocate(struct bulkio_params *bp)
{
	int i;

	C2_ASSERT(bp != NULL);

	/* Initialized the standard buffer with a data pattern for read IO. */
	memset(bp->bp_readbuf, 'b', C2_0VEC_ALIGN);
	memset(bp->bp_writebuf, 'a', C2_0VEC_ALIGN);

	for (i = 0; i < IO_FOPS_NR; ++i)
		vec_alloc(&bp->bp_iobuf[i]->nb_buffer, IO_SEGS_NR,
			  C2_0VEC_ALIGN);
}

static void io_buffers_deallocate(struct bulkio_params *bp)
{
	int i;

	C2_ASSERT(bp != NULL);

	for (i = 0; i < IO_FOPS_NR; ++i)
		c2_bufvec_free(&bp->bp_iobuf[i]->nb_buffer);
}

static void io_fop_populate(struct bulkio_params *bp, int index,
			    uint64_t off_index, struct c2_io_fop **io_fops,
			    int segs_nr)
{
	int			 i;
	int			 rc;
	struct c2_io_fop	*iofop;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*rw;

	C2_ASSERT(bp != NULL);
	C2_ASSERT(io_fops != NULL);

	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;

	/*
	 * Adds a c2_rpc_bulk_buf structure to list of such structures
	 * in c2_rpc_bulk.
	 */
	rc = c2_rpc_bulk_buf_add(rbulk, segs_nr, &bp->bp_cnetdom, NULL, &rbuf);
	C2_ASSERT(rc == 0);
	C2_ASSERT(rbuf != NULL);

	rw = io_rw_get(&iofop->if_fop);
	rw->crw_fid = bp->bp_fids[off_index];

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < segs_nr; ++i) {
		rc = c2_rpc_bulk_buf_databuf_add(rbuf,
				bp->bp_iobuf[index]->nb_buffer.ov_buf[i],
				bp->bp_iobuf[index]->nb_buffer.ov_vec.
				v_count[i],
				bp->bp_offsets[off_index], &bp->bp_cnetdom);
		C2_ASSERT(rc == 0);

		bp->bp_offsets[off_index] +=
			bp->bp_iobuf[index]->nb_buffer.ov_vec.v_count[i];
	}

	/*
	 * Allocates memory for array of net buf descriptors and array of
	 * index vectors from io fop.
	 */
	rc = c2_io_fop_prepare(&iofop->if_fop);
	C2_ASSERT(rc == 0);
	C2_ASSERT(rw->crw_desc.id_nr ==
		     c2_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist));
	C2_ASSERT(rw->crw_desc.id_descs != NULL);

	/*
	 * Stores the net buf desc/s after adding the corresponding
	 * net buffers to transfer machine to io fop wire format.
	 */
	rc = c2_rpc_bulk_store(rbulk, &bp->bp_cctx->rcx_connection,
			       rw->crw_desc.id_descs);
	C2_ASSERT(rc == 0);

}

static void io_fops_create(struct bulkio_params *bp, enum C2_RPC_OPCODES op,
			   int fids_nr, int fops_nr, int segs_nr)
{
	int			  i;
	int			  rc;
	uint64_t		  seed;
	uint64_t		  rnd;
	struct c2_fop_type	 *fopt;
	struct c2_io_fop	**io_fops;

	seed = 0;
	for (i = 0; i < fids_nr; ++i)
		bp->bp_offsets[i] = IO_SEG_START_OFFSET;
	if (op == C2_IOSERVICE_WRITEV_OPCODE) {
		C2_ASSERT(bp->bp_wfops == NULL);
		C2_ALLOC_ARR(bp->bp_wfops, fops_nr);
		fopt = &c2_fop_cob_writev_fopt;
		io_fops = bp->bp_wfops;
	} else {
		C2_ASSERT(bp->bp_rfops == NULL);
		C2_ALLOC_ARR(bp->bp_rfops, fops_nr);
		fopt = &c2_fop_cob_readv_fopt;
		io_fops = bp->bp_rfops;
	}
	C2_ASSERT(io_fops != NULL);

	/* Allocates io fops. */
	for (i = 0; i < fops_nr; ++i) {
		C2_ALLOC_PTR(io_fops[i]);
		C2_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], fopt);
		C2_ASSERT(rc == 0);
		io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ops;
                io_fops[i]->if_fop.f_type->ft_fom_type = bulkio_fom_type;

	}

	/* Populates io fops. */
	for (i = 0; i < fops_nr; ++i) {
		if (fids_nr < fops_nr) {
			rnd = c2_rnd(fids_nr, &seed);
			C2_UT_ASSERT(rnd < fids_nr);
		}
		else rnd = i;

		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
			   bp->bp_rfops;
		io_fop_populate(bp, i, rnd, io_fops, segs_nr);
	}
}

static void io_fops_destroy(struct bulkio_params *bp)
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
	struct bulkio_params     *bp;

	i = t->ta_index;
	bp = t->ta_bp;
	io_fops = (t->ta_op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
		  bp->bp_rfops;
	rbulk = c2_fop_to_rpcbulk(&io_fops[i]->if_fop);
	item = &io_fops[i]->if_fop.f_item;
	item->ri_session = &bp->bp_cctx->rcx_session;
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
	C2_ASSERT(rc == 0);

	rc = c2_rpc_reply_timedwait(&clink, timeout);
	if (c2_is_read_fop(&io_fops[i]->if_fop)) {
		for (j = 0; j < bp->bp_iobuf[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(bp->bp_iobuf[i]->nb_buffer.ov_buf[j],
				    bp->bp_readbuf,
				    bp->bp_iobuf[i]->nb_buffer.ov_vec.
				    	v_count[j]);
			C2_ASSERT(rc == 0);
			memset(bp->bp_iobuf[i]->nb_buffer.ov_buf[j], 'a',
			       C2_0VEC_ALIGN);
		}
		c2_mutex_lock(&rbulk->rb_mutex);
		C2_ASSERT(rbulk->rb_rc == 0);
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

void bulkio_test(struct bulkio_params *bp, int fids_nr, int fops_nr,
		 int segs_nr)
{
	int		    rc;
	int		    i;
	enum C2_RPC_OPCODES op;
	struct thrd_arg	   *targ;

	C2_ASSERT(bp != NULL);
	C2_ALLOC_ARR(targ, fops_nr);
	C2_ASSERT(targ != NULL);

	for (op = C2_IOSERVICE_READV_OPCODE; op <= C2_IOSERVICE_WRITEV_OPCODE;
	     ++op) {
		C2_ASSERT(op == C2_IOSERVICE_READV_OPCODE ||
			     op == C2_IOSERVICE_WRITEV_OPCODE);

		/*
		 * IO fops are deallocated by an rpc item type op on receiving
		 * the reply fop. See io_item_free().
		 */
		io_fops_create(bp, op, fids_nr, fops_nr, segs_nr);
		for (i = 0; i < fops_nr; ++i) {
			targ[i].ta_index = i;
			targ[i].ta_op = op;
			targ[i].ta_bp = bp;
			C2_SET0(bp->bp_threads[i]);
			rc = C2_THREAD_INIT(bp->bp_threads[i],
					    struct thrd_arg *,
					    NULL, &io_fops_rpc_submit,
					    &targ[i], "io_thrd");
			C2_ASSERT(rc == 0);
		}

		/* Waits till all threads finish their job. */
		for (i = 0; i < fops_nr; ++i)
			c2_thread_join(bp->bp_threads[i]);
	}
	c2_free(targ);
	io_fops_destroy(bp);
}

void bulkio_params_init(struct bulkio_params *bp)
{
	int  i;
	int  rc;
	//char addr[]	 = "127.0.0.1:23134:2";
	//char cdbname[]	 = "bulk_c_db";
	//char sdbfile[]	 = "bulkio_ut.db";
	//char slogfile[]  = "bulkio_ut.log";
	//char sstobfile[] = "bulkio_ut_stob";

	C2_ASSERT(bp != NULL);

	/* Initialize fids and allocate buffers used for bulk transfer. */
	io_fids_init(bp);

	C2_ASSERT(bp->bp_iobuf == NULL);
	C2_ALLOC_ARR(bp->bp_iobuf, IO_FOPS_NR);
	C2_ASSERT(bp->bp_iobuf != NULL);

	C2_ASSERT(bp->bp_threads == NULL);
	C2_ALLOC_ARR(bp->bp_threads, IO_FOPS_NR);
	C2_ASSERT(bp->bp_threads != NULL);
	for (i = 0; i < IO_FOPS_NR; ++i) {
		C2_ALLOC_PTR(bp->bp_iobuf[i]);
		C2_ASSERT(bp->bp_iobuf[i] != NULL);
		C2_ALLOC_PTR(bp->bp_threads[i]);
		C2_ASSERT(bp->bp_threads[i] != NULL);
	}

	C2_ASSERT(bp->bp_readbuf == NULL);
	C2_ALLOC_ARR(bp->bp_readbuf, C2_0VEC_ALIGN);
	C2_ASSERT(bp->bp_readbuf != NULL);

	C2_ASSERT(bp->bp_writebuf == NULL);
	C2_ALLOC_ARR(bp->bp_writebuf, C2_0VEC_ALIGN);
	C2_ASSERT(bp->bp_writebuf != NULL);

	io_buffers_allocate(bp);

	bp->bp_xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_domain_init(&bp->bp_cnetdom, bp->bp_xprt);
	C2_ASSERT(rc == 0);

	//memcpy(b->bp_sdbfile, sdbfile, sizeof sdbfile);
	//memcpy(b->bp_sstobfile, sstobfile, sizeof sstobfile);
	//memcpy(b->bp_slogfile, slogfile, sizeof slogfile);
	//memcpy(b->bp_cendpaddr, addr, sizeof addr);
	//memcpy(b->bp_cdbname, cdbname, sizeof cdbname);

	/* Starts a colibri server. */
	//s_rctx.rsx_xprts = &bp->bp_xprt;
	//s_rctx.rsx_log_file_name = bp->bp_slogfile;
	//b->bp_sctx = &s_rctx;
	//rc = c2_rpc_server_start(b->bp_sctx);
	//C2_ASSERT(rc == 0);

	/* Starts an rpc client. */
	/*c_rctx.rcx_net_dom = &bp->bp_cnetdom;
	c_rctx.rcx_local_addr = bp->bp_cendpaddr;
	c_rctx.rcx_db_name = bp->bp_cdbname;
	c_rctx.rcx_dbenv = &bp->bp_cdbenv;
	c_rctx.rcx_cob_dom = &bp->bp_ccbdom;
	*/

	/*b->bp_cctx = &c_rctx;
	rc = c2_rpc_client_init(b->bp_cctx);
	C2_ASSERT(rc == 0);
	*/

	for (i = 0; i < IO_FIDS_NR; ++i)
		bp->bp_offsets[i] = IO_SEG_START_OFFSET;

	bp->bp_rfops = NULL;
	bp->bp_wfops = NULL;
}

void bulkio_params_fini(struct bulkio_params *bp)
{
	int i;

	C2_ASSERT(bp != NULL);

	c2_net_domain_fini(&bp->bp_cnetdom);
	C2_ASSERT(bp->bp_iobuf != NULL);
	io_buffers_deallocate(bp);
	for (i = 0; i < IO_FOPS_NR; ++i) {
		c2_free(bp->bp_iobuf[i]);
		c2_free(bp->bp_threads[i]);
	}
	c2_free(bp->bp_iobuf);
	c2_free(bp->bp_threads);

	C2_ASSERT(bp->bp_readbuf != NULL);
	c2_free(bp->bp_readbuf);
	C2_ASSERT(bp->bp_writebuf != NULL);
	c2_free(bp->bp_writebuf);

	C2_ASSERT(bp->bp_rfops == NULL);
	C2_ASSERT(bp->bp_wfops == NULL);

	c2_free(bp->bp_saddr);
	c2_free(bp->bp_caddr);
	c2_free(bp->bp_cdbname);
	c2_free(bp->bp_slogfile);
}

void bulkio_netep_form(const char *addr, int port, int svc_id, char *out)
{
	char str[8];

	C2_ASSERT(addr != NULL);
	C2_ASSERT(out != NULL);

	strcat(out, addr);
	strcat(out, ":");
	memset(str, 0, 8);
	sprintf(str, "%d", port);
	strcat(out, str);
	strcat(out, ":");
	memset(str, 0, 8);
	sprintf(str, "%1d", svc_id);
	strcat(out, str);
}

int bulkio_client_start(struct bulkio_params *bp, const char *caddr, int port,
			const char *saddr)
{
	int			  rc;
	char			 *cdbname;
	char			 *srv_addr;
	char			 *cli_addr;
	struct c2_rpc_client_ctx *cctx;

	C2_ASSERT(bp != NULL);
	C2_ASSERT(caddr != NULL);
	C2_ASSERT(saddr != NULL);

	C2_ALLOC_PTR(cctx);
	C2_ASSERT(cctx != NULL);

	C2_ALLOC_ARR(srv_addr, IO_ADDR_LEN);
	C2_ASSERT(srv_addr != NULL);
	bulkio_netep_form(saddr, port, IO_SERVER_SVC_ID, srv_addr);

	cctx->rcx_remote_addr = srv_addr;
	cctx->rcx_cob_dom_id  = IO_CLIENT_COBDOM_ID;
	cctx->rcx_nr_slots    = IO_RPC_SESSION_SLOTS;
	cctx->rcx_max_rpcs_in_flight = IO_RPC_MAX_IN_FLIGHT;
	cctx->rcx_timeout_s   = IO_RPC_CONN_TIMEOUT;

	C2_ALLOC_ARR(cli_addr, IO_ADDR_LEN);
	C2_ASSERT(cli_addr != NULL);
	bulkio_netep_form(caddr, port, IO_CLIENT_SVC_ID, cli_addr);
	cctx->rcx_local_addr = cli_addr;
	cctx->rcx_net_dom = &bp->bp_cnetdom;

	C2_ALLOC_ARR(cdbname, IO_STR_LEN);
	C2_ASSERT(cdbname != NULL);
	strcpy(cdbname, IO_CLIENT_DBNAME);
	cctx->rcx_db_name = cdbname;
	cctx->rcx_dbenv = &bp->bp_cdbenv;
	cctx->rcx_cob_dom = &bp->bp_ccbdom;

	rc = c2_rpc_client_init(cctx);
	C2_ASSERT(rc == 0);

	bp->bp_cctx = cctx;
	bp->bp_saddr = srv_addr;
	bp->bp_caddr = cli_addr;
	bp->bp_cdbname = cdbname;

	return rc;
}

void bulkio_client_stop(struct c2_rpc_client_ctx *cctx)
{
	int rc;

	C2_ASSERT(cctx != NULL);

	rc = c2_rpc_client_fini(cctx);
	C2_ASSERT(rc == 0);

	c2_free(cctx);
}
