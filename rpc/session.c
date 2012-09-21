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

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "lib/arith.h"
#include "lib/finject.h"
#include "rpc/session_ff.h"
#include "rpc/session_internal.h"
#include "db/db.h"
#include "dtm/verno.h"
#include "rpc/session_fops.h"
#include "rpc/rpc2.h"
#include "rpc/packet.h"      /* C2_RPC_PACKET_OW_HEADER_SIZE */
#include "rpc/formation2.h"

/**
   @addtogroup rpc_session

   @{

   This file defines functions related to c2_rpc_session.
 */

static void snd_slot_idle(struct c2_rpc_slot *slot);

static void snd_item_consume(struct c2_rpc_item *item);

static void snd_reply_consume(struct c2_rpc_item *req,
				 struct c2_rpc_item *reply);

static void rcv_slot_idle(struct c2_rpc_slot *slot);

static void rcv_item_consume(struct c2_rpc_item *item);

static void rcv_reply_consume(struct c2_rpc_item *req,
			      struct c2_rpc_item *reply);

/**
   Creates cob hierarchy that represents persistent state of @session.
 */
static int session_persistent_state_attach(struct c2_rpc_session *session,
					   uint64_t               session_id,
					   struct c2_db_tx       *tx);

/**
  Deletes all the cobs associated with the session and slots belonging to
  the session
 */
static int session_persistent_state_destroy(struct c2_rpc_session *session,
					    struct c2_db_tx       *tx);

static int nr_active_items_count(const struct c2_rpc_session *session);

static int slot_table_alloc_and_init(struct c2_rpc_session *session);
static void __session_fini(struct c2_rpc_session *session);
static void session_failed(struct c2_rpc_session *session, int32_t error);

static const struct c2_rpc_slot_ops snd_slot_ops = {
	.so_slot_idle = snd_slot_idle,
	.so_item_consume = snd_item_consume,
	.so_reply_consume = snd_reply_consume
};

static const struct c2_rpc_slot_ops rcv_slot_ops = {
	.so_slot_idle = rcv_slot_idle,
	.so_item_consume = rcv_item_consume,
	.so_reply_consume = rcv_reply_consume
};

/**
   Container of session_establish fop.

   Required only on sender to obtain pointer to session being established,
   when reply to session_establish is received.
 */
struct fop_session_establish_ctx
{
	/** A fop instance of type c2_rpc_fop_conn_establish_fopt */
	struct c2_fop          sec_fop;

	/** sender side session object */
	struct c2_rpc_session *sec_session;
};

static void fop_session_establish_item_free(struct c2_rpc_item *item);

static const struct c2_rpc_item_ops session_establish_item_ops = {
	.rio_replied = c2_rpc_session_establish_reply_received,
	.rio_free    = fop_session_establish_item_free,
};

static const struct c2_rpc_item_ops session_terminate_item_ops = {
	.rio_replied = c2_rpc_session_terminate_reply_received,
	.rio_free    = c2_fop_item_free,
};

/**
   The routine is also called from session_foms.c, hence can't be static
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session)
{
	struct c2_rpc_slot *slot;
	bool                ok;
	int                 i;

	ok = session != NULL &&
	     c2_is_po2(session->s_state) &&
	     session->s_conn != NULL &&
	     session->s_nr_slots > 0 &&
	     nr_active_items_count(session) == session->s_nr_active_items &&
	     c2_list_contains(&session->s_conn->c_sessions,
			      &session->s_link) &&
	     ergo(session->s_session_id != SESSION_ID_0,
		  session->s_conn->c_nr_sessions > 0);

	if (!ok)
		return false;

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		ok = c2_rpc_slot_invariant(slot) &&
		     ergo(C2_IN(session->s_state,
				(C2_RPC_SESSION_INITIALISED,
				 C2_RPC_SESSION_ESTABLISHING,
				 C2_RPC_SESSION_TERMINATING,
				 C2_RPC_SESSION_TERMINATED,
				 C2_RPC_SESSION_FAILED)),
		          !c2_list_link_is_in(&slot->sl_link));
		    /* A slot cannot be on ready slots list if session is
		       in one of above states */
		if (!ok)
			return false;
	}

	switch (session->s_state) {
	case C2_RPC_SESSION_INITIALISED:
	case C2_RPC_SESSION_ESTABLISHING:
		return session->s_session_id == SESSION_ID_INVALID &&
		       c2_rpc_session_is_idle(session);

	case C2_RPC_SESSION_TERMINATED:
		return 	session->s_cob == NULL &&
			session->s_session_id <= SESSION_ID_MAX &&
			c2_rpc_session_is_idle(session);

	case C2_RPC_SESSION_IDLE:
	case C2_RPC_SESSION_TERMINATING:
		return c2_rpc_session_is_idle(session) &&
		       session->s_session_id <= SESSION_ID_MAX;

	case C2_RPC_SESSION_BUSY:
		return !c2_rpc_session_is_idle(session) &&
		       session->s_session_id <= SESSION_ID_MAX;

	case C2_RPC_SESSION_FAILED:
		return session->s_rc != 0;

	default:
		return false;
	}
	/* Should never reach here */
	C2_ASSERT(0);
}

bool c2_rpc_session_is_idle(const struct c2_rpc_session *session)
{
	return session->s_nr_active_items == 0 &&
	       session->s_hold_cnt == 0;
}

static int nr_active_items_count(const struct c2_rpc_session *session)
{
	struct c2_rpc_slot *slot;
	struct c2_rpc_item *item;
	int                 i;
	int                 count = 0;

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];

		c2_list_for_each_entry(&slot->sl_item_list, item,
				       struct c2_rpc_item,
				       ri_slot_refs[0].sr_link) {

			if (C2_IN(item->ri_stage, (RPC_ITEM_STAGE_IN_PROGRESS,
						   RPC_ITEM_STAGE_FUTURE))) {
				count++;
			}

		}
	}
	return count;
}

int c2_rpc_session_init(struct c2_rpc_session *session,
			struct c2_rpc_conn    *conn,
			uint32_t               nr_slots)
{
	struct c2_rpc_machine *machine;
	int                    rc;

	C2_PRE(session != NULL && conn != NULL && nr_slots >= 1);

	machine = conn->c_rpc_machine;
	C2_PRE(machine != NULL);

	c2_rpc_machine_lock(machine);

	rc = c2_rpc_session_init_locked(session, conn, nr_slots);

	c2_rpc_machine_unlock(machine);

	return rc;
}
C2_EXPORTED(c2_rpc_session_init);

int c2_rpc_session_init_locked(struct c2_rpc_session *session,
			       struct c2_rpc_conn    *conn,
			       uint32_t               nr_slots)
{
	int rc;

	C2_PRE(session != NULL && conn != NULL && nr_slots >= 1);
	C2_PRE(c2_rpc_machine_is_locked(conn->c_rpc_machine));

	C2_SET0(session);

	session->s_session_id          = SESSION_ID_INVALID;
	session->s_conn                = conn;
	session->s_nr_slots            = nr_slots;
	session->s_slot_table_capacity = nr_slots;
	session->s_cob                 = NULL;

	c2_list_link_init(&session->s_link);
	c2_list_init(&session->s_unbound_items);
	c2_list_init(&session->s_ready_slots);

	c2_cond_init(&session->s_state_changed);

	rc = slot_table_alloc_and_init(session);
	if (rc == 0) {
		session->s_state = C2_RPC_SESSION_INITIALISED;
		c2_rpc_conn_add_session(conn, session);
		C2_ASSERT(c2_rpc_session_invariant(session));
	} else {
		__session_fini(session);
	}

	return rc;
}

static int slot_table_alloc_and_init(struct c2_rpc_session *session)
{
	const struct c2_rpc_slot_ops *slot_ops;
	struct c2_rpc_slot           *slot;
	int                           i;
	int                           rc;

	C2_ALLOC_ARR(session->s_slot_table, session->s_nr_slots);
	if (session->s_slot_table == NULL)
		return -ENOMEM;

	slot_ops = c2_rpc_conn_is_snd(session->s_conn) ? &snd_slot_ops
					               : &rcv_slot_ops;

	for (i = 0; i < session->s_nr_slots; i++) {

		C2_ALLOC_PTR(slot);
		if (slot == NULL)
			/* __session_fini() will do the cleanup */
			return -ENOMEM;

		rc = c2_rpc_slot_init(slot, slot_ops);
		if (rc != 0) {
			c2_free(slot);
			return rc;
		}

		slot->sl_session = session;
		slot->sl_slot_id = i;

		session->s_slot_table[i] = slot;
	}
	return 0;
}

/**
   Finalises session.
   Used by
    c2_rpc_session_init(), when initialisation fails.
    c2_rpc_session_fini() for cleanup
 */
static void __session_fini(struct c2_rpc_session *session)
{
	struct c2_rpc_slot *slot;
	int                 i;

	if (session->s_slot_table != NULL) {
		for (i = 0; i < session->s_nr_slots; i++) {
			slot = session->s_slot_table[i];
			if (slot != NULL) {
				c2_rpc_slot_fini(slot);
				c2_free(slot);
			}
			session->s_slot_table[i] = NULL;
		}
		session->s_nr_slots = 0;
		session->s_slot_table_capacity = 0;
		c2_free(session->s_slot_table);
		session->s_slot_table = NULL;
	}
	c2_list_link_fini(&session->s_link);
	c2_cond_fini(&session->s_state_changed);
	c2_list_fini(&session->s_ready_slots);
	c2_list_fini(&session->s_unbound_items);
}

void c2_rpc_session_fini(struct c2_rpc_session *session)
{
	struct c2_rpc_machine *machine;

	C2_PRE(session != NULL &&
	       session->s_conn != NULL &&
	       session->s_conn->c_rpc_machine != NULL);

	machine = session->s_conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);
	c2_rpc_session_fini_locked(session);
	c2_rpc_machine_unlock(machine);
}
C2_EXPORTED(c2_rpc_session_fini);

void c2_rpc_session_fini_locked(struct c2_rpc_session *session)
{
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(C2_IN(session->s_state, (C2_RPC_SESSION_TERMINATED,
					C2_RPC_SESSION_INITIALISED,
					C2_RPC_SESSION_FAILED)));

	c2_rpc_conn_remove_session(session);
	__session_fini(session);
	session->s_session_id = SESSION_ID_INVALID;
}

bool c2_rpc_session_timedwait(struct c2_rpc_session *session,
			      uint64_t               state_flags,
			      const c2_time_t        abs_timeout)
{
	struct c2_rpc_machine *machine;
	bool                   got_event = true;
	bool                   state_reached;

	machine = session->s_conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);

	C2_ASSERT(c2_rpc_session_invariant(session));

	while ((session->s_state & state_flags) == 0 && got_event) {
		got_event = c2_cond_timedwait(&session->s_state_changed,
					      c2_rpc_machine_mutex(machine),
					      abs_timeout);
		/*
		 * If got_event == false then TIME_OUT has occured.
		 * break the loop
		 */
		C2_ASSERT(c2_rpc_session_invariant(session));
	}
	state_reached = (session->s_state & state_flags) != 0;

	c2_rpc_machine_unlock(machine);

	return state_reached;
}
C2_EXPORTED(c2_rpc_session_timedwait);

int c2_rpc_session_create(struct c2_rpc_session *session,
			  struct c2_rpc_conn    *conn,
			  uint32_t               nr_slots,
			  uint32_t               timeout_sec)
{
	int rc;

	rc = c2_rpc_session_init(session, conn, nr_slots);
	if (rc == 0) {
		rc = c2_rpc_session_establish_sync(session, timeout_sec);
		if (rc != 0)
			c2_rpc_session_fini(session);
	}

	return rc;
}

int c2_rpc_session_establish_sync(struct c2_rpc_session *session,
				  uint32_t               timeout_sec)
{
	bool state_reached;
	int  rc;

	rc = c2_rpc_session_establish(session);
	if (rc != 0)
		return rc;

	state_reached = c2_rpc_session_timedwait(session,
				C2_RPC_SESSION_IDLE | C2_RPC_SESSION_FAILED,
				c2_time_from_now(timeout_sec, 0));
	/*
	 * When rpc-timeouts will be implemented !state_reached should never
	 * arise. Even if we return error e.g. -ETIMEDOUT what to do with
	 * a session in ESTABLISHING state?
	 */
	C2_ASSERT(state_reached);
	C2_ASSERT(C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
					   C2_RPC_SESSION_FAILED)));

	return session->s_state == C2_RPC_SESSION_IDLE ? 0 : session->s_rc;
}
C2_EXPORTED(c2_rpc_session_establish_sync);

int c2_rpc_session_establish(struct c2_rpc_session *session)
{
	struct c2_rpc_conn                  *conn;
	struct c2_fop                       *fop;
	struct c2_rpc_fop_session_establish *args;
	struct fop_session_establish_ctx    *ctx;
	struct c2_rpc_session               *session_0;
	struct c2_rpc_machine               *machine;
	int                                  rc;

	C2_PRE(session != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	C2_ALLOC_PTR(ctx);
	if (ctx == NULL) {
		rc = -ENOMEM;
	} else {
		ctx->sec_session = session;
		c2_fop_init(&ctx->sec_fop,
			    &c2_rpc_fop_session_establish_fopt, NULL);
		rc = c2_fop_data_alloc(&ctx->sec_fop);
		if (rc != 0)
			c2_free(ctx);
	}

	machine = session->s_conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);

	C2_ASSERT(c2_rpc_session_invariant(session) &&
		  session->s_state == C2_RPC_SESSION_INITIALISED);

	if (rc != 0) {
		session_failed(session, rc);
		c2_rpc_machine_unlock(machine);
		return rc;
	}

	conn = session->s_conn;

	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn_state(conn) == C2_RPC_CONN_ACTIVE);

	fop  = &ctx->sec_fop;
	args = c2_fop_data(fop);
	C2_ASSERT(args != NULL);

	args->rse_sender_id = conn->c_sender_id;
	args->rse_slot_cnt  = session->s_nr_slots;

	session_0 = c2_rpc_conn_session0(conn);
	rc = c2_rpc__fop_post(fop, session_0, &session_establish_item_ops);
	if (rc == 0) {
		session->s_state = C2_RPC_SESSION_ESTABLISHING;
	} else {
		session_failed(session, rc);
		c2_fop_fini(fop);
		c2_free(ctx);
	}

	C2_POST(ergo(rc != 0, session->s_state == C2_RPC_SESSION_FAILED));
	C2_POST(c2_rpc_session_invariant(session));
	C2_POST(c2_rpc_conn_invariant(conn));

	c2_cond_broadcast(&session->s_state_changed,
			  c2_rpc_machine_mutex(machine));
	c2_rpc_machine_unlock(machine);

	/* see c2_rpc_session_establish_reply_received() */
	return rc;
}
C2_EXPORTED(c2_rpc_session_establish);

/**
   Moves session to FAILED state and take it out of conn->c_sessions list.
   Caller is expected to broadcast of session->s_state_changed CV.

   @pre c2_mutex_is_locked(&session->s_mutex)
   @pre C2_IN(session->s_state, (C2_RPC_SESSION_INITIALISED,
				 C2_RPC_SESSION_ESTABLISHING,
				 C2_RPC_SESSION_IDLE,
				 C2_RPC_SESSION_BUSY,
				 C2_RPC_SESSION_TERMINATING))
 */
static void session_failed(struct c2_rpc_session *session, int32_t error)
{
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(C2_IN(session->s_state, (C2_RPC_SESSION_INITIALISED,
					C2_RPC_SESSION_ESTABLISHING,
					C2_RPC_SESSION_IDLE,
					C2_RPC_SESSION_BUSY,
					C2_RPC_SESSION_TERMINATING)));

	session->s_state = C2_RPC_SESSION_FAILED;
	session->s_rc    = error;

	C2_ASSERT(c2_rpc_session_invariant(session));
}

void c2_rpc_session_establish_reply_received(struct c2_rpc_item *item)
{
	struct c2_rpc_fop_session_establish_rep *reply;
	struct fop_session_establish_ctx        *ctx;
	struct c2_rpc_machine                   *machine;
	struct c2_rpc_session                   *session;
	struct c2_rpc_slot                      *slot;
	struct c2_rpc_item                      *reply_item;
	struct c2_fop                           *fop;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int32_t                                  rc;
	int                                      i;

	C2_PRE(item != NULL &&
	       item->ri_session != NULL &&
	       item->ri_session->s_session_id == SESSION_ID_0);

	reply_item = item->ri_reply;
	rc         = item->ri_error;

	C2_PRE(ergo(rc == 0, reply_item != NULL &&
			     item->ri_session == reply_item->ri_session));

	fop = c2_rpc_item_to_fop(item);
	ctx = container_of(fop, struct fop_session_establish_ctx, sec_fop);
	session = ctx->sec_session;
	C2_ASSERT(session != NULL);

	machine = session->s_conn->c_rpc_machine;
	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_ESTABLISHING);

	if (rc != 0) {
		session_failed(session, rc);
		goto out;
	}

	reply = c2_fop_data(c2_rpc_item_to_fop(reply_item));

	sender_id  = reply->rser_sender_id;
	session_id = reply->rser_session_id;
	rc         = reply->rser_rc;

	if (rc == 0) {
		if (session_id > SESSION_ID_MIN &&
		    session_id < SESSION_ID_MAX &&
		    sender_id != SENDER_ID_INVALID) {

			session->s_session_id = session_id;
			session->s_state      = C2_RPC_SESSION_IDLE;

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
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_POST(C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
					 C2_RPC_SESSION_FAILED)));

	c2_cond_broadcast(&session->s_state_changed,
			  c2_rpc_machine_mutex(machine));

	C2_ASSERT(c2_rpc_machine_is_locked(machine));
}

static void fop_session_establish_item_free(struct c2_rpc_item *item)
{
	struct fop_session_establish_ctx *ctx;
	struct c2_fop                    *fop;

	fop = c2_rpc_item_to_fop(item);
	ctx = container_of(fop, struct fop_session_establish_ctx, sec_fop);
	c2_free(ctx);
}

int c2_rpc_session_destroy(struct c2_rpc_session *session, uint32_t timeout_sec)
{
	int rc;

	rc = c2_rpc_session_terminate_sync(session, timeout_sec);
	c2_rpc_session_fini(session);

	/* Amit: What the sender will do with this return value? Is rc really required? */
	return rc;
}
C2_EXPORTED(c2_rpc_session_destroy);

int c2_rpc_session_terminate_sync(struct c2_rpc_session *session,
				  uint32_t timeout_sec)
{
	int rc;
	bool state_reached;

	/* Wait for session to become IDLE */
	c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE,
				 c2_time_from_now(timeout_sec, 0));

	rc = c2_rpc_session_terminate(session);
	if (rc == 0) {
		state_reached = c2_rpc_session_timedwait(session,
			    C2_RPC_SESSION_TERMINATED | C2_RPC_SESSION_FAILED,
			    c2_time_from_now(timeout_sec, 0));

		/*
		 * In the absense of rpc-timeouts, it is not very clear yet,
		 * that what to do with a session in TERMINATING state.
		 */
		C2_ASSERT(state_reached);

		C2_ASSERT(C2_IN(session->s_state, (C2_RPC_SESSION_TERMINATED,
						   C2_RPC_SESSION_FAILED)));

		rc = session->s_state == C2_RPC_SESSION_TERMINATED ? 0
							     : session->s_rc;
	}
	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate_sync);

int c2_rpc_session_terminate(struct c2_rpc_session *session)
{
	struct c2_fop                       *fop;
	struct c2_rpc_fop_session_terminate *args;
	struct c2_rpc_session               *session_0;
	struct c2_rpc_machine               *machine;
	struct c2_rpc_conn                  *conn;
	int                                  rc;

	C2_PRE(session != NULL && session->s_conn != NULL);

	conn    = session->s_conn;
	machine = conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
					   C2_RPC_SESSION_TERMINATING)));

	if (session->s_state == C2_RPC_SESSION_TERMINATING) {
		c2_rpc_machine_unlock(machine);
		return 0;
	}

	c2_rpc_session_del_slots_from_ready_list(session);

	fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		/* See [^1] about decision to move session to FAILED state */
		session_failed(session, rc);
		goto out_unlock;
	}

	args                 = c2_fop_data(fop);
	args->rst_sender_id  = conn->c_sender_id;
	args->rst_session_id = session->s_session_id;

	session_0 = c2_rpc_conn_session0(conn);

	rc = c2_rpc__fop_post(fop, session_0, &session_terminate_item_ops);

	if (rc == 0) {
		session->s_state = C2_RPC_SESSION_TERMINATING;
	} else {
		session_failed(session, rc);
		c2_fop_free(fop);
	}

out_unlock:
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_POST(ergo(rc != 0, session->s_state == C2_RPC_SESSION_FAILED));

	c2_cond_broadcast(&session->s_state_changed,
			  c2_rpc_machine_mutex(machine));

	c2_rpc_machine_unlock(machine);

	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate);
/*
 * c2_rpc_session_terminate
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
 *    continue to exist. And receiver can send unsolicited
 *    items, that will be received on sender i.e. current node.
 *    Current code will drop such items. When/how to fini and
 *    cleanup receiver side state? XXX
 *
 * For now, later is chosen. This can be changed in future
 * to alternative 1, iff required.
 */


void c2_rpc_session_terminate_reply_received(struct c2_rpc_item *item)
{
	struct c2_rpc_fop_session_terminate_rep *reply;
	struct c2_rpc_fop_session_terminate     *args;
	struct c2_rpc_item                      *reply_item;
	struct c2_rpc_conn                      *conn;
	struct c2_rpc_session                   *session;
	struct c2_rpc_machine                   *machine;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int32_t                                  rc;

	C2_PRE(item != NULL &&
	       item->ri_session != NULL &&
	       item->ri_session->s_session_id == SESSION_ID_0);

	conn    = item->ri_session->s_conn;
	machine = conn->c_rpc_machine;

	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn_state(conn) == C2_RPC_CONN_ACTIVE);

	reply_item = item->ri_reply;
	rc         = item->ri_error;

	C2_ASSERT(ergo(rc == 0, reply_item != NULL &&
			        item->ri_session == reply_item->ri_session));

	args = c2_fop_data(c2_rpc_item_to_fop(item));

	sender_id  = args->rst_sender_id;
	session_id = args->rst_session_id;

	C2_ASSERT(sender_id == conn->c_sender_id);

	session = c2_rpc_session_search(conn, session_id);
	C2_ASSERT(c2_rpc_session_invariant(session) &&
		  session->s_state == C2_RPC_SESSION_TERMINATING);

	if (rc == 0) {
		reply = c2_fop_data(c2_rpc_item_to_fop(reply_item));
		rc    = reply->rstr_rc;
	}

	if (rc == 0)
		session->s_state = C2_RPC_SESSION_TERMINATED;
	else
		session_failed(session, rc);

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(C2_IN(session->s_state, (C2_RPC_SESSION_TERMINATED,
					   C2_RPC_SESSION_FAILED)));

	c2_cond_broadcast(&session->s_state_changed,
			  c2_rpc_machine_mutex(machine));

	C2_ASSERT(c2_rpc_machine_is_locked(machine));
}

c2_bcount_t
c2_rpc_session_get_max_item_size(const struct c2_rpc_session *session)
{
	return session->s_conn->c_rpc_machine->rm_min_recv_size -
		C2_RPC_PACKET_OW_HEADER_SIZE;
}

void c2_rpc_session_hold_busy(struct c2_rpc_session *session)
{
	struct c2_rpc_machine *machine;

	machine = session->s_conn->c_rpc_machine;
	C2_PRE(c2_rpc_machine_is_locked(machine));
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
					C2_RPC_SESSION_BUSY)));

	++session->s_hold_cnt;
	if (session->s_state == C2_RPC_SESSION_IDLE) {
		session->s_state = C2_RPC_SESSION_BUSY;
		c2_cond_broadcast(&session->s_state_changed,
				  c2_rpc_machine_mutex(machine));
	}
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_POST(session->s_state == C2_RPC_SESSION_BUSY);
}

void c2_rpc_session_release(struct c2_rpc_session *session)
{
	struct c2_rpc_machine *machine;

	machine = session->s_conn->c_rpc_machine;
	C2_PRE(c2_rpc_machine_is_locked(machine));
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(session->s_state == C2_RPC_SESSION_BUSY);
	C2_PRE(session->s_hold_cnt > 0);

	--session->s_hold_cnt;
	if (c2_rpc_session_is_idle(session)) {
		session->s_state = C2_RPC_SESSION_IDLE;
		c2_cond_broadcast(&session->s_state_changed,
				  c2_rpc_machine_mutex(machine));
	}

	C2_ASSERT(c2_rpc_session_invariant(session));
}

int c2_rpc_session_cob_lookup(struct c2_cob   *conn_cob,
			      uint64_t         session_id,
			      struct c2_cob  **session_cob,
			      struct c2_db_tx *tx)
{
	struct c2_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(conn_cob != NULL && session_id <= SESSION_ID_MAX &&
			session_cob != NULL);

	*session_cob = NULL;
	sprintf(name, "SESSION_%lu", (unsigned long)session_id);

	rc = c2_rpc_cob_lookup_helper(conn_cob->co_dom, conn_cob, name,
					&cob, tx);
	C2_ASSERT(ergo(rc != 0, cob == NULL));
	*session_cob = cob;
	return rc;
}

int c2_rpc_session_cob_create(struct c2_cob   *conn_cob,
			      uint64_t         session_id,
			      struct c2_cob  **session_cob,
			      struct c2_db_tx *tx)
{
	struct c2_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(conn_cob != NULL && session_id != SESSION_ID_INVALID &&
			session_cob != NULL);

	*session_cob = NULL;
	sprintf(name, "SESSION_%lu", (unsigned long)session_id);

	rc = c2_rpc_cob_create_helper(conn_cob->co_dom, conn_cob, name,
					&cob, tx);
	C2_ASSERT(ergo(rc != 0, cob == NULL));
	*session_cob = cob;
	return rc;
}

/**
   Allocates and returns new session_id
 */
uint64_t session_id_allocate(void)
{
	static struct c2_atomic64 cnt;
	uint64_t                  session_id;
	uint64_t                  sec;
	bool                      session_id_is_valid;

	do {
		c2_atomic64_inc(&cnt);
		sec = c2_time_nanoseconds(c2_time_now()) * 1000000;

		session_id = (sec << 10) | (c2_atomic64_get(&cnt) & 0x3FF);
		session_id_is_valid = (session_id > SESSION_ID_MIN &&
				       session_id < SESSION_ID_MAX);

	} while (!session_id_is_valid);

	return session_id;
}

static void snd_slot_idle(struct c2_rpc_slot *slot)
{
	struct c2_rpc_frm *frm;

	C2_PRE(slot != NULL);
	C2_PRE(slot->sl_session != NULL);
	C2_PRE(slot->sl_in_flight == 0);
	C2_PRE(!c2_list_link_is_in(&slot->sl_link));

	c2_list_add_tail(&slot->sl_session->s_ready_slots, &slot->sl_link);
	frm = &slot->sl_session->s_conn->c_rpcchan->rc_frm;
	c2_rpc_frm_run_formation(frm);
}

bool c2_rpc_session_bind_item(struct c2_rpc_item *item)
{
	struct c2_rpc_session *session;
	struct c2_rpc_slot    *slot;

	C2_PRE(item != NULL && item->ri_session != NULL);

	session = item->ri_session;

	if (c2_list_is_empty(&session->s_ready_slots)) {
		return false;
	}
	slot = c2_list_entry(c2_list_first(&session->s_ready_slots),
			     struct c2_rpc_slot, sl_link);
	c2_list_del(&slot->sl_link);
	c2_rpc_slot_item_add_internal(slot, item);

	C2_POST(c2_rpc_item_is_bound(item));

	return true;
}

static void snd_item_consume(struct c2_rpc_item *item)
{
	c2_rpc_frm_enq_item(&item->ri_session->s_conn->c_rpcchan->rc_frm, item);
}

static void snd_reply_consume(struct c2_rpc_item *req,
			      struct c2_rpc_item *reply)
{
	/* Don't do anything on sender to consume reply */
}

static void rcv_slot_idle(struct c2_rpc_slot *slot)
{
	C2_ASSERT(slot->sl_in_flight == 0);
	/*
	 * On receiver side, no slot is placed on ready_slots list.
	 * All consumed reply items, will be treated as bound items by
	 * formation, and will find these items in its own lists.
	 */
}

static void rcv_item_consume(struct c2_rpc_item *item)
{
	c2_rpc_item_dispatch(item);
}

static void rcv_reply_consume(struct c2_rpc_item *req,
			      struct c2_rpc_item *reply)
{
	c2_rpc_frm_enq_item(&req->ri_session->s_conn->c_rpcchan->rc_frm,
			    reply);
}

int c2_rpc_rcv_session_establish(struct c2_rpc_session *session)
{
	struct c2_rpc_machine *machine;
	struct c2_db_tx        tx;
	uint64_t               session_id;
	int                    rc;

	C2_PRE(session != NULL);

	machine = session->s_conn->c_rpc_machine;
	C2_PRE(c2_rpc_machine_is_locked(machine));
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_INITIALISED);

	rc = c2_db_tx_init(&tx, session->s_conn->c_cob->co_dom->cd_dbenv, 0);
	if (rc == 0) {
		session_id = session_id_allocate();
		rc = session_persistent_state_attach(session, session_id, &tx);
		if (rc == 0)
			rc = c2_db_tx_commit(&tx);
		else
			c2_db_tx_abort(&tx);
	}
	if (rc == 0) {
		session->s_session_id = session_id;
		session->s_state      = C2_RPC_SESSION_IDLE;
	} else {
		session_failed(session, rc);
	}

	C2_ASSERT(c2_rpc_session_invariant(session));
	return rc;
}

static int session_persistent_state_attach(struct c2_rpc_session *session,
					   uint64_t               session_id,
					   struct c2_db_tx       *tx)
{
	struct c2_cob  *cob;
	int             rc;
	int             i;

	C2_PRE(session != NULL &&
	       c2_rpc_session_invariant(session) &&
	       session->s_state == C2_RPC_SESSION_INITIALISED &&
	       session->s_cob == NULL);

	session->s_cob = NULL;
	rc = c2_rpc_session_cob_create(session->s_conn->c_cob,
					session_id, &cob, tx);
	if (rc != 0)
		goto errout;

	C2_ASSERT(cob != NULL);
	session->s_cob = cob;

	for (i = 0; i < session->s_nr_slots; i++) {
		C2_ASSERT(session->s_slot_table[i]->sl_cob == NULL);
		rc = c2_rpc_slot_cob_create(session->s_cob, i, 0,
							&cob, tx);
		if (rc != 0)
			goto errout;

		C2_ASSERT(cob != NULL);
		session->s_slot_table[i]->sl_cob = cob;
	}
	return rc;

errout:
	C2_ASSERT(rc != 0);

	for (i = 0; i < session->s_nr_slots; i++) {
		cob = session->s_slot_table[i]->sl_cob;
		if (cob != NULL)
			c2_cob_put(cob);
		session->s_slot_table[i]->sl_cob = NULL;
	}
	if (session->s_cob != NULL) {
		c2_cob_put(session->s_cob);
		session->s_cob = NULL;
	}
	return rc;
}

static int session_persistent_state_destroy(struct c2_rpc_session *session,
					    struct c2_db_tx       *tx)
{
	struct c2_rpc_slot *slot;
	int                 i;

	C2_ASSERT(session != NULL);

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		if (slot != NULL && slot->sl_cob != NULL) {
			/*
			 * c2_cob_delete() "puts" the cob even if cob delete
			 * fails. Irrespective of success/failure of
			 * c2_cob_delete(), the c2_cob becomes unusable. So
			 * no need to handle the error
			 */
			c2_cob_delete(slot->sl_cob, tx);
			slot->sl_cob = NULL;
		}
	}
	if (session->s_cob != NULL) {
		c2_cob_delete(session->s_cob, tx);
		session->s_cob = NULL;
	}

	return 0;
}

int c2_rpc_rcv_session_terminate(struct c2_rpc_session *session)
{
	struct c2_rpc_machine *machine;
	struct c2_rpc_conn    *conn;
	struct c2_db_tx        tx;
	int                    rc;

	C2_PRE(session != NULL);

	conn    = session->s_conn;
	machine = conn->c_rpc_machine;

	C2_PRE(c2_rpc_machine_is_locked(machine));

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(session->s_state == C2_RPC_SESSION_IDLE);

	/* For receiver side session, no slots are on ready_slots list
	   since all reply items are bound items. */

	rc = c2_db_tx_init(&tx, session->s_cob->co_dom->cd_dbenv, 0);
	if (rc == 0) {
		rc = session_persistent_state_destroy(session, &tx);
		if (rc == 0)
			rc = c2_db_tx_commit(&tx);
		else
			c2_db_tx_abort(&tx);
	}

	if (rc == 0)
		session->s_state = C2_RPC_SESSION_TERMINATED;
	else
		session_failed(session, rc);

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	return rc;
}

/**
   For all slots belonging to @session,
     if slot is in c2_rpc_machine::rm_ready_slots list,
     then remove it from the list.
 */
void c2_rpc_session_del_slots_from_ready_list(struct c2_rpc_session *session)
{
	struct c2_rpc_slot    *slot;
	struct c2_rpc_machine *machine;
	int                    i;

	machine = session->s_conn->c_rpc_machine;
	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];

		C2_ASSERT(slot != NULL);

		if (c2_list_link_is_in(&slot->sl_link))
			c2_list_del(&slot->sl_link);
	}
}
#ifndef __KERNEL__
/* for debugging  */
int c2_rpc_session_items_print(struct c2_rpc_session *session, bool only_active)
{
	struct c2_rpc_slot *slot;
	int                 count;
	int                 i;

	count = 0;
	for (i = 0; i < session->s_nr_slots; i++) {
		slot  = session->s_slot_table[i];
		count = c2_rpc_slot_item_list_print(slot, only_active, count);
	}
	return count;
}
#endif
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
