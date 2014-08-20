/* -*- C -*- */
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
 * Original author: Dmytro Podgornyi <dmytro_podgornyi@xyratex.com>
 * Original creation date: 8-Aug-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/errno.h"
#include "lib/memory.h"               /* m0_free */
#include "lib/string.h"               /* m0_strdup */
#include "lib/trace.h"
#include "lib/chan.h"
#include "lib/misc.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"
#include "net/net.h"                  /* m0_net_end_point */
#include "sm/sm.h"
#include "rpc/rpc_machine.h"          /* m0_rpc_machine */
#include "rpc/rpc_machine_internal.h" /* m0_rpc_machine_lock */
#include "rpc/link.h"

/**
 * @addtogroup rpc_link
 *
 * @{
 */

#define CONN_STATE(conn) (conn)->c_sm.sm_state
#define CONN_RC(conn)    (conn)->c_sm.sm_rc
#define CONN_CHAN(conn)  (conn)->c_sm.sm_chan

#define SESS_STATE(sess) (sess)->s_sm.sm_state
#define SESS_RC(sess)    (sess)->s_sm.sm_rc
#define SESS_CHAN(sess)  (sess)->s_sm.sm_chan

struct rpc_link_state_transition {
	/** Function which executes current phase */
	int       (*rlst_state_function)(struct m0_rpc_link *);
	int         rlst_next_phase;
	/** Description of phase */
	const char *rlst_st_desc;
};

static int    rpc_link_conn_fom_tick(struct m0_fom *fom);
static void   rpc_link_conn_fom_fini(struct m0_fom *fom);
static int    rpc_link_disc_fom_tick(struct m0_fom *fom);
static void   rpc_link_disc_fom_fini(struct m0_fom *fom);
static void   rpc_link_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);
static size_t rpc_link_fom_locality(const struct m0_fom *fom);

struct m0_fom_type rpc_link_conn_fom_type;
struct m0_fom_type rpc_link_disc_fom_type;

const struct m0_fom_ops rpc_link_conn_fom_ops = {
	.fo_fini          = rpc_link_conn_fom_fini,
	.fo_tick          = rpc_link_conn_fom_tick,
	.fo_home_locality = rpc_link_fom_locality,
	.fo_addb_init     = rpc_link_fom_addb_init
};

const struct m0_fom_ops rpc_link_disc_fom_ops = {
	.fo_fini          = rpc_link_disc_fom_fini,
	.fo_tick          = rpc_link_disc_fom_tick,
	.fo_home_locality = rpc_link_fom_locality,
	.fo_addb_init     = rpc_link_fom_addb_init
};

static const struct m0_fom_type_ops rpc_link_conn_fom_type_ops = {
	.fto_create = NULL,
};

static const struct m0_fom_type_ops rpc_link_disc_fom_type_ops = {
	.fto_create = NULL,
};

static size_t rpc_link_fom_locality(const struct m0_fom *fom)
{
	return 1;
}

/* Routines for connection */

static int rpc_link_conn_establish(struct m0_rpc_link *rlink)
{
	int                      rc;
	struct m0_net_end_point *ep;

	rc = m0_net_end_point_create(&ep, &rlink->rlk_rpcmach->rm_tm,
				     rlink->rlk_rem_ep);
	if (rc == 0) {
		rc = m0_rpc_conn_init(&rlink->rlk_conn, ep, rlink->rlk_rpcmach,
				      rlink->rlk_max_rpcs_in_flight);
		m0_net_end_point_put(ep);
	}
	if (rc == 0) {
		rc = m0_rpc_conn_establish(&rlink->rlk_conn,
					m0_time_from_now(rlink->rlk_timeout, 0));
		if (rc != 0)
			m0_rpc_conn_fini(&rlink->rlk_conn);
	}
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Connection establish failed (rlink=%p)",
		       rlink);
	}
	return rc == 0 ? M0_FSO_AGAIN : rc;
}

static int rpc_link_sess_establish(struct m0_rpc_link *rlink)
{
	int rc;

	rc = m0_rpc_session_init(&rlink->rlk_sess, &rlink->rlk_conn);
	if (rc == 0) {
		rc = m0_rpc_session_establish(&rlink->rlk_sess,
					m0_time_from_now(rlink->rlk_timeout, 0));
		if (rc != 0)
			m0_rpc_session_fini(&rlink->rlk_sess);
	}
	if (rc != 0)
		M0_LOG(M0_ERROR, "Session establish failed (rlink=%p)", rlink);

	return rc == 0 ? M0_FSO_AGAIN : rc;
}

static int rpc_link_sess_established(struct m0_rpc_link *rlink)
{
	/*
	 * Rpc_link considered to be connected after fom is fini'ed. This avoids
	 * race when rlink->rlk_fom is used by m0_rpc_link_disconnect_async().
	 * @see rpc_link_conn_fom_fini()
	 */
	return M0_FSO_WAIT;
}

/* Routines for disconnection */

static int rpc_link_disc_init(struct m0_rpc_link *rlink)
{
	return M0_FSO_AGAIN;
}

static int rpc_link_conn_terminate(struct m0_rpc_link *rlink)
{
	int rc;

	m0_rpc_session_fini(&rlink->rlk_sess);
	rc = m0_rpc_conn_terminate(&rlink->rlk_conn,
				   m0_time_from_now(rlink->rlk_timeout, 0));
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Connection termination failed (rlink=%p)",
		       rlink);
	}
	return rc == 0 ? M0_FSO_AGAIN : rc;
}

static int rpc_link_conn_terminated(struct m0_rpc_link *rlink)
{
	m0_rpc_conn_fini(&rlink->rlk_conn);
	return M0_FSO_WAIT;
}

static int rpc_link_sess_terminate(struct m0_rpc_link *rlink)
{
	int rc;

	M0_PRE(SESS_STATE(&rlink->rlk_sess) == M0_RPC_SESSION_IDLE);

	rc = m0_rpc_session_terminate(&rlink->rlk_sess,
				      m0_time_from_now(rlink->rlk_timeout, 0));
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Session termination failed (rlink=%p)",
		       rlink);
	}
	return rc == 0 ? M0_FSO_AGAIN : rc;
}

static int rpc_link_conn_failure(struct m0_rpc_link *rlink)
{
	rlink->rlk_rc = m0_fom_rc(&rlink->rlk_fom);
	return M0_FSO_WAIT;
}

static struct rpc_link_state_transition rpc_link_conn_states[] = {

	[M0_RLS_INIT] =
	{ &rpc_link_conn_establish, M0_RLS_CONN_CONNECTING, "Initialised" },

	[M0_RLS_CONN_CONNECTING] =
	{ &rpc_link_sess_establish, M0_RLS_SESS_ESTABLISHING,
	  "Connection establish" },

	[M0_RLS_SESS_ESTABLISHING] =
	{ &rpc_link_sess_established, M0_RLS_FINI, "Session establish" },

	[M0_RLS_FAILURE] =
	{ &rpc_link_conn_failure, M0_RLS_FINI, "Failure in connection" },
};

static struct rpc_link_state_transition rpc_link_disc_states[] = {

	[M0_RLS_INIT] =
	{ &rpc_link_disc_init, M0_RLS_SESS_WAIT_IDLE, "Initialised" },

	[M0_RLS_SESS_WAIT_IDLE] =
	{ &rpc_link_sess_terminate, M0_RLS_SESS_TERMINATING, "IDLE state wait" },

	[M0_RLS_SESS_TERMINATING] =
	{ &rpc_link_conn_terminate, M0_RLS_CONN_TERMINATING,
	  "Session termination" },

	[M0_RLS_CONN_TERMINATING] =
	{ &rpc_link_conn_terminated, M0_RLS_FINI, "Conn termination" },

	[M0_RLS_FAILURE] =
	{ &rpc_link_conn_failure, M0_RLS_FINI, "Failure in disconnection" },
};

static void rpc_link_conn_fom_wait_on(struct m0_fom *fom,
				      struct m0_rpc_link *rlink)
{
	m0_fom_callback_init(&rlink->rlk_fomcb);
	m0_fom_wait_on(fom, &CONN_CHAN(&rlink->rlk_conn), &rlink->rlk_fomcb);
}

static void rpc_link_sess_fom_wait_on(struct m0_fom *fom,
				      struct m0_rpc_link *rlink)
{
	m0_fom_callback_init(&rlink->rlk_fomcb);
	m0_fom_wait_on(fom, &SESS_CHAN(&rlink->rlk_sess), &rlink->rlk_fomcb);
}

static int rpc_link_conn_fom_tick(struct m0_fom *fom)
{
	int                 rc    = 0;
	int                 phase = m0_fom_phase(fom);
	bool                armed = false;
	uint32_t            state;
	struct m0_rpc_link *rlink;

	M0_ENTRY("fom=%p phase=%s", fom, m0_fom_phase_name(fom, phase));

	rlink = container_of(fom, struct m0_rpc_link, rlk_fom);

	m0_rpc_machine_lock(rlink->rlk_rpcmach);
	switch (phase) {
	case M0_RLS_CONN_CONNECTING:
		state = CONN_STATE(&rlink->rlk_conn);
		M0_ASSERT(M0_IN(state, (M0_RPC_CONN_CONNECTING,
				M0_RPC_CONN_ACTIVE, M0_RPC_CONN_FAILED)));
		if (state == M0_RPC_CONN_FAILED)
			rc = CONN_RC(&rlink->rlk_conn);
		if (state == M0_RPC_CONN_CONNECTING) {
			rpc_link_conn_fom_wait_on(fom, rlink);
			armed = true;
			rc    = M0_FSO_WAIT;
		}
		break;
	case M0_RLS_SESS_ESTABLISHING:
		state = SESS_STATE(&rlink->rlk_sess);
		M0_ASSERT(M0_IN(state, (M0_RPC_SESSION_ESTABLISHING,
				M0_RPC_SESSION_IDLE, M0_RPC_SESSION_FAILED)));
		if (state == M0_RPC_SESSION_FAILED)
			rc = SESS_RC(&rlink->rlk_sess);
		if (state == M0_RPC_SESSION_ESTABLISHING) {
			rpc_link_sess_fom_wait_on(fom, rlink);
			armed = true;
			rc    = M0_FSO_WAIT;
		}
		break;
	}
	m0_rpc_machine_unlock(rlink->rlk_rpcmach);

	if (rc == 0) {
		rc = (*rpc_link_conn_states[phase].rlst_state_function)(rlink);
		M0_ASSERT(rc != 0);
		if (rc > 0) {
			phase = rpc_link_conn_states[phase].rlst_next_phase;
			m0_fom_phase_set(fom, phase);
		}
	}

	if (rc < 0) {
		if (armed)
			m0_fom_callback_cancel(&rlink->rlk_fomcb);
		m0_fom_phase_move(fom, rc, M0_RLS_FAILURE);
		rc = M0_FSO_AGAIN;
	}
	return M0_RC(rc);
}

static int rpc_link_disc_fom_tick(struct m0_fom *fom)
{
	int                 rc    = 0;
	int                 phase = m0_fom_phase(fom);
	bool                armed = false;
	uint32_t            state;
	struct m0_rpc_link *rlink;

	M0_ENTRY("fom=%p phase=%s", fom, m0_fom_phase_name(fom, phase));

	rlink = container_of(fom, struct m0_rpc_link, rlk_fom);

	m0_rpc_machine_lock(rlink->rlk_rpcmach);
	switch (phase) {
	case M0_RLS_SESS_WAIT_IDLE:
		state = SESS_STATE(&rlink->rlk_sess);
		M0_ASSERT(M0_IN(state, (M0_RPC_SESSION_IDLE,
				M0_RPC_SESSION_BUSY, M0_RPC_SESSION_FAILED)));
		if (state == M0_RPC_SESSION_FAILED)
			rc = SESS_RC(&rlink->rlk_sess);
		if (state == M0_RPC_SESSION_BUSY) {
			rpc_link_sess_fom_wait_on(fom, rlink);
			armed = true;
			rc    = M0_FSO_WAIT;
		}
		break;
	case M0_RLS_SESS_TERMINATING:
		state = SESS_STATE(&rlink->rlk_sess);
		M0_ASSERT(M0_IN(state, (M0_RPC_SESSION_TERMINATING,
				M0_RPC_SESSION_TERMINATED,
				M0_RPC_SESSION_FAILED)));
		/*
		 * We need to terminate connection even if session termination
		 * failed.
		 */
		if (state == M0_RPC_SESSION_TERMINATING) {
			rpc_link_sess_fom_wait_on(fom, rlink);
			armed = true;
			rc    = M0_FSO_WAIT;
		}
		break;
	case M0_RLS_CONN_TERMINATING:
		state = CONN_STATE(&rlink->rlk_conn);
		M0_ASSERT(M0_IN(state, (M0_RPC_CONN_TERMINATING,
				M0_RPC_CONN_TERMINATED,
				M0_RPC_CONN_FAILED)));
		if (state == M0_RPC_CONN_TERMINATING) {
			rpc_link_conn_fom_wait_on(fom, rlink);
			armed = true;
			rc    = M0_FSO_WAIT;
		}
		break;
	}
	m0_rpc_machine_unlock(rlink->rlk_rpcmach);

	if (rc == 0) {
		rc = (*rpc_link_disc_states[phase].rlst_state_function)(rlink);
		M0_ASSERT(rc != 0);
		if (rc > 0) {
			phase = rpc_link_disc_states[phase].rlst_next_phase;
			m0_fom_phase_set(fom, phase);
		}
	}

	if (rc < 0) {
		if (armed)
			m0_fom_callback_cancel(&rlink->rlk_fomcb);
		m0_fom_phase_move(fom, rc, M0_RLS_FAILURE);
		rc = M0_FSO_AGAIN;
	}
	return M0_RC(rc);
}


static void rpc_link_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static void rpc_link_fom_fini_common(struct m0_fom *fom, bool connected)
{
	struct m0_rpc_link *rlink;

	M0_ENTRY("fom=%p", fom);

	rlink = container_of(fom, struct m0_rpc_link, rlk_fom);
	m0_fom_fini(fom);
	rlink->rlk_connected = connected;
	m0_chan_broadcast_lock(&rlink->rlk_wait);

	M0_LEAVE();
}

static void rpc_link_conn_fom_fini(struct m0_fom *fom)
{
	rpc_link_fom_fini_common(fom, true);
}

static void rpc_link_disc_fom_fini(struct m0_fom *fom)
{
	rpc_link_fom_fini_common(fom, false);
}

static struct m0_sm_state_descr rpc_link_conn_state_descr[] = {

	[M0_RLS_INIT] = {
		.sd_flags       = M0_SDF_INITIAL,
		.sd_name        = "Initialised",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_CONN_CONNECTING)
	},
	[M0_RLS_FAILURE] = {
		.sd_flags       = M0_SDF_FAILURE,
		.sd_name        = "Failure",
		.sd_allowed     = M0_BITS(M0_RLS_FINI)
	},
	[M0_RLS_CONN_CONNECTING] = {
		.sd_flags       = 0,
		.sd_name        = "Connection establish",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_SESS_ESTABLISHING)
	},
	[M0_RLS_SESS_ESTABLISHING] = {
		.sd_flags       = 0,
		.sd_name        = "Session establish",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_FINI)
	},
	[M0_RLS_FINI] = {
		.sd_flags       = M0_SDF_TERMINAL,
		.sd_name        = "Fini",
		.sd_allowed     = 0
	},
};

static struct m0_sm_conf rpc_link_conn_sm_conf = {
	.scf_name      = "rpc_link connection state machine",
	.scf_nr_states = ARRAY_SIZE(rpc_link_conn_state_descr),
	.scf_state     = rpc_link_conn_state_descr
};

static struct m0_sm_state_descr rpc_link_disc_state_descr[] = {

	[M0_RLS_INIT] = {
		.sd_flags       = M0_SDF_INITIAL,
		.sd_name        = "Initialised",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_SESS_WAIT_IDLE)
	},
	[M0_RLS_FAILURE] = {
		.sd_flags       = M0_SDF_FAILURE,
		.sd_name        = "Failure",
		.sd_allowed     = M0_BITS(M0_RLS_FINI)
	},
	[M0_RLS_SESS_WAIT_IDLE] = {
		.sd_flags       = 0,
		.sd_name        = "Waiting for session is idle",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_SESS_TERMINATING)
	},
	[M0_RLS_SESS_TERMINATING] = {
		.sd_flags       = 0,
		.sd_name        = "Session termination",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_CONN_TERMINATING)
	},
	[M0_RLS_CONN_TERMINATING] = {
		.sd_flags       = 0,
		.sd_name        = "Connection termination",
		.sd_allowed     = M0_BITS(M0_RLS_FAILURE,
					  M0_RLS_FINI)
	},
	[M0_RLS_FINI] = {
		.sd_flags       = M0_SDF_TERMINAL,
		.sd_name        = "Fini",
		.sd_allowed     = 0
	},
};

static struct m0_sm_conf rpc_link_disc_sm_conf = {
	.scf_name      = "rpc_link disconnection state machine",
	.scf_nr_states = ARRAY_SIZE(rpc_link_disc_state_descr),
	.scf_state     = rpc_link_disc_state_descr
};

extern struct m0_reqh_service_type m0_rpc_service_type;

M0_INTERNAL int m0_rpc_link_module_init(void)
{
	m0_fom_type_init(&rpc_link_conn_fom_type, &rpc_link_conn_fom_type_ops,
			 &m0_rpc_service_type, &rpc_link_conn_sm_conf);
	m0_fom_type_init(&rpc_link_disc_fom_type, &rpc_link_disc_fom_type_ops,
			 &m0_rpc_service_type, &rpc_link_disc_sm_conf);
	return 0;
}

M0_INTERNAL void m0_rpc_link_module_fini(void)
{
}

M0_INTERNAL int m0_rpc_link_init(struct m0_rpc_link *rlink,
				 struct m0_rpc_machine *mach,
				 const char *ep,
				 uint64_t timeout,
				 uint64_t max_rpcs_in_flight)
{
	int rc;

	M0_ENTRY("rlink=%p ep=%s", rlink, ep);

	rlink->rlk_connected          = false;
	rlink->rlk_timeout            = timeout;
	rlink->rlk_max_rpcs_in_flight = max_rpcs_in_flight;
	rlink->rlk_rpcmach            = mach;
	rlink->rlk_rem_ep             = m0_strdup(ep);
	rc = rlink->rlk_rem_ep == NULL ? -ENOMEM : 0;
	if (rc == 0) {
		m0_mutex_init(&rlink->rlk_wait_mutex);
		m0_chan_init(&rlink->rlk_wait, &rlink->rlk_wait_mutex);
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_rpc_link_fini(struct m0_rpc_link *rlink)
{
	M0_PRE(!rlink->rlk_connected);
	m0_chan_fini_lock(&rlink->rlk_wait);
	m0_mutex_fini(&rlink->rlk_wait_mutex);
	m0_free(rlink->rlk_rem_ep);
}

static void rpc_link_fom_queue(struct m0_rpc_link *rlink,
			       struct m0_clink *wait_clink,
			       const struct m0_fom_type *fom_type,
			       const struct m0_fom_ops *fom_ops)
{
	struct m0_rpc_machine *mach = rlink->rlk_rpcmach;

	M0_ENTRY("rlink=%p", rlink);
	M0_PRE(ergo(wait_clink != NULL, wait_clink->cl_is_oneshot));

	rlink->rlk_rc = 0;
	if (wait_clink != NULL)
		m0_clink_add_lock(&rlink->rlk_wait, wait_clink);
	m0_fom_init(&rlink->rlk_fom, fom_type, fom_ops, NULL, NULL,
		    mach->rm_reqh);
	m0_fom_queue(&rlink->rlk_fom, mach->rm_reqh);

	M0_LEAVE();
}

static int rpc_link_call_sync(struct m0_rpc_link *rlink,
			      void (*cb)(struct m0_rpc_link*, struct m0_clink*))
{
	struct m0_clink clink;

	m0_clink_init(&clink, NULL);
	clink.cl_is_oneshot = true;
	cb(rlink, &clink);
	m0_chan_wait(&clink);
	m0_clink_fini(&clink);
	return rlink->rlk_rc;
}

M0_INTERNAL void m0_rpc_link_connect_async(struct m0_rpc_link *rlink,
					   struct m0_clink *wait_clink)
{
	M0_PRE(!rlink->rlk_connected);
	rpc_link_fom_queue(rlink, wait_clink, &rpc_link_conn_fom_type,
			   &rpc_link_conn_fom_ops);
}

M0_INTERNAL int m0_rpc_link_connect_sync(struct m0_rpc_link *rlink)
{
	return rpc_link_call_sync(rlink, &m0_rpc_link_connect_async);
}

M0_INTERNAL void m0_rpc_link_disconnect_async(struct m0_rpc_link *rlink,
					      struct m0_clink *wait_clink)
{
	M0_PRE(rlink->rlk_connected);
	rpc_link_fom_queue(rlink, wait_clink, &rpc_link_disc_fom_type,
			   &rpc_link_disc_fom_ops);
}

M0_INTERNAL int m0_rpc_link_disconnect_sync(struct m0_rpc_link *rlink)
{
	return rpc_link_call_sync(rlink, &m0_rpc_link_disconnect_async);
}
#undef M0_TRACE_SUBSYSTEM

/** @} end of rpc_link group */

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
