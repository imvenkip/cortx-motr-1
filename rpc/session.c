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
 *		    Amit Jambure <Amit_Jambure@xyratex.com>
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
#include "rpc/rpccore.h"
#include "rpc/formation.h"

/**
   @addtogroup rpc_session

   @{
 */

/**
   Attaches session 0 object to conn object.
 */
static int session_zero_attach(struct c2_rpc_conn *conn);

/**
   Detaches session 0 from conn
 */
static void session_zero_detach(struct c2_rpc_conn *conn);

/**
   Searches slot->sl_item_list to find item with matching @xid.
   Returns item if found, NULL otherwise.
 */
static struct c2_rpc_item* item_find(const struct c2_rpc_slot *slot,
				     uint64_t                  xid);

/**
   Allocates and returns new sender_id
 */
static uint64_t sender_id_get(void);

/**
   Allocates and returns new session_id
 */
static uint64_t session_id_get(void);

/**
   Returns true iff @conn is sender end of rpc connection.
 */
static bool conn_is_snd(const struct c2_rpc_conn *conn);

/**
   Returns true iff @conn is receiver end of rpc connection.
 */
static bool conn_is_rcv(const struct c2_rpc_conn *conn);

/**
  XXX temporary routine that submits the fop inside item for execution.
 */
static void item_dispatch(struct c2_rpc_item *item);

static void snd_slot_idle(struct c2_rpc_slot *slot);

static void snd_item_consume(struct c2_rpc_item *item);

static void snd_reply_consume(struct c2_rpc_item *req,
				 struct c2_rpc_item *reply);

static void rcv_slot_idle(struct c2_rpc_slot *slot);

static void rcv_item_consume(struct c2_rpc_item *item);

static void rcv_reply_consume(struct c2_rpc_item *req,
			      struct c2_rpc_item *reply);

/**
   Creates "/SESSIONS/SENDER_$sender_id/SESSION_ID_0/SLOT_0:0" in cob namespace.
   Returns corresponding references to cobs in out parameters.
 */
static int conn_persistent_state_create(struct c2_cob_domain  *dom,
					uint64_t              sender_id,
					struct c2_cob       **conn_cob_out,
					struct c2_cob       **session0_cob_out,
					struct c2_cob       **slot0_cob_out,
					struct c2_db_tx      *tx);

/**
   Delegates persistent state creation to conn_persistent_state_create().
   And associates returned cobs to conn->c_cob, session0->s_cob and
   slot0->sl_cob
 */
static int conn_persistent_state_attach(struct c2_rpc_conn *conn,
					uint64_t            sender_id,
					struct c2_db_tx    *tx);

/**
   Creates SESSION_$session_id/SLOT_[0...($nr_slots - 1)]:0 cob entries
   within parent cob @conn_cob
 */
static int session_persistent_state_create(struct c2_cob    *conn_cob,
					   uint64_t          session_id,
					   struct c2_cob   **session_cob_out,
					   struct c2_cob   **slot_cob_array_out,
					   uint32_t          nr_slots,
					   struct c2_db_tx  *tx);

/**
   Delegates persistent state creation to session_persistent_state_create().
   And associates cobs to session->s_cob and slot[0..(nr_slots - 1)]->sl_cob
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

int c2_rpc_session_module_init(void)
{
	return c2_rpc_session_fop_init();
}
C2_EXPORTED(c2_rpc_session_module_init);

void conn_list_fini(struct c2_list *list)
{
	struct c2_rpc_conn *conn;
	struct c2_rpc_conn *conn_next;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, conn, conn_next, struct c2_rpc_conn,
			c_link)
		c2_rpc_conn_terminate_reply_sent(conn);
}

void c2_rpc_session_module_fini(struct c2_rpcmachine *machine)
{
	C2_PRE(machine != NULL);

	conn_list_fini(&machine->cr_incoming_conns);
	conn_list_fini(&machine->cr_outgoing_conns);

	c2_rpc_session_fop_fini();
}
C2_EXPORTED(c2_rpc_session_module_fini);

struct c2_rpc_session *c2_rpc_session_search(const struct c2_rpc_conn *conn,
					     uint64_t session_id)
{
	struct c2_rpc_session *session;

	C2_ASSERT(conn != NULL);

	c2_rpc_for_each_session(conn, session) {
		if (session->s_session_id == session_id)
			return session;
	}
	return NULL;
}

void c2_rpc_sender_uuid_generate(struct c2_rpc_sender_uuid *u)
{
	/* XXX temporary */
	uint64_t  rnd;
	c2_time_t now;

	rnd = c2_time_nanoseconds(c2_time_now(&now)) * 1000;
	u->su_uuid = c2_rnd(~0ULL >> 16, &rnd);
}

int c2_rpc_sender_uuid_cmp(const struct c2_rpc_sender_uuid *u1,
			   const struct c2_rpc_sender_uuid *u2)
{
	return C2_3WAY(u1->su_uuid, u2->su_uuid);
}

static bool conn_is_snd(const struct c2_rpc_conn *conn)
{
	return (conn->c_flags & RCF_SENDER_END) == RCF_SENDER_END;
}

static bool conn_is_rcv(const struct c2_rpc_conn *conn)
{
	return (conn->c_flags & RCF_RECV_END) == RCF_RECV_END;
}

/**
   Common code in c2_rpc_conn_fini() and init failed case in __conn_init()
 */
static void __conn_fini(struct c2_rpc_conn *conn)
{
	C2_ASSERT(conn != NULL);

	c2_list_fini(&conn->c_sessions);
	c2_cond_fini(&conn->c_state_changed);
	c2_list_link_fini(&conn->c_link);
	c2_mutex_fini(&conn->c_mutex);
}

static int __conn_init(struct c2_rpc_conn      *conn,
		       struct c2_net_end_point *ep,
		       struct c2_rpcmachine    *machine)
{
	int rc;

	C2_PRE(conn != NULL && ep != NULL && machine != NULL &&
		conn_is_snd(conn) != conn_is_rcv(conn));

	conn->c_end_point = ep;
	conn->c_sender_id = SENDER_ID_INVALID;
	conn->c_cob = NULL;
	c2_list_init(&conn->c_sessions);
	conn->c_nr_sessions = 0;
	c2_cond_init(&conn->c_state_changed);
	c2_mutex_init(&conn->c_mutex);
	c2_list_link_init(&conn->c_link);
	conn->c_rpcmachine = machine;
	conn->c_rc = 0;

	rc = session_zero_attach(conn);
	if (rc == 0) {
		conn->c_state = C2_RPC_CONN_INITIALISED;
	} else {
		__conn_fini(conn);
		C2_SET0(conn);
	}
	return rc;
}

int c2_rpc_conn_init(struct c2_rpc_conn      *conn,
		     struct c2_net_end_point *ep,
		     struct c2_rpcmachine    *machine)
{
	int rc;

	C2_ASSERT(conn != NULL && machine != NULL && ep != NULL);

	C2_SET0(conn);
	conn->c_flags = RCF_SENDER_END;
	c2_rpc_sender_uuid_generate(&conn->c_uuid);
	printf("rci: conn uuid %lu\n", (unsigned long)conn->c_uuid.su_uuid);
	rc = __conn_init(conn, ep, machine);

	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
			c2_rpc_conn_invariant(conn) && conn_is_snd(conn)));

	return rc;
}
C2_EXPORTED(c2_rpc_conn_init);

int c2_rpc_rcv_conn_init(struct c2_rpc_conn              *conn,
		         struct c2_net_end_point         *ep,
		         struct c2_rpcmachine            *machine,
			 const struct c2_rpc_sender_uuid *uuid)
{
	int rc;

	C2_ASSERT(conn != NULL && machine != NULL && ep != NULL);

	C2_SET0(conn);
	conn->c_flags = RCF_RECV_END;
	conn->c_uuid = *uuid;
	rc = __conn_init(conn, ep, machine);
	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
			c2_rpc_conn_invariant(conn) && conn_is_rcv(conn)));

	return rc;
}

int c2_rpc_conn_establish(struct c2_rpc_conn *conn)
{
	struct c2_fop                      *fop;
	struct c2_rpc_fop_conn_establish   *fop_ce;
	struct c2_rpc_item                 *item;
	struct c2_rpc_session              *session_0;
	struct c2_rpcmachine               *machine;
	int                                 rc;

	C2_PRE(conn != NULL);

	fop = c2_fop_alloc(&c2_rpc_fop_conn_establish_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	machine = conn->c_rpcmachine;
	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(conn->c_state == C2_RPC_CONN_INITIALISED &&
	          conn_is_snd(conn) && c2_rpc_conn_invariant(conn));

	conn->c_state = C2_RPC_CONN_CONNECTING;
	/*
	 * Get a source endpoint and in turn a transfer machine
	 *  to associate with this c2_rpc_conn.
	 */
	conn->c_rpcchan = c2_rpc_chan_get(machine);

	fop_ce = c2_fop_data(fop);
	C2_ASSERT(fop_ce != NULL);

	/*
	 * Receiver will copy this cookie in conn_establish reply
	 * XXX the cookie does not serve any significant purpose.
	 * It was introduced to be able to match conn_establish reply to
	 * corresponding request. But now as CONN_ESTABLISH fops are also
	 * sent using SESSION_ID_0, this cookie is not useful anymore.
	 */
	fop_ce->rce_cookie = (uint64_t)conn;

	session_0 = c2_rpc_conn_session0(conn);

	item = &fop->f_item;
	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_ops = &c2_rpc_item_conn_establish_ops;

	/* Formation client side keeps track of current rpcs in flight,
	   while server side of formation does not. So we need to differentiate
	   between client and server sides for formation. */
	machine->cr_formation->rf_client_side = true;
	rc = c2_rpc_post(item);
	if (rc == 0) {
		c2_mutex_lock(&machine->cr_session_mutex);
		c2_list_add(&machine->cr_outgoing_conns, &conn->c_link);
		c2_mutex_unlock(&machine->cr_session_mutex);
	} else {
		conn->c_state = C2_RPC_CONN_INITIALISED;
		conn->c_end_point = NULL;
		c2_fop_free(fop);
	}
	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_CONNECTING &&
			c2_rpc_conn_invariant(conn)));
	C2_POST(ergo(rc != 0, conn->c_state == C2_RPC_CONN_INITIALISED) &&
			c2_rpc_conn_invariant(conn));
	c2_cond_broadcast(&conn->c_state_changed, &conn->c_mutex);
	c2_mutex_unlock(&conn->c_mutex);

out:
	return rc;
}
C2_EXPORTED(c2_rpc_conn_establish);

void c2_rpc_conn_establish_reply_received(struct c2_rpc_item *req,
					  struct c2_rpc_item *reply,
					  int                 rc)
{
	struct c2_rpc_fop_conn_establish_rep *fop_cer;
	struct c2_fop                        *fop;
	struct c2_rpc_conn                   *conn;

	C2_PRE(req != NULL && req->ri_session != NULL &&
		req->ri_session->s_session_id == SESSION_ID_0);
	C2_PRE(ergo(rc == 0, req->ri_session == reply->ri_session));

	void conn_failed(int error)
	{
		C2_ASSERT(c2_mutex_is_locked(&conn->c_mutex));

		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_sender_id = SENDER_ID_INVALID;
		/*
		 * Remove conn from conn->c_rpcmachine->cr_outgoing_conns list
		 */
		c2_mutex_lock(&conn->c_rpcmachine->cr_session_mutex);
		c2_list_del(&conn->c_link);
		c2_mutex_unlock(&conn->c_rpcmachine->cr_session_mutex);

		conn->c_rc = error;
	}

	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_CONNECTING &&
		  c2_rpc_conn_invariant(conn));

	if (rc != 0) {
		conn_failed(rc);
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	C2_ASSERT(fop != NULL);

	fop_cer = c2_fop_data(fop);
	C2_ASSERT(fop_cer != NULL);

	if (fop_cer->rcer_rc != 0) {
		/*
		 * Receiver has reported conn create failure
		 */
		conn_failed(fop_cer->rcer_rc);
		/*
		 * end-user is expected to call c2_rpc_conn_fini() on
		 * this object, to free any memory allocated by conn for
		 * its internal structures (session0, slots etc.).
		 * Then the end-user can free the object.
		 */
		printf("ccrr: conn create failed %d\n",
			fop_cer->rcer_rc);
	} else {
		C2_ASSERT(fop_cer->rcer_snd_id != SENDER_ID_INVALID &&
			  conn->c_sender_id == SENDER_ID_INVALID);
		conn->c_sender_id = fop_cer->rcer_snd_id;
		conn->c_state = C2_RPC_CONN_ACTIVE;
		printf("ccrr: conn created %lu\n",
			(unsigned long)fop_cer->rcer_snd_id);
	}

out:
	C2_ASSERT(conn->c_state == C2_RPC_CONN_FAILED ||
		  conn->c_state == C2_RPC_CONN_ACTIVE);
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_cond_broadcast(&conn->c_state_changed, &conn->c_mutex);
	c2_mutex_unlock(&conn->c_mutex);
}

static int session_zero_attach(struct c2_rpc_conn *conn)
{
	struct c2_rpc_slot    *slot;
	struct c2_rpc_session *session;
	int                    rc;

	C2_ASSERT(conn != NULL);

	C2_ALLOC_PTR(session);
	if (session == NULL)
		return -ENOMEM;

	C2_SET0(session);

	rc = c2_rpc_session_init(session, conn, 1);   /* 1 => number of slots */
	if (rc != 0) {
		c2_free(session);
		return rc;
	}

	session->s_session_id = SESSION_ID_0;
	session->s_state = C2_RPC_SESSION_IDLE;

	c2_list_add(&conn->c_sessions, &session->s_link);
	slot = session->s_slot_table[0];
	C2_ASSERT(slot != NULL &&
		  slot->sl_ops != NULL &&
		  slot->sl_ops->so_slot_idle != NULL);
	slot->sl_ops->so_slot_idle(slot);
	C2_ASSERT(c2_rpc_session_invariant(session));
	return 0;
}

static void session_zero_detach(struct c2_rpc_conn *conn)
{
	struct c2_rpc_session	*session;
	struct c2_rpc_slot	*slot;

	C2_PRE(conn != NULL);

	session = c2_rpc_conn_session0(conn);
	session->s_state = C2_RPC_SESSION_TERMINATED;
	c2_list_del(&session->s_link);
	session->s_conn = NULL;

	/*
	 * Remove slot0 from c2_rpcmachine::cr_ready_slots list
	 */
	slot = session->s_slot_table[0];
	if (c2_list_link_is_in(&slot->sl_link))
		c2_list_del(&slot->sl_link);

	c2_rpc_session_fini(session);
	c2_free(session);
}

struct c2_rpc_session *c2_rpc_conn_session0(const struct c2_rpc_conn *conn)
{
	struct c2_rpc_session *session0;

	session0 = c2_rpc_session_search(conn, SESSION_ID_0);

	C2_ASSERT(session0 != NULL);
	return session0;
}
int c2_rpc_conn_terminate(struct c2_rpc_conn *conn)
{
	struct c2_fop                    *fop;
	struct c2_rpc_fop_conn_terminate *fop_ct;
	struct c2_rpc_item               *item;
	struct c2_rpc_session            *session_0;
	int                               rc;

	C2_PRE(conn != NULL);

	fop = c2_fop_alloc(&c2_rpc_fop_conn_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_sender_id != SENDER_ID_INVALID &&
		  c2_rpc_conn_invariant(conn));

	if (conn->c_state != C2_RPC_CONN_ACTIVE) {
		rc = -EINVAL;
		goto out_unlock;
	}

	if (conn->c_nr_sessions > 0) {
		rc = -EBUSY;
		goto out_unlock;
	}

	printf("sender_conn_terminate: %p(%lu)\n", conn,
			(unsigned long)conn->c_sender_id);
	fop_ct = c2_fop_data(fop);
	C2_ASSERT(fop_ct != NULL);

	fop_ct->ct_sender_id = conn->c_sender_id;

	session_0 = c2_rpc_conn_session0(conn);

	item = &fop->f_item;
	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_ops = &c2_rpc_item_conn_terminate_ops;

	rc = c2_rpc_post(item);
	if (rc == 0) {
		conn->c_state = C2_RPC_CONN_TERMINATING;
		c2_cond_broadcast(&conn->c_state_changed, &conn->c_mutex);
	} else {
		c2_fop_free(fop);
	}

out_unlock:
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);

out:
	return rc;
}
C2_EXPORTED(c2_rpc_conn_terminate);

void c2_rpc_conn_terminate_reply_received(struct c2_rpc_item *req,
					  struct c2_rpc_item *reply,
					  int                 rc)
{
	struct c2_rpc_fop_conn_terminate_rep *fop_ctr;
	struct c2_fop                        *fop;
	struct c2_rpc_conn                   *conn;
	struct c2_rpc_slot                   *slot;
	uint64_t                              sender_id;

	C2_PRE(req != NULL && req->ri_session != NULL &&
		req->ri_session->s_session_id == SESSION_ID_0);
	C2_PRE(ergo(rc == 0, req->ri_session == reply->ri_session));

	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_TERMINATING &&
		  c2_rpc_conn_invariant(conn));

	c2_mutex_lock(&conn->c_rpcmachine->cr_session_mutex);
	c2_list_del(&conn->c_link);
	c2_mutex_unlock(&conn->c_rpcmachine->cr_session_mutex);

	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = rc;
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	C2_ASSERT(fop != NULL);

	fop_ctr = c2_fop_data(fop);
	C2_ASSERT(fop_ctr != NULL);

	sender_id = fop_ctr->ctr_sender_id;
	C2_ASSERT(conn->c_sender_id == sender_id);

	if (fop_ctr->ctr_rc == 0) {
		printf("ctrr: connection terminated %lu\n",
			(unsigned long)conn->c_sender_id);
		conn->c_state = C2_RPC_CONN_TERMINATED;
		conn->c_sender_id = SENDER_ID_INVALID;
		conn->c_rc = 0;
		conn->c_rpcmachine = NULL;
		/*
		 * session 0 will be cleaned up in c2_rpc_conn_fini()
		 */
	} else {
		/*
		 * Connection termination failed
		 */
		printf("ctrr: conn termination failed %lu\n",
			(unsigned long)conn->c_sender_id);
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = fop_ctr->ctr_rc;
	}

out:
	C2_POST(conn->c_state == C2_RPC_CONN_TERMINATED ||
		conn->c_state == C2_RPC_CONN_FAILED);
	C2_POST(c2_rpc_conn_invariant(conn));
	/*
	 * XXX Rethink: Remove the slot0 of session0, from ready_slots lists.
	 */
	slot = req->ri_session->s_slot_table[0];
	C2_ASSERT(slot != NULL && c2_list_link_is_in(&slot->sl_link));
	c2_list_del(&slot->sl_link);

	c2_cond_broadcast(&conn->c_state_changed, &conn->c_mutex);
	c2_mutex_unlock(&conn->c_mutex);
	/* Release the reference on c2_rpc_chan structure being used. */
	c2_rpc_chan_put(conn->c_rpcchan);
}

void c2_rpc_conn_fini(struct c2_rpc_conn *conn)
{
	C2_PRE(conn != NULL);
	C2_PRE(conn->c_state == C2_RPC_CONN_TERMINATED ||
	       conn->c_state == C2_RPC_CONN_FAILED ||
	       conn->c_state == C2_RPC_CONN_INITIALISED);

	C2_ASSERT(c2_rpc_conn_invariant(conn));

	session_zero_detach(conn);
	__conn_fini(conn);
	C2_SET0(conn);
}
C2_EXPORTED(c2_rpc_conn_fini);

bool c2_rpc_conn_timedwait(struct c2_rpc_conn *conn,
			   uint64_t            state_flags,
			   const c2_time_t     abs_timeout)
{
	bool            got_event = true;

	c2_mutex_lock(&conn->c_mutex);
	while ((conn->c_state & state_flags) == 0 && got_event) {
		got_event = c2_cond_timedwait(&conn->c_state_changed,
					&conn->c_mutex, abs_timeout);
		/*
		 * If got_event == false then TIME_OUT has occured.
		 * break the loop
		 */
		C2_ASSERT(c2_rpc_conn_invariant(conn));
	}
	c2_mutex_unlock(&conn->c_mutex);

	return (conn->c_state & state_flags) != 0;
}
C2_EXPORTED(c2_rpc_conn_timedwait);

/**
   Checks connection object invariant.

   Function is also called from session_foms.c, hence cannot be static.
 */
bool c2_rpc_conn_invariant(const struct c2_rpc_conn *conn)
{
	struct c2_list *conn_list;
	bool            sender_end;
	bool            recv_end;
	bool		ret;

	if (conn == NULL)
		return false;

	sender_end = conn_is_snd(conn);
	recv_end = conn_is_rcv(conn);

	/*
	 * conditions that should be true irrespective of conn state
	 */
	ret = sender_end != recv_end && c2_list_invariant(&conn->c_sessions) &&
		c2_list_length(&conn->c_sessions) == conn->c_nr_sessions + 1;

	if (!ret)
		return ret;

	/*
	 * conditions that are common to CONNECTING, ACTIVE and TERMINATING
	 * state
	 */
	switch (conn->c_state) {
	case C2_RPC_CONN_CONNECTING:
	case C2_RPC_CONN_ACTIVE:
	case C2_RPC_CONN_TERMINATING:
		conn_list = sender_end ?
				&conn->c_rpcmachine->cr_outgoing_conns :
				&conn->c_rpcmachine->cr_incoming_conns;
		ret = c2_list_contains(conn_list, &conn->c_link) &&
			conn->c_end_point != NULL &&
			conn->c_rpcmachine != NULL &&
			conn->c_rc == 0;
		if (!ret)
			return ret;
		break;
	default:
		; /* Do nothing. Just to avoid compiler warnings. */
	}

	switch (conn->c_state) {
	case C2_RPC_CONN_INITIALISED:
		return conn->c_sender_id == SENDER_ID_INVALID &&
			conn->c_rpcmachine != NULL &&
			conn->c_end_point != NULL &&
			!c2_list_link_is_in(&conn->c_link) &&
			conn->c_nr_sessions == 0;

	case C2_RPC_CONN_CONNECTING:
		return conn->c_sender_id == SENDER_ID_INVALID &&
			conn->c_nr_sessions == 0;

	case C2_RPC_CONN_ACTIVE:
		return conn->c_sender_id != SENDER_ID_INVALID &&
			ergo(recv_end, conn->c_cob != NULL) &&
			c2_rpc_session_invariant(c2_rpc_conn_session0(conn));

	case C2_RPC_CONN_TERMINATING:
		return conn->c_nr_sessions == 0 &&
			conn->c_sender_id != SENDER_ID_INVALID;

	case C2_RPC_CONN_TERMINATED:
		return !c2_list_link_is_in(&conn->c_link) &&
			conn->c_rpcmachine == NULL &&
			conn->c_nr_sessions == 0 &&
			conn->c_cob == NULL &&
			conn->c_rc == 0;

	case C2_RPC_CONN_FAILED:
		return conn->c_rc != 0 &&
			!c2_list_link_is_in(&conn->c_link) &&
			conn->c_rpcmachine == NULL;

	default:
		return false;
	}
	/* Should never reach here */
	C2_ASSERT(0);
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

	if (conn_is_snd(conn)) {
		slot_ops = &snd_slot_ops;
	} else {
		C2_ASSERT(conn_is_rcv(conn));
		slot_ops = &rcv_slot_ops;
	}
	for (i = 0; i < nr_slots; i++) {
		C2_ALLOC_PTR(slot);
		if (slot == NULL) {
			rc = -ENOMEM;
			goto out_err;
		}

		rc = c2_rpc_slot_init(slot, slot_ops);
		if (rc != 0)
			goto out_err;

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
	C2_SET0(session);
	return rc;
}
C2_EXPORTED(c2_rpc_session_init);

int c2_rpc_session_establish(struct c2_rpc_session *session)
{
	struct c2_rpc_conn                  *conn;
	struct c2_fop                       *fop;
	struct c2_rpc_fop_session_establish *fop_se;
	struct c2_rpc_item                  *item;
	struct c2_rpc_session               *session_0;
	int                                  rc;

	C2_PRE(session != NULL &&
		session->s_state == C2_RPC_SESSION_INITIALISED);

	fop = c2_fop_alloc(&c2_rpc_fop_session_establish_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session));

	conn = session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	fop_se = c2_fop_data(fop);
	C2_ASSERT(fop_se != NULL);

	fop_se->rse_snd_id = conn->c_sender_id;
	fop_se->rse_slot_cnt = session->s_nr_slots;

	session_0 = c2_rpc_conn_session0(conn);

	item = &fop->f_item;
	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_ops = &c2_rpc_item_session_establish_ops;

	rc = c2_rpc_post(item);
	if (rc == 0) {
		session->s_state = C2_RPC_SESSION_ESTABLISHING;
		c2_list_add(&conn->c_sessions, &session->s_link);
		conn->c_nr_sessions++;
		c2_cond_broadcast(&session->s_state_changed,
				  &session->s_mutex);
	} else {
		c2_fop_free(fop);
	}

	C2_ASSERT(c2_mutex_is_locked(&conn->c_mutex));
	C2_POST(ergo(rc == 0, session->s_state == C2_RPC_SESSION_ESTABLISHING));
	C2_POST(ergo(rc != 0, session->s_state == C2_RPC_SESSION_INITIALISED));
	C2_POST(c2_rpc_session_invariant(session));
	C2_POST(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);
	c2_mutex_unlock(&session->s_mutex);

out:
	return rc;
}
C2_EXPORTED(c2_rpc_session_establish);

void c2_rpc_session_establish_reply_received(struct c2_rpc_item *req,
					     struct c2_rpc_item *reply,
					     int                 rc)
{
	struct c2_rpc_fop_session_establish_rep *fop_ser;
	struct c2_fop                           *fop;
	struct c2_rpc_conn                      *conn;
	struct c2_rpc_session                   *session;
	struct c2_rpc_session                   *s;
	struct c2_rpc_slot                      *slot;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int                                      i;

	C2_PRE(req != NULL && req->ri_session != NULL &&
		req->ri_session->s_session_id == SESSION_ID_0);
	C2_PRE(ergo(rc == 0, req->ri_session == reply->ri_session));

	void session_failed(int error)
	{
		C2_ASSERT(c2_mutex_is_locked(&session->s_mutex));

		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = error;
		/*
		 * Remove session from conn->c_sessions list
		 */
		c2_mutex_lock(&conn->c_mutex);
		c2_list_del(&session->s_link);
		conn->c_nr_sessions--;
		C2_ASSERT(c2_rpc_conn_invariant(conn));
		c2_mutex_unlock(&conn->c_mutex);
	}

	printf("scrr: sender_id %lu session_id %lu\n",
		(unsigned long)sender_id, (unsigned long)session_id);
	/*
	 * Search session object, for which SESSION_ESTABLISH_REPLY
	 * is received
	 */
	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	/*
	 * For a c2_rpc_conn
	 * There can be only one session create in progress at any given point.
	 * XXX If we support more than one items in flight for a slot, then
	 * this assumption does not hold. In that case we need some mechanism
	 * to match SESSION_ESTABLISH_REPLY with session being established.
	 */
	session = NULL;
	c2_rpc_for_each_session(conn, s) {
		if (s->s_state == C2_RPC_SESSION_ESTABLISHING) {
			session = s;
			break;
		}
	}
	C2_ASSERT(session != NULL);
	c2_mutex_unlock(&conn->c_mutex);

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(session->s_state == C2_RPC_SESSION_ESTABLISHING);
	C2_ASSERT(c2_rpc_session_invariant(session));

	if (rc != 0) {
		session_failed(rc);
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	C2_ASSERT(fop != NULL);

	fop_ser = c2_fop_data(fop);
	C2_ASSERT(fop_ser != NULL);

	sender_id = fop_ser->rser_sender_id;
	session_id = fop_ser->rser_session_id;
	C2_ASSERT(sender_id != SENDER_ID_INVALID);

	if (fop_ser->rser_rc != 0) {
		printf("scrr: Session create failed\n");
		session_failed(fop_ser->rser_rc);
	} else {
		C2_ASSERT(session_id >= SESSION_ID_MIN &&
			  session_id <= SESSION_ID_MAX);
		session->s_session_id = session_id;
		session->s_state = C2_RPC_SESSION_IDLE;
		for (i = 0; i < session->s_nr_slots; i++) {
			slot = session->s_slot_table[i];
			C2_ASSERT(slot != NULL && c2_rpc_slot_invariant(slot));
			slot->sl_ops->so_slot_idle(slot);
		}
		printf("scrr: session created %lu\n",
			(unsigned long)session_id);
	}

out:
	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE ||
		  session->s_state == C2_RPC_SESSION_FAILED);
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_cond_broadcast(&session->s_state_changed, &session->s_mutex);
	c2_mutex_unlock(&session->s_mutex);
}

int c2_rpc_session_terminate(struct c2_rpc_session *session)
{
	struct c2_fop                       *fop;
	struct c2_rpc_fop_session_terminate *fop_st;
	struct c2_rpc_item                  *item;
	struct c2_rpc_session               *session_0;
	struct c2_rpc_slot                  *slot;
	int                                  rc;
	int                                  i;

	C2_PRE(session != NULL && session->s_conn != NULL);

	fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	c2_mutex_lock(&session->s_mutex);

	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE ||
		  session->s_state == C2_RPC_SESSION_TERMINATING);
	C2_ASSERT(c2_rpc_session_invariant(session));

	/*
	 * c2_rpc_session_terminate() is a no-op if session is already
	 * in TERMINATING state
	 */
	if (session->s_state == C2_RPC_SESSION_TERMINATING) {
		rc = 0;
		goto out_unlock;
	}

	fop_st = c2_fop_data(fop);
	C2_ASSERT(fop_st != NULL);

	fop_st->rst_sender_id = session->s_conn->c_sender_id;
	fop_st->rst_session_id = session->s_session_id;

	c2_mutex_lock(&session->s_conn->c_mutex);
	session_0 = c2_rpc_conn_session0(session->s_conn);
	C2_ASSERT(session_0->s_state == C2_RPC_SESSION_IDLE ||
		  session_0->s_state == C2_RPC_SESSION_BUSY);
	c2_mutex_unlock(&session->s_conn->c_mutex);

	item = &fop->f_item;
	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_ops = &c2_rpc_item_session_terminate_ops;

	/*
	 * Take all the slots out of c2_rpcmachine::cr_ready_slots
	 * This should be done before c2_rpc_post(). Because if formation
	 * finds a slot belonging to the session being terminated, it might
	 * try to lock slot->sl_session causing self deadlock.
	 */
	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		if (c2_list_link_is_in(&slot->sl_link))
			/* lock on ready slot list??? */
			c2_list_del(&slot->sl_link);
		/*
		 * XXX slot->sl_link and ready slot lst is completely
		 * managed by formation.
		 * So instead of session trying to remove slot from
		 * ready slot list directly, there should be one more
		 * event generated by slot and handled by formation,
		 * saying "stop using slot".
		 * In the event handler, formation can remove the slot
		 * from ready slot list.
		 */
	}

	rc = c2_rpc_post(item);
	if (rc == 0) {
		session->s_state = C2_RPC_SESSION_TERMINATING;
		c2_cond_broadcast(&session->s_state_changed, &session->s_mutex);
	} else {
		/*
		 * slots belonging to this session were taken out of ready slot
		 * list. Again mark these slots as ready
		 */
		for (i = 0; i < session->s_nr_slots; i++) {
			slot = session->s_slot_table[i];
			C2_ASSERT(slot != NULL && c2_rpc_slot_invariant(slot));
			slot->sl_ops->so_slot_idle(slot);
		}
		c2_fop_free(fop);
	}

out_unlock:
	C2_POST(ergo(rc == 0, session->s_state == C2_RPC_SESSION_TERMINATING));
	C2_POST(c2_rpc_session_invariant(session));
	c2_mutex_unlock(&session->s_mutex);

out:
	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate);

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
	C2_PRE(ergo(rc == 0, req->ri_session == reply->ri_session));

	/*
	 * Extract sender_id and session_id from SESSION_TERMINATE
	 * _request_ fop.
	 */
	fop = c2_rpc_item_to_fop(req);
	C2_ASSERT(fop != NULL);

	fop_st = c2_fop_data(fop);
	C2_ASSERT(fop_st != NULL);

	sender_id = fop_st->rst_sender_id;
	session_id = fop_st->rst_session_id;

	/*
	 * Search session being terminated <sender_id, session_id>
	 */
	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	C2_ASSERT(conn->c_sender_id == sender_id);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	session = c2_rpc_session_search(conn, session_id);
	C2_ASSERT(session != NULL &&
		  session->s_state == C2_RPC_SESSION_TERMINATING);
	/*
	 * Remove session from conn->c_sessions list
	 */
	c2_list_del(&session->s_link);
	conn->c_nr_sessions--;
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session));

	if (rc != 0) {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = rc;
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	C2_ASSERT(fop != NULL);

	fop_str = c2_fop_data(fop);
	C2_ASSERT(fop_str != NULL);

	C2_ASSERT(sender_id == fop_str->rstr_sender_id &&
			session_id == fop_str->rstr_session_id);

	if (fop_str->rstr_rc == 0) {
		session->s_state = C2_RPC_SESSION_TERMINATED;
		session->s_rc = 0;
		printf("strr: session terminated %lu\n",
			(unsigned long)session_id);
	} else {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = fop_str->rstr_rc;
		printf("strr: session terminate failed %lu\n",
			(unsigned long)session_id);
	}

out:
	session->s_conn = NULL;
	C2_ASSERT(session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_FAILED);
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_cond_broadcast(&session->s_state_changed, &session->s_mutex);
	c2_mutex_unlock(&session->s_mutex);
}

bool c2_rpc_session_timedwait(struct c2_rpc_session *session,
			      uint64_t               state_flags,
			      const c2_time_t        abs_timeout)
{
	bool            got_event = true;

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
	c2_mutex_unlock(&session->s_mutex);

	return (session->s_state & state_flags) != 0;
}
C2_EXPORTED(c2_rpc_session_timedwait);

void c2_rpc_session_fini(struct c2_rpc_session *session)
{

	C2_PRE(session != NULL);
	C2_PRE(session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_INITIALISED ||
			session->s_state == C2_RPC_SESSION_FAILED);

	C2_ASSERT(c2_rpc_session_invariant(session));

	__session_fini(session);
	C2_SET0(session);
	session->s_session_id = SESSION_ID_INVALID;
}
C2_EXPORTED(c2_rpc_session_fini);

/**
   Invariants that are common for session in state {IDLE, BUSY, TERMINATING}
 */
static bool session_alive_invariants(const struct c2_rpc_session *session)
{
	bool result;
	int  i;

	result = session->s_session_id <= SESSION_ID_MAX &&
		 session->s_conn != NULL &&
		 ergo(session->s_session_id != SESSION_ID_0,
			session->s_conn->c_state == C2_RPC_CONN_ACTIVE &&
			session->s_conn->c_nr_sessions > 0) &&
		 session->s_nr_slots >= 0 &&
		 c2_list_contains(&session->s_conn->c_sessions,
			&session->s_link);

	if (!result)
		return result;

	for (i = 0; i < session->s_nr_slots; i++) {
		if (session->s_slot_table[i] == NULL)
			return false;
	}
	return true;
}

/**
   The routine is also called from session_foms.c, hence can't be static
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session)
{
	bool                result;

	int nr_active_items_count(void)
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
				if (item->ri_tstate == RPC_ITEM_IN_PROGRESS ||
				    item->ri_tstate == RPC_ITEM_FUTURE)
					count++;
			}
		}

		return count;
	}

	if (session == NULL)
		return false;

	switch (session->s_state) {
	case C2_RPC_SESSION_INITIALISED:
		return session->s_session_id == SESSION_ID_INVALID &&
			session->s_conn != NULL;

	case C2_RPC_SESSION_ESTABLISHING:
		return session->s_session_id == SESSION_ID_INVALID &&
			session->s_conn != NULL &&
			c2_list_contains(&session->s_conn->c_sessions,
				&session->s_link);

	case C2_RPC_SESSION_TERMINATED:
		return  !c2_list_link_is_in(&session->s_link) &&
			session->s_conn == NULL &&
			session->s_cob == NULL;

	case C2_RPC_SESSION_IDLE:
		result = session->s_nr_active_items == 0 &&
			 c2_list_is_empty(&session->s_unbound_items) &&
			 session_alive_invariants(session) &&
			 nr_active_items_count() == 0;

		if (!result)
			return result;

		return true;

	case C2_RPC_SESSION_BUSY:
		return (session->s_nr_active_items > 0 ||
		       !c2_list_is_empty(&session->s_unbound_items)) &&
		       session_alive_invariants(session) &&
		       nr_active_items_count() ==
				session->s_nr_active_items;

	case C2_RPC_SESSION_TERMINATING:
		return session_alive_invariants(session);

	case C2_RPC_SESSION_FAILED:
		return session->s_rc != 0 &&
			!c2_list_link_is_in(&session->s_link) &&
			session->s_conn == NULL;

	default:
		return false;
	}
	/* Should never reach here */
	C2_ASSERT(0);
}

/**
   Resizes slot table in 'session' to 'nr_slots'.

   If nr_slots > current capacity of slot table then
        it reallocates the slot table.
   else
        it just marks slots above nr_slots as 'dont use'
 */
int c2_rpc_session_slot_table_resize(struct c2_rpc_session *session,
				     uint32_t               nr_slots)
{
	return 0;
}

int c2_rpc_cob_create_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx)
{
	struct c2_cob_nskey  *key;
	struct c2_cob_nsrec   nsrec;
	struct c2_cob_fabrec  fabrec;
	struct c2_cob        *cob;
	uint64_t              pfid_hi;
	uint64_t              pfid_lo;
	uint64_t              rnd;
	c2_time_t             now;
	int                   rc;

	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;

	if (pcob == NULL) {
		pfid_hi = pfid_lo = 1;
	} else {
		pfid_hi = COB_GET_PFID_HI(pcob);
		pfid_lo = COB_GET_PFID_LO(pcob);
	}

	c2_cob_nskey_make(&key, pfid_hi, pfid_lo, name);
	if (key == NULL)
		return -ENOMEM;

	/*
	 * XXX How to get unique stob_id for new cob?
	 */
	rnd = c2_time_nanoseconds(c2_time_now(&now)) * 1000;

	nsrec.cnr_stobid.si_bits.u_hi = c2_rnd(1000, &rnd);
	nsrec.cnr_stobid.si_bits.u_lo = c2_rnd(1000, &rnd);
	nsrec.cnr_nlink = 1;

	printf("cob_create_helper: hi:lo %lu:%lu\n",
		(unsigned long)nsrec.cnr_stobid.si_bits.u_hi,
		(unsigned long)nsrec.cnr_stobid.si_bits.u_lo);
	/*
	 * Temporary assignment for lsn
	 */
	fabrec.cfb_version.vn_lsn = C2_LSN_RESERVED_NR + 2;
	fabrec.cfb_version.vn_vc = 0;

	rc = c2_cob_create(dom, key, &nsrec, &fabrec, CA_NSKEY_FREE | CA_FABREC,
				&cob, tx);
	if (rc == 0)
		*out = cob;

	return rc;
}

int c2_rpc_cob_lookup_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx)
{
	struct c2_cob_nskey *key = NULL;
	uint64_t             pfid_hi;
	uint64_t             pfid_lo;
	int                  rc;

	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;
	if (pcob == NULL) {
		pfid_hi = pfid_lo = 1;
	} else {
		pfid_hi = COB_GET_PFID_HI(pcob);
		pfid_lo = COB_GET_PFID_LO(pcob);
	}

	c2_cob_nskey_make(&key, pfid_hi, pfid_lo, name);
	if (key == NULL)
		return -ENOMEM;
	rc = c2_cob_lookup(dom, key, CA_NSKEY_FREE | CA_FABREC, out, tx);

	C2_POST(ergo(rc == 0, *out != NULL));
	return rc;
}

int c2_rpc_root_session_cob_get(struct c2_cob_domain *dom,
				struct c2_cob       **out,
				struct c2_db_tx      *tx)
{
	return c2_rpc_cob_lookup_helper(dom, NULL, "SESSIONS", out, tx);
}

int c2_rpc_root_session_cob_create(struct c2_cob_domain *dom,
				   struct c2_cob       **out,
				   struct c2_db_tx      *tx)
{
	int rc;

	rc = c2_rpc_cob_create_helper(dom, NULL, "SESSIONS", out, tx);
	if (rc == -EEXIST)
		rc = 0;

	return rc;
}

int c2_rpc_conn_cob_lookup(struct c2_cob_domain *dom,
			   uint64_t              sender_id,
			   struct c2_cob       **out,
			   struct c2_db_tx      *tx)
{
	struct c2_cob *root_session_cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(sender_id != SENDER_ID_INVALID);

	rc = c2_rpc_root_session_cob_get(dom, &root_session_cob, tx);
	if (rc != 0)
		return rc;

	sprintf(name, "SENDER_%lu", (unsigned long)sender_id);

	rc = c2_rpc_cob_lookup_helper(dom, root_session_cob, name, out, tx);
	c2_cob_put(root_session_cob);

	return rc;
}

int c2_rpc_conn_cob_create(struct c2_cob_domain *dom,
			   uint64_t              sender_id,
			   struct c2_cob       **out,
			   struct c2_db_tx      *tx)
{
	struct c2_cob *conn_cob;
	struct c2_cob *root_session_cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(dom != NULL && out != NULL);
	C2_PRE(sender_id != SENDER_ID_INVALID);

	sprintf(name, "SENDER_%lu", (unsigned long)sender_id);
	*out = NULL;

	rc = c2_rpc_cob_lookup_helper(dom, NULL, "SESSIONS",
					&root_session_cob, tx);
	if (rc != 0) {
		C2_ASSERT(rc != -EEXIST);
		return rc;
	}
	rc = c2_rpc_cob_create_helper(dom, root_session_cob, name, &conn_cob,
					tx);
	if (rc == 0)
		*out = conn_cob;
	c2_cob_put(root_session_cob);
	return rc;
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

int c2_rpc_slot_cob_lookup(struct c2_cob   *session_cob,
			   uint32_t         slot_id,
			   uint64_t         slot_generation,
			   struct c2_cob  **slot_cob,
			   struct c2_db_tx *tx)
{
	struct c2_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(session_cob != NULL && slot_cob != NULL);

	*slot_cob = NULL;
	sprintf(name, "SLOT_%u:%lu", slot_id, (unsigned long)slot_generation);

	rc = c2_rpc_cob_lookup_helper(session_cob->co_dom, session_cob, name,
					&cob, tx);
	C2_ASSERT(ergo(rc != 0, cob == NULL));
	*slot_cob = cob;
	return rc;
}

int c2_rpc_slot_cob_create(struct c2_cob   *session_cob,
			   uint32_t         slot_id,
			   uint64_t         slot_generation,
			   struct c2_cob  **slot_cob,
			   struct c2_db_tx *tx)
{
	struct c2_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(session_cob != NULL && slot_cob != NULL);

	*slot_cob = NULL;
	sprintf(name, "SLOT_%u:%lu", slot_id, (unsigned long)slot_generation);

	rc = c2_rpc_cob_create_helper(session_cob->co_dom, session_cob, name,
					&cob, tx);
	C2_ASSERT(ergo(rc != 0, cob == NULL));
	*slot_cob = cob;
	return rc;
}

static uint64_t sender_id_get(void)
{
	static struct c2_atomic64 cnt;
	c2_time_t                 now;
	uint64_t                  sender_id;
	uint64_t                  sec;

	do {
		c2_atomic64_inc(&cnt);
		sec = c2_time_seconds(c2_time_now(&now));

		sender_id = (sec << 10) | (c2_atomic64_get(&cnt) & 0x3FF);

	} while (sender_id == SENDER_ID_INVALID || sender_id == 0);

	return sender_id;
}

uint64_t session_id_get(void)
{
	static struct c2_atomic64 cnt;
	c2_time_t                 now;
	uint64_t                  session_id;
	uint64_t                  sec;

	do {
		c2_atomic64_inc(&cnt);
		sec = c2_time_seconds(c2_time_now(&now));

		session_id = (sec << 10) | (c2_atomic64_get(&cnt) & 0x3FF);
	} while (session_id < SESSION_ID_MIN ||
			session_id > SESSION_ID_MAX);

	return session_id;
}

int c2_rpc_slot_init(struct c2_rpc_slot           *slot,
		     const struct c2_rpc_slot_ops *ops)
{
	struct c2_fop          *fop;
	struct c2_rpc_item     *dummy_item;
	struct c2_rpc_slot_ref *sref;

	c2_list_link_init(&slot->sl_link);
	/*
	 * XXX temporary value for lsn. This will be set to some proper value
	 * when sessions will be integrated with FOL
	 */
	slot->sl_verno.vn_lsn = C2_LSN_RESERVED_NR + 2;
	slot->sl_verno.vn_vc = 0;
	slot->sl_slot_gen = 0;
	slot->sl_xid = 1; /* xid 0 will be taken by dummy item */
	slot->sl_in_flight = 0;
	slot->sl_max_in_flight = SLOT_DEFAULT_MAX_IN_FLIGHT;
	c2_list_init(&slot->sl_item_list);
	c2_list_init(&slot->sl_ready_list);
	c2_mutex_init(&slot->sl_mutex);
	slot->sl_cob = NULL;
	slot->sl_ops = ops;

	/*
	 * Add a dummy item with very low verno in item_list
	 * The dummy item is used to avoid special cases
	 * i.e. last_sent == NULL, last_persistent == NULL
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_noop_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;

	dummy_item = &fop->f_item;
	dummy_item->ri_tstate = RPC_ITEM_PAST_COMMITTED;
	/* set ri_reply to some value. Doesn't matter what */
	dummy_item->ri_reply = dummy_item;
	slot->sl_last_sent = dummy_item;
	slot->sl_last_persistent = dummy_item;

	sref = &dummy_item->ri_slot_refs[0];
	sref->sr_slot = slot;
	sref->sr_item = dummy_item;
	sref->sr_xid = 0;
	/*
	 * XXX lsn will be assigned to some proper value once sessions code
	 * will be integrated with FOL
	 */
	sref->sr_verno.vn_lsn = C2_LSN_RESERVED_NR + 2;
	sref->sr_verno.vn_vc = 0;
	sref->sr_slot_gen = slot->sl_slot_gen;
	c2_list_link_init(&sref->sr_link);
	c2_list_add(&slot->sl_item_list, &sref->sr_link);

	return 0;
}

uint32_t c2_rpc_slot_items_possible_inflight(struct c2_rpc_slot *slot)
{
	C2_PRE(slot != NULL);

	return slot->sl_max_in_flight - slot->sl_in_flight;
}

/**
   If slot->sl_item_list has item(s) in state FUTURE then
	call slot->sl_ops->so_item_consume() for upto slot->sl_max_in_flight
	  number of (FUTURE)items. (On sender, each "consumed" item will be
	  given to formation for transmission. On receiver, "consumed" item is
	  "dispatched" to request handler for execution)
   else
	Notify that the slot is idle (i.e. call slot->sl_ops->so_slot_idle()

   if allow_events is false then items are not consumed.
   This is required when formation wants to add item to slot->sl_item_list
   but do not want item to be consumed.
 */
static void __slot_balance(struct c2_rpc_slot *slot,
			   bool                allow_events)
{
	struct c2_rpc_item  *item;
	struct c2_list_link *link;

	C2_PRE(slot != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_rpc_slot_invariant(slot));

	while (slot->sl_in_flight < slot->sl_max_in_flight) {
		/* Is slot->item_list is empty? */
		link = &slot->sl_last_sent->ri_slot_refs[0].sr_link;
		if (c2_list_link_is_last(link, &slot->sl_item_list)) {
			if (allow_events)
				slot->sl_ops->so_slot_idle(slot);
			break;
		}
		/* Take slot->last_sent->next item for sending */
		item = c2_list_entry(link->ll_next, struct c2_rpc_item,
				     ri_slot_refs[0].sr_link);
		if (item->ri_tstate == RPC_ITEM_FUTURE) {
			item->ri_tstate = RPC_ITEM_IN_PROGRESS;
			printf("Item %p IN_PROGRESS\n", item);
		}
		if (item->ri_reply != NULL && !c2_rpc_item_is_update(item)) {
			/*
			 * Don't send read only queries for which answer is
			 * already known
			 */
			continue;
		}
		slot->sl_last_sent = item;
		slot->sl_in_flight++;
		/*
		 * Tell formation module that an item is ready to be put in rpc
		 */
		if (allow_events)
			slot->sl_ops->so_item_consume(item);
	}
	C2_POST(c2_rpc_slot_invariant(slot));
}

/**
   For more information see __slot_balance()
   @see __slot_balance()
 */
static void slot_balance(struct c2_rpc_slot *slot)
{
	__slot_balance(slot, true);
}

/**
   @see c2_rpc_slot_item_add_internal()
 */
static void __slot_item_add(struct c2_rpc_slot *slot,
			    struct c2_rpc_item *item,
			    bool                allow_events)
{
	struct c2_rpc_slot_ref *sref;
	struct c2_rpc_session  *session;

	C2_PRE(slot != NULL && item != NULL && slot->sl_session != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_mutex_is_locked(&slot->sl_session->s_mutex));
	C2_PRE(slot->sl_session == item->ri_session);
	C2_PRE(c2_rpc_slot_invariant(slot));

	session = slot->sl_session;

	printf("slot_item_add: session %p(%lu)\n", session,
				(unsigned long)session->s_session_id);

	sref = &item->ri_slot_refs[0];
	item->ri_tstate = RPC_ITEM_FUTURE;
	sref->sr_session_id = session->s_session_id;
	sref->sr_sender_id = session->s_conn->c_sender_id;
	sref->sr_uuid = session->s_conn->c_uuid;
	printf("Itemp %p FUTURE\n", item);

	/*
	 * c2_rpc_slot_item_apply() will provide an item
	 * which already has verno initialised. Yet, following
	 * assignment should not be any problem because slot_item_apply()
	 * will call this routine only if verno of slot and item
	 * matches
	 */
	sref->sr_slot_id = slot->sl_slot_id;
	sref->sr_verno = slot->sl_verno;
	sref->sr_xid = slot->sl_xid;

	slot->sl_xid++;
	if (c2_rpc_item_is_update(item)) {
		/*
		 * When integrated with lsn,
		 * use c2_fol_lsn_allocate() to allocate new lsn and
		 * use c2_verno_inc() to advance vn_vc.
		 */
		slot->sl_verno.vn_lsn++;
		slot->sl_verno.vn_vc++;
	}

	sref->sr_slot_gen = slot->sl_slot_gen;
	sref->sr_slot = slot;
	sref->sr_item = item;
	c2_list_link_init(&sref->sr_link);
	c2_list_add_tail(&slot->sl_item_list, &sref->sr_link);

	if (session != NULL) {
		/*
		 * we're already under the cover of session->s_mutex
		 */
		session->s_nr_active_items++;
		if (session->s_state == C2_RPC_SESSION_IDLE) {
			printf("session %p marked BUSY\n", session);
			/*
			 * XXX When formation adds an item to
			 * c2_rpc_session::s_unbound_items it should
			 * set session->s_state as BUSY
			 */
			session->s_state = C2_RPC_SESSION_BUSY;
			c2_cond_broadcast(&session->s_state_changed,
					  &session->s_mutex);
		}
	}

	printf("item %p<%s> added [%lu:%lu] slot [%lu:%lu]\n", item,
			c2_rpc_item_is_update(item) ? "UPDATE" : "READ_ONLY",
			(unsigned long)sref->sr_verno.vn_vc,
			(unsigned long)sref->sr_xid,
			(unsigned long)slot->sl_verno.vn_vc,
			(unsigned long)slot->sl_xid);
	__slot_balance(slot, allow_events);
}

void c2_rpc_slot_item_add_internal(struct c2_rpc_slot *slot,
				   struct c2_rpc_item *item)
{
	C2_PRE(slot != NULL && item != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_mutex_is_locked(&slot->sl_session->s_mutex));
	C2_PRE(slot->sl_session == item->ri_session);

	__slot_item_add(slot, item,
			false);  /* slot is not allowed to trigger events */
}

int c2_rpc_slot_misordered_item_received(struct c2_rpc_slot *slot,
					 struct c2_rpc_item *item)
{
	struct c2_rpc_item *reply;
	struct c2_fop      *fop;

	/*
	 * Send a dummy NOOP fop as reply to report misordered item
	 * XXX We should've a special fop type to report session error
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_noop_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;

	reply = &fop->f_item;

	reply->ri_session = item->ri_session;
	reply->ri_error = -EBADR;

	reply->ri_slot_refs[0] = item->ri_slot_refs[0];
	c2_list_link_init(&reply->ri_slot_refs[0].sr_link);
	c2_list_link_init(&reply->ri_slot_refs[0].sr_ready_link);

	printf("Misordered item: %p, sending reply: %p\n", item, reply);
	slot->sl_ops->so_reply_consume(item, reply);
	return 0;
}

int c2_rpc_slot_item_apply(struct c2_rpc_slot *slot,
			   struct c2_rpc_item *item)
{
	struct c2_rpc_item *req;
	int                 redoable;
	int                 rc = 0;	/* init to 0, required */

	C2_ASSERT(slot != NULL && item != NULL);
	C2_ASSERT(c2_mutex_is_locked(&slot->sl_mutex));
	C2_ASSERT(c2_mutex_is_locked(&slot->sl_session->s_mutex));
	C2_ASSERT(c2_rpc_slot_invariant(slot));

	printf("Applying item [%lu:%lu] on slot [%lu:%lu]\n",
			(unsigned long)item->ri_slot_refs[0].sr_verno.vn_vc,
			(unsigned long)item->ri_slot_refs[0].sr_xid,
			(unsigned long)slot->sl_verno.vn_vc,
			(unsigned long)slot->sl_xid);
	redoable = c2_verno_is_redoable(&slot->sl_verno,
					&item->ri_slot_refs[0].sr_verno,
					false);
	printf("redoable: %d\n", redoable);
	switch (redoable) {
	case 0:
		printf("Applying item %p\n", item);
		__slot_item_add(slot, item, true);
		break;
	case -EALREADY:
		req = item_find(slot, item->ri_slot_refs[0].sr_xid);
		if (req == NULL) {
			rc = c2_rpc_slot_misordered_item_received(slot,
								 item);
			break;
		}
		/*
		 * XXX At this point req->ri_slot_refs[0].sr_verno and
		 * item->ri_slot_refs[0].sr_verno MUST be same. If they are
		 * not same then generate ADDB record.
		 * For now, assert this condition for testing purpose.
		 */
		C2_ASSERT(c2_verno_cmp(&req->ri_slot_refs[0].sr_verno,
				&item->ri_slot_refs[0].sr_verno) == 0);

		switch (req->ri_tstate) {
		case RPC_ITEM_PAST_VOLATILE:
		case RPC_ITEM_PAST_COMMITTED:
			/*
			 * @item is duplicate and corresponding original is
			 * already consumed (i.e. executed if item is FOP).
			 * Consume cached reply. (on receiver, this means
			 * resend cached reply)
			 */
			C2_ASSERT(req->ri_reply != NULL);
			printf("resending reply: req %p reply %p\n",
					req, req->ri_reply);
			slot->sl_ops->so_reply_consume(req,
						req->ri_reply);
			break;
		case RPC_ITEM_IN_PROGRESS:
		case RPC_ITEM_FUTURE:
			/* item is already present but is not
			   processed yet. Ignore it*/
			/* do nothing */;
			printf("ignoring item: %p\n", item);
		}
		break;
	case -EAGAIN:
		rc = c2_rpc_slot_misordered_item_received(slot, item);
		break;
	}
	C2_ASSERT(c2_rpc_slot_invariant(slot));
	return rc;
}

static struct c2_rpc_item* item_find(const struct c2_rpc_slot *slot,
				     uint64_t                  xid)
{
	struct c2_rpc_item *item;

	C2_PRE(slot != NULL);

	c2_list_for_each_entry(&slot->sl_item_list, item, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {

		if (item->ri_slot_refs[0].sr_xid == xid)
			return item;
	}
	return NULL;
}

void c2_rpc_slot_reply_received(struct c2_rpc_slot  *slot,
				struct c2_rpc_item  *reply,
				struct c2_rpc_item **req_out)
{
	struct c2_rpc_item     *req;
	struct c2_rpc_slot_ref *sref;
	struct c2_rpc_session  *session;

	C2_PRE(slot != NULL && reply != NULL && req_out != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_mutex_is_locked(&slot->sl_session->s_mutex));

	*req_out = NULL;

	sref = &reply->ri_slot_refs[0];
	C2_ASSERT(slot == sref->sr_slot);

	req = item_find(slot, reply->ri_slot_refs[0].sr_xid);
	if (req == NULL) {
		/*
		 * Either it is a duplicate reply and its corresponding request
		 * item is pruned from the item list, or it is a corrupted
		 * reply
		 */
		return;
	}
	/*
	 * XXX At this point req->ri_slot_refs[0].sr_verno and
	 * reply->ri_slot_refs[0].sr_verno MUST be same. If they are not,
	 * then generate ADDB record.
	 * For now, assert this condition for testing purpose.
	 */
	C2_ASSERT(c2_verno_cmp(&req->ri_slot_refs[0].sr_verno,
			&reply->ri_slot_refs[0].sr_verno) == 0);

	if (c2_verno_cmp(&req->ri_slot_refs[0].sr_verno,
		&slot->sl_last_sent->ri_slot_refs[0].sr_verno) > 0) {
		/*
		 * Received a reply to an item that wasn't sent. This is
		 * possible if the receiver failed and forget about some
		 * items. The sender moved last_seen to the past, but then a
		 * late reply to one of items unreplied before the failure
		 * arrived.
		 *
		 * Such reply must be ignored
		 */
		;
	} else if (req->ri_tstate == RPC_ITEM_PAST_COMMITTED ||
			req->ri_tstate == RPC_ITEM_PAST_VOLATILE) {
		/*
		 * Got a reply to an item for which the reply was already
		 * received in the past. Compare with the original reply.
		 * XXX find out how to compare two rpc items to be same
		 */
		printf("got duplicate reply for %p\n", req);
	} else {
		C2_ASSERT(req->ri_tstate == RPC_ITEM_IN_PROGRESS);
		C2_ASSERT(slot->sl_in_flight > 0);

		req->ri_tstate = RPC_ITEM_PAST_VOLATILE;
		printf("Item %p PAST_VOLATILE\n", req);
		req->ri_reply = reply;
		*req_out = req;
		slot->sl_in_flight--;

		session = slot->sl_session;
		if (session != NULL) {
			C2_ASSERT(session->s_state == C2_RPC_SESSION_BUSY);
			session->s_nr_active_items--;
			if (session->s_nr_active_items == 0 &&
				c2_list_is_empty(&session->s_unbound_items)) {
				printf("session %p marked IDLE\n", session);
				session->s_state = C2_RPC_SESSION_IDLE;
			}
		}
		slot->sl_ops->so_reply_consume(req, reply);
		slot_balance(slot);
	}
}

void c2_rpc_slot_persistence(struct c2_rpc_slot *slot,
			     struct c2_verno     last_persistent)
{
	struct c2_rpc_item     *item;
	struct c2_list_link    *link;

	C2_PRE(slot != NULL && c2_mutex_is_locked(&slot->sl_mutex));

	/*
	 * From last persistent item to end of slot->item_list,
	 *    if item->verno <= @last_persistent
	 *       Mark item as PAST_COMMITTED
	 *    else
	 *       break
	 */
	link = &slot->sl_last_persistent->ri_slot_refs[0].sr_link;
	for (; link != (void *)&slot->sl_item_list; link = link->ll_next) {

		item = c2_list_entry(link, struct c2_rpc_item,
					ri_slot_refs[0].sr_link);

		if (c2_verno_cmp(&item->ri_slot_refs[0].sr_verno,
				&last_persistent) <= 0) {

			C2_ASSERT(item->ri_tstate == RPC_ITEM_PAST_COMMITTED ||
				  item->ri_tstate == RPC_ITEM_PAST_VOLATILE);

			item->ri_tstate = RPC_ITEM_PAST_COMMITTED;
			slot->sl_last_persistent = item;
		} else {
			break;
		}
	}
	C2_POST(
	   c2_verno_cmp(&slot->sl_last_persistent->ri_slot_refs[0].sr_verno,
			&last_persistent) >= 0);
}

void c2_rpc_slot_reset(struct c2_rpc_slot *slot,
		       struct c2_verno     last_seen)
{
	struct c2_rpc_item     *item;
	struct c2_rpc_slot_ref *sref;

	C2_PRE(slot != NULL && c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_verno_cmp(&slot->sl_verno, &last_seen) >= 0);

	c2_list_for_each_entry(&slot->sl_item_list, item, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {
		sref = &item->ri_slot_refs[0];
		if (c2_verno_cmp(&sref->sr_verno, &last_seen) == 0) {
			C2_ASSERT(item->ri_tstate != RPC_ITEM_FUTURE);
			slot->sl_last_sent = item;
			break;
		}
	}
	C2_ASSERT(c2_verno_cmp(&slot->sl_last_sent->ri_slot_refs[0].sr_verno,
				&last_seen) == 0);
	slot_balance(slot);
}

bool c2_rpc_slot_invariant(const struct c2_rpc_slot *slot)
{
	struct c2_rpc_item *item1 = NULL;  /* init to NULL, required */
	struct c2_rpc_item *item2;
	struct c2_verno    *v1;
	struct c2_verno    *v2;
	bool                ret = true;   /* init to true, required */

	if (slot == NULL)
		return false;

	/*
	 * Traverse slot->sl_item_list using item2 ptr
	 * item1 will be previous item of item2 i.e.
	 * next(item1) == item2
	 */
	c2_list_for_each_entry(&slot->sl_item_list, item2, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {
		if (item1 == NULL) {
			/*
			 * First element is dummy item. So no need to check.
			 */
			item1 = item2;
			continue;
		}
		ret = ergo(item2->ri_tstate == RPC_ITEM_PAST_VOLATILE ||
			   item2->ri_tstate == RPC_ITEM_PAST_COMMITTED,
			   item2->ri_reply != NULL);
		if (!ret)
			break;

		ret = (item1->ri_tstate <= item2->ri_tstate);
		if (!ret)
			break;

		v1 = &item1->ri_slot_refs[0].sr_verno;
		v2 = &item2->ri_slot_refs[0].sr_verno;

		/*
		 * AFTER an "update" item is applied on a slot
		 * the version number of slot is advanced
		 */
		ret = c2_rpc_item_is_update(item1) ?
			v1->vn_vc + 1 == v2->vn_vc :
			v1->vn_vc == v2->vn_vc;
		if (!ret)
			break;

		ret = (item1->ri_slot_refs[0].sr_xid + 1 ==
			item2->ri_slot_refs[0].sr_xid);
		if (!ret)
			break;

		item1 = item2;
	}
	return ret;
}

/**
  Frees all the items from slot->sl_item_list except dummy_item.

  XXX This is temporary. This routine will be scraped entirely.
  When slots will be integrated with FOL, there will be some pruning mechanism
  that will evict items from slot's item_list. But for now, we need to be able
  to fini() slot for testing purpose. That's why freeing the items explicitly.
 */
static void slot_item_list_prune(struct c2_rpc_slot *slot)
{
	struct c2_rpc_item  *item;
	struct c2_rpc_item  *reply;
	struct c2_rpc_item  *next;
	struct c2_rpc_item  *dummy_item;
	struct c2_fop       *fop;
	struct c2_list_link *link;
	int                  count = 0;
	bool                 first_item = true;

	/*
	 * XXX See comments above function prototype
	 */
	C2_ASSERT(slot != NULL);
	printf("item_list_prune: slot %p [%lu:%u]\n", slot,
			(unsigned long)slot->sl_session->s_session_id,
			slot->sl_slot_id);

	c2_list_for_each_entry_safe(&slot->sl_item_list, item, next,
			struct c2_rpc_item, ri_slot_refs[0].sr_link) {
		if (first_item) {
			/*
			 * Don't delete dummy item
			 */
			first_item = false;
			continue;
		}
		reply = item->ri_reply;
		if (reply != NULL) {
			fop = c2_rpc_item_to_fop(reply);
			c2_fop_free(fop);
		}
		item->ri_reply = NULL;

		c2_list_del(&item->ri_slot_refs[0].sr_link);
		fop = c2_rpc_item_to_fop(item);
		c2_fop_free(fop);
		count++;
	}
        C2_ASSERT(c2_list_length(&slot->sl_item_list) == 1);

        link = c2_list_first(&slot->sl_item_list);
        C2_ASSERT(link != NULL);

        dummy_item = c2_list_entry(link, struct c2_rpc_item,
                                   ri_slot_refs[0].sr_link);
        C2_ASSERT(c2_list_link_is_in(&dummy_item->ri_slot_refs[0].sr_link));

	slot->sl_last_sent = dummy_item;
	slot->sl_last_persistent = dummy_item;
	printf("item_list_prune: pruned %d entries\n", count);
}

void c2_rpc_slot_fini(struct c2_rpc_slot *slot)
{
	struct c2_rpc_item  *dummy_item;
	struct c2_fop       *fop;
	struct c2_list_link *link;

	printf("slot_fini: %p\n", slot);
	slot_item_list_prune(slot);
	c2_list_link_fini(&slot->sl_link);
	c2_list_fini(&slot->sl_ready_list);

	/*
	 * Remove the dummy item from the list
	 */
	C2_ASSERT(c2_list_length(&slot->sl_item_list) == 1);

	link = c2_list_first(&slot->sl_item_list);
	C2_ASSERT(link != NULL);

	dummy_item = c2_list_entry(link, struct c2_rpc_item,
				ri_slot_refs[0].sr_link);
	C2_ASSERT(c2_list_link_is_in(&dummy_item->ri_slot_refs[0].sr_link));

	c2_list_del(&dummy_item->ri_slot_refs[0].sr_link);
	C2_ASSERT(dummy_item->ri_slot_refs[0].sr_xid == 0);

	fop = c2_rpc_item_to_fop(dummy_item);
	c2_fop_free(fop);

	c2_list_fini(&slot->sl_item_list);
	if (slot->sl_cob != NULL) {
		c2_cob_put(slot->sl_cob);
	}
	C2_SET0(slot);
}

static void snd_slot_idle(struct c2_rpc_slot *slot)
{
	printf("sender_slot_idle called %p\n", slot);
	c2_rpc_frm_slot_idle(slot);
}

static void snd_item_consume(struct c2_rpc_item *item)
{
	printf("sender_consume_item called %p\n", item);
	c2_rpc_frm_item_ready(item);
}

static void snd_reply_consume(struct c2_rpc_item *req,
			      struct c2_rpc_item *reply)
{
	printf("snd_reply_consume called %p %p\n", req, reply);
	/* Don't do anything on sender to consume reply */
}

static void rcv_slot_idle(struct c2_rpc_slot *slot)
{
	printf("rcv_slot_idle called %p [%lu:%lu]\n", slot,
			(unsigned long)slot->sl_verno.vn_vc,
			(unsigned long)slot->sl_xid);
	c2_rpc_frm_slot_idle(slot);
}

static void rcv_item_consume(struct c2_rpc_item *item)
{
	printf("rcv_item_consume called %p\n", item);
	item_dispatch(item);
}

static void rcv_reply_consume(struct c2_rpc_item *req,
			      struct c2_rpc_item *reply)
{
	printf("rcv_consume_reply called %p %p\n", req, reply);
	c2_rpc_frm_item_ready(reply);
}

static int conn_persistent_state_create(struct c2_cob_domain *dom,
					uint64_t              sender_id,
					struct c2_cob       **conn_cob_out,
					struct c2_cob       **session0_cob_out,
					struct c2_cob       **slot0_cob_out,
					struct c2_db_tx      *tx)
{
	struct c2_cob *conn_cob;
	struct c2_cob *session0_cob;
	struct c2_cob *slot0_cob;
	int            rc;

	*conn_cob_out = *session0_cob_out = *slot0_cob_out = NULL;

	rc = c2_rpc_conn_cob_create(dom, sender_id, &conn_cob, tx);
	if (rc != 0)
		goto errout;

	rc = c2_rpc_session_cob_create(conn_cob, SESSION_ID_0, &session0_cob, tx);
	if (rc != 0) {
		c2_cob_put(conn_cob);
		goto errout;
	}

	rc = c2_rpc_slot_cob_create(session0_cob,
					0,      /* Slot id */
					0,      /* slot generation */
					&slot0_cob, tx);
	if (rc != 0) {
		c2_cob_put(session0_cob);
		c2_cob_put(conn_cob);
		goto errout;
	}

	*conn_cob_out = conn_cob;
	*session0_cob_out = session0_cob;
	*slot0_cob_out = slot0_cob;
	return 0;

errout:
	*conn_cob_out = NULL;
	*session0_cob_out = NULL;
	*slot0_cob_out = NULL;
	return rc;
}

static int conn_persistent_state_attach(struct c2_rpc_conn *conn,
				        uint64_t            sender_id,
				        struct c2_db_tx    *tx)
{
	struct c2_rpc_session *session0;
	struct c2_rpc_slot    *slot0;
	struct c2_cob         *conn_cob;
	struct c2_cob         *session0_cob;
	struct c2_cob         *slot0_cob;
	struct c2_cob_domain  *dom;
	int                    rc;

	C2_PRE(conn != NULL && conn->c_state == C2_RPC_CONN_INITIALISED &&
			c2_rpc_conn_invariant(conn));

	printf("persistent_state_attach: sender_id %lu\n",
		(unsigned long)sender_id);
	dom = conn->c_rpcmachine->cr_dom;
	rc = conn_persistent_state_create(dom, sender_id,
					  &conn_cob, &session0_cob, &slot0_cob,
					  tx);
	if (rc != 0)
		return rc;

	C2_ASSERT(conn_cob != NULL && session0_cob != NULL &&
			slot0_cob != NULL);
	conn->c_cob = conn_cob;

	session0 = c2_rpc_conn_session0(conn);
	session0->s_cob = session0_cob;

	slot0 = session0->s_slot_table[0];
	C2_ASSERT(slot0 != NULL);
	slot0->sl_cob = slot0_cob;

	return 0;
}

int c2_rpc_rcv_conn_establish(struct c2_rpc_conn *conn)
{
	struct c2_rpcmachine *machine;
	struct c2_db_tx       tx;
	uint64_t              sender_id;
	int                   rc;

	C2_PRE(conn != NULL);
	C2_PRE(conn->c_state == C2_RPC_CONN_INITIALISED && conn_is_rcv(conn));

	C2_ASSERT(c2_rpc_conn_invariant(conn));

	machine = conn->c_rpcmachine;
	C2_ASSERT(machine != NULL && machine->cr_dom != NULL);

	c2_db_tx_init(&tx, machine->cr_dom->cd_dbenv, 0);
	sender_id = sender_id_get();
	rc = conn_persistent_state_attach(conn, sender_id,
					  &tx);
	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = rc;
		c2_db_tx_abort(&tx);
		C2_ASSERT(c2_rpc_conn_invariant(conn));
		return rc;
	}
	c2_db_tx_commit(&tx);
	conn->c_sender_id = sender_id;
	conn->c_rpcchan = c2_rpc_chan_get(conn->c_rpcmachine);
	conn->c_state = C2_RPC_CONN_ACTIVE;
	c2_mutex_lock(&machine->cr_session_mutex);
	c2_list_add(&machine->cr_incoming_conns, &conn->c_link);
	c2_mutex_unlock(&machine->cr_session_mutex);
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	return 0;
}

int c2_rpc_rcv_session_establish(struct c2_rpc_session *session)
{
	struct c2_db_tx tx;
	uint64_t        session_id;
	int             rc;

	C2_PRE(session != NULL &&
		session->s_state == C2_RPC_SESSION_INITIALISED);

	C2_ASSERT(c2_rpc_session_invariant(session));

	c2_db_tx_init(&tx, session->s_conn->c_cob->co_dom->cd_dbenv, 0);
	session_id = session_id_get();
	rc = session_persistent_state_attach(session, session_id, &tx);
	if (rc != 0) {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = rc;
		c2_db_tx_abort(&tx);
		C2_ASSERT(c2_rpc_session_invariant(session));
		return rc;
	}
	c2_db_tx_commit(&tx);
	session->s_session_id = session_id;
	session->s_state = C2_RPC_SESSION_IDLE;

	/*
	 * As session_[create|terminate] request arrive on same slot,
	 * they are already serialised. So no need to take lock on
	 * conn object.
	 */
	c2_list_add(&session->s_conn->c_sessions, &session->s_link);
	session->s_conn->c_nr_sessions++;
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(c2_rpc_conn_invariant(session->s_conn));
	return 0;
}

static int session_persistent_state_create(struct c2_cob    *conn_cob,
					   uint64_t          session_id,
					   struct c2_cob   **session_cob_out,
					   struct c2_cob   **slot_cob_array_out,
					   uint32_t          nr_slots,
					   struct c2_db_tx  *tx)
{
	struct c2_cob *session_cob;
	struct c2_cob *slot_cob;
	int            rc;
	int            i;

	*session_cob_out = NULL;
	for (i = 0; i < nr_slots; i++)
		slot_cob_array_out[i] = NULL;

	rc = c2_rpc_session_cob_create(conn_cob, session_id, &session_cob, tx);
	if (rc != 0)
		goto errout;

	for (i = 0; i < nr_slots; i++) {
		rc = c2_rpc_slot_cob_create(session_cob, i, 0, &slot_cob, tx);
		if (rc != 0)
			goto errout;
		slot_cob_array_out[i] = slot_cob;
	}
	*session_cob_out = session_cob;
	return 0;
errout:
	for (i = 0; i < nr_slots; i++)
		if (slot_cob_array_out[i] != NULL) {
			c2_cob_put(slot_cob_array_out[i]);
			slot_cob_array_out[i] = NULL;
		}
	if (session_cob != NULL) {
		c2_cob_put(session_cob);
		*session_cob_out = NULL;
	}

	return rc;
}

static int session_persistent_state_attach(struct c2_rpc_session *session,
					   uint64_t               session_id,
					   struct c2_db_tx       *tx)
{
	struct c2_rpc_slot *slot;
	struct c2_cob      *session_cob;
	struct c2_cob     **slot_cobs;
	int                 rc;
	int                 i;

	C2_PRE(session != NULL &&
	       session->s_state == C2_RPC_SESSION_INITIALISED &&
	       session->s_nr_slots > 0);
	C2_PRE(session->s_conn != NULL && session->s_conn->c_cob != NULL);

	C2_ALLOC_ARR(slot_cobs, session->s_nr_slots);
	if (slot_cobs == NULL)
		return -ENOMEM;

	rc = session_persistent_state_create(session->s_conn->c_cob, session_id,
						&session_cob, &slot_cobs[0],
						session->s_nr_slots, tx);
	if (rc != 0)
		return rc;

	C2_ASSERT(session->s_cob == NULL && session_cob != NULL);
	session->s_cob = session_cob;
	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		C2_ASSERT(slot != NULL && slot->sl_cob == NULL &&
				slot_cobs[i] != NULL);
		slot->sl_cob = slot_cobs[i];
		C2_ASSERT(slot_cobs[i]->co_dom->cd_dbenv != NULL);
	}
	return 0;
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
	struct c2_rpc_slot *slot;
	struct c2_db_tx	    tx;
	int                 rc;
	int                 i;

	C2_PRE(session != NULL && session->s_state == C2_RPC_SESSION_IDLE);

	C2_ASSERT(c2_rpc_session_invariant(session));

	session->s_state = C2_RPC_SESSION_TERMINATING;

	/*
	 * Take all the slots out of c2_rpcmachine::cr_ready_slots
	 */
	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		if (c2_list_link_is_in(&slot->sl_link))
			/*lock on ready slot list??? */
			c2_list_del(&slot->sl_link);
		/*
		 * XXX slot->sl_link and ready slot lst is completely managed
		 * by formation.
		 * So instead of session trying to remove slot from ready list
		 * directly, there should be one more event generated by slot
		 * and handled by formation, saying "stop using slot".
		 * In the event handler, formation can remove the slot from
		 * ready slot list.
		 */
	}
	c2_mutex_lock(&session->s_mutex);
	c2_db_tx_init(&tx, session->s_cob->co_dom->cd_dbenv, 0);
	rc = session_persistent_state_destroy(session, &tx);
	if (rc != 0) {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = rc;
		c2_db_tx_abort(&tx);
		C2_ASSERT(c2_rpc_session_invariant(session));
		c2_mutex_unlock(&session->s_mutex);
		return rc;
	}
	c2_db_tx_commit(&tx);
	c2_mutex_lock(&session->s_conn->c_mutex);
	c2_list_del(&session->s_link);
	session->s_conn->c_nr_sessions--;
	c2_mutex_unlock(&session->s_conn->c_mutex);
	session->s_state = C2_RPC_SESSION_TERMINATED;
	session->s_rc = 0;
	session->s_conn = NULL;
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_mutex_unlock(&session->s_mutex);
	return 0;
}

static int conn_persistent_state_destroy(struct c2_rpc_conn *conn,
					 struct c2_db_tx    *tx)
{
	struct c2_rpc_session *session0;
	struct c2_rpc_slot    *slot0;

	session0 = c2_rpc_conn_session0(conn);
	slot0 = session0->s_slot_table[0];
	C2_ASSERT(slot0 != NULL && c2_rpc_slot_invariant(slot0));

	C2_ASSERT(conn->c_cob != NULL && session0->s_cob != NULL &&
			slot0->sl_cob != NULL);
	c2_cob_delete(conn->c_cob, tx);
	c2_cob_delete(session0->s_cob, tx);
	c2_cob_delete(slot0->sl_cob, tx);

	conn->c_cob = session0->s_cob = slot0->sl_cob = NULL;
	return 0;
}

int c2_rpc_rcv_conn_terminate(struct c2_rpc_conn *conn)
{
	struct c2_db_tx tx;

	C2_PRE(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE);

	C2_ASSERT(conn_is_rcv(conn) && c2_rpc_conn_invariant(conn));

	if (conn->c_nr_sessions > 0) {
		return -EBUSY;
	}
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	conn->c_state = C2_RPC_CONN_TERMINATING;
	c2_db_tx_init(&tx, conn->c_cob->co_dom->cd_dbenv, 0);
	conn_persistent_state_destroy(conn, &tx);
	c2_db_tx_commit(&tx);
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	/* In-core state will be cleaned up by conn_terminate_reply_sent() */
	return 0;
}

void c2_rpc_conn_terminate_reply_sent(struct c2_rpc_conn *conn)
{
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_TERMINATING);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	c2_mutex_lock(&conn->c_rpcmachine->cr_session_mutex);
	c2_list_del(&conn->c_link);
	c2_mutex_unlock(&conn->c_rpcmachine->cr_session_mutex);
	conn->c_state = C2_RPC_CONN_TERMINATED;
	conn->c_sender_id = SENDER_ID_INVALID;
	conn->c_rc = 0;
	conn->c_rpcmachine = NULL;
	conn->c_end_point = NULL;
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_rpc_conn_fini(conn);
	c2_free(conn);
}

bool c2_rpc_item_is_conn_establish(const struct c2_rpc_item *item)
{
	return item->ri_type == &c2_rpc_item_conn_establish;
}

static void item_dispatch(struct c2_rpc_item *item)
{
	printf("Executing %p\n", item);
	c2_queue_put(&exec_queue, &item->ri_dummy_qlinkage);
	c2_chan_broadcast(&exec_chan);
}

static int associate_session_and_slot(struct c2_rpc_item *item)
{
	struct c2_list         *conn_list;
	struct c2_rpc_conn     *conn;
	struct c2_rpc_session  *session;
	struct c2_rpc_slot     *slot;
	struct c2_rpc_slot_ref *sref;
	bool                    found;
	bool                    use_uuid;

	sref = &item->ri_slot_refs[0];
	if (sref->sr_session_id > SESSION_ID_MAX)
		return -EINVAL;

	printf("associate_session: [%lu:%lu:%u]\n",
			(unsigned long)sref->sr_sender_id,
			(unsigned long)sref->sr_session_id,
			sref->sr_slot_id);
	conn_list = c2_rpc_item_is_request(item) ?
			&item->ri_mach->cr_incoming_conns :
			&item->ri_mach->cr_outgoing_conns;

	use_uuid = (sref->sr_sender_id == SENDER_ID_INVALID);
	printf("associate_session: uuid %s\n", use_uuid ? "true" : "false");

	c2_mutex_lock(&item->ri_mach->cr_session_mutex);
	c2_list_for_each_entry(conn_list, conn, struct c2_rpc_conn, c_link) {
		printf("associate_session: conn %lu\n",
			(unsigned long)conn->c_sender_id);
		found = use_uuid ?
			c2_rpc_sender_uuid_cmp(&conn->c_uuid,
					       &sref->sr_uuid) == 0 :
			conn->c_sender_id == sref->sr_sender_id;
		if (found)
			break;
	}
	c2_mutex_unlock(&item->ri_mach->cr_session_mutex);
	if (!found) {
		printf("associate_session: cannot find conn\n");
		return -ENOENT;
	}
	c2_mutex_lock(&conn->c_mutex);
	session = c2_rpc_session_search(conn, sref->sr_session_id);
	c2_mutex_unlock(&conn->c_mutex);
	if (session == NULL) {
		printf("associate_session: cannot find session\n");
		return -ENOENT;
	}
	c2_mutex_lock(&session->s_mutex);
	if (sref->sr_slot_id >= session->s_nr_slots) {
		c2_mutex_unlock(&session->s_mutex);
		printf("associate_session: failed item slot id %u nr slot %u\n",
				sref->sr_slot_id, session->s_nr_slots);
		return -ENOENT;
	}
	slot = session->s_slot_table[sref->sr_slot_id];
	/* XXX Check generation of slot */
	C2_ASSERT(slot != NULL);
	sref->sr_slot = slot;
	item->ri_session = session;
	C2_POST(item->ri_session != NULL &&
		item->ri_slot_refs[0].sr_slot != NULL);
	c2_mutex_unlock(&session->s_mutex);

	printf("associate_session: successful\n");
	return 0;
}

int c2_rpc_item_received(struct c2_rpc_item *item)
{
	struct c2_rpc_item *req;
	struct c2_rpc_slot *slot;
	int                 rc;

	C2_ASSERT(item != NULL && item->ri_mach != NULL);
	printf("item_received: %p\n", item);
	rc = associate_session_and_slot(item);
	c2_rpc_item_exit_stats_set(item, INCOMING);
	if (rc != 0) {
		if (c2_rpc_item_is_conn_establish(item)) {
			item_dispatch(item);
			return 0;
		}
		/*
		 * If we cannot associate the item with its slot
		 * then there is nothing that we can do with this
		 * item except to discard it.
		 */
		printf("item_received: rc != 0\n");
		return rc;
	}
	C2_ASSERT(item->ri_session != NULL &&
		  item->ri_slot_refs[0].sr_slot != NULL);

	slot = item->ri_slot_refs[0].sr_slot;
	c2_mutex_lock(&slot->sl_mutex);
	c2_mutex_lock(&slot->sl_session->s_mutex);
	if (c2_rpc_item_is_request(item)) {
		printf("IR: item %p is REQUEST\n", item);
		c2_rpc_slot_item_apply(slot, item);

		c2_mutex_unlock(&slot->sl_session->s_mutex);
		c2_mutex_unlock(&slot->sl_mutex);
	} else {
		printf("IR: item %p is REPLY\n", item);
		c2_rpc_slot_reply_received(slot, item, &req);

		c2_mutex_unlock(&slot->sl_session->s_mutex);
		c2_mutex_unlock(&slot->sl_mutex);

		/*
		 * In case the reply is duplicate/unwanted then
		 * c2_rpc_slot_reply_received() sets req to NULL.
		 */
		if (req != NULL) {
			/* Send reply received event to formation component.*/
			rc = c2_rpc_frm_item_reply_received(item,
					req);
		}

		if (req != NULL && req->ri_ops != NULL &&
		    req->ri_ops->rio_replied != NULL) {
			req->ri_ops->rio_replied(req, item, 0);
		}
	}
	printf("item_received: %p finished\n", item);
	return 0;
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
