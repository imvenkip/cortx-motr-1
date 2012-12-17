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
 * Original creation date: 03/17/2011
 */

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "mero/magic.h"
#include "fop/fop.h"
#include "lib/arith.h"             /* M0_CNT_DEC */
#include "lib/finject.h"
#include "db/db.h"
#include "dtm/verno.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{

   This file defines functions related to m0_rpc_session.
 */

static void snd_slot_idle(struct m0_rpc_slot *slot);

static void snd_item_consume(struct m0_rpc_item *item);

static void snd_reply_consume(struct m0_rpc_item *req,
				 struct m0_rpc_item *reply);

static void rcv_slot_idle(struct m0_rpc_slot *slot);

static void rcv_item_consume(struct m0_rpc_item *item);

static void rcv_reply_consume(struct m0_rpc_item *req,
			      struct m0_rpc_item *reply);

/**
   Creates cob hierarchy that represents persistent state of @session.
 */
static int session_persistent_state_attach(struct m0_rpc_session *session,
					   uint64_t               session_id,
					   struct m0_db_tx       *tx);

/**
  Deletes all the cobs associated with the session and slots belonging to
  the session
 */
static int session_persistent_state_destroy(struct m0_rpc_session *session,
					    struct m0_db_tx       *tx);

static int nr_active_items_count(const struct m0_rpc_session *session);

static int slot_table_alloc_and_init(struct m0_rpc_session *session);
static void __session_fini(struct m0_rpc_session *session);
static void session_failed(struct m0_rpc_session *session, int32_t error);
static void session_idle_x_busy(struct m0_rpc_session *session);

static const struct m0_rpc_slot_ops snd_slot_ops = {
	.so_slot_idle = snd_slot_idle,
	.so_item_consume = snd_item_consume,
	.so_reply_consume = snd_reply_consume
};

static const struct m0_rpc_slot_ops rcv_slot_ops = {
	.so_slot_idle = rcv_slot_idle,
	.so_item_consume = rcv_item_consume,
	.so_reply_consume = rcv_reply_consume
};

/**
   Container of session_establish fop.

   Required only on sender to obtain pointer to session being established,
   when reply to session_establish is received.
 */
struct fop_session_establish_ctx {
	/** A fop instance of type m0_rpc_fop_session_establish_fopt */
	struct m0_fop          sec_fop;

	/** sender side session object */
	struct m0_rpc_session *sec_session;
};

static void session_establish_fop_release(struct m0_ref *ref);

static const struct m0_rpc_item_ops session_establish_item_ops = {
	.rio_replied = m0_rpc_session_establish_reply_received,
};

static const struct m0_rpc_item_ops session_terminate_item_ops = {
	.rio_replied = m0_rpc_session_terminate_reply_received,
};

M0_TL_DESCR_DEFINE(ready_slot, "ready-slots", M0_INTERNAL, struct m0_rpc_slot,
		   sl_link, sl_magic, M0_RPC_SLOT_MAGIC,
		   M0_RPC_SLOT_HEAD_MAGIC);
M0_TL_DEFINE(ready_slot, M0_INTERNAL, struct m0_rpc_slot);

M0_TL_DESCR_DEFINE(rpc_session, "rpc-sessions", M0_INTERNAL,
		   struct m0_rpc_session, s_link, s_magic, M0_RPC_SESSION_MAGIC,
		   M0_RPC_SESSION_HEAD_MAGIC);
M0_TL_DEFINE(rpc_session, M0_INTERNAL, struct m0_rpc_session);

static const struct m0_sm_state_descr session_states[] = {
	[M0_RPC_SESSION_INITIALISED] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Initialised",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_ESTABLISHING,
					M0_RPC_SESSION_FINALISED,
					M0_RPC_SESSION_FAILED)
	},
	[M0_RPC_SESSION_ESTABLISHING] = {
		.sd_name      = "Establishing",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_IDLE,
					M0_RPC_SESSION_FAILED)
	},
	[M0_RPC_SESSION_IDLE] = {
		.sd_name      = "Idle",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_TERMINATING,
					M0_RPC_SESSION_BUSY,
					M0_RPC_SESSION_FAILED)
	},
	[M0_RPC_SESSION_BUSY] = {
		.sd_name      = "Busy",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_IDLE)
	},
	[M0_RPC_SESSION_TERMINATING] = {
		.sd_name      = "Terminating",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_TERMINATED,
					M0_RPC_SESSION_FAILED)
	},
	[M0_RPC_SESSION_TERMINATED] = {
		.sd_name      = "Terminated",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_FINALISED)
	},
	[M0_RPC_SESSION_FAILED] = {
		.sd_flags     = M0_SDF_FAILURE,
		.sd_name      = "Failed",
		.sd_allowed   = M0_BITS(M0_RPC_SESSION_FINALISED)
	},
	[M0_RPC_SESSION_FINALISED] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Finalised",
	},
};

static const struct m0_sm_conf session_conf = {
	.scf_name      = "Session states",
	.scf_nr_states = ARRAY_SIZE(session_states),
	.scf_state     = session_states
};

M0_INTERNAL void session_state_set(struct m0_rpc_session *session, int state)
{
	M0_PRE(session != NULL);

	M0_LOG(M0_INFO, "Session %p: %s -> %s", session,
		session_states[session->s_sm.sm_state].sd_name,
		session_states[state].sd_name);
	m0_sm_state_set(&session->s_sm, state);
}

M0_INTERNAL int session_state(const struct m0_rpc_session *session)
{
	return session->s_sm.sm_state;
}

M0_INTERNAL struct m0_rpc_machine *session_machine(const struct m0_rpc_session
						   *s)
{
	return s->s_conn->c_rpc_machine;
}

/**
   The routine is also called from session_foms.c, hence can't be static
 */
M0_INTERNAL bool m0_rpc_session_invariant(const struct m0_rpc_session *session)
{
	struct m0_rpc_slot *slot;
	bool                ok;
	int                 i;

	ok = session != NULL &&
	     session->s_conn != NULL &&
	     session->s_nr_slots > 0 &&
	     nr_active_items_count(session) == session->s_nr_active_items &&
	     rpc_session_tlist_contains(&session->s_conn->c_sessions,
			             session) &&
	     ergo(session->s_session_id != SESSION_ID_0,
		  session->s_conn->c_nr_sessions > 0);

	if (!ok)
		return false;

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		ok = m0_rpc_slot_invariant(slot) &&
		     ergo(M0_IN(session_state(session),
				(M0_RPC_SESSION_INITIALISED,
				 M0_RPC_SESSION_ESTABLISHING,
				 M0_RPC_SESSION_TERMINATING,
				 M0_RPC_SESSION_TERMINATED,
				 M0_RPC_SESSION_FAILED)),
		          !ready_slot_tlink_is_in(slot));
		    /* A slot cannot be on ready slots list if session is
		       in one of above states */
		if (!ok)
			return false;
	}

	switch (session_state(session)) {
	case M0_RPC_SESSION_INITIALISED:
	case M0_RPC_SESSION_ESTABLISHING:
		return session->s_session_id == SESSION_ID_INVALID &&
		       m0_rpc_session_is_idle(session);

	case M0_RPC_SESSION_TERMINATED:
		return  session->s_cob == NULL &&
			session->s_session_id <= SESSION_ID_MAX &&
			m0_rpc_session_is_idle(session);

	case M0_RPC_SESSION_IDLE:
	case M0_RPC_SESSION_TERMINATING:
		return m0_rpc_session_is_idle(session) &&
		       session->s_session_id <= SESSION_ID_MAX;

	case M0_RPC_SESSION_BUSY:
		return !m0_rpc_session_is_idle(session) &&
		       session->s_session_id <= SESSION_ID_MAX;

	case M0_RPC_SESSION_FAILED:
		return session->s_sm.sm_rc != 0;

	default:
		return false;
	}
	/* Should never reach here */
	M0_ASSERT(0);
}

M0_INTERNAL bool m0_rpc_session_is_idle(const struct m0_rpc_session *session)
{
	return session->s_nr_active_items == 0 &&
	       session->s_hold_cnt == 0;
}

static int nr_active_items_count(const struct m0_rpc_session *session)
{
	struct m0_rpc_slot *slot;
	struct m0_rpc_item *item;
	int                 i;
	int                 count = 0;

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		for_each_item_in_slot(item, slot) {
			if (item_is_active(item))
				count++;
		} end_for_each_item_in_slot;
	}
	return count;
}


M0_INTERNAL int m0_rpc_session_init(struct m0_rpc_session *session,
				    struct m0_rpc_conn *conn, uint32_t nr_slots)
{
	struct m0_rpc_machine *machine;
	int                    rc;

	M0_ENTRY("session: %p, conn: %p, nr_slots: %u", session,
		 conn, nr_slots);
	M0_PRE(session != NULL && conn != NULL && nr_slots >= 1);

	machine = conn->c_rpc_machine;
	M0_PRE(machine != NULL);

	m0_rpc_machine_lock(machine);

	rc = m0_rpc_session_init_locked(session, conn, nr_slots);

	m0_rpc_machine_unlock(machine);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_session_init);

M0_INTERNAL int m0_rpc_session_init_locked(struct m0_rpc_session *session,
					   struct m0_rpc_conn *conn,
					   uint32_t nr_slots)
{
	int rc;

	M0_ENTRY("session: %p, conn: %p, nr_slots: %u", session,
		 conn, nr_slots);
	M0_PRE(session != NULL && conn != NULL && nr_slots >= 1);
	M0_PRE(m0_rpc_machine_is_locked(conn->c_rpc_machine));

	M0_SET0(session);

	session->s_session_id          = SESSION_ID_INVALID;
	session->s_conn                = conn;
	session->s_nr_slots            = nr_slots;
	session->s_slot_table_capacity = nr_slots;
	session->s_cob                 = NULL;

	rpc_session_tlink_init(session);
	m0_list_init(&session->s_unbound_items);
	ready_slot_tlist_init(&session->s_ready_slots);

	rc = slot_table_alloc_and_init(session);
	if (rc == 0) {
		m0_sm_init(&session->s_sm, &session_conf,
			   M0_RPC_SESSION_INITIALISED,
			   &conn->c_rpc_machine->rm_sm_grp,
			   NULL /* addb context */);
		M0_LOG(M0_INFO, "Session %p INITIALISED \n", session);
		m0_rpc_conn_add_session(conn, session);
		M0_ASSERT(m0_rpc_session_invariant(session));
	} else {
		__session_fini(session);
	}

	M0_RETURN(rc);
}

static int slot_table_alloc_and_init(struct m0_rpc_session *session)
{
	const struct m0_rpc_slot_ops *slot_ops;
	struct m0_rpc_slot           *slot;
	int                           i;
	int                           rc;

	M0_ENTRY("session: %p", session);

	M0_ALLOC_ARR(session->s_slot_table, session->s_nr_slots);
	if (session->s_slot_table == NULL)
		M0_RETURN(-ENOMEM);

	slot_ops = m0_rpc_conn_is_snd(session->s_conn) ? &snd_slot_ops
					               : &rcv_slot_ops;

	for (i = 0; i < session->s_nr_slots; i++) {

		M0_ALLOC_PTR(slot);
		if (slot == NULL) {
			M0_RETURN(-ENOMEM);
			/* __session_fini() will do the cleanup */
		}

		rc = m0_rpc_slot_init(slot, slot_ops);
		if (rc != 0) {
			m0_free(slot);
			M0_RETURN(rc);
		}

		slot->sl_session = session;
		slot->sl_slot_id = i;

		session->s_slot_table[i] = slot;
	}
	M0_RETURN(0);
}

/**
   Finalises session.
   Used by
    m0_rpc_session_init(), when initialisation fails.
    m0_rpc_session_fini() for cleanup
 */
static void __session_fini(struct m0_rpc_session *session)
{
	struct m0_rpc_slot *slot;
	int                 i;

	M0_ENTRY("session: %p", session);

	if (session->s_slot_table != NULL) {
		for (i = 0; i < session->s_nr_slots; i++) {
			slot = session->s_slot_table[i];
			if (slot != NULL) {
				m0_rpc_slot_fini(slot);
				m0_free(slot);
			}
			session->s_slot_table[i] = NULL;
		}
		session->s_nr_slots = 0;
		session->s_slot_table_capacity = 0;
		m0_free(session->s_slot_table);
		session->s_slot_table = NULL;
	}
	rpc_session_tlink_fini(session);
	ready_slot_tlist_fini(&session->s_ready_slots);
	m0_list_fini(&session->s_unbound_items);
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_session_fini(struct m0_rpc_session *session)
{
	struct m0_rpc_machine *machine;

	M0_ENTRY("session: %p", session);
	M0_PRE(session != NULL &&
	       session->s_conn != NULL &&
	       session_machine(session) != NULL);

	machine = session_machine(session);

	m0_rpc_machine_lock(machine);
	m0_rpc_session_fini_locked(session);
	m0_rpc_machine_unlock(machine);
	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_session_fini);

M0_INTERNAL void m0_rpc_session_fini_locked(struct m0_rpc_session *session)
{
	M0_ENTRY();
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_PRE(M0_IN(session_state(session), (M0_RPC_SESSION_TERMINATED,
					      M0_RPC_SESSION_INITIALISED,
					      M0_RPC_SESSION_FAILED)));

	m0_rpc_conn_remove_session(session);
	__session_fini(session);
	session->s_session_id = SESSION_ID_INVALID;
	session_state_set(session, M0_RPC_SESSION_FINALISED);
	m0_sm_fini(&session->s_sm);
	M0_LOG(M0_INFO, "Session %p FINALISED \n", session);
	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_session_timedwait(struct m0_rpc_session *session,
					 uint64_t states,
					 const m0_time_t abs_timeout)
{
	struct m0_rpc_machine *machine = session_machine(session);
	int                    rc;

	M0_ENTRY("session: %p, abs_timeout: [%llu:%llu]", session,
		 (unsigned long long)m0_time_seconds(abs_timeout),
		 (unsigned long long)m0_time_nanoseconds(abs_timeout));

	m0_rpc_machine_lock(machine);
	M0_ASSERT(m0_rpc_session_invariant(session));
	rc = m0_sm_timedwait(&session->s_sm, states, abs_timeout);
	M0_ASSERT(m0_rpc_session_invariant(session));
	m0_rpc_machine_unlock(machine);

	M0_RETURN(rc ?: session->s_sm.sm_rc);
}
M0_EXPORTED(m0_rpc_session_timedwait);

M0_INTERNAL int m0_rpc_session_create(struct m0_rpc_session *session,
				      struct m0_rpc_conn *conn,
				      uint32_t nr_slots, uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY("session: %p, conn: %p, nr_slots: %u", session,
		 conn, nr_slots);

	rc = m0_rpc_session_init(session, conn, nr_slots);
	if (rc == 0) {
		rc = m0_rpc_session_establish_sync(session, timeout_sec);
		if (rc != 0)
			m0_rpc_session_fini(session);
	}

	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_session_establish_sync(struct m0_rpc_session *session,
					      uint32_t timeout_sec)
{
	int  rc;

	M0_ENTRY("session: %p, timeout_sec: %u", session, timeout_sec);
	rc = m0_rpc_session_establish(session);
	if (rc != 0){
		M0_RETURN(rc);
	}

	rc = m0_rpc_session_timedwait(session, M0_BITS(M0_RPC_SESSION_IDLE,
						       M0_RPC_SESSION_FAILED),
				      m0_time_from_now(timeout_sec, 0));

	M0_ASSERT(M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
						 M0_RPC_SESSION_FAILED)));
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_session_establish_sync);

M0_INTERNAL int m0_rpc_session_establish(struct m0_rpc_session *session)
{
	struct m0_rpc_conn                  *conn;
	struct m0_fop                       *fop;
	struct m0_rpc_fop_session_establish *args;
	struct fop_session_establish_ctx    *ctx;
	struct m0_rpc_session               *session_0;
	struct m0_rpc_machine               *machine;
	int                                  rc;

	M0_ENTRY("session: %p", session);
	M0_PRE(session != NULL);

	if (M0_FI_ENABLED("fake_error"))
		M0_RETURN(-EINVAL);

	M0_ALLOC_PTR(ctx);
	if (ctx == NULL) {
		rc = -ENOMEM;
	} else {
		ctx->sec_session = session;
		m0_fop_init(&ctx->sec_fop,
			    &m0_rpc_fop_session_establish_fopt, NULL,
			    session_establish_fop_release);
		rc = m0_fop_data_alloc(&ctx->sec_fop);
		if (rc != 0)
			m0_fop_put(&ctx->sec_fop);
	}

	machine = session_machine(session);

	m0_rpc_machine_lock(machine);

	M0_ASSERT(m0_rpc_session_invariant(session) &&
		  session_state(session) == M0_RPC_SESSION_INITIALISED);

	if (rc != 0) {
		session_failed(session, rc);
		m0_rpc_machine_unlock(machine);
		M0_RETURN(rc);
	}

	conn = session->s_conn;

	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_ACTIVE);

	fop  = &ctx->sec_fop;
	args = m0_fop_data(fop);
	M0_ASSERT(args != NULL);

	args->rse_sender_id = conn->c_sender_id;
	args->rse_slot_cnt  = session->s_nr_slots;

	session_0 = m0_rpc_conn_session0(conn);
	rc = m0_rpc__fop_post(fop, session_0, &session_establish_item_ops);
	if (rc == 0) {
		session_state_set(session, M0_RPC_SESSION_ESTABLISHING);
	} else {
		session_failed(session, rc);
	}
	m0_fop_put(fop);

	M0_POST(ergo(rc != 0, session_state(session) == M0_RPC_SESSION_FAILED));
	M0_POST(m0_rpc_session_invariant(session));

	m0_rpc_machine_unlock(machine);

	/* see m0_rpc_session_establish_reply_received() */
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_session_establish);

/**
   Moves session to FAILED state and take it out of conn->c_sessions list.

   @pre m0_mutex_is_locked(&session->s_mutex)
   @pre M0_IN(session_state(session), (M0_RPC_SESSION_INITIALISED,
				       M0_RPC_SESSION_ESTABLISHING,
				       M0_RPC_SESSION_IDLE,
				       M0_RPC_SESSION_BUSY,
				       M0_RPC_SESSION_TERMINATING))
 */
static void session_failed(struct m0_rpc_session *session, int32_t error)
{
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_PRE(M0_IN(session_state(session), (M0_RPC_SESSION_INITIALISED,
					      M0_RPC_SESSION_ESTABLISHING,
					      M0_RPC_SESSION_IDLE,
					      M0_RPC_SESSION_BUSY,
					      M0_RPC_SESSION_TERMINATING)));
	m0_sm_fail(&session->s_sm, M0_RPC_SESSION_FAILED, error);

	M0_ASSERT(m0_rpc_session_invariant(session));
}

M0_INTERNAL void m0_rpc_session_establish_reply_received(struct m0_rpc_item
							 *item)
{
	struct m0_rpc_fop_session_establish_rep *reply;
	struct fop_session_establish_ctx        *ctx;
	struct m0_rpc_machine                   *machine;
	struct m0_rpc_session                   *session;
	struct m0_rpc_slot                      *slot;
	struct m0_rpc_item                      *reply_item;
	struct m0_fop                           *fop;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int32_t                                  rc;
	int                                      i;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL &&
	       item->ri_session != NULL &&
	       item->ri_session->s_session_id == SESSION_ID_0);

	reply_item = item->ri_reply;
	rc         = item->ri_error;

	M0_PRE(ergo(rc == 0, reply_item != NULL &&
			     item->ri_session == reply_item->ri_session));

	fop = m0_rpc_item_to_fop(item);
	ctx = container_of(fop, struct fop_session_establish_ctx, sec_fop);
	session = ctx->sec_session;
	M0_ASSERT(session != NULL);

	machine = session_machine(session);
	M0_ASSERT(m0_rpc_machine_is_locked(machine));

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_ASSERT(session_state(session) == M0_RPC_SESSION_ESTABLISHING);

	if (rc != 0) {
		session_failed(session, rc);
		goto out;
	}

	reply = m0_fop_data(m0_rpc_item_to_fop(reply_item));

	sender_id  = reply->rser_sender_id;
	session_id = reply->rser_session_id;
	rc         = reply->rser_rc;

	if (rc == 0) {
		if (session_id > SESSION_ID_MIN &&
		    session_id < SESSION_ID_MAX &&
		    sender_id != SENDER_ID_INVALID) {

			session->s_session_id = session_id;
			session_state_set(session, M0_RPC_SESSION_IDLE);

			for (i = 0; i < session->s_nr_slots; i++) {
				slot = session->s_slot_table[i];
				slot->sl_ops->so_slot_idle(slot);
			}
		} else {
			rc = -EPROTO;
		}
	}

	if (rc != 0)
		session_failed(session, rc);

out:
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_POST(M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
					       M0_RPC_SESSION_FAILED)));
	M0_ASSERT(m0_rpc_machine_is_locked(machine));
	M0_LEAVE();
}

static void session_establish_fop_release(struct m0_ref *ref)
{
	struct fop_session_establish_ctx *ctx;
	struct m0_fop                    *fop;

	fop = container_of(ref, struct m0_fop, f_ref);
	m0_fop_fini(fop);
	ctx = container_of(fop, struct fop_session_establish_ctx, sec_fop);
	m0_free(ctx);
}

int m0_rpc_session_destroy(struct m0_rpc_session *session, uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY("session: %p", session);

	rc = m0_rpc_session_terminate_sync(session, timeout_sec);
	m0_rpc_session_fini(session);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_session_destroy);

M0_INTERNAL int m0_rpc_session_terminate_sync(struct m0_rpc_session *session,
					      uint32_t timeout_sec)
{
	int rc;

	M0_ENTRY("session: %p", session);

	/* Wait for session to become IDLE */
	m0_rpc_session_timedwait(session, M0_BITS(M0_RPC_SESSION_IDLE),
				 M0_TIME_NEVER);

	rc = m0_rpc_session_terminate(session);
	if (rc == 0) {
		rc = m0_rpc_session_timedwait(session,
					      M0_BITS(M0_RPC_SESSION_TERMINATED,
						      M0_RPC_SESSION_FAILED),
					      m0_time_from_now(timeout_sec, 0));

		M0_ASSERT(M0_IN(session_state(session),
				(M0_RPC_SESSION_TERMINATED,
				 M0_RPC_SESSION_FAILED)));
	}
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_session_terminate_sync);

M0_INTERNAL int m0_rpc_session_terminate(struct m0_rpc_session *session)
{
	struct m0_fop                       *fop;
	struct m0_rpc_fop_session_terminate *args;
	struct m0_rpc_session               *session_0;
	struct m0_rpc_machine               *machine;
	struct m0_rpc_conn                  *conn;
	int                                  rc;

	M0_ENTRY("session: %p", session);
	M0_PRE(session != NULL && session->s_conn != NULL);

	conn    = session->s_conn;
	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_ASSERT(M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
						 M0_RPC_SESSION_TERMINATING)));

	if (session_state(session) == M0_RPC_SESSION_TERMINATING) {
		m0_rpc_machine_unlock(machine);
		M0_RETURN(0);
	}

	m0_rpc_session_del_slots_from_ready_list(session);

	fop = m0_fop_alloc(&m0_rpc_fop_session_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		/* See [^1] about decision to move session to FAILED state */
		session_failed(session, rc);
		goto out_unlock;
	}

	args                 = m0_fop_data(fop);
	args->rst_sender_id  = conn->c_sender_id;
	args->rst_session_id = session->s_session_id;

	session_0 = m0_rpc_conn_session0(conn);

	rc = m0_rpc__fop_post(fop, session_0, &session_terminate_item_ops);
	if (rc == 0) {
		session_state_set(session, M0_RPC_SESSION_TERMINATING);
	} else {
		session_failed(session, rc);
	}
	m0_fop_put(fop);

out_unlock:
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_POST(ergo(rc != 0, session_state(session) == M0_RPC_SESSION_FAILED));

	m0_rpc_machine_unlock(machine);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_session_terminate);
/*
 * m0_rpc_session_terminate
 * [^1]
 * There are two choices here:
 *
 * 1. leave session in TERMNATING state FOREVER.
 *    Then when to fini/cleanup session.
 *    This will not allow finialising of session, in turn conn,
 *    and rpc_machine can't be finalised.
 *
 * 2. Move session to FAILED state.
 *    For this session the receiver side state will still
 *    continue to exist. And receiver can send one-way
 *    items, that will be received on sender i.e. current node.
 *    Current code will drop such items. When/how to fini and
 *    cleanup receiver side state? XXX
 *
 * For now, later is chosen. This can be changed in future
 * to alternative 1, iff required.
 */


M0_INTERNAL void m0_rpc_session_terminate_reply_received(struct m0_rpc_item
							 *item)
{
	struct m0_rpc_fop_session_terminate_rep *reply;
	struct m0_rpc_fop_session_terminate     *args;
	struct m0_rpc_item                      *reply_item;
	struct m0_rpc_conn                      *conn;
	struct m0_rpc_session                   *session;
	struct m0_rpc_machine                   *machine;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int32_t                                  rc;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL &&
	       item->ri_session != NULL &&
	       item->ri_session->s_session_id == SESSION_ID_0);

	conn    = item->ri_session->s_conn;
	machine = conn->c_rpc_machine;

	M0_ASSERT(m0_rpc_machine_is_locked(machine));

	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_ACTIVE);

	reply_item = item->ri_reply;
	rc         = item->ri_error;

	M0_ASSERT(ergo(rc == 0, reply_item != NULL &&
			        item->ri_session == reply_item->ri_session));

	args = m0_fop_data(m0_rpc_item_to_fop(item));

	sender_id  = args->rst_sender_id;
	session_id = args->rst_session_id;

	M0_ASSERT(sender_id == conn->c_sender_id);

	session = m0_rpc_session_search(conn, session_id);
	M0_ASSERT(m0_rpc_session_invariant(session) &&
		  session_state(session) == M0_RPC_SESSION_TERMINATING);

	if (rc == 0) {
		reply = m0_fop_data(m0_rpc_item_to_fop(reply_item));
		rc    = reply->rstr_rc;
	}

	if (rc == 0)
		session_state_set(session, M0_RPC_SESSION_TERMINATED);
	else
		session_failed(session, rc);

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_ASSERT(M0_IN(session_state(session), (M0_RPC_SESSION_TERMINATED,
						 M0_RPC_SESSION_FAILED)));
	M0_ASSERT(m0_rpc_machine_is_locked(machine));
	M0_LEAVE();
}

M0_INTERNAL m0_bcount_t
m0_rpc_session_get_max_item_size(const struct m0_rpc_session *session)
{
	return session->s_conn->c_rpc_machine->rm_min_recv_size -
		m0_rpc_packet_onwire_header_size();
}

M0_INTERNAL m0_bcount_t
m0_rpc_session_get_max_item_payload_size(const struct m0_rpc_session *session)
{
	return m0_rpc_session_get_max_item_size(session) -
	       m0_rpc_item_onwire_header_size();
}

M0_INTERNAL void m0_rpc_session_hold_busy(struct m0_rpc_session *session)
{
	++session->s_hold_cnt;
	session_idle_x_busy(session);
}

M0_INTERNAL void m0_rpc_session_release(struct m0_rpc_session *session)
{
	M0_PRE(session_state(session) == M0_RPC_SESSION_BUSY);
	M0_PRE(session->s_hold_cnt > 0);

	--session->s_hold_cnt;
	session_idle_x_busy(session);
}

M0_INTERNAL void m0_rpc_session_mod_nr_active_items(struct m0_rpc_session
						    *session, int delta)
{
	M0_PRE(session != NULL);

	if (delta != 0) {
		/* XXX TODO overflow check */
		session->s_nr_active_items += delta;
		session_idle_x_busy(session);
	}
}

/** Perform (IDLE -> BUSY) or (BUSY -> IDLE) transition if required */
static void session_idle_x_busy(struct m0_rpc_session *session)
{
	int  state = session_state(session);
	bool idle  = m0_rpc_session_is_idle(session);

	M0_PRE(M0_IN(state, (M0_RPC_SESSION_IDLE, M0_RPC_SESSION_BUSY)));

	if (state == M0_RPC_SESSION_IDLE && !idle) {
		session_state_set(session, M0_RPC_SESSION_BUSY);
	} else if (state == M0_RPC_SESSION_BUSY && idle) {
		session_state_set(session, M0_RPC_SESSION_IDLE);
	}

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_POST(M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
					       M0_RPC_SESSION_BUSY)));
}

M0_INTERNAL int m0_rpc_session_cob_lookup(struct m0_cob *conn_cob,
					  uint64_t session_id,
					  struct m0_cob **session_cob,
					  struct m0_db_tx *tx)
{
	struct m0_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	M0_ENTRY("conn_cob: %p, session_id: %llu", conn_cob,
		 (unsigned long long)session_id);
	M0_PRE(conn_cob != NULL && session_id <= SESSION_ID_MAX &&
			session_cob != NULL);

	*session_cob = NULL;
	sprintf(name, "SESSION_%lu", (unsigned long)session_id);

	rc = m0_rpc_cob_lookup_helper(conn_cob->co_dom, conn_cob, name,
					&cob, tx);
	M0_ASSERT(ergo(rc != 0, cob == NULL));
	*session_cob = cob;
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_session_cob_create(struct m0_cob *conn_cob,
					  uint64_t session_id,
					  struct m0_cob **session_cob,
					  struct m0_db_tx *tx)
{
	struct m0_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	M0_ENTRY("conn_cob: %p, session_id: %llu", conn_cob,
		 (unsigned long long)session_id);
	M0_PRE(conn_cob != NULL && session_id != SESSION_ID_INVALID &&
			session_cob != NULL);

	*session_cob = NULL;
	sprintf(name, "SESSION_%lu", (unsigned long)session_id);

	rc = m0_rpc_cob_create_helper(conn_cob->co_dom, conn_cob, name,
					&cob, tx);
	M0_ASSERT(ergo(rc != 0, cob == NULL));
	*session_cob = cob;
	M0_RETURN(rc);
}

M0_INTERNAL uint64_t uuid_generate(void)
{
	static struct m0_atomic64 cnt;
	uint64_t                  uuid;
	uint64_t                  millisec;

	do {
		m0_atomic64_inc(&cnt);
		millisec = m0_time_nanoseconds(m0_time_now()) * 1000000;
		uuid = (millisec << 10) | (m0_atomic64_get(&cnt) & 0x3FF);
	} while (uuid == 0 || uuid == SENDER_ID_INVALID);

	return uuid;
}

/**
   Allocates and returns new session_id
 */
M0_INTERNAL uint64_t session_id_allocate(void)
{
	uint64_t session_id;

	M0_ENTRY();

	do {
		session_id = uuid_generate();
	} while (session_id <= SESSION_ID_MIN &&
		 session_id >= SESSION_ID_MAX);

	M0_LEAVE("session_id: %llu", (unsigned long long)session_id);
	return session_id;
}

static void snd_slot_idle(struct m0_rpc_slot *slot)
{
	struct m0_rpc_frm *frm;

	M0_PRE(slot != NULL);
	M0_PRE(slot->sl_session != NULL);
	M0_PRE(slot->sl_in_flight == 0);
	M0_PRE(!ready_slot_tlink_is_in(slot));

	ready_slot_tlist_add_tail(&slot->sl_session->s_ready_slots, slot);
	frm = session_frm(slot->sl_session);
	m0_rpc_frm_run_formation(frm);
}

M0_INTERNAL bool m0_rpc_session_bind_item(struct m0_rpc_item *item)
{
	struct m0_rpc_session *session;
	struct m0_rpc_slot    *slot;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && item->ri_session != NULL);

	session = item->ri_session;

	if (ready_slot_tlist_is_empty(&session->s_ready_slots)) {
		M0_LEAVE("rc: FALSE");
		return false;
	}
	slot = ready_slot_tlist_head(&session->s_ready_slots);
	ready_slot_tlist_del(slot);
	m0_rpc_slot_item_add_internal(slot, item);

	M0_POST(m0_rpc_item_is_bound(item));

	M0_LEAVE("rc: TRUE");
	return true;
}

static void snd_item_consume(struct m0_rpc_item *item)
{
	m0_rpc_frm_enq_item(session_frm(item->ri_session), item);
}

static void snd_reply_consume(struct m0_rpc_item *req,
			      struct m0_rpc_item *reply)
{
	/* Don't do anything on sender to consume reply */
}

static void rcv_slot_idle(struct m0_rpc_slot *slot)
{
	M0_ASSERT(slot->sl_in_flight == 0);
	/*
	 * On receiver side, no slot is placed on ready_slots list.
	 * All consumed reply items, will be treated as bound items by
	 * formation, and will find these items in its own lists.
	 */
}

static void rcv_item_consume(struct m0_rpc_item *item)
{
	m0_rpc_item_dispatch(item);
}

static void rcv_reply_consume(struct m0_rpc_item *req,
			      struct m0_rpc_item *reply)
{
	m0_rpc_frm_enq_item(session_frm(req->ri_session),
			    reply);
}

M0_INTERNAL int m0_rpc_rcv_session_establish(struct m0_rpc_session *session)
{
	struct m0_rpc_machine *machine;
	struct m0_db_tx        tx;
	uint64_t               session_id;
	int                    rc;

	M0_ENTRY("session: %p", session);
	M0_PRE(session != NULL);

	machine = session_machine(session);
	M0_PRE(m0_rpc_machine_is_locked(machine));
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_ASSERT(session_state(session) == M0_RPC_SESSION_INITIALISED);

	session_state_set(session, M0_RPC_SESSION_ESTABLISHING);
	rc = m0_db_tx_init(&tx, session->s_conn->c_cob->co_dom->cd_dbenv, 0);
	if (rc == 0) {
		session_id = session_id_allocate();
		rc = session_persistent_state_attach(session, session_id, &tx);
		if (rc == 0)
			rc = m0_db_tx_commit(&tx);
		else
			m0_db_tx_abort(&tx);
	}
	if (rc == 0) {
		session->s_session_id = session_id;
		session_state_set(session, M0_RPC_SESSION_IDLE);
	} else {
		session_failed(session, rc);
	}

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_RETURN(rc);
}

static int session_persistent_state_attach(struct m0_rpc_session *session,
					   uint64_t               session_id,
					   struct m0_db_tx       *tx)
{
	struct m0_cob  *cob;
	int             rc;
	int             i;

	M0_ENTRY("session: %p, session_id: %llu", session,
		 (unsigned long long)session_id);
	M0_PRE(session != NULL &&
	       m0_rpc_session_invariant(session) &&
	       session_state(session) == M0_RPC_SESSION_ESTABLISHING &&
	       session->s_cob == NULL);

	session->s_cob = NULL;
	rc = m0_rpc_session_cob_create(session->s_conn->c_cob,
					session_id, &cob, tx);
	if (rc != 0)
		goto errout;

	M0_ASSERT(cob != NULL);
	session->s_cob = cob;

	for (i = 0; i < session->s_nr_slots; i++) {
		M0_ASSERT(session->s_slot_table[i]->sl_cob == NULL);
		rc = m0_rpc_slot_cob_create(session->s_cob, i, 0,
							&cob, tx);
		if (rc != 0)
			goto errout;

		M0_ASSERT(cob != NULL);
		session->s_slot_table[i]->sl_cob = cob;
	}
	M0_RETURN(rc);

errout:
	M0_ASSERT(rc != 0);

	for (i = 0; i < session->s_nr_slots; i++) {
		cob = session->s_slot_table[i]->sl_cob;
		if (cob != NULL)
			m0_cob_put(cob);
		session->s_slot_table[i]->sl_cob = NULL;
	}
	if (session->s_cob != NULL) {
		m0_cob_put(session->s_cob);
		session->s_cob = NULL;
	}
	M0_RETURN(rc);
}

static int session_persistent_state_destroy(struct m0_rpc_session *session,
					    struct m0_db_tx       *tx)
{
	struct m0_rpc_slot *slot;
	int                 i;

	M0_ENTRY("session: %p", session);
	M0_ASSERT(session != NULL);

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		if (slot != NULL && slot->sl_cob != NULL) {
			/*
			 * m0_cob_delete_put() - even failed one - leaves the
			 * cob unusable.  And we don't care about error code.
			 */
			(void)m0_cob_delete_put(slot->sl_cob, tx);
			slot->sl_cob = NULL;
		}
	}
	if (session->s_cob != NULL) {
		m0_cob_delete_put(session->s_cob, tx);
		session->s_cob = NULL;
	}

	M0_RETURN(0);
}

M0_INTERNAL int m0_rpc_rcv_session_terminate(struct m0_rpc_session *session)
{
	struct m0_rpc_machine *machine;
	struct m0_rpc_conn    *conn;
	struct m0_db_tx        tx;
	int                    rc;

	M0_ENTRY("session: %p", session);
	M0_PRE(session != NULL);

	conn    = session->s_conn;
	machine = conn->c_rpc_machine;

	M0_PRE(m0_rpc_machine_is_locked(machine));

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_PRE(session_state(session) == M0_RPC_SESSION_IDLE);

	session_state_set(session, M0_RPC_SESSION_TERMINATING);
	/* For receiver side session, no slots are on ready_slots list
	   since all reply items are bound items. */

	rc = m0_db_tx_init(&tx, session->s_cob->co_dom->cd_dbenv, 0);
	if (rc == 0) {
		rc = session_persistent_state_destroy(session, &tx);
		if (rc == 0)
			rc = m0_db_tx_commit(&tx);
		else
			m0_db_tx_abort(&tx);
	}

	if (rc == 0)
		session_state_set(session, M0_RPC_SESSION_TERMINATED);
	else
		session_failed(session, rc);

	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_ASSERT(m0_rpc_machine_is_locked(machine));

	M0_RETURN(rc);
}

M0_INTERNAL void m0_rpc_session_item_failed(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_error != 0);
	M0_PRE(item->ri_sm.sm_state == M0_RPC_ITEM_FAILED);

	if (m0_rpc_item_is_request(item)) {
		if (item->ri_error == -ETIMEDOUT)
			m0_rpc_item_set_stage(item, RPC_ITEM_STAGE_TIMEDOUT);
		else
			m0_rpc_item_set_stage(item, RPC_ITEM_STAGE_FAILED);
	}
	/*
	 * Note that the slot is not marked as idle. Because we cannot
	 * use this slot to send more items.
	 * When session->s_nr_slots number of items fail then there will be
	 * no slot left to send further items.
	 *
	 * @todo Replay mechanism should bring items from UNKNOWN stage to
	 *       some known stage e.g. {PAST_VOLATILE, PAST_COMMITTED}
	 * @todo If item is failed _before_ placing on network, then we
	 *       can keep the slot in usable state, by performing inverse
	 *       operation of m0_rpc_slot_item_apply(). But this will
	 *       require reference counting implemented for RPC items.
	 *       Otherwise how the item (which was removed from slot) will
	 *       be freed?
	 */
}

/**
   For all slots belonging to @session,
     if slot is in m0_rpc_session::s_ready_slots list,
     then remove it from the list.
 */
M0_INTERNAL void m0_rpc_session_del_slots_from_ready_list(struct m0_rpc_session
							  *session)
{
	struct m0_rpc_slot    *slot;
	struct m0_rpc_machine *machine = session_machine(session);
	int                    i;

	M0_ENTRY("session: %p", session);

	M0_ASSERT(m0_rpc_machine_is_locked(machine));

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];

		M0_ASSERT(slot != NULL);

		if (ready_slot_tlink_is_in(slot))
			ready_slot_tlist_del(slot);
	}
	M0_LEAVE();
}
#ifndef __KERNEL__
/* for debugging  */
M0_INTERNAL int m0_rpc_session_items_print(struct m0_rpc_session *session,
					   bool only_active)
{
	struct m0_rpc_slot *slot;
	int                 count;
	int                 i;

	count = 0;
	for (i = 0; i < session->s_nr_slots; i++) {
		slot  = session->s_slot_table[i];
		count = m0_rpc_slot_item_list_print(slot, only_active, count);
	}
	return count;
}
#endif

#undef M0_TRACE_SUBSYSTEM

/** @} end of session group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
