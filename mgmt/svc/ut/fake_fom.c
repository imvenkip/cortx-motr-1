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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 25-Mar-2013
 */

/* This file is designed to be included by mgmt/svc/ut/mgmt_svc_ut.c */

/*
 ******************************************************************************
 * Fake FOM
 ******************************************************************************
 */
struct m0_fop_type m0_fop_mgmt_svc_ut_fake_fopt;

struct mgmt_svc_ut_fake_fom {
	struct m0_fom msuff_fom;
};

enum mgmt_svc_ut_fake_fop_phases {
	MGMT_SVC_UT_FAKE_FOP_INIT = M0_FOM_PHASE_INIT,
	MGMT_SVC_UT_FAKE_FOP_FINI = M0_FOM_PHASE_FINISH
};

static struct m0_sm_state_descr mgmt_svc_ut_fake_fop_descr[] = {
        [MGMT_SVC_UT_FAKE_FOP_INIT] = {
                .sd_flags       = M0_SDF_INITIAL,
                .sd_name        = "Init",
                .sd_allowed     = M0_BITS(MGMT_SVC_UT_FAKE_FOP_FINI)
        },
        [MGMT_SVC_UT_FAKE_FOP_FINI] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Fini",
                .sd_allowed     = 0
        },
};

static struct m0_sm_conf mgmt_svc_ut_fake_fop_sm = {
	.scf_name      = "Service State FOM Phases",
	.scf_nr_states = ARRAY_SIZE(mgmt_svc_ut_fake_fop_descr),
	.scf_state     = mgmt_svc_ut_fake_fop_descr,
};

static void mgmt_svc_ut_fake_fop_fo_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
}

static size_t mgmt_svc_ut_fake_fop_fo_locality(const struct m0_fom *fom)
{
	return 0;
}

static int mgmt_svc_ut_fake_fop_fo_tick(struct m0_fom *fom)
{
	m0_fom_phase_set(fom, MGMT_SVC_UT_FAKE_FOP_FINI);
	return M0_FSO_WAIT;
}

static void mgmt_svc_ut_fake_fop_fo_addb_init(struct m0_fom *fom,
					      struct m0_addb_mc *mc)
{
	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx,
			 &m0_addb_ct_mgmt_fom_ss, /* fake ctx */
			 &fom->fo_service->rs_addb_ctx, 0);
}

static const struct m0_fom_ops mgmt_svc_ut_fake_fop_fom_ops = {
        .fo_fini          = mgmt_svc_ut_fake_fop_fo_fini,
        .fo_tick          = mgmt_svc_ut_fake_fop_fo_tick,
        .fo_home_locality = mgmt_svc_ut_fake_fop_fo_locality,
	.fo_addb_init     = mgmt_svc_ut_fake_fop_fo_addb_init
};

static int mgmt_svc_ut_fake_fop_fto_create(struct m0_fop *fop,
					   struct m0_fom **out,
					   struct m0_reqh *reqh)
{
	struct mgmt_svc_ut_fake_fom *ffom;

	M0_ALLOC_PTR(ffom);
	if (ffom == NULL)
		return -ENOMEM;
	m0_fom_init(&ffom->msuff_fom, &fop->f_type->ft_fom_type,
		    &mgmt_svc_ut_fake_fop_fom_ops, fop, NULL, reqh);
	*out = &ffom->msuff_fom;
	return 0;
}

static const struct m0_fom_type_ops mgmt_svc_ut_fake_fop_fom_type_ops = {
        .fto_create   = mgmt_svc_ut_fake_fop_fto_create
};

static int mgmt_svc_ut_fake_fop_init(void)
{
	M0_FOP_TYPE_INIT(&m0_fop_mgmt_svc_ut_fake_fopt,
			 .name      = "Mgmt Service UT Fake FOP",
			 .opcode    = M0_MGMT_SERVICE_UT_FAKE_FOP_OPCODE,
			 .xt        = m0_mgmt_svc_ut_fake_fop_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_ONEWAY,
			 .sm        = &mgmt_svc_ut_fake_fop_sm,
			 .fom_ops   = &mgmt_svc_ut_fake_fop_fom_type_ops,
			 .svc_type  = &m0_mgmt_svc_ut_svc_type);
	return 0;
}

static void mgmt_svc_ut_fake_fop_fini(void)
{
	m0_fop_type_fini(&m0_fop_mgmt_svc_ut_fake_fopt);
}

static struct m0_fop *mgmt_svc_ut_fake_fop_alloc(void)
{
	return m0_fop_alloc(&m0_fop_mgmt_svc_ut_fake_fopt, NULL);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
