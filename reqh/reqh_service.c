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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"

#include "lib/rwlock.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/misc.h"    /* M0_SET_ARR0 */
#include "lib/finject.h" /* M0_FI_ENABLED */
#include "lib/lockers.h"
#include "fop/fom.h"
#include "rpc/conn.h"
#include "rpc/rpclib.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "mero/magic.h"
#include "conf/objs/common.h" /* m0_conf_obj_find */
#include "conf/helpers.h"     /* m0_conf_obj2reqh */
#include "pool/pool.h"        /* m0_pools_common_service_ctx_find */

/**
   @addtogroup reqhservice
   @{
 */

/**
   static global list of service types.
   Holds struct m0_reqh_service_type instances linked via
   m0_reqh_service_type::rst_linkage.

   @see struct m0_reqh_service_type
 */
static struct m0_tl rstypes;

enum {
	M0_REQH_SVC_RPC_SERVICE_TYPE,
	REQH_SVC_MAX_RPCS_IN_FLIGHT = 100
};

/** Protects access to list rstypes. */
static struct m0_rwlock rstypes_rwlock;

M0_TL_DESCR_DEFINE(rstypes, "reqh service types", static,
		   struct m0_reqh_service_type, rst_linkage, rst_magix,
		   M0_REQH_SVC_TYPE_MAGIC, M0_REQH_SVC_HEAD_MAGIC);

M0_TL_DEFINE(rstypes, static, struct m0_reqh_service_type);

static struct m0_bob_type rstypes_bob;
M0_BOB_DEFINE(static, &rstypes_bob, m0_reqh_service_type);

static const struct m0_bob_type reqh_svc_ctx = {
	.bt_name         = "m0_reqh_service_ctx",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_reqh_service_ctx,
					   sc_magic),
	.bt_magix        = M0_REQH_SVC_CTX_MAGIC,
	.bt_check        = NULL
};
M0_BOB_DEFINE(static, &reqh_svc_ctx, m0_reqh_service_ctx);

static struct m0_sm_state_descr service_states[] = {
	[M0_RST_INITIALISING] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Initializing",
		.sd_allowed   = M0_BITS(M0_RST_INITIALISED)
	},
	[M0_RST_INITIALISED] = {
		.sd_name      = "Initialized",
		.sd_allowed   = M0_BITS(M0_RST_STARTING, M0_RST_FAILED)
	},
	[M0_RST_STARTING] = {
		.sd_name      = "Starting",
		.sd_allowed   = M0_BITS(M0_RST_STARTED, M0_RST_FAILED)
	},
	[M0_RST_STARTED] = {
		.sd_name      = "Started",
		.sd_allowed   = M0_BITS(M0_RST_STOPPING)
	},
	[M0_RST_STOPPING] = {
		.sd_name      = "Stopping",
		.sd_allowed   = M0_BITS(M0_RST_STOPPED)
	},
	[M0_RST_STOPPED] = {
		.sd_flags     = M0_SDF_FINAL,
		.sd_name      = "Stopped",
		.sd_allowed   = M0_BITS(M0_RST_STARTING)
	},
	[M0_RST_FAILED] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Failed",
	},
};

const struct m0_sm_conf service_states_conf = {
	.scf_name      = "Service states",
	.scf_nr_states = ARRAY_SIZE(service_states),
	.scf_state     = service_states
};

M0_INTERNAL bool m0_reqh_service_invariant(const struct m0_reqh_service *svc)
{
	return _0C(m0_reqh_service_bob_check(svc)) &&
	_0C(M0_IN(svc->rs_sm.sm_state, (M0_RST_INITIALISING, M0_RST_INITIALISED,
				        M0_RST_STARTING, M0_RST_STARTED,
				        M0_RST_STOPPING, M0_RST_STOPPED,
				        M0_RST_FAILED))) &&
	_0C(svc->rs_type != NULL && svc->rs_ops != NULL &&
		(svc->rs_ops->rso_start_async != NULL ||
		 svc->rs_ops->rso_start != NULL)) &&
	_0C(ergo(M0_IN(svc->rs_sm.sm_state, (M0_RST_INITIALISED,
		       M0_RST_STARTING, M0_RST_STARTED, M0_RST_STOPPING,
		       M0_RST_STOPPED, M0_RST_FAILED)),
	     svc->rs_reqh != NULL)) &&
	_0C(ergo(M0_IN(svc->rs_sm.sm_state, (M0_RST_STARTED, M0_RST_STOPPING,
					 M0_RST_STOPPED, M0_RST_FAILED)),
	     m0_reqh_svc_tlist_contains(&svc->rs_reqh->rh_services, svc))) &&
	_0C(ergo(svc->rs_reqh != NULL,
	     M0_IN(m0_reqh_lockers_get(svc->rs_reqh, svc->rs_type->rst_key),
		   (NULL, svc)))) &&
	_0C(svc->rs_level > M0_RS_LEVEL_UNKNOWN);
}
M0_EXPORTED(m0_reqh_service_invariant);

M0_INTERNAL struct m0_reqh_service_type *
m0_reqh_service_type_find(const char *sname)
{
	struct m0_reqh_service_type *t;

	M0_PRE(sname != NULL);

	m0_rwlock_read_lock(&rstypes_rwlock);

	t = m0_tl_find(rstypes, t, &rstypes, strcmp(t->rst_name, sname) == 0);
	if (t != NULL)
		M0_ASSERT(m0_reqh_service_type_bob_check(t));

	m0_rwlock_read_unlock(&rstypes_rwlock);
	return t;
}

M0_INTERNAL int
m0_reqh_service_allocate(struct m0_reqh_service **out,
			 const struct m0_reqh_service_type *stype,
			 struct m0_reqh_context *rctx)
{
	int rc;

	M0_ENTRY();
	M0_PRE(out != NULL && stype != NULL);

	rc = stype->rst_ops->rsto_service_allocate(out, stype);
	if (rc == 0) {
		struct m0_reqh_service *service = *out;
		service->rs_type = stype;
		service->rs_reqh_ctx = rctx;
		m0_reqh_service_bob_init(service);
		if (service->rs_level == M0_RS_LEVEL_UNKNOWN)
			service->rs_level = stype->rst_level;
		M0_POST(m0_reqh_service_invariant(service));
	}
	return M0_RC(rc);
}

static void reqh_service_state_set(struct m0_reqh_service *service,
				   enum m0_reqh_service_state state)
{
	m0_sm_group_lock(&service->rs_reqh->rh_sm_grp);
	m0_sm_state_set(&service->rs_sm, state);
	m0_sm_group_unlock(&service->rs_reqh->rh_sm_grp);
}

static void reqh_service_starting_common(struct m0_reqh *reqh,
					 struct m0_reqh_service *service,
					 unsigned key)
{
	reqh_service_state_set(service, M0_RST_STARTING);

	/*
	 * NOTE: The key is required to be set before 'rso_start'
	 * as some services can call m0_fom_init() directly in
	 * their service start, m0_fom_init() finds the service
	 * given reqh, using this key
	 */
	M0_ASSERT(m0_reqh_lockers_is_empty(reqh, key));
	m0_reqh_lockers_set(reqh, key, service);
	M0_LOG(M0_DEBUG, "key init for reqh=%p, key=%d", reqh, key);
}

static void reqh_service_failed_common(struct m0_reqh *reqh,
				       struct m0_reqh_service *service,
				       unsigned key)
{
	if (!m0_reqh_lockers_is_empty(reqh, key))
		m0_reqh_lockers_clear(reqh, key);
	reqh_service_state_set(service, M0_RST_FAILED);
}

M0_INTERNAL int
m0_reqh_service_start_async(struct m0_reqh_service_start_async_ctx *asc)
{
	int                     rc;
	unsigned                key;
	struct m0_reqh         *reqh;
	struct m0_reqh_service *service;

	M0_PRE(asc != NULL && asc->sac_service != NULL && asc->sac_fom != NULL);
	service = asc->sac_service;
	M0_PRE(m0_reqh_service_bob_check(service));
	reqh = service->rs_reqh;
	key = service->rs_type->rst_key;

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_service_invariant(service));
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_INITIALISED);
	M0_PRE(service->rs_ops->rso_start_async != NULL);
	reqh_service_starting_common(reqh, service, key);
	M0_POST(m0_reqh_service_invariant(service));
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	rc = service->rs_ops->rso_start_async(asc);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	if (rc == 0)
		M0_POST(m0_reqh_service_invariant(service));
	else
		reqh_service_failed_common(reqh, service, key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	return M0_RC(rc);
}

static void reqh_service_started_common(struct m0_reqh *reqh,
					struct m0_reqh_service *service)
{
	reqh_service_state_set(service, M0_RST_STARTED);
}

M0_INTERNAL void m0_reqh_service_started(struct m0_reqh_service *service)
{
	struct m0_reqh *reqh;

	M0_PRE(m0_reqh_service_bob_check(service));
	reqh = service->rs_reqh;

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_service_invariant(service));
	M0_PRE(m0_reqh_service_state_get(service) == M0_RST_STARTING);
	reqh_service_started_common(reqh, service);
	M0_POST(m0_reqh_service_invariant(service));
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_service_failed(struct m0_reqh_service *service)
{
	unsigned        key;
	struct m0_reqh *reqh;

	M0_PRE(m0_reqh_service_bob_check(service));
	reqh = service->rs_reqh;
	key = service->rs_type->rst_key;

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_service_invariant(service));
	M0_ASSERT(M0_IN(m0_reqh_service_state_get(service),
			(M0_RST_STARTING, M0_RST_INITIALISED)));
	reqh_service_failed_common(reqh, service, key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL int m0_reqh_service_start(struct m0_reqh_service *service)
{
	int             rc;
	unsigned        key;
	struct m0_reqh *reqh;

	M0_PRE(m0_reqh_service_bob_check(service));
	reqh = service->rs_reqh;
	key = service->rs_type->rst_key;

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_service_invariant(service));
	M0_PRE(service->rs_ops->rso_start != NULL);
	reqh_service_starting_common(reqh, service, key);
	M0_POST(m0_reqh_service_invariant(service));
	M0_POST(m0_reqh_lockers_get(reqh, key) == service);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	rc = service->rs_ops->rso_start(service);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	if (rc == 0)
		reqh_service_started_common(reqh, service);
	else
		reqh_service_failed_common(reqh, service, key);
	M0_POST(ergo(rc == 0, m0_reqh_service_invariant(service)));
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	return M0_RC(rc);
}

M0_INTERNAL void
m0_reqh_service_prepare_to_stop(struct m0_reqh_service *service)
{
	struct m0_reqh *reqh;
	bool            run_method = false;

	M0_PRE(m0_reqh_service_bob_check(service));
	reqh = service->rs_reqh;

	M0_LOG(M0_DEBUG, "Preparing to stop %s [%d] (%d)",
	       service->rs_type->rst_name,
	       service->rs_level, service->rs_sm.sm_state);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_PRE(m0_reqh_service_invariant(service));
	M0_ASSERT(M0_IN(service->rs_sm.sm_state, (M0_RST_STARTED,
						  M0_RST_STOPPING)));
	if (service->rs_sm.sm_state == M0_RST_STARTED) {
		reqh_service_state_set(service, M0_RST_STOPPING);
		M0_ASSERT(m0_reqh_service_invariant(service));
		run_method = true;
	}
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	if (run_method && service->rs_ops->rso_prepare_to_stop != NULL)
		service->rs_ops->rso_prepare_to_stop(service);
}

M0_INTERNAL void m0_reqh_service_stop(struct m0_reqh_service *service)
{
	struct m0_reqh *reqh;
	unsigned        key;

	M0_PRE(m0_reqh_service_bob_check(service));
	M0_PRE(m0_fom_domain_is_idle_for(service));
	reqh = service->rs_reqh;
	key = service->rs_type->rst_key;

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	M0_ASSERT(m0_reqh_service_invariant(service));
	M0_ASSERT(service->rs_sm.sm_state == M0_RST_STOPPING);
	reqh_service_state_set(service, M0_RST_STOPPED);
	M0_ASSERT(m0_reqh_service_invariant(service));
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	service->rs_ops->rso_stop(service);
	/*
	 * Wait again, in case ->rso_stop() launched more foms. E.g., rpcservice
	 * starts reverse connection disconnection at this point.
	 */
	m0_reqh_idle_wait_for(reqh, service);
	m0_reqh_lockers_clear(reqh, key);
}

M0_INTERNAL void m0_reqh_service_init(struct m0_reqh_service *service,
				      struct m0_reqh         *reqh,
				      const struct m0_fid    *fid)
{
	M0_PRE(service != NULL && reqh != NULL &&
		service->rs_sm.sm_state == M0_RST_INITIALISING);
	/* Currently fid may be NULL */
	M0_PRE(fid == NULL || m0_fid_is_valid(fid));

	m0_sm_init(&service->rs_sm, &service_states_conf, M0_RST_INITIALISING,
		   &reqh->rh_sm_grp);

	if (fid != NULL)
		service->rs_service_fid = *fid;
	service->rs_reqh = reqh;
	m0_mutex_init(&service->rs_mutex);
	reqh_service_state_set(service, M0_RST_INITIALISED);

	/*
	 * We want to track these services externally so add them to the list
	 * just as soon as they enter the M0_RST_INITIALISED state.
	 * They will be left on the list until they get fini'd.
	 */
	m0_reqh_svc_tlink_init_at(service, &reqh->rh_services);
	service->rs_fom_key = m0_locality_lockers_allot();
	M0_POST(!m0_buf_is_set(&service->rs_ss_param));
	M0_POST(m0_reqh_service_invariant(service));
}

M0_INTERNAL void m0_reqh_service_fini(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL && m0_reqh_service_bob_check(service));

	M0_ASSERT(m0_fom_domain_is_idle_for(service));
	m0_locality_lockers_free(service->rs_fom_key);
	m0_reqh_svc_tlink_del_fini(service);
	m0_reqh_service_bob_fini(service);
	m0_sm_group_lock(&service->rs_reqh->rh_sm_grp);
	m0_sm_fini(&service->rs_sm);
	m0_sm_group_unlock(&service->rs_reqh->rh_sm_grp);
	m0_mutex_fini(&service->rs_mutex);
	m0_buf_free(&service->rs_ss_param);
	service->rs_ops->rso_fini(service);
}

int m0_reqh_service_type_register(struct m0_reqh_service_type *rstype)
{
	M0_PRE(rstype != NULL);
	M0_PRE(!m0_reqh_service_is_registered(rstype->rst_name));

	if (M0_FI_ENABLED("fake_error"))
		return M0_ERR(-EINVAL);

	m0_reqh_service_type_bob_init(rstype);
	m0_rwlock_write_lock(&rstypes_rwlock);
	rstype->rst_key = m0_reqh_lockers_allot();
	rstypes_tlink_init_at_tail(rstype, &rstypes);
	m0_rwlock_write_unlock(&rstypes_rwlock);

	return 0;
}

void m0_reqh_service_type_unregister(struct m0_reqh_service_type *rstype)
{
	M0_PRE(rstype != NULL && m0_reqh_service_type_bob_check(rstype));

	rstypes_tlink_del_fini(rstype);
	m0_reqh_lockers_free(rstype->rst_key);
	m0_reqh_service_type_bob_fini(rstype);
}

M0_INTERNAL int m0_reqh_service_types_length(void)
{
	return rstypes_tlist_length(&rstypes);
}

M0_INTERNAL void m0_reqh_service_list_print(void)
{
	struct m0_reqh_service_type *stype;

	m0_tl_for(rstypes, &rstypes, stype) {
		M0_ASSERT(m0_reqh_service_type_bob_check(stype));
		m0_console_printf(" %s\n", stype->rst_name);
	} m0_tl_endfor;
}

M0_INTERNAL bool m0_reqh_service_is_registered(const char *sname)
{
	return m0_tl_exists(rstypes, stype, &rstypes,
			    m0_strcaseeq(stype->rst_name, sname));
}

M0_INTERNAL int m0_reqh_service_types_init(void)
{
	rstypes_tlist_init(&rstypes);
	m0_bob_type_tlist_init(&rstypes_bob, &rstypes_tl);
	m0_rwlock_init(&rstypes_rwlock);

	return 0;
}
M0_EXPORTED(m0_reqh_service_types_init);

M0_INTERNAL void m0_reqh_service_types_fini(void)
{
	rstypes_tlist_fini(&rstypes);
	m0_rwlock_fini(&rstypes_rwlock);
}
M0_EXPORTED(m0_reqh_service_types_fini);

M0_INTERNAL struct m0_reqh_service *
m0_reqh_service_find(const struct m0_reqh_service_type *st,
		     const struct m0_reqh              *reqh)
{
	struct m0_reqh_service *service;

	M0_PRE(st != NULL && reqh != NULL);
	service = m0_reqh_lockers_get(reqh, st->rst_key);
	M0_POST(ergo(service != NULL, service->rs_type == st));
	return service;
}
M0_EXPORTED(m0_reqh_service_find);

M0_INTERNAL struct m0_reqh_service *
m0_reqh_service_lookup(const struct m0_reqh *reqh, const struct m0_fid *fid)
{
	M0_PRE(reqh != NULL);
	M0_PRE(fid != NULL);

	return m0_tl_find(m0_reqh_svc, s, &reqh->rh_services,
			  m0_fid_eq(fid, &s->rs_service_fid));
}

M0_INTERNAL int m0_reqh_service_state_get(const struct m0_reqh_service *s)
{
	return s->rs_sm.sm_state;
}

M0_INTERNAL int m0_reqh_service_setup(struct m0_reqh_service     **out,
				      struct m0_reqh_service_type *stype,
				      struct m0_reqh              *reqh,
				      struct m0_reqh_context      *rctx,
				      const struct m0_fid         *fid)
{
	int result;

	M0_PRE(m0_reqh_service_find(stype, reqh) == NULL);
	M0_ENTRY();

	result = m0_reqh_service_allocate(out, stype, rctx);
	if (result == 0) {
		struct m0_reqh_service *svc = *out;

		m0_reqh_service_init(svc, reqh, fid);
		result = m0_reqh_service_start(svc);
		if (result != 0)
			m0_reqh_service_fini(svc);
	}
	return M0_RC(result);
}

M0_INTERNAL void m0_reqh_service_quit(struct m0_reqh_service *svc)
{
	if (svc != NULL && svc->rs_sm.sm_state == M0_RST_STARTED) {
		M0_ASSERT(m0_reqh_service_find(svc->rs_type,
					       svc->rs_reqh) == svc);
		m0_reqh_service_prepare_to_stop(svc);
		m0_reqh_idle_wait_for(svc->rs_reqh, svc);
		m0_reqh_service_stop(svc);
		m0_reqh_service_fini(svc);
	}
}

int
m0_reqh_service_async_start_simple(struct m0_reqh_service_start_async_ctx *asc)
{
	M0_ENTRY();
	M0_PRE(m0_reqh_service_state_get(asc->sac_service) == M0_RST_STARTING);

	asc->sac_rc = asc->sac_service->rs_ops->rso_start(asc->sac_service);
	m0_fom_wakeup(asc->sac_fom);
	return M0_RC(asc->sac_rc);
}
M0_EXPORTED(m0_reqh_service_async_start_simple);

static bool service_type_is_valid(enum m0_conf_service_type t)
{
	return 0 < t && t < M0_CST_NR;
}

static bool reqh_service_context_invariant(const struct m0_reqh_service_ctx *ctx)
{
	return _0C(ctx != NULL) && _0C(m0_reqh_service_ctx_bob_check(ctx)) &&
	       _0C(m0_fid_is_set(&ctx->sc_fid)) &&
	       _0C(service_type_is_valid(ctx->sc_type));
}

M0_INTERNAL int m0_reqh_service_connect(struct m0_reqh_service_ctx *ctx,
					struct m0_rpc_machine *rmach,
					const char *addr,
					uint32_t max_rpc_nr_in_flight)
{
	int rc;

	M0_ENTRY("'%s' Connect to service '%s'", m0_rpc_machine_ep(rmach),
						 addr);
	M0_PRE(reqh_service_context_invariant(ctx));

	M0_SET0(&ctx->sc_rlink);
	rc = m0_rpc_link_init(&ctx->sc_rlink, rmach, ctx->sc_sobj, addr,
			      max_rpc_nr_in_flight);
	if (rc == 0) {
		rc = m0_rpc_link_connect_sync(&ctx->sc_rlink, M0_TIME_NEVER);
		if (rc != 0)
			m0_rpc_link_fini(&ctx->sc_rlink);
	}
	ctx->sc_is_active = rc == 0;

	return M0_RC(rc);
}

M0_INTERNAL void m0_reqh_service_disconnect(struct m0_reqh_service_ctx *ctx)
{
	m0_time_t timeout;

	M0_ENTRY("Disconnecting from service '%s'",
		 m0_rpc_link_end_point(&ctx->sc_rlink));
	M0_PRE(reqh_service_context_invariant(ctx));

	/*
	 * Not required to wait for reply on session/connection termination
	 * if process is died otherwise wait for some timeout.
	 */
	timeout = !ctx->sc_is_active ? M0_TIME_IMMEDIATELY :
		m0_rpc__down_timeout();

	m0_rpc_link_disconnect_async(&ctx->sc_rlink, timeout,
				     &ctx->sc_rlink_wait);
	ctx->sc_is_active = false;
	M0_LEAVE();
}

M0_INTERNAL int m0_reqh_service_disconnect_wait(struct m0_reqh_service_ctx *ctx)
{
	int rc;

	m0_chan_wait(&ctx->sc_rlink_wait);
	rc = ctx->sc_rlink.rlk_rc;
	m0_rpc_link_fini(&ctx->sc_rlink);

	return M0_RC(rc);
}

static int reqh_service_reconnect(struct m0_reqh_service_ctx *ctx,
				  struct m0_rpc_machine *rmach,
				  const char *addr,
				  uint32_t max_rpc_nr_in_flight)
{
	int rc;

	m0_reqh_service_disconnect(ctx);
	rc = m0_reqh_service_disconnect_wait(ctx) ?:
	     m0_reqh_service_connect(ctx, rmach, addr, max_rpc_nr_in_flight);

	return M0_RC(rc);
}

M0_INTERNAL void m0_reqh_service_ctx_fini(struct m0_reqh_service_ctx *ctx)
{
	M0_ENTRY();

	M0_PRE(reqh_service_context_invariant(ctx));
	M0_PRE(!ctx->sc_is_active);

	m0_clink_fini(&ctx->sc_rlink_wait);
	m0_mutex_fini(&ctx->sc_max_pending_tx_lock);
	m0_clink_fini(&ctx->sc_svc_event);
	m0_clink_fini(&ctx->sc_process_event);

	m0_reqh_service_ctx_bob_fini(ctx);

	M0_LEAVE();
}

/**
 * Connect/Disconnect service context on process event from HA.
 */
static bool process_event_handler(struct m0_clink *clink)
{
	struct m0_reqh_service_ctx *ctx =
		container_of(clink, struct m0_reqh_service_ctx,
			     sc_process_event);
	struct m0_conf_obj         *obj =
		container_of(clink->cl_chan, struct m0_conf_obj, co_ha_chan);
	struct m0_conf_process     *process;
	int                         rc = 0;

	M0_ENTRY();
	M0_PRE(m0_conf_fid_type(&obj->co_id) == &M0_CONF_PROCESS_TYPE);
	process = M0_CONF_CAST(obj, m0_conf_process);

	switch (obj->co_ha_state) {
	case M0_NC_FAILED:
	case M0_NC_TRANSIENT:
		if (ctx->sc_is_active)
			m0_rpc_session_cancel(&ctx->sc_rlink.rlk_sess);
		/*
		 * The session is just cancelled, or it was already cancelled
		 * some time before.
		 */
		M0_ASSERT(m0_rpc_session_is_cancelled(&ctx->sc_rlink.rlk_sess));
		/* m0_rpc_post() needs valid session, so service context is not
		 * finalised. Here making service context as inactive, which
		 * will become active again after reconnection when process is
		 * restarted.
		 */
		ctx->sc_is_active = false;
		break;
	case M0_NC_ONLINE:
		if (ctx->sc_is_active ||
		    m0_conf_obj_grandparent(obj)->co_ha_state != M0_NC_ONLINE)
			break;
		/*
		 * Process may become online prior to service object.
		 *
		 * Make sure respective service object is known online. In case
		 * it is not, quit and let service_event_handler() do the job.
		 *
		 * Note: until service object gets known online, re-connection
		 * is not possible due to assertions in RPC connection HA
		 * subscription code.
		 */
		if (ctx->sc_sobj == NULL)
			ctx->sc_sobj = m0_conf_cache_lookup(obj->co_cache,
							    &ctx->sc_fid);
		M0_ASSERT(ctx->sc_sobj != NULL);
		if (ctx->sc_sobj->co_ha_state != M0_NC_ONLINE)
			break;
		/*
		 * We are about to reconnect, so conf object cache is to unlock
		 * to let reconnection go smooth.
		 */
		m0_conf_cache_unlock(obj->co_cache);
		/* XXX Since reqh_service_reconnect() is synchronous this
		 * prevents situation when other handler is called during
		 * connection establishment/termination. But this blocks the
		 * thread as well.
		 */
		rc = reqh_service_reconnect(ctx, ctx->sc_pc->pc_rmach,
					    process->pc_endpoint,
					    REQH_SVC_MAX_RPCS_IN_FLIGHT);
		m0_conf_cache_lock(obj->co_cache);
		if (rc != 0) {
			M0_LOG(M0_DEBUG, "Service"FID_F", type=%d, rc=%d",
					 FID_P(&ctx->sc_fid), ctx->sc_type, rc);
			return false;
		}
		break;
	default:
		break;
	}

	M0_LEAVE();
	return true;
}

/**
 * Cancel items for service on service failure event from HA.
 */
static bool service_event_handler(struct m0_clink *clink)
{
	struct m0_reqh_service_ctx *ctx =
		container_of(clink, struct m0_reqh_service_ctx, sc_svc_event);
	struct m0_conf_obj         *obj =
		container_of(clink->cl_chan, struct m0_conf_obj, co_ha_chan);
	struct m0_conf_service     *service;
	struct m0_rpc_session      *session = &ctx->sc_rlink.rlk_sess;
	struct m0_reqh             *reqh = m0_conf_obj2reqh(obj);
	bool                        result = true;

	M0_ENTRY();
	M0_PRE(m0_conf_fid_type(&obj->co_id) == &M0_CONF_SERVICE_TYPE);
	service = M0_CONF_CAST(obj, m0_conf_service);
	M0_PRE(ctx == m0_pools_common_service_ctx_find(reqh->rh_pools,
						       &obj->co_id,
						       service->cs_type));

	switch (obj->co_ha_state) {
	case M0_NC_TRANSIENT:
		/*
		 * It seems important to do nothing here to let rpc item ha
		 * timeout do its job. When HA really decides on service death,
		 * it notifies with M0_NC_FAILED.
		 */
		break;
	case M0_NC_FAILED:
		if (ctx->sc_is_active &&
		    !m0_rpc_session_is_cancelled(session))
			m0_rpc_session_cancel(session);
		break;
	case M0_NC_ONLINE:
		/*
		 * In case service event comes to context that is already
		 * active, just make sure the session is not cancelled, and
		 * restore the one otherwise.
		 *
		 * In case the context is not active, do reconnect service
		 * context.
		 *
		 * Note: Make no assumptions about process HA state, as service
		 * state update may take a lead in the batch updates.
		 */
		if (ctx->sc_is_active) {
			if (m0_rpc_session_is_cancelled(session))
				m0_rpc_session_restore(session);
		} else {
			int rc;

			/*
			 * We are about to reconnect, so conf object cache is to
			 * unlock to let reconnection go smooth.
			 */
			m0_conf_cache_unlock(obj->co_cache);
			/* XXX Since reqh_service_reconnect() is synchronous
			 * this prevents situation when other handler is called
			 * during connection establishment/termination. But this
			 * blocks the thread as well.
			 */
			rc = reqh_service_reconnect(ctx, ctx->sc_pc->pc_rmach,
						    service->cs_endpoints[0],
						    REQH_SVC_MAX_RPCS_IN_FLIGHT);
			m0_conf_cache_lock(obj->co_cache);
			if (rc != 0) {
				M0_LOG(M0_DEBUG, "Service"FID_F", type=%d, rc=%d",
				       FID_P(&ctx->sc_fid), ctx->sc_type, rc);
				return false;
			}
		}
		break;
	default:
		break;
	}

	M0_LEAVE();
	return result;
}

M0_INTERNAL int m0_reqh_service_ctx_init(struct m0_reqh_service_ctx *ctx,
					 struct m0_conf_obj *sobj,
					 enum m0_conf_service_type stype)
{
	struct m0_conf_obj *pobj = m0_conf_obj_grandparent(sobj);

	M0_ENTRY();
	M0_LOG(M0_DEBUG, FID_F "%d", FID_P(&sobj->co_id), stype);

	M0_SET0(ctx);
	ctx->sc_fid = sobj->co_id;
	ctx->sc_sobj = sobj;
	ctx->sc_pobj = pobj;
	ctx->sc_type = stype;
	m0_reqh_service_ctx_bob_init(ctx);
	m0_mutex_init(&ctx->sc_max_pending_tx_lock);
	m0_clink_init(&ctx->sc_svc_event, service_event_handler);
	m0_clink_init(&ctx->sc_process_event, process_event_handler);
	m0_clink_init(&ctx->sc_rlink_wait, NULL);
	ctx->sc_rlink_wait.cl_is_oneshot = true;
	ctx->sc_is_active = false;

	M0_POST(reqh_service_context_invariant(ctx));
	M0_LEAVE();
	return 0;
}

M0_INTERNAL int m0_reqh_service_ctx_create(struct m0_conf_obj *sobj,
					   enum m0_conf_service_type stype,
					   struct m0_reqh_service_ctx **ctx)
{
	int rc;

	M0_PRE(m0_fid_is_set(&sobj->co_id));
	M0_PRE(service_type_is_valid(stype));

	M0_ENTRY(FID_F "stype:%d", FID_P(&sobj->co_id), stype);
	M0_ALLOC_PTR(*ctx);
	if (*ctx == NULL)
		return M0_ERR(-ENOMEM);
	rc = m0_reqh_service_ctx_init(*ctx, sobj, stype);
	if (rc != 0)
		m0_free(*ctx);

	return M0_RC(rc);
}

M0_INTERNAL void
m0_reqh_service_ctx_destroy(struct m0_reqh_service_ctx *ctx)
{
	m0_reqh_service_ctx_fini(ctx);
	m0_free(ctx);
}

M0_INTERNAL void m0_reqh_service_ctx_subscribe(struct m0_reqh_service_ctx *ctx)
{
	m0_clink_add_lock(&ctx->sc_sobj->co_ha_chan, &ctx->sc_svc_event);
	m0_clink_add_lock(&ctx->sc_pobj->co_ha_chan, &ctx->sc_process_event);
}

M0_INTERNAL void
m0_reqh_service_ctx_unsubscribe(struct m0_reqh_service_ctx *ctx)
{
	m0_clink_del_lock(&ctx->sc_svc_event);
	m0_clink_del_lock(&ctx->sc_process_event);
}

M0_INTERNAL struct m0_reqh_service_ctx *
m0_reqh_service_ctx_from_session(struct m0_rpc_session *session)
{
	struct m0_reqh_service_ctx *ret;
	struct m0_rpc_link         *rlink;

	M0_PRE(session != NULL);

	rlink = container_of(session, struct m0_rpc_link, rlk_sess);
	ret = container_of(rlink, struct m0_reqh_service_ctx, sc_rlink);

	M0_POST(reqh_service_context_invariant(ret));

	return ret;
}

/** @} endgroup reqhservice */
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
