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
//#include "rpc/session.ff"
#include "rpc/session_int.h"
#include "db/db.h"
#include "dtm/verno.h"

const char *C2_RPC_SLOT_TABLE_NAME = "slot_table";
const char *C2_RPC_INMEM_SLOT_TABLE_NAME = "inmem_slot_table";

struct c2_rpcmachine g_rpcmachine;

struct c2_stob_id c2_root_stob_id = {
        .si_bits = {1, 1}
};

static void session_fields_init(struct c2_rpc_session	*session,
				struct c2_rpc_conn	*conn,
				uint32_t		nr_slots);

static void c2_rpc_snd_slot_init(struct c2_rpc_snd_slot *slot);
static void c2_rpc_session_zero_attach(struct c2_rpc_conn *conn);

/**
   Global reply cache 
 */
struct c2_rpc_reply_cache	c2_rpc_reply_cache;

int c2_rpc_slot_table_key_cmp(struct c2_table	*table,
			      const void	*key0,
			      const void	*key1)
{
	const struct c2_rpc_slot_table_key *stk0 = key0;
	const struct c2_rpc_slot_table_key *stk1 = key1;
	int rc;

	rc = C2_3WAY(stk0->stk_sender_id, stk1->stk_sender_id);
	if (rc == 0) {
		rc = C2_3WAY(stk0->stk_session_id, stk1->stk_session_id);
		if (rc == 0) {
			rc = C2_3WAY(stk0->stk_slot_id, stk1->stk_slot_id);
			if (rc == 0) {
				rc = C2_3WAY(stk0->stk_slot_generation,
						stk1->stk_slot_generation);
			}
		}
	}
	return rc;
}

const struct c2_table_ops c2_rpc_slot_table_ops = {
        .to = {
                [TO_KEY] = {
                        .max_size = sizeof(struct c2_rpc_slot_table_key)
                },
                [TO_REC] = {
                        .max_size = ~0
                }
        },
        .key_cmp = c2_rpc_slot_table_key_cmp
};

int c2_rpc_reply_cache_init(struct c2_rpc_reply_cache	*rcache,
			    struct c2_dbenv 		*dbenv)
{
	int	rc;

	C2_PRE(dbenv != NULL);

	rcache->rc_dbenv = dbenv;

	C2_ALLOC_PTR(rcache->rc_slot_table);
	C2_ALLOC_PTR(rcache->rc_inmem_slot_table);

	C2_ASSERT(rcache->rc_slot_table != NULL &&
			rcache->rc_inmem_slot_table != NULL);

	c2_list_init(&rcache->rc_item_list);

	rc = c2_table_init(rcache->rc_slot_table, rcache->rc_dbenv,
			C2_RPC_SLOT_TABLE_NAME, 0, &c2_rpc_slot_table_ops);
	if (rc != 0)
		goto errout;

	/*
	  XXX find out how to create an in memory c2_table
	 */
	rc = c2_table_init(rcache->rc_inmem_slot_table, rcache->rc_dbenv,
			C2_RPC_INMEM_SLOT_TABLE_NAME, 0, &c2_rpc_slot_table_ops);
	if (rc != 0) {
		c2_table_fini(rcache->rc_slot_table);
		goto errout;
	}
	return 0;		/* success */

errout:
	if (rcache->rc_slot_table != NULL)
		c2_free(rcache->rc_slot_table);
	if (rcache->rc_inmem_slot_table != NULL)
		c2_free(rcache->rc_inmem_slot_table);
	return rc;
}

void c2_rpc_reply_cache_fini(struct c2_rpc_reply_cache *rcache)
{
	C2_PRE(rcache != NULL && rcache->rc_dbenv != NULL &&
			rcache->rc_slot_table != NULL &&
			rcache->rc_inmem_slot_table != NULL);

	c2_table_fini(rcache->rc_inmem_slot_table);
	c2_table_fini(rcache->rc_slot_table);
}

int c2_rpc_session_module_init(void)
{
	int		rc;

	rc = c2_rpc_session_fop_init();
	return rc;
}

void c2_rpc_session_module_fini(void)
{
	c2_rpc_session_fop_fini();
}

void c2_rpc_conn_search(struct c2_rpcmachine    *machine,
                        uint64_t                sender_id,
                        struct c2_rpc_conn      **out)
{
	struct c2_rpc_conn	*conn;

	*out = NULL;

	c2_list_for_each_entry(&machine->cr_rpc_conn_list, conn,
				struct c2_rpc_conn, c_link) {
		if (conn->c_sender_id == sender_id) {
			*out = conn;
			break;
		}
	}
}

void c2_rpc_session_search(struct c2_rpc_conn           *conn,
                           uint64_t                     session_id,
                           struct c2_rpc_session        **out)
{
	struct c2_rpc_session		*session;

	C2_PRE(c2_mutex_is_locked(&conn->c_mutex));
	*out = NULL;

	c2_list_for_each_entry(&conn->c_sessions, session,
				struct c2_rpc_session, s_link) {
		if (session->s_session_id == session_id) {
			*out = session;
			break;
		}
	}
}

int c2_rpc_conn_init(struct c2_rpc_conn		*conn,
		     struct c2_service_id	*svc_id)
{
	struct c2_fop			*fop;
	struct c2_rpc_conn_create	*fop_cc;
	struct c2_rpc_item		*item;
	struct c2_time			deadline;
	int				rc;

	C2_PRE(conn != NULL);

	printf("conn_create: called\n");
	C2_SET0(conn);
	/* Deprecated: service_id */
	conn->c_service_id = svc_id;
	conn->c_sender_id = SENDER_ID_INVALID;
	c2_list_init(&conn->c_sessions);
	conn->c_nr_sessions = 0;
	c2_chan_init(&conn->c_chan);
	c2_mutex_init(&conn->c_mutex);
	/*
	 * XXX temporary:
	 * TODO: Find out how to get reference to rpcmachine from rpc-core
	 */
	conn->c_rpcmachine = &g_rpcmachine;

	fop = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;
	conn->c_private = fop;

	fop_cc = c2_fop_data(fop);

	C2_ASSERT(fop_cc != NULL);

	/*
	 * Receiver will copy this cookie in conn_create reply
	 */
	fop_cc->rcc_cookie = (uint64_t)conn;

	item = c2_fop_to_rpc_item(fop);

	/*
	 * Send conn_create request "out of any session"
	 */
	item->ri_sender_id = SENDER_ID_INVALID;
	item->ri_session_id = SESSION_ID_NOSESSION;

	C2_SET0(&deadline);
	rc = c2_rpc_submit(NULL, item, C2_RPC_ITEM_PRIO_MAX, &deadline);

	if (rc == 0)
		conn->c_state = CS_CONN_INITIALIZING;
	else
		conn->c_state = CS_CONN_INIT_FAILED;

	printf("conn_create: finished %d\n", rc);
	return rc;
}

void c2_rpc_conn_create_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_conn_create_rep	*fop_ccr;
	struct c2_rpc_conn		*conn;

	C2_PRE(fop != NULL);

	printf("conn_create_reply_received\n");
	fop_ccr = c2_fop_data(fop);

	C2_ASSERT(fop_ccr != NULL);

	conn = (struct c2_rpc_conn *)fop_ccr->rccr_cookie;

	c2_mutex_lock(&conn->c_mutex);

	C2_ASSERT(conn != NULL);
	C2_ASSERT(conn->c_state == CS_CONN_INITIALIZING &&
			conn->c_private != NULL);

	if (fop_ccr->rccr_rc != 0) {
		/*
		 * Receiver has reported conn create failure
		 */
		conn->c_state = CS_CONN_INIT_FAILED;
		conn->c_sender_id = SENDER_ID_INVALID;
	} else {
		conn->c_sender_id = fop_ccr->rccr_snd_id;
		c2_rpc_session_zero_attach(conn);
		conn->c_state = CS_CONN_ACTIVE;
		c2_list_add(&conn->c_rpcmachine->cr_rpc_conn_list,
				&conn->c_link);
	}
	c2_fop_free(conn->c_private);
	conn->c_private = NULL;
	c2_fop_free(fop);

	c2_mutex_unlock(&conn->c_mutex);
	C2_ASSERT(conn->c_state == CS_CONN_INIT_FAILED ||
			conn->c_state == CS_CONN_ACTIVE);
	c2_chan_broadcast(&conn->c_chan);
	printf("conn_create_reply_received: finished\n");
}
static void c2_rpc_session_zero_attach(struct c2_rpc_conn *conn)
{
	struct c2_rpc_session   *session;

	printf("Creating session 0\n");
	C2_ASSERT(conn != NULL && conn->c_state == CS_CONN_INITIALIZING);
	C2_ASSERT(c2_mutex_is_locked(&conn->c_mutex));

	C2_ALLOC_PTR(session);
	C2_SET0(session);

	session_fields_init(session, conn, 1);
	session->s_session_id = SESSION_0;
	session->s_state = SESSION_ALIVE;

	c2_list_add(&conn->c_sessions, &session->s_link);
	printf("Session zero attached\n");
}
static void c2_rpc_snd_slot_init(struct c2_rpc_snd_slot *slot)
{
        C2_SET0(slot);
        slot->ss_flags = SLOT_IN_USE;
        c2_list_init(&slot->ss_ready_list);
        c2_list_init(&slot->ss_replay_list);
}

int c2_rpc_conn_terminate(struct c2_rpc_conn *conn)
{
	return 0;
}

void c2_rpc_conn_terminate_reply_received(struct c2_fop *fop)
{
}

int  c2_rpc_conn_fini(struct c2_rpc_conn *rpc_conn)
{
       return 0;
}

void c2_rpc_conn_timedwait(struct c2_rpc_conn	*rpc_conn,
			   uint64_t		state_flags,
                           const struct c2_time	*time)
{

}

bool c2_rpc_conn_invariant(const struct c2_rpc_conn *rpc_conn)
{
       return true;
}

static void session_fields_init(struct c2_rpc_session	*session,
				struct c2_rpc_conn	*conn,
				uint32_t		nr_slots)
{
	int	i;

	C2_PRE(session != NULL && nr_slots >= 1);

	c2_list_link_init(&session->s_link);
	session->s_session_id = SESSION_ID_INVALID;
	session->s_conn = conn;
	c2_chan_init(&session->s_chan);
	c2_mutex_init(&session->s_mutex);
	session->s_nr_slots = nr_slots;
	session->s_slot_table_capacity = nr_slots;
	C2_ALLOC_ARR(session->s_slot_table, nr_slots);

	C2_ASSERT(session->s_slot_table != NULL);

	for (i = 0; i < nr_slots; i++) {
		C2_ALLOC_PTR(session->s_slot_table[i]);
		c2_rpc_snd_slot_init(session->s_slot_table[0]);
	}
}
int c2_rpc_session_create(struct c2_rpc_session	*session,
			  struct c2_rpc_conn	*conn)
{
	struct c2_fop			*fop;
	struct c2_rpc_session_create	*fop_sc;
	struct c2_rpc_item		*item;
	struct c2_time			deadline;
	int				rc;

	C2_PRE(conn != NULL && session != NULL);
	C2_PRE(conn->c_state == CS_CONN_ACTIVE &&
		session->s_state == SESSION_UNINITIALIZED);

	printf("session_Create: session object %p\n", session);
	session_fields_init(session, conn, DEFAULT_SLOT_COUNT);

	fop = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;

	fop_sc = c2_fop_data(fop);
	C2_ASSERT(fop_sc != NULL);

	fop_sc->rsc_snd_id = conn->c_sender_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);

	item->ri_sender_id = conn->c_sender_id;
	item->ri_session_id = SESSION_0;
	item->ri_slot_id = 0;
	item->ri_slot_generation = 0;
	/* item->ri_verno should be filled in c2_rpc_item_prepare() */

	C2_SET0(&deadline);
	rc = c2_rpc_submit(NULL, item, C2_RPC_ITEM_PRIO_MAX, &deadline);

	if (rc != 0) {
		session->s_state = SESSION_CREATE_FAILED;
	} else {
		session->s_state = SESSION_CREATING;
	}
	c2_mutex_lock(&conn->c_mutex);
	c2_list_add(&conn->c_sessions, &session->s_link);
	conn->c_nr_sessions++;
	c2_mutex_unlock(&conn->c_mutex);

	c2_chan_broadcast(&session->s_chan);

	return rc;
}

void c2_rpc_session_create_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_session_create_rep	*fop_scr;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session = NULL;
	struct c2_rpc_session			*s;
	uint64_t				sender_id;
	uint64_t				session_id;

	printf("session_create_reply_received: called\n");
	C2_PRE(fop != NULL);

	fop_scr = c2_fop_data(fop);
	C2_ASSERT(fop_scr != NULL);

	sender_id = fop_scr->rscr_sender_id;
	session_id = fop_scr->rscr_session_id;

	c2_rpc_conn_search(&g_rpcmachine, sender_id, &conn);
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	/*
	 * For a c2_rpc_conn
	 * There can be only session create in progress at any given point
	 */
	c2_list_for_each_entry(&conn->c_sessions, s,
				struct c2_rpc_session, s_link) {
		if (s->s_state == SESSION_CREATING) {
			session = s;
			break;
		}
	}

	/*
	 * Duplicate reply message will be filtered by
	 * c2_rpc_session_reply_recieved().
	 * If this function is called then there must be ONE session
	 * with state == CREATING. If not then it is a bug.
	 */
	C2_ASSERT(session != NULL);

	printf("Found session object %p\n", session);
	c2_mutex_unlock(&conn->c_mutex);

	if (fop_scr->rscr_rc != 0) {
		session->s_state = SESSION_CREATE_FAILED;
	} else {
		printf("setting session id to %lu\n", fop_scr->rscr_session_id);
		session->s_session_id = fop_scr->rscr_session_id;
		session->s_state = SESSION_ALIVE;
	}
	c2_chan_broadcast(&session->s_chan);

	C2_ASSERT(session->s_state == SESSION_ALIVE ||
			session->s_state == SESSION_CREATE_FAILED);
	return;
}

int c2_rpc_session_terminate(struct c2_rpc_session *session)
{
	struct c2_fop			*fop;
	struct c2_rpc_session_destroy	*fop_sd;
	struct c2_rpc_item		*item;
	struct c2_time			deadline;
	int				i;
	int				rc;

	C2_PRE(session != NULL && session->s_conn != NULL);

	printf("Session terminate called for %lu\n", session->s_session_id);

	c2_mutex_lock(&session->s_mutex);

	if (session->s_state != SESSION_ALIVE) {
		printf("attempt to terminate non-ALIVE session\n");
		rc = -EINVAL;
		goto out;
	}

	for (i = 0; i < session->s_nr_slots; i++) {
		if (c2_rpc_snd_slot_is_busy(session->s_slot_table[i])) {
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

	fop = c2_fop_alloc(&c2_rpc_session_destroy_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	fop_sd = c2_fop_data(fop);

	C2_ASSERT(fop_sd != NULL);

	fop_sd->rsd_sender_id = session->s_conn->c_sender_id;
	fop_sd->rsd_session_id = session->s_session_id;

	item = c2_fop_to_rpc_item(fop);

	C2_ASSERT(item != NULL);

	item->ri_sender_id = session->s_conn->c_sender_id;
	item->ri_session_id = SESSION_0;
	item->ri_slot_id = 0;
	item->ri_slot_generation = 0;
	/* item->ri_verno should be filled in c2_rpc_item_prepare() */

	C2_SET0(&deadline);
	rc = c2_rpc_submit(NULL, item, C2_RPC_ITEM_PRIO_MAX, &deadline);

	if (rc != 0) {
		session->s_state = SESSION_ALIVE;
	} else {
		session->s_state = SESSION_TERMINATING;
	}

out:
	c2_mutex_unlock(&session->s_mutex);
	c2_chan_broadcast(&session->s_chan);

	printf("session_terminate: complete %d\n", rc);
	return rc;
}

void c2_rpc_session_terminate_reply_received(struct c2_fop *fop)
{
	struct c2_rpc_session_destroy_rep	*fop_sdr;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session;
	uint64_t				sender_id;
	uint64_t				session_id;

	C2_PRE(fop != NULL);

	fop_sdr = c2_fop_data(fop);
	C2_ASSERT(fop_sdr != NULL);

	sender_id = fop_sdr->rsdr_sender_id;
	session_id = fop_sdr->rsdr_session_id;		

	printf("session_term_reply: %lu %lu %d\n", sender_id, session_id,
			fop_sdr->rsdr_rc);
	C2_ASSERT(sender_id != SENDER_ID_INVALID &&
			session_id != SESSION_ID_INVALID &&
			session_id != 0);

	c2_rpc_conn_search(&g_rpcmachine, sender_id, &conn);
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);

	c2_rpc_session_search(conn, session_id, &session);

	C2_ASSERT(session != NULL && session->s_state == SESSION_TERMINATING);
	C2_ASSERT(conn->c_nr_sessions > 0);

	c2_mutex_lock(&session->s_mutex);

	if (fop_sdr->rsdr_rc == 0) {
		session->s_state = SESSION_TERMINATED;
		conn->c_nr_sessions--;
		c2_list_del(&session->s_link);
	} else {
		session->s_state = SESSION_ALIVE;
	}

	c2_mutex_unlock(&session->s_mutex);
	c2_mutex_unlock(&conn->c_mutex);
	c2_chan_broadcast(&session->s_chan);
	printf("Session terminate reply received finished\n");
}

void c2_rpc_session_timedwait(struct c2_rpc_session	*session,
			      uint64_t			state_flags,
			      const struct c2_time	*abs_timeout)
{

}

bool c2_rpc_session_invariant(const struct c2_rpc_session *session)
{
       return true;
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

   Assumption: c2_rpc_item has a field giving service_id of
                destination service.
 */
int c2_rpc_session_item_prepare(struct c2_rpc_item *rpc_item)
{
	return 0;
}


/**
   Inform session module that a reply item is received.

   rpc-core can call this function when it receives an item. session module
   can then mark corresponding slot "unbusy", move the item to replay list etc.
 */
void c2_rpc_session_reply_item_received(struct c2_rpc_item *rpc_item)
{

}

/**
   Start session recovery.

   @pre c2_rpc_session->s_state == SESSION_ALIVE
   @post c2_rpc_session->s_state == SESSION_RECOVERING
   
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
   
   In the absence of stable transaction APIs, we're using
   db5 transaction apis as place holder
    
   @note that for certain fop types eg. READ, reply cache does not
   contain the whole reply state, because it is too large. Instead
   the cache contains the pointer (block address) to the location
   of the data.
 */
int c2_rpc_reply_cache_insert(struct c2_rpc_item	*item,
			      struct c2_cob_domain	*dom,
			      struct c2_db_tx		*tx)
{
	struct c2_table				*inmem_slot_table;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_pair			inmem_pair;
	struct c2_cob				*slot_cob;
	int				rc;

	C2_PRE(item != NULL && tx != NULL);

	inmem_slot_table = c2_rpc_reply_cache.rc_inmem_slot_table;

	key.stk_sender_id = item->ri_sender_id;
	key.stk_session_id = item->ri_session_id;
	key.stk_slot_id = item->ri_slot_id;
	key.stk_slot_generation = item->ri_slot_generation;

	/*
	 * Update version of cob representing slot
	 */
	rc = c2_rpc_rcv_slot_lookup_by_item(dom, item, &slot_cob, tx);
	if (rc != 0)
		goto out;

	printf("cache_insert: current slot ver: %lu\n",
			slot_cob->co_fabrec.cfb_version.vn_vc);

	/*
	 * When integrated with fol assign proper lsn 
	 * instead of just increamenting it
	 */
	slot_cob->co_fabrec.cfb_version.vn_lsn++;
	slot_cob->co_fabrec.cfb_version.vn_vc++;

	rc = c2_cob_update(slot_cob, NULL, &slot_cob->co_fabrec, tx);
	if (rc != 0) {
		printf("cache_insert: failed to update cob %d\n", rc);
		goto out;
	}

	c2_cob_put(slot_cob);

	/*
	 * Mark in core slot as "not busy"
	 */
	c2_db_pair_setup(&inmem_pair, inmem_slot_table, &key, sizeof key,
			&inmem_slot, sizeof inmem_slot);
	rc = c2_table_lookup(tx, &inmem_pair);
	if (rc != 0)
		goto out1;

	C2_ASSERT(inmem_slot.istv_busy);

	inmem_slot.istv_busy = false;

	rc = c2_table_update(tx, &inmem_pair);
	if (rc != 0)
		goto out1;

	c2_list_add(&c2_rpc_reply_cache.rc_item_list, &(item->ri_rc_link));

out1:
	c2_db_pair_release(&inmem_pair);
	c2_db_pair_fini(&inmem_pair);

out:
	return rc;
}

int c2_rpc_session_reply_prepare(struct c2_rpc_item	*req,
				 struct c2_rpc_item	*reply,
				 struct c2_cob_domain	*dom,
				 struct c2_db_tx	*tx)
{
	C2_PRE(req != NULL && reply != NULL && tx != NULL);

	printf("Called prepare reply item\n");
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
		c2_rpc_reply_cache_insert(reply, dom, tx);
	} else {
		printf("it's conn create/terminate req. not caching reply\n");
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
			     struct c2_cob_domain	*dom,
			     struct c2_rpc_item 	**reply_out)
{
	struct c2_table				*slot_table;
	struct c2_table				*inmem_slot_table;
	struct c2_rpc_slot_table_key		key;
	/* pair for inmem slot table */
	struct c2_db_pair			im_pair; 
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_tx				tx;
	struct c2_rpc_item			*citem;	/* cached rpc item */
	struct c2_cob				*slot_cob = NULL;
	enum c2_rpc_session_seq_check_result	rc = SCR_ERROR;
	int					undoable;
	int					redoable;
	bool					slot_is_busy;
	int					err;

	printf("item_received called\n");
	C2_PRE(item != NULL);

	*reply_out = NULL;

	/*
	 * No seq check for items with session_id SESSION_ID_NOSESSION
	 *	e.g. rpc_conn_create request
	 */
	if (item->ri_session_id == SESSION_ID_NOSESSION) {
		printf("Item out of session. Accepting item\n");
		return SCR_ACCEPT_ITEM;
	}

	slot_table = c2_rpc_reply_cache.rc_slot_table;
	inmem_slot_table = c2_rpc_reply_cache.rc_inmem_slot_table;

	/*
	 * Read persistent slot table value
	 */
	key.stk_sender_id = item->ri_sender_id;
	key.stk_session_id = item->ri_session_id;
	key.stk_slot_id = item->ri_slot_id;
	key.stk_slot_generation = item->ri_slot_generation;

	err = c2_db_tx_init(&tx, c2_rpc_reply_cache.rc_dbenv, 0);
	if (err != 0) {
		rc = SCR_ERROR;
		goto errout;
	}

	/*
	 * Read in memory slot table value
	 */
	c2_db_pair_setup(&im_pair, inmem_slot_table, &key, sizeof key,
				&inmem_slot, sizeof inmem_slot);
	err = c2_table_lookup(&tx, &im_pair);
	if (err != 0) {
		printf("err occured while reading inmem_slot_tbl %d\n", err);
		rc = SCR_SESSION_INVALID;
		goto errabort;
	}
	printf("item_received: slot.busy %d\n", (int)inmem_slot.istv_busy);

	err = c2_rpc_rcv_slot_lookup_by_item(dom, item, &slot_cob, &tx);
	if (err != 0) {
		rc = SCR_ERROR;
		goto errabort;
	}

	printf("Current slot verno: [%lu:%lu]\n", slot_cob->co_fabrec.cfb_version.vn_lsn,
			slot_cob->co_fabrec.cfb_version.vn_vc);
	printf("Current item verno: [%lu:%lu]\n", item->ri_verno.vn_lsn,
			item->ri_verno.vn_vc);

	C2_ASSERT(slot_cob->co_valid & CA_FABREC);

	undoable = c2_verno_is_undoable(&slot_cob->co_fabrec.cfb_version, &item->ri_verno, 0);
	redoable = c2_verno_is_redoable(&slot_cob->co_fabrec.cfb_version, &item->ri_verno, 0);
	slot_is_busy = inmem_slot.istv_busy;

	if (undoable == 0) {
		bool		found = false;

		/*
		 * Search reply in reply cache list
		 */
		c2_list_for_each_entry(&c2_rpc_reply_cache.rc_item_list,
		    item, struct c2_rpc_item, ri_rc_link) {
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
		 * XXX Following assert is disabled for testing, but it is valid one
		 * and should be enabled
		 */
		//C2_ASSERT(found);
		printf("item_received: reply fetched from reply cache\n");
	} else {
		if (redoable == 0) {
			if (slot_is_busy) {
				printf("request already in progress\n");
				rc = SCR_IGNORE_ITEM;
			} else {
				/* Mark slot as busy and accept the item */
				inmem_slot.istv_busy = true;
				err = c2_table_update(&tx, &im_pair);
				if (err != 0)
					goto errabort;
				rc = SCR_ACCEPT_ITEM;
				printf("item accepted\n");
			}
		} else if (redoable == -EALREADY || redoable == -EAGAIN) {
			printf("misordered entry\n");
			rc = SCR_SEND_ERROR_MISORDERED;
		}
	}

errabort:
	if (rc == SCR_ERROR || rc == SCR_SESSION_INVALID)
		c2_db_tx_abort(&tx);
	else
		c2_db_tx_commit(&tx);
	
	if (slot_cob != NULL)
		c2_cob_put(slot_cob);

	c2_db_pair_release(&im_pair);
	c2_db_pair_fini(&im_pair);
errout:
	return rc;
}

int global_slot_id_counter = 0;

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

	printf("create_helper called: %s\n", name);
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
	nsrec.cnr_stobid.si_bits.u_hi = ++global_slot_id_counter;
	nsrec.cnr_stobid.si_bits.u_lo = global_slot_id_counter;
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
	
	printf("cob_create: rc %d\n", rc);
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

	rc = c2_cob_lookup(dom, key, CA_NSKEY_FREE | CA_FABREC, out, tx);

	return rc;
}
int c2_rpc_rcv_sessions_root_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx)
{
	return c2_rpc_cob_lookup_helper(dom, NULL, "SESSIONS", out, tx);
}

int c2_rpc_rcv_conn_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx)
{
	struct c2_cob	*root_session_cob;
	char		name[20];
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
	char		name[20];
	int		rc;

	C2_PRE(dom != NULL && out != NULL);
	C2_PRE(sender_id != SENDER_ID_INVALID);

	sprintf(name, "SENDER_%lu", sender_id);
	*out = NULL;

	/*
	 * check whether sender_id already exists
	 */
	rc = c2_rpc_cob_lookup_helper(dom, NULL, "SESSIONS", &root_session_cob, tx);
	if (rc != 0)
		return rc;
	
	rc = c2_rpc_cob_lookup_helper(dom, root_session_cob, name, &conn_cob, tx);

	if (rc == 0) {
		rc = -EEXIST;
		c2_cob_put(conn_cob);
		c2_cob_put(root_session_cob);
		return rc;
	}

	if (rc != -ENOENT) {
		c2_cob_put(root_session_cob);
		return rc;
	}

	C2_ASSERT(rc == -ENOENT);

	/*
	 * Connection with @sender_id is not present. create it
	 */
	rc = c2_rpc_cob_create_helper(dom, root_session_cob, name, &conn_cob, tx);
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
	char		name[20];
	int		rc = 0;

	C2_PRE(conn_cob != NULL && session_id != SESSION_ID_INVALID &&
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
	char		name[20];
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
	char		name[20];
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
	
	C2_PRE(dom != NULL && item != NULL && slot_cob != NULL);

	C2_PRE(item->ri_sender_id != SENDER_ID_INVALID &&
		item->ri_session_id != SESSION_ID_INVALID &&
		item->ri_session_id != SESSION_ID_NOSESSION);

	printf("slot_lookup_by_item [%lu:%lu:%u]\n", item->ri_sender_id,
			item->ri_session_id, item->ri_slot_id);

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
	*cob = slot_cob;
	printf("read cob with : [%lu:%lu]\n", slot_cob->co_nsrec.cnr_stobid.si_bits.u_hi, 
			slot_cob->co_nsrec.cnr_stobid.si_bits.u_lo);
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

/** @} end of session group */

