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
 * Original creation date: 02/21/2011
 */

#include "ioservice/st/bulkio_common.h"
#include "ioservice/io_fops.c" /* To access static APIs. */

#define S_ENDP_ADDR		  "127.0.0.1:23134:1"
#define S_ENDPOINT		  "bulk-sunrpc:"S_ENDP_ADDR
#define S_DBFILE		  "bulkio_st.db"
#define S_STOBFILE		  "bulkio_st_stob"

/* Input arguments for colibri server setup. */
/*
static char *server_args[]	= {"bulkio_st", "-r", "-T", "AD", "-D",
				   S_DBFILE, "-S", S_STOBFILE, "-e",
				   S_ENDPOINT, "-s", "ds1", "-s", "ds2"};
				   */

/* Array of services to be started by colibri server. */
/*
static struct c2_reqh_service_type *stypes[] = {
	&ds1_service_type,
	&ds2_service_type,
};
*/

/*
 * Colibri server rpc context. Can't use C2_RPC_SERVER_CTX_DECLARE_SIMPLE()
 * since it limits the scope of struct c2_rpc_server_ctx to the function
 * where it is declared.
 */
/*
static struct c2_rpc_server_ctx s_rctx = {
	.rsx_xprts_nr		= IO_XPRT_NR,
	.rsx_argv		= server_args,
	.rsx_argc		= ARRAY_SIZE(server_args),
	.rsx_service_types	= stypes,
	.rsx_service_types_nr	= ARRAY_SIZE(stypes),
};
*/

/* Global reference to bulkio_params structure. */
struct bulkio_params *bparm;

static int io_fop_dummy_fom_init(struct c2_fop *fop, struct c2_fom **m);
extern void io_fop_replied(struct c2_fop *fop, struct c2_fop *bkpfop);
extern int io_fop_coalesce(struct c2_fop *res_fop, uint64_t size);
extern void io_fop_desc_get(struct c2_fop *fop, struct c2_net_buf_desc **desc);
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
extern void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
		      c2_bcount_t seg_size);
extern bool is_write(const struct c2_fop *fop);
extern bool is_read(const struct c2_fop *fop);
C2_TL_DESCR_DECLARE(rpcbulk, extern);

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
	C2_ASSERT(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);

	C2_ALLOC_ARR(netbufs, rw->crw_desc.id_nr);
	C2_ASSERT(netbufs != NULL);

	C2_ALLOC_PTR(rbulk);
	C2_ASSERT(rbulk != NULL);
	c2_rpc_bulk_init(rbulk);
	C2_ASSERT(rw->crw_desc.id_nr != 0);

	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i)
		for (j = 0; j < rw->crw_ivecs.cis_ivecs[i].ci_nr; ++j)
			C2_ASSERT(rw->crw_ivecs.cis_ivecs[i].ci_iosegs[j].
			     ci_count == C2_0VEC_ALIGN);

	for (tc = 0, i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];

		C2_ALLOC_PTR(netbufs[i]);
		C2_ASSERT(netbufs[i] != NULL);

		vec_alloc(&netbufs[i]->nb_buffer, ivec->ci_nr,
			  ivec->ci_iosegs[0].ci_count);

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 netbufs[i], &rbuf);

		C2_ASSERT(rc == 0);
		C2_ASSERT(rbuf != NULL);

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
	C2_ASSERT(rc == 0);
	c2_chan_wait(&clink);

	/* Makes sure that list of buffers in c2_rpc_bulk is empty. */
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);

	rc = rbulk->rb_rc;
	C2_ASSERT(rc == 0);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	/* Checks if the write io bulk data is received as is. */
	for (i = 0; i < rw->crw_desc.id_nr && is_write(fom->fo_fop); ++i) {
		for (j = 0; j < netbufs[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(bparm->bp_writebuf,
				    netbufs[i]->nb_buffer.ov_buf[j],
				    netbufs[i]->nb_buffer.ov_vec.v_count[j]);
			C2_ASSERT(rc == 0);
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
	C2_ASSERT(fop != NULL);

	fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fop->f_item);
	C2_ASSERT(rc == 0);

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
	C2_ASSERT(fom != NULL);

	fom->fo_fop = fop;
	c2_fom_init(fom);
	fop->f_type->ft_fom_type.ft_ops = &bulkio_fom_type_ops;
	fom->fo_type = &bulkio_fom_type;
	fom->fo_ops = &bulkio_fom_ops;

	*m = fom;
	return 0;
}

int bulkio_server_start(struct bulkio_params *bp, const char *saddr, int sport)
{
	int			      i;
	int			      rc;
	char			    **server_args;
	char			     xprt[IO_ADDR_LEN] = "bulk-sunrpc:";
	char			     sep[IO_ADDR_LEN];
	struct c2_rpc_server_ctx     *sctx;
	struct c2_reqh_service_type **stypes;

	C2_ASSERT(saddr != NULL);

	C2_ALLOC_ARR(server_args, IO_SERVER_ARGC);
	C2_ASSERT(server_args != NULL);
	if (server_args == NULL)
		return -ENOMEM;

	for (i = 0; i < IO_SERVER_ARGC; ++i) {
		C2_ALLOC_ARR(server_args[i], IO_ADDR_LEN);
		C2_ASSERT(server_args[i] != NULL);
		if (server_args[i] == NULL) {
			rc = -ENOMEM;
			break;
		}
	}

	if (rc != 0) {
		for (i = 0; i < IO_SERVER_ARGC; ++i)
			c2_free(server_args[i]);
		c2_free(server_args);
		return -ENOMEM;
	}

	/* Copy all server arguments to server_args list. */
	strcpy(server_args[0], "bulkio_st");
	strcpy(server_args[1], "-r");
	strcpy(server_args[2], "-T");
	strcpy(server_args[3], "AD");
	strcpy(server_args[4], "-D");
	strcpy(server_args[5], S_DBFILE);
	strcpy(server_args[6], "-S");
	strcpy(server_args[7], S_STOBFILE);
	strcpy(server_args[8], "-e");
	strcat(server_args[9], xprt);
	memset(sep, 0, IO_ADDR_LEN);
	bulkio_netep_form(saddr, sport, IO_SERVER_SVC_ID, sep);
	strcat(server_args[9], sep);
	strcpy(server_args[10], "-s");
	strcpy(server_args[11], "ds1");
	strcpy(server_args[12], "-s");
	strcpy(server_args[13], "ds2");

	C2_ALLOC_ARR(stypes, IO_SERVER_SERVICE_NR);
	C2_ASSERT(stypes != NULL);

	stypes[0] = &ds1_service_type;
	stypes[1] = &ds2_service_type;

	C2_ALLOC_PTR(sctx);
	C2_ASSERT(sctx != NULL);

	sctx->rsx_xprts_nr = IO_XPRT_NR;
	sctx->rsx_argv = server_args;
	sctx->rsx_argc = IO_SERVER_ARGC;
	sctx->rsx_service_types = stypes;
	sctx->rsx_service_types_nr = IO_SERVER_SERVICE_NR;

	C2_ALLOC_ARR(bp->bp_slogfile, IO_STR_LEN);
	C2_ASSERT(bp->bp_slogfile != NULL);
	strcpy(bp->bp_slogfile, IO_SERVER_LOGFILE);
	sctx->rsx_log_file_name = bp->bp_slogfile;
	sctx->rsx_xprts = &bp->bp_xprt;

	rc = c2_rpc_server_start(sctx);
	C2_ASSERT(rc == 0);

	bp->bp_sctx = sctx;
	bparm = bp;
	return rc;
}

void bulkio_server_stop(struct c2_rpc_server_ctx *sctx)
{
	int i;

	C2_ASSERT(sctx != NULL);

	c2_rpc_server_stop(sctx);
	for (i = 0; i < IO_SERVER_ARGC; ++i)
		c2_free(sctx->rsx_argv[i]);

	c2_free(sctx->rsx_argv);
	c2_free(sctx->rsx_service_types);
	c2_free(sctx);
}
