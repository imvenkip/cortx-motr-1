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
 * Original creation date: 08/24/2011
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
#include "rpc/session_fops.h"
#include "rpc/rpccore.h"
#include "rpc/formation.h"

/**
   @addtogroup rpc_session

   @{
 */

static const char conn_cob_name_fmt[] = "SENDER_%lu";

/**
   Attaches session 0 object to conn object.
 */
static int session_zero_attach(struct c2_rpc_conn *conn);

/**
   Detaches session 0 from conn
 */
static void session_zero_detach(struct c2_rpc_conn *conn);

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
   Checks connection object invariant.

   Function is also called from session_foms.c, hence cannot be static.
 */
bool c2_rpc_conn_invariant(const struct c2_rpc_conn *conn)
{
	struct c2_list *conn_list;
	bool            sender_end;
	bool            recv_end;
	bool            ret;

	if (conn == NULL)
		return false;

	sender_end = c2_rpc_conn_is_snd(conn);
	recv_end = c2_rpc_conn_is_rcv(conn);

	/*
	 * conditions that should be true irrespective of conn state
	 */
	ret = sender_end != recv_end && c2_list_invariant(&conn->c_sessions) &&
		conn->c_rpcmachine != NULL &&
		conn->c_end_point != NULL &&
		c2_list_length(&conn->c_sessions) == conn->c_nr_sessions + 1;

	if (!ret)
		return ret;

        /*
         * Exactly one state bit must be set.
         */
        if (!c2_is_po2(conn->c_state))
                return false;

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
			conn->c_nr_sessions == 0 &&
			conn->c_cob == NULL &&
			conn->c_rc == 0;

	case C2_RPC_CONN_FAILED:
		return conn->c_rc != 0 &&
			!c2_list_link_is_in(&conn->c_link);

	default:
		return false;
	}
	/* Should never reach here */
	C2_ASSERT(0);
}

/**
   Common code in c2_rpc_conn_fini() and init failed case in __conn_init()
 */
static void __conn_fini(struct c2_rpc_conn *conn)
{
	C2_ASSERT(conn != NULL);

	c2_net_end_point_put(conn->c_end_point);
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
		c2_rpc_conn_is_snd(conn) != c2_rpc_conn_is_rcv(conn));

	c2_net_end_point_get(ep);
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
	rc = __conn_init(conn, ep, machine);

	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
		  c2_rpc_conn_invariant(conn) && c2_rpc_conn_is_snd(conn)));

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
		  c2_rpc_conn_invariant(conn) && c2_rpc_conn_is_rcv(conn)));

	return rc;
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
	bool got_event = true;
	bool result;

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
	result = ((conn->c_state & state_flags) != 0);
	c2_mutex_unlock(&conn->c_mutex);

	return result;
}
C2_EXPORTED(c2_rpc_conn_timedwait);

struct c2_rpc_session *
c2_rpc_session_search(const struct c2_rpc_conn *conn,
		      uint64_t                  session_id)
{
	struct c2_rpc_session *session;

	/*
	 * Caller is expected to decide whether conn->c_mutex should be held
	 * or not. There are situations where it is safe to call this
	 * routine without holding conn->c_mutex.
	 * e.g. c2_rpc_conn_fini() => session_zero_detach() =>
	 *         c2_rpc_conn_session0() => c2_rpc_session_search()
	 * Safety from concurrency is ensured in this case because caller of
	 * c2_rpc_conn_fini() takes care that the caller is the only user of
	 * conn
	 */
	C2_ASSERT(conn != NULL);

	c2_rpc_for_each_session(conn, session) {
		if (session->s_session_id == session_id)
			return session;
	}
	return NULL;
}

/**
   Searches and returns session with session id 0.
   Note: Every rpc connection always has exactly one active session with
   session id 0.
 */
struct c2_rpc_session *c2_rpc_conn_session0(const struct c2_rpc_conn *conn)
{
	struct c2_rpc_session *session0;

	session0 = c2_rpc_session_search(conn, SESSION_ID_0);

	C2_ASSERT(session0 != NULL);
	return session0;
}

/**
   Returns true iff @conn is sender end of rpc connection.
 */
bool c2_rpc_conn_is_snd(const struct c2_rpc_conn *conn)
{
	return (conn->c_flags & RCF_SENDER_END) == RCF_SENDER_END;
}

/**
   Returns true iff @conn is receiver end of rpc connection.
 */
bool c2_rpc_conn_is_rcv(const struct c2_rpc_conn *conn)
{
	return (conn->c_flags & RCF_RECV_END) == RCF_RECV_END;
}

/**
   Allocates and returns new sender_id
 */
static uint64_t sender_id_allocate(void)
{
	static struct c2_atomic64 cnt;
	uint64_t                  sender_id;
	uint64_t                  sec;

	do {
		c2_atomic64_inc(&cnt);
		sec = c2_time_seconds(c2_time_now());

		sender_id = (sec << 10) | (c2_atomic64_get(&cnt) & 0x3FF);

	} while (sender_id == SENDER_ID_INVALID || sender_id == 0);

	return sender_id;
}

/**
   Moves @conn to C2_RPC_CONN_FAILED state, setting error code to @error.
 */
static void conn_failed(struct c2_rpc_conn *conn, int32_t error)
{
	C2_ASSERT(c2_mutex_is_locked(&conn->c_mutex));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_CONNECTING ||
		  conn->c_state == C2_RPC_CONN_ACTIVE ||
		  conn->c_state == C2_RPC_CONN_TERMINATING);

	conn->c_state = C2_RPC_CONN_FAILED;
	conn->c_rc = error;
	/*
	 * Remove conn from conn->c_rpcmachine->cr_outgoing_conns or
	 * conn->c_rpcmachine->cr_incoming_conns list
	 */
	c2_mutex_lock(&conn->c_rpcmachine->cr_session_mutex);
	c2_list_del(&conn->c_link);
	c2_mutex_unlock(&conn->c_rpcmachine->cr_session_mutex);

	C2_ASSERT(c2_rpc_conn_invariant(conn));
}

int c2_rpc_conn_establish(struct c2_rpc_conn *conn)
{
	struct c2_fop                    *fop;
	struct c2_rpc_session            *session_0;
	struct c2_rpcmachine             *machine;
	int                               rc;

	C2_PRE(conn != NULL);

	fop = c2_fop_alloc(&c2_rpc_fop_conn_establish_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		/*
		 * No need to hold conn->c_mutex here.
		 */
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = rc;
		C2_ASSERT(c2_rpc_conn_invariant(conn));
		goto out;
	}

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_INITIALISED &&
	          c2_rpc_conn_is_snd(conn) && c2_rpc_conn_invariant(conn));

	/*
	 * Get a source endpoint and in turn a transfer machine
	 *  to associate with this c2_rpc_conn.
	 */
	machine = conn->c_rpcmachine;

	conn->c_state = C2_RPC_CONN_CONNECTING;

	c2_mutex_lock(&machine->cr_session_mutex);
	c2_list_add(&machine->cr_outgoing_conns, &conn->c_link);
	c2_mutex_unlock(&machine->cr_session_mutex);

	C2_ASSERT(c2_rpc_conn_invariant(conn));

	conn->c_rpcchan = c2_rpc_chan_get(machine);

	/*
	 * c2_rpc_fop_conn_establish FOP doesn't contain any data.
	 */

	session_0 = c2_rpc_conn_session0(conn);

	/* Formation client side keeps track of current rpcs in flight,
	   while server side of formation does not. So we need to differentiate
	   between client and server sides for formation. */
	machine->cr_formation.rf_sender_side = true;

	rc = c2_rpc__fop_post(fop, session_0, &c2_rpc_item_conn_establish_ops);
	if (rc != 0) {
		conn_failed(conn, rc);
		c2_fop_free(fop);
	}

	C2_POST(ergo(rc != 0, conn->c_state == C2_RPC_CONN_FAILED));
	C2_ASSERT(c2_rpc_conn_invariant(conn));
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
	C2_PRE(ergo(rc == 0, reply != NULL &&
			req->ri_session == reply->ri_session));

	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_CONNECTING &&
		  c2_rpc_conn_invariant(conn));

	if (rc != 0) {
		conn_failed(conn, rc);
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	fop_cer = c2_fop_data(fop);

	if (fop_cer->rcer_rc != 0) {
		/*
		 * Receiver has reported conn create failure
		 */
		conn_failed(conn, fop_cer->rcer_rc);
		/*
		 * end-user is expected to call c2_rpc_conn_fini() on
		 * this object, to free any memory allocated by conn for
		 * its internal structures (session0, slots etc.).
		 * Then the end-user can free the object.
		 */
		printf("ccrr: conn create failed %d\n",
			fop_cer->rcer_rc);
	} else {
		if (fop_cer->rcer_sender_id == SENDER_ID_INVALID) {
			/*
			 * Return code (fop_cer->rcer_rc) says that conn
			 * establish is successful. In that case, sender_id
			 * in the reply fop MUST not be SENDER_ID_INVALID.
			 * We do not assert, for inconsistent data received from
			 * network/disk.
			 * move conn to FAILED state and XXX generate ADDB
			 * record.
			 */
			conn_failed(conn, -EPROTO);
			goto out;
		}
		conn->c_sender_id = fop_cer->rcer_sender_id;
		conn->c_state = C2_RPC_CONN_ACTIVE;
		printf("ccrr: conn created %lu\n",
			(unsigned long)fop_cer->rcer_sender_id);
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
	struct c2_rpc_session *session;
	struct c2_rpc_slot    *slot;

	C2_PRE(conn != NULL);

	session = c2_rpc_conn_session0(conn);
	session->s_state = C2_RPC_SESSION_TERMINATED;
	c2_list_del(&session->s_link);

	/*
	 * Remove slot0 from c2_rpcmachine::cr_ready_slots list
	 */
	slot = session->s_slot_table[0];
	if (c2_list_link_is_in(&slot->sl_link))
		c2_list_del(&slot->sl_link);

	c2_rpc_session_fini(session);
	c2_free(session);
}

int c2_rpc_conn_terminate(struct c2_rpc_conn *conn)
{
	struct c2_fop                    *fop = NULL; /* NULL required */
	struct c2_rpc_fop_conn_terminate *fop_ct;
	struct c2_rpc_session            *session_0;
	int                               rc;

	C2_PRE(conn != NULL);

	fop = c2_fop_alloc(&c2_rpc_fop_conn_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	c2_mutex_lock(&conn->c_mutex);

	/*
	 * If rpc connection is already in TERMINATING state then
	 * c2_rpc_conn_terminate() is a no-op.
	 */
	if (conn->c_state == C2_RPC_CONN_TERMINATING) {
		rc = 0;
		c2_fop_free(fop);
		goto out_unlock;
	}

	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE &&
			conn->c_nr_sessions == 0 &&
			c2_rpc_conn_invariant(conn));

	conn->c_state = C2_RPC_CONN_TERMINATING;

	printf("sender_conn_terminate: %p(%lu)\n", conn,
			(unsigned long)conn->c_sender_id);
	fop_ct = c2_fop_data(fop);
	C2_ASSERT(fop_ct != NULL);

	fop_ct->ct_sender_id = conn->c_sender_id;

	session_0 = c2_rpc_conn_session0(conn);
	rc = c2_rpc__fop_post(fop, session_0, &c2_rpc_item_conn_terminate_ops);
	if (rc != 0) {
		conn_failed(conn, rc);
		c2_fop_free(fop);
	}
	C2_POST(ergo(rc != 0, conn->c_state == C2_RPC_CONN_FAILED));
	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_TERMINATING));
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_cond_broadcast(&conn->c_state_changed, &conn->c_mutex);

out_unlock:
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
	uint64_t                              sender_id;

	C2_PRE(req != NULL && req->ri_session != NULL &&
		req->ri_session->s_session_id == SESSION_ID_0);
	C2_PRE(ergo(rc == 0, reply != NULL &&
			req->ri_session == reply->ri_session));

	conn = req->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == C2_RPC_CONN_TERMINATING &&
		  c2_rpc_conn_invariant(conn));

	/* Remove conn from rpcmachine::cr_outgoing_conns list */
	c2_mutex_lock(&conn->c_rpcmachine->cr_session_mutex);
	c2_list_del(&conn->c_link);
	c2_mutex_unlock(&conn->c_rpcmachine->cr_session_mutex);

	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = rc;
		goto out;
	}

	fop = c2_rpc_item_to_fop(reply);
	fop_ctr = c2_fop_data(fop);

	/*
	 * c2_rpc_conn_terminate() sends sender_id in the conn_terminate
	 * request fop. Receiver simply copies it back in reply. Make sure it
	 * matches with connection being terminated.
	 * No assert on data received from network/disk.
	 * XXX generate ADDB record here.
	 */
	sender_id = fop_ctr->ctr_sender_id;
	if (conn->c_sender_id != sender_id) {
		printf("ctrr: conn->c_sender_id != sender_id\n");
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = -EPROTO;
		goto out;
	}

	/*
	 * Update conn state depending on rc reported in reply fop.
	 * session 0 will be cleaned up in c2_rpc_conn_fini().
	 */
	if (fop_ctr->ctr_rc == 0) {
		printf("ctrr: connection terminated %lu\n",
			(unsigned long)conn->c_sender_id);
		conn->c_state = C2_RPC_CONN_TERMINATED;
		conn->c_sender_id = SENDER_ID_INVALID;
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
	c2_cond_broadcast(&conn->c_state_changed, &conn->c_mutex);
	c2_mutex_unlock(&conn->c_mutex);
	/* Release the reference on c2_rpc_chan structure being used. */
	c2_rpc_chan_put(conn->c_rpcchan);
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

	sprintf(name, conn_cob_name_fmt, (unsigned long)sender_id);

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

	sprintf(name, conn_cob_name_fmt, (unsigned long)sender_id);
	*out = NULL;

	rc = c2_rpc_cob_lookup_helper(dom, NULL, root_session_cob_name,
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

	rc = c2_rpc_session_cob_create(conn_cob, SESSION_ID_0, &session0_cob,
					tx);
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

	C2_PRE(conn != NULL && c2_rpc_conn_invariant(conn) &&
			conn->c_state == C2_RPC_CONN_INITIALISED);

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

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_INITIALISED &&
			c2_rpc_conn_is_rcv(conn));

	machine = conn->c_rpcmachine;
	C2_ASSERT(machine != NULL && machine->cr_dom != NULL);

	rc = c2_db_tx_init(&tx, machine->cr_dom->cd_dbenv, 0);
	if (rc != 0)
		goto out;

	sender_id = sender_id_allocate();
	rc = conn_persistent_state_attach(conn, sender_id,
					  &tx);
	if (rc != 0) {
		/*
		 * Regardless of return value of c2_db_tx_abort(), the conn
		 * will be moved to FAILED state.
		 */
		c2_db_tx_abort(&tx);
		goto out;
	}

	rc = c2_db_tx_commit(&tx);
	if (rc != 0)
		goto out;

	conn->c_sender_id = sender_id;
	conn->c_rpcchan = c2_rpc_chan_get(conn->c_rpcmachine);
	conn->c_state = C2_RPC_CONN_ACTIVE;
	c2_mutex_lock(&machine->cr_session_mutex);
	c2_list_add(&machine->cr_incoming_conns, &conn->c_link);
	c2_mutex_unlock(&machine->cr_session_mutex);
	/* Fall through */
out:
	conn->c_state = (rc == 0) ? C2_RPC_CONN_ACTIVE : C2_RPC_CONN_FAILED;
	conn->c_rc = rc;
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);
	return rc;
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
	int             rc;

	C2_PRE(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	C2_ASSERT(c2_rpc_conn_is_rcv(conn));

	if (conn->c_nr_sessions > 0) {
		c2_mutex_unlock(&conn->c_mutex);
		return -EBUSY;
	}

	conn->c_state = C2_RPC_CONN_TERMINATING;

	rc = c2_db_tx_init(&tx, conn->c_cob->co_dom->cd_dbenv, 0);
	if (rc != 0)
		goto out;

	rc = conn_persistent_state_destroy(conn, &tx);
	if (rc == 0)
		rc = c2_db_tx_commit(&tx);
	else
		c2_db_tx_abort(&tx);
out:
	if (rc != 0) {
		struct c2_rpc_session *session0;
		struct c2_rpc_slot    *slot;

		/*
		 * Take out slot0 of session0 out of ready slots list.
		 */
		session0 = c2_rpc_conn_session0(conn);
		slot = session0->s_slot_table[0];
		C2_ASSERT(slot != NULL);
		c2_mutex_lock(&conn->c_rpcmachine->cr_ready_slots_mutex);
		if (c2_list_link_is_in(&slot->sl_link))
			c2_list_del(&slot->sl_link);
		c2_mutex_unlock(&conn->c_rpcmachine->cr_ready_slots_mutex);

		conn_failed(conn, rc);
	}
	/*
	 * Note: conn is not moved to TERMINATED state even if operation is
	 * successful. This is required to be able to send successful conn
	 * terminate reply.
	 */
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	/* In-core state will be cleaned up by
	   c2_rpc_conn_terminate_reply_sent() */
	c2_mutex_unlock(&conn->c_mutex);
	return rc;
}

void c2_rpc_conn_terminate_reply_sent(struct c2_rpc_conn *conn)
{
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(conn->c_state == C2_RPC_CONN_TERMINATING);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	c2_mutex_lock(&conn->c_rpcmachine->cr_session_mutex);
	c2_list_del(&conn->c_link);
	c2_mutex_unlock(&conn->c_rpcmachine->cr_session_mutex);

	conn->c_state = C2_RPC_CONN_TERMINATED;
	conn->c_sender_id = SENDER_ID_INVALID;
	conn->c_rc = 0;
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_rpc_chan_put(conn->c_rpcchan);

	c2_mutex_unlock(&conn->c_mutex);

	c2_rpc_conn_fini(conn);
	c2_free(conn);
}

bool c2_rpc_item_is_conn_establish(const struct c2_rpc_item *item)
{
	return item->ri_type == &c2_rpc_item_conn_establish;
}

/**
   Just for debugging purpose. Useful in gdb.

   dir = 1, to print incoming conn list
   dir = 0, to print outgoing conn list
 */
int c2_rpcmachine_conn_list_print(struct c2_rpcmachine *machine, int dir)
{
	struct c2_list     *list;
	struct c2_rpc_conn *conn;

	list = dir ? &machine->cr_incoming_conns : &machine->cr_outgoing_conns;

	c2_list_for_each_entry(list, conn, struct c2_rpc_conn, c_link) {
		printf("CONN: %p id %llu state %x\n", conn,
				(unsigned long long)conn->c_sender_id,
				conn->c_state);
	}
	return 0;
}

int c2_rpc_conn_session_list_print(const struct c2_rpc_conn *conn)
{
	struct c2_rpc_session *session;

	c2_list_for_each_entry(&conn->c_sessions, session,
				struct c2_rpc_session, s_link) {
		printf("session %p id %llu state %x\n", session,
			(unsigned long long)session->s_session_id,
			session->s_state);
	}
	return 0;
}
