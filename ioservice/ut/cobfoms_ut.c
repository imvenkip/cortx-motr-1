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
	COB_NAME_STRLEN           = 20,
	COB_FID_CONTAINER_ID      = 1234,
	COB_FID_KEY_ID            = 5678,
	GOB_FID_CONTAINER_ID      = 1000,
	GOB_FID_KEY_ID            = 5678,
};

#define SERVER_EP_ADDR    "127.0.0.1:12345:123"
#define CLIENT_EP_ADDR    "127.0.0.1:12345:124"
#define SERVER_ENDP       "bulk-sunrpc:"SERVER_EP_ADDR
#define SERVER_LOGFILE    "cobfops_ut.log"
#define CLIENT_DBNAME     "cobfoms_ut.db"

struct cobfoms_ut {
	struct c2_rpc_server_ctx cu_sctx;
	struct c2_rpc_client_ctx cu_cctx;
	struct c2_fop           *cu_cobfop;
	struct c2_fid            cu_gfid;
	struct c2_fid            cu_cfid;
};

static char *server_args[] = {
	"cobfops_ut", "-r", "-T", "AD", "-D", "cobfops_ut.db", "-S",
	"cobfops_ut_stob", "-e", SERVER_ENDP, "-s", "ioservice", 
};

static void cobfops_populate(struct cobfoms_ut *cut, struct c2_fop_type *ftype)
{
	struct c2_fop            *fop;
	struct c2_fop_cob_create *cc;
	struct c2_fop_cob_common *common;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(ftype != NULL);
	C2_UT_ASSERT(cut->cu_cobfop != NULL);

	fop = cut->cu_cobfop;
	common = c2_cobfop_common_get(fop);
	common->c_gobfid.f_seq = COB_FID_CONTAINER_ID;
	common->c_gobfid.f_oid = COB_FID_KEY_ID;
	common->c_cobfid.f_seq = GOB_FID_CONTAINER_ID;
	common->c_cobfid.f_oid = GOB_FID_KEY_ID;

	if (ftype == &c2_fop_cob_create_fopt) {
		cc = c2_fop_data(fop);
		C2_ALLOC_ARR(cc->cc_cobname.ib_buf, (2 * COB_NAME_STRLEN) + 1);
		C2_UT_ASSERT(cc->cc_cobname.ib_buf != NULL);
		sprintf((char*)cc->cc_cobname.ib_buf, "%20lu:%20lu",
			(unsigned long)cut->cu_cfid.f_container,
			(unsigned long)cut->cu_cfid.f_key);
		cc->cc_cobname.ib_count = strlen((char*)cc->cc_cobname.ib_buf);
	}
}

static void cobfops_create(struct cobfoms_ut *cut, struct c2_fop_type *ftype)
{
	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(ftype != NULL);
	C2_UT_ASSERT(cut->cu_cobfop == NULL);

	cut->cu_cobfop = c2_fop_alloc(ftype, NULL);
	C2_UT_ASSERT(cut->cu_cobfop != NULL);

	cobfops_populate(cut, ftype);
}

static void cobfops_send_wait(struct cobfoms_ut *cut,
			      struct c2_fop_type *ftype)
{
	int rc;
	struct c2_fop *fop;
	struct c2_fop_cob_op_reply *rfop;

	C2_UT_ASSERT(cut != NULL);
	C2_UT_ASSERT(ftype != NULL);

	fop = cut->cu_cobfop;
	rc = c2_rpc_client_call(fop, &cut->cu_cctx.rcx_session,
				&cob_req_rpc_item_ops,
				CLIENT_RPC_CONN_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	rfop = c2_fop_data(c2_rpc_item_to_fop(fop->f_item.ri_reply));
	C2_UT_ASSERT(rfop->cor_rc == 0);
}

static void cobfops_sendrecv(void)
{
	int                           rc;
	struct c2_dbenv               dbenv;
	struct cobfoms_ut            *cut;
	enum C2_RPC_OPCODES           op;
	struct c2_fop_type           *ftype;
	struct c2_cob_domain          cob_dom;
	struct c2_net_domain          nd;
	struct c2_net_xprt           *xprt = &c2_net_bulk_sunrpc_xprt;
	struct c2_rpc_server_ctx     *sctx;
	struct c2_rpc_client_ctx     *cctx;
	struct c2_reqh_service_type **stypes;

	rc = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(rc == 0);

	C2_SET0(&nd);
	rc = c2_net_domain_init(&nd, xprt);
	C2_UT_ASSERT(rc == 0);

	C2_ALLOC_PTR(cut);
	C2_UT_ASSERT(cut != NULL);
	sctx = &cut->cu_sctx;

	C2_ALLOC_ARR(stypes, 1);
	C2_UT_ASSERT(stypes != NULL);
	stypes[0] = &ds1_service_type;

	sctx->rsx_xprts            = &xprt;
	sctx->rsx_xprts_nr         = 1;
	sctx->rsx_argv             = server_args;
	sctx->rsx_argc             = ARRAY_SIZE(server_args);
	sctx->rsx_service_types    = stypes;
	sctx->rsx_service_types_nr = 1;
	sctx->rsx_log_file_name    = SERVER_LOGFILE;

	rc = c2_rpc_server_start(sctx);
	C2_UT_ASSERT(rc == 0);

	C2_SET0(&dbenv);
	C2_SET0(&cob_dom);
	cctx = &cut->cu_cctx;

	cctx->rcx_net_dom            = &nd;
	cctx->rcx_local_addr         = CLIENT_EP_ADDR;
	cctx->rcx_remote_addr        = SERVER_EP_ADDR;
	cctx->rcx_dbenv              = &dbenv;
	cctx->rcx_db_name            = CLIENT_DBNAME;
	cctx->rcx_cob_dom            = &cob_dom;
	cctx->rcx_cob_dom_id         = CLIENT_COB_DOM_ID;
	cctx->rcx_nr_slots           = CLIENT_RPC_SESSION_SLOTS;
	cctx->rcx_timeout_s          = CLIENT_RPC_CONN_TIMEOUT;
	cctx->rcx_max_rpcs_in_flight = CLIENT_MAX_RPCS_IN_FLIGHT;

	rc = c2_rpc_client_init(cctx);
	C2_UT_ASSERT(rc == 0);

	ftype = &c2_fop_cob_create_fopt;
	
	for (op = C2_IOSERVICE_COB_CREATE_OPCODE;
	     op <= C2_IOSERVICE_COB_DELETE_OPCODE; ++op) {
		C2_UT_ASSERT(op == C2_IOSERVICE_COB_CREATE_OPCODE ||
			     op == C2_IOSERVICE_COB_DELETE_OPCODE);

		cobfops_create(cut, ftype);

		/*
		 * Sends cob create/delete fops to colibri server and waits for
		 * reply.
		 */
		cobfops_send_wait(cut, ftype);

		/* Fops are deallocated internally by rpc code. */
		cut->cu_cobfop = NULL;
		ftype = &c2_fop_cob_delete_fopt;
	}

	rc = c2_rpc_client_fini(cctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_stop(sctx);
	
	c2_net_domain_fini(&nd);
	c2_net_xprt_fini(xprt);

	c2_free(stypes);
	c2_free(cut);
}

const struct c2_test_suite cobfoms_ut = {
	.ts_name  = "cob-foms-ut",
	.ts_init  = NULL,
	.ts_fini  = NULL,
	.ts_tests = {
		{ "fops_sendrecv",  cobfops_sendrecv},
		{ NULL, NULL }
	}
};
