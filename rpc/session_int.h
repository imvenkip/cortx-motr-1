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
   Register session ops
   C2_PRE(session->s_ops == NULL)
   C2_POST(session->s_ops == ops)
 */
extern void c2_rpc_session_ops_register(struct c2_rpc_session *session,
				struct c2_rpc_session_ops *ops);
/**
   Unregisters session ops
   C2_PRE(session->s_ops != NULL)
   C2_POST(session->s_ops == NULL)
 */
extern void c2_rpc_session_ops_unregister(struct c2_rpc_session *session);

extern int c2_rpc_session_module_init(void);
extern void c2_rpc_session_module_fini(void);
#endif

