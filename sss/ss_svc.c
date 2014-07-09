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
static int ss_fom_tick__init(struct ss_fom *m, const struct m0_sss_req *fop,
			     struct m0_reqh *reqh);
static int ss_fom_tick__svc_alloc(struct m0_reqh *reqh, struct ss_fom *m,
				  const struct m0_fid *svc_id);
static int ss_fom_tick__stop(struct m0_reqh_service *svc);
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

static int
ss_svc_rsto_service_allocate(struct m0_reqh_service           **service,
			     const struct m0_reqh_service_type *stype,
			     struct m0_reqh_context            *rctx)
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
	[SS_FOM_INIT]= {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "SS_FOM_INIT",
		.sd_allowed = M0_BITS(SS_FOM_SVC_ALLOC, SS_FOM_STOP,
				      SS_FOM_STATUS, M0_FOPH_FAILURE),
	},
	[SS_FOM_SVC_ALLOC]= {
		.sd_name    = "SS_FOM_SVC_ALLOC",
		.sd_allowed = M0_BITS(SS_FOM_START, M0_FOPH_FAILURE),
	},
	[SS_FOM_START]= {
		.sd_name    = "SS_FOM_START",
		.sd_allowed = M0_BITS(SS_FOM_START_WAIT, M0_FOPH_FAILURE),
	},
	[SS_FOM_START_WAIT]= {
		.sd_name    = "SS_FOM_START_WAIT",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
	[SS_FOM_STOP]= {
		.sd_name    = "SS_FOM_STOP",
		.sd_allowed = M0_BITS(SS_FOM_STOP_WAIT, M0_FOPH_FAILURE),
	},
	[SS_FOM_STOP_WAIT] = {
		.sd_name    = "SS_FOM_STOP_WAIT",
		.sd_allowed = M0_BITS(SS_FOM_STOP_WAIT, M0_FOPH_SUCCESS),
	},
	[SS_FOM_STATUS]= {
		.sd_name    = "SS_FOM_STATUS",
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
	struct ss_fom     *ssfom;
	struct m0_fom     *fom;
	struct m0_fop     *rfop;
	struct m0_sss_rep *ssrep_fop;

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

static int ss_fom_tick(struct m0_fom *fom)
{
	struct ss_fom     *m;
	struct m0_reqh    *reqh;
	struct m0_sss_req *fop;
	struct m0_sss_rep *rep;

	M0_PRE(fom != NULL);

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);

	m = container_of(fom, struct ss_fom, ssf_fom);
	fop = m0_fop_data(fom->fo_fop);
	rep = m0_fop_data(fom->fo_rep_fop);
	reqh = fom->fo_loc->fl_dom->fd_reqh;

	switch (m0_fom_phase(fom)) {
	case SS_FOM_INIT:
		rep->ssr_rc = ss_fom_tick__init(m, fop, reqh);
		if (rep->ssr_rc != 0)
			m0_fom_phase_move(fom, rep->ssr_rc, M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_SVC_ALLOC:
		rep->ssr_rc = ss_fom_tick__svc_alloc(reqh, m, &fop->ss_id);
		m0_fom_phase_moveif(fom, rep->ssr_rc, SS_FOM_START,
				    M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_START:
		M0_PRE(m0_reqh_service_state_get(m->ssf_svc) ==
		       M0_RST_INITIALISED);
		m->ssf_ctx.sac_service = m->ssf_svc;
		m->ssf_ctx.sac_fom = fom;
		rep->ssr_rc = m0_reqh_service_start_async(&m->ssf_ctx);
		m0_fom_phase_moveif(fom, rep->ssr_rc, SS_FOM_START_WAIT,
				    M0_FOPH_FAILURE);
		return rep->ssr_rc == 0 ? M0_FSO_WAIT : M0_FSO_AGAIN;

	case SS_FOM_START_WAIT:
		if (m->ssf_ctx.sac_rc == 0)
			m0_reqh_service_started(m->ssf_svc);
		else
			m0_reqh_service_failed(m->ssf_svc);
		rep->ssr_rc = m->ssf_ctx.sac_rc;
		rep->ssr_status = m0_reqh_service_state_get(m->ssf_svc);
		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_STOP:
		rep->ssr_rc = ss_fom_tick__stop(m->ssf_svc);
		m0_fom_phase_moveif(fom, rep->ssr_rc, SS_FOM_STOP_WAIT,
				    M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_STOP_WAIT:
		if (m0_fom_domain_is_idle_for(&reqh->rh_fom_dom, m->ssf_svc)) {
			struct m0_reqh_service *svc = m->ssf_svc;

			m0_reqh_service_stop(svc);
			rep->ssr_status = m0_reqh_service_state_get(svc);
			rep->ssr_rc = 0;
			m0_reqh_service_fini(svc);
			m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
			return M0_FSO_AGAIN;
		}
		m0_sm_group_lock(&reqh->rh_sm_grp);
		m0_fom_wait_on(fom, &reqh->rh_sm_grp.s_chan, &fom->fo_cb);
		m0_sm_group_unlock(&reqh->rh_sm_grp);
		m0_fom_phase_set(fom, SS_FOM_STOP_WAIT);
		return M0_FSO_WAIT;

	case SS_FOM_STATUS:
		if (m->ssf_svc == NULL) {
			rep->ssr_status = M0_RST_STOPPED;
			rep->ssr_rc = m->ssf_stype == NULL ? -ENOENT : 0;
		} else {
			rep->ssr_status =
				m0_reqh_service_state_get(m->ssf_svc);
			rep->ssr_rc = 0;
		}
		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	default:
		M0_IMPOSSIBLE("Invalid phase");
	}
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

static int ss_fom_tick__init(struct ss_fom *m, const struct m0_sss_req *fop,
			     struct m0_reqh *reqh)
{
	static enum ss_fom_phases next_phase[] = {
		[M0_SERVICE_START]  = SS_FOM_SVC_ALLOC,
		[M0_SERVICE_STOP]   = SS_FOM_STOP,
		[M0_SERVICE_STATUS] = SS_FOM_STATUS
	};

	if (!IS_IN_ARRAY(fop->ss_cmd, next_phase) ||
	    m0_fid_type_getfid(&fop->ss_id) != &M0_CONF_SERVICE_TYPE.cot_ftype)
		return -ENOENT;

	strncpy(m->ssf_sname, (const char *)fop->ss_name.s_buf,
		min32u(sizeof m->ssf_sname, fop->ss_name.s_len));
	m->ssf_stype = m0_reqh_service_type_find(m->ssf_sname);
	if (m->ssf_stype == NULL)
		return -ENOENT;

	m->ssf_svc = m0_reqh_service_find(m->ssf_stype, reqh);
	m0_fom_phase_set(&m->ssf_fom, next_phase[fop->ss_cmd]);
	return 0;
}

static int ss_fom_tick__svc_alloc(struct m0_reqh *reqh, struct ss_fom *m,
				  const struct m0_fid *svc_id)
{
	int                     rc;
	struct m0_reqh_context *rctx =
		container_of(reqh, struct m0_reqh_context, rc_reqh);

	if (m->ssf_svc != NULL)
		return -EALREADY;

	rc = m0_reqh_service_allocate(&m->ssf_svc, m->ssf_stype, rctx);
	if (rc == 0)
		m0_reqh_service_init(m->ssf_svc, reqh,
				     &M0_UINT128(svc_id->f_container,
						 svc_id->f_key));
	return rc;
}

static int ss_fom_tick__stop(struct m0_reqh_service *svc)
{
	if (svc == NULL)
		return -ENOENT;
	if (m0_reqh_service_state_get(svc) == M0_RST_STOPPING)
		return -EALREADY;
	m0_reqh_service_prepare_to_stop(svc);
	return 0;
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
