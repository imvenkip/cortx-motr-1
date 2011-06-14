/* -*- C -*- */

/* Declarations of functions that are private to rpc-layer */

#ifndef _COLIBRI_RPC_SESSION_INT_H
#define _COLIBRI_RPC_SESSION_INT_H

#include "cob/cob.h"
#include "rpc/session.h"
#include "dtm/verno.h"

/**
   @addtogroup rpc_session

   @{
 */
enum {
	/* XXX need to set some proper value to this constant */
	DEFAULT_SLOT_COUNT = 4
};

extern const char C2_RPC_SLOT_TABLE_NAME[];

/**
    Key into c2_rpc_inmem_slot_table.
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

/**
   In core slot table stores attributes of slots which
   are not needed to be persistent.
   Key is c2_rpc_slot_table_key.
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
                                	struct c2_db_tx		*tx);

extern int c2_rpc_session_module_init(void);
extern void c2_rpc_session_module_fini(void);

/**
   All reply cache related objects are included in this structure.

   c2_rpcmachine CONTAINS one object of c2_rpc_reply_cache.
 */
struct c2_rpc_reply_cache {
	/** dbenv to which slot table belong */
	struct c2_dbenv		*rc_dbenv;
	/** In memory slot table */
	struct c2_table		*rc_inmem_slot_table;
	/** cob domain containing slot cobs */
	struct c2_cob_domain	*rc_dom;
	/** replies are cached in FOL. Currently not used */
	struct c2_fol		*rc_fol;
	/**
	   XXX Temporary mechanism to cache reply items.
	   We don't yet have methods to serialize rpc-item in a buffer,
	   so as to be able to store them in db table.
 	*/
	struct c2_list          rc_item_list;
};

int c2_rpc_reply_cache_init(struct c2_rpc_reply_cache	*rcache,
			    struct c2_cob_domain	*dom,
			    struct c2_fol		*fol);

void c2_rpc_reply_cache_fini(struct c2_rpc_reply_cache *rcache);

enum c2_rpc_session_seq_check_result {
        /** item is valid in sequence. accept it */
        SCR_ACCEPT_ITEM,
        /** item is duplicate of request whose reply is cached in reply cache*/
        SCR_RESEND_REPLY,
        /** Already received this item and its processing is in progress */
        SCR_IGNORE_ITEM,
        /** Item is not in sequence. send err msg to sender */
        SCR_SEND_ERROR_MISORDERED,
        /** Invalid session or slot */
        SCR_SESSION_INVALID,
        /** Error occured while checking rpc item */
        SCR_ERROR
};

/**
   Checks whether received item is correct in sequence or not and suggests
   action to be taken.
   'reply_out' is valid only if return value is RESEND_REPLY.
 */
enum c2_rpc_session_seq_check_result
c2_rpc_session_item_received(struct c2_rpc_item		*item,
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

/**
  Locates cob associated with slot identified by
  <item->ri_sender_id, item->ri_session_id, item->ri_slot_id, item->ri_slot_gen>

  @return 0 if successful. slot_cob != NULL
  @return < 0 if unsuccessful. slot_cob == NULL
 */
int c2_rpc_rcv_slot_lookup_by_item(struct c2_cob_domain        *dom,
                                   struct c2_rpc_item          *item,
                                   struct c2_cob               **slot_cob,
                                   struct c2_db_tx             *tx);

bool c2_rpc_snd_slot_is_busy(const struct c2_rpc_snd_slot *slot);
void c2_rpc_snd_slot_mark_busy(struct c2_rpc_snd_slot *slot);
void c2_rpc_snd_slot_mark_unbusy(struct c2_rpc_snd_slot *slot);

/**
   Insert the item at the end of c2_rpc_snd_slot::ss_ready_list

   @pre session->s_state == SESSION_ALIVE
   @pre slot_id < session->s_nr_slots
   @pre c2_mutex_is_locked(&session->s_mutex)
 */
void c2_rpc_snd_slot_enq(struct c2_rpc_session		*session,
			 uint32_t			slot_id,
			 struct c2_rpc_item		*item);

/**
   Called when an item is enqueued in the slot OR slot becomes "unbusy"
 */
void c2_rpc_snd_slot_state_changed(struct c2_clink	*clink);

/**
   Checks whether reply item is valid in sequence.
   Used on sender side.

   @param item is received item
   @return 0 if reply item is valid and can be accepted. out != NULL and
		contains reference to item whose reply it is.
   @return < 0 if reply item is not valid and should be discarded. out == NULL
 */
int c2_rpc_session_reply_item_received(struct c2_rpc_item	*item,
				       struct c2_rpc_item	**out);

/**
   Fill all session related information in item.

   Doesn't do anything if item already has session related fields assigned.
   If there is no connection with target end-point, then
   c2_rpc_session_item_prepare() initiates connection establishing. Same with
   session.

   If the item is assigned to a slot successfuly then the slot is marked as
   busy.

   @pre item->ri_service_id != NULL
   @return -EBUSY if there is no slot available
   @return -EAGAIN if there is no existing ACTIVE connection or ALIVE session.
   @return 0 if item is filled with session related fields
 */
int c2_rpc_session_item_prepare(struct c2_rpc_item	*item);

uint64_t c2_rpc_sender_id_get(void);
uint64_t c2_rpc_session_id_get(void);

struct c2_rpc_slot_ref {
	struct c2_verno		sr_verno;
	uint64_t		sr_cookie;
	uint64_t		sr_slot_gen;
	struct c2_rpc_slot	*sr_slot;
	struct c2_rpc_item	*sr_item;
	/** Anchor to put item on c2_rpc_slot::sl_item_list */
	struct c2_list_link	sr_link;
	/** Anchor to put item on c2_rpc_slot::sl_ready_list */
	struct c2_list_link	sr_ready_link;
};

/** @}  End of rpc_session group */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

