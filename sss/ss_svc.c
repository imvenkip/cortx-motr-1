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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SSS
#include "lib/trace.h"

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/finject.h" /* M0_FI_ENABLED */
#include "lib/misc.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "reqh/reqh_service.h"
#include "rpc/rpc_opcodes.h"
#ifndef __KERNEL__
#include "mero/setup.h"
#endif
#include "spiel/spiel.h" /* m0_spiel_health */
#include "sss/process_fops.h"
#include "sss/ss_fops.h"
#include "sss/ss_svc.h"

static int ss_fom_create(struct m0_fop *fop, struct m0_fom **out,
			  struct m0_reqh *reqh);
static int ss_fom_tick(struct m0_fom *fom);
static int ss_fom_tick__init(struct ss_fom *m, const struct m0_sss_req *fop,
			     const struct m0_reqh *reqh);
static void ss_fom_fini(struct m0_fom *fom);
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

static enum m0_service_health ss_svc_rso_health(struct m0_reqh_service *service)
{
	M0_ENTRY("service %p", service);

	return M0_RC(M0_HEALTH_GOOD);
}

static const struct m0_reqh_service_ops ss_svc_ops = {
	.rso_start  = ss_svc_rso_start,
	.rso_stop   = ss_svc_rso_stop,
	.rso_fini   = ss_svc_rso_fini,
	.rso_health = ss_svc_rso_health
};

static int
ss_svc_rsto_service_allocate(struct m0_reqh_service **service,
			     const struct m0_reqh_service_type *stype)
{
	struct ss_svc *svc;

	M0_ENTRY();
	M0_PRE(service != NULL && stype != NULL);

	if (M0_FI_ENABLED("fail_allocation"))
		svc = NULL;
	else
		M0_ALLOC_PTR(svc);

	if (svc == NULL)
		return M0_ERR(-ENOMEM);

	*service = &svc->sss_reqhs;
	(*service)->rs_type = stype;
	(*service)->rs_ops  = &ss_svc_ops;
	return M0_RC(0);
}

static const struct m0_reqh_service_type_ops ss_svc_type_ops = {
	.rsto_service_allocate = ss_svc_rsto_service_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_ss_svc_type, &ss_svc_type_ops,
			    "sss", 1, M0_CST_SSS);

/*
 * Public interfaces.
 */
M0_INTERNAL int m0_ss_svc_init(void)
{
	int rc;

	M0_ENTRY();
	rc = m0_reqh_service_type_register(&m0_ss_svc_type);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_ss_fops_init();
	if (rc != 0)
		m0_reqh_service_type_unregister(&m0_ss_svc_type);

	rc = m0_ss_process_fops_init();
	if (rc != 0){
		m0_ss_fops_fini();
		m0_reqh_service_type_unregister(&m0_ss_svc_type);
	}

	return M0_RC(rc);
}

M0_INTERNAL void m0_ss_svc_fini(void)
{
	M0_ENTRY();
	m0_reqh_service_type_unregister(&m0_ss_svc_type);
	m0_ss_fops_fini();
	m0_ss_process_fops_fini();
	M0_LEAVE();
}

/*
 * Start Stop fom.
 */
const struct m0_fom_ops ss_fom_ops = {
	.fo_tick          = ss_fom_tick,
	.fo_home_locality = ss_fom_home_locality,
	.fo_fini          = ss_fom_fini
};

const struct m0_fom_type_ops ss_fom_type_ops = {
	.fto_create = ss_fom_create
};

struct m0_sm_state_descr ss_fom_phases[] = {
	[SS_FOM_INIT]= {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "SS_FOM_INIT",
		.sd_allowed = M0_BITS(SS_FOM_SVC_INIT, SS_FOM_QUIESCE,
				      SS_FOM_START, SS_FOM_STOP, SS_FOM_STATUS,
				      M0_FOPH_FAILURE, SS_FOM_HEALTH),
	},
	[SS_FOM_SVC_INIT]= {
		.sd_name    = "SS_FOM_SVC_INIT",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
	[SS_FOM_START]= {
		.sd_name    = "SS_FOM_START",
		.sd_allowed = M0_BITS(SS_FOM_START_WAIT, M0_FOPH_FAILURE),
	},
	[SS_FOM_START_WAIT]= {
		.sd_name    = "SS_FOM_START_WAIT",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
	[SS_FOM_QUIESCE]= {
		.sd_name    = "SS_FOM_QUIESCE",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
	[SS_FOM_STOP] = {
		.sd_name    = "SS_FOM_STOP",
		.sd_allowed = M0_BITS(SS_FOM_STOP, M0_FOPH_SUCCESS),
	},
	[SS_FOM_STATUS]= {
		.sd_name    = "SS_FOM_STATUS",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
	[SS_FOM_HEALTH]= {
		.sd_name    = "SS_FOM_HEALTH",
		.sd_allowed = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE),
	},
};

M0_INTERNAL struct m0_sm_conf ss_fom_conf = {
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

	M0_ALLOC_PTR(ssfom);
	if (ssfom == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(ssrep_fop);
	if (ssrep_fop == NULL)
		goto err;

	rfop = m0_fop_alloc(&m0_fop_ss_rep_fopt, ssrep_fop,
			    m0_fop_rpc_machine(fop));
	if (rfop == NULL)
		goto err;

	fom = &ssfom->ssf_fom;
	m0_fom_init(fom, &fop->f_type->ft_fom_type,
		    &ss_fom_ops, fop, rfop, reqh);

	ssfom->ssf_magic = M0_SS_FOM_MAGIC;
	*out = fom;
	M0_LOG(M0_DEBUG, "fom %p, ss_fom %p", fom, ssfom);
	return M0_RC(0);
err:
	m0_free(ssrep_fop);
	m0_free(ssfom);
	return M0_ERR(-ENOMEM);
}

static enum m0_service_health ss_svc_health(struct m0_reqh_service *svc)
{
	enum m0_service_health health;

	M0_ENTRY();
	M0_PRE(svc != NULL);

	if(m0_reqh_service_state_get(svc) != M0_RST_STARTED) {
		health = M0_HEALTH_INACTIVE;
		goto exit;
	}
	if (svc->rs_ops->rso_health == NULL) {
		M0_LOG(M0_WARN, "rso_health isn't implemented, service type %s",
		                 svc->rs_type->rst_name);
		health = M0_HEALTH_UNKNOWN;
	} else {
		health = svc->rs_ops->rso_health(svc);
	}
exit:
	M0_LEAVE("health %d", health);
	return health;
}

static int ss_fom_tick__init(struct ss_fom *m, const struct m0_sss_req *fop,
			     const struct m0_reqh *reqh)
{
	static const enum ss_fom_phases next_phase[] = {
		[M0_SERVICE_START]   = SS_FOM_START,
		[M0_SERVICE_STOP]    = SS_FOM_STOP,
		[M0_SERVICE_STATUS]  = SS_FOM_STATUS,
		[M0_SERVICE_HEALTH]  = SS_FOM_HEALTH,
		[M0_SERVICE_QUIESCE] = SS_FOM_QUIESCE,
		[M0_SERVICE_INIT]    = SS_FOM_SVC_INIT
	};

	M0_ENTRY("cmd=%d", fop->ss_cmd);

	if (!IS_IN_ARRAY(fop->ss_cmd, next_phase) ||
	    m0_fid_type_getfid(&fop->ss_id) != &M0_CONF_SERVICE_TYPE.cot_ftype)
		return M0_ERR(-EINVAL);

	/*
	 * TODO: FID can be NULL for services started at mero setup via
	 * '-s' CLI argument. In that case service won't be found by
	 * m0_reqh_service_lookup().
	 *
	 * In future FID for service provided by command-line will be mandatory.
	 */
	m->ssf_svc = m0_reqh_service_lookup(reqh, &fop->ss_id);

	if (m->ssf_svc == NULL && fop->ss_cmd != M0_SERVICE_INIT)
		return M0_ERR(-ENOENT);

	m0_fom_phase_set(&m->ssf_fom, next_phase[fop->ss_cmd]);
	return M0_RC(0);
}

#ifndef __KERNEL__
static int ss_fom_tick__svc_alloc(struct ss_fom           *m,
				  const struct m0_sss_req *fop,
				  struct m0_reqh          *reqh)
{
	int                           rc;
	char                         *name;
	struct m0_reqh_service_type  *stype;
	struct m0_reqh_context *rctx =
		container_of(reqh, struct m0_reqh_context, rc_reqh);

	if (m->ssf_svc != NULL)
		return M0_ERR(-EALREADY);

	name = m0_buf_strdup(&fop->ss_name);
	if (name == NULL)
		return M0_ERR(-ENOMEM);

	stype = m0_reqh_service_type_find(name);
	if (stype == NULL) {
		m0_free(name);
		return M0_ERR(-ENOENT);
	}

	rc = m0_reqh_service_allocate(&m->ssf_svc, stype, rctx);
	if (rc == 0) {
		m0_reqh_service_init(m->ssf_svc, reqh, &fop->ss_id);
		m0_buf_copy(&m->ssf_svc->rs_ss_param, &fop->ss_param);
	}

	m0_free(name);
	return M0_RC(rc);
}
#else
static int ss_fom_tick__svc_alloc(struct ss_fom           *m M0_UNUSED,
				  const struct m0_sss_req *fop M0_UNUSED,
				  struct m0_reqh          *reqh M0_UNUSED)
{
	return M0_RC(0);
}
#endif /* __KERNEL__ */

static int ss_fom_tick__start(struct m0_reqh_service                 *svc,
			      struct m0_fom                          *fom,
			      struct m0_reqh_service_start_async_ctx *ctx)
{
	M0_ENTRY();
	M0_PRE(svc != NULL);

	if (m0_reqh_service_state_get(svc) != M0_RST_INITIALISED)
		return M0_ERR(-EPROTO);

	ctx->sac_service = svc;
	ctx->sac_fom = fom;
	return M0_RC(m0_reqh_service_start_async(ctx));
}

static int ss_fom_tick__stop(struct m0_reqh_service *svc,
		             struct m0_reqh         *reqh,
			     struct m0_fom          *fom)
{
	int rc;

	M0_ENTRY();
	M0_PRE(svc != NULL);
	M0_PRE(reqh != NULL);

	if (m0_reqh_service_state_get(svc) != M0_RST_STOPPING)
		return M0_ERR(-EPROTO);

	if (m0_fom_domain_is_idle_for(svc)) {
		m0_reqh_service_stop(svc);
		m0_reqh_service_fini(svc);
		rc = 0;
	} else {
		m0_sm_group_lock(&reqh->rh_sm_grp);
		m0_fom_wait_on(fom, &reqh->rh_sm_grp.s_chan, &fom->fo_cb);
		m0_sm_group_unlock(&reqh->rh_sm_grp);
		rc = -EBUSY;
	}

	return M0_RC(rc);
}

static int ss_fom_tick__quiesce(struct m0_reqh_service *svc)
{
	M0_PRE(svc != NULL);

	if (m0_reqh_service_state_get(svc) != M0_RST_STARTED)
		return M0_ERR(-EPROTO);
	m0_reqh_service_prepare_to_stop(svc);
	return M0_RC(0);
}

static int ss_fom_tick(struct m0_fom *fom)
{
	struct ss_fom     *m;
	struct m0_reqh    *reqh;
	struct m0_sss_req *fop;
	struct m0_sss_rep *rep;

	M0_ENTRY("fom %p, state %d", fom, m0_fom_phase(fom));
	M0_PRE(fom != NULL);

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);

	m = container_of(fom, struct ss_fom, ssf_fom);
	fop = m0_fop_data(fom->fo_fop);
	rep = m0_fop_data(fom->fo_rep_fop);
	reqh = m0_fom_reqh(fom);

	switch (m0_fom_phase(fom)) {
	case SS_FOM_INIT:
		rep->ssr_rc = ss_fom_tick__init(m, fop, reqh);
		if (rep->ssr_rc != 0)
			m0_fom_phase_move(fom, rep->ssr_rc, M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_SVC_INIT:
		rep->ssr_rc = ss_fom_tick__svc_alloc(m, fop, reqh);

		if (m->ssf_svc != NULL)
			rep->ssr_state = m0_reqh_service_state_get(m->ssf_svc);

		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		M0_LOG(M0_DEBUG, "init: m %p svc %p", m, m->ssf_svc);
		return M0_FSO_AGAIN;

	case SS_FOM_START:
		M0_PRE(m->ssf_svc != NULL);
		M0_LOG(M0_DEBUG, "start: m %p svc %p", m, m->ssf_svc);
		rep->ssr_rc = ss_fom_tick__start(m->ssf_svc, fom, &m->ssf_ctx);
		m0_fom_phase_moveif(fom, rep->ssr_rc, SS_FOM_START_WAIT,
				    M0_FOPH_FAILURE);
		return rep->ssr_rc == 0 ? M0_FSO_WAIT : M0_FSO_AGAIN;

	case SS_FOM_START_WAIT:
		M0_PRE(m->ssf_svc != NULL);
		if (m->ssf_ctx.sac_rc == 0)
			m0_reqh_service_started(m->ssf_svc);
		else
			m0_reqh_service_failed(m->ssf_svc);
		rep->ssr_rc = m->ssf_ctx.sac_rc;
		rep->ssr_state = m0_reqh_service_state_get(m->ssf_svc);
		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_QUIESCE:
		M0_PRE(m->ssf_svc != NULL);
		rep->ssr_rc = ss_fom_tick__quiesce(m->ssf_svc);
		rep->ssr_state = m0_reqh_service_state_get(m->ssf_svc);
		m0_fom_phase_moveif(fom, rep->ssr_rc, M0_FOPH_SUCCESS,
				    M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;

	case SS_FOM_STOP:
		M0_PRE(m->ssf_svc != NULL);
		rep->ssr_rc = ss_fom_tick__stop(m->ssf_svc, reqh, fom);
		if (rep->ssr_rc == -EBUSY)
			return M0_FSO_WAIT;

		M0_ASSERT(rep->ssr_rc == 0);
		rep->ssr_state = M0_RST_STOPPED;
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		return M0_FSO_AGAIN;

	case SS_FOM_STATUS:
		M0_PRE(m->ssf_svc != NULL);
		M0_LOG(M0_DEBUG, "status: m %p svc %p", m, m->ssf_svc);
		rep->ssr_state = m0_reqh_service_state_get(m->ssf_svc);
		rep->ssr_rc = 0;
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		return M0_FSO_AGAIN;

	case SS_FOM_HEALTH:
		M0_PRE(m->ssf_svc != NULL);
		rep->ssr_state = m0_reqh_service_state_get(m->ssf_svc);
		rep->ssr_rc = ss_svc_health(m->ssf_svc);
		M0_ASSERT(rep->ssr_rc >= 0);
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		return M0_FSO_AGAIN;

	default:
		M0_IMPOSSIBLE("Invalid phase");
	}
}

static void ss_fom_fini(struct m0_fom *fom)
{
	struct ss_fom *m = container_of(fom, struct ss_fom, ssf_fom);

	M0_ENTRY();
	M0_LOG(M0_DEBUG, "fom %p ss_fom %p", fom, m);
	m0_fom_fini(fom);
	m0_free(m);

	M0_LEAVE();
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
