/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 *                  Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 03/07/2012
 */

#include "ut/ut.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "net/lnet/lnet.h"
#include "rpc/rpclib.h"                  /* m0_rpc_server_ctx */
#include "fop/ut/fop_put_norpc.h"
#include "ioservice/ut/bulkio_common.h"
#include "ioservice/cob_foms.c"          /* To access static APIs. */
#include "ioservice/io_service.h"
#include "ioservice/io_fops_xc.h"
#include "ut/cs_service.h"               /* ds1_service_type */
#include "ut/ut.h"                       /* m0_ut_fom_phase_set */

extern struct m0_fop_type m0_fop_cob_create_fopt;
extern struct m0_fop_type m0_fop_cob_delete_fopt;
extern struct m0_reqh_service_type m0_ios_type;

/* Static instance of struct cobfoms_ut used by all test cases. */
static struct cobfoms_ut      *cut;
static struct m0_fom_locality  dummy_loc;

static struct m0_cob *test_cob = NULL;

static struct m0_fom *cd_fom_alloc();
static void cd_fom_dealloc(struct m0_fom *fom);

enum cob_fom_type {
	COB_CREATE = 1,
	COB_DELETE = 2
};

enum {
	CLIENT_COB_DOM_ID         = 12,
	CLIENT_RPC_SESSION_SLOTS  = 1,
	CLIENT_RPC_CONN_TIMEOUT   = 200,
	CLIENT_MAX_RPCS_IN_FLIGHT = 8,
	COB_NAME_STRLEN           = 34,
	COB_FID_CONTAINER_ID      = 1234,
	COB_FID_KEY_ID            = 5678,
	GOB_FID_CONTAINER_ID      = 1000,
	GOB_FID_KEY_ID            = 5678,
	COB_FOP_SINGLE            = 1,
	COB_FOP_NR                = 10,
	COB_TEST_ID               = 111,
};

#define SERVER_EP_ADDR              "0@lo:12345:34:123"
#define CLIENT_EP_ADDR              "0@lo:12345:34:*"
#define SERVER_ENDP                 "lnet:" SERVER_EP_ADDR
static const char *SERVER_LOGFILE = "cobfoms_ut.log";
static const char *CLIENT_DBNAME  = "cobfops_ut.db";

struct cobfoms_ut {
	struct m0_rpc_server_ctx      cu_sctx;
	struct m0_rpc_client_ctx      cu_cctx;
	uint64_t                      cu_cobfop_nr;
	struct m0_fop               **cu_createfops;
	struct m0_fop               **cu_deletefops;
	struct m0_fid                 cu_gfid;
	struct m0_fid                 cu_cfid;
	struct m0_reqh_service_type **cu_stypes;
	struct m0_net_xprt           *cu_xprt;
	struct m0_net_domain          cu_nd;
	struct m0_dbenv               cu_dbenv;
	struct m0_cob_domain          cu_cob_dom;
	uint64_t                      cu_thread_nr;
	struct m0_thread            **cu_threads;
	uint64_t                      cu_gobindex;
};

struct cobthread_arg {
	struct m0_fop_type *ca_ftype;
	int                 ca_index;
	int                 ca_rc;
};

static char *server_args[] = {
	"m0d", "-r", "-p", "-T", "Linux", "-D", "cobfoms_ut.db", "-S",
	"cobfoms_ut_stob", "-A", "cobfoms_ut_addb_stob", "-e", SERVER_ENDP,
	"-s", "ioservice", "-w", "10"
};

static void cobfoms_utinit(void)
{
	int                       rc;
	struct m0_rpc_server_ctx *sctx;
	struct m0_rpc_client_ctx *cctx;

	M0_ALLOC_PTR(cut);
	M0_UT_ASSERT(cut != NULL);

	cut->cu_xprt = &m0_net_lnet_xprt;
	rc = m0_net_xprt_init(cut->cu_xprt);
	M0_UT_ASSERT(rc == 0);

	rc = m0_net_domain_init(&cut->cu_nd, cut->cu_xprt, &m0_addb_proc_ctx);
	M0_UT_ASSERT(rc == 0);

	M0_ALLOC_ARR(cut->cu_stypes, 1);
	M0_UT_ASSERT(cut->cu_stypes != NULL);
	cut->cu_stypes[0] = &ds1_service_type;

	sctx = &cut->cu_sctx;
	sctx->rsx_xprts            = &cut->cu_xprt;
	sctx->rsx_xprts_nr         = 1;
	sctx->rsx_argv             = server_args;
	sctx->rsx_argc             = ARRAY_SIZE(server_args);
	sctx->rsx_service_types    = cut->cu_stypes;
	sctx->rsx_service_types_nr = 1;
	sctx->rsx_log_file_name    = SERVER_LOGFILE;

	rc = m0_rpc_server_start(sctx);
	M0_UT_ASSERT(rc == 0);

	cctx = &cut->cu_cctx;
	cctx->rcx_net_dom            = &cut->cu_nd;
	cctx->rcx_local_addr         = CLIENT_EP_ADDR;
	cctx->rcx_remote_addr        = SERVER_EP_ADDR;
	cctx->rcx_dbenv              = &cut->cu_dbenv;
	cctx->rcx_db_name            = CLIENT_DBNAME;
	cctx->rcx_cob_dom            = &cut->cu_cob_dom;
	cctx->rcx_cob_dom_id         = CLIENT_COB_DOM_ID;
	cctx->rcx_nr_slots           = CLIENT_RPC_SESSION_SLOTS;
	cctx->rcx_max_rpcs_in_flight = CLIENT_MAX_RPCS_IN_FLIGHT;

	rc = m0_rpc_client_start(cctx);
	M0_UT_ASSERT(rc == 0);

	cut->cu_gobindex = 0;
}

static void cobfoms_utfini(void)
{
	int rc;

	M0_UT_ASSERT(cut != NULL);

	rc = m0_rpc_client_stop(&cut->cu_cctx);
	M0_UT_ASSERT(rc == 0);

	m0_rpc_server_stop(&cut->cu_sctx);

	m0_net_domain_fini(&cut->cu_nd);
	m0_net_xprt_fini(cut->cu_xprt);

	m0_free(cut->cu_stypes);
	m0_free(cut);
	cut = NULL;
}

static void cobfops_populate_internal(struct m0_fop *fop, uint64_t index)
{
	struct m0_fop_cob_common *common;

	M0_UT_ASSERT(fop != NULL);
	M0_UT_ASSERT(fop->f_type != NULL);

	common = m0_cobfop_common_get(fop);
	m0_fid_set(&common->c_gobfid, GOB_FID_CONTAINER_ID + index,
		   GOB_FID_KEY_ID + index);
	m0_fid_set(&common->c_cobfid, GOB_FID_CONTAINER_ID + index,
		   GOB_FID_KEY_ID + index);
}

static void cobfops_populate(uint64_t index)
{
	struct m0_fop            *fop;

	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(cut->cu_createfops != NULL);
	M0_UT_ASSERT(cut->cu_deletefops != NULL);

	fop = cut->cu_deletefops[index];
	cobfops_populate_internal(fop, cut->cu_gobindex);
	fop = cut->cu_createfops[index];
	cobfops_populate_internal(fop, cut->cu_gobindex);
	cut->cu_gobindex++;
}

static void cobfops_create(void)
{
	uint64_t i;

	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(cut->cu_createfops == NULL);
	M0_UT_ASSERT(cut->cu_deletefops == NULL);

	M0_ALLOC_ARR(cut->cu_createfops, cut->cu_cobfop_nr);
	M0_UT_ASSERT(cut->cu_createfops != NULL);

	M0_ALLOC_ARR(cut->cu_deletefops, cut->cu_cobfop_nr);
	M0_UT_ASSERT(cut->cu_deletefops != NULL);

	for (i = 0; i < cut->cu_cobfop_nr; ++i) {
		cut->cu_createfops[i] = m0_fop_alloc(&m0_fop_cob_create_fopt,
						     NULL);
		M0_UT_ASSERT(cut->cu_createfops[i] != NULL);

		cut->cu_deletefops[i] = m0_fop_alloc(&m0_fop_cob_delete_fopt,
						     NULL);
		M0_UT_ASSERT(cut->cu_deletefops[i] != NULL);
		cobfops_populate(i);
	}
}

static void cobfops_destroy(struct m0_fop_type *ftype1,
			    struct m0_fop_type *ftype2)
{
	uint64_t i;

	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(cut->cu_createfops != NULL);
	M0_UT_ASSERT(cut->cu_deletefops != NULL);
	M0_UT_ASSERT(ftype1 == NULL || ftype1 == &m0_fop_cob_create_fopt);
	M0_UT_ASSERT(ftype2 == NULL || ftype2 == &m0_fop_cob_delete_fopt);

	if (ftype1 != NULL)
		for (i = 0; i < cut->cu_cobfop_nr; ++i)
			m0_fop_put(cut->cu_createfops[i]);

	if (ftype2 != NULL)
		for (i = 0; i < cut->cu_cobfop_nr; ++i)
			m0_fop_put(cut->cu_deletefops[i]);

	m0_free(cut->cu_createfops);
	m0_free(cut->cu_deletefops);
	cut->cu_createfops = NULL;
	cut->cu_deletefops = NULL;
}

static void cobfops_threads_init(void)
{
	int i;

	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(cut->cu_thread_nr > 0);

	M0_ALLOC_ARR(cut->cu_threads, cut->cu_thread_nr);
	M0_UT_ASSERT(cut->cu_threads != NULL);

	for (i = 0; i < cut->cu_thread_nr; ++i) {
		M0_ALLOC_PTR(cut->cu_threads[i]);
		M0_UT_ASSERT(cut->cu_threads[i] != NULL);
	}
}

static void cobfops_threads_fini(void)
{
	int i;

	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(cut->cu_threads != NULL);
	M0_UT_ASSERT(cut->cu_thread_nr > 0);

	for (i = 0; i < cut->cu_thread_nr; ++i)
		m0_free(cut->cu_threads[i]);
	m0_free(cut->cu_threads);
}

static void cobfops_send_wait(struct cobthread_arg *arg)
{
	int i;
	int rc;
	struct m0_fop *fop;
	struct m0_fop_cob_op_reply *rfop;

	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(arg != NULL);
	M0_UT_ASSERT(arg->ca_ftype != NULL);

	i = arg->ca_index;
	fop = arg->ca_ftype == &m0_fop_cob_create_fopt ? cut->cu_createfops[i] :
		cut->cu_deletefops[i];

	rc = m0_rpc_client_call(fop, &cut->cu_cctx.rcx_session,
				NULL, 0 /* deadline */);
	M0_UT_ASSERT(rc == 0);
	rfop = m0_fop_data(m0_rpc_item_to_fop(fop->f_item.ri_reply));
	M0_UT_ASSERT(rfop->cor_rc == arg->ca_rc);
}

static void cobfoms_fops_dispatch(struct m0_fop_type *ftype, int expected_rc)
{
	int                   rc;
	uint64_t              i;
	struct cobthread_arg *arg;

	M0_UT_ASSERT(ftype != NULL);
	M0_UT_ASSERT(cut != NULL);
	M0_UT_ASSERT(cut->cu_createfops != NULL);
	M0_UT_ASSERT(cut->cu_cobfop_nr > 0);
	M0_UT_ASSERT(cut->cu_deletefops != NULL);
	M0_UT_ASSERT(cut->cu_thread_nr > 0);

	M0_ALLOC_ARR(arg, cut->cu_cobfop_nr);
	M0_UT_ASSERT(arg != NULL);

	for (i = 0; i < cut->cu_cobfop_nr; ++i) {
		arg[i].ca_ftype = ftype;
		arg[i].ca_index = i;
		arg[i].ca_rc = expected_rc;
		M0_SET0(cut->cu_threads[i]);
		rc = M0_THREAD_INIT(cut->cu_threads[i], struct cobthread_arg *,
				    NULL, &cobfops_send_wait, &arg[i],
				    ftype == &m0_fop_cob_create_fopt ?
				    "cob_create" : "cob_delete");
		M0_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < cut->cu_cobfop_nr; ++i)
		m0_thread_join(cut->cu_threads[i]);

	m0_free(arg);
}

static void cobfoms_fop_thread_init(uint64_t fop_nr, uint64_t thread_nr)
{
	M0_UT_ASSERT(fop_nr > 0 && thread_nr > 0);
	M0_UT_ASSERT(cut != NULL);

	cut->cu_cobfop_nr = fop_nr;
	cobfops_create();
	cut->cu_thread_nr = thread_nr;
	cobfops_threads_init();
}

static void cobfoms_fop_thread_fini(struct m0_fop_type *ftype1,
				    struct m0_fop_type *ftype2)
{
	cobfops_destroy(ftype1, ftype2);
	cobfops_threads_fini();
}

static void cobfoms_send_internal(struct m0_fop_type *ftype1,
				  struct m0_fop_type *ftype2,
				  int rc1, int rc2,
				  uint64_t nr)
{
	cobfoms_fop_thread_init(nr, nr);

	if (ftype1 != NULL)
		cobfoms_fops_dispatch(ftype1, rc1);
	if (ftype2 != NULL)
		cobfoms_fops_dispatch(ftype2, rc2);

	cobfoms_fop_thread_fini(ftype1, ftype2);
}

static void cobfoms_single(void)
{
	cobfoms_send_internal(&m0_fop_cob_create_fopt, &m0_fop_cob_delete_fopt,
			      0, 0, COB_FOP_SINGLE);
}

/*
 * Sends multiple cob_create fops to same ioservice instance
 * so as to stress the fom code with multiple simultaneous requests.
 */
static void cobfoms_multiple(void)
{
	cobfoms_send_internal(&m0_fop_cob_create_fopt, &m0_fop_cob_delete_fopt,
			      0, 0, COB_FOP_NR);
}

static void cobfoms_preexisting_cob(void)
{
	cobfoms_send_internal(&m0_fop_cob_create_fopt, NULL, 0, 0,
			      COB_FOP_SINGLE);

	/*
	 * Hack the value of cobfoms_ut::cu_gobindex to send cob_create
	 * fop and subsequence cob_delete fop with same fid.
	 */
	--cut->cu_gobindex;
	cobfoms_send_internal(&m0_fop_cob_create_fopt, NULL, -EEXIST, 0,
			      COB_FOP_SINGLE);

	--cut->cu_gobindex;

	/* Cleanup. */
	cobfoms_send_internal(NULL, &m0_fop_cob_delete_fopt, 0, 0,
			      COB_FOP_SINGLE);
	cut->cu_gobindex++;
	cut->cu_gobindex++;
}

static void cobfoms_del_nonexist_cob(void)
{
	cobfoms_send_internal(NULL, &m0_fop_cob_delete_fopt, 0, -ENOENT,
			      COB_FOP_SINGLE);
}

/*
 * Create COB FOMs - create or delete
 */
static void fom_create(struct m0_fom **fom, enum cob_fom_type fomtype)
{
	struct m0_fom          *base_fom;
	struct m0_reqh         *reqh;
	struct m0_fom_type      ft;
	int		        rc;

	rc = cob_op_fom_create(fom);
	M0_UT_ASSERT(rc == 0);

	base_fom = *fom;
	m0_fom_type_init(&ft, NULL, &m0_ios_type, &m0_generic_conf);

	reqh = m0_cs_reqh_get(&cut->cu_sctx.rsx_mero_ctx, "ioservice");
	M0_UT_ASSERT(reqh != NULL);
	m0_fom_init(base_fom, &ft,
		    fomtype == COB_CREATE ? &cc_fom_ops : &cd_fom_ops,
		    NULL, NULL, reqh, &m0_ios_type);

	base_fom->fo_service = m0_reqh_service_find(ft.ft_rstype, reqh);
	M0_UT_ASSERT(base_fom->fo_service != NULL);

	base_fom->fo_loc = &dummy_loc;

	M0_CNT_INC(base_fom->fo_loc->fl_foms);
	base_fom->fo_type = &ft;

	m0_fom_sm_init(base_fom);
	m0_fol_rec_init(&base_fom->fo_tx.tx_fol_rec);
}

/*
 * Delete COB FOMs - create or delete
 */
static void fom_fini(struct m0_fom *fom, enum cob_fom_type fomtype)
{
	m0_ut_fom_phase_set(fom, M0_FOPH_FINISH);

	switch (fomtype) {
	case COB_CREATE:
		cc_fom_fini(fom);
		break;
	case COB_DELETE:
		cd_fom_fini(fom);
		break;
	default:
		M0_IMPOSSIBLE("Invalid COB-FOM type");
	}
	m0_fol_rec_fini(&fom->fo_tx.tx_fol_rec);
}

/*
 * Allocate desired FOP and populate test-data in it.
 */
static void fop_alloc(struct m0_fom *fom, enum cob_fom_type fomtype)
{
	struct m0_fop_cob_common *c;
	struct m0_fop		 *base_fop;

	switch (fomtype) {
	case COB_CREATE:
		base_fop = m0_fop_alloc(&m0_fop_cob_create_fopt, NULL);
		M0_UT_ASSERT(base_fop != NULL);
		break;
	case COB_DELETE:
		base_fop = m0_fop_alloc(&m0_fop_cob_delete_fopt, NULL);
		M0_UT_ASSERT(base_fop != NULL);
		break;
	default:
		M0_IMPOSSIBLE("Invalid COB-FOM type");
		base_fop = NULL;
		break;
	}
	c = m0_cobfop_common_get(base_fop);
	m0_fid_set(&c->c_gobfid, COB_TEST_ID, COB_TEST_ID);
	m0_fid_set(&c->c_cobfid, COB_TEST_ID, COB_TEST_ID);
	c->c_cob_idx = COB_TEST_ID;
	fom->fo_fop = base_fop;
	fom->fo_type = &base_fop->f_type->ft_fom_type;

	fom->fo_rep_fop = m0_fop_alloc(&m0_fop_cob_op_reply_fopt, NULL);
	M0_UT_ASSERT(fom->fo_rep_fop != NULL);
}

/*
 * A generic COB-FOM-delete verification function. Check memory usage.
 */
static void fom_fini_test(enum cob_fom_type fomtype)
{
	size_t	       tot_mem;
	size_t	       base_mem;
	struct m0_fom *fom;

	/*
	 * 1. Allocate FOM object of interest
	 * 2. Calculate memory usage before and after object allocation
	 *    and de-allocation.
	 */
	base_mem = m0_allocated();
	fom_create(&fom, fomtype);

	/*
	 * Ensure - after fom_fini() memory usage drops back to original value
	 */
	fom_fini(fom, fomtype);
	tot_mem = m0_allocated();
	M0_UT_ASSERT(tot_mem == base_mem);
}

/*
 * A generic COB-FOM test function that validates the sub-class FOM object.
 */
static void fom_get_test(enum cob_fom_type fomtype)
{
	struct m0_fom        *fom;
	struct m0_fom_cob_op *cc;

	fom_create(&fom, fomtype);
	M0_UT_ASSERT(fom != NULL);

	cc = cob_fom_get(fom);
	M0_UT_ASSERT(cc != NULL);
	M0_UT_ASSERT(&cc->fco_fom == fom);
	fom_fini(fom, fomtype);
}

/*
 * A generic test to verify COM-FOM create functions.
 */
static void fom_create_test(enum cob_fom_type fomtype)
{
	struct m0_fom *fom;

	fom_create(&fom, fomtype);
	M0_UT_ASSERT(fom != NULL);
	fom_fini(fom, fomtype);
}

/*
 * Delete COB-create FOM.
 */
static void cc_fom_dealloc(struct m0_fom *fom)
{
	m0_ut_fom_phase_set(fom, M0_FOPH_FINISH);
	fom_fop_put_norpc(fom);
	cc_fom_fini(fom);
}

/*
 * Create COB-create FOM and populate it with testdata.
 */
static struct m0_fom *cc_fom_alloc()
{
	struct m0_fom *fom = NULL;

	fom_create(&fom, COB_CREATE);
	M0_UT_ASSERT(fom != NULL);

	fop_alloc(fom, COB_CREATE);
	M0_UT_ASSERT(fom->fo_fop != NULL);
	cob_fom_populate(fom);
	m0_fom_phase_set(fom, M0_FOPH_CC_COB_CREATE);
	return fom;
}

/*
 * Test function for cc_fom_create().
 */
static void cc_fom_create_test()
{
	fom_create_test(COB_CREATE);
}

/*
 * Test function for cc_fom_fini().
 */
static void cc_fom_fini_test()
{
	fom_fini_test(COB_CREATE);
}

/*
 * Test function for cc_fom_get().
 */
static void cc_fom_get_test()
{
	fom_get_test(COB_CREATE);
}

/*
 * Test function for cc_stob_create().
 */
static void cc_stob_create_test()
{
	int                   rc;
	struct m0_fom        *fom;
	struct m0_fom_cob_op *cc;

	fom = cc_fom_alloc();
	M0_UT_ASSERT(fom != NULL);

	cc = cob_fom_get(fom);

	rc = cc_stob_create(fom, cc);
	M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_CC_COB_CREATE);

	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(cc->fco_stobid.si_bits.u_hi == COB_TEST_ID);
	M0_UT_ASSERT(cc->fco_stobid.si_bits.u_lo == COB_TEST_ID);

	cc_fom_dealloc(fom);
}

/*
 * Test function to check COB record in the database.
 */
static void cob_verify(struct m0_fom *fom, const bool exists)
{
	int		      rc;
	struct m0_db_tx	      tx;
	struct m0_cob_domain *cobdom;
	struct m0_cob_nskey  *nskey;
	struct m0_dbenv	     *dbenv;
	struct m0_fid         fid = {COB_TEST_ID, COB_TEST_ID};
        char                  nskey_bs[UINT32_MAX_STR_LEN];
        uint32_t              nskey_bs_len;
	uint32_t              cob_idx = COB_TEST_ID;

	rc = m0_ios_cdom_get(m0_fom_reqh(fom), &cobdom);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(cobdom != NULL);

	dbenv = m0_fom_reqh(fom)->rh_dbenv;

        snprintf((char*)nskey_bs, UINT32_MAX_STR_LEN, "%u",
                 (uint32_t)cob_idx);
        nskey_bs_len = strlen(nskey_bs);

	rc = m0_cob_nskey_make(&nskey, &fid, (char *)nskey_bs, nskey_bs_len);
	M0_UT_ASSERT(rc == 0);

	M0_SET0(&tx);
	rc = m0_db_tx_init(&tx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_cob_lookup(cobdom, nskey, 0, &test_cob, &tx);
	m0_db_tx_commit(&tx);

	if (exists) {
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(test_cob != NULL);
		M0_UT_ASSERT(test_cob->co_flags & M0_CA_NSREC);
	} else
		M0_UT_ASSERT(rc == -ENOENT);
        if (rc != 0)
	        m0_free(nskey);
}

/*
 * Test function for cc_cob_create().
 */
static void cc_cob_create_test()
{
	int                   rc;
	struct m0_fom        *fom;
	struct m0_dbenv      *dbenv;
	struct m0_fom_cob_op *cc;

	fom = cc_fom_alloc();
	M0_UT_ASSERT(fom != NULL);
	cc = cob_fom_get(fom);

	/*
	 * Create STOB first.
	 */
	rc = cc_stob_create(fom, cc);
	M0_UT_ASSERT(rc == 0);

	/*
	 * Set the FOM phase and set transaction context
	 * Test-case 1: Test successful creation of COB
	 */
	dbenv = m0_fom_reqh(fom)->rh_dbenv;
	rc = m0_db_tx_init(&fom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = cc_cob_create(fom, cc);
	m0_db_tx_commit(&fom->fo_tx.tx_dbtx);

	M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_CC_COB_CREATE);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(cc->fco_stobid.si_bits.u_hi == COB_TEST_ID);
	M0_UT_ASSERT(cc->fco_stobid.si_bits.u_lo == COB_TEST_ID);

	/*
	 * Test-case 1 - Verify COB creation
	 */
	rc = m0_db_tx_init(&fom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	cob_verify(fom, true);
	m0_db_tx_commit(&fom->fo_tx.tx_dbtx);

	/*
	 * Test-case 2 - Test failure case. Try to create the
	 * same COB.
	 */
	rc = m0_db_tx_init(&fom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = cc_cob_create(fom, cc);
	M0_UT_ASSERT(rc != 0);
	m0_db_tx_commit(&fom->fo_tx.tx_dbtx);

	/*
	 * Start cleanup by deleting the COB
	 */
	rc = m0_db_tx_init(&fom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_cob_delete_put(test_cob, &fom->fo_tx.tx_dbtx);
	m0_db_tx_commit(&fom->fo_tx.tx_dbtx);
	M0_UT_ASSERT(rc == 0);
	test_cob = NULL;

	cc_fom_dealloc(fom);
}

/*
 * Test function for cc_fom_tick().
 */
static void cc_fom_state_test(void)
{
	int                   rc;
	struct m0_fom        *cfom;
	struct m0_fom        *dfom;
	struct m0_dbenv      *dbenv;
	struct m0_fom_cob_op *cc;

	cfom = cc_fom_alloc();
	M0_UT_ASSERT(cfom != NULL);

	dbenv = m0_fom_reqh(cfom)->rh_dbenv;
	rc = m0_db_tx_init(&cfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = cc_fom_tick(cfom);
	m0_db_tx_commit(&cfom->fo_tx.tx_dbtx);

	M0_UT_ASSERT(rc == M0_FSO_AGAIN);
	M0_UT_ASSERT(m0_fom_phase(cfom) == M0_FOPH_SUCCESS);

	cc = cob_fom_get(cfom);
	M0_UT_ASSERT(cc->fco_stobid.si_bits.u_hi == COB_TEST_ID);
	M0_UT_ASSERT(cc->fco_stobid.si_bits.u_lo == COB_TEST_ID);

	rc = m0_db_tx_init(&cfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	cob_verify(cfom, true);
	m0_db_tx_commit(&cfom->fo_tx.tx_dbtx);

	/*
	 * Now create delete fom. Use FOM functions to delete cob-data.
	 */
	dfom = cd_fom_alloc();
	M0_UT_ASSERT(dfom != NULL);

	rc = m0_db_tx_init(&dfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);

	rc = cd_fom_tick(dfom);
	M0_UT_ASSERT(rc == M0_FSO_AGAIN);
	M0_UT_ASSERT(m0_fom_phase(dfom) == M0_FOPH_SUCCESS);

	m0_db_tx_commit(&dfom->fo_tx.tx_dbtx);

	cc_fom_dealloc(cfom);
	cd_fom_dealloc(dfom);
}

/*
 * Test function for cc_fom_populate().
 */
static void cc_fom_populate_test()
{
	struct m0_fom        *fom;
	struct m0_fom_cob_op *cc;

	fom = cc_fom_alloc();
	M0_UT_ASSERT(fom != NULL);

	cc = cob_fom_get(fom);
	M0_UT_ASSERT(cc->fco_cfid.f_container == COB_TEST_ID);
	M0_UT_ASSERT(cc->fco_cfid.f_key == COB_TEST_ID);
	cc_fom_dealloc(fom);
}

/*
 *****************
 * COB delete-FOM test functions
 ******************
 */

/*
 * Delete COB-delete FOM object.
 */
static void cd_fom_dealloc(struct m0_fom *fom)
{
	m0_ut_fom_phase_set(fom, M0_FOPH_FINISH);
	fom_fop_put_norpc(fom);
	cd_fom_fini(fom);
}

/*
 * Create COB-delete FOM and populate it with testdata.
 */
static struct m0_fom *cd_fom_alloc()
{
	struct m0_fom *fom = NULL;

	fom_create(&fom, COB_DELETE);
	M0_UT_ASSERT(fom != NULL);

	fop_alloc(fom, COB_DELETE);
	M0_UT_ASSERT(fom->fo_fop != NULL);
	cob_fom_populate(fom);
	m0_fom_phase_set(fom, M0_FOPH_CD_COB_DEL);

	return fom;
}

/*
 * Test function for cd_fom_create().
 */
static void cd_fom_create_test()
{
	fom_create_test(COB_DELETE);
}

/*
 * Test function for cd_fom_fini().
 */
static void cd_fom_fini_test()
{
	fom_fini_test(COB_DELETE);
}

/*
 * Test function for cd_fom_get().
 */
static void cd_fom_get_test()
{
	fom_get_test(COB_DELETE);
}

/*
 * Test function for cd_fom_populate().
 */
static void cd_fom_populate_test()
{
	struct m0_fom        *fom;
	struct m0_fom_cob_op *cd;

	fom = cd_fom_alloc();
	M0_UT_ASSERT(fom != NULL);

	cd = cob_fom_get(fom);
	M0_UT_ASSERT(cd->fco_cfid.f_container == COB_TEST_ID);
	M0_UT_ASSERT(cd->fco_cfid.f_key == COB_TEST_ID);
	M0_UT_ASSERT(cd->fco_stobid.si_bits.u_hi == COB_TEST_ID);
	M0_UT_ASSERT(cd->fco_stobid.si_bits.u_lo == COB_TEST_ID);
	cd_fom_dealloc(fom);
}

/*
 * Before testing COB-delete FOM functions, create COB testdata.
 */
static struct m0_fom *cob_testdata_create()
{
	struct m0_fom   *fom;
	struct m0_dbenv *dbenv;
	int	         rc;

	/*
	 * Create cob-create FOM.
	 * Crate COB and related meta-data.
	 */
	fom = cc_fom_alloc();
	M0_UT_ASSERT(fom != NULL);

	dbenv = m0_fom_reqh(fom)->rh_dbenv;
	rc = m0_db_tx_init(&fom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);

	rc = cc_fom_tick(fom);
	m0_db_tx_commit(&fom->fo_tx.tx_dbtx);

	M0_UT_ASSERT(rc == M0_FSO_AGAIN);
	M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_SUCCESS);

	return fom;
}

/*
 * Delete COB testdata. In this case we delete COB-create FOM.
 */
static void cob_testdata_cleanup(struct m0_fom *fom)
{
	cc_fom_dealloc(fom);
}

/*
 * Test function for cd_stob_delete()
 */
static void cd_stob_delete_test()
{
	struct m0_fom_cob_op     *cd;
	struct m0_fom_cob_op     *cc;
	struct m0_fom		 *cfom;
	struct m0_fom		 *dfom;
	int			  rc;

	cfom = cc_fom_alloc();
	M0_UT_ASSERT(cfom != NULL);
	cc = cob_fom_get(cfom);
	rc = cc_stob_create(cfom, cc);
	M0_UT_ASSERT(rc == 0);

	/* Test stob delete after it has been created */
	dfom = cd_fom_alloc();
	M0_UT_ASSERT(dfom != NULL);

	cd = cob_fom_get(dfom);
	rc = cd_stob_delete(dfom, cd);
	M0_UT_ASSERT(m0_fom_phase(dfom) == M0_FOPH_CD_COB_DEL);
	M0_ASSERT(rc == 0);

	cd_fom_dealloc(dfom);
	cc_fom_dealloc(cfom);
}

/*
 * Test function for cd_cob_delete()
 */
static void cd_cob_delete_test()
{
	int                   rc;
	struct m0_fom        *cfom;
	struct m0_fom        *dfom;
	struct m0_dbenv      *dbenv;
	struct m0_fom_cob_op *cd;

	cfom = cob_testdata_create();

	/* Test COB delete after COB has been created */
	dfom = cd_fom_alloc();
	M0_UT_ASSERT(dfom != NULL);

	cd = cob_fom_get(dfom);
	dbenv = m0_fom_reqh(dfom)->rh_dbenv;
	/*
	 * Test-case 1: Delete cob. The test should succeed.
	 */
	rc = m0_db_tx_init(&dfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);

	rc = cd_cob_delete(dfom, cd);
	m0_db_tx_commit(&dfom->fo_tx.tx_dbtx);

	M0_UT_ASSERT(m0_fom_phase(dfom) == M0_FOPH_CD_COB_DEL);
	M0_UT_ASSERT(rc == 0);

	/*
	 * Make sure that there no entry in the database.
	 */
	rc = m0_db_tx_init(&dfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	cob_verify(cfom, false);
	m0_db_tx_commit(&dfom->fo_tx.tx_dbtx);

	/*
	 * Test-case 2: Delete cob again. The test should fail.
	 */
	rc = m0_db_tx_init(&dfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);

	rc = cd_cob_delete(dfom, cd);
	m0_db_tx_commit(&dfom->fo_tx.tx_dbtx);
	M0_UT_ASSERT(rc != 0);

	/*
	 * Now do the cleanup.
	 */
	rc = cd_stob_delete(dfom, cd);
	M0_UT_ASSERT(rc == 0);

	cd_fom_dealloc(dfom);
	cob_testdata_cleanup(cfom);
}

/*
 * Test function for cd_fom_tick()
 */
static void cd_fom_state_test(void)
{
	struct m0_fom		 *cfom;
	struct m0_fom		 *dfom;
	struct m0_dbenv		 *dbenv;
	int			  rc;

	cfom = cob_testdata_create();

	/* Test if COB-map got deleted */
	dfom = cd_fom_alloc();
	M0_UT_ASSERT(dfom != NULL);

	dbenv = m0_fom_reqh(cfom)->rh_dbenv;
	rc = m0_db_tx_init(&dfom->fo_tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);

	rc = cd_fom_tick(dfom);
	m0_db_tx_commit(&dfom->fo_tx.tx_dbtx);

	M0_UT_ASSERT(m0_fom_phase(dfom) == M0_FOPH_SUCCESS);
	M0_UT_ASSERT(rc == M0_FSO_AGAIN);

	cd_fom_dealloc(dfom);
	cob_testdata_cleanup(cfom);
}

static void dummy_locality_setup()
{
	struct m0_reqh *reqh;

	reqh = m0_cs_reqh_get(&cut->cu_sctx.rsx_mero_ctx, "ioservice");
	M0_UT_ASSERT(reqh != NULL);

	dummy_loc.fl_dom = &reqh->rh_fom_dom;
	m0_sm_group_init(&dummy_loc.fl_group);
}

static void cob_create_api_test(void)
{
	/* Dummy locality setup */
	dummy_locality_setup();

	m0_sm_group_lock(&dummy_loc.fl_group);

	/* Test for cc_fom_create() */
	cc_fom_create_test();

	/* Test for cc_fom_fini() */
	cc_fom_fini_test();

	/* Test for cc_fom_get() */
	cc_fom_get_test();

	/* Test cc_fom_populate() */
	cc_fom_populate_test();

	/* Test cc_stob_create() */
	cc_stob_create_test();

	/* Test cc_cob_create() */
	cc_cob_create_test();

	/* Test for cc_fom_tick() */
	cc_fom_state_test();

	m0_sm_group_unlock(&dummy_loc.fl_group);
}

static void cob_delete_api_test(void)
{
	m0_sm_group_lock(&dummy_loc.fl_group);

	/* Test for cd_fom_create() */
	cd_fom_create_test();

	/* Test for cd_fom_fini() */
	cd_fom_fini_test();

	/* Test for cd_fom_fini() */
	cd_fom_get_test();

	/* Test cd_fom_populate() */
	cd_fom_populate_test();

	/* Test cd_stob_delete() */
	cd_stob_delete_test();

	/* Test cd_cob_delete() */
	cd_cob_delete_test();

	/* Test for cd_fom_tick() */
	cd_fom_state_test();

	m0_sm_group_unlock(&dummy_loc.fl_group);
}

static inline void cobfoms_fv_updates(void)
{
	struct m0_reqh      *reqh;
	struct m0_poolmach  *pm;
	struct m0_pool_event event;
	int rc;

	event.pe_type  = M0_POOL_DEVICE;
	event.pe_index = 1;
	event.pe_state = M0_PNDS_FAILED;

	reqh = m0_cs_reqh_get(&cut->cu_sctx.rsx_mero_ctx, "ioservice");
	M0_UT_ASSERT(reqh != NULL);

	pm = m0_ios_poolmach_get(reqh);
	M0_UT_ASSERT(pm != NULL);

	rc = m0_poolmach_state_transit(pm, &event);
	M0_UT_ASSERT(rc == 0);

	cobfoms_send_internal(&m0_fop_cob_create_fopt, &m0_fop_cob_delete_fopt,
			      M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH,
			      M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH,
			      COB_FOP_SINGLE);
}

#define COB_DATA(data) M0_XCODE_OBJ(m0_fop_cob_common_xc, data)

static void cobfoms_fol_verify(void)
{
	struct m0_reqh		 *reqh;
	struct m0_fol_rec	  dec_cc_rec;
	struct m0_fol_rec	  dec_cd_rec;
	struct m0_dtx             dtx;
	int			  result;
	struct m0_fol_rec_part	 *dec_part;
	struct m0_fop_cob_common *cob_cmn;
	struct m0_fop		 *c_fop;
	struct m0_fop		 *d_fop;

	cobfoms_fop_thread_init(1, 1);
	cobfoms_fops_dispatch(&m0_fop_cob_create_fopt, 0);
	cobfoms_fops_dispatch(&m0_fop_cob_delete_fopt, 0);

	c_fop = cut->cu_createfops[0];
	d_fop = cut->cu_deletefops[0];

	reqh = m0_cs_reqh_get(&cut->cu_sctx.rsx_mero_ctx, "ioservice");
	M0_UT_ASSERT(reqh != NULL);

	m0_dtx_init(&dtx);
	result = m0_dtx_open(&dtx, reqh->rh_dbenv);
	M0_UT_ASSERT(result == 0);

	result = m0_fol_rec_lookup(reqh->rh_fol, &dtx.tx_dbtx,
				   reqh->rh_fol->f_lsn - 2, &dec_cc_rec);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(dec_cc_rec.fr_desc.rd_header.rh_parts_nr == 1);

	result = m0_fol_rec_lookup(reqh->rh_fol, &dtx.tx_dbtx,
				   reqh->rh_fol->f_lsn - 1, &dec_cd_rec);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(dec_cd_rec.fr_desc.rd_header.rh_parts_nr == 1);

	m0_tl_for(m0_rec_part, &dec_cd_rec.fr_fol_rec_parts, dec_part) {
		if (dec_part->rp_ops->rpo_type->rpt_index ==
		    m0_fop_fol_rec_part_type.rpt_index) {
			struct m0_fop_fol_rec_part *fp_part;
			struct m0_fop_cob_delete   *del_fop;
			struct m0_fop_cob_op_reply *cob_rep;
			struct m0_fop_type *ftype;

			fp_part = dec_part->rp_data;
			del_fop = fp_part->ffrp_fop;
			cob_rep = fp_part->ffrp_rep;
			cob_cmn = m0_cobfop_common_get(d_fop);
			M0_UT_ASSERT(m0_xcode_cmp(
				     &COB_DATA(&del_fop->cd_common),
				     &COB_DATA(cob_cmn)) == 0);
			M0_UT_ASSERT(cob_rep->cor_rc == 0);

			ftype = m0_fop_type_find(fp_part->ffrp_fop_code);
			M0_UT_ASSERT(ftype != NULL);
			M0_UT_ASSERT(ftype->ft_ops->fto_undo != NULL &&
				     ftype->ft_ops->fto_redo != NULL);
			ftype->ft_ops->fto_undo(fp_part, reqh->rh_fol);
			m0_reqh_fom_domain_idle_wait(reqh);
		}
	} m0_tl_endfor;

	m0_tl_for(m0_rec_part, &dec_cc_rec.fr_fol_rec_parts, dec_part) {
		if (dec_part->rp_ops->rpo_type->rpt_index ==
		    m0_fop_fol_rec_part_type.rpt_index) {
			struct m0_fop_fol_rec_part  *fp_part;
			struct m0_fop_cob_create    *create_fop;
			struct m0_fop_cob_op_reply  *cob_rep;
			struct m0_fop_type *ftype;

			fp_part    = dec_part->rp_data;
			create_fop = fp_part->ffrp_fop;
			cob_rep    = fp_part->ffrp_rep;
			cob_cmn    = m0_cobfop_common_get(c_fop);
			M0_UT_ASSERT(m0_xcode_cmp(
				     &COB_DATA(&create_fop->cc_common),
				     &COB_DATA(cob_cmn)) == 0);
			M0_UT_ASSERT(cob_rep->cor_rc == 0);

			ftype = m0_fop_type_find(fp_part->ffrp_fop_code);
			M0_UT_ASSERT(ftype != NULL);
			M0_UT_ASSERT(ftype->ft_ops->fto_undo != NULL &&
				     ftype->ft_ops->fto_redo != NULL);
			ftype->ft_ops->fto_undo(fp_part, reqh->rh_fol);
			m0_reqh_fom_domain_idle_wait(reqh);
		}
	} m0_tl_endfor;

	m0_fol_lookup_rec_fini(&dec_cc_rec);
	m0_fol_lookup_rec_fini(&dec_cd_rec);
	m0_dtx_done(&dtx);

	cobfoms_fop_thread_fini(&m0_fop_cob_create_fopt, &m0_fop_cob_delete_fopt);
}

const struct m0_test_suite cobfoms_ut = {
	.ts_name  = "cob-foms-ut",
	.ts_init  = NULL,
	.ts_fini  = NULL,
	.ts_tests = {
		{ "cobfoms_utinit",                 cobfoms_utinit},
		{ "cobfoms_single_fop",             cobfoms_single},
		{ "cobfoms_multiple_fops",          cobfoms_multiple},
		{ "cobfoms_preexisting_cob_create", cobfoms_preexisting_cob},
		{ "cobfoms_delete_nonexistent_cob", cobfoms_del_nonexist_cob},
		{ "cobfoms_create_cob_apitest",     cob_create_api_test},
		{ "cobfoms_delete_cob_apitest",     cob_delete_api_test},
		{ "cob_create_delete_fol_verify",   cobfoms_fol_verify},
		{ "single_fop_with_mismatch_fv",    cobfoms_fv_updates},
		{ "cobfoms_utfini",                 cobfoms_utfini},
		{ NULL, NULL }
	}
};
