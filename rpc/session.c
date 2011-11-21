/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "lib/arith.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#else
#include "rpc/session_u.h"
#endif

#include "rpc/session_internal.h"
#include "db/db.h"
#include "dtm/verno.h"
#include "rpc/session_fops.h"
#include "rpc/rpc2.h"

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

extern void frm_item_ready(struct c2_rpc_item *item);
extern void frm_slot_idle(struct c2_rpc_slot *slot);

/**
   The routine is also called from session_foms.c, hence can't be static
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session)
{
	bool result;
	int  i;

	/*
	 * Invariants that are common for session in state
	 * {IDLE, BUSY, TERMINATING}
	 */
	bool session_alive_invariants(void)
	{
		return session->s_session_id <= SESSION_ID_MAX &&
		       ergo(session->s_session_id != SESSION_ID_0,
			    session->s_conn->c_state == C2_RPC_CONN_ACTIVE &&
			    session->s_conn->c_nr_sessions > 0) &&
		       session->s_nr_slots >= 0 &&
		       c2_list_contains(&session->s_conn->c_sessions,
				&session->s_link);
	}

	bool no_slot_in_ready_slots_list(void)
	{
		struct c2_rpc_slot *slot;
		int                 i;

		for (i = 0 ; i < session->s_nr_slots; i++) {

			slot = session->s_slot_table[i];
			if (c2_list_link_is_in(&slot->sl_link))
				return false;

		}
		return true;
	}

	if (session == NULL)
		return false;

	C2_ASSERT(ergo(session->s_state != C2_RPC_SESSION_INITIALISED &&
			session->s_state != C2_RPC_SESSION_TERMINATED &&
			session->s_state != C2_RPC_SESSION_FAILED &&
			session->s_session_id != SESSION_ID_0,
			c2_mutex_is_locked(&session->s_mutex)));

	/*
	 * invariants that are independent on session state
	 */
	result = session->s_conn != NULL &&
		 nr_active_items_count(session) == session->s_nr_active_items;
	if (!result)
		return result;

	for (i = 0; i < session->s_nr_slots; i++) {
		if (session->s_slot_table[i] == NULL)
			return false;
	}

	/*
	 * Exactly one state bit must be set.
	 */
	if (!c2_is_po2(session->s_state))
		return false;

	if (session->s_state != C2_RPC_SESSION_IDLE &&
	    session->s_state != C2_RPC_SESSION_BUSY) {
		result = no_slot_in_ready_slots_list();
		if (!result)
			return result;
	}
	switch (session->s_state) {
	case C2_RPC_SESSION_INITIALISED:
		return session->s_session_id == SESSION_ID_INVALID &&
			session->s_nr_active_items == 0;

	case C2_RPC_SESSION_ESTABLISHING:
		return session->s_session_id == SESSION_ID_INVALID &&
			c2_list_contains(&session->s_conn->c_sessions,
				&session->s_link) &&
			session->s_nr_active_items == 0;

	case C2_RPC_SESSION_TERMINATED:
		return  !c2_list_link_is_in(&session->s_link) &&
			session->s_cob == NULL &&
			c2_list_is_empty(&session->s_unbound_items) &&
			session->s_nr_active_items == 0;

	case C2_RPC_SESSION_IDLE:
	case C2_RPC_SESSION_TERMINATING:
		return session->s_nr_active_items == 0 &&
			 c2_list_is_empty(&session->s_unbound_items) &&
			 session_alive_invariants();

	case C2_RPC_SESSION_BUSY:
		return (session->s_nr_active_items > 0 ||
		       !c2_list_is_empty(&session->s_unbound_items)) &&
		       session_alive_invariants();

	case C2_RPC_SESSION_FAILED:
		return session->s_rc != 0 &&
			!c2_list_link_is_in(&session->s_link);

	default:
		return false;
	}
	/* Should never reach here */
	C2_ASSERT(0);
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

			if (item->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS ||
			    item->ri_stage == RPC_ITEM_STAGE_FUTURE) {
				count++;
			}

		}
	}
	return count;
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
	c2_mutex_fini(&session->s_mutex);
	c2_list_fini(&session->s_unbound_items);
}

int c2_rpc_session_init(struct c2_rpc_session *session,
			struct c2_rpc_conn    *conn,
			uint32_t               nr_slots)
{
	const struct c2_rpc_slot_ops *slot_ops;
	struct c2_rpc_slot           *slot;
	int                           i;
	int                           rc;

	C2_PRE(session != NULL && conn != NULL && nr_slots >= 1);

	C2_SET0(session);
	c2_list_link_init(&session->s_link);
	session->s_session_id = SESSION_ID_INVALID;
	session->s_conn = conn;
	c2_cond_init(&session->s_state_changed);
	c2_mutex_init(&session->s_mutex);
	session->s_nr_slots = nr_slots;
	session->s_slot_table_capacity = nr_slots;
	c2_list_init(&session->s_unbound_items);
	session->s_cob = NULL;

	C2_ALLOC_ARR(session->s_slot_table, nr_slots);
	if (session->s_slot_table == NULL) {
		rc = -ENOMEM;
		goto out_err;
	}

	if (c2_rpc_conn_is_snd(conn)) {
		slot_ops = &snd_slot_ops;
	} else {
		C2_ASSERT(c2_rpc_conn_is_rcv(conn));
		slot_ops = &rcv_slot_ops;
	}
	for (i = 0; i < nr_slots; i++) {
		C2_ALLOC_PTR(slot);
		if (slot == NULL) {
			rc = -ENOMEM;
			goto out_err;
		}

		rc = c2_rpc_slot_init(slot, slot_ops);
		if (rc != 0) {
			c2_free(slot);
			goto out_err;
		}

		slot->sl_session = session;
		slot->sl_slot_id = i;

		session->s_slot_table[i] = slot;
	}
	session->s_state = C2_RPC_SESSION_INITIALISED;
	C2_ASSERT(c2_rpc_session_invariant(session));
	return 0;

out_err:
	C2_ASSERT(rc != 0);
	__session_fini(session);
	return rc;
}
C2_EXPORTED(c2_rpc_session_init);

void c2_rpc_session_fini(struct c2_rpc_session *session)
{

	C2_PRE(session != NULL);
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_INITIALISED ||
			session->s_state == C2_RPC_SESSION_FAILED);


	__session_fini(session);
	session->s_session_id = SESSION_ID_INVALID;
}
C2_EXPORTED(c2_rpc_session_fini);

bool c2_rpc_session_timedwait(struct c2_rpc_session *session,
			      uint64_t               state_flags,
			      const c2_time_t        abs_timeout)
{
	bool got_event = true;
	bool result;

	c2_mutex_lock(&session->s_mutex);
	while ((session->s_state & state_flags) == 0 && got_event) {
		got_event = c2_cond_timedwait(&session->s_state_changed,
						&session->s_mutex, abs_timeout);
		/*
		 * If got_event == false then TIME_OUT has occured.
		 * break the loop
		 */
		C2_ASSERT(c2_rpc_session_invariant(session));
	}
	result = ((session->s_state & state_flags) != 0);
	c2_mutex_unlock(&session->s_mutex);

	return result;
}
C2_EXPORTED(c2_rpc_session_timedwait);

int c2_rpc_session_establish(struct c2_rpc_session *session)
{
	struct c2_rpc_conn                  *conn;
	struct c2_fop                       *fop;
	struct c2_rpc_fop_session_establish *fop_se;
	struct fop_session_establish_ctx    *ctx;
	struct c2_rpc_session               *session_0;
	int                                  rc;

	C2_PRE(session != NULL &&
		session->s_state == C2_RPC_SESSION_INITIALISED);

	C2_ALLOC_PTR(ctx);
	if (ctx == NULL) {
		rc = -ENOMEM;
	} else {
		ctx->sec_session = session;

		rc = c2_fop_init(&ctx->sec_fop,
			         &c2_rpc_fop_session_establish_fopt, NULL);
		if (rc != 0)
			c2_free(ctx);

	}
	if (rc != 0) {
		/*
		 * It is okay to update session state without holding
		 * session->s_mutex here.
		 */
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = rc;
		C2_ASSERT(c2_rpc_session_invariant(session));
		goto out;
	}

	fop = &ctx->sec_fop;

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session));

	conn = session->s_conn;

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);

	fop_se = c2_fop_data(fop);
	C2_ASSERT(fop_se != NULL);

	fop_se->rse_sender_id = conn->c_sender_id;
	fop_se->rse_slot_cnt = session->s_nr_slots;

	session_0 = c2_rpc_conn_session0(conn);
	rc = c2_rpc__fop_post(fop, session_0,
				&c2_rpc_item_session_establish_ops);
	if (rc == 0) {
		/*
		 * conn->c_mutex protects from a race, if reply comes before
		 * adding session to conn->c_sessions list.
		 */
		session->s_state = C2_RPC_SESSION_ESTABLISHING;
		c2_list_add(&conn->c_sessions, &session->s_link);
		conn->c_nr_sessions++;
	} else {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = rc;
		c2_fop_fini(fop);
		c2_free(ctx);
	}

	C2_POST(ergo(rc != 0, session->s_state == C2_RPC_SESSION_FAILED));
	C2_POST(c2_rpc_session_invariant(session));
	C2_POST(c2_rpc_conn_invariant(conn));

	c2_cond_broadcast(&session->s_state_changed,
			  &session->s_mutex);
	c2_mutex_unlock(&conn->c_mutex);
	c2_mutex_unlock(&session->s_mutex);

out:
	return rc;
}
C2_EXPORTED(c2_rpc_session_establish);

int c2_rpc_session_establish_sync(struct c2_rpc_session *session,
				  uint32_t timeout_sec)
{
	int rc;
	bool state_reached;

	rc = c2_rpc_session_establish(session);
	if (rc != 0)
		return rc;

	/* Wait for session to become idle */
	state_reached = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE |
						 C2_RPC_SESSION_FAILED,
						 c2_time_from_now(timeout_sec, 0));
	if (!state_reached)
		return -ETIMEDOUT;

	switch (session->s_state) {
	case C2_RPC_SESSION_IDLE:
		rc = 0;
		break;
	case C2_RPC_SESSION_FAILED:
		rc = session->s_rc;
		break;
	default:
		C2_ASSERT("internal logic error in "
			  "c2_rpc_session_timedwait()" == 0);
	}

	return rc;
}
C2_EXPORTED(c2_rpc_session_establish_sync);

int c2_rpc_session_create(struct c2_rpc_session *session,
			  struct c2_rpc_conn    *conn,
			  uint32_t               nr_slots,
			  uint32_t               timeout_sec)
{
	int rc;

	rc = c2_rpc_session_init(session, conn, nr_slots);
	if (rc != 0)
		return rc;

	rc = c2_rpc_session_establish_sync(session, timeout_sec);
	if (rc != 0)
		c2_rpc_session_fini(session);

	return rc;
}
C2_EXPORTED(c2_rpc_session_create);

/**
   Moves session to FAILED state and take it out of conn->c_sessions list.
   Caller is expected to broadcast of session->s_state_changed CV.

   @pre c2_mutex_is_locked(&session->s_mutex) && (
		 (session->s_state == C2_RPC_SESSION_ESTABLISHING ||
                  session->s_state == C2_RPC_SESSION_IDLE ||
                  session->s_state == C2_RPC_SESSION_BUSY ||
                  session->s_state == C2_RPC_SESSION_TERMINATING)
 */
static void session_failed(struct c2_rpc_session *session, int32_t error)
{
	struct c2_rpc_conn *conn;

	C2_PRE(c2_mutex_is_locked(&session->s_mutex));
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_PRE(session->s_state == C2_RPC_SESSION_ESTABLISHING ||
		  session->s_state == C2_RPC_SESSION_IDLE ||
		  session->s_state == C2_RPC_SESSION_BUSY ||
		  session->s_state == C2_RPC_SESSION_TERMINATING);

	session->s_state = C2_RPC_SESSION_FAILED;
	session->s_rc = error;
	/*
	 * Remove session from conn->c_sessions list
	 */
	conn = session->s_conn;
	c2_mutex_lock(&conn->c_mutex);
	c2_list_del(&session->s_link);
	conn->c_nr_sessions--;
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);

	C2_ASSERT(c2_rpc_session_invariant(session));
}

void c2_rpc_session_establish_reply_received(struct c2_rpc_item *req,
					     struct c2_rpc_item *reply,
					     int                 rc)
{
	struct c2_rpc_fop_session_establish_rep *fop_ser;
	struct fop_session_establish_ctx        *ctx;
	struct c2_fop                           *fop;
	struct c2_rpc_session                   *session;
	struct c2_rpc_slot                      *slot;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int                                      i;

	C2_PRE(req != NULL && req->ri_session != NULL &&
		req->ri_session->s_session_id == SESSION_ID_0);
	C2_PRE(ergo(rc == 0, reply != NULL &&
			req->ri_session == reply->ri_session));

	fop = c2_rpc_item_to_fop(req);
	ctx = container_of(fop, struct fop_session_establish_ctx, sec_fop);
	session = ctx->sec_session;
	C2_ASSERT(session != NULL);

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_ESTABLISHING);

	if (rc != 0) {
		session_failed(session, rc);
		goto out;
	}

	fop_ser = c2_fop_data(c2_rpc_item_to_fop(reply));

	sender_id = fop_ser->rser_sender_id;
	session_id = fop_ser->rser_session_id;

	if (fop_ser->rser_rc != 0) {
		session_failed(session, fop_ser->rser_rc);
	} else {
		if (session_id < SESSION_ID_MIN ||
		    session_id > SESSION_ID_MAX ||
		    sender_id == SENDER_ID_INVALID) {
			/*
			 * error_code (rser_rc) in reply fop says session
			 * establish is successful. But either session_id is
			 * out of valid range or sender_id is invalid. This
			 * should not happen.
			 * Move session to FAILED state and XXX generate ADDB
			 * record.
			 * No assert on data received from network/disk.
			 */
			session_failed(session, -EPROTO);
			goto out;
		}
		session->s_session_id = session_id;
		session->s_state = C2_RPC_SESSION_IDLE;
		for (i = 0; i < session->s_nr_slots; i++) {
			slot = session->s_slot_table[i];
			C2_ASSERT(slot != NULL && c2_rpc_slot_invariant(slot));
			slot->sl_ops->so_slot_idle(slot);
		}
	}

out:
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE ||
		  session->s_state == C2_RPC_SESSION_FAILED);
	c2_cond_broadcast(&session->s_state_changed, &session->s_mutex);
	c2_mutex_unlock(&session->s_mutex);
}

int c2_rpc_session_terminate(struct c2_rpc_session *session)
{
	struct c2_fop                       *fop;
	struct c2_rpc_fop_session_terminate *fop_st;
	struct c2_rpc_session               *session_0;
	struct c2_rpc_conn                  *conn;
	int                                  rc;

	C2_PRE(session != NULL && session->s_conn != NULL);

	c2_rpc_session_del_slots_from_ready_list(session);
	c2_mutex_lock(&session->s_mutex);

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE ||
		  session->s_state == C2_RPC_SESSION_TERMINATING);

	/*
	 * c2_rpc_session_terminate() is a no-op if session is already
	 * in TERMINATING state
	 */
	if (session->s_state == C2_RPC_SESSION_TERMINATING) {
		rc = 0;
		goto out_unlock;
	}

	session->s_state = C2_RPC_SESSION_TERMINATING;

	/*
	 * Attempt to move this fop allocation before taking session->s_mutex
	 * resulted in lots of code duplication. So this memory allocation
	 * while holding a mutex is intentional.
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		/*
		 * There are two choices here:
		 *
		 * 1. leave session in TERMNATING state FOREVER.
		 *    Then when to fini/cleanup session.
		 *    This will not allow finialising of session, in turn conn,
		 *    and rpcmachine can't be finalised.
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
		session_failed(session, rc);
		goto out;
	}

	conn = session->s_conn;

	fop_st = c2_fop_data(fop);
	fop_st->rst_sender_id = conn->c_sender_id;
	fop_st->rst_session_id = session->s_session_id;

	c2_mutex_lock(&conn->c_mutex);
	session_0 = c2_rpc_conn_session0(conn);
	c2_mutex_unlock(&conn->c_mutex);

	c2_mutex_unlock(&session->s_mutex);

	rc = c2_rpc__fop_post(fop, session_0,
				&c2_rpc_item_session_terminate_ops);

	c2_mutex_lock(&session->s_mutex);

	if (rc != 0) {
		session_failed(session, rc);
		c2_fop_free(fop);
	}

out_unlock:
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_POST(ergo(rc != 0, session->s_state == C2_RPC_SESSION_FAILED));
	c2_cond_broadcast(&session->s_state_changed, &session->s_mutex);
	c2_mutex_unlock(&session->s_mutex);

out:
	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate);

int c2_rpc_session_terminate_sync(struct c2_rpc_session *session,
				  uint32_t timeout_sec)
{
	int rc;
	bool state_reached;

	/* Wait for session to become IDLE */
	c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE,
				 c2_time_from_now(timeout_sec, 0));

	/* Terminate session */
	rc = c2_rpc_session_terminate(session);
	if (rc != 0)
		return rc;

	/* Wait for session to become TERMINATED */
	state_reached = c2_rpc_session_timedwait(session,
				C2_RPC_SESSION_TERMINATED | C2_RPC_SESSION_FAILED,
				c2_time_from_now(timeout_sec, 0));
	if (!state_reached)
		return -ETIMEDOUT;

	switch (session->s_state) {
	case C2_RPC_SESSION_TERMINATED:
		rc = 0;
		break;
	case C2_RPC_SESSION_FAILED:
		rc = session->s_rc;
		break;
	default:
		C2_ASSERT("internal logic error in "
			  "c2_rpc_session_timedwait()" == 0);
	}

	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate_sync);

int c2_rpc_session_destroy(struct c2_rpc_session *session, uint32_t timeout_sec)
{
	int rc;

	rc = c2_rpc_session_terminate_sync(session, timeout_sec);
	if (rc != 0)
		return rc;

	c2_rpc_session_fini(session);

	return rc;
}
C2_EXPORTED(c2_rpc_session_destroy);

void c2_rpc_session_terminate_reply_received(struct c2_rpc_item *req,
					     struct c2_rpc_item *reply,
					     int                 rc)
{
	struct c2_rpc_fop_session_terminate_rep *fop_str;
	struct c2_rpc_fop_session_terminate     *fop_st;
	struct c2_fop                           *fop;
	struct c2_rpc_conn                      *conn;
	struct c2_rpc_session                   *session;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;

	C2_PRE(req != NULL && req->ri_session != NULL &&
		req->ri_session->s_session_id == SESSION_ID_0);
	C2_PRE(ergo(rc == 0, reply != NULL &&
			req->ri_session == reply->ri_session));

	/*
	 * Extract sender_id and session_id from SESSION_TERMINATE
	 * _request_ fop.
	 */
	fop = c2_rpc_item_to_fop(req);
	fop_st = c2_fop_data(fop);

	sender_id = fop_st->rst_sender_id;
	session_id = fop_st->rst_session_id;

	if (session_id == SESSION_ID_0) {
		/*
		 * There is no explicit SESSION_TERMINATE request from sender
		 * to terminate session 0.
		 * Session 0 is terminated along with the c2_rpc_conn object
		 * to which the session 0 belongs. So control should never
		 * reach here. But because of possible corruption on network
		 * or bug in receiver side code, session_id can be
		 * SESSION_ID_0. We cannot put assert here, this needs to be
		 * handled. XXX generate ADDB record here.
		 */
		C2_ASSERT(0);  /* For testing purpose */
		return;
	}

	/*
	 * Search session being terminated <sender_id, session_id>
	 */
	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);

	session = c2_rpc_session_search(conn, session_id);
	C2_ASSERT(session != NULL);

	c2_mutex_unlock(&conn->c_mutex);

	/* Locking order: session => conn */
	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session) &&
			session->s_state == C2_RPC_SESSION_TERMINATING);

	c2_mutex_lock(&conn->c_mutex);
	/*
	 * Remove session from conn->c_sessions list
	 */
	c2_list_del(&session->s_link);
	C2_ASSERT(conn->c_nr_sessions > 0);
	conn->c_nr_sessions--;
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);

	if (rc != 0) {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = rc;
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	fop_str = c2_fop_data(fop);

	if (sender_id != fop_str->rstr_sender_id ||
			session_id != fop_str->rstr_session_id) {
		/*
		 * Contents of conn terminate reply are not as per expectations.
		 * Move session to FAILED state. XXX And generate ADDB record.
		 * No asserts on data received from network/disk.
		 */
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = -EPROTO;
		goto out;
	}

	if (fop_str->rstr_rc == 0) {
		session->s_state = C2_RPC_SESSION_TERMINATED;
	} else {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = fop_str->rstr_rc;
	}

out:
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_FAILED);
	c2_cond_broadcast(&session->s_state_changed, &session->s_mutex);
	c2_mutex_unlock(&session->s_mutex);
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

	do {
		c2_atomic64_inc(&cnt);
		sec = c2_time_nanoseconds(c2_time_now()) * 1000000;

		session_id = (sec << 10) | (c2_atomic64_get(&cnt) & 0x3FF);

	} while (session_id < SESSION_ID_MIN ||
			session_id > SESSION_ID_MAX);

	return session_id;
}

static void snd_slot_idle(struct c2_rpc_slot *slot)
{
	C2_ASSERT(slot->sl_in_flight == 0);
	frm_slot_idle(slot);
}

static void snd_item_consume(struct c2_rpc_item *item)
{
	frm_item_ready(item);
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
	frm_item_ready(reply);
}

int c2_rpc_rcv_session_establish(struct c2_rpc_session *session)
{
	struct c2_db_tx tx;
	uint64_t        session_id;
	int             rc;

	C2_PRE(session != NULL);

	c2_mutex_lock(&session->s_mutex);

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(session->s_state == C2_RPC_SESSION_INITIALISED);

	rc = c2_db_tx_init(&tx, session->s_conn->c_cob->co_dom->cd_dbenv, 0);
	if (rc != 0)
		goto out;

	session_id = session_id_allocate();
	rc = session_persistent_state_attach(session, session_id, &tx);
	if (rc != 0) {
		/*
		 * Regardless of return value of c2_db_tx_abort() session
		 * will be moved to FAILED state.
		 */
		c2_db_tx_abort(&tx);
		goto out;
	}
	rc = c2_db_tx_commit(&tx);
	if (rc != 0)
		goto out;

	session->s_session_id = session_id;

	c2_mutex_lock(&session->s_conn->c_mutex);
	C2_ASSERT(c2_rpc_conn_invariant(session->s_conn));

	c2_list_add(&session->s_conn->c_sessions, &session->s_link);
	session->s_conn->c_nr_sessions++;

	C2_ASSERT(c2_rpc_conn_invariant(session->s_conn));
	c2_mutex_unlock(&session->s_conn->c_mutex);

out:
	session->s_state = (rc == 0) ? C2_RPC_SESSION_IDLE :
				       C2_RPC_SESSION_FAILED;
	session->s_rc = rc;
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_mutex_unlock(&session->s_mutex);
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
	struct c2_rpc_conn *conn;
	struct c2_db_tx     tx;
	int                 rc;

	C2_PRE(session != NULL);

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session));

	if (session->s_state != C2_RPC_SESSION_IDLE) {
		/*
		 * Should catch this situation while testing. This can be
		 * because of some bug.
		 */
		C2_ASSERT(0);
		/*
		 * XXX Generate ADDB record here.
		 */
		c2_mutex_unlock(&session->s_mutex);
		return -EPROTO;
	}

	/* For receiver side session, no slots are on ready_slots list
	   since all reply items are bound items. */

	/*
	 * remove session from list of sessions maintained in c2_rpc_conn.
	 */
	conn = session->s_conn;
	c2_mutex_lock(&conn->c_mutex);

	c2_list_del(&session->s_link);
	conn->c_nr_sessions--;

	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);

	/*
	 * Remove persistent state of session.
	 */
	rc = c2_db_tx_init(&tx, session->s_cob->co_dom->cd_dbenv, 0);
	if (rc != 0)
		goto out;

	rc = session_persistent_state_destroy(session, &tx);
	if (rc == 0) {
		rc = c2_db_tx_commit(&tx);
	} else {
		/*
		 * Even if abort fails, we will move session to FAILEd state.
		 */
		c2_db_tx_abort(&tx);
	}

out:
	session->s_state = (rc == 0) ? C2_RPC_SESSION_TERMINATED :
				C2_RPC_SESSION_FAILED;
	session->s_rc = rc;
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_mutex_unlock(&session->s_mutex);
	return rc;
}

/**
   For all slots belonging to @session,
     if slot is in c2_rpcmachine::cr_ready_slots list,
     then remove it from the list.
 */
void c2_rpc_session_del_slots_from_ready_list(struct c2_rpc_session *session)
{
	struct c2_rpc_slot   *slot;
	struct c2_rpcmachine *machine;
	int                   i;

	machine = session->s_conn->c_rpcmachine;

	/*
	 * XXX lock and unlock of cr_ready_slots_mutex is commented, until
	 * formation adds a fix for correct lock ordering.
	 */
	c2_mutex_lock(&machine->cr_ready_slots_mutex);

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];

		C2_ASSERT(slot != NULL);

		if (c2_list_link_is_in(&slot->sl_link))
			c2_list_del(&slot->sl_link);
	}

	c2_mutex_unlock(&machine->cr_ready_slots_mutex);
}

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
