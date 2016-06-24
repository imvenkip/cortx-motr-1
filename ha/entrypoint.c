/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 28-Apr-2016
 */


/**
 * @addtogroup ha
 *
 * TODO static struct m0_sm_trans_descr ha_entrypoint_client_state_trans[]
 * TODO transit to a failed state if rc != 0 in ha_entrypoint_client_replied()
 * TODO handle errors
 * TODO make magics for hes_req tlist
 * TODO print the sm .dot representation to some file in UT for all SMs
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/entrypoint.h"
#include "ha/entrypoint_xc.h"

#include "lib/memory.h"         /* m0_free */
#include "lib/time.h"           /* M0_TIME_IMMEDIATELY */
#include "lib/mutex.h"          /* m0_mutex */

#include "fop/fom.h"            /* m0_fom */
#include "fop/fop.h"            /* m0_fop_opcode */
#include "fop/fom_generic.h"    /* m0_rpc_item_generic_reply_rc */
#include "reqh/reqh_service.h"  /* m0_reqh_service */
#include "rpc/rpc.h"            /* m0_rpc_reply_post */
#include "rpc/rpc_opcodes.h"    /* M0_HA_ENTRYPOINT_CLIENT_OPCODE */


struct m0_reqh;

struct ha_entrypoint_service {
	struct m0_reqh_service          hsv_service;
	struct m0_reqh                 *hsv_reqh;
	struct m0_ha_entrypoint_server *hsv_server;
	/* protects hsv_server */
	struct m0_mutex                 hsv_lock;
};

static struct ha_entrypoint_service *
ha_entrypoint_service_container(struct m0_reqh_service *service)
{
	return container_of(service, struct ha_entrypoint_service, hsv_service);
}

static void ha_entrypoint_service_init(struct m0_reqh_service *service)
{
	struct ha_entrypoint_service *he_service;

	he_service = ha_entrypoint_service_container(service);
	M0_ENTRY("service=%p he_service=%p", service, he_service);
	m0_mutex_init(&he_service->hsv_lock);
	M0_LEAVE();
}

static void ha_entrypoint_service_fini(struct m0_reqh_service *service)
{
	struct ha_entrypoint_service *he_service;

	he_service = ha_entrypoint_service_container(service);
	M0_ENTRY("service=%p he_service=%p", service, he_service);
	m0_mutex_fini(&he_service->hsv_lock);
	/* allocated in ha_entrypoint_service_allocate() */
	m0_free(container_of(service, struct ha_entrypoint_service, hsv_service));
	M0_LEAVE();
}

static int ha_entrypoint_service_start(struct m0_reqh_service *service)
{
	M0_ENTRY();
	return M0_RC(0);
}

static void ha_entrypoint_service_stop(struct m0_reqh_service *service)
{
	M0_ENTRY();
	M0_LEAVE();
}

static const struct m0_reqh_service_ops ha_entrypoint_service_ops = {
	.rso_start = ha_entrypoint_service_start,
	.rso_stop  = ha_entrypoint_service_stop,
	.rso_fini  = ha_entrypoint_service_fini,
};

static int
ha_entrypoint_service_allocate(struct m0_reqh_service            **service,
                               const struct m0_reqh_service_type  *stype);

static const struct m0_reqh_service_type_ops ha_entrypoint_stype_ops = {
	.rsto_service_allocate = ha_entrypoint_service_allocate,
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_ha_entrypoint_service_type,
			    &ha_entrypoint_stype_ops,
			    "ha_entrypoint-service",
			    M0_HA_ENTRYPOINT_SVC_LEVEL, 0);

static int
ha_entrypoint_service_allocate(struct m0_reqh_service            **service,
                               const struct m0_reqh_service_type  *stype)
{
	struct ha_entrypoint_service *he_service;

	M0_ENTRY();
	M0_PRE(stype == &m0_ha_entrypoint_service_type);

	M0_ALLOC_PTR(he_service);
	if (he_service == NULL)
		return M0_RC(-ENOMEM);

	ha_entrypoint_service_init(&he_service->hsv_service);
	*service           = &he_service->hsv_service;
	(*service)->rs_ops = &ha_entrypoint_service_ops;

	return M0_RC(0);
}

struct ha_entrypoint_server_fom {
	struct m0_fom               esf_gen;
	struct m0_ha_entrypoint_req esf_req;
	struct m0_uint128           esf_req_id;
	struct m0_tlink             esf_tlink;
	uint64_t                    esf_magic;
};

static struct m0_sm_state_descr ha_entrypoint_server_fom_states[] = {
	[M0_HES_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "M0_HES_INIT",
		.sd_allowed = M0_BITS(M0_HES_REPLY_WAIT),
	},
	[M0_HES_REPLY_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HES_REPLY_WAIT",
		.sd_allowed = M0_BITS(M0_HES_FINI),
	},
	[M0_HES_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "M0_HES_FINI",
		.sd_allowed = 0,
	},
};

struct m0_sm_conf m0_ha_entrypoint_server_fom_states_conf = {
	.scf_name      = "entrypoint server fom",
	.scf_nr_states = ARRAY_SIZE(ha_entrypoint_server_fom_states),
	.scf_state     = ha_entrypoint_server_fom_states,
};

/*
 * TODO Either move the service to another file or make these functions static.
 */
M0_INTERNAL int m0_ha_entrypoint_service_init(struct m0_reqh_service **service,
                                              struct m0_reqh          *reqh)
{
	M0_ENTRY("reqh=%p", reqh);
	return M0_RC(m0_reqh_service_setup(service,
					   &m0_ha_entrypoint_service_type,
	                                   reqh, NULL, NULL));
}

M0_INTERNAL void
m0_ha_entrypoint_service_fini(struct m0_reqh_service *he_service)
{
	M0_ENTRY();
	m0_reqh_service_quit(he_service);
	M0_LEAVE();
}

M0_TL_DESCR_DEFINE(hes_req, "m0_ha_entrypoint_server::hes_requests", static,
		   struct ha_entrypoint_server_fom, esf_tlink, esf_magic,
		   10, 11);
M0_TL_DEFINE(hes_req, static, struct ha_entrypoint_server_fom);

M0_INTERNAL int
m0_ha_entrypoint_server_init(struct m0_ha_entrypoint_server     *hes,
                             struct m0_ha_entrypoint_server_cfg *hes_cfg)
{
	hes->hes_cfg = *hes_cfg;
	hes_req_tlist_init(&hes->hes_requests);
	hes->hes_next_id = (struct m0_uint128){
		.u_hi = 0,
		.u_lo = 1,
	};
	m0_mutex_init(&hes->hes_lock);
	return 0;
}

M0_INTERNAL void
m0_ha_entrypoint_server_fini(struct m0_ha_entrypoint_server *hes)
{
	m0_mutex_fini(&hes->hes_lock);
	hes_req_tlist_fini(&hes->hes_requests);
}

M0_INTERNAL void
m0_ha_entrypoint_server_start(struct m0_ha_entrypoint_server *hes)
{
	struct ha_entrypoint_service *he_service;
	int                           rc;

	M0_ENTRY();
	rc = m0_ha_entrypoint_service_init(&hes->hes_he_service,
	                                   hes->hes_cfg.hesc_reqh);
	M0_ASSERT(rc == 0);
	he_service = ha_entrypoint_service_container(hes->hes_he_service);
	m0_mutex_lock(&he_service->hsv_lock);
	he_service->hsv_server = hes;
	m0_mutex_unlock(&he_service->hsv_lock);
	M0_LEAVE();
}

M0_INTERNAL void
m0_ha_entrypoint_server_stop(struct m0_ha_entrypoint_server *hes)
{
	m0_ha_entrypoint_service_fini(hes->hes_he_service);
}

static struct ha_entrypoint_server_fom *
ha_entrypoint_server_find(struct m0_ha_entrypoint_server *hes,
                          const struct m0_uint128        *req_id)
{
	struct ha_entrypoint_server_fom *server_fom;

	M0_ENTRY("hes=%p req_id="U128X_F, hes, U128_P(req_id));
	m0_mutex_lock(&hes->hes_lock);
	server_fom = m0_tl_find(hes_req, server_fom1, &hes->hes_requests,
	                       m0_uint128_eq(&server_fom1->esf_req_id, req_id));
	m0_mutex_unlock(&hes->hes_lock);
	M0_LEAVE("hes=%p server_fom=%p req_id="U128X_F,
		 hes, server_fom, U128_P(req_id));
	return server_fom;
}

static void
ha_entrypoint_server_register(struct m0_ha_entrypoint_server  *hes,
                              struct ha_entrypoint_server_fom *server_fom)
{
	M0_ENTRY("hes=%p server_fom=%p", hes, server_fom);
	m0_mutex_lock(&hes->hes_lock);
	server_fom->esf_req_id = hes->hes_next_id;
	++hes->hes_next_id.u_lo;
	hes_req_tlist_add_tail(&hes->hes_requests, server_fom);
	m0_mutex_unlock(&hes->hes_lock);
	M0_LEAVE("hes=%p server_fom=%p esf_req_id="U128X_F,
	         hes, server_fom, U128_P(&server_fom->esf_req_id));
}

static void
ha_entrypoint_server_deregister(struct m0_ha_entrypoint_server  *hes,
                                struct ha_entrypoint_server_fom *server_fom)
{
	M0_ENTRY("hes=%p server_fom=%p esf_req_id="U128X_F,
	         hes, server_fom, U128_P(&server_fom->esf_req_id));
	m0_mutex_lock(&hes->hes_lock);
	hes_req_tlist_del(server_fom);
	m0_mutex_unlock(&hes->hes_lock);
	M0_LEAVE("hes=%p server_fom=%p", hes, server_fom);
}

M0_INTERNAL void
m0_ha_entrypoint_server_reply(struct m0_ha_entrypoint_server    *hes,
                              const struct m0_uint128           *req_id,
                              const struct m0_ha_entrypoint_rep *rep)
{
	struct ha_entrypoint_server_fom *server_fom;
	struct m0_fom                   *fom;
	int                              rc;

	server_fom = ha_entrypoint_server_find(hes, req_id);
	fom = &server_fom->esf_gen;
	rc = m0_ha_entrypoint_rep2fop(rep, m0_fop_data(fom->fo_rep_fop));
	M0_ASSERT(rc == 0);
	m0_fom_wakeup(fom);
}

M0_INTERNAL const struct m0_ha_entrypoint_req *
m0_ha_entrypoint_server_request_find(struct m0_ha_entrypoint_server *hes,
                                     const struct m0_uint128        *req_id)
{
	struct ha_entrypoint_server_fom *server_fom;

	server_fom = ha_entrypoint_server_find(hes, req_id);
	return &server_fom->esf_req;
}

static struct m0_sm_state_descr ha_entrypoint_client_states[] = {
	[M0_HEC_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "M0_HEC_INIT",
		.sd_allowed = M0_BITS(M0_HEC_STOPPED),
	},
	[M0_HEC_STOPPED] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HEC_STOPPED",
		.sd_allowed = M0_BITS(M0_HEC_UNAVAILABLE, M0_HEC_FINI),
	},
	[M0_HEC_UNAVAILABLE] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HEC_UNAVAILABLE",
		.sd_allowed = M0_BITS(M0_HEC_FILL, M0_HEC_STOPPED),
	},
	[M0_HEC_FILL] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HEC_FILL",
		.sd_allowed = M0_BITS(M0_HEC_SEND),
	},
	[M0_HEC_SEND] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HEC_SEND",
		.sd_allowed = M0_BITS(M0_HEC_WAIT),
	},
	[M0_HEC_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HEC_WAIT",
		.sd_allowed = M0_BITS(M0_HEC_UNAVAILABLE, M0_HEC_AVAILABLE),
	},
	[M0_HEC_AVAILABLE] = {
		.sd_flags   = 0,
		.sd_name    = "M0_HEC_AVAILABLE",
		.sd_allowed = M0_BITS(M0_HEC_UNAVAILABLE, M0_HEC_STOPPED),
	},
	[M0_HEC_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "M0_HEC_FINI",
		.sd_allowed = 0,
	},
};

static struct m0_sm_conf ha_entrypoint_client_states_conf = {
	.scf_name      = "m0_ha_entrypoint_client::ecl_sm",
	.scf_nr_states = ARRAY_SIZE(ha_entrypoint_client_states),
	.scf_state     = ha_entrypoint_client_states,
};

enum {
	HEC_FOM_INIT = M0_FOM_PHASE_INIT,
	HEC_FOM_FINI = M0_FOM_PHASE_FINISH,
};

static struct m0_sm_state_descr ha_entrypoint_client_fom_states[] = {
	[HEC_FOM_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "HEC_FOM_INIT",
		.sd_allowed = M0_BITS(HEC_FOM_FINI),
	},
	[HEC_FOM_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "HEC_FOM_FINI",
		.sd_allowed = 0,
	},
};

static struct m0_sm_conf ha_entrypoint_client_fom_states_conf = {
	.scf_name      = "entrypoint client fom",
	.scf_nr_states = ARRAY_SIZE(ha_entrypoint_client_fom_states),
	.scf_state     = ha_entrypoint_client_fom_states,
};

M0_INTERNAL int
m0_ha_entrypoint_client_init(struct m0_ha_entrypoint_client     *ecl,
                             struct m0_ha_entrypoint_client_cfg *ecl_cfg)
{
	ecl->ecl_cfg = *ecl_cfg;
	m0_sm_group_init(&ecl->ecl_sm_group);
	m0_sm_init(&ecl->ecl_sm, &ha_entrypoint_client_states_conf,
	           M0_HEC_INIT, &ecl->ecl_sm_group);
	ecl->ecl_reply = NULL;
	M0_SET0(&ecl->ecl_fom);
	M0_SET0(&ecl->ecl_req_fop);
	m0_sm_group_lock(&ecl->ecl_sm_group);
	ecl->ecl_fom_running = false;
	ecl->ecl_stopping    = false;
	m0_sm_state_set(&ecl->ecl_sm, M0_HEC_STOPPED);
	m0_sm_group_unlock(&ecl->ecl_sm_group);
	return 0;
}

M0_INTERNAL void
m0_ha_entrypoint_client_fini(struct m0_ha_entrypoint_client *ecl)
{
	m0_sm_group_lock(&ecl->ecl_sm_group);
	m0_sm_state_set(&ecl->ecl_sm, M0_HEC_FINI);
	m0_sm_fini(&ecl->ecl_sm);
	m0_sm_group_unlock(&ecl->ecl_sm_group);
	m0_sm_group_fini(&ecl->ecl_sm_group);
}

static void ha_entrypoint_client_replied(struct m0_rpc_item *item)
{
	struct m0_fop                  *fop   = m0_rpc_item_to_fop(item);
	struct m0_ha_entrypoint_client *ecl   = fop->f_opaque;
	struct m0_rpc_item             *reply = item->ri_reply;
	int                             rc;

	M0_ENTRY();
	M0_PRE(ecl->ecl_reply == NULL);

	rc = item->ri_error ?: m0_rpc_item_generic_reply_rc(reply);
	if (rc == 0) {
		m0_rpc_item_get(reply);
		ecl->ecl_reply = reply;
	}
	m0_fom_wakeup(&ecl->ecl_fom);
	m0_rpc_item_put(item);

	M0_LEAVE("rc=%d", rc);
}

static struct m0_rpc_item_ops ha_entrypoint_client_item_ops = {
	.rio_replied = ha_entrypoint_client_replied,
};

static void ha_entrypoint_client_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop;

	M0_ENTRY();
	M0_PRE(ref != NULL);
	fop = container_of(ref, struct m0_fop, f_ref);
	fop->f_data.fd_data = NULL;
	m0_fop_fini(fop);
	M0_SET0(fop);
	M0_LEAVE();
}

static int ha_entrypoint_client_fom_tick(struct m0_fom *fom)
{
	enum m0_ha_entrypoint_client_state  state;
	enum m0_ha_entrypoint_client_state  next_state;
	struct m0_ha_entrypoint_req_fop    *req_fop_data;
	struct m0_rpc_item                 *item;
	struct m0_fop                      *fop;
	bool                                stopping;
	int                                 rc = 0;

	struct m0_ha_entrypoint_client     *ecl =
		container_of(fom, struct m0_ha_entrypoint_client, ecl_fom);

	M0_ENTRY();

	m0_sm_group_lock(&ecl->ecl_sm_group);
	state    = ecl->ecl_sm.sm_state;
	stopping = ecl->ecl_stopping;
	m0_sm_group_unlock(&ecl->ecl_sm_group);
	next_state = state;
	M0_LOG(M0_DEBUG, "state=%s", m0_sm_state_name(&ecl->ecl_sm, state));

	switch (state) {
	case M0_HEC_AVAILABLE:
		next_state = M0_HEC_UNAVAILABLE;
		rc = M0_FSO_AGAIN;
		break;

	case M0_HEC_UNAVAILABLE:
		if (stopping) {
			m0_fom_phase_set(fom, HEC_FOM_FINI);
			return M0_RC(M0_FSO_WAIT);
		}
		next_state = M0_HEC_FILL;
		rc = M0_FSO_AGAIN;
		break;

	case M0_HEC_FILL:
		/*
		 * TODO XXX make M0_HEC_FILL state asynchronous - user should
		 * call some function before client transitions to the WAIT
		 * state. It would be similar to
		 * m0_ha_entrypoint_server_reply().
		 */

		ecl->ecl_req.heq_process_fid = ecl->ecl_cfg.hecc_process_fid;
		ecl->ecl_req.heq_profile_fid = ecl->ecl_cfg.hecc_profile_fid;
		next_state = M0_HEC_SEND;
		rc = M0_FSO_AGAIN;
		break;

	case M0_HEC_SEND:
		req_fop_data = &ecl->ecl_req_fop_data;
		M0_SET0(req_fop_data);
		rc = m0_ha_entrypoint_req2fop(&ecl->ecl_req, req_fop_data);
		M0_ASSERT(rc == 0);
		fop = &ecl->ecl_req_fop;
		M0_ASSERT_EX(M0_IS0(fop));
		m0_fop_init(fop, &m0_ha_entrypoint_req_fopt, NULL,
			    &ha_entrypoint_client_fop_release);
		fop->f_data.fd_data = req_fop_data;
		item = &fop->f_item;
		item->ri_rmachine = m0_fop_session_machine(ecl->ecl_cfg.hecc_session),
		item->ri_ops      = &ha_entrypoint_client_item_ops;
		item->ri_session  = ecl->ecl_cfg.hecc_session;
		item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
		item->ri_deadline = M0_TIME_IMMEDIATELY;
		fop->f_opaque = ecl;
		rc = m0_rpc_post(item);

		next_state = M0_HEC_WAIT;
		rc = rc == 0 ? M0_FSO_WAIT : M0_FSO_AGAIN;
		break;

	case M0_HEC_WAIT:
		item = ecl->ecl_reply;
		if (item == NULL) {
			M0_LOG(M0_DEBUG, "RPC error occured, resending fop");
			next_state = M0_HEC_UNAVAILABLE;
			rc = M0_FSO_AGAIN;
			break;
		}

		M0_ASSERT(m0_rpc_item_generic_reply_rc(item) == 0);
		m0_ha_entrypoint_rep_free(&ecl->ecl_rep);
		rc = m0_ha_entrypoint_fop2rep(
				m0_fop_data(m0_rpc_item_to_fop(item)),
				&ecl->ecl_rep);
		M0_ASSERT_INFO(rc == 0, "rc=%d", rc);
		m0_rpc_item_put_lock(item);
		ecl->ecl_reply = NULL;
		m0_fom_phase_set(fom, HEC_FOM_FINI);
		/* transit to M0_HEC_AVAILABLE in the fom fini callback */
		return M0_RC(M0_FSO_WAIT);

	default:
		M0_IMPOSSIBLE("Unexpected state");
	}

	M0_LOG(M0_DEBUG, "%s -> %s",
	       m0_sm_state_name(&ecl->ecl_sm, state),
	       m0_sm_state_name(&ecl->ecl_sm, next_state));
	m0_sm_group_lock(&ecl->ecl_sm_group);
	M0_ASSERT(ecl->ecl_sm.sm_state == state);
	M0_ASSERT(state != next_state);
	m0_sm_state_set(&ecl->ecl_sm, next_state);
	m0_sm_group_unlock(&ecl->ecl_sm_group);

	M0_POST(M0_IN(rc, (M0_FSO_AGAIN, M0_FSO_WAIT)));

	return M0_RC(rc);
}

static void ha_entrypoint_client_fom_fini(struct m0_fom *fom)
{
	enum m0_ha_entrypoint_client_state  state;
	struct m0_ha_entrypoint_client     *ecl =
		container_of(fom, struct m0_ha_entrypoint_client, ecl_fom);

	M0_ENTRY();

	m0_fom_fini(fom);
	M0_SET0(fom);
	m0_sm_group_lock(&ecl->ecl_sm_group);
	ecl->ecl_fom_running = false;
	state = ecl->ecl_sm.sm_state;
	M0_ASSERT(M0_IN(state, (M0_HEC_WAIT, M0_HEC_UNAVAILABLE)));
	if (state == M0_HEC_WAIT) {
		M0_LOG(M0_DEBUG, "M0_HEC_WAIT -> M0_HEC_AVAILABLE");
		m0_sm_state_set(&ecl->ecl_sm, M0_HEC_AVAILABLE);
	}
	m0_sm_group_unlock(&ecl->ecl_sm_group);

	M0_LEAVE();
}

static size_t ha_entrypoint_client_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

static struct m0_fom_type ha_entrypoint_client_fom_type;

static const struct m0_fom_type_ops ha_entrypoint_client_fom_type_ops = {
	.fto_create = NULL,
};

static const struct m0_fom_ops ha_entrypoint_client_fom_ops = {
	.fo_fini          = ha_entrypoint_client_fom_fini,
	.fo_tick          = ha_entrypoint_client_fom_tick,
	.fo_home_locality = ha_entrypoint_client_fom_locality,
};

M0_INTERNAL void
m0_ha_entrypoint_client_request(struct m0_ha_entrypoint_client *ecl)
{
	M0_ENTRY();

	m0_sm_group_lock(&ecl->ecl_sm_group);
	if (!ecl->ecl_fom_running) {
		M0_ASSERT(M0_IN(ecl->ecl_sm.sm_state, (M0_HEC_AVAILABLE,
						       M0_HEC_UNAVAILABLE)));
		ecl->ecl_fom_running = true;
		M0_ASSERT_EX(M0_IS0(&ecl->ecl_fom));
		m0_fom_init(&ecl->ecl_fom, &ha_entrypoint_client_fom_type,
			    &ha_entrypoint_client_fom_ops, NULL, NULL,
			    ecl->ecl_cfg.hecc_reqh);
		m0_fom_queue(&ecl->ecl_fom);
	}
	m0_sm_group_unlock(&ecl->ecl_sm_group);

	M0_LEAVE();
}

M0_INTERNAL void
m0_ha_entrypoint_client_start(struct m0_ha_entrypoint_client *ecl)
{
	M0_ENTRY();
	m0_sm_group_lock(&ecl->ecl_sm_group);
	ecl->ecl_stopping = false;
	m0_sm_state_set(&ecl->ecl_sm, M0_HEC_UNAVAILABLE);
	m0_sm_group_unlock(&ecl->ecl_sm_group);
	m0_ha_entrypoint_client_request(ecl);
	M0_LEAVE();
}

static bool ha_entrypoint_client_start_check(struct m0_clink *clink)
{
	struct m0_ha_entrypoint_client *ecl =
		container_of(clink, struct m0_ha_entrypoint_client, ecl_clink);

	M0_PRE(m0_sm_group_is_locked(&ecl->ecl_sm_group));
	M0_ENTRY("state=%s fom_running=%d",
		 m0_sm_state_name(&ecl->ecl_sm, ecl->ecl_sm.sm_state),
		 !!ecl->ecl_fom_running);

	if (ecl->ecl_sm.sm_state == M0_HEC_AVAILABLE) {
		/* let m0_chan_wait wake up */
		return false;
	}
	return true;
}

static bool ha_entrypoint_client_stop_check(struct m0_clink *clink)
{
	struct m0_ha_entrypoint_client *ecl =
		container_of(clink, struct m0_ha_entrypoint_client, ecl_clink);

	M0_PRE(m0_sm_group_is_locked(&ecl->ecl_sm_group));
	M0_ENTRY("state=%s fom_running=%d",
		 m0_sm_state_name(&ecl->ecl_sm, ecl->ecl_sm.sm_state),
		 !!ecl->ecl_fom_running);

	return ecl->ecl_fom_running;
}

M0_INTERNAL void
m0_ha_entrypoint_client_start_sync(struct m0_ha_entrypoint_client *ecl)
{
	M0_ENTRY();
	m0_clink_init(&ecl->ecl_clink, &ha_entrypoint_client_start_check);
	m0_clink_add_lock(m0_ha_entrypoint_client_chan(ecl), &ecl->ecl_clink);
	m0_ha_entrypoint_client_start(ecl);
	m0_chan_wait(&ecl->ecl_clink);
	m0_clink_del_lock(&ecl->ecl_clink);
	m0_clink_fini(&ecl->ecl_clink);
	M0_LEAVE();
}

M0_INTERNAL void
m0_ha_entrypoint_client_stop(struct m0_ha_entrypoint_client *ecl)
{
	bool fom_running;

	M0_ENTRY();

	/* wait for fom */
	m0_sm_group_lock(&ecl->ecl_sm_group);
	ecl->ecl_stopping = true;
	fom_running = ecl->ecl_fom_running;
	if (fom_running) {
		m0_clink_init(&ecl->ecl_clink,
			      &ha_entrypoint_client_stop_check);
		m0_clink_add(m0_ha_entrypoint_client_chan(ecl),
			     &ecl->ecl_clink);
	}
	m0_sm_group_unlock(&ecl->ecl_sm_group);
	if (fom_running) {
		m0_chan_wait(&ecl->ecl_clink);
		m0_clink_del_lock(&ecl->ecl_clink);
		m0_clink_fini(&ecl->ecl_clink);
	}

	m0_sm_group_lock(&ecl->ecl_sm_group);
	m0_sm_state_set(&ecl->ecl_sm, M0_HEC_STOPPED);
	M0_ASSERT(!ecl->ecl_fom_running);
	m0_sm_group_unlock(&ecl->ecl_sm_group);
	if (ecl->ecl_reply != NULL)
		m0_rpc_item_put_lock(ecl->ecl_reply);
	ecl->ecl_reply = NULL;
	m0_ha_entrypoint_rep_free(&ecl->ecl_rep);

	M0_LEAVE();
}

M0_INTERNAL struct m0_chan *
m0_ha_entrypoint_client_chan(struct m0_ha_entrypoint_client *ecl)
{
	return &ecl->ecl_sm.sm_chan;
}

M0_INTERNAL enum m0_ha_entrypoint_client_state
m0_ha_entrypoint_client_state_get(struct m0_ha_entrypoint_client *ecl)
{
	M0_PRE(m0_sm_group_is_locked(&ecl->ecl_sm_group));
	return ecl->ecl_sm.sm_state;
}

M0_INTERNAL int m0_ha_entrypoint_mod_init(void)
{
	int rc;

	rc = m0_reqh_service_type_register(&m0_ha_entrypoint_service_type);
	M0_ASSERT(rc == 0);
	m0_ha_entrypoint_fops_init();
	m0_fom_type_init(&ha_entrypoint_client_fom_type,
			 M0_HA_ENTRYPOINT_CLIENT_OPCODE,
			 &ha_entrypoint_client_fom_type_ops,
			 &m0_ha_entrypoint_service_type,
			 &ha_entrypoint_client_fom_states_conf);
	return 0;
}

M0_INTERNAL void m0_ha_entrypoint_mod_fini(void)
{
	m0_ha_entrypoint_fops_fini();
	m0_reqh_service_type_unregister(&m0_ha_entrypoint_service_type);
}

static size_t ha_entrypoint_home_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

static void ha_entrypoint_fom_fini(struct m0_fom *fom)
{
	struct ha_entrypoint_server_fom *server_fom;

	server_fom = container_of(fom, struct ha_entrypoint_server_fom,
				  esf_gen);
	hes_req_tlink_fini(server_fom);
	m0_fom_fini(fom);
	m0_free(server_fom);
}

static int ha_entrypoint_get_fom_tick(struct m0_fom *fom)
{
	struct ha_entrypoint_server_fom *server_fom;
	struct m0_ha_entrypoint_server  *hes;
	struct ha_entrypoint_service    *he_service;
	struct m0_ha_entrypoint_rep_fop *rep_data;
	int                              rc;

	he_service = ha_entrypoint_service_container(fom->fo_service);
	M0_ENTRY("fom=%p he_service=%p", fom, he_service);

	m0_mutex_lock(&he_service->hsv_lock);
	hes = he_service->hsv_server;
	m0_mutex_unlock(&he_service->hsv_lock);

	M0_ASSERT(hes != NULL); /* XXX handle it, reply -EBUSY */
	M0_ASSERT(M0_IN(m0_fom_phase(fom), (M0_HES_INIT, M0_HES_REPLY_WAIT)));

	server_fom = container_of(fom, struct ha_entrypoint_server_fom,
				  esf_gen);
	if (m0_fom_phase(fom) == M0_HES_INIT) {
		m0_fom_phase_move(fom, 0, M0_HES_REPLY_WAIT);

		rc = m0_ha_entrypoint_fop2req(m0_fop_data(fom->fo_fop),
			      m0_rpc_item_remote_ep_addr(&fom->fo_fop->f_item),
					      &server_fom->esf_req);
		M0_ASSERT(rc == 0);
		ha_entrypoint_server_register(hes, server_fom);
		hes->hes_cfg.hesc_request_received(hes, &server_fom->esf_req,
		                                   &server_fom->esf_req_id);
		rc = 0;
		return M0_FSO_WAIT;
	} else {
		ha_entrypoint_server_deregister(hes, server_fom);
		m0_ha_entrypoint_req_free(&server_fom->esf_req);
		rc = 0;
		rep_data = m0_fop_data(fom->fo_rep_fop);
		rep_data->hbp_rc = rc;
		m0_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
		m0_fom_phase_set(fom, M0_HES_FINI);
	}
	M0_LEAVE("rc=%d", rc);
	return M0_FSO_WAIT;
}

const struct m0_fom_ops ha_entrypoint_get_fom_ops = {
	.fo_tick          = ha_entrypoint_get_fom_tick,
	.fo_fini          = ha_entrypoint_fom_fini,
	.fo_home_locality = ha_entrypoint_home_locality,
};

static int ha_entrypoint_fom_create(struct m0_fop   *fop,
				    struct m0_fom  **m,
				    struct m0_reqh  *reqh)
{
	struct ha_entrypoint_server_fom *server_fom;
	struct m0_ha_entrypoint_rep_fop *reply;
	struct m0_fom                   *fom;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(server_fom);
	if (server_fom == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(reply);
	if (reply == NULL){
		m0_free(server_fom);
		return M0_ERR(-ENOMEM);
	}

	fom = &server_fom->esf_gen;
	fom->fo_rep_fop = m0_fop_alloc(&m0_ha_entrypoint_rep_fopt, reply,
				       m0_fop_rpc_machine(fop));
	if (fom->fo_rep_fop == NULL) {
		m0_free(reply);
		m0_free(server_fom);
		return M0_ERR(-ENOMEM);
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ha_entrypoint_get_fom_ops,
		    fop, fom->fo_rep_fop, reqh);

	hes_req_tlink_init(server_fom);

	*m = &server_fom->esf_gen;
	return M0_RC(0);
}

const struct m0_fom_type_ops m0_ha_entrypoint_fom_type_ops = {
	.fto_create = &ha_entrypoint_fom_create,
};

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
