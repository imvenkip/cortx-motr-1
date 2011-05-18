/* Declarations of functions that are private to rpc-layer */

#ifndef _COLIBRI_RPC_SESSION_INT_H
#define _COLIBRI_RPC_SESSION_INT_H

#include "cob/cob.h"
#include "rpc/session.h"
#include "rpc/session_fops.h"
#include "dtm/verno.h"

enum {
	DEFAULT_SLOT_COUNT = 4
};

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

/**
   In core slot table stores attributes of slots which
   are not needed to be persistent.
   Key is same as c2_rpc_slot_table_key.
   Value is modified in transaction. So no explicit lock required.
 */
struct c2_rpc_inmem_slot_table_value {
        /** A request is being executed on this slot */
        bool            istv_busy;
};

extern const struct c2_table_ops c2_rpc_slot_table_ops;

/**
   Copies all session related information from @req to @reply.
   Caches reply item @reply in persistent cache in the context of @tx.
 */
extern int c2_rpc_session_reply_prepare(struct c2_rpc_item	*req,
					struct c2_rpc_item	*reply,
					struct c2_cob_domain	*dom,
                                	struct c2_db_tx		*tx);

extern int c2_rpc_session_module_init(void);
extern void c2_rpc_session_module_fini(void);

/**
   Temporary reply cache arrangement until we implement reply cache
   in cob framework.
   There will be only one global object of this type.
 */
struct c2_rpc_reply_cache {
	/** dbenv to which slot table belong */
	struct c2_dbenv		*rc_dbenv;
	/** persistent slot table
	    XXX currently we don't store reply items in this table
	 */
	struct c2_table		*rc_slot_table;
	/** In memory slot table */
	struct c2_table		*rc_inmem_slot_table;
	/**
	   Temporary mechanism to cache reply items.
	   We don't yet have methods to serialize rpc-item in a buffer, so as to be
	   able to store them in db table.
 	*/
	struct c2_list          rc_item_list;
};
extern struct c2_rpc_reply_cache c2_rpc_reply_cache;
int c2_rpc_reply_cache_init(struct c2_rpc_reply_cache *rcache,
				struct c2_dbenv *dbenv);
void c2_rpc_reply_cache_fini(struct c2_rpc_reply_cache *rcache);

enum c2_rpc_session_seq_check_result {
        /** item is valid in sequence. accept it */
        SCR_ACCEPT_ITEM,
        /** item is duplicate of request whose reply is cached in reply cache*/
        SCR_RESEND_REPLY,
        /** Already received this item and its processing is in progress */
        SCR_IGNORE_ITEM,
        /** Item is not in seq. send err msg to sender */
        SCR_SEND_ERROR_MISORDERED,
        /** Invalid session or slot */
        SCR_SESSION_INVALID,
        /** Error occured which checking rpc item */
        SCR_ERROR
};

/**
   Checks whether received item is correct in sequence or not and suggests
   action to be taken.
   'reply_out' is valid only if return value is RESEND_REPLY.
 */
enum c2_rpc_session_seq_check_result
c2_rpc_session_item_received(struct c2_rpc_item		*item,
			     struct c2_cob_domain 	*dom,
			     struct c2_rpc_item 	**reply_out);


int c2_rpc_cob_create_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx);

#define COB_GET_PFID_HI(cob)    (cob)->co_nsrec.cnr_stobid.si_bits.u_hi
#define COB_GET_PFID_LO(cob)    (cob)->co_nsrec.cnr_stobid.si_bits.u_lo

int c2_rpc_cob_lookup_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx);

int c2_rpc_rcv_sessions_root_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx);

int c2_rpc_rcv_conn_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

int c2_rpc_rcv_conn_create(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

int c2_rpc_rcv_session_lookup(struct c2_cob		*conn_cob,
			      uint64_t			session_id,
			      struct c2_cob		**session_cob,
			      struct c2_db_tx		*tx);

int c2_rpc_rcv_session_create(struct c2_cob	*conn_cob,
			      uint64_t		session_id,
			      struct c2_cob	**session_cob,
			      struct c2_db_tx	*tx);

int c2_rpc_rcv_slot_lookup(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

int c2_rpc_rcv_slot_create(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

int c2_rpc_rcv_slot_lookup_by_item(struct c2_cob_domain        *dom,
                                    struct c2_rpc_item          *item,
                                    struct c2_cob               **slot_cob,
                                    struct c2_db_tx             *tx);

extern struct c2_stob_id	c2_root_stob_id;

/*
 * XXX temporary global rpc machine
 */
extern struct c2_rpcmachine	g_rpcmachine;

void c2_rpc_conn_create_reply_received(struct c2_fop *fop);

void c2_rpc_session_create_reply_received(struct c2_fop *fop);

void c2_rpc_conn_terminate_reply_received(struct c2_fop *fop);

void c2_rpc_conn_search(struct c2_rpcmachine	*machine,
			uint64_t		sender_id,
			struct c2_rpc_conn	**out);

void c2_rpc_session_search(struct c2_rpc_conn		*conn,
			   uint64_t			session_id,
			   struct c2_rpc_session	**out);

bool c2_rpc_snd_slot_is_busy(const struct c2_rpc_snd_slot *slot);
#endif

