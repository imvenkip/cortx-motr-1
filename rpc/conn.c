/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 08/24/2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"    /* M0_BITS */
#include "lib/bitstring.h"
#include "lib/arith.h"
#include "lib/finject.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "db/db.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{

   This file implements functions related to m0_rpc_conn.
 */

static const char conn_cob_name_fmt[] = "SENDER_%lu";

M0_INTERNAL struct m0_rpc_chan *rpc_chan_get(struct m0_rpc_machine *machine,
					     struct m0_net_end_point *dest_ep,
					     uint64_t max_rpcs_in_flight);
M0_INTERNAL void rpc_chan_put(struct m0_rpc_chan *chan);

/**
   Attaches session 0 object to conn object.
 */
static int session_zero_attach(struct m0_rpc_conn *conn);

/**
   Detaches session 0 from conn
 */
static void session_zero_detach(struct m0_rpc_conn *conn);

/**
   Creates "/SESSIONS/SENDER_$sender_id/SESSION_ID_0/SLOT_0:0" in cob namespace.
   Returns corresponding references to cobs in out parameters.
 */
static int conn_persistent_state_create(struct m0_cob_domain  *dom,
					uint64_t              sender_id,
					struct m0_cob       **conn_cob_out,
					struct m0_cob       **session0_cob_out,
					struct m0_cob       **slot0_cob_out,
					struct m0_db_tx      *tx);

/**
   Delegates persistent state creation to conn_persistent_state_create().
   And associates returned cobs to conn->c_cob, session0->s_cob and
   slot0->sl_cob
 */
static int conn_persistent_state_attach(struct m0_rpc_conn *conn,
					uint64_t            sender_id,
					struct m0_db_tx    *tx);

static int __conn_init(struct m0_rpc_conn      *conn,
		       struct m0_net_end_point *ep,
		       struct m0_rpc_machine   *machine,
		       uint64_t			max_rpcs_in_flight);
/**
   Common code in m0_rpc_conn_fini() and init failed case in __conn_init()
 */
static void __conn_fini(struct m0_rpc_conn *conn);

static void conn_failed(struct m0_rpc_conn *conn, int32_t error);

/*
 * This is sender side item_ops of conn_establish fop.
 * Receiver side conn_establish fop has different item_ops
 * rcv_conn_establish_item_ops defined in rpc/session_fops.c
 */
static const struct m0_rpc_item_ops conn_establish_item_ops = {
	.rio_replied = m0_rpc_conn_establish_reply_received,
};

static const struct m0_rpc_item_ops conn_terminate_item_ops = {
	.rio_replied = m0_rpc_conn_terminate_reply_received,
};

static const struct m0_sm_state_descr conn_states[] = {
	[M0_RPC_CONN_INITIALISED] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Initialised",
		.sd_allowed   = M0_BITS(M0_RPC_CONN_CONNECTING,
					M0_RPC_CONN_FINALISED,
					M0_RPC_CONN_FAILED)
	},
	[M0_RPC_CONN_CONNECTING] = {
		.sd_name      = "Connecting",
		.sd_allowed   = M0_BITS(M0_RPC_CONN_ACTIVE,
					M0_RPC_CONN_FAILED)
	},
	[M0_RPC_CONN_ACTIVE] = {
		.sd_name      = "Active",
		.sd_allowed   = M0_BITS(M0_RPC_CONN_TERMINATING,
					M0_RPC_CONN_FAILED)
	},
	[M0_RPC_CONN_TERMINATING] = {
		.sd_name      = "Terminating",
		.sd_allowed   = M0_BITS(M0_RPC_CONN_TERMINATED,
					M0_RPC_CONN_FAILED)
	},
	[M0_RPC_CONN_TERMINATED] = {
		.sd_name      = "Terminated",
		.sd_allowed   = M0_BITS(M0_RPC_CONN_FINALISED)
	},
	[M0_RPC_CONN_FAILED] = {
		.sd_flags     = M0_SDF_FAILURE,
		.sd_name      = "Failed",
		.sd_allowed   = M0_BITS(M0_RPC_CONN_FINALISED)
	},
	[M0_RPC_CONN_FINALISED] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Finalised",
	},
};

static const struct m0_sm_conf conn_conf = {
	.scf_name      = "Conn states",
	.scf_nr_states = ARRAY_SIZE(conn_states),
	.scf_state     = conn_states
};

static void conn_state_set(struct m0_rpc_conn *conn, int state)
{
	M0_PRE(conn != NULL);

	M0_LOG(M0_INFO, "%p[%s] %s -> %s", conn,
		m0_rpc_conn_is_snd(conn) ? "SENDER" : "RECEIVER",
		conn_states[conn->c_sm.sm_state].sd_name,
		conn_states[state].sd_name);
	m0_sm_state_set(&conn->c_sm, state);
}

/**
   Checks connection object invariant.

   Function is also called from session_foms.c, hence cannot be static.
 */
M0_INTERNAL bool m0_rpc_conn_invariant(const struct m0_rpc_conn *conn)
{
	struct m0_rpc_session *session0;
	struct m0_tl          *conn_list;
	int                    s0nr; /* Number of sessions with id == 0 */
	bool                   sender_end;
	bool                   recv_end;
	bool                   ok;

	if (conn == NULL || conn->c_rpc_machine == NULL)
		return false;

	session0   = NULL;
	sender_end = m0_rpc_conn_is_snd(conn);
	recv_end   = m0_rpc_conn_is_rcv(conn);
	conn_list  = sender_end ?
			&conn->c_rpc_machine->rm_outgoing_conns :
			&conn->c_rpc_machine->rm_incoming_conns;
	s0nr       = 0;

	/* conditions that should be true irrespective of conn state */
	ok = sender_end != recv_end &&
	     rpc_conn_tlist_contains(conn_list, conn) &&
	     M0_CHECK_EX(m0_tlist_invariant(&rpc_session_tl,
					    &conn->c_sessions)) &&
	     rpc_session_tlist_length(&conn->c_sessions) ==
		conn->c_nr_sessions &&
	     conn_state(conn) <= M0_RPC_CONN_TERMINATED &&
	     /*
	      * Each connection has exactly one session with id SESSION_ID_0.
	      * From m0_rpc_conn_init() to m0_rpc_conn_fini(), this session0 is
	      * either in IDLE state or BUSY state.
	      */
	     m0_tl_forall(rpc_session, s, &conn->c_sessions,
			  ergo(s->s_session_id == SESSION_ID_0,
			       ++s0nr &&
			       (session0 = s) && /*'=' is intentional */
			       M0_IN(session_state(s), (M0_RPC_SESSION_IDLE,
							M0_RPC_SESSION_BUSY)))) &&
	     session0 != NULL &&
	     s0nr == 1;

	if (!ok)
		return false;

	switch (conn_state(conn)) {
	case M0_RPC_CONN_INITIALISED:
		return  conn->c_sender_id == SENDER_ID_INVALID &&
			conn->c_nr_sessions == 1 &&
			session_state(session0) == M0_RPC_SESSION_IDLE;

	case M0_RPC_CONN_CONNECTING:
		return  conn->c_sender_id == SENDER_ID_INVALID &&
			conn->c_nr_sessions == 1;

	case M0_RPC_CONN_ACTIVE:
		return  conn->c_sender_id != SENDER_ID_INVALID &&
			conn->c_nr_sessions >= 1 &&
			ergo(recv_end, conn->c_cob != NULL);

	case M0_RPC_CONN_TERMINATING:
		return  conn->c_nr_sessions == 1 &&
			conn->c_sender_id != SENDER_ID_INVALID;

	case M0_RPC_CONN_TERMINATED:
		return	conn->c_nr_sessions == 1 &&
			conn->c_sender_id != SENDER_ID_INVALID &&
			conn->c_cob == NULL &&
			conn->c_sm.sm_rc == 0;

	case M0_RPC_CONN_FAILED:
		return conn->c_sm.sm_rc != 0;

	default:
		return false;
	}
	/* Should never reach here */
	M0_ASSERT(0);
}

/**
   Returns true iff @conn is sender end of rpc connection.
 */
M0_INTERNAL bool m0_rpc_conn_is_snd(const struct m0_rpc_conn *conn)
{
	return (conn->c_flags & RCF_SENDER_END) == RCF_SENDER_END;
}

/**
   Returns true iff @conn is receiver end of rpc connection.
 */
M0_INTERNAL bool m0_rpc_conn_is_rcv(const struct m0_rpc_conn *conn)
{
	return (conn->c_flags & RCF_RECV_END) == RCF_RECV_END;
}

M0_INTERNAL int m0_rpc_conn_init(struct m0_rpc_conn *conn,
				 struct m0_net_end_point *ep,
				 struct m0_rpc_machine *machine,
				 uint64_t max_rpcs_in_flight)
{
	int rc;

	M0_ENTRY("conn: %p", conn);
	M0_ASSERT(conn != NULL && machine != NULL && ep != NULL);

	M0_SET0(conn);

	m0_rpc_machine_lock(machine);

	conn->c_flags = RCF_SENDER_END;
	m0_rpc_sender_uuid_get(&conn->c_uuid);

	rc = __conn_init(conn, ep, machine, max_rpcs_in_flight);
	if (rc == 0) {
		rpc_conn_tlist_add(&machine->rm_outgoing_conns, conn);
		m0_sm_init(&conn->c_sm, &conn_conf, M0_RPC_CONN_INITIALISED,
			   &machine->rm_sm_grp, NULL /* addb context */);
		M0_LOG(M0_INFO, "%p INITIALISED \n", conn);
	}

	M0_POST(ergo(rc == 0, m0_rpc_conn_invariant(conn) &&
			      conn_state(conn) == M0_RPC_CONN_INITIALISED &&
			      m0_rpc_conn_is_snd(conn)));

	m0_rpc_machine_unlock(machine);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_conn_init);

static int __conn_init(struct m0_rpc_conn      *conn,
		       struct m0_net_end_point *ep,
		       struct m0_rpc_machine   *machine,
		       uint64_t			max_rpcs_in_flight)
{
	int rc;

	M0_ENTRY();
	M0_PRE(conn != NULL && ep != NULL &&
	       m0_rpc_machine_is_locked(machine) &&
	       m0_rpc_conn_is_snd(conn) != m0_rpc_conn_is_rcv(conn));

	conn->c_rpcchan = rpc_chan_get(machine, ep, max_rpcs_in_flight);
	if (conn->c_rpcchan == NULL) {
		M0_SET0(conn);
		M0_RETURN(-ENOMEM);
	}

	conn->c_rpc_machine = machine;
	conn->c_sender_id   = SENDER_ID_INVALID;
	conn->c_cob         = NULL;
	conn->c_service     = NULL;
	conn->c_nr_sessions = 0;

	rpc_session_tlist_init(&conn->c_sessions);
	rpc_conn_tlink_init(conn);

	rc = session_zero_attach(conn);
	if (rc != 0) {
		__conn_fini(conn);
		M0_SET0(conn);
	}
	M0_RETURN(rc);
}

static int session_zero_attach(struct m0_rpc_conn *conn)
{
	struct m0_rpc_slot    *slot;
	struct m0_rpc_session *session;
	int                    rc;
	int                    i;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL && m0_rpc_machine_is_locked(conn->c_rpc_machine));

	RPC_ALLOC_PTR(session, CONN_SESSION_ZERO_ATTACH, &m0_rpc_addb_ctx);
	if (session == NULL)
		M0_RETURN(-ENOMEM);

	rc = m0_rpc_session_init_locked(session, conn, 10 /* NR_SLOTS */);
	if (rc != 0) {
		m0_free(session);
		M0_RETURN(rc);
	}

	session->s_session_id = SESSION_ID_0;

	/* It is done as there is no need to establish session0 explicitly
	 * and direct transition from INITIALISED => IDLE is not allowed.
	 */
	session_state_set(session, M0_RPC_SESSION_ESTABLISHING);
	session_state_set(session, M0_RPC_SESSION_IDLE);

	for (i = 0; i < session->s_nr_slots; ++i) {
		slot = session->s_slot_table[i];
		M0_ASSERT(slot != NULL &&
			  slot->sl_ops != NULL &&
			  slot->sl_ops->so_slot_idle != NULL);
		slot->sl_ops->so_slot_idle(slot);
	}
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_RETURN(0);
}

static void __conn_fini(struct m0_rpc_conn *conn)
{
	M0_ENTRY("conn: %p", conn);
	M0_ASSERT(conn != NULL);

	rpc_chan_put(conn->c_rpcchan);

	rpc_session_tlist_fini(&conn->c_sessions);
	rpc_conn_tlink_fini(conn);
	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_rcv_conn_init(struct m0_rpc_conn *conn,
				     struct m0_net_end_point *ep,
				     struct m0_rpc_machine *machine,
				     const struct m0_rpc_sender_uuid *uuid)
{
	int rc;

	M0_ENTRY("conn: %p, ep_addr: %s, machine: %p,"
		 " sender_uuid: %llu", conn, (char *)ep->nep_addr,
		 machine, (unsigned long long)uuid->su_uuid);
	M0_ASSERT(conn != NULL && ep != NULL);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	M0_SET0(conn);

	conn->c_flags = RCF_RECV_END;
	conn->c_uuid = *uuid;

	rc = __conn_init(conn, ep, machine, 8 /* max packets in flight */);
	if (rc == 0) {
		rpc_conn_tlist_add(&machine->rm_incoming_conns, conn);
		m0_sm_init(&conn->c_sm, &conn_conf, M0_RPC_CONN_INITIALISED,
			   &machine->rm_sm_grp, NULL /* addb context */);
		M0_LOG(M0_INFO, "%p INITIALISED \n", conn);
	}

	M0_POST(ergo(rc == 0, m0_rpc_conn_invariant(conn) &&
			      conn_state(conn) == M0_RPC_CONN_INITIALISED &&
			      m0_rpc_conn_is_rcv(conn)));
	M0_POST(m0_rpc_machine_is_locked(machine));

	M0_RETURN(rc);
}

M0_INTERNAL void m0_rpc_conn_fini(struct m0_rpc_conn *conn)
{
	struct m0_rpc_machine *machine;
	struct m0_rpc_session *session0;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL && conn->c_rpc_machine != NULL);

	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	session0 = m0_rpc_conn_session0(conn);
	m0_sm_timedwait(&session0->s_sm, M0_BITS(M0_RPC_SESSION_IDLE),
			M0_TIME_NEVER);

	m0_rpc_conn_fini_locked(conn);
	/* Don't look in conn after this point */
	m0_rpc_machine_unlock(machine);

	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_conn_fini);

M0_INTERNAL void m0_rpc_conn_fini_locked(struct m0_rpc_conn *conn)
{
	M0_ENTRY("conn: %p", conn);
	M0_PRE(m0_rpc_machine_is_locked(conn->c_rpc_machine));

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_PRE(M0_IN(conn_state(conn), (M0_RPC_CONN_TERMINATED,
					M0_RPC_CONN_FAILED,
					M0_RPC_CONN_INITIALISED)));

	rpc_conn_tlist_del(conn);
	session_zero_detach(conn);
	__conn_fini(conn);
	conn_state_set(conn, M0_RPC_CONN_FINALISED);
	m0_sm_fini(&conn->c_sm);
	M0_LOG(M0_INFO, "%p FINALISED \n", conn);
	M0_SET0(conn);
	M0_LEAVE();
}

static void session_zero_detach(struct m0_rpc_conn *conn)
{
	struct m0_rpc_session *session;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL);
	M0_PRE(m0_rpc_machine_is_locked(conn->c_rpc_machine));

	session = m0_rpc_conn_session0(conn);
	M0_ASSERT(session_state(session) == M0_RPC_SESSION_IDLE);

	session_state_set(session, M0_RPC_SESSION_TERMINATING);
	m0_rpc_session_del_slots_from_ready_list(session);
	session_state_set(session, M0_RPC_SESSION_TERMINATED);
	m0_rpc_session_fini_locked(session);
	m0_free(session);

	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_conn_timedwait(struct m0_rpc_conn *conn, uint64_t states,
				      const m0_time_t timeout)
{
	int rc;

	M0_ENTRY("conn: %p, abs_timeout: [%llu:%llu]", conn,
		 (unsigned long long)m0_time_seconds(timeout),
		 (unsigned long long)m0_time_nanoseconds(timeout));
	M0_PRE(conn != NULL && conn->c_rpc_machine != NULL);

	m0_rpc_machine_lock(conn->c_rpc_machine);
	M0_ASSERT(m0_rpc_conn_invariant(conn));

	rc = m0_sm_timedwait(&conn->c_sm, states, timeout);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	m0_rpc_machine_unlock(conn->c_rpc_machine);

	M0_RETURN(rc ?: conn->c_sm.sm_rc);
}
M0_EXPORTED(m0_rpc_conn_timedwait);

M0_INTERNAL void m0_rpc_conn_add_session(struct m0_rpc_conn *conn,
					 struct m0_rpc_session *session)
{
	rpc_session_tlist_add(&conn->c_sessions, session);
	conn->c_nr_sessions++;
}

M0_INTERNAL void m0_rpc_conn_remove_session(struct m0_rpc_session *session)
{
	M0_ASSERT(session->s_conn->c_nr_sessions > 0);

	rpc_session_tlist_del(session);
	session->s_conn->c_nr_sessions--;
}

/**
   Searches and returns session with session id 0.
   Note: Every rpc connection always has exactly one active session with
   session id 0.
 */
M0_INTERNAL struct m0_rpc_session *m0_rpc_conn_session0(const struct m0_rpc_conn
							*conn)
{
	struct m0_rpc_session *session0;

	session0 = m0_rpc_session_search(conn, SESSION_ID_0);

	M0_ASSERT(session0 != NULL);
	return session0;
}

M0_INTERNAL struct m0_rpc_session *m0_rpc_session_search(const struct
							 m0_rpc_conn *conn,
							 uint64_t session_id)
{
	struct m0_rpc_session *session;

	M0_ENTRY("conn: %p, session_id: %llu", conn,
		 (unsigned long long) session_id);
	M0_ASSERT(conn != NULL);

	m0_tl_for(rpc_session, &conn->c_sessions, session) {
		if (session->s_session_id == session_id)
			return session;
	} m0_tl_endfor;
	M0_LEAVE("session: (nil)");
	return NULL;
}

M0_INTERNAL int m0_rpc_conn_create(struct m0_rpc_conn *conn,
				   struct m0_net_end_point *ep,
				   struct m0_rpc_machine *rpc_machine,
				   uint64_t max_rpcs_in_flight,
				   uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY("conn: %p, ep_addr: %s, machine: %p,"
		 " max_rpcs_in_flight: %llu, timeout_sec: %u", conn,
		 (char *)ep->nep_addr, rpc_machine,
		 (unsigned long long)max_rpcs_in_flight, timeout_sec);

	if (M0_FI_ENABLED("fake_error"))
		M0_RETURN(-EINVAL);

	rc = m0_rpc_conn_init(conn, ep, rpc_machine, max_rpcs_in_flight);
	if (rc == 0) {
		rc = m0_rpc_conn_establish_sync(conn, timeout_sec);
		if (rc != 0)
			m0_rpc_conn_fini(conn);
	}
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_conn_establish_sync(struct m0_rpc_conn *conn,
					   uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY();

	rc = m0_rpc_conn_establish(conn);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_rpc_conn_timedwait(conn, M0_BITS(M0_RPC_CONN_ACTIVE,
						 M0_RPC_CONN_FAILED),
				   m0_time_from_now(timeout_sec, 0));

	M0_POST(M0_IN(conn_state(conn),
		      (M0_RPC_CONN_ACTIVE, M0_RPC_CONN_FAILED)));
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_conn_establish_sync);

M0_INTERNAL int m0_rpc_conn_establish(struct m0_rpc_conn *conn)
{
	struct m0_fop         *fop;
	struct m0_rpc_session *session_0;
	struct m0_rpc_machine *machine;
	int                    rc;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL && conn->c_rpc_machine != NULL);

	if (M0_FI_ENABLED("fake_error"))
		M0_RETURN(-EINVAL);

	machine = conn->c_rpc_machine;

	fop = m0_fop_alloc(&m0_rpc_fop_conn_establish_fopt, NULL);
	if (fop == NULL) {
		m0_rpc_machine_lock(machine);
		conn_failed(conn, -ENOMEM);
		m0_rpc_machine_unlock(machine);
		M0_RETURN(-ENOMEM);
	}

	m0_rpc_machine_lock(machine);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_INITIALISED &&
	          m0_rpc_conn_is_snd(conn));

	/* m0_rpc_fop_conn_establish FOP doesn't contain any data. */

	session_0 = m0_rpc_conn_session0(conn);

	rc = m0_rpc__fop_post(fop, session_0, &conn_establish_item_ops);
	if (rc == 0)
		conn_state_set(conn, M0_RPC_CONN_CONNECTING);
	else
		conn_failed(conn, rc);
	m0_fop_put(fop);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_POST(ergo(rc != 0, conn_state(conn) == M0_RPC_CONN_FAILED));
	M0_ASSERT(ergo(rc == 0, conn_state(conn) == M0_RPC_CONN_CONNECTING));

	m0_rpc_machine_unlock(machine);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_conn_establish);

/**
   Moves conn to M0_RPC_CONN_FAILED state, setting error code to error.
 */
static void conn_failed(struct m0_rpc_conn *conn, int32_t error)
{
	struct m0_rpc_session *session0;

	M0_ENTRY("conn: %p, error: %d", conn, error);
	m0_sm_fail(&conn->c_sm, M0_RPC_CONN_FAILED, error);

	session0 = m0_rpc_conn_session0(conn);
	m0_rpc_session_del_slots_from_ready_list(session0);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_conn_establish_reply_received(struct m0_rpc_item *item)
{
	struct m0_rpc_fop_conn_establish_rep *reply;
	struct m0_rpc_machine                *machine;
	struct m0_rpc_conn                   *conn;
	struct m0_rpc_item                   *reply_item;
	struct m0_fop                        *reply_fop;
	int32_t                               rc;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL &&
	       item->ri_session != NULL &&
	       item->ri_session->s_session_id == SESSION_ID_0);

	reply_item = item->ri_reply;
	rc         = item->ri_error;
	M0_PRE(ergo(rc == 0, reply_item != NULL &&
			     item->ri_session == reply_item->ri_session));

	conn    = item->ri_session->s_conn;
	machine = conn->c_rpc_machine;

	M0_PRE(m0_rpc_machine_is_locked(machine));
	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_PRE(conn_state(conn) == M0_RPC_CONN_CONNECTING);

	if (rc == 0) {
		reply_fop = m0_rpc_item_to_fop(reply_item);
		reply     = m0_fop_data(reply_fop);

		rc = reply->rcer_rc;
		if (rc == 0) {
			if (reply->rcer_sender_id != SENDER_ID_INVALID) {
				conn->c_sender_id = reply->rcer_sender_id;
				conn_state_set(conn, M0_RPC_CONN_ACTIVE);
			} else
				rc = -EPROTO;
		}
	}

	if (rc != 0)
		conn_failed(conn, rc);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_ASSERT(M0_IN(conn_state(conn), (M0_RPC_CONN_FAILED,
					   M0_RPC_CONN_ACTIVE)));
	M0_LEAVE();
}

int m0_rpc_conn_destroy(struct m0_rpc_conn *conn, uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY("conn: %p, timeout: %u secs", conn, timeout_sec);

	rc = m0_rpc_conn_terminate_sync(conn, timeout_sec);
	m0_rpc_conn_fini(conn);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_conn_destroy);

M0_INTERNAL int m0_rpc_conn_terminate_sync(struct m0_rpc_conn *conn,
					   uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY();

	rc = m0_rpc_conn_terminate(conn);
	if (rc != 0) {
		M0_RETURN(rc);
	}

	rc = m0_rpc_conn_timedwait(conn, M0_BITS(M0_RPC_CONN_TERMINATED,
						 M0_RPC_CONN_FAILED),
				   m0_time_from_now(timeout_sec, 0));

	M0_ASSERT(M0_IN(conn_state(conn), (M0_RPC_CONN_TERMINATED,
					   M0_RPC_CONN_FAILED)));
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_conn_terminate_sync);

M0_INTERNAL int m0_rpc_conn_terminate(struct m0_rpc_conn *conn)
{
	struct m0_fop                    *fop;
	struct m0_rpc_fop_conn_terminate *args;
	struct m0_rpc_session            *session_0;
	struct m0_rpc_machine            *machine;
	int                               rc;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL);
	M0_PRE(conn->c_service == NULL);
	M0_PRE(conn->c_rpc_machine != NULL);

	fop = m0_fop_alloc(&m0_rpc_fop_conn_terminate_fopt, NULL);
	machine = conn->c_rpc_machine;
	m0_rpc_machine_lock(machine);
	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_PRE(M0_IN(conn_state(conn), (M0_RPC_CONN_ACTIVE,
					M0_RPC_CONN_TERMINATING)));
	M0_PRE(conn->c_nr_sessions == 1);
	if (fop == NULL) {
		/* see note [^1] at the end of function */
		rc = -ENOMEM;
		conn_failed(conn, rc);
		m0_rpc_machine_unlock(machine);
		M0_RETERR(rc, "conn_terminate_fop: Memory Allocation");
	}
	if (conn_state(conn) == M0_RPC_CONN_TERMINATING) {
		m0_fop_put(fop);
		m0_rpc_machine_unlock(machine);
		M0_RETURN(0);
	}
	args = m0_fop_data(fop);
	args->ct_sender_id = conn->c_sender_id;

	session_0 = m0_rpc_conn_session0(conn);
	rc = m0_rpc__fop_post(fop, session_0, &conn_terminate_item_ops);
	if (rc == 0) {
		conn_state_set(conn, M0_RPC_CONN_TERMINATING);
	} else {
		conn_failed(conn, rc);
	}
	m0_fop_put(fop);
	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_POST(ergo(rc != 0, conn_state(conn) == M0_RPC_CONN_FAILED));
	/*
	 * CAUTION: Following assertion is not guaranteed as soon as
	 * rpc_machine is unlocked.
	 */
	M0_ASSERT(ergo(rc == 0, conn_state(conn) == M0_RPC_CONN_TERMINATING));

	m0_rpc_machine_unlock(machine);
	/* see m0_rpc_conn_terminate_reply_received() */
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_conn_terminate);
/*
 * m0_rpc_conn_terminate [^1]
 * There are two choices here:
 *
 * 1. leave conn in TERMNATING state FOREVER.
 *    Then when to fini/cleanup conn.
 *
 * 2. Move conn to FAILED state.
 *    For this conn the receiver side state will still
 *    continue to exist. And receiver can send one-way
 *    items, that will be received on sender i.e. current node.
 *    Current code will drop such items. When/how to fini and
 *    cleanup receiver side state? XXX
 *
 * For now, later is chosen. This can be changed in future
 * to alternative 1, iff required.
 */

M0_INTERNAL void m0_rpc_conn_terminate_reply_received(struct m0_rpc_item *item)
{
	struct m0_rpc_fop_conn_terminate_rep *reply;
	struct m0_fop                        *reply_fop;
	struct m0_rpc_conn                   *conn;
	struct m0_rpc_machine                *machine;
	struct m0_rpc_item                   *reply_item;
	int32_t                               rc;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL &&
	       item->ri_session != NULL &&
	       item->ri_session->s_session_id == SESSION_ID_0);

	reply_item = item->ri_reply;
	rc         = item->ri_error;

	M0_ASSERT(ergo(rc == 0, reply_item != NULL &&
				item->ri_session == reply_item->ri_session));

	conn    = item->ri_session->s_conn;
	machine = conn->c_rpc_machine;

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_PRE(m0_rpc_machine_is_locked(machine));
	M0_PRE(conn_state(conn) == M0_RPC_CONN_TERMINATING);

	if (rc == 0) {
		reply_fop = m0_rpc_item_to_fop(reply_item);
		reply     = m0_fop_data(reply_fop);

		rc = reply->ctr_rc;
		if (rc == 0) {
			if (conn->c_sender_id == reply->ctr_sender_id)
				conn_state_set(conn, M0_RPC_CONN_TERMINATED);
			else
				/* XXX generate ADDB record here. */
				rc = -EPROTO;
		}
	}

	if (rc != 0)
		conn_failed(conn, rc);

	M0_POST(m0_rpc_conn_invariant(conn));
	M0_POST(M0_IN(conn_state(conn), (M0_RPC_CONN_TERMINATED,
					 M0_RPC_CONN_FAILED)));
	M0_POST(m0_rpc_machine_is_locked(machine));
}

M0_INTERNAL int m0_rpc_conn_cob_lookup(struct m0_cob_domain *dom,
				       uint64_t sender_id,
				       struct m0_cob **out, struct m0_db_tx *tx)
{
	struct m0_cob *root_session_cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	M0_ENTRY("cob_dom: %p, sender_id: %llu", dom,
		 (unsigned long long)sender_id);
	M0_PRE(sender_id != SENDER_ID_INVALID);

	rc = m0_rpc_root_session_cob_get(dom, &root_session_cob, tx);
	if (rc == 0) {
		sprintf(name, conn_cob_name_fmt, (unsigned long)sender_id);

		rc = m0_rpc_cob_lookup_helper(dom, root_session_cob, name,
					      out, tx);
		m0_cob_put(root_session_cob);
	}
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_conn_cob_create(struct m0_cob_domain *dom,
				       uint64_t sender_id,
				       struct m0_cob **out, struct m0_db_tx *tx)
{
	struct m0_cob *conn_cob;
	struct m0_cob *root_session_cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	M0_ENTRY("cob_dom: %p, sender_id: %llu", dom,
		 (unsigned long long)sender_id);
	M0_PRE(dom != NULL && out != NULL);
	M0_PRE(sender_id != SENDER_ID_INVALID);

	sprintf(name, conn_cob_name_fmt, (unsigned long)sender_id);
	*out = NULL;

	rc = m0_rpc_cob_lookup_helper(dom, NULL, M0_COB_SESSIONS_NAME,
					&root_session_cob, tx);
	if (rc != 0) {
		M0_ASSERT(rc != -EEXIST);
		M0_RETURN(rc);
	}
	rc = m0_rpc_cob_create_helper(dom, root_session_cob, name, &conn_cob,
					tx);
	if (rc == 0)
		*out = conn_cob;
	m0_cob_put(root_session_cob);
	M0_RETURN(rc);
}

static int conn_persistent_state_create(struct m0_cob_domain *dom,
					uint64_t              sender_id,
					struct m0_cob       **conn_cob_out,
					struct m0_cob       **session0_cob_out,
					struct m0_cob       **slot0_cob_out,
					struct m0_db_tx      *tx)
{
	struct m0_cob *conn_cob     = NULL;
	struct m0_cob *session0_cob = NULL;
	struct m0_cob *slot0_cob    = NULL;
	int            rc;

	M0_ENTRY("cob_dom: %p, sender_id: %llu", dom,
		 (unsigned long long)sender_id);
	*conn_cob_out = *session0_cob_out = *slot0_cob_out = NULL;

	rc = m0_rpc_conn_cob_create(dom, sender_id, &conn_cob, tx) ?:
	     m0_rpc_session_cob_create(conn_cob, SESSION_ID_0,
				       &session0_cob, tx)          ?:
	     m0_rpc_slot_cob_create(session0_cob, 0 /*SLOT0*/, 0 /*SLOT_GEN*/,
				    &slot0_cob, tx);

	if (rc == 0) {
		*conn_cob_out     = conn_cob;
		*session0_cob_out = session0_cob;
		*slot0_cob_out    = slot0_cob;
	} else {
		if (slot0_cob != NULL)    m0_cob_put(slot0_cob);
		if (session0_cob != NULL) m0_cob_put(session0_cob);
		if (conn_cob != NULL)     m0_cob_put(conn_cob);
	}
	M0_RETURN(rc);
}

static int conn_persistent_state_attach(struct m0_rpc_conn *conn,
				        uint64_t            sender_id,
				        struct m0_db_tx    *tx)
{
	struct m0_rpc_session *session0;
	struct m0_rpc_slot    *slot0;
	struct m0_cob         *conn_cob;
	struct m0_cob         *session0_cob;
	struct m0_cob         *slot0_cob;
	struct m0_cob_domain  *dom;
	int                    rc;

	M0_ENTRY("conn: %p, sender_id: %llu", conn,
		 (unsigned long long)sender_id);
	M0_PRE(conn != NULL && m0_rpc_conn_invariant(conn) &&
	       conn_state(conn) == M0_RPC_CONN_CONNECTING);

	dom = conn->c_rpc_machine->rm_dom;
	rc = conn_persistent_state_create(dom, sender_id,
					  &conn_cob, &session0_cob, &slot0_cob,
					  tx);
	if (rc != 0)
		M0_RETURN(rc);

	M0_ASSERT(conn_cob != NULL && session0_cob != NULL &&
			slot0_cob != NULL);
	conn->c_cob = conn_cob;

	session0 = m0_rpc_conn_session0(conn);
	session0->s_cob = session0_cob;

	slot0 = session0->s_slot_table[0];
	M0_ASSERT(slot0 != NULL);
	slot0->sl_cob = slot0_cob;

	M0_RETURN(0);
}

M0_INTERNAL int m0_rpc_rcv_conn_establish(struct m0_rpc_conn *conn)
{
	struct m0_rpc_machine *machine;
	struct m0_db_tx        tx;
	uint64_t               sender_id;
	int                    rc;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL);

	machine = conn->c_rpc_machine;
	M0_PRE(m0_rpc_machine_is_locked(machine));
	M0_PRE(machine->rm_dom != NULL);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_INITIALISED &&
		  m0_rpc_conn_is_rcv(conn));

	conn_state_set(conn, M0_RPC_CONN_CONNECTING);
	rc = m0_db_tx_init(&tx, machine->rm_dom->cd_dbenv, 0);
	if (rc == 0) {
		sender_id = uuid_generate();
		rc = conn_persistent_state_attach(conn, sender_id, &tx);
		if (rc == 0)
			rc = m0_db_tx_commit(&tx);
		else
			m0_db_tx_abort(&tx);
	}

	if (rc == 0) {
		conn->c_sender_id = sender_id;
		conn_state_set(conn, M0_RPC_CONN_ACTIVE);
	} else
		conn_failed(conn, rc);

	M0_POST(m0_rpc_machine_is_locked(machine));
	M0_RETURN(rc);
}

static int conn_persistent_state_destroy(struct m0_rpc_conn *conn,
					 struct m0_db_tx    *tx)
{
	struct m0_rpc_session *session0;
	struct m0_rpc_slot    *slot0;

	M0_ENTRY("conn: %p", conn);
	session0 = m0_rpc_conn_session0(conn);
	slot0    = session0->s_slot_table[0];

	M0_ASSERT(slot0 != NULL && m0_rpc_slot_invariant(slot0));

	M0_ASSERT(conn->c_cob != NULL && session0->s_cob != NULL &&
		  slot0->sl_cob != NULL);

	m0_cob_delete_put(conn->c_cob, tx);
	m0_cob_delete_put(session0->s_cob, tx);
	m0_cob_delete_put(slot0->sl_cob, tx);

	conn->c_cob = session0->s_cob = slot0->sl_cob = NULL;
	M0_RETURN(0);
}

M0_INTERNAL int m0_rpc_rcv_conn_terminate(struct m0_rpc_conn *conn)
{
	struct m0_rpc_machine *machine;
	struct m0_db_tx        tx;
	int                    rc;

	M0_ENTRY("conn: %p", conn);
	M0_PRE(conn != NULL);

	machine = conn->c_rpc_machine;
	M0_ASSERT(m0_rpc_machine_is_locked(machine));

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_ACTIVE);
	M0_ASSERT(m0_rpc_conn_is_rcv(conn));

	if (conn->c_nr_sessions > 1) {
		M0_RETURN(-EBUSY);
	}

	conn_state_set(conn, M0_RPC_CONN_TERMINATING);

	rc = m0_db_tx_init(&tx, conn->c_cob->co_dom->cd_dbenv, 0);
	if (rc == 0) {
		rc = conn_persistent_state_destroy(conn, &tx);
		if (rc == 0)
			rc = m0_db_tx_commit(&tx);
		else
			m0_db_tx_abort(&tx);
	}

	if (rc != 0)
		conn_failed(conn, rc);
	/*
	 * Note: conn is not moved to TERMINATED state even if operation is
	 * successful. This is required to be able to send successful conn
	 * terminate reply.
	 */
	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_POST(ergo(rc == 0, conn_state(conn) == M0_RPC_CONN_TERMINATING));
	M0_POST(ergo(rc != 0, conn_state(conn) == M0_RPC_CONN_FAILED));
	/* In-core state will be cleaned up by
	   m0_rpc_conn_terminate_reply_sent() */
	M0_ASSERT(m0_rpc_machine_is_locked(machine));
	M0_RETURN(rc);
}

M0_INTERNAL void m0_rpc_conn_terminate_reply_sent(struct m0_rpc_conn *conn)
{
	struct m0_rpc_machine *machine;

	M0_ENTRY("conn: %p", conn);
	M0_ASSERT(conn != NULL);

	machine = conn->c_rpc_machine;
	M0_ASSERT(m0_rpc_machine_is_locked(machine));

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_TERMINATING);

	conn_state_set(conn, M0_RPC_CONN_TERMINATED);
	conn->c_sm.sm_rc = 0;

	M0_ASSERT(m0_rpc_conn_invariant(conn));

	m0_rpc_conn_fini_locked(conn);
	m0_free(conn);

	M0_POST(m0_rpc_machine_is_locked(machine));
	M0_LEAVE();
}

M0_INTERNAL bool m0_rpc_item_is_conn_establish(const struct m0_rpc_item *item)
{
	return item->ri_type->rit_opcode == M0_RPC_CONN_ESTABLISH_OPCODE;
}

M0_INTERNAL bool m0_rpc_item_is_conn_terminate(const struct m0_rpc_item *item)
{
	return item->ri_type->rit_opcode == M0_RPC_CONN_TERMINATE_OPCODE;
}

#ifndef __KERNEL__
/**
   Just for debugging purpose. Useful in gdb.

   dir = 1, to print incoming conn list
   dir = 0, to print outgoing conn list
 */
M0_INTERNAL int m0_rpc_machine_conn_list_print(struct m0_rpc_machine *machine,
					       int dir)
{
	struct m0_tl       *list;
	struct m0_rpc_conn *conn;

	list = dir ? &machine->rm_incoming_conns : &machine->rm_outgoing_conns;

	m0_tl_for(rpc_conn, list, conn) {
		printf("CONN: %p id %llu state %x\n", conn,
				(unsigned long long)conn->c_sender_id,
				conn_state(conn));
	} m0_tl_endfor;
	return 0;
}

M0_INTERNAL int m0_rpc_conn_session_list_print(const struct m0_rpc_conn *conn)
{
	struct m0_rpc_session *session;

	m0_tl_for(rpc_session, &conn->c_sessions, session) {
		printf("session %p id %llu state %x\n", session,
			(unsigned long long)session->s_session_id,
			session_state(session));
	} m0_tl_endfor;
	return 0;
}

/** @} */
#endif
