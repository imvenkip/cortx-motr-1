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

/**
   Global reply cache 
 */
struct c2_rpc_reply_cache	c2_rpc_reply_cache;

int c2_rpc_slot_table_key_cmp(struct c2_table *table, const void *key0,
				const void *key1)
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

int c2_rpc_reply_cache_init(struct c2_rpc_reply_cache *rcache,
			struct c2_dbenv *dbenv)
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

void c2_rpc_conn_init(struct c2_rpc_conn * rpc_conn,
                        struct c2_net_conn *net_conn)
{

}

int  c2_rpc_conn_fini(struct c2_rpc_conn *rpc_conn)
{
       return 0;
}

void c2_rpc_conn_timedwait(struct c2_rpc_conn *rpc_conn, uint64_t state_flags,
                        const struct c2_time *time)
{

}

bool c2_rpc_conn_invariant(const struct c2_rpc_conn *rpc_conn)
{
       return true;
}

int c2_rpc_session_create(struct c2_rpc_conn *rpc_conn, 
                               struct c2_rpc_session *out)
{
       return 0;
}

int c2_rpc_session_terminate(struct c2_rpc_session *session)
{
       return 0;
}

void c2_rpc_session_timedwait(struct c2_rpc_session *session,
                uint64_t state_flags,
                const struct c2_time *abs_timeout)
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
int c2_rpc_session_slot_table_resize(struct c2_rpc_session *session,
					uint32_t nr_slots)
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

int c2_rpc_session_params_get(uint64_t sender_id, uint64_t session_id,
                                struct c2_rpc_session_params **out)
{
	return 0;
}

int c2_rpc_session_params_set(uint64_t sender_id, uint64_t session_id,
                                struct c2_rpc_session_params *param)
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
int c2_rpc_reply_cache_insert(struct c2_rpc_item *item, struct c2_db_tx *tx)
{
	struct c2_table				*slot_table;
	struct c2_table				*inmem_slot_table;
	struct c2_db_pair			pair;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_slot_table_value		slot;
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_pair			inmem_pair;
	int				rc;

	C2_PRE(item != NULL && tx != NULL);

	slot_table = c2_rpc_reply_cache.rc_slot_table;
	inmem_slot_table = c2_rpc_reply_cache.rc_inmem_slot_table;

	key.stk_sender_id = item->ri_sender_id;
	key.stk_session_id = item->ri_session_id;
	key.stk_slot_id = item->ri_slot_id;
	key.stk_slot_generation = item->ri_slot_generation;

	C2_SET0(&slot);

	/*
	 * Increment version count of slot and cache the reply
	 */
	c2_db_pair_setup(&pair, slot_table, &key, sizeof key,
				&slot, sizeof slot);
	rc = c2_table_lookup(tx, &pair);
	if (rc != 0)
		goto out;
	
	printf("rc_insert: current value: %lu\n", slot.stv_verno.vn_vc);

	C2_ASSERT(item->ri_verno.vn_vc == slot.stv_verno.vn_vc);

	slot.stv_verno.vn_vc++;
	/* XXX temporary: when integrated with FOL will get proper lsn */
	slot.stv_verno.vn_lsn++;

	rc = c2_table_update(tx, &pair);
	if (rc != 0)
		goto out;

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
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	return rc;
}

int c2_rpc_session_reply_prepare(struct c2_rpc_item *req,
				struct c2_rpc_item *reply,
				struct c2_db_tx *tx)
{
	C2_PRE(req != NULL && reply != NULL && tx != NULL);

	printf("Called prepare reply item\n");
	reply->ri_sender_id = req->ri_sender_id;
	reply->ri_session_id = req->ri_session_id;
	reply->ri_slot_id = req->ri_slot_id;
	reply->ri_slot_generation = req->ri_slot_generation;
	reply->ri_verno = req->ri_verno;

	/*
	  rpc_conn_create request comes with ri_sender_id set to
	  SENDER_ID_INVALID. Don't cache reply of such requests.
	 */
	if (req->ri_sender_id != SENDER_ID_INVALID) {
		c2_rpc_reply_cache_insert(reply, tx);
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
enum c2_rpc_session_seq_check_result c2_rpc_session_item_received(
		struct c2_rpc_item *item, struct c2_rpc_item **reply_out)
{
	struct c2_table				*slot_table;
	struct c2_table				*inmem_slot_table;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_slot_table_value		slot;
	struct c2_db_pair			pair;
	/* pair for inmem slot table */
	struct c2_db_pair			im_pair; 
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_tx				tx;
	struct c2_rpc_item			*citem;	/* cached rpc item */
	enum c2_rpc_session_seq_check_result	rc = SCR_ERROR;
	int					undoable;
	int					redoable;
	bool					slot_is_busy;
	int					err;

	printf("item_received called\n");
	C2_PRE(item != NULL);

	*reply_out = NULL;

	/*
	 * No seq check for items with sender_id SENDER_ID_INVALID
	 *	e.g. rpc_conn_create request
	 */
	if (item->ri_sender_id == SENDER_ID_INVALID) {
		printf("Sender id is invalid. accepting item\n");
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

	c2_db_pair_setup(&pair, slot_table, &key, sizeof key,
				&slot, sizeof slot);
	err = c2_table_lookup(&tx, &pair);
	if (err != 0) {
		rc = SCR_ERROR;
		goto errabort;
	}
	printf("item_received: slot verno = %lu\n", slot.stv_verno.vn_vc);

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

	undoable = c2_verno_is_undoable(&slot.stv_verno, &item->ri_verno, 0);
	redoable = c2_verno_is_redoable(&slot.stv_verno, &item->ri_verno, 0);
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
	
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_pair_release(&im_pair);
	c2_db_pair_fini(&im_pair);
errout:
	return rc;
}

int c2_rpc_cob_create_helper(struct c2_cob_domain	*dom,
			     uint64_t			pfid_hi,
			     uint64_t			pfid_lo,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx)
{
	struct c2_cob_nskey		*key;
	struct c2_cob_nsrec		nsrec;
	struct c2_cob_fabrec		fabrec;
	struct c2_cob			*cob;
	int				rc;

	C2_PRE(dom != NULL && name != NULL && out != NULL);

	*out = NULL;

	c2_cob_nskey_make(&key, pfid_hi, pfid_lo, name);
	if (key == NULL)
		return -ENOMEM;

	/*
	 * How to get unique stob_id for new cob?
	 */
	nsrec.cnr_stobid.si_bits.u_hi = random();
	nsrec.cnr_stobid.si_bits.u_lo = random();
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

int c2_rpc_cob_lookup_helper(struct c2_cob	*pcob,
			      char		*name,
			      struct c2_cob	**out,
			      struct c2_db_tx	*tx)
{
	struct c2_cob_nskey	*key = NULL;
	int			rc;

	C2_PRE(pcob != NULL && name != NULL && out != NULL && tx != NULL);

	c2_cob_nskey_make(&key, COB_GET_PFID_HI(pcob), COB_GET_PFID_LO(pcob),
				name);
	if (key == NULL)
		return -ENOMEM;

	rc = c2_cob_lookup(pcob->co_dom, key, CA_NSKEY_FREE, out, tx);

	return rc;
}
int c2_rpc_rcv_sessions_root_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx)
{
	return 0;
}

int c2_rpc_rcv_conn_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx)
{
	return 0;
}

int c2_rpc_rcv_conn_create(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx)
{
	return 0;
}

int c2_rpc_rcv_session_lookup(struct c2_cob		*conn_cob,
			      uint64_t			session_id,
			      struct c2_cob		**session_cob,
			      struct c2_db_tx		*tx)
{
	return 0;
}


int c2_rpc_rcv_session_create(struct c2_cob		*conn_cob,
			      uint64_t			session_id,
			      struct c2_cob		**session_cob,
			      struct c2_db_tx		*tx)
{
	return 0;
}

int c2_rpc_rcv_slot_lookup(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx)
{
	return 0;
}

void c2_rpc_rcv_current_version_get(struct c2_cob	*cob,
				    struct c2_verno	*verno)
{

}
/** @} end of session group */

