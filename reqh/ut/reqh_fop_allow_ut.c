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
 * Original author: Rajanikant Chirmade <Rajnaikant_Chirmade@xyratex.com>
 * Original creation date: 28-July-2014
 */
#include "lib/buf.h"
#include "lib/memory.h"
#include "ut/ut.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"
#include "sss/ss_fops.h"
#include "rpc/item.h"
#include "rpc/rpclib.h"
#include "reqh/ut/reqh_service.h"
#include "reqh/ut/reqh_service_xc.h"
#include "ut/cs_fop_foms.h"
#include "ut/ut_rpc_machine.h"

#include "rpc/ut/clnt_srv_ctx.c"
#include "reqh/ut/reqhut_fom.c"

static int fom_tick(struct m0_fom *fom);

static char *ut_server_argv[] = {
	"rpclib_ut", "-T", "AD", "-D", SERVER_DB_NAME,
	"-S", SERVER_STOB_NAME, "-A", SERVER_ADDB_STOB_NAME,
	"-e", SERVER_ENDPOINT, "-w", "10"
};

struct m0_reqh_service_type *ut_stypes[] = {
        &ds1_service_type,
};

static const struct m0_fid ut_fid = {
	.f_container = 8286623314361712755,
	.f_key       = 1
};

static const struct m0_fom_ops ut_fom_ops = {
	.fo_fini = reqhut_fom_fini,
	.fo_tick = fom_tick,
	.fo_home_locality = reqhut_find_fom_home_locality,
	.fo_addb_init = reqhut_fom_addb_init
};

static int fom_create(struct m0_fop  *fop,
		      struct m0_fom **out,
		      struct m0_reqh *reqh)
{
	struct m0_fom *fom;
        struct m0_fop *rfop;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	rfop = m0_fop_reply_alloc(fop, &m0_fop_generic_reply_fopt);
	M0_UT_ASSERT(rfop != NULL);

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ut_fom_ops,
		    fop, rfop, reqh);

	*out = fom;

	return 0;
}

static const struct m0_fom_type_ops ut_fom_type_ops = {
        .fto_create = fom_create,
};


static int fom_tick(struct m0_fom *fom)
{
	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);

	m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
	return M0_FSO_AGAIN;
}

static int send_fop()
{
	struct m0_fop *fop;
	int            rc;

	fop = m0_fop_alloc(&m0_reqhut_dummy_fopt, NULL, &cctx.rcx_rpc_machine);
	M0_UT_ASSERT(fop != NULL);

	rc = m0_rpc_post_sync(fop, &cctx.rcx_session, NULL, 0);
	m0_fop_put_lock(fop);

	return rc;
}

static struct m0_fop *ut_ssfop_alloc(const char *name, uint32_t cmd)
{
	struct m0_fop     *fop;
	struct m0_sss_req *ss_fop;

	M0_ALLOC_PTR(fop);
	M0_UT_ASSERT(fop != NULL);

	M0_ALLOC_PTR(ss_fop);
	M0_UT_ASSERT(ss_fop != NULL);

	m0_buf_init(&ss_fop->ss_name, (void *)name, strlen(name));
	ss_fop->ss_cmd = cmd;
	ss_fop->ss_id  = ut_fid;

	m0_fop_init(fop, &m0_fop_ss_fopt, (void *)ss_fop, m0_ss_fop_release);

	return fop;

}

static int ut_sss_req(const char *name, uint32_t cmd)
{
	int                 rc;
	struct m0_fop      *fop;
	struct m0_fop      *rfop;
	struct m0_rpc_item *item;
	struct m0_sss_rep  *ss_rfop;

	fop = ut_ssfop_alloc(name, cmd);
	item = &fop->f_item;

	rc = m0_rpc_post_sync(fop, &cctx.rcx_session, NULL, 0);
	M0_UT_ASSERT(rc == 0);

	rfop  = m0_rpc_item_to_fop(item->ri_reply);
	M0_UT_ASSERT(rfop != NULL);

	ss_rfop = m0_fop_data(rfop);
	M0_UT_ASSERT(ss_rfop->ssr_rc == 0);
	rc = ss_rfop->ssr_state;

	m0_fop_put_lock(fop);

	return rc;
}

static void fop_allow_test(void)
{
	int rc;

	sctx.rsx_argv             = ut_server_argv,
	sctx.rsx_argc             = ARRAY_SIZE(ut_server_argv),
	sctx.rsx_service_types    = ut_stypes,

	rc = m0_reqhut_fop_init(&ut_fom_type_ops);
	M0_UT_ASSERT(rc == 0);

	start_rpc_client_and_server();

	M0_UT_ASSERT(ut_sss_req(ds1_service_type.rst_name,
				M0_SERVICE_STATUS) == M0_RST_STOPPED);

	M0_UT_ASSERT(send_fop() == -ESHUTDOWN);

	M0_UT_ASSERT(ut_sss_req(ds1_service_type.rst_name,
				M0_SERVICE_START) == M0_RST_STARTED);

	M0_UT_ASSERT(send_fop() == 0);

	m0_reqhut_fop_fini();
	stop_rpc_client_and_server();
}

struct m0_ut_suite reqh_fop_allow_ut = {
        .ts_name = "reqh-fop-allow-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "reqh-fop-allow", fop_allow_test },
                { NULL, NULL }
        }
};
M0_EXPORTED(reqh_fop_allow_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
