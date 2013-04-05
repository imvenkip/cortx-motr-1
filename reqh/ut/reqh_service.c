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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 05-Feb-2013
 */

#include "lib/memory.h"
#include "lib/misc.h"
#include "ut/ut.h"

#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "fop/ut/fop_put_norpc.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "reqh/ut/reqh_service.h"
#include "reqh/ut/reqh_service_xc.h"
#include "rpc/rpc_opcodes.h"
#include "ut/ut.h"
#include "ut/cs_service.h"
#include "ut/cs_fop_foms.h"

static struct m0_fop_type m0_reqhut_dummy_fopt;

static int reqhut_fom_create(struct m0_fop *fop, struct m0_fom **out,
			     struct m0_reqh *reqh);
static int reqhut_fom_tick(struct m0_fom *fom);
static void reqhut_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);
static void reqhut_fom_fini(struct m0_fom *fom);
static size_t reqhut_find_fom_home_locality(const struct m0_fom *fom);

static const struct m0_fom_ops reqhut_fom_ops = {
	.fo_fini = reqhut_fom_fini,
	.fo_tick = reqhut_fom_tick,
	.fo_home_locality = reqhut_find_fom_home_locality,
	.fo_addb_init = reqhut_fom_addb_init
};

static const struct m0_fom_type_ops reqhut_fom_type_ops = {
	.fto_create = reqhut_fom_create,
};

enum {
	MAX_REQH_UT_FOP = 25
};

static int reqhut_fom_create(struct m0_fop  *fop,
			     struct m0_fom **out,
			     struct m0_reqh *reqh)
{
	struct m0_fom *fom;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &reqhut_fom_ops,
		    fop, NULL, reqh,
		    fop->f_type->ft_fom_type.ft_rstype);

	*out = fom;

	return 0;
}

static size_t reqhut_find_fom_home_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}


static int reqhut_fom_tick(struct m0_fom *fom)
{
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	return M0_FSO_WAIT;
}

static void reqhut_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static void reqhut_fom_fini(struct m0_fom *fom)
{
	/*
	 * m0_fom_fini() requires rpc machine for m0_fop_put(),
	 * which we don't have here, so put the fop explicitly.
	 */
	m0_fop_put(fom->fo_fop);
	fom->fo_fop = NULL;

	m0_fom_fini(fom);
	m0_free(fom);
}

int m0_reqhut_fop_init(void)
{
	m0_xc_reqh_service_init();
	return M0_FOP_TYPE_INIT(&m0_reqhut_dummy_fopt,
				.name      = "Reqh unit test",
				.opcode    = M0_REQH_UT_DUMMY_OPCODE,
				.xt        = m0_reqhut_dummy_xc,
				.fom_ops   = &reqhut_fom_type_ops,
				.sm        = &m0_generic_conf,
				.rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
				.svc_type  = &ds1_service_type);
}

void m0_reqhut_fop_fini(void)
{
	m0_fop_type_fini(&m0_reqhut_dummy_fopt);
	m0_xc_reqh_service_fini();
}

static void test_service(void)
{
	int                           i;
	int                           rc;
	struct m0_reqh                reqh;
	struct m0_reqh_service_type  *svct;
	struct m0_reqh_service       *reqh_svc;
	struct m0_fop                *fop;
	static struct m0_dbenv        dbenv;

	rc = m0_dbenv_init(&dbenv, "something", 0);
	M0_UT_ASSERT(rc == 0);

	M0_SET0(&reqh);

	rc = m0_reqhut_fop_init();
	M0_UT_ASSERT(rc == 0);

	rc = M0_REQH_INIT(&reqh,
			  .rhia_db        = &dbenv,
			  .rhia_mdstore   = (void *)1,
			  .rhia_fol       = (void *)1);
	M0_UT_ASSERT(rc == 0);

	svct = m0_reqh_service_type_find("ds1");
	M0_UT_ASSERT(svct != NULL);

	rc = m0_reqh_service_allocate(&reqh_svc, svct, NULL);
	M0_UT_ASSERT(rc == 0);

	m0_reqh_service_init(reqh_svc, &reqh);

	rc = m0_reqh_service_start(reqh_svc);
	M0_UT_ASSERT(rc == 0);

	fop = m0_fop_alloc(&m0_reqhut_dummy_fopt, NULL);
	M0_UT_ASSERT(fop != NULL);

	for (i = 0; i < MAX_REQH_UT_FOP; ++i)
		m0_reqh_fop_handle(&reqh, fop);

	m0_reqh_shutdown_wait(&reqh);

	m0_fop_put(fop);

	m0_reqh_service_stop(reqh_svc);
	m0_reqh_service_fini(reqh_svc);
	m0_reqh_fini(&reqh);
	m0_dbenv_fini(&dbenv);

	m0_reqhut_fop_fini();
}

const struct m0_test_suite reqh_service_ut = {
	.ts_name = "reqh-service-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "reqh service", test_service },
		{ NULL, NULL }
	}
};
M0_EXPORTED(reqh_service_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
