/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 30-May-2014
 */

/**
 * @page DLD-ss_svc Start_Stop Service
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "sss/ss_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SSS
#include "lib/trace.h"
#include "lib/assert.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "reqh/reqh_service.h"
#include "mero/setup.h"
#include "sss/ss_fops.h"
#include "sss/ss_svc.h"

struct m0_addb_ctx m0_ss_svc_addb_ctx;
struct m0_addb_ctx m0_ss_fom_addb_ctx;

static int ss_fom_create(struct m0_fop *fop, struct m0_fom **out,
			  struct m0_reqh *reqh);
static int ss_fom_tick(struct m0_fom *fom);
static void ss_fom_fini(struct m0_fom *fom);
static void ss_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);
static size_t ss_fom_home_locality(const struct m0_fom *fom);

static int ss_svc_rso_start(struct m0_reqh_service *service)
{
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STARTING);
	return 0;
}

static void ss_svc_rso_stop(struct m0_reqh_service *service)
{
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STOPPED);
}

static void ss_svc_rso_fini(struct m0_reqh_service *service)
{
	struct ss_svc *svc = container_of(service, struct ss_svc, sss_reqhs);

	M0_ENTRY();
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STOPPED);

	m0_free(svc);
	M0_LEAVE();
}

static const struct m0_reqh_service_ops ss_svc_ops = {
	.rso_start = ss_svc_rso_start,
	.rso_stop  = ss_svc_rso_stop,
	.rso_fini  = ss_svc_rso_fini
};

static int ss_svc_rsto_service_allocate(struct m0_reqh_service      **service,
					struct m0_reqh_service_type  *stype,
					struct m0_reqh_context       *rctx)
{
	struct ss_svc *svc;

	M0_ENTRY();
	M0_PRE(service != NULL && stype != NULL);

	SS_ALLOC_PTR(svc, &m0_ss_svc_addb_ctx, SERVICE_ALLOC);
	if (svc == NULL)
		return M0_RC(-ENOMEM);

	*service = &svc->sss_reqhs;
	(*service)->rs_type = stype;
	(*service)->rs_ops  = &ss_svc_ops;

	return M0_RC(0);
}

static const struct m0_reqh_service_type_ops ss_svc_type_ops = {
	.rsto_service_allocate = ss_svc_rsto_service_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_ss_svc_type, &ss_svc_type_ops,
			    M0_START_STOP_SVC_NAME, &m0_addb_ct_ss_svc, 1);

/*
 * Public interfaces.
 */
M0_INTERNAL int m0_ss_svc_init(void)
{
	int rc;

	M0_ENTRY();
	m0_addb_ctx_type_register(&m0_addb_ct_ss_svc);
	m0_addb_ctx_type_register(&m0_addb_ct_ss_fom);
	rc = m0_reqh_service_type_register(&m0_ss_svc_type);
	if (rc != 0)
		return M0_RC(rc);

	rc = m0_ss_fops_init();
	if (rc != 0)
		m0_reqh_service_type_unregister(&m0_ss_svc_type);
	return M0_RC(rc);
}

M0_INTERNAL void m0_ss_svc_fini(void)
{
	M0_ENTRY();
	m0_reqh_service_type_unregister(&m0_ss_svc_type);
	m0_ss_fops_fini();
	M0_LEAVE();
}

/*
 * Start Stop fom.
 */
struct m0_fom_ops ss_fom_ops = {
	.fo_tick          = ss_fom_tick,
	.fo_home_locality = ss_fom_home_locality,
	.fo_addb_init     = ss_fom_addb_init,
	.fo_fini          = ss_fom_fini
};

const struct m0_fom_type_ops ss_fom_type_ops = {
	.fto_create = ss_fom_create
};

struct m0_sm_state_descr ss_fom_phases[] = {
	[START_STOP_FOM_INIT]= {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "Init",
		.sd_allowed = M0_BITS(START_STOP_FOM_START,
				      START_STOP_FOM_STOP,
				      START_STOP_FOM_STATUS,
				      M0_FOPH_FAILURE),
	},
	[START_STOP_FOM_START]= {
		.sd_name    = "Start",
		.sd_allowed = M0_BITS(START_STOP_FOM_START_ASYNC,
				      M0_FOPH_FAILURE),
	},
	[START_STOP_FOM_START_ASYNC]= {
		.sd_name    = "Async Start",
		.sd_allowed = M0_BITS(START_STOP_FOM_START_WAIT,
				      M0_FOPH_FAILURE),
	},
	[START_STOP_FOM_START_WAIT]= {
		.sd_name    = "Wait",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
	[START_STOP_FOM_STOP]= {
		.sd_name    = "Stop",
		.sd_allowed = M0_BITS(START_STOP_FOM_STOP_WAIT,
				      M0_FOPH_FAILURE),
	},
	[START_STOP_FOM_STOP_WAIT] = {
		.sd_name    = "Stop wait",
		.sd_allowed = M0_BITS(START_STOP_FOM_STOP_WAIT,
				      M0_FOPH_SUCCESS),
	},
	[START_STOP_FOM_STATUS]= {
		.sd_name    = "Status",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
};

struct m0_sm_conf ss_fom_conf = {
	.scf_name      = "ss-fom-sm",
	.scf_nr_states = ARRAY_SIZE(ss_fom_phases),
	.scf_state     = ss_fom_phases
};

static int
ss_fom_create(struct m0_fop *fop, struct m0_fom **out, struct m0_reqh *reqh)
{
	struct ss_fom            *ssfom;
	struct m0_fom            *fom;
	struct m0_fop            *rfop;
	struct m0_sssservice_rep *ssrep_fop;

	M0_ENTRY();
	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	SS_ALLOC_PTR(ssfom, &m0_ss_fom_addb_ctx, FOM_ALLOC);
	if (ssfom == NULL)
		return M0_RC(-ENOMEM);

	SS_ALLOC_PTR(ssrep_fop, &m0_ss_fom_addb_ctx, REP_FOP_ALLOC);
	if (ssrep_fop == NULL)
		goto err;

	rfop = m0_fop_alloc(&m0_fop_ss_rep_fopt, ssrep_fop);
	if (rfop == NULL)
		goto err;

	fom = &ssfom->ssf_fom;
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ss_fom_ops, fop, rfop,
		    reqh);

	ssfom->ssf_magic = M0_SS_FOM_MAGIC;
	*out = fom;
	return M0_RC(0);
err:
	m0_free(ssrep_fop);
	m0_free(ssfom);
	return M0_RC(-ENOMEM);
}

static int ss_fom_phase_move(struct m0_fom *fom, uint32_t cmd)
{
	int rc = 0;

	switch(cmd) {
	case M0_SERVICE_START:
		m0_fom_phase_set(fom, START_STOP_FOM_START);
		break;
	case M0_SERVICE_STOP:
		m0_fom_phase_set(fom, START_STOP_FOM_STOP);
		break;
	case M0_SERVICE_STATUS:
		m0_fom_phase_set(fom, START_STOP_FOM_STATUS);
		break;
	default :
		rc = -EPROTO;
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		break;
	}
	return rc;
}

static bool ss_check_ftype(const struct m0_fid *fid)
{
	return m0_fid_type_getfid(fid) == &M0_CONF_SERVICE_TYPE.cot_ftype;
}

static int ss_fom_tick(struct m0_fom *fom)
{
	struct ss_fom            *ssfom;
	struct m0_reqh           *reqh;
	struct m0_sssservice_req *fop;
	struct m0_sssservice_rep *rep;
	struct m0_reqh_context	 *rctx;
	int                       rc;

	M0_PRE(fom != NULL);

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);

	ssfom = container_of(fom, struct ss_fom, ssf_fom);
	fop = m0_fop_data(fom->fo_fop);
	rep = m0_fop_data(fom->fo_rep_fop);
	reqh = fom->fo_loc->fl_dom->fd_reqh;

	switch(m0_fom_phase(fom)) {
	case START_STOP_FOM_INIT:
		strncpy(ssfom->ssf_sname, (const char *)fop->ss_name.s_buf,
			min32u(sizeof ssfom->ssf_sname, fop->ss_name.s_len));
		ssfom->ssf_stype = m0_reqh_service_type_find(ssfom->ssf_sname);

		if (ssfom->ssf_stype != NULL && ss_check_ftype(&fop->ss_id)) {
			ssfom->ssf_svc =
				m0_reqh_service_find(ssfom->ssf_stype, reqh);
			rep->ssr_rc = ss_fom_phase_move(fom, fop->ss_cmd);
		} else {
			rep->ssr_rc = -ENOENT;
			m0_fom_phase_move(fom, rep->ssr_rc, M0_FOPH_FAILURE);
		}
		rc = M0_FSO_AGAIN;
		break;
	case START_STOP_FOM_START:
		if (ssfom->ssf_svc == NULL) {
			reqh = fom->fo_loc->fl_dom->fd_reqh;
			rctx = container_of(reqh, struct m0_reqh_context,
					    rc_reqh);
			rc = m0_reqh_service_allocate(&ssfom->ssf_svc,
						      ssfom->ssf_stype, rctx);
			if (rc == 0) {
				struct m0_uint128 *uuid =
					&M0_UINT128(fop->ss_id.f_container,
						    fop->ss_id.f_key);
				m0_reqh_service_init(ssfom->ssf_svc, reqh,
						     uuid);
			} else
				rep->ssr_rc = rc;
		} else
			rep->ssr_rc = -EALREADY;

		m0_fom_phase_moveif(fom, rep->ssr_rc,
				    START_STOP_FOM_START_ASYNC,
				    M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
		break;
	case START_STOP_FOM_START_ASYNC:
		M0_PRE(m0_reqh_service_state_get(ssfom->ssf_svc) ==
			M0_RST_INITIALISED);

		ssfom->ssf_ctx.sac_service = ssfom->ssf_svc;
		ssfom->ssf_ctx.sac_fom = fom;
		rep->ssr_rc = m0_reqh_service_start_async(&ssfom->ssf_ctx);

		m0_fom_phase_moveif(fom, rep->ssr_rc,
				    START_STOP_FOM_START_WAIT, M0_FOPH_FAILURE);
		rc = rep->ssr_rc == 0 ? M0_FSO_WAIT : M0_FSO_AGAIN;
		break;
	case START_STOP_FOM_START_WAIT:
		if (ssfom->ssf_ctx.sac_rc == 0)
			m0_reqh_service_started(ssfom->ssf_svc);
		else
			m0_reqh_service_failed(ssfom->ssf_svc);

		rep->ssr_rc = ssfom->ssf_ctx.sac_rc;
		rep->ssr_status = m0_reqh_service_state_get(ssfom->ssf_svc);

		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
		break;
	case START_STOP_FOM_STOP:
		rep->ssr_rc = ssfom->ssf_svc == NULL ? -ENOENT :
			m0_reqh_service_state_get(ssfom->ssf_svc) ==
				M0_RST_STOPPING ? -EALREADY : 0;

		if (rep->ssr_rc == 0)
			m0_reqh_service_prepare_to_stop(ssfom->ssf_svc);

		m0_fom_phase_moveif(fom, rep->ssr_rc, START_STOP_FOM_STOP_WAIT,
				    M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
		break;
	case START_STOP_FOM_STOP_WAIT:
		if (m0_fom_domain_is_idle_for(&reqh->rh_fom_dom,
					      ssfom->ssf_svc)) {
			struct m0_reqh_service *svc = ssfom->ssf_svc;

			m0_reqh_service_stop(svc);

			rep->ssr_status = m0_reqh_service_state_get(svc);
			rep->ssr_rc = 0;

			m0_reqh_service_fini(svc);
			m0_fom_phase_move(fom, rep->ssr_rc, M0_FOPH_SUCCESS);
			rc = M0_FSO_AGAIN;
		} else {
			m0_sm_group_lock(&reqh->rh_sm_grp);
			m0_fom_wait_on(fom, &reqh->rh_sm_grp.s_chan,
				       &fom->fo_cb);
			m0_sm_group_unlock(&reqh->rh_sm_grp);

			m0_fom_phase_move(fom, rep->ssr_rc,
					  START_STOP_FOM_STOP_WAIT);
			rc = M0_FSO_WAIT;
		}
		break;
	case START_STOP_FOM_STATUS:
		if (ssfom->ssf_svc != NULL)
			rep->ssr_status =
				m0_reqh_service_state_get(ssfom->ssf_svc);
		else {
			rep->ssr_status = M0_RST_STOPPED;
			rep->ssr_rc = ssfom->ssf_stype != NULL ? 0 : -ENOENT;
		}

		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
		break;
	default:
		M0_IMPOSSIBLE("Impossible phase.");
		break;
	}

	return rc;
}

static void ss_fom_fini(struct m0_fom *fom)
{
	struct ss_fom *ssfom;

	M0_ENTRY();
	M0_PRE(fom != NULL);
	ssfom = container_of(fom, struct ss_fom, ssf_fom);

	m0_fom_fini(fom);
	m0_free(ssfom);
	M0_LEAVE();
}

static void ss_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_ss_fom,
			 &fom->fo_service->rs_addb_ctx);
}

static size_t ss_fom_home_locality(const struct m0_fom *fom)
{
	return 1;
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
