/* Declarations of functions that are private to rpc-layer */

#ifndef _COLIBRI_RPC_SESSION_INT_H
#define _COLIBRI_RPC_SESSION_INT_H

#include "cob/cob.h"
#include "rpc/session.h"
#include "rpc/session_fops.h"
#include "dtm/verno.h"

extern const char *C2_RPC_SLOT_TABLE_NAME;

/**
    Key into c2_rpc_in_core_slot_table.
    Receiver side.

    session_id is not unique on receiver.
    For each sender_id, receiver has session 0 associated with it.
    Hence snd_id (sender_id) is also a part of key.
 */

struct c2_rpc_slot_table_key {
	uint64_t	stk_sender_id;
	uint64_t	stk_session_id;
	uint32_t	stk_slot_id;
	uint64_t	stk_slot_generation;
};

struct c2_rpc_slot_table_value {
	struct c2_verno	stv_verno;
	uint64_t	stv_reply_len;
	char		stv_data[0];
};

extern const struct c2_table_ops c2_rpc_slot_table_ops;

/**
   Copies all session related information from @req to @reply.
   Caches reply item @reply in persistent cache in the context of @tx.
 */
extern int c2_rpc_session_reply_prepare(struct c2_rpc_item *req,
				struct c2_rpc_item *reply,
                                struct c2_db_tx *tx);

extern int c2_rpc_session_module_init(void);
extern void c2_rpc_session_module_fini(void);


/**
   Temporary mechanism to cache reply items.
   We don't yet have methods to serialize rpc-item in a buffer, so as to be
   able to store them in db table.
 */
extern struct c2_list          c2_reply_cache_list;

#endif

