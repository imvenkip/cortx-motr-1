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
 * Original creation date: 03/07/2012
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/bulk_sunrpc.h"
#include "ioservice/ut/bulkio_common.h"

extern struct c2_fop_type c2_fop_cob_create_fopt;
extern struct c2_fop_type c2_fop_cob_delete_fopt;
extern const struct c2_rpc_item_ops cob_req_rpc_item_ops;

enum {
	CLIENT_COB_DOM_ID         = 12,
	CLIENT_RPC_SESSION_SLOTS  = 1,
	CLIENT_RPC_CONN_TIMEOUT   = 200,
	CLIENT_MAX_RPCS_IN_FLIGHT = 8,
	COB_NAME_STRLEN           = 16,
	COB_FID_CONTAINER_ID      = 1234,
	COB_FID_KEY_ID            = 5678,
	GOB_FID_CONTAINER_ID      = 1000,
	GOB_FID_KEY_ID            = 5678,
	COB_FOP_SINGLE            = 1,
	COB_FOP_NR                = 10,
};

#define SERVER_EP_ADDR    "127.0.0.1:12345:123"
#define CLIENT_EP_ADDR    "127.0.0.1:12345:124"
#define SERVER_ENDP       "bulk-sunrpc:"SERVER_EP_ADDR
#define SERVER_LOGFILE    "cobfoms_ut.log"
#define CLIENT_DBNAME     "cobfops_ut.db"

struct cobfoms_ut {
	struct c2_rpc_server_ctx      cu_sctx;
	struct c2_rpc_client_ctx      cu_cctx;
	uint64_t                      cu_cobfop_nr;
	struct c2_fop               **cu_createfops;
	struct c2_fop               **cu_deletefops;
	struct c2_fid                 cu_gfid;
	struct c2_fid                 cu_cfid;
	struct c2_reqh_service_type **cu_stypes;
	struct c2_net_xprt           *cu_xprt;
	struct c2_net_domain          cu_nd;
	struct c2_dbenv               cu_dbenv;
	struct c2_cob_domain          cu_cob_dom;
	uint64_t                      cu_thread_nr;
	struct c2_thread            **cu_threads;
	uint64_t                      cu_gobindex;
};

struct cobthread_arg {
	struct c2_fop_type *ca_ftype;
	int                 ca_index;
};

/* Static instance of struct cobfoms_ut used by all test cases. */
static struct cobfoms_ut *cut;

static char *server_args[] = {
	"cobfoms_ut", "-r", "-T", "AD", "-D", "cobfoms_ut.db", "-S",
	"cobfoms_ut_stob", "-e", SERVER_ENDP, "-s", "ioservice", 
};

static void cobfoms_utinit(void)
{
	int                       rc;
	struct c2_rpc_server_ctx *sctx;
	struct c2_rpc_client_ctx *cctx;

	C2_ALLOC_PTR(cut);
	C2_UT_ASSERT(cut != NULL);

	cut->cu_xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_xprt_init(cut->cu_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&cut->cu_nd, cut->cu_xprt);
	C2_UT_ASSERT(rc == 0);

	C2_ALLOC_ARR(cut->cu_stypes, 1);
	C2_UT_ASSERT(cut->cu_stypes != NULL);
	cut->cu_stypes[0] = &ds1_service_type;

	sctx = &cut->cu_sctx;
	sctx->rsx_xprts            = &cut->cu_xprt;
	sctx->rsx_xprts_nr         = 1;
	sctx->rsx_argv             = server_args;
	sctx->rsx_argc             = ARRAY_SIZE(server_args);
	sctx->rsx_service_types    = cut->cu_stypes;
	sctx->rsx_service_types_nr = 1;
	sctx->rsx_log_file_name    = SERVER_LOGFILE;

	rc = c2_rpc_server_start(sctx);
	C2_UT_ASSERT(rc == 0);

	cctx = &cut->cu_cctx;
	cctx->rcx_net_dom            = &cut->cu_nd;
	cctx->rcx_local_addr         = CLIENT_EP_ADDR;
	cctx->rcx_remote_addr        = SERVER_EP_ADDR;
	cctx->rcx_dbenv              = &cut->cu_dbenv;
	cctx->rcx_db_name            = CLIENT_DBNAME;
	cctx->rcx_cob_dom            = &cut->cu_cob_dom;
	cctx->rcx_cob_dom_id         = CLIENT_COB_DOM_ID;
	cctx->rcx_nr_slots           = CLIENT_RPC_SESSION_SLOTS;
	cctx->rcx_timeout_s          = CLIENT_RPC_CONN_TIMEOUT;
	cctx->rcx_max_rpcs_in_flight = CLIENT_MAX_RPCS_IN_FLIGHT;

	rc = c2_rpc_client_init(cctx);
	C2_UT_ASSERT(rc == 0);

	cut->cu_gobindex = 0;
	//c2_addb_choose_default_level_console(AEL_NONE);
}

static void cobfoms_utfini(void)
{
	int rc;

	C2_UT_ASSERT(cut != NULL);

	rc = c2_rpc_client_fini(&cut->cu_cctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_stop(&cut->cu_sctx);
	
	c2_net_domain_fini(&cut->cu_nd);
	c2_net_xprt_fini(cut->cu_xprt);

	c2_free(cut->cu_stypes);
	c2_free(cut);
	cut = NULL;
}

static void cobfops_populate_internal(struct c2_fop *fop, uint64_t index)
{
	struct c2_fop_cob_common *common;

	C2_UT_ASSERT(fop != NULL);
	C2_UT_ASSERT(fop->f_type != NULL);

	common = c2_cobfop_common_get(fop);
	common->c_gobfid.f_seq = GOB_FID_CONTAINER_ID + index;
	common->c_gobfid.f_oid = GOB_FID_KEY_ID + index;
	common->c_cobfid.f_seq = COB_FID_CONTAINER_ID + index;
	common->c_cobfid.f_oid = COB_FID_KEY_ID + index;
}

static void cobfops_populate(uint64_t index)
{
	struct c2_fop            *fop;
	struct c2_fop_cob_create *cc;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(cut->cu_createfops != NULL);
	C2_UT_ASSERT(cut->cu_deletefops != NULL);

	fop = cut->cu_deletefops[index];
	cobfops_populate_internal(fop, cut->cu_gobindex);
	fop = cut->cu_createfops[index];
	cobfops_populate_internal(fop, cut->cu_gobindex);
	cut->cu_gobindex++;

	cc = c2_fop_data(fop);
	C2_ALLOC_ARR(cc->cc_cobname.ib_buf, (2 * COB_NAME_STRLEN) + 2);
	C2_UT_ASSERT(cc->cc_cobname.ib_buf != NULL);
	sprintf((char*)cc->cc_cobname.ib_buf, "%16lx:%16lx",
			(unsigned long)cc->cc_common.c_cobfid.f_seq,
			(unsigned long)cc->cc_common.c_cobfid.f_oid);
	cc->cc_cobname.ib_count = strlen((char*)cc->cc_cobname.ib_buf);
}

static void cobfops_create()
{
	uint64_t i;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(cut->cu_createfops == NULL);
	C2_UT_ASSERT(cut->cu_deletefops == NULL);

	C2_ALLOC_ARR(cut->cu_createfops, cut->cu_cobfop_nr);
	C2_UT_ASSERT(cut->cu_createfops != NULL);

	C2_ALLOC_ARR(cut->cu_deletefops, cut->cu_cobfop_nr);
	C2_UT_ASSERT(cut->cu_deletefops != NULL);

	for (i = 0; i < cut->cu_cobfop_nr; ++i) {
		cut->cu_createfops[i] = c2_fop_alloc(&c2_fop_cob_create_fopt,
						     NULL);
		C2_UT_ASSERT(cut->cu_createfops[i] != NULL);

		cut->cu_deletefops[i] = c2_fop_alloc(&c2_fop_cob_delete_fopt,
						     NULL);
		C2_UT_ASSERT(cut->cu_deletefops[i] != NULL);
		cobfops_populate(i);
	}
}

static void cobfops_destroy()
{
	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(cut->cu_createfops != NULL);
	C2_UT_ASSERT(cut->cu_deletefops != NULL);

	c2_free(cut->cu_createfops);
	c2_free(cut->cu_deletefops);
	cut->cu_createfops = NULL;
	cut->cu_deletefops = NULL;
}

static void cobfops_threads_init(void)
{
	int i;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(cut->cu_thread_nr > 0);

	C2_ALLOC_ARR(cut->cu_threads, cut->cu_thread_nr);
	C2_UT_ASSERT(cut->cu_threads != NULL);

	for (i = 0; i < cut->cu_thread_nr; ++i) {
		C2_ALLOC_PTR(cut->cu_threads[i]);
		C2_UT_ASSERT(cut->cu_threads[i] != NULL);
	}
}

static void cobfops_threads_fini(void)
{
	int i;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(cut->cu_threads != NULL);
	C2_UT_ASSERT(cut->cu_thread_nr > 0);

	for (i = 0; i < cut->cu_thread_nr; ++i)
		c2_free(cut->cu_threads[i]);
	c2_free(cut->cu_threads);
}

static void cobfops_send_wait(struct cobthread_arg *arg)
{
	int i;
	int rc;
	struct c2_fop *fop;
	struct c2_fop_cob_op_reply *rfop;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(arg != NULL);
	C2_UT_ASSERT(arg->ca_ftype != NULL);

	i = arg->ca_index;
	fop = arg->ca_ftype == &c2_fop_cob_create_fopt ? cut->cu_createfops[i] :
		cut->cu_deletefops[i];;

	rc = c2_rpc_client_call(fop, &cut->cu_cctx.rcx_session,
				&cob_req_rpc_item_ops,
				CLIENT_RPC_CONN_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	rfop = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
	C2_UT_ASSERT(rfop->cor_rc == 0);
}

static void cobfoms_single(void)
{
	int                  i;
	int                  rc;
	struct cobthread_arg arg;
	struct c2_fop_type  *ftype;

	C2_SET0(&arg);
	cut->cu_cobfop_nr = COB_FOP_SINGLE;
	cut->cu_thread_nr =  2 * COB_FOP_SINGLE;
	cobfops_threads_init();
	cobfops_create();
	ftype = &c2_fop_cob_create_fopt;
	
	for (i = 0; i <= COB_FOP_SINGLE; ++i) {
		arg.ca_ftype = ftype;
		arg.ca_index = COB_FOP_SINGLE - 1;

		rc = C2_THREAD_INIT(cut->cu_threads[i],
				    struct cobthread_arg *,
				    NULL, &cobfops_send_wait, &arg,
				    "cob_create");
		C2_UT_ASSERT(rc == 0);

		c2_thread_join(cut->cu_threads[i]);

		C2_SET0(cut->cu_threads[i]);
		ftype = &c2_fop_cob_delete_fopt;
	}

	/* Individual Fops are deallocated internally by rpc code. */
	cobfops_destroy();
	cobfops_threads_fini();
}

static void cobfoms_multiple_internal(int nr, struct c2_fop_type *ftype)
{
	int                   i;
	int                   rc;
	struct cobthread_arg *arg;

	C2_UT_ASSERT(nr > 0);
	C2_UT_ASSERT(ftype != NULL);
	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(cut->cu_threads != NULL);

	C2_ALLOC_ARR(arg, nr);
	C2_UT_ASSERT(arg != NULL);

	for (i = 0; i < nr; ++i) {
		arg[i].ca_ftype = ftype;
		arg[i].ca_index = i;
		C2_SET0(cut->cu_threads[i]);
		rc = C2_THREAD_INIT(cut->cu_threads[i], struct cobthread_arg *,
				    NULL, &cobfops_send_wait, &arg[i],
				    ftype == &c2_fop_cob_create_fopt ?
				    "cob_create" : "cob_delete");
		C2_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < nr; ++i)
		c2_thread_join(cut->cu_threads[i]);

	c2_free(arg);
}

static void cobfoms_multiple(void)
{
	cut->cu_cobfop_nr = COB_FOP_NR;
	cobfops_create();

	cut->cu_thread_nr = COB_FOP_NR;
	cobfops_threads_init();

	/*
	 * Sends multiple cob_create fops to same ioservice instance
	 * so as to stress the fom code with multiple simultaneous requests.
	 */
	cobfoms_multiple_internal(COB_FOP_NR, &c2_fop_cob_create_fopt);

	/* Change fop type and send cob_delete fops. */
	cobfoms_multiple_internal(COB_FOP_NR, &c2_fop_cob_delete_fopt);
	cobfops_threads_fini();
	cobfops_destroy();
}

const struct c2_test_suite cobfoms_ut = {
	.ts_name  = "cob-foms-ut",
	.ts_init  = NULL,
	.ts_fini  = NULL,
	.ts_tests = {
		{ "cobfoms_utinit",         cobfoms_utinit},
		{ "cobfoms_single_fop",     cobfoms_single},
		{ "cobfoms_multiple_fops",  cobfoms_multiple},
		{ "cobfoms_utfini",         cobfoms_utfini},
		{ NULL, NULL }
	}
};
