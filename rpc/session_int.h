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

extern int c2_rpc_session_module_init(void);
extern void c2_rpc_session_module_fini(void);

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

int c2_rpc_root_session_cob_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx);

int c2_rpc_conn_cob_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

int c2_rpc_conn_cob_create(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

int c2_rpc_session_cob_lookup(struct c2_cob		*conn_cob,
			      uint64_t			session_id,
			      struct c2_cob		**session_cob,
			      struct c2_db_tx		*tx);

int c2_rpc_session_cob_create(struct c2_cob	*conn_cob,
			      uint64_t		session_id,
			      struct c2_cob	**session_cob,
			      struct c2_db_tx	*tx);

int c2_rpc_slot_cob_lookup(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

int c2_rpc_slot_cob_create(struct c2_cob	*session_cob,
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
int c2_rpc_slot_cob_lookup_by_item(struct c2_cob_domain        *dom,
                                   struct c2_rpc_item          *item,
                                   struct c2_cob               **slot_cob,
                                   struct c2_db_tx             *tx);

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

uint64_t c2_rpc_sender_id_get(void);
uint64_t c2_rpc_session_id_get(void);

struct c2_rpc_slot_ref {
	struct c2_verno		sr_verno;
	uint64_t		sr_xid;
	uint64_t		sr_slot_gen;
	struct c2_rpc_slot	*sr_slot;
	struct c2_rpc_item	*sr_item;
	/** Anchor to put item on c2_rpc_slot::sl_item_list */
	struct c2_list_link	sr_link;
	/** Anchor to put item on c2_rpc_slot::sl_ready_list */
	struct c2_list_link	sr_ready_link;
};

int __conn_init(struct c2_rpc_conn	*conn,
		struct c2_rpcmachine	*machine);

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

