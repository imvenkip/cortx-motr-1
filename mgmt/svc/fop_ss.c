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
 * Original creation date: 11-Mar-2013
 */

/**
   @page MGMT-SVC-DLD-FOP-SS Management Service Status FOP
   This FOP, defined by m0_fop_mgmt_service_status_req, conveys a request to
   return the status of a specified list of services, or of all services.
   The response is through a @ref MGMT-SVC-DLD-FOP-SS "Service Status FOP".

   The FOP defines a FOM with a simple phase state machine:
   @dot
   digraph ss_fop_phases {
	R_LOCK     -> GET_STATUS
	GET_STATUS -> R_UNLOCK
	R_UNLOCK   -> REPLY
	REPLY      -> FINI
   }
   @enddot
   The states are:
   - @b R_LOCK The request handler is read-locked.
   This is a potentially blocking call, so is sandwiched between
   m0_fom_block_enter() and m0_fom_block_leave().
   - @b GET_STATUS The mgmt_fop_ssr_fill() subroutine is used to fill in
   the reply FOP.
   - @b R_UNLOCK The request handler read lock is released.
   This is not considered to be blocking because the lock is currently held
   by the FOM.
   - @b REPLY The response FOP is sent back.
   - @b FINI The final state.

   @todo This FOM does not need the standard FOM phases defined in
   m0_generic_conf.  Re-address this when support for security phases become
   necessary.

 */


/* This file is designed to be included by mgmt/mgmt.c */

struct m0_fop_type m0_fop_mgmt_service_state_req_fopt;

#ifdef M0_MGMT_SERVICE_PRESENT
/**
   @addtogroup mgmt_svc_pvt
   @{
 */

/*
 ******************************************************************************
 * Utility subs
 ******************************************************************************
 */

/**
   Recover FOP pointer from the FOM.
 */
static struct m0_fop_mgmt_service_state_req *mgmt_fop_ss_from_fom(
							    struct m0_fom *fom)
{
	struct m0_fop *fop = fom->fo_fop;

	M0_ASSERT(fop->f_type == &m0_fop_mgmt_service_state_req_fopt);
	return m0_fop_data(fop);
}

/*
 ******************************************************************************
 * FOM
 ******************************************************************************
 */

/**
   Internal representation of the m0_fop_mgmt_service_state_req FOM.
 */
struct mgmt_fop_ss_fom {
	uint64_t      ss_magic;
	struct m0_fom ss_m0fom;
};

static const struct m0_bob_type mgmt_fop_ss_fom_bob = {
	.bt_name = "mgmt ssfom",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct mgmt_fop_ss_fom, ss_magic),
	.bt_magix = M0_MGMT_FOP_SS_FOM_MAGIC,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &mgmt_fop_ss_fom_bob, mgmt_fop_ss_fom);

/*
 ******************************************************************************
 * FOM Phase state machine
 ******************************************************************************
 */
enum mgmt_fop_ss_phases {
	MGMT_FOP_SS_PHASE_R_LOCK     = M0_FOM_PHASE_INIT,
	MGMT_FOP_SS_PHASE_FINI       = M0_FOM_PHASE_FINISH,
	MGMT_FOP_SS_PHASE_GET_STATUS = M0_FOM_PHASE_NR,
	MGMT_FOP_SS_PHASE_R_UNLOCK,
	MGMT_FOP_SS_PHASE_REPLY,

	MGMT_FOP_SS_PHASE_NR
};

static struct m0_sm_state_descr mgmt_fop_ss_descr[] = {
        [MGMT_FOP_SS_PHASE_R_LOCK] = {
                .sd_flags       = M0_SDF_INITIAL,
                .sd_name        = "ReadLock",
                .sd_allowed     = M0_BITS(MGMT_FOP_SS_PHASE_GET_STATUS)
        },
        [MGMT_FOP_SS_PHASE_GET_STATUS] = {
                .sd_flags       = 0,
                .sd_name        = "GetStatus",
                .sd_allowed     = M0_BITS(MGMT_FOP_SS_PHASE_R_UNLOCK)
        },
        [MGMT_FOP_SS_PHASE_R_UNLOCK] = {
                .sd_flags       = 0,
                .sd_name        = "ReadUnlock",
                .sd_allowed     = M0_BITS(MGMT_FOP_SS_PHASE_REPLY)
        },
        [MGMT_FOP_SS_PHASE_REPLY] = {
                .sd_flags       = 0,
                .sd_name        = "Reply",
                .sd_allowed     = M0_BITS(MGMT_FOP_SS_PHASE_FINI)
        },
        [MGMT_FOP_SS_PHASE_FINI] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Fini",
                .sd_allowed     = 0
        },
};

static struct m0_sm_conf mgmt_fop_ss_sm = {
	.scf_name      = "Service State FOM Phases",
	.scf_nr_states = ARRAY_SIZE(mgmt_fop_ss_descr),
	.scf_state     = mgmt_fop_ss_descr,
};

/*
 ******************************************************************************
 * FOM methods
 ******************************************************************************
 */
static void mgmt_fop_ss_fo_fini(struct m0_fom *fom)
{
        struct mgmt_fop_ss_fom *ssfom = bob_of(fom, struct mgmt_fop_ss_fom,
					       ss_m0fom, &mgmt_fop_ss_fom_bob);

	M0_ENTRY();

	mgmt_fop_ss_fom_bob_fini(ssfom);
	m0_fom_fini(fom);
	m0_free(ssfom);
}

static size_t mgmt_fop_ss_fo_locality(const struct m0_fom *fom)
{
	M0_PRE(mgmt_fop_ss_fom_bob_check(container_of(fom,
						      struct mgmt_fop_ss_fom,
						      ss_m0fom)));
	return m0_time_now() >> 20; /* arbitrary */
}

static int mgmt_fop_ss_fo_tick(struct m0_fom *fom)
{
	struct m0_fop_mgmt_service_state_req *ssfop;
	int rc;

	M0_PRE(mgmt_fop_ss_fom_bob_check(container_of(fom,
						      struct mgmt_fop_ss_fom,
						      ss_m0fom)));
	M0_ENTRY();
	M0_ASSERT(m0_fom_phase(fom) < MGMT_FOP_SS_PHASE_NR);
	M0_LOG(M0_DEBUG, "State: %s", fom->fo_sm_phase.sm_conf->
	       scf_state[fom->fo_sm_phase.sm_state].sd_name);

	rc = M0_FSO_AGAIN;
	switch (m0_fom_phase(fom)) {
	case MGMT_FOP_SS_PHASE_R_LOCK:
		m0_fom_block_enter(fom);
		m0_rwlock_read_lock(&m0_fom_reqh(fom)->rh_rwlock);
		m0_fom_block_leave(fom);
		m0_fom_phase_set(fom, MGMT_FOP_SS_PHASE_GET_STATUS);
		break;

	case MGMT_FOP_SS_PHASE_GET_STATUS:
		ssfop = mgmt_fop_ss_from_fom(fom);
		mgmt_fop_ssr_fill(fom, &ssfop->mssrq_services);
		m0_fom_phase_set(fom, MGMT_FOP_SS_PHASE_R_UNLOCK);
		break;

	case MGMT_FOP_SS_PHASE_R_UNLOCK:
		m0_rwlock_read_unlock(&m0_fom_reqh(fom)->rh_rwlock);
		m0_fom_phase_set(fom, MGMT_FOP_SS_PHASE_REPLY);
		break;

	case MGMT_FOP_SS_PHASE_REPLY:
		rc = m0_rpc_reply_post(&fom->fo_fop->f_item,
				       &fom->fo_rep_fop->f_item);
		M0_ASSERT(rc == 0);
		m0_fom_phase_set(fom, MGMT_FOP_SS_PHASE_FINI);
		rc = M0_FSO_WAIT;
		break;
	default:
		M0_ASSERT(m0_fom_phase(fom) < MGMT_FOP_SS_PHASE_FINI);
	}

	return M0_RC(rc);
}

static void mgmt_fop_ss_fo_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	struct m0_reqh_service               *rsvc = fom->fo_service;
	struct m0_fop_mgmt_service_state_req *ssfop = mgmt_fop_ss_from_fom(fom);

	M0_PRE(mgmt_fop_ss_fom_bob_check(container_of(fom,
						      struct mgmt_fop_ss_fom,
						      ss_m0fom)));
	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_mgmt_fom_ss,
			 &rsvc->rs_addb_ctx, ssfop->mssrq_services.msus_nr);
}

static const struct m0_fom_ops mgmt_fop_ss_fom_ops = {
        .fo_fini          = mgmt_fop_ss_fo_fini,
        .fo_tick          = mgmt_fop_ss_fo_tick,
        .fo_home_locality = mgmt_fop_ss_fo_locality,
	.fo_addb_init     = mgmt_fop_ss_fo_addb_init
};

/*
 ******************************************************************************
 * FOM type ops
 ******************************************************************************
 */
static int mgmt_fop_ss_fto_create(struct m0_fop *fop, struct m0_fom **out,
				  struct m0_reqh *reqh)
{
	int			 rc;
	struct m0_fop           *rfop;
	struct mgmt_fop_ss_fom  *ssfom = NULL;

	M0_ENTRY();

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type == &m0_fop_mgmt_service_state_req_fopt);
	M0_PRE(out != NULL);
	M0_PRE(reqh != NULL);

	rc = -ENOMEM;
	MGMT_ALLOC_PTR(ssfom, FOP_SS_FTOC_1);
	if (ssfom == NULL)
		goto failed;

	rfop = m0_fop_alloc(&m0_fop_mgmt_service_state_res_fopt, NULL);
	if (rfop == NULL) {
		MGMT_ADDB_FUNCFAIL(rc, FOP_SS_FTOC_2);
		goto failed;
	}

	/* bless now as m0_fom_init() will invoke the fo_addb_init() method */
	mgmt_fop_ss_fom_bob_init(ssfom);

	m0_fom_init(&ssfom->ss_m0fom, &fop->f_type->ft_fom_type,
		    &mgmt_fop_ss_fom_ops, fop, rfop, reqh);
	m0_fop_put(rfop);
	M0_POST(m0_ref_read(&rfop->f_ref) == 1);

	*out = &ssfom->ss_m0fom;
	return M0_RC(0);

 failed:
	if (ssfom != NULL)
		m0_free(ssfom);
	return M0_RC(rc);
}

static const struct m0_fom_type_ops mgmt_fop_ss_fom_type_ops = {
        .fto_create   = mgmt_fop_ss_fto_create
};

/** @} end group mgmt_svc_pvt */
#endif /* M0_MGMT_SERVICE_PRESENT */

/**
   @addtogroup mgmt_pvt
   @{
 */

/*
 ******************************************************************************
 * FOP initialization logic
 ******************************************************************************
 */

static int mgmt_fop_ss_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_mgmt_fom_ss);
	M0_FOP_TYPE_INIT(&m0_fop_mgmt_service_state_req_fopt,
			 .name      = "Mgmt Service Status Request",
			 .opcode    = M0_MGMT_SERVICE_STATE_OPCODE,
			 .xt        = m0_fop_mgmt_service_state_req_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
#ifdef M0_MGMT_SERVICE_PRESENT
			 .sm        = &mgmt_fop_ss_sm,
			 .fom_ops   = &mgmt_fop_ss_fom_type_ops,
			 .svc_type  = &m0_mgmt_svc_type,
#endif
			 );
	return 0;

}

static void mgmt_fop_ss_fini(void)
{
	m0_fop_type_fini(&m0_fop_mgmt_service_state_req_fopt);
}

/** @} end group mgmt_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
