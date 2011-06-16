/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include "lib/memory.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "rpc/session_u.h"
#include "rpc/session_int.h"
#include "db/db.h"
#include "dtm/verno.h"
#include "rpc/session_fops.h"

/**
   @addtogroup rpc_session

   @{
 */

static int session_zero_attach(struct c2_rpc_conn *conn);

static void session_zero_detach(struct c2_rpc_conn *conn);

static void conn_search(const struct c2_rpcmachine	*machine,
                        uint64_t			sender_id,
                        struct c2_rpc_conn		**out);

static void session_search(const struct c2_rpc_conn	*conn,
                           uint64_t			session_id,
                           struct c2_rpc_session	**out);

/** Stub routine */
void c2_rpc_form_slot_idle(struct c2_rpc_slot *slot);
/** Stub routine */
void c2_rpc_form_item_ready(struct c2_rpc_item *item);

void c2_rpc_sender_slot_idle(struct c2_rpc_slot *slot);
void c2_rpc_sender_consume_item(struct c2_rpc_item *item);
void c2_rpc_sender_consume_reply(struct c2_rpc_item	*req,
				 struct c2_rpc_item	*reply);

void c2_rpc_rcv_slot_idle(struct c2_rpc_slot *slot);
void c2_rpc_rcv_consume_item(struct c2_rpc_item *item);
void c2_rpc_rcv_consume_reply(struct c2_rpc_item  *req,
			      struct c2_rpc_item  *reply);

const struct c2_rpc_slot_ops c2_rpc_sender_slot_ops = {
	.so_slot_idle = c2_rpc_sender_slot_idle,
	.so_consume_item = c2_rpc_sender_consume_item,
	.so_consume_reply = c2_rpc_sender_consume_reply
};
const struct c2_rpc_slot_ops c2_rpc_rcv_slot_ops = {
	.so_slot_idle = c2_rpc_rcv_slot_idle,
	.so_consume_item = c2_rpc_rcv_consume_item,
	.so_consume_reply = c2_rpc_rcv_consume_reply
};


int c2_rpc_session_module_init(void)
{
	int		rc;

	rc = c2_rpc_session_fop_init();
	return rc;
}
C2_EXPORTED(c2_rpc_session_module_init);

void c2_rpc_session_module_fini(void)
{
	c2_rpc_session_fop_fini();
}
C2_EXPORTED(c2_rpc_session_module_fini);

/**
   Search for a c2_rpc_conn object having sender_id equal to
   @sender_id in machine->cr_rpc_conn_list.

   If NOT found then *out is set to NULL
   If found then *out contains pointer to c2_rpc_conn object

   @pre c2_mutex_is_locked(&machine->cr_session_mutex)
   @post ergo(*out != NULL, (*out)->c_sender_id == sender_id)
*/
static void conn_search(const struct c2_rpcmachine	*machine,
                        uint64_t			sender_id,
                        struct c2_rpc_conn		**out)
{
	struct c2_rpc_conn	*conn;

	C2_PRE(c2_mutex_is_locked(&machine->cr_session_mutex));
	*out = NULL;

	if (sender_id == SENDER_ID_INVALID)
		return;

	c2_rpc_for_each_outgoing_conn(machine, conn) {
		if (conn->c_sender_id == sender_id) {
			*out = conn;
			break;
		}
	}
	C2_POST(ergo(*out != NULL, (*out)->c_sender_id == sender_id));
}

/**
   Search for session object with given @session_id in conn->c_sessions list
   If not found *out is set to NULL
   If found *out contains pointer to session object

   Caller is expected to decide whether conn will be locked or not

   @post ergo(*out != NULL, (*out)->s_session_id == session_id)
 */
static void session_search(const struct c2_rpc_conn	*conn,
                           uint64_t			session_id,
                           struct c2_rpc_session	**out)
{
	struct c2_rpc_session		*session;

	*out = NULL;

	if (session_id > SESSION_ID_MAX)
		return;

	c2_rpc_for_each_session(conn, session) {
		if (session->s_session_id == session_id) {
			*out = session;
			break;
		}
	}

	C2_POST(ergo(*out != NULL, (*out)->s_session_id == session_id));
}

int __conn_init(struct c2_rpc_conn	*conn,
		struct c2_rpcmachine	*machine)
{
	int	rc = 0;

	C2_PRE(conn != NULL &&
	  ((conn->c_flags & RCF_SENDER_END) != (conn->c_flags & RCF_RECV_END)));

	conn->c_sender_id = SENDER_ID_INVALID;
	conn->c_cob = NULL;
	c2_list_init(&conn->c_sessions);
	conn->c_nr_sessions = 0;
	c2_chan_init(&conn->c_chan);
	c2_mutex_init(&conn->c_mutex);
	c2_list_link_init(&conn->c_link);
	conn->c_rpcmachine = machine;
	conn->c_rc = 0;

	rc = session_zero_attach(conn);
	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_UNINITIALISED;
		return rc;
	}
	return rc;
}
int c2_rpc_conn_init(struct c2_rpc_conn		*conn,
		     struct c2_rpcmachine	*machine)
{
	int	rc = 0;

	if (conn == NULL || machine == NULL ||
		conn->c_state != C2_RPC_CONN_UNINITIALISED)
		return -EINVAL;

	conn->c_flags = RCF_SENDER_END;

	rc = __conn_init(conn, machine);

	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
				conn->c_rpcmachine == machine &&
				conn->c_sender_id == SENDER_ID_INVALID &&
				(conn->c_flags & RCF_SENDER_END) != 0));
	C2_POST(ergo(rc != 0, conn->c_state == C2_RPC_CONN_UNINITIALISED));
	return rc;
}
int c2_rpc_conn_create(struct c2_rpc_conn	*conn,
		       struct c2_net_end_point	*ep)
{
	struct c2_fop			*fop;
	struct c2_rpc_fop_conn_create	*fop_cc;
	struct c2_rpc_item		*item;
	struct c2_rpc_session		*session_0;
	struct c2_rpcmachine		*machine;
	int				rc;

	if (conn == NULL || ep == NULL)
		return -EINVAL;

	if (conn->c_state != C2_RPC_CONN_INITIALISED &&
	      (conn->c_flags & RCF_SENDER_END) == 0)
		return -EINVAL;

	conn->c_end_point = ep;

	/*
	 * fill a conn create fop and send
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_conn_create_fopt, NULL);
	if (fop == NULL)
		return rc;

	fop_cc = c2_fop_data(fop);
	C2_ASSERT(fop_cc != NULL);

	/*
	 * Receiver will copy this cookie in conn_create reply
	 */
	fop_cc->rcc_cookie = (uint64_t)conn;

	/*
	 * When conn create reply will be received then we will need this
	 * fop to compare the cookies to find the right conn object.
	 * The fop is freed either if conn_create() fails or after processing
	 * conn create reply.
	 */
	conn->c_private = fop;

	conn->c_state = C2_RPC_CONN_CREATING;

	machine = conn->c_rpcmachine;
	c2_mutex_lock(&machine->cr_session_mutex);
	c2_list_add(&machine->cr_outgoing_conns, &conn->c_link);
	c2_mutex_unlock(&machine->cr_session_mutex);

	session_search(conn, SESSION_0, &session_0);
	C2_ASSERT(session_0 != NULL);

	item = c2_fop_to_rpc_item(fop);
	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_flags |= RPC_ITEM_MUTABO;

	rc = c2_rpc_post(item);
	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_INITIALISED;
		conn->c_private = NULL;
		c2_fop_free(fop);

		c2_mutex_lock(&machine->cr_session_mutex);
		c2_list_del(&conn->c_link);
		c2_mutex_unlock(&machine->cr_session_mutex);
	}
	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_CREATING &&
			c2_rpc_conn_invariant(conn)));
	C2_POST(ergo(rc != 0, conn->c_state == C2_RPC_CONN_INITIALISED));
	return rc;
}
C2_EXPORTED(c2_rpc_conn_create);

void c2_rpc_conn_create_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_fop_conn_create_rep	*fop_ccr;
	struct c2_rpc_fop_conn_create		*fop_conn_create;
	struct c2_fop				*saved_fop = NULL;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_item			*item;
	struct c2_rpcmachine			*machine;
	bool					found = false;

	C2_PRE(fop != NULL);

	fop_ccr = c2_fop_data(fop);
	C2_ASSERT(fop_ccr != NULL);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	/*
	 * Search for c2_rpc_conn object whose create reply is
	 * being processed
	 */
	machine = item->ri_mach;
	c2_mutex_lock(&machine->cr_session_mutex);

	c2_rpc_for_each_outgoing_conn(machine, conn) {
		c2_mutex_lock(&conn->c_mutex);
		if (conn->c_state == C2_RPC_CONN_CREATING) {
			/*
			 * during conn_create() the fop is stored in
			 * conn->c_private
			 */
			saved_fop = conn->c_private;
			C2_ASSERT(saved_fop != NULL);

			fop_conn_create = c2_fop_data(saved_fop);
			C2_ASSERT(fop_conn_create != NULL);

			if (fop_conn_create->rcc_cookie ==
				fop_ccr->rccr_cookie) {
				/*
				 * Address of conn object is used as cookie.
				 * Just double check whether it is the right
				 * object?
				 */
				C2_ASSERT(fop_ccr->rccr_cookie ==
						(uint64_t)conn);
				found = true;
				break;
			}
		}
		c2_mutex_unlock(&conn->c_mutex);
	}

	if (!found) {
		/*
		 * This can be a duplicate reply. That's why
		 * cannot find an CREATING conn
		 */
		c2_mutex_unlock(&machine->cr_session_mutex);
		return;
	}

	C2_ASSERT(conn != NULL && c2_mutex_is_locked(&conn->c_mutex));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_CREATING &&
			c2_rpc_conn_invariant(conn));

	/*
	 * machine->cr_session_mutex is not dropped yet, because we might
	 * require it to remove the conn object from conn_list if conn_create
	 * is failed
	 */

	if (fop_ccr->rccr_rc != 0) {
		C2_ASSERT(fop_ccr->rccr_snd_id == SENDER_ID_INVALID);
		/*
		 * Receiver has reported conn create failure
		 */
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_sender_id = SENDER_ID_INVALID;
		c2_list_del(&conn->c_link);
		conn->c_rc = fop_ccr->rccr_rc;
		printf("ccrr: conn create failed %d\n",
			fop_ccr->rccr_rc);
	} else {
		C2_ASSERT(fop_ccr->rccr_snd_id != SENDER_ID_INVALID &&
				conn->c_sender_id == SENDER_ID_INVALID);
		conn->c_sender_id = fop_ccr->rccr_snd_id;
		conn->c_state = C2_RPC_CONN_ACTIVE;
		conn->c_rc = 0;
		printf("ccrr: conn created %lu\n", fop_ccr->rccr_snd_id);
	}

	C2_ASSERT(saved_fop != NULL);
	c2_fop_free(saved_fop);	/* conn_create req fop */
	conn->c_private = NULL;

	c2_fop_free(fop);	/* reply fop */

	C2_ASSERT(conn->c_state == C2_RPC_CONN_FAILED ||
			conn->c_state == C2_RPC_CONN_ACTIVE);
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);
	c2_mutex_unlock(&machine->cr_session_mutex);
	c2_chan_broadcast(&conn->c_chan);
	return;
}
static int session_zero_attach(struct c2_rpc_conn *conn)
{
	struct c2_rpc_session   *session;
	int			rc;

	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_CREATING);

	C2_ALLOC_PTR(session);
	if (session == NULL)
		return -ENOMEM;

	C2_SET0(session);

	rc = c2_rpc_session_init(session, conn, 1);   /* 1 => number of slots */
	if (rc != 0) {
		c2_free(session);
		return rc;
	}

	session->s_session_id = SESSION_0;
	session->s_state = C2_RPC_SESSION_ALIVE;

	c2_list_add(&conn->c_sessions, &session->s_link);
	return 0;
}
void session_zero_detach(struct c2_rpc_conn	*conn)
{
	struct c2_rpc_session	*session = NULL;

	C2_ASSERT(conn != NULL);

	session_search(conn, SESSION_0, &session);
	C2_ASSERT(session != NULL);

	if (session != NULL) {
		session->s_state = C2_RPC_SESSION_TERMINATED;
		c2_list_del(&session->s_link);
		session->s_conn = NULL;
		c2_rpc_session_fini(session);
		c2_free(session);
	}
}
int c2_rpc_conn_terminate(struct c2_rpc_conn *conn)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate	*fop_ct;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session_0 = NULL;
	int					rc;

	C2_PRE(conn != NULL);
	if (conn == NULL) {
		return -EINVAL;
	}

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(conn->c_sender_id != SENDER_ID_INVALID &&
			c2_rpc_conn_invariant(conn));

	if (conn->c_state != C2_RPC_CONN_ACTIVE) {
		rc = -EINVAL;
		goto out;
	}

	if (conn->c_nr_sessions > 0) {
		rc = -EBUSY;
		goto out;
	}

	fop = c2_fop_alloc(&c2_rpc_fop_conn_terminate_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * will free the fop when reply is received. In case we might
	 * need to resend it so store address of fop in conn->c_private
	 */
	C2_ASSERT(conn->c_private == NULL);
	conn->c_private = fop;

	fop_ct = c2_fop_data(fop);
	C2_ASSERT(fop_ct != NULL);

	fop_ct->ct_sender_id = conn->c_sender_id;

	session_search(conn, SESSION_0, &session_0);
	C2_ASSERT(session_0 != NULL);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);

	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_flags |= RPC_ITEM_MUTABO;

	conn->c_state = C2_RPC_CONN_TERMINATING;

	rc = c2_rpc_post(item);
	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_ACTIVE;
		conn->c_private = NULL;
		c2_fop_free(fop);
	}

out:
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);
	c2_chan_broadcast(&conn->c_chan);
	return rc;
}
C2_EXPORTED(c2_rpc_conn_terminate);

void c2_rpc_conn_terminate_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_fop_conn_terminate_rep	*fop_ctr;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_item			*item;
	struct c2_rpcmachine			*machine;
	uint64_t				sender_id;

	C2_PRE(fop != NULL);

	fop_ctr = c2_fop_data(fop);
	C2_ASSERT(fop_ctr != NULL);

	sender_id = fop_ctr->ctr_sender_id;
	C2_ASSERT(sender_id != SENDER_ID_INVALID);

	item = c2_fop_to_rpc_item(fop);

	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	machine = item->ri_mach;
	c2_mutex_lock(&machine->cr_session_mutex);

	conn_search(machine, sender_id, &conn);
	if (conn == NULL) {
		/*
		 * This can be a duplicate reply. That's why the
		 * c2_rpc_conn object might have already been
		 * finalised
		 */
		c2_fop_free(fop);
		c2_mutex_unlock(&machine->cr_session_mutex);
		return;
	}

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(conn->c_state == C2_RPC_CONN_TERMINATING &&
			c2_rpc_conn_invariant(conn));

	if (fop_ctr->ctr_rc == 0) {
		printf("ctrr: connection terminated %lu\n", conn->c_sender_id);
		conn->c_state = C2_RPC_CONN_TERMINATED;
		conn->c_sender_id = SENDER_ID_INVALID;
		c2_list_del(&conn->c_link);
		conn->c_rc = 0;
		conn->c_rpcmachine = NULL;
		conn->c_end_point = NULL;
		/*
		 * session 0 will be cleaned up in c2_rpc_conn_fini()
		 */
	} else {
		/*
		 * Connection termination failed
		 */
		printf("ctrr: conn termination failed %lu\n", conn->c_sender_id);
		conn->c_state = C2_RPC_CONN_FAILED;
		conn->c_rc = fop_ctr->ctr_rc;
	}

	C2_ASSERT(conn->c_private != NULL);
	c2_fop_free(conn->c_private); /* request fop */
	conn->c_private = NULL;

	C2_POST(conn->c_state == C2_RPC_CONN_TERMINATED ||
			conn->c_state == C2_RPC_CONN_FAILED);
	C2_POST(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&conn->c_mutex);
	c2_mutex_unlock(&machine->cr_session_mutex);

	c2_chan_broadcast(&conn->c_chan);

	c2_fop_free(fop);	/* reply fop */
	return;
}

void c2_rpc_conn_fini(struct c2_rpc_conn *conn)
{
	C2_PRE(conn->c_state == C2_RPC_CONN_TERMINATED ||
		conn->c_state == C2_RPC_CONN_FAILED ||
		conn->c_state == C2_RPC_CONN_INITIALISED);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	session_zero_detach(conn);
	c2_list_link_fini(&conn->c_link);
	c2_list_fini(&conn->c_sessions);
	c2_chan_fini(&conn->c_chan);
	c2_mutex_fini(&conn->c_mutex);

	C2_SET0(conn);
}
C2_EXPORTED(c2_rpc_conn_fini);

bool c2_rpc_conn_timedwait(struct c2_rpc_conn	*conn,
			   uint64_t		state_flags,
                           const c2_time_t	abs_timeout)
{
	struct c2_clink         clink;
	bool                    got_event = true;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&conn->c_chan, &clink);

	while ((conn->c_state & state_flags) == 0 && got_event) {
		got_event = c2_chan_timedwait(&clink, abs_timeout);
		/*
		 * If got_event == false then TIME_OUT has occured.
		 * break the loop
		 */
		C2_ASSERT(c2_rpc_conn_invariant(conn));
	}

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return (conn->c_state & state_flags) != 0;
}
C2_EXPORTED(c2_rpc_conn_fini);

bool c2_rpc_conn_invariant(const struct c2_rpc_conn *conn)
{
	int			count = 0;
	struct c2_rpc_session	*session;
	bool			result;

	if (conn == NULL)
		return false;

	switch (conn->c_state) {
		case C2_RPC_CONN_TERMINATED:
			return !c2_list_link_is_in(&conn->c_link) &&
				conn->c_rpcmachine == NULL &&
				conn->c_rc == 0;

		case C2_RPC_CONN_INITIALISED:
			return conn->c_sender_id == SENDER_ID_INVALID &&
				conn->c_rpcmachine != NULL;

		case C2_RPC_CONN_CREATING:
			return conn->c_sender_id == SENDER_ID_INVALID &&
			conn->c_nr_sessions == 0 &&
			conn->c_end_point != NULL &&
			conn->c_rpcmachine != NULL &&
			conn->c_private != NULL &&
			c2_list_contains(&conn->c_rpcmachine->cr_outgoing_conns,
				&conn->c_link);
			
		case C2_RPC_CONN_ACTIVE:
			result = conn->c_sender_id != SENDER_ID_INVALID &&
		        	    conn->c_end_point != NULL &&
		        	    conn->c_rpcmachine != NULL &&
				    c2_list_invariant(&conn->c_sessions) &&
			c2_list_contains(&conn->c_rpcmachine->cr_outgoing_conns,
				&conn->c_link) &&
				    conn->c_private == NULL;
			
			if (!result)
				return result;

			c2_rpc_for_each_session(conn, session) {
				count++;
			}
			/*
			 * session 0 is not taken into account in nr_sessions
			 */
			count--;
			return count == conn->c_nr_sessions;

		case C2_RPC_CONN_TERMINATING:
			return conn->c_nr_sessions == 0 &&
				conn->c_sender_id != SENDER_ID_INVALID &&
				conn->c_rpcmachine != NULL &&
			c2_list_contains(&conn->c_rpcmachine->cr_outgoing_conns,
				&conn->c_link) &&
				conn->c_private != NULL;

		case C2_RPC_CONN_UNINITIALISED:
			return true;
		case C2_RPC_CONN_FAILED:
			return conn->c_rc != 0 &&
				!c2_list_link_is_in(&conn->c_link) &&
				conn->c_rpcmachine == NULL &&
				conn->c_private == NULL;
		default:
			return false;
	}
	C2_ASSERT(0);
}

int c2_rpc_session_init(struct c2_rpc_session	*session,
			struct c2_rpc_conn	*conn,
			uint32_t		nr_slots)
{
	const struct c2_rpc_slot_ops	*slot_ops;
	struct c2_rpc_slot		*slot;
	int				i;
	int				rc = 0;

	C2_PRE(session != NULL && nr_slots >= 1);
	if (session == NULL || conn == NULL || nr_slots == 0)
		return -EINVAL;

	if (session->s_state != C2_RPC_SESSION_UNINITIALISED)
		return -EINVAL;

	c2_list_link_init(&session->s_link);
	session->s_session_id = SESSION_ID_INVALID;
	session->s_conn = conn;
	c2_chan_init(&session->s_chan);
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

	if ((conn->c_flags & RCF_SENDER_END) != 0) {
		slot_ops = &c2_rpc_sender_slot_ops;
	} else {
		C2_ASSERT((conn->c_flags & RCF_RECV_END) != 0);
		slot_ops = &c2_rpc_rcv_slot_ops;
	}
	for (i = 0; i < nr_slots; i++) {
		C2_ALLOC_PTR(session->s_slot_table[i]);
		if (session->s_slot_table[i] == NULL) {
			rc = -ENOMEM;
			goto out_err;
		}
		rc = c2_rpc_slot_init(session->s_slot_table[0],
					slot_ops);
		if (rc != 0)
			goto out_err;

		session->s_slot_table[0]->sl_session = session;
		session->s_slot_table[0]->sl_slot_id = i;
	}
	session->s_state = C2_RPC_SESSION_INITIALISED;
	return 0;

out_err:
	C2_ASSERT(rc != 0);
	if (session->s_slot_table != NULL) {
		for (i = 0; i < nr_slots; i++) {
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
	session->s_state = C2_RPC_SESSION_UNINITIALISED;
	return rc;
}
int c2_rpc_session_create(struct c2_rpc_session	*session)
{
	struct c2_rpc_conn			*conn;
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_create	*fop_sc;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session_0 = NULL;
	int					rc = 0;

	C2_PRE(session != NULL &&
		session->s_state == C2_RPC_SESSION_INITIALISED);

	conn = session->s_conn;
	C2_ASSERT(conn != NULL);
	c2_mutex_lock(&conn->c_mutex);
	if (conn->c_state != C2_RPC_CONN_ACTIVE ||
		session->s_state != C2_RPC_SESSION_INITIALISED) {
		rc = -EINVAL;
		goto out;
	}
	C2_ASSERT(c2_rpc_conn_invariant(conn));
		
	fop = c2_fop_alloc(&c2_rpc_fop_conn_create_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	fop_sc = c2_fop_data(fop);
	C2_ASSERT(fop_sc != NULL);

	fop_sc->rsc_snd_id = conn->c_sender_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);

	session_search(conn, SESSION_0, &session_0);
	C2_ASSERT(session_0 != NULL);

	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_flags |= RPC_ITEM_MUTABO;

	rc = c2_rpc_post(item);
	if (rc == 0) {
		session->s_state = C2_RPC_SESSION_CREATING;
		c2_list_add(&conn->c_sessions, &session->s_link);
		conn->c_nr_sessions++;
	}

out:
	C2_ASSERT(c2_mutex_is_locked(&conn->c_mutex));
	C2_POST(ergo(rc == 0, session->s_state == C2_RPC_SESSION_CREATING &&
			c2_rpc_session_invariant(session)));

	c2_mutex_unlock(&conn->c_mutex);
	c2_chan_broadcast(&session->s_chan);

	return rc;
}
C2_EXPORTED(c2_rpc_session_create);

void c2_rpc_session_create_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_fop_session_create_rep	*fop_scr;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session = NULL;
	struct c2_rpc_session			*s = NULL;
	struct c2_rpc_item			*item;
	struct c2_rpc_slot			*slot;
	uint64_t				sender_id;
	uint64_t				session_id;
	int					i;

	C2_PRE(fop != NULL);

	fop_scr = c2_fop_data(fop);
	C2_ASSERT(fop_scr != NULL);

	sender_id = fop_scr->rscr_sender_id;
	session_id = fop_scr->rscr_session_id;
	C2_ASSERT(sender_id != SENDER_ID_INVALID);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	c2_mutex_lock(&item->ri_mach->cr_session_mutex);

	conn_search(item->ri_mach, sender_id, &conn);

	/*
	 * If session create is in progress then conn MUST exist
	 */
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	c2_mutex_unlock(&item->ri_mach->cr_session_mutex);

	/*
	 * For a c2_rpc_conn
	 * There can be only session create in progress at any given point
	 */
	c2_rpc_for_each_session(conn, s) {
		if (s->s_state == C2_RPC_SESSION_CREATING) {
			session = s;
			break;
		}
	}

	/*
	 * Duplicate reply message will be filtered by
	 * c2_rpc_session_reply_recieved().
	 * If current function is called then there must be ONE session
	 * with state == CREATING. If not then it is a bug.
	 */
	C2_ASSERT(session != NULL &&
			session->s_state == C2_RPC_SESSION_CREATING);
	C2_ASSERT(c2_rpc_session_invariant(session));

	c2_mutex_lock(&session->s_mutex);

	if (fop_scr->rscr_rc != 0) {
		session->s_state = C2_RPC_SESSION_FAILED;
		c2_list_del(&session->s_link);
		conn->c_nr_sessions--;
		session->s_rc = fop_scr->rscr_rc;
		printf("scrr: Session create failed\n");
	} else {
		C2_ASSERT(session_id >= SESSION_ID_MIN &&
				session_id <= SESSION_ID_MAX);
		session->s_session_id = session_id;
		session->s_state = C2_RPC_SESSION_ALIVE;
		session->s_rc = 0;
		for (i = 0; i < session->s_nr_slots; i++) {
			slot = session->s_slot_table[i];
			C2_ASSERT(slot != NULL && c2_rpc_slot_invariant(slot));
			slot->sl_ops->so_slot_idle(slot);
		}
		printf("scrr: session created %lu\n", session_id);
	}

	C2_ASSERT(session->s_state == C2_RPC_SESSION_ALIVE ||
			session->s_state == C2_RPC_SESSION_FAILED);
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&session->s_mutex);
	c2_mutex_unlock(&conn->c_mutex);
	c2_chan_broadcast(&session->s_chan);

	return;
}

int c2_rpc_session_terminate(struct c2_rpc_session *session)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_terminate	*fop_st;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session_0 = NULL;
	int					rc = 0;

	C2_PRE(session != NULL && session->s_conn != NULL);

	/*
	 * Make sure session does not have any "reserved" session id
	 */
	C2_ASSERT(session->s_session_id >= SESSION_ID_MIN &&
			session->s_session_id <= SESSION_ID_MAX);

	c2_mutex_lock(&session->s_mutex);

	C2_ASSERT(c2_rpc_session_invariant(session));

	/* XXX allow session terminate only if it is IDLE */
	if (session->s_state != C2_RPC_SESSION_ALIVE) {
		c2_mutex_unlock(&session->s_mutex);
		rc = -EINVAL;
		goto out;
	}

	fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_fopt, NULL);
	if (fop == NULL) {
		c2_mutex_unlock(&session->s_mutex);
		rc = -ENOMEM;
		goto out;
	}

	fop_st = c2_fop_data(fop);
	C2_ASSERT(fop_st != NULL);

	fop_st->rst_sender_id = session->s_conn->c_sender_id;
	fop_st->rst_session_id = session->s_session_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);

	/*
	 * We need ptr to session 0 to submit the item.
	 * To search session 0 need a lock on conn. And locking
	 * order is conn => session. Hence release lock on session.
	 */
	c2_mutex_unlock(&session->s_mutex);

	c2_mutex_lock(&session->s_conn->c_mutex);
	session_search(session->s_conn, SESSION_0, &session_0);
	C2_ASSERT(session_0 != NULL &&
			session_0->s_state == C2_RPC_SESSION_ALIVE);
	c2_mutex_unlock(&session->s_conn->c_mutex);

	item->ri_session = session_0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;
	item->ri_flags |= RPC_ITEM_MUTABO;

	rc = c2_rpc_post(item);
	if (rc == 0)
		session->s_state = C2_RPC_SESSION_TERMINATING;

	c2_chan_broadcast(&session->s_chan);
out:
	C2_POST(ergo(rc == 0, session->s_state == C2_RPC_SESSION_TERMINATING));
	C2_POST(c2_rpc_session_invariant(session));
	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate);

void c2_rpc_session_terminate_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_fop_session_terminate_rep	*fop_str;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session;
	struct c2_rpc_item			*item;
	struct c2_rpcmachine			*machine;
	uint64_t				sender_id;
	uint64_t				session_id;

	C2_PRE(fop != NULL);

	fop_str = c2_fop_data(fop);
	C2_ASSERT(fop_str != NULL);

	sender_id = fop_str->rstr_sender_id;
	session_id = fop_str->rstr_session_id;

	C2_ASSERT(sender_id != SENDER_ID_INVALID &&
			session_id >= SESSION_ID_MIN &&
			session_id <= SESSION_ID_MAX);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	machine = item->ri_mach;
	c2_mutex_lock(&machine->cr_session_mutex);

	conn_search(machine, sender_id, &conn);

	/*
	 * If session terminate is in progress then there must be
	 * ACTIVE conn with @sender_id
	 */
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE);

	c2_mutex_lock(&conn->c_mutex);
	c2_mutex_unlock(&machine->cr_session_mutex);
	
	C2_ASSERT(c2_rpc_conn_invariant(conn));

	session_search(conn, session_id, &session);
	C2_ASSERT(session != NULL &&
			session->s_state == C2_RPC_SESSION_TERMINATING);
	C2_ASSERT(conn->c_nr_sessions > 0);

	c2_mutex_lock(&session->s_mutex);
	C2_ASSERT(c2_rpc_session_invariant(session));

	if (fop_str->rstr_rc == 0) {
		session->s_state = C2_RPC_SESSION_TERMINATED;
		session->s_rc = 0;
		printf("strr: session terminated %lu\n", session_id);
	} else {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = fop_str->rstr_rc;
		printf("strr: session terminate failed %lu\n", session_id);
	}
	c2_list_del(&session->s_link);
	session->s_conn = NULL;
	conn->c_nr_sessions--;

	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	c2_mutex_unlock(&session->s_mutex);
	c2_mutex_unlock(&conn->c_mutex);
	c2_chan_broadcast(&session->s_chan);
	return;
}

bool c2_rpc_session_timedwait(struct c2_rpc_session	*session,
			      uint64_t			state_flags,
			      const c2_time_t		abs_timeout)
{
	struct c2_clink		clink;
	bool 			got_event = true;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&session->s_chan, &clink);

	while ((session->s_state & state_flags) == 0 && got_event) {
		got_event = c2_chan_timedwait(&clink, abs_timeout);
		/*
		 * If got_event == false then TIME_OUT has occured.
		 * break the loop
		 */
		C2_ASSERT(c2_rpc_session_invariant(session));
	}

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return (session->s_state & state_flags) != 0;
}
C2_EXPORTED(c2_rpc_session_timedwait);

void c2_rpc_session_fini(struct c2_rpc_session *session)
{
	struct c2_rpc_slot	*slot;
	int			i;

	C2_PRE(session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_INITIALISED ||
			session->s_state == C2_RPC_SESSION_FAILED);
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_list_link_fini(&session->s_link);
	session->s_session_id = SESSION_ID_INVALID;
	session->s_conn = NULL;
	c2_chan_fini(&session->s_chan);
	c2_mutex_fini(&session->s_mutex);
	c2_list_fini(&session->s_unbound_items);

	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		c2_rpc_slot_fini(slot);
		c2_free(slot);
	}
	c2_free(session->s_slot_table);
	session->s_slot_table = NULL;
	session->s_nr_slots = session->s_slot_table_capacity = 0;

	session->s_state = C2_RPC_SESSION_UNINITIALISED;
}
C2_EXPORTED(c2_rpc_session_fini);

bool c2_rpc_session_invariant(const struct c2_rpc_session *session)
{
	int		i;
	bool		result;

	if (session == NULL)
		return false;

	switch (session->s_state) {
		case C2_RPC_SESSION_INITIALISED:
			return session->s_session_id == SESSION_ID_INVALID &&
				session->s_conn != NULL;
		case C2_RPC_SESSION_CREATING:
			return session->s_session_id == SESSION_ID_INVALID &&
				session->s_conn != NULL &&
				c2_list_contains(&session->s_conn->c_sessions,
					&session->s_link);
		case C2_RPC_SESSION_TERMINATED:
			return  !c2_list_link_is_in(&session->s_link) &&
				session->s_conn == NULL;

		case C2_RPC_SESSION_ALIVE:
		case C2_RPC_SESSION_TERMINATING:
			result = session->s_session_id <= SESSION_ID_MAX &&
			       session->s_conn != NULL &&
			       session->s_conn->c_state == C2_RPC_CONN_ACTIVE &&
			       session->s_conn->c_nr_sessions > 0 &&
			       session->s_nr_slots >= 0 &&
			       c2_list_contains(&session->s_conn->c_sessions,
						&session->s_link);
			if (!result)
				return result;
			for (i = 0; i < session->s_nr_slots; i++) {
				if (session->s_slot_table[i] == NULL)
					return false;
				if (!c2_rpc_slot_invariant(
					session->s_slot_table[i]))
					return false;
			}
		case C2_RPC_SESSION_UNINITIALISED:
			return true;
		case C2_RPC_SESSION_FAILED:
			return session->s_rc != 0 &&
				!c2_list_link_is_in(&session->s_link) &&
				session->s_conn == NULL;
		default:
			return false;
	}
	C2_ASSERT(0);
}

/**
   Change size of slot table in 'session' to 'nr_slots'.

   If nr_slots > current capacity of slot table then
        it reallocates the slot table.
   else
        it just marks slots above nr_slots as 'dont use'
 */
int c2_rpc_session_slot_table_resize(struct c2_rpc_session	*session,
				     uint32_t			nr_slots)
{
	return 0;
}


int c2_rpc_cob_create_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx)
{
	struct c2_cob_nskey		*key;
	struct c2_cob_nsrec		nsrec;
	struct c2_cob_fabrec		fabrec;
	struct c2_cob			*cob;
	uint64_t			pfid_hi;
	uint64_t			pfid_lo;
	int				rc;

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
	 * How to get unique stob_id for new cob?
	 */
	nsrec.cnr_stobid.si_bits.u_hi = random() % 1000;
	nsrec.cnr_stobid.si_bits.u_lo = random() % 1000;
	nsrec.cnr_nlink = 1;

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

int c2_rpc_cob_lookup_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx)
{
	struct c2_cob_nskey	*key = NULL;
	uint64_t		pfid_hi;
	uint64_t		pfid_lo;
	int			rc;

	C2_PRE(dom != NULL && name != NULL && out != NULL);

	if (pcob == NULL) {
		pfid_hi = pfid_lo = 1;
	} else {
		pfid_hi = COB_GET_PFID_HI(pcob);
		pfid_lo = COB_GET_PFID_LO(pcob);
	}

	c2_cob_nskey_make(&key, pfid_hi, pfid_lo, name);
	if (key == NULL)
		return -ENOMEM;
	*out = NULL;
	rc = c2_cob_lookup(dom, key, CA_NSKEY_FREE | CA_FABREC, out, tx);

	C2_POST(ergo(rc == 0, *out != NULL));
	return rc;
}
int c2_rpc_rcv_sessions_root_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx)
{
	return c2_rpc_cob_lookup_helper(dom, NULL, "SESSIONS", out, tx);
}

enum {
	SESSION_COB_MAX_NAME_LEN = 40
};

int c2_rpc_rcv_conn_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx)
{
	struct c2_cob	*root_session_cob;
	char		name[SESSION_COB_MAX_NAME_LEN];
	int		rc;

	C2_PRE(sender_id != SENDER_ID_INVALID);

	rc = c2_rpc_rcv_sessions_root_get(dom, &root_session_cob, tx);
	if (rc != 0)
		return rc;

	sprintf(name, "SENDER_%lu", sender_id);

	rc = c2_rpc_cob_lookup_helper(dom, root_session_cob, name, out, tx);
	c2_cob_put(root_session_cob);

	return rc;
}

int c2_rpc_rcv_conn_create(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx)
{
	struct c2_cob	*conn_cob = NULL;
	struct c2_cob	*root_session_cob = NULL;
	char		name[SESSION_COB_MAX_NAME_LEN];
	int		rc;

	C2_PRE(dom != NULL && out != NULL);
	C2_PRE(sender_id != SENDER_ID_INVALID);

	sprintf(name, "SENDER_%lu", sender_id);
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

int c2_rpc_rcv_session_lookup(struct c2_cob		*conn_cob,
			      uint64_t			session_id,
			      struct c2_cob		**session_cob,
			      struct c2_db_tx		*tx)
{
	struct c2_cob	*cob = NULL;
	char		name[SESSION_COB_MAX_NAME_LEN];
	int		rc = 0;

	C2_PRE(conn_cob != NULL && session_id <= SESSION_ID_MAX &&
			session_cob != NULL);

	*session_cob = NULL;
	sprintf(name, "SESSION_%lu", session_id);

	rc = c2_rpc_cob_lookup_helper(conn_cob->co_dom, conn_cob, name,
					&cob, tx);
	*session_cob = cob;
	return rc;
}


int c2_rpc_rcv_session_create(struct c2_cob		*conn_cob,
			      uint64_t			session_id,
			      struct c2_cob		**session_cob,
			      struct c2_db_tx		*tx)
{
	struct c2_cob	*cob = NULL;
	char		name[20];
	int		rc = 0;

	C2_PRE(conn_cob != NULL && session_id != SESSION_ID_INVALID &&
			session_cob != NULL);

	*session_cob = NULL;
	sprintf(name, "SESSION_%lu", session_id);

	rc = c2_rpc_cob_create_helper(conn_cob->co_dom, conn_cob, name,
					&cob, tx);
	*session_cob = cob;
	return rc;
}

int c2_rpc_rcv_slot_lookup(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx)
{
	struct c2_cob	*cob = NULL;
	char		name[SESSION_COB_MAX_NAME_LEN];
	int		rc = 0;

	C2_PRE(session_cob != NULL && slot_cob != NULL);

	*slot_cob = NULL;
	sprintf(name, "SLOT_%u:%lu", slot_id, slot_generation);

	rc = c2_rpc_cob_lookup_helper(session_cob->co_dom, session_cob, name,
					&cob, tx);
	*slot_cob = cob;
	return rc;
}


int c2_rpc_rcv_slot_create(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx)
{
	struct c2_cob	*cob = NULL;
	char		name[SESSION_COB_MAX_NAME_LEN];
	int		rc = 0;

	C2_PRE(session_cob != NULL && slot_cob != NULL);

	*slot_cob = NULL;
	sprintf(name, "SLOT_%u:%lu", slot_id, slot_generation);

	rc = c2_rpc_cob_create_helper(session_cob->co_dom, session_cob, name,
					&cob, tx);
	*slot_cob = cob;
	return rc;
}

int c2_rpc_rcv_slot_lookup_by_item(struct c2_cob_domain		*dom,
				   struct c2_rpc_item		*item,
				   struct c2_cob		**cob,
				   struct c2_db_tx		*tx)
{
	struct c2_cob		*conn_cob;
	struct c2_cob		*session_cob;
	struct c2_cob		*slot_cob;
	int			rc;

	C2_PRE(dom != NULL && item != NULL && cob != NULL);

	C2_PRE(item->ri_sender_id != SENDER_ID_INVALID &&
		item->ri_session_id <= SESSION_ID_MAX);

	*cob = NULL;
	rc = c2_rpc_rcv_conn_lookup(dom, item->ri_sender_id, &conn_cob, tx);
	if (rc != 0)
		goto out;

	rc = c2_rpc_rcv_session_lookup(conn_cob, item->ri_session_id,
					&session_cob, tx);
	if (rc != 0)
		goto putconn;

	rc = c2_rpc_rcv_slot_lookup(session_cob, item->ri_slot_id,
					item->ri_slot_generation,
					&slot_cob, tx);
	/*
	 * *cob will be set to NULL if rc != 0
	 */
	*cob = slot_cob;
	c2_cob_put(session_cob);
putconn:
	c2_cob_put(conn_cob);
out:
	return rc;
}

uint64_t c2_rpc_sender_id_get()
{
	uint64_t	sender_id;

	do {
		sender_id = random();
	} while (sender_id == SENDER_ID_INVALID || sender_id == 0);
	return sender_id;
}
uint64_t c2_rpc_session_id_get()
{
	uint64_t	session_id;

	do {
		session_id = random();
	} while (session_id < SESSION_ID_MIN ||
			session_id > SESSION_ID_MAX);
	return session_id;
}

int c2_rpc_slot_init(struct c2_rpc_slot			*slot,
		     const struct c2_rpc_slot_ops	*ops)
{
	struct c2_fop		*fop;
	struct c2_rpc_item	*item;
	struct c2_rpc_slot_ref	*sref;

	c2_list_link_init(&slot->sl_link);
	/* XXX temporary number 4 */
	slot->sl_verno.vn_lsn = 4;
	slot->sl_verno.vn_vc = 0;
	slot->sl_slot_gen = 0;
	slot->sl_xid = 0;
	slot->sl_in_flight = 0;
	slot->sl_max_in_flight = SLOT_DEFAULT_MAX_IN_FLIGHT;
	c2_list_init(&slot->sl_item_list);
	c2_list_init(&slot->sl_ready_list);
	c2_mutex_init(&slot->sl_mutex);
	slot->sl_cob = NULL;

	/*
	 * Add a dummy item with very low verno in item_list
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_noop_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_tstate = RPC_ITEM_PAST_COMMITTED;
	slot->sl_last_sent = item;
	slot->sl_last_persistent = item;

	sref = &item->ri_slot_refs[0];
	sref->sr_slot = slot;
	sref->sr_item = item;
	sref->sr_xid = 0;
	sref->sr_verno.vn_lsn = 4;
	sref->sr_verno.vn_vc = 0;
	sref->sr_slot_gen = slot->sl_slot_gen;
	c2_list_link_init(&sref->sr_link);
	c2_list_add(&slot->sl_item_list, &sref->sr_link);

	return 0;
}

/**
   @see slot_balance
 */
void __slot_balance(struct c2_rpc_slot	*slot,
		    bool		can_consume)
{
	struct c2_rpc_item	*item;
	struct c2_list_link	*link;

	C2_PRE(slot != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_rpc_slot_invariant(slot));

	while (slot->sl_in_flight <= slot->sl_max_in_flight) {
		link = &slot->sl_last_sent->ri_slot_refs[0].sr_link;
		if (c2_list_link_is_last(link, &slot->sl_item_list)) {
			if (can_consume)
				slot->sl_ops->so_slot_idle(slot);
			break;
		}
		item = c2_list_entry(link->ll_next, struct c2_rpc_item,
				ri_slot_refs[0].sr_link);
		if (item->ri_tstate == RPC_ITEM_FUTURE)
			item->ri_tstate = RPC_ITEM_IN_PROGRESS;
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
		if (can_consume)
			slot->sl_ops->so_consume_item(item);
	}
	C2_POST(c2_rpc_slot_invariant(slot));
}

void slot_balance(struct c2_rpc_slot	*slot)
{
	__slot_balance(slot, true);
}

/**
   @see c2_rpc_slot_item_add
   @see c2_rpc_slot_item_add_internal
 */
void __slot_item_add(struct c2_rpc_slot	*slot,
		     struct c2_rpc_item	*item,
		     bool		can_consume)
{
	struct c2_rpc_slot_ref		*sref;

	C2_PRE(slot != NULL && item != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));

	item->ri_tstate = RPC_ITEM_FUTURE;
	sref = &item->ri_slot_refs[0];
	if (c2_rpc_item_is_update(item)) {
		sref->sr_verno.vn_lsn = ++slot->sl_verno.vn_lsn;
		sref->sr_verno.vn_vc = ++slot->sl_verno.vn_vc;
	} else {
		sref->sr_verno.vn_lsn = slot->sl_verno.vn_lsn;
		sref->sr_verno.vn_vc = slot->sl_verno.vn_vc;
	}
	sref->sr_xid = ++slot->sl_xid;
	sref->sr_slot_gen = slot->sl_slot_gen;
	sref->sr_slot = slot;
	sref->sr_item = item;
	c2_list_link_init(&sref->sr_link);
	c2_list_add_tail(&slot->sl_item_list, &sref->sr_link);
	__slot_balance(slot, can_consume);
}

void c2_rpc_slot_item_add(struct c2_rpc_slot	*slot,
			  struct c2_rpc_item	*item)
{
	__slot_item_add(slot, item, true);
}

void c2_rpc_slot_item_add_internal(struct c2_rpc_slot	*slot,
				   struct c2_rpc_item	*item)
{
	__slot_item_add(slot, item, false);
}
struct c2_rpc_item *get_matching_request_item(struct c2_rpc_slot	*slot,
					      struct c2_rpc_item	*reply)
{
	struct c2_rpc_item	*item;
	struct c2_rpc_slot_ref	*sref;

	C2_PRE(slot != NULL && reply != NULL);
	sref = &reply->ri_slot_refs[0];
	C2_PRE(slot == sref->sr_slot);

	if (slot->sl_slot_gen != sref->sr_slot_gen)
		return NULL;

	c2_list_for_each_entry(&slot->sl_item_list, item, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {
		if (c2_verno_cmp(&item->ri_slot_refs[0].sr_verno,
			&sref->sr_verno) == 0 &&
			item->ri_slot_refs[0].sr_xid == sref->sr_xid) {
			return item;
		}
	}
	return NULL;
}

void c2_rpc_slot_reply_received(struct c2_rpc_slot	*slot,
				struct c2_rpc_item	*reply)
{
	struct c2_rpc_item	*req;
	struct c2_rpc_slot_ref	*sref;

	C2_PRE(slot != NULL && reply != NULL && slot->sl_last_sent != NULL);

	sref = &reply->ri_slot_refs[0];
	C2_PRE(slot == sref->sr_slot);

	req = get_matching_request_item(slot, reply);
	if (req == NULL)
		return;

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
	} else {
		C2_ASSERT(req->ri_tstate == RPC_ITEM_IN_PROGRESS);
		C2_ASSERT(slot->sl_in_flight > 0);

		req->ri_tstate = RPC_ITEM_PAST_VOLATILE;
		req->ri_reply = reply;
		slot->sl_in_flight--;
		slot_balance(slot);
	}
}

void c2_rpc_slot_persistence(struct c2_rpc_slot	*slot,
			     struct c2_verno	last_persistent)
{
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*last_persistent_item;
	struct c2_rpc_slot_ref	*sref;

	C2_PRE(slot != NULL && c2_mutex_is_locked(&slot->sl_mutex));

	/*
	 * last persistent should never go back
	 */
	last_persistent_item = slot->sl_last_persistent;
	sref = &last_persistent_item->ri_slot_refs[0];
	if (c2_verno_cmp(&sref->sr_verno, &last_persistent) > 0)
		return;

	/*
	 * Can optimize this loop by starting scanning from
	 * last_persistent_item
	 */
	c2_list_for_each_entry(&slot->sl_item_list, item, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {
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
}

void c2_rpc_slot_reset(struct c2_rpc_slot	*slot,
		       struct c2_verno		last_seen)
{
	struct c2_rpc_item	*item = NULL;
	struct c2_rpc_slot_ref	*sref;

	C2_PRE(slot != NULL);
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

bool c2_rpc_slot_invariant(struct c2_rpc_slot	*slot)
{
	struct c2_rpc_item	*item1 = NULL;
	struct c2_rpc_item	*item2 = NULL;
	struct c2_verno		*v1;
	struct c2_verno		*v2;
	bool			ret = true;

	/*
	 * Traverse slot->sl_item_list using item2 ptr
	 * item1 will be previous item of item2 i.e.
	 * next(item1) == item2
	 */
	c2_list_for_each_entry(&slot->sl_item_list, item2, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {
		if (item1 == NULL) {
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

		ret = c2_rpc_item_is_update(item2) ?
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

void c2_rpc_slot_fini(struct c2_rpc_slot *slot)
{
	struct c2_rpc_item	*item;
	struct c2_fop		*fop;

	c2_list_link_fini(&slot->sl_link);
	c2_list_fini(&slot->sl_ready_list);
	/*
	 * Remove the dummy item from the list
	 */
	C2_ASSERT(c2_list_length(&slot->sl_item_list) == 1);
	item = c2_list_entry(slot->sl_item_list.l_head, struct c2_rpc_item,
				ri_slot_refs[0].sr_link);
	C2_ASSERT(c2_list_link_is_in(&item->ri_slot_refs[0].sr_link));
	c2_list_del(&item->ri_slot_refs[0].sr_link);
	C2_ASSERT(item->ri_slot_refs[0].sr_verno.vn_vc == 0);
	fop = c2_rpc_item_to_fop(item);
	c2_fop_free(fop);

	c2_list_fini(&slot->sl_item_list);
	C2_SET0(slot);
}
void c2_rpc_form_slot_idle(struct c2_rpc_slot *slot)
{
}
void c2_rpc_form_item_ready(struct c2_rpc_item *item)
{
}
void c2_rpc_sender_slot_idle(struct c2_rpc_slot *slot)
{
	c2_rpc_form_slot_idle(slot);
}
void c2_rpc_sender_consume_item(struct c2_rpc_item *item)
{
	c2_rpc_form_item_ready(item);
}
void c2_rpc_sender_consume_reply(struct c2_rpc_item	*req,
				 struct c2_rpc_item	*reply)
{
}

void c2_rpc_rcv_slot_idle(struct c2_rpc_slot *slot)
{
}
void c2_rpc_rcv_consume_item(struct c2_rpc_item *item)
{
}
void c2_rpc_rcv_consume_reply(struct c2_rpc_item  *req,
			      struct c2_rpc_item  *reply)
{
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

