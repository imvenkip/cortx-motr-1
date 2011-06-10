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

const char C2_RPC_INMEM_SLOT_TABLE_NAME[] = "inmem_slot_table";

static int session_fields_init(struct c2_rpc_session	*session,
			       struct c2_rpc_conn	*conn,
			       uint32_t			nr_slots);

static int slot_init(struct c2_rpc_snd_slot	*slot,
		      struct c2_rpc_session	*session);

static int session_zero_attach(struct c2_rpc_conn *conn);

static void session_zero_detach(struct c2_rpc_conn *conn);

static void conn_search(const struct c2_rpcmachine	*machine,
                        uint64_t			sender_id,
                        struct c2_rpc_conn		**out);

static void session_search(const struct c2_rpc_conn	*conn,
                           uint64_t			session_id,
                           struct c2_rpc_session	**out);

int c2_rpc_slot_table_key_cmp(struct c2_table	*table,
			      const void	*key0,
			      const void	*key1)
{
	const struct c2_rpc_slot_table_key *stk0 = key0;
	const struct c2_rpc_slot_table_key *stk1 = key1;
	int rc;

	rc = C2_3WAY(stk0->stk_sender_id, stk1->stk_sender_id) ?:
		C2_3WAY(stk0->stk_session_id, stk1->stk_session_id) ?:
		C2_3WAY(stk0->stk_slot_id, stk1->stk_slot_id) ?:
		C2_3WAY(stk0->stk_slot_generation, stk1->stk_slot_generation);
	return rc;
}

const struct c2_table_ops c2_rpc_slot_table_ops = {
	.to = {
		[TO_KEY] = {
		     .max_size = sizeof (struct c2_rpc_slot_table_key)
		},
		[TO_REC] = {
		     .max_size = sizeof (struct c2_rpc_inmem_slot_table_value)
		}
	},
	.key_cmp = c2_rpc_slot_table_key_cmp
};

int c2_rpc_reply_cache_init(struct c2_rpc_reply_cache	*rcache,
			    struct c2_cob_domain	*dom,
			    struct c2_fol		*fol)
{
	struct c2_db_tx		tx;
	struct c2_cob		*cob;
	int	rc;

	C2_PRE(dom != NULL);
	//C2_PRE(fol != NULL);
	rcache->rc_dbenv = dom->cd_dbenv;
	rcache->rc_dom = dom;
	rcache->rc_fol = fol;

	C2_ALLOC_PTR(rcache->rc_inmem_slot_table);

	if (rcache->rc_inmem_slot_table == NULL)
		return -ENOMEM;

	c2_list_init(&rcache->rc_item_list);

	/*
	  XXX find out how to create an in memory c2_table
	 */
	rc = c2_table_init(rcache->rc_inmem_slot_table, rcache->rc_dbenv,
		       C2_RPC_INMEM_SLOT_TABLE_NAME, 0, &c2_rpc_slot_table_ops);
	if (rc != 0) {
		goto errout;
	}
	/*
	 * Create root sessions cob
	 */
	c2_db_tx_init(&tx, rcache->rc_dbenv, 0);
        rc = c2_rpc_cob_create_helper(dom, NULL, "SESSIONS", &cob, &tx);
        C2_ASSERT(rc == 0 || rc == -EEXIST);
        c2_db_tx_commit(&tx);
        if(rc == 0)
                c2_cob_put(cob);
        cob = NULL;

	return 0;		/* success */

errout:
	if (rcache->rc_inmem_slot_table != NULL)
		c2_free(rcache->rc_inmem_slot_table);
	return rc;
}

void c2_rpc_reply_cache_fini(struct c2_rpc_reply_cache *rcache)
{
	C2_PRE(rcache != NULL && rcache->rc_dbenv != NULL &&
			rcache->rc_inmem_slot_table != NULL);

	c2_table_fini(rcache->rc_inmem_slot_table);
	C2_SET0(rcache);
}

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

	c2_rpc_for_each_conn(machine, conn) {
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

int c2_rpc_conn_init(struct c2_rpc_conn		*conn,
		     struct c2_service_id	*svc_id,
		     struct c2_rpcmachine	*machine)
{
	struct c2_fop			*fop;
	struct c2_rpc_fop_conn_create	*fop_cc;
	struct c2_rpc_item		*item;
	c2_time_t			deadline;
	int				rc;

	C2_PRE(conn != NULL && conn->c_state == C2_RPC_CONN_UNINITIALISED);
	C2_PRE(svc_id != NULL && machine != NULL);

	/*
	 * XXX Add assert to confirm @machine is in valid state
	 */

	if (conn == NULL || svc_id == NULL || machine == NULL)
		return -EINVAL;

	conn->c_state = C2_RPC_CONN_INITIALISING;
	conn->c_service_id = svc_id;
	conn->c_sender_id = SENDER_ID_INVALID;
	c2_list_init(&conn->c_sessions);
	conn->c_nr_sessions = 0;
	c2_chan_init(&conn->c_chan);
	c2_mutex_init(&conn->c_mutex);
	c2_list_link_init(&conn->c_link);
	conn->c_flags = 0;
	conn->c_rpcmachine = machine;
	conn->c_rc = 0;

	rc = session_zero_attach(conn);
	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_UNINITIALISED;
		return rc;
	}

	fop = c2_fop_alloc(&c2_rpc_fop_conn_create_fopt, NULL);
	if (fop == NULL) {
		conn->c_state = C2_RPC_CONN_UNINITIALISED;
		session_zero_detach(conn);
		return rc;
	}

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

	/*
	 * Send conn_create request "out of any session"
	 */
	item = c2_fop_to_rpc_item(fop);
	item->ri_sender_id = SENDER_ID_INVALID;
	item->ri_session_id = SESSION_ID_NOSESSION;

	c2_mutex_lock(&machine->cr_session_mutex);
	c2_list_add(&machine->cr_rpc_conn_list, &conn->c_link);
	c2_mutex_unlock(&machine->cr_session_mutex);

	C2_SET0(&deadline);
	rc = c2_rpc_submit(conn->c_service_id, NULL, item,
				C2_RPC_ITEM_PRIO_MAX, &deadline);

	if (rc != 0) {
		conn->c_state = C2_RPC_CONN_UNINITIALISED;
		session_zero_detach(conn);

		conn->c_private = NULL;
		c2_fop_free(fop);

		c2_mutex_lock(&machine->cr_session_mutex);
		c2_list_del(&conn->c_link);
		c2_mutex_unlock(&machine->cr_session_mutex);
	}
	C2_POST(ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISING &&
			c2_rpc_conn_invariant(conn)));
	C2_POST(ergo(rc != 0, conn->c_state == C2_RPC_CONN_UNINITIALISED));
	return rc;
}
C2_EXPORTED(c2_rpc_conn_init);

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

	c2_rpc_for_each_conn(machine, conn) {
		c2_mutex_lock(&conn->c_mutex);
		if (conn->c_state == C2_RPC_CONN_INITIALISING) {
			/*
			 * during conn_init() the fop is stored in
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
		 * cannot find an INITIALISING conn
		 */
		c2_mutex_unlock(&machine->cr_session_mutex);
		return;
	}

	C2_ASSERT(conn != NULL && c2_mutex_is_locked(&conn->c_mutex));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_INITIALISING &&
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

	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_INITIALISING);

	C2_ALLOC_PTR(session);
	if (session == NULL)
		return -ENOMEM;

	C2_SET0(session);

	rc = session_fields_init(session, conn, 1);   /* 1 => number of slots */
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
static int slot_init(struct c2_rpc_snd_slot	*slot,
		      struct c2_rpc_session	*session)
{
	struct c2_clink		*clink;

        C2_SET0(slot);
	slot->ss_session = session;
        slot->ss_flags = SLOT_IN_USE;
        c2_list_init(&slot->ss_ready_list);
        c2_list_init(&slot->ss_replay_list);
	c2_mutex_init(&slot->ss_mutex);
	c2_chan_init(&slot->ss_chan);

	C2_ALLOC_PTR(clink);
	if (clink == NULL) {
		return -ENOMEM;
	}

	c2_clink_init(clink, c2_rpc_snd_slot_state_changed);
	c2_clink_add(&slot->ss_chan, clink);
	return 0;
}

int c2_rpc_conn_terminate(struct c2_rpc_conn *conn)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate	*fop_ct;
	struct c2_rpc_item			*item;
	c2_time_t				deadline;
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

	/*
	 * Send conn terminate fop "out of session"
	 */
	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);

	item->ri_sender_id = SENDER_ID_INVALID;
	item->ri_session_id = SESSION_ID_NOSESSION;

	conn->c_state = C2_RPC_CONN_TERMINATING;

	C2_SET0(&deadline);
	rc = c2_rpc_submit(conn->c_service_id, NULL, item,
				C2_RPC_ITEM_PRIO_MAX, &deadline);

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
		conn->c_service_id = NULL;
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
		conn->c_state == C2_RPC_CONN_FAILED);
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

		case C2_RPC_CONN_INITIALISING:
			return conn->c_sender_id == SENDER_ID_INVALID &&
			conn->c_nr_sessions == 0 &&
			conn->c_service_id != NULL &&
			conn->c_rpcmachine != NULL &&
			conn->c_private != NULL &&
			c2_list_contains(&conn->c_rpcmachine->cr_rpc_conn_list,
				&conn->c_link);
			
		case C2_RPC_CONN_ACTIVE:
			result = conn->c_sender_id != SENDER_ID_INVALID &&
		        	    conn->c_service_id != NULL &&
		        	    conn->c_rpcmachine != NULL &&
				    c2_list_invariant(&conn->c_sessions) &&
			c2_list_contains(&conn->c_rpcmachine->cr_rpc_conn_list,
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
			c2_list_contains(&conn->c_rpcmachine->cr_rpc_conn_list,
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

static int session_fields_init(struct c2_rpc_session	*session,
			       struct c2_rpc_conn	*conn,
			       uint32_t			nr_slots)
{
	int	i;
	int	rc = 0;

	C2_PRE(session != NULL && nr_slots >= 1);

	c2_list_link_init(&session->s_link);
	session->s_session_id = SESSION_ID_INVALID;
	session->s_conn = conn;
	c2_chan_init(&session->s_chan);
	c2_mutex_init(&session->s_mutex);
	session->s_nr_slots = nr_slots;
	session->s_slot_table_capacity = nr_slots;

	C2_ALLOC_ARR(session->s_slot_table, nr_slots);
	if (session->s_slot_table == NULL) {
		rc = -ENOMEM;
		goto out_err;
	}

	C2_SET0(session->s_slot_table);
	for (i = 0; i < nr_slots; i++) {
		C2_ALLOC_PTR(session->s_slot_table[i]);
		if (session->s_slot_table[i] == NULL) {
			rc = -ENOMEM;
			goto out_err;
		}
		rc = slot_init(session->s_slot_table[0], session);
		if (rc != 0)
			goto out_err;
	}
	return 0;

out_err:
	C2_ASSERT(rc != 0);
	if (session->s_slot_table != NULL) {
		for (i = 0; i < nr_slots; i++) {
			if (session->s_slot_table[i] != NULL)
				c2_free(session->s_slot_table[i]);
		}
		session->s_nr_slots = 0;
		session->s_slot_table_capacity = 0;
		c2_free(session->s_slot_table);
	}
	return rc;
}
int c2_rpc_session_create(struct c2_rpc_session	*session,
			  struct c2_rpc_conn	*conn)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_create	*fop_sc;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session_0 = NULL;
	int					rc = 0;

	C2_PRE(conn != NULL && session != NULL &&
		session->s_state == C2_RPC_SESSION_UNINITIALISED);

	c2_mutex_lock(&conn->c_mutex);
	if (conn->c_state != C2_RPC_CONN_ACTIVE ||
		session->s_state != C2_RPC_SESSION_UNINITIALISED) {
		rc = -EINVAL;
		goto out;
	}
	C2_ASSERT(c2_rpc_conn_invariant(conn));
		
	rc = session_fields_init(session, conn, DEFAULT_SLOT_COUNT);
	if (rc != 0)
		goto out;

	fop = c2_fop_alloc(&c2_rpc_fop_conn_create_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	fop_sc = c2_fop_data(fop);
	C2_ASSERT(fop_sc != NULL);

	fop_sc->rsc_snd_id = conn->c_sender_id;

	session->s_state = C2_RPC_SESSION_CREATING;
	c2_list_add(&conn->c_sessions, &session->s_link);
	conn->c_nr_sessions++;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);

	session_search(conn, SESSION_0, &session_0);
	C2_ASSERT(session_0 != NULL);

	c2_mutex_lock(&session_0->s_mutex);
	/*
	 * Session_create request always go on session 0 and slot 0
	 */
	c2_rpc_snd_slot_enq(session_0, 0, item);  /* 0 => slot id */
	c2_mutex_unlock(&session_0->s_mutex);

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
	uint64_t				sender_id;
	uint64_t				session_id;

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
	struct c2_rpc_fop_session_destroy	*fop_sd;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session_0 = NULL;
	bool					slot_is_busy;
	struct c2_rpc_snd_slot			*slot;
	int					i;
	int					rc = 0;

	C2_PRE(session != NULL && session->s_conn != NULL);

	/*
	 * Make sure session does not have any "reserved" session id
	 */
	C2_ASSERT(session->s_session_id >= SESSION_ID_MIN &&
			session->s_session_id <= SESSION_ID_MAX);

	c2_mutex_lock(&session->s_mutex);

	C2_ASSERT(c2_rpc_session_invariant(session));

	if (session->s_state != C2_RPC_SESSION_ALIVE) {
		c2_mutex_unlock(&session->s_mutex);
		rc = -EINVAL;
		goto out;
	}

	/*
	 * Make sure no slot is "busy"
	 */
	for (i = 0; i < session->s_nr_slots; i++) {
		slot = session->s_slot_table[i];
		C2_ASSERT(slot != NULL);
		slot_is_busy = c2_rpc_snd_slot_is_busy(slot);
		if (slot_is_busy) {
			c2_mutex_unlock(&session->s_mutex);
			rc = -EBUSY;
			goto out;
		}
	}

	/*
	 * XXX TODO:
	 * Add a check to confirm that there is no active update_stream
	 * present. Can A counter that count number of active update_stream
	 * help? Then we need two routines update_stream_created() and
	 * update_stream_terminated() to report the events to session module
	 */

	fop = c2_fop_alloc(&c2_rpc_fop_session_destroy_fopt, NULL);
	if (fop == NULL) {
		c2_mutex_unlock(&session->s_mutex);
		rc = -ENOMEM;
		goto out;
	}

	fop_sd = c2_fop_data(fop);
	C2_ASSERT(fop_sd != NULL);

	fop_sd->rsd_sender_id = session->s_conn->c_sender_id;
	fop_sd->rsd_session_id = session->s_session_id;

	item = c2_fop_to_rpc_item(fop);

	C2_ASSERT(item != NULL);

	session->s_state = C2_RPC_SESSION_TERMINATING;
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

	c2_mutex_lock(&session_0->s_mutex);
	c2_rpc_snd_slot_enq(session_0, 0, item);
	c2_mutex_unlock(&session_0->s_mutex);

	c2_mutex_unlock(&session->s_conn->c_mutex);

	c2_chan_broadcast(&session->s_chan);
out:
	C2_POST(ergo(rc == 0, session->s_state == C2_RPC_SESSION_TERMINATING));
	C2_POST(c2_rpc_session_invariant(session));
	return rc;
}
C2_EXPORTED(c2_rpc_session_terminate);

void c2_rpc_session_terminate_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_fop_session_destroy_rep	*fop_sdr;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session;
	struct c2_rpc_item			*item;
	struct c2_rpcmachine			*machine;
	uint64_t				sender_id;
	uint64_t				session_id;

	C2_PRE(fop != NULL);

	fop_sdr = c2_fop_data(fop);
	C2_ASSERT(fop_sdr != NULL);

	sender_id = fop_sdr->rsdr_sender_id;
	session_id = fop_sdr->rsdr_session_id;

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

	if (fop_sdr->rsdr_rc == 0) {
		session->s_state = C2_RPC_SESSION_TERMINATED;
		session->s_rc = 0;
		printf("strr: session terminated %lu\n", session_id);
	} else {
		session->s_state = C2_RPC_SESSION_FAILED;
		session->s_rc = fop_sdr->rsdr_rc;
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
	int	i;

	C2_PRE(session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_FAILED);
	C2_ASSERT(c2_rpc_session_invariant(session));
	c2_list_link_fini(&session->s_link);
	session->s_session_id = SESSION_ID_INVALID;
	session->s_conn = NULL;
	c2_chan_fini(&session->s_chan);
	c2_mutex_fini(&session->s_mutex);

	for (i = 0; i < session->s_nr_slots; i++) {
		/*
		 * XXX Is this assert is valid?
		 * sender sent an item. receiver crashed and never came up.
		 * sender decided to terminate session. slot can be busy in
		 * that case
		 */
		C2_ASSERT(!c2_rpc_snd_slot_is_busy(session->s_slot_table[i]));
		c2_free(session->s_slot_table[i]);
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


/**
   Fill all the session related fields of c2_rpc_item.

   If item is unbound, assign session and slot id.
   If item is bound, then no need to assign session and slot as it is already
   there in the item.

   Copy verno of slot into item.verno. And mark slot as 'waiting_for_reply'

   rpc-core can call this routine whenever it finds it appropriate to
   assign slot and session info to an item.

   If no rpc conn or rpc session exists to target endpoint, this routine
   triggers creation of conn or session respectively and return -EAGAIN.
   Caller is expected to call this routine again the same item.

   Assumption: c2_rpc_item has a field giving service_id of
                destination service.
 */
int c2_rpc_session_item_prepare(struct c2_rpc_item *item)
{
	struct c2_rpcmachine		*machine = NULL;
	struct c2_rpc_conn		*conn = NULL;
	struct c2_rpc_conn		*saved_conn = NULL;
	struct c2_rpc_session		*session = NULL;
	struct c2_rpc_snd_slot		*slot = NULL;
	bool				found = false;
	bool				conn_exists = false;
	bool				session_exists = false;
	bool				slots_scanned = false;
	int				i;
	int				rc = 0;

	C2_ASSERT(item != NULL && item->ri_mach != NULL &&
			item->ri_service_id != NULL);

	/*
	 * XXX Important: Whenever an c2_rpc_item is init-ed
	 * ri_sender_id and ri_session_id should be set to
	 * SENDER_ID_INVALID and SESSION_ID_INVALID respectively.
	 */
	if (item->ri_session_id == SESSION_ID_NOSESSION) {
		/*
		 * This item needs to be sent out of any session.
		 * e.g. conn_create/conn_terminate request
		 */
		return 0;
	}
	if (item->ri_sender_id != SENDER_ID_INVALID) {
		/*
		 * This item already has all the session related information
		 * Nothing to do.
		 */
		C2_ASSERT(item->ri_session_id != SESSION_ID_INVALID &&
				item->ri_slot_id != SLOT_ID_INVALID);
		return 0;
	}

	/*
	 * Find out any unbusy slot which can go to destination pointed by
	 * item->ri_service_id
	 */
	machine = item->ri_mach;
	c2_mutex_lock(&machine->cr_session_mutex);

	c2_rpc_for_each_conn(machine, conn) {

		/*
		 * Does any conn exists to destination end-point?
		 * No need to hold conn->c_mutex here as c_service_id
		 * does not change in lifetime of conn
		 */
		if (c2_services_are_same(conn->c_service_id,
				item->ri_service_id)) {
			conn_exists = true;
			/*
			 * If conn does not have any session then we can use
			 * saved_conn to initiate session creation.
			 */
			saved_conn = conn;
		} else {
			continue;
		}

		c2_mutex_lock(&conn->c_mutex);
		if (conn->c_state == C2_RPC_CONN_ACTIVE) {

			c2_rpc_for_each_session(conn, session) {
				if (session->s_session_id != SESSION_0)
					/*
					 * there exists a non-zero session in 
					 * conn. So no need to trigger session
					 * creation
					 */
					session_exists = true;

				c2_mutex_lock(&session->s_mutex);

				/*
				 * We don't use session 0 to send anything except
				 * session_create and session_termiante items
				 */
				if (session->s_state != C2_RPC_SESSION_ALIVE ||
					session->s_session_id == SESSION_0) {
					c2_mutex_unlock(&session->s_mutex);
					continue;
				}

				for (i = 0; i < session->s_nr_slots; i++) {
					slots_scanned = true;
					slot = session->s_slot_table[i];
					C2_ASSERT(slot != NULL);

					if (c2_rpc_snd_slot_is_busy(slot))
						continue;

					found = true;
					/*
					 * jump out of loop leaving conn and
					 * session "locked"
					 */
					goto out_of_loops;
				}
				c2_mutex_unlock(&session->s_mutex);
			} /* end of c2_rpc_for_each_session() */
		}
		c2_mutex_unlock(&conn->c_mutex);
	}	/* end of c2_rpc_for_each_conn() */

out_of_loops:
	c2_mutex_unlock(&machine->cr_session_mutex);

	if (found) {
		C2_ASSERT(c2_mutex_is_locked(&session->s_mutex));
		C2_ASSERT(c2_mutex_is_locked(&conn->c_mutex));

		item->ri_sender_id = conn->c_sender_id;
		item->ri_session_id = session->s_session_id;
		item->ri_slot_id = i;
		item->ri_slot_generation = slot->ss_generation;
		item->ri_verno = slot->ss_verno;

		printf("item bound: [%lu:%lu:%u->%lu]\n", item->ri_sender_id,
			item->ri_session_id, item->ri_slot_id,
			item->ri_verno.vn_vc);
		slot->ss_sent_item = item;
		c2_rpc_snd_slot_mark_busy(slot);

		c2_mutex_unlock(&session->s_mutex);
		c2_mutex_unlock(&conn->c_mutex);

		return 0;
	}

	/*
	 * XXX Important
	 * If there is no rpc-connection or alive session to the destination,
	 * to send first unbound item to such a destination, will require
	 * multiple calls to c2_rpc_session_item_prepare() until the call
	 * returns 0.
	 * On first call, when it doesn't find any rpc_conn object present,
	 * this function triggers rpc_conn create with the destination and
	 * returns -EAGAIN
	 * Assume by the time the next call is made to item_prepare() the
	 * conn_create is successfully completed. (even if conn_create is in
	 * progress there is no issue. item_prepare() will return -EAGAIN)
	 * Then item_prepare() triggers rpc_session create and returns -EAGAIN
	 * When next time item_prepare() is called with same unbound item and
	 * assuming session creation is completed successfully, the item will
	 * get <sender_id, session_id, slot_id, verno>
	 * XXX It is possible to optimize this sequence of events. But is
	 * optimization necessary, as this is going to happen just ONCE with
	 * each receiver?
	 */

	if (slots_scanned) {
		/*
		 * This implies, there is at least one active rpc connection
		 * and alive session.
		 */
		C2_ASSERT(conn_exists && session_exists);
		return -EBUSY;
	}
	if (session_exists && !slots_scanned) {
		/*
		 * This can happen only if session is present but not in
		 * ALIVE state.
		 * !slots_scanned is redundant part of condition and is written
		 * for better understanding.
		 */
		return -EAGAIN;
	}
	if (conn_exists && !session_exists) {
		C2_ASSERT(saved_conn != NULL &&
			c2_services_are_same(saved_conn->c_service_id,
						item->ri_service_id));
		c2_mutex_lock(&saved_conn->c_mutex);

		if (saved_conn->c_state == C2_RPC_CONN_ACTIVE) { 
			C2_ALLOC_PTR(session);
			if (session == NULL) {
				c2_mutex_unlock(&saved_conn->c_mutex);
				return -ENOMEM;
			}
			c2_mutex_unlock(&saved_conn->c_mutex);
			rc = c2_rpc_session_create(session, saved_conn);

			return rc ?: -EAGAIN;
		}
		/*
		 * conn exists but is not ACTIVE. So cannot trigger session
		 * creation
		 */
		c2_mutex_unlock(&saved_conn->c_mutex);
		return -EAGAIN;
	}
	if (!conn_exists) {
		/*
		 * No connection exists to the destination. create one
		 */
		C2_ASSERT(!session_exists && !slots_scanned);
		C2_ALLOC_PTR(conn);
		if (conn == NULL)
			return -ENOMEM;

		rc = c2_rpc_conn_init(conn, item->ri_service_id, machine);
		return rc ?: -EAGAIN;
	}
	/*
	 * Control shouldn't reach here. All the cases where a slot is not
	 * assigned are covered above.
	 */
	C2_ASSERT(0);
}

/**
   Inform session module that a reply item is received.

   rpc-core can call this function when it receives an item. session module
   can then mark corresponding slot "unbusy", move the item to replay list etc.

   @return 0 If reply has expected verno. @out contains pointer to item whose
		reply is this. For "out-of-session" reply it the routine
		simply returns 0 but *out is NULL.
   @return < 0 If reply item does not have expected verno
 */
int c2_rpc_session_reply_item_received(struct c2_rpc_item	*item,
				       struct c2_rpc_item	**out)
{
	struct c2_rpc_conn		*conn = NULL;
	struct c2_rpc_session		*session = NULL;
	struct c2_rpc_snd_slot		*slot = NULL;
	struct c2_rpcmachine		*machine = NULL;
	int				rc = 0;

	C2_PRE(item != NULL && item->ri_mach != NULL);

	*out = NULL;
	if (item->ri_session_id == SESSION_ID_NOSESSION) {
		/*
		 * This is reply to an "out-of-session" request e.g.
		 * conn create or conn terminate
		 */
		return 0;
	}

	C2_ASSERT(item->ri_sender_id != SENDER_ID_INVALID &&
		item->ri_session_id != SESSION_ID_INVALID &&
		item->ri_slot_id != SLOT_ID_INVALID);

	machine = item->ri_mach;
	c2_mutex_lock(&machine->cr_session_mutex);

	conn_search(machine, item->ri_sender_id, &conn);
	if (conn == NULL) {
		c2_mutex_unlock(&machine->cr_session_mutex);
		return -ENOENT;
	}
	c2_mutex_lock(&conn->c_mutex);

	c2_mutex_unlock(&machine->cr_session_mutex);

	session_search(conn, item->ri_session_id, &session);
	if (session == NULL) {
		c2_mutex_unlock(&conn->c_mutex);
		return -ENOENT;
	}
	c2_mutex_lock(&session->s_mutex);

	c2_mutex_unlock(&conn->c_mutex);

	if (session->s_state != C2_RPC_SESSION_ALIVE ||
			item->ri_slot_id >= session->s_nr_slots) {
		rc = -1;
		goto out;
	}

	slot = session->s_slot_table[item->ri_slot_id];
	C2_ASSERT(slot != NULL);

	if (!c2_rpc_snd_slot_is_busy(slot) ||
	    item->ri_slot_generation != slot->ss_generation ||
	    c2_verno_cmp(&item->ri_verno, &slot->ss_verno) != 0) {
		rc = -1;
		goto out;
	}

	/*
	 * XXX temporary. Use proper lsn when integrated with FOL
	 */
	slot->ss_verno.vn_lsn++;
	slot->ss_verno.vn_vc++;

	*out = slot->ss_sent_item;
	c2_list_add(&slot->ss_replay_list, &(*out)->ri_slot_link);
	c2_rpc_snd_slot_mark_unbusy(slot);
	c2_chan_broadcast(&slot->ss_chan);

out:
	c2_mutex_unlock(&session->s_mutex);
	return rc;
}

/**
   Start session recovery.

   @pre session->s_state == C2_RPC_SESSION_ALIVE
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_RECOVERING)

 */
int c2_rpc_session_recovery_start(struct c2_rpc_session *session)
{
	return 0;
}


/**
   All session specific parameters except slot table
   should go here

   All instances of c2_rpc_session_params will be stored
   in db5 in memory table with <sender_id, session_id> as key.
 */
struct c2_rpc_session_params {
        uint32_t        sp_nr_slots;
        uint32_t        sp_target_highest_slot_id;
        uint32_t        sp_enforced_highest_slot_id;
};

int c2_rpc_session_params_get(uint64_t				sender_id,
			      uint64_t				session_id,
			      struct c2_rpc_session_params	**out)
{
	return 0;
}

int c2_rpc_session_params_set(uint64_t				sender_id,
			      uint64_t				session_id,
			      struct c2_rpc_session_params	*param)
{
	return 0;
}

/**
   Insert a reply item in reply cache and advance slot version.

   @note that for certain fop types eg. READ, reply cache does not
   contain the whole reply state, because it is too large. Instead
   the cache contains the pointer (block address) to the location
   of the data.
 */
int c2_rpc_reply_cache_insert(struct c2_rpc_item	*item,
			      struct c2_db_tx		*tx)
{
	struct c2_table				*inmem_slot_table;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_pair			inmem_pair;
	struct c2_cob_domain			*dom;
	struct c2_rpcmachine			*machine;
	struct c2_cob				*slot_cob;
	int					rc;

	C2_PRE(item != NULL && tx != NULL);

	machine = item->ri_mach;
	C2_ASSERT(machine != NULL);

	dom = machine->cr_rcache.rc_dom;
	C2_ASSERT(dom != NULL);

	inmem_slot_table = machine->cr_rcache.rc_inmem_slot_table;

	key.stk_sender_id = item->ri_sender_id;
	key.stk_session_id = item->ri_session_id;
	key.stk_slot_id = item->ri_slot_id;
	key.stk_slot_generation = item->ri_slot_generation;

	printf("rci: inserting in cache: [%lu:%lu:%u]\n",
		item->ri_sender_id,
		item->ri_session_id,
		item->ri_slot_id);
	/*
	 * Update version of cob representing slot
	 */
	rc = c2_rpc_rcv_slot_lookup_by_item(dom, item, &slot_cob, tx);
	if (rc != 0) {
		printf("rci: slot lookup failed with %d\n", rc);
		goto out;
	}

	/*
	 * XXX When integrated with fol assign proper lsn
	 * instead of just increamenting it
	 */
	slot_cob->co_fabrec.cfb_version.vn_lsn++;
	slot_cob->co_fabrec.cfb_version.vn_vc++;

	rc = c2_cob_update(slot_cob, NULL, &slot_cob->co_fabrec, tx);
	if (rc != 0) {
		printf("rci: cob update failed with %d\n", rc);
		goto out;
	}
	c2_cob_put(slot_cob);

	/*
	 * Mark in core slot as "not busy"
	 */
	c2_db_pair_setup(&inmem_pair, inmem_slot_table, &key, sizeof key,
			&inmem_slot, sizeof inmem_slot);
	rc = c2_table_lookup(tx, &inmem_pair);
	if (rc != 0) {
		printf("rci: error %d occured while looking up in slot table\n", rc);
		goto out1;
	}
	C2_ASSERT(inmem_slot.istv_busy);

	inmem_slot.istv_busy = false;

	rc = c2_table_update(tx, &inmem_pair);
	if (rc != 0) {
		printf("rci: error %d occured while marking slot unbusy\n", rc);
		goto out1;
	}
	c2_list_add(&machine->cr_rcache.rc_item_list, &(item->ri_rc_link));
	printf("rci: reply cached\n");
out1:
	c2_db_pair_release(&inmem_pair);
	c2_db_pair_fini(&inmem_pair);

out:
	return rc;
}

/**
   XXX express following precondition as a function of req->ri_state
   @pre c2_rpc_item_init() must have been called on reply item
 */
int c2_rpc_session_reply_prepare(struct c2_rpc_item	*req,
				 struct c2_rpc_item	*reply,
				 struct c2_db_tx	*tx)
{
	C2_PRE(req != NULL && reply != NULL && tx != NULL);

	//C2_ASSERT(reply->ri_mach != NULL);
	reply->ri_mach = req->ri_mach;

	reply->ri_sender_id = req->ri_sender_id;
	reply->ri_session_id = req->ri_session_id;
	reply->ri_slot_id = req->ri_slot_id;
	reply->ri_slot_generation = req->ri_slot_generation;
	reply->ri_verno = req->ri_verno;

	/*
	  rpc_conn_create request comes with ri_session_id set to
	  SESSION_ID_NOSESSION. Don't cache reply of such requests.
	 */
	if (req->ri_session_id != SESSION_ID_NOSESSION) {
		c2_rpc_reply_cache_insert(reply, tx);
	}
	return 0;
}

/**
   Checks whether received item is correct in sequence or not and suggests
   action to be taken.
   'reply_out' is valid only if return value is RESEND_REPLY.
 */
enum c2_rpc_session_seq_check_result
c2_rpc_session_item_received(struct c2_rpc_item 	*item,
			     struct c2_rpc_item 	**reply_out)
{
	struct c2_rpcmachine			*machine;
	struct c2_cob_domain			*dom;
	struct c2_table				*inmem_slot_table;
	struct c2_rpc_slot_table_key		key;
	struct c2_db_pair			pair;
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_tx				tx;
	struct c2_rpc_item			*citem = NULL;	/* cached rpc item */
	struct c2_cob				*slot_cob = NULL;
	enum c2_rpc_session_seq_check_result	rc = SCR_ERROR;
	int					undoable;
	int					redoable;
	bool					slot_is_busy;
	int					err;

	C2_PRE(item != NULL);
	*reply_out = NULL;

	/*
	 * No seq check for items with session_id SESSION_ID_NOSESSION
	 *	e.g. rpc_conn_create request
	 */
	if (item->ri_session_id == SESSION_ID_NOSESSION)
		return SCR_ACCEPT_ITEM;

	machine = item->ri_mach;
	C2_ASSERT(machine != NULL);

	dom = machine->cr_rcache.rc_dom;
	C2_ASSERT(dom != NULL);

	inmem_slot_table = machine->cr_rcache.rc_inmem_slot_table;

	/*
	 * Is slot already busy?
	 */
	key.stk_sender_id = item->ri_sender_id;
	key.stk_session_id = item->ri_session_id;
	key.stk_slot_id = item->ri_slot_id;
	key.stk_slot_generation = item->ri_slot_generation;

	err = c2_db_tx_init(&tx, machine->cr_rcache.rc_dbenv, 0);
	if (err != 0) {
		rc = SCR_ERROR;
		goto errout;
	}

	/*
	 * Read in memory slot table value
	 */
	c2_db_pair_setup(&pair, inmem_slot_table, &key, sizeof key,
				&inmem_slot, sizeof inmem_slot);
	err = c2_table_lookup(&tx, &pair);
	if (err != 0) {
		rc = SCR_SESSION_INVALID;
		goto errabort;
	}

	err = c2_rpc_rcv_slot_lookup_by_item(dom, item, &slot_cob, &tx);
	if (err != 0) {
		rc = SCR_ERROR;
		goto errabort;
	}

	C2_ASSERT(slot_cob->co_valid & CA_FABREC);

		
	undoable = c2_verno_is_undoable(&slot_cob->co_fabrec.cfb_version,
						&item->ri_verno, 0);
	redoable = c2_verno_is_redoable(&slot_cob->co_fabrec.cfb_version,
						&item->ri_verno, 0);
	slot_is_busy = inmem_slot.istv_busy;

	printf("item_rcvd busy: %s [%lu:%lu] == [%lu:%lu]\n",
			slot_is_busy ? "BUSY" : "NOT_BUSY",
			slot_cob->co_fabrec.cfb_version.vn_lsn,
			slot_cob->co_fabrec.cfb_version.vn_vc,
			item->ri_verno.vn_lsn,
			item->ri_verno.vn_vc);
	if (undoable == 0) {
		bool		found = false;

		/*
		 * Reply should be fetched from reply cache
		 */
		c2_list_for_each_entry(&machine->cr_rcache.rc_item_list,
		    citem, struct c2_rpc_item, ri_rc_link) {
			if (citem->ri_sender_id == item->ri_sender_id &&
			    citem->ri_session_id == item->ri_session_id &&
			    citem->ri_slot_id == item->ri_slot_id &&
			citem->ri_slot_generation == item->ri_slot_generation) {
				*reply_out = citem;
				rc = SCR_RESEND_REPLY;
				found = true;
			}
		}

		/*
		 * Reply MUST be present in reply cache
		 * XXX Following assert is disabled for testing, but it is valid
		 * one and should be enabled
		 */
		//C2_ASSERT(found);
	} else {
		if (redoable == 0) {
			if (slot_is_busy) {
				/*
				 * Same item is already in process.
				 */
				rc = SCR_IGNORE_ITEM;
			} else {
				/*
				 * Mark slot as busy and accept the item
				 */
				inmem_slot.istv_busy = true;
				err = c2_table_update(&tx, &pair);
				if (err != 0)
					goto errabort;
				rc = SCR_ACCEPT_ITEM;
			}
		} else if (redoable == -EALREADY || redoable == -EAGAIN) {
			/*
			 * This is misordered entry
			 */
			rc = SCR_SEND_ERROR_MISORDERED;
		}
	}

errabort:
	/*
	 * state of slot is marked as busy only in case item is accepted
	 */
	if (rc == SCR_ACCEPT_ITEM)
		c2_db_tx_commit(&tx);
	else
		c2_db_tx_abort(&tx);

	if (slot_cob != NULL)
		c2_cob_put(slot_cob);

	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
errout:
	return rc;
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

bool c2_rpc_snd_slot_is_busy(const struct c2_rpc_snd_slot *slot)
{
        return (slot->ss_flags & SLOT_WAITING_FOR_REPLY) != 0;
}

void c2_rpc_snd_slot_mark_busy(struct c2_rpc_snd_slot *slot)
{
	C2_ASSERT((slot->ss_flags & SLOT_WAITING_FOR_REPLY) == 0);
	slot->ss_flags |= SLOT_WAITING_FOR_REPLY;
}

void c2_rpc_snd_slot_mark_unbusy(struct c2_rpc_snd_slot *slot)
{
	C2_ASSERT((slot->ss_flags & SLOT_WAITING_FOR_REPLY) != 0);
	slot->ss_flags &= ~SLOT_WAITING_FOR_REPLY;
}
void c2_rpc_snd_slot_enq(struct c2_rpc_session	*session,
			 uint32_t		slot_id,
			 struct c2_rpc_item	*item)
{
	struct c2_rpc_snd_slot		*slot;

	/*
	 * Should we return -EINVAL if any of following
	 * preconditions is false?
	 */
	C2_PRE(session != NULL);
	C2_PRE(c2_mutex_is_locked(&session->s_mutex));
	C2_PRE(session->s_state == C2_RPC_SESSION_ALIVE);
	C2_PRE(slot_id < session->s_nr_slots);

	slot = session->s_slot_table[slot_id];
	C2_ASSERT(slot != NULL);

	item->ri_service_id = session->s_conn->c_service_id;
	item->ri_sender_id = session->s_conn->c_sender_id;
	item->ri_session_id = session->s_session_id;
	item->ri_slot_id = slot_id;

	c2_list_link_init(&item->ri_slot_link);
	c2_list_add(&slot->ss_ready_list, &item->ri_slot_link);
	c2_chan_broadcast(&slot->ss_chan);
}

/**
   Called when there is signal on c2_rpc_snd_slot::ss_chan
 */
void c2_rpc_snd_slot_state_changed(struct c2_clink	*clink)
{
	struct c2_chan		*chan;
	struct c2_rpc_snd_slot	*slot;
	struct c2_rpc_item	*item;

	chan = clink->cl_chan;
	C2_ASSERT(chan != NULL);

	slot = container_of(chan, struct c2_rpc_snd_slot, ss_chan);
	C2_ASSERT(slot != NULL);
	C2_ASSERT(c2_mutex_is_locked(&slot->ss_session->s_mutex));

	if (!c2_rpc_snd_slot_is_busy(slot) &&
		!c2_list_is_empty(&slot->ss_ready_list)) {
		/*
		 * obtain item at the head of the list
		 */
		item = c2_list_entry(slot->ss_ready_list.l_head,
					struct c2_rpc_item, ri_slot_link);
		C2_ASSERT(item != NULL);
		c2_list_del(&item->ri_slot_link);

		/*
		 * item should already filled with sender_id, session_id and
		 * slot id
		 */
		C2_ASSERT(item->ri_sender_id != SENDER_ID_INVALID &&
				item->ri_session_id <= SESSION_ID_MAX &&
				item->ri_slot_id != SLOT_ID_INVALID);

		item->ri_slot_generation = slot->ss_generation;
		item->ri_verno = slot->ss_verno;
		c2_rpc_snd_slot_mark_busy(slot);
		slot->ss_sent_item = item;

		/*
		 * XXX TODO confirm that this doesn't lead to self deadlock.
		 */
		c2_rpc_submit(item->ri_service_id, NULL, item, item->ri_prio,
				&item->ri_deadline);
	}
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

