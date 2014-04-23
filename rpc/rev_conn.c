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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 9-Apr-2013
 */


/**
 * @addtogroup rev_conn
 *
 * @{
 */
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"
#include "net/net.h"
#include "sm/sm.h"
#include "rpc/rev_conn.h"

#define CONN_STATE(conn) conn->c_sm.sm_state
#define CONN_GRP(conn)   conn->c_sm.sm_grp
#define CONN_CHAN(conn)  conn->c_sm.sm_chan

#define SESS_STATE(sess) (sess)->s_sm.sm_state
#define SESS_GRP(sess)   (sess)->s_sm.sm_grp
#define SESS_CHAN(sess)  (sess)->s_sm.sm_chan

enum {
	RPCS_IN_FLIGHT = 1,
};

struct rev_conn_state_transition {
	int         rcst_current_phase;
	/** Function which executes current phase */
	int       (*rcst_state_function)(struct m0_fom *);
	/** Next phase in which FOM is going to execute */
	int         rcst_next_phase_again;
	/** Next phase in which FOM is going to wait */
	int         rcst_next_phase_wait;
	/** Description of phase */
	const char *rcst_st_desc;
};

static int    rev_conn_fom_tick(struct m0_fom *fom);
static void   rev_conn_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);
static void   rev_conn_fom_fini(struct m0_fom *fom);
static size_t rev_conn_fom_locality(const struct m0_fom *fom);

struct m0_fom_type rev_conn_fom_type;

const struct m0_fom_ops rev_conn_fom_ops = {
	.fo_fini          = rev_conn_fom_fini,
	.fo_tick          = rev_conn_fom_tick,
	.fo_home_locality = rev_conn_fom_locality,
	.fo_addb_init     = rev_conn_fom_addb_init
};

static const struct m0_fom_type_ops rev_conn_fom_type_ops = {
	.fto_create = NULL,
};

static size_t rev_conn_fom_locality(const struct m0_fom *fom)
{
	return 1;
}

/* Routines for connection */

static void connection_wait_complete(struct m0_fom_callback *cb)
{
	struct m0_fom                *fom   = cb->fc_fom;
	int                           phase = m0_fom_phase(fom);
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	switch (phase) {
	case M0_RCS_CONN_WAIT:
		if (CONN_STATE(revc->rcf_conn) == M0_RPC_CONN_ACTIVE)
			m0_fom_ready(fom);
		else {
			m0_fom_callback_init(&revc->rcf_fomcb);
			revc->rcf_fomcb.fc_bottom = connection_wait_complete;
			m0_sm_group_lock(CONN_GRP(revc->rcf_conn));
			m0_fom_callback_arm(fom, &CONN_CHAN(revc->rcf_conn),
					    &revc->rcf_fomcb);
			m0_sm_group_unlock(CONN_GRP(revc->rcf_conn));
		}
		break;
	case M0_RCS_SESSION_WAIT:
		if (SESS_STATE(revc->rcf_sess) == M0_RPC_SESSION_IDLE)
			m0_fom_ready(fom);
		else {
			m0_fom_callback_init(&revc->rcf_fomcb);
			revc->rcf_fomcb.fc_bottom = connection_wait_complete;
			m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
			m0_fom_callback_arm(fom, &SESS_CHAN(revc->rcf_sess),
					    &revc->rcf_fomcb);
			m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
		}
		break;
	}
}

static int conn_establish(struct m0_fom *fom)
{
	int                           rc;
	struct m0_net_end_point      *ep;
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	M0_LOG(M0_DEBUG, "Reverse connection request to end point %s",
	       revc->rcf_rem_ep);

	rc = m0_net_end_point_create(&ep, &revc->rcf_rpcmach->rm_tm,
				     revc->rcf_rem_ep);
	if (rc != 0) {
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
	}

	M0_ALLOC_PTR(revc->rcf_conn);
	if (revc->rcf_conn == NULL) {
		m0_fom_phase_move(fom, -ENOMEM, M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
	}

	m0_rpc_conn_init(revc->rcf_conn, ep, revc->rcf_rpcmach,
			 RPCS_IN_FLIGHT);
	m0_net_end_point_put(ep);
	if (rc == 0) {
		m0_fom_callback_init(&revc->rcf_fomcb);
		revc->rcf_fomcb.fc_bottom = connection_wait_complete;
		m0_sm_group_lock(CONN_GRP(revc->rcf_conn));
		m0_fom_callback_arm(fom, &CONN_CHAN(revc->rcf_conn),
				    &revc->rcf_fomcb);
		m0_sm_group_unlock(CONN_GRP(revc->rcf_conn));
		rc = m0_rpc_conn_establish(revc->rcf_conn,
					   m0_time_from_now(
						   M0_REV_CONN_TIMEOUT, 0));
		if (rc == 0) {
			rc = M0_FSO_WAIT;
		} else {
			m0_sm_group_lock(CONN_GRP(revc->rcf_conn));
			m0_fom_callback_cancel(&revc->rcf_fomcb);
			m0_sm_group_unlock(CONN_GRP(revc->rcf_conn));
			m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
			rc = M0_FSO_AGAIN;
		}
	}
	return rc;
}

static int conn_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

static int session_establish(struct m0_fom *fom)
{
	int                           rc;
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	rc = m0_rpc_session_init(revc->rcf_sess, revc->rcf_conn);
	if (rc == 0) {
		rc = m0_rpc_session_establish(revc->rcf_sess,
				      m0_time_from_now(M0_REV_CONN_TIMEOUT, 0));
		if (rc == 0) {
			m0_fom_callback_init(&revc->rcf_fomcb);
			revc->rcf_fomcb.fc_bottom = connection_wait_complete;
			m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
			m0_fom_callback_arm(fom, &SESS_CHAN(revc->rcf_sess),
					    &revc->rcf_fomcb);
			m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
			rc = M0_FSO_WAIT;
		} else {
			m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
			m0_fom_callback_cancel(&revc->rcf_fomcb);
			m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
			m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
			rc = M0_FSO_AGAIN;
		}
	}

	return rc;
}

static int session_wait(struct m0_fom *fom)
{
	m0_chan_broadcast_lock(&fom->fo_service->rs_rev_conn_wait);
	return M0_FSO_WAIT;
}

/* Routines for disconnection */

static void disconnection_wait_complete(struct m0_fom_callback *cb)
{
	struct m0_fom                *fom   = cb->fc_fom;
	int                           phase = m0_fom_phase(fom);
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	switch (phase) {
	case M0_RCS_CONN_WAIT:
		if (SESS_STATE(revc->rcf_sess) == M0_RPC_SESSION_IDLE)
			m0_fom_ready(fom);
		else {
			m0_fom_callback_init(&revc->rcf_fomcb);
			revc->rcf_fomcb.fc_bottom = connection_wait_complete;
			m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
			m0_fom_callback_arm(fom, &SESS_CHAN(revc->rcf_sess),
					    &revc->rcf_fomcb);
			m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
		}
		break;
	case M0_RCS_SESSION:
		if (SESS_STATE(revc->rcf_sess) == M0_RPC_SESSION_TERMINATED ||
		    SESS_STATE(revc->rcf_sess) == M0_RPC_SESSION_FAILED)
			m0_fom_ready(fom);
		else {
			m0_fom_callback_init(&revc->rcf_fomcb);
			revc->rcf_fomcb.fc_bottom = disconnection_wait_complete;
			m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
			m0_fom_callback_arm(fom, &SESS_CHAN(revc->rcf_sess),
					    &revc->rcf_fomcb);
			m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
		}
		break;
	case M0_RCS_SESSION_WAIT:
		if (CONN_STATE(revc->rcf_conn) == M0_RPC_CONN_TERMINATED ||
		    CONN_STATE(revc->rcf_conn) == M0_RPC_CONN_FAILED)
			m0_fom_ready(fom);
		else {
			m0_fom_callback_init(&revc->rcf_fomcb);
			revc->rcf_fomcb.fc_bottom = connection_wait_complete;
			m0_sm_group_lock(CONN_GRP(revc->rcf_conn));
			m0_fom_callback_arm(fom, &CONN_CHAN(revc->rcf_conn),
					    &revc->rcf_fomcb);
			m0_sm_group_unlock(CONN_GRP(revc->rcf_conn));
		}
		break;
	}
}

static int conn_terminate(struct m0_fom *fom)
{
	int                           rc;
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	m0_rpc_session_fini(revc->rcf_sess);
	rc = m0_rpc_conn_terminate(revc->rcf_conn,
				   m0_time_from_now(M0_REV_CONN_TIMEOUT, 0));
	if (rc == 0) {
		m0_fom_callback_init(&revc->rcf_fomcb);
		revc->rcf_fomcb.fc_bottom = disconnection_wait_complete;
		m0_sm_group_lock(CONN_GRP(revc->rcf_conn));
		m0_fom_callback_arm(fom, &CONN_CHAN(revc->rcf_conn),
				    &revc->rcf_fomcb);
		m0_sm_group_unlock(CONN_GRP(revc->rcf_conn));
		rc = M0_FSO_WAIT;
	} else {
		m0_sm_group_lock(CONN_GRP(revc->rcf_conn));
		m0_fom_callback_cancel(&revc->rcf_fomcb);
		m0_sm_group_unlock(CONN_GRP(revc->rcf_conn));
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
	}

	return rc;
}

static int conn_disc_wait(struct m0_fom *fom)
{
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);
	m0_rpc_conn_fini(revc->rcf_conn);
	return M0_FSO_WAIT;
}

static int session_terminate(struct m0_fom *fom)
{
	int                           rc;
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	if (SESS_STATE(revc->rcf_sess) == M0_RPC_SESSION_IDLE) {
		rc = M0_FSO_AGAIN;
	} else {
		m0_fom_callback_init(&revc->rcf_fomcb);
		revc->rcf_fomcb.fc_bottom = disconnection_wait_complete;
		m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
		m0_fom_callback_arm(fom, &SESS_CHAN(revc->rcf_sess),
				    &revc->rcf_fomcb);
		m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
		rc = M0_FSO_WAIT;
	}
	return rc;
}

static int session_disc_wait(struct m0_fom *fom)
{
	int                           rc;
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	rc = m0_rpc_session_terminate(revc->rcf_sess,
				      m0_time_from_now(M0_REV_CONN_TIMEOUT, 0));
	if (rc == 0) {
		m0_fom_callback_init(&revc->rcf_fomcb);
		revc->rcf_fomcb.fc_bottom = disconnection_wait_complete;
		m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
		m0_fom_callback_arm(fom, &SESS_CHAN(revc->rcf_sess),
				    &revc->rcf_fomcb);
		m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
		rc = M0_FSO_WAIT;
	} else {
		m0_sm_group_lock(SESS_GRP(revc->rcf_sess));
		m0_fom_callback_cancel(&revc->rcf_fomcb);
		m0_sm_group_unlock(SESS_GRP(revc->rcf_sess));
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
	}

	return rc;

}

static struct rev_conn_state_transition connect_states[] = {

	[M0_RCS_CONN] =
	{ M0_RCS_CONN, &conn_establish,
	  0, M0_RCS_CONN_WAIT, "Connection establish", },

	[M0_RCS_CONN_WAIT] =
	{ M0_RCS_CONN_WAIT, &conn_wait,
	  M0_RCS_SESSION, 0, "Connection establish wait", },

	[M0_RCS_SESSION] =
	{ M0_RCS_SESSION, &session_establish,
	  0, M0_RCS_SESSION_WAIT, "Session establish", },

	[M0_RCS_SESSION_WAIT] =
	{ M0_RCS_SESSION_WAIT, &session_wait,
	  0, M0_RCS_FINI, "Session establish wait", },
};

static struct rev_conn_state_transition disconnect_states[] = {

	[M0_RCS_CONN] =
	{ M0_RCS_CONN, &session_terminate,
	  M0_RCS_CONN_WAIT, M0_RCS_CONN_WAIT, "Session terminate", },

	[M0_RCS_CONN_WAIT] =
	{ M0_RCS_CONN_WAIT, &session_disc_wait,
	  0, M0_RCS_SESSION, "Session terminate wait", },

	[M0_RCS_SESSION] =
	{ M0_RCS_SESSION, &conn_terminate,
	  0, M0_RCS_SESSION_WAIT, "Conn terminate", },

	[M0_RCS_SESSION_WAIT] =
	{ M0_RCS_SESSION_WAIT, &conn_disc_wait,
	  0, M0_RCS_FINI, "Conn terminate wait", },
};

static int rev_conn_fom_tick(struct m0_fom *fom)
{
	int                              rc;
	int                              phase = m0_fom_phase(fom);
	struct m0_reverse_connection    *revc;
	struct rev_conn_state_transition st;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);

	st = revc->rcf_ft == M0_REV_CONNECT ? connect_states[phase] :
		disconnect_states[phase];

	rc = (*st.rcst_state_function)(fom);
	m0_fom_phase_set(fom, rc == M0_FSO_AGAIN ?
			 st.rcst_next_phase_again :
			 st.rcst_next_phase_wait);
	return rc;
}

static void rev_conn_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static void rev_conn_fom_fini(struct m0_fom *fom)
{
	struct m0_reverse_connection *revc;

	revc = container_of(fom, struct m0_reverse_connection, rcf_fom);
	m0_fom_fini(fom);
	if (revc->rcf_ft == M0_REV_DISCONNECT)
		m0_chan_signal_lock(&revc->rcf_chan);
}

static struct m0_sm_state_descr rev_conn_state_descr[] = {

        [M0_RCS_CONN] = {
                .sd_flags       = M0_SDF_INITIAL,
                .sd_name        = "Connection establish",
                .sd_allowed     = M0_BITS(M0_RCS_FAILURE,
					  M0_RCS_CONN_WAIT)
        },
	[M0_RCS_FAILURE] = {
		.sd_flags       = M0_SDF_FAILURE,
		.sd_name        = "Failure",
		.sd_allowed     = M0_BITS(M0_RCS_FINI)
	},
	[M0_RCS_CONN_WAIT] = {
		.sd_flags       = 0,
		.sd_name        = "Connection wait",
		.sd_allowed     = M0_BITS(M0_RCS_FAILURE,
					  M0_RCS_SESSION)
	},
	[M0_RCS_SESSION] = {
		.sd_flags       = 0,
		.sd_name        = "Session establish",
		.sd_allowed     = M0_BITS(M0_RCS_FAILURE,
					  M0_RCS_SESSION_WAIT)
	},
        [M0_RCS_SESSION_WAIT] = {
                .sd_flags       = 0,
                .sd_name        = "Session wait",
                .sd_allowed     = M0_BITS(M0_RCS_FINI)
        },
	[M0_RCS_FINI] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Fini",
                .sd_allowed     = 0
        },
};

static struct m0_sm_conf rev_conn_sm_conf = {
	.scf_name      = "Reverse connection state machine",
	.scf_nr_states = ARRAY_SIZE(rev_conn_state_descr),
	.scf_state     = rev_conn_state_descr
};

extern struct m0_reqh_service_type m0_rpc_service_type;

M0_INTERNAL void m0_rev_conn_fom_type_init(void)
{
	m0_fom_type_init(&rev_conn_fom_type, &rev_conn_fom_type_ops,
			 &m0_rpc_service_type, &rev_conn_sm_conf);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of rev_conn group */

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
