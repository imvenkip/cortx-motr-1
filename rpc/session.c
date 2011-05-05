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

const char *C2_RPC_SLOT_TABLE_NAME = "slot_table";

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

void c2_rpc_session_ops_register(struct c2_rpc_session *session,
                                struct c2_rpc_session_ops *ops)
{
        C2_PRE(session != NULL && session->s_ops == NULL);
        session->s_ops = ops;
}

void c2_rpc_session_ops_unregister(struct c2_rpc_session *session)
{
        session->s_ops = NULL;
}

/**
   In core slot table stores attributes of slots which
   are not needed to be persistent.
   Key is same as c2_rpc_slot_table_key.
   Value is modified in transaction. So no explicit lock required.
 */
struct c2_rpc_in_core_slot_table_value {
	/** A request is being executed on this slot */
	bool		ics_busy;
};

/**
   Insert a reply item in reply cache and advance slot version.
   
   In the absence of stable transaction APIs, we're using
   db5 transaction apis as place holder
    
   @note that for certain fop types eg. READ, reply cache does not
   contain the whole reply state, because it is too large. Instead
   the cache contains the pointer (block address) to the location
   of the data.
 */
int c2_rpc_reply_cache_insert(struct c2_rpc_item *rpc_item, struct c2_db_tx *tx)
{
	return 0;
}

enum c2_rpc_session_seq_check_result {
	/** item is valid in sequence. accept it */
	RSSC_ACCEPT_ITEM,
	/** item is duplicate of request whose reply is cached in reply cache*/
	RSSC_RESEND_REPLY,
	/** Already received this item and its processing is in progress */
	RSSC_IGNORE_ITEM,
	/** Item is not in seq. send err msg to sender */
	RSSC_SEND_ERROR_MISORDERED,
	/** Invalid session or slot */
	RSSC_SESSION_INVALID
};

/**
   Checks whether received item is correct in sequence or not and suggests
   action to be taken.
   'reply_out' is valid only if return value is RESEND_REPLY.
 */
enum c2_rpc_session_seq_check_result c2_rpc_session_item_received(
		struct c2_rpc_item *rpc_item, struct c2_rpc_item **reply_out)
{
	return RSSC_ACCEPT_ITEM;
}

/**
   Receiver side SESSION_CREATE handler
 */
int c2_rpc_session_create_handler(struct c2_fom *fom)
{
	return 0;
}

/**
   Destroys all the information associated with the session on the receiver
   including reply cache entries.
 */
int c2_rpc_session_destroy_handler(struct c2_fom *fom)
{
	return 0;
}

int c2_rpc_session_create_rep_handler(struct c2_fom *fom)
{
	return 0;
}

int c2_rpc_session_destroy_rep_handler(struct c2_fom *fom)
{
	return 0;
}

int c2_rpc_conn_create_handler(struct c2_fom *fom)
{
	return 0;
}

int c2_rpc_conn_create_rep_handler(struct c2_fom *fom)
{
	return 0;
}

int c2_rpc_conn_terminate_handler(struct c2_fom *fom)
{
	return 0;
}

int c2_rpc_conn_terminate_rep_handler(struct c2_fom *fom)
{
	return 0;
}

/** @} end of session group */

