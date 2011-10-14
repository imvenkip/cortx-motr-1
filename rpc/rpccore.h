/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita_Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

/**
   @defgroup rpc_layer_core RPC layer core
   @page rpc-layer-core-dld RPC layer core DLD
   @section Overview
   RPC layer core is used to transmitt rpc items and groups of them.

   @section rpc-layer-core-logic Logical specification.
   Typical scenario in terms of current interfaces looks like the following:
   User sends (a) FOPs one by one, or (b)group of FOPs and waits for
   reply from endpoint. Such interactions can be expressed in terms of
   provided interfaces:

   @code
   static void us_callback(struct c2_update_stream *us,
                           struct c2_rpc_item *item)             { ... }
   static const struct c2_update_stream_ops us_ops { .uso_event_cb = us_callback };
   //...
   int ret;
   int i;
   struct c2_rpcmachine mach;
   uint64_t session_id;
   struct c2_update_stream *update_stream;
   struct c2_rpc_item item[] = {DUMMY_INITIALIZER, DUMMY_INITIALIZER, ...};
   struct c2_rpc_group *group;
   struct c2_rpc_item_type_ops item_ops = { .rito_item_cb = item_callback };
   struct c2_time timeout = DUMMY_TIMEOUT;
   // initialising fop operations vectors:
   static struct c2_rpc_item_type_ops fop_item_type_ops;
   static struct c2_rpc_item_type fop_item_type;
   fop_item_type.rit_ops = &fop_item_type_ops;

   // INITIALISATION:
   //
   // initialise rpc layer core internal data structures.
   // c2_rpc_core_init() should be called in core/colibri/init.c
   // and executed as a part of c2_init().

   // create rpc machine.
   ret = c2_rpcmachine_init(&mach, cob_domain, net_domain, ep_addr);
   // create/get update stream used for interaction between endpoints
   ret = c2_rpc_update_stream_get(&mach, &srvid,
	C2_UPDATE_STREAM_SHARED_SLOT, &us_ops, &update_stream);

   // USAGE (a):
   // sending rpc_items
   item.ri_type = &fop_item_type;
   ret = c2_rpc_submit(&srvid, &update_stream, &item,
	C2_RPC_ITEM_PRIO_MIN, C2_RPC_CACHING_TYPE);
   // waiting for reply:
   ret = c2_rpc_reply_timedwait(&clink, &timeout);

   // USAGE (b):
   // open and generate new group, used in formation.
   ret = c2_rpc_group_open(&mach, &group);

   // send group of items
   for (i = 0; i < ARRAY_SIZE(item); ++i) {
      item[i].ri_type = &fop_item_type;
      ret = c2_rpc_group_submit(&mach, group, &item[i], &srvid, &update_stream,
	C2_RPC_ITEM_PRIO_MIN, C2_RPC_CACHING_TYPE);
   }

   ret = c2_rpc_group_close(&mach, group);

   @endcode

   @section rpc-layer-core-func Functional specification.
   Internally, the RPC layer core should do the following:
   @li put items into sub-caches, associated with specified services;
   @li monitor the occupancy of the sub-caches;
   @li when there is enough pages in a sub-cache to form an optimal rpc---form it and send.

   For simple implementation one update stream may be maped onto one slot.
   Several update streams may be mapped onto one slot for more complex cases.

   Update stream state machine:
   @verbatim
      UNINITIALIZED
           | update_stream_init()
           |
           |                     next_item()
           |                  +-----+
           V    next_item()   V     |    timeout
          IDLE------------->SENDING-+------------>TIMEDOUT---+
           | ^                ^next_item()                   |
           | |  revovery done |               retry          |
           | +--------------RECOVERY<------------------------+
           |
           | update_stream_fini()
           V
       FINALIZED
    @endverbatim

   RPC-item state machine:
    @verbatim
      UNINITIALIZED
           | rpc_item_init()
           V
         IN USE
           |
           | c2_rpc_item_submit()
           |
           V   added to RPC    sent over nw      got reply over nw
         SUBMITTED------>ADDED------------->SENT----------------> REPLIED--+
           |               |                                               |
  c2_rpc_item_cancel()     | c2_rpc_item_cancel()                          |
           |               V                           rpc_item_fini()     |
           +----------->FINALIZED<-----------------------------------------+

      @endverbatim

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMTljbTZ3anhjbg&hl=en

   @{
*/

#ifndef __COLIBRI_RPC_RPCCORE_H__
#define __COLIBRI_RPC_RPCCORE_H__

#include "lib/cdefs.h"
#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/time.h"
#include "lib/refs.h"
#include "lib/chan.h"
#include "net/net.h"
#include "dtm/verno.h"		/* for c2_verno */
#include "lib/time.h"
#include "lib/timer.h"

#include "cob/cob.h"
#include "rpc/session_internal.h"
#include "rpc/session.h"
#include "addb/addb.h"

enum c2_rpc_item_priority {
	C2_RPC_ITEM_PRIO_MIN,
	C2_RPC_ITEM_PRIO_MID,
	C2_RPC_ITEM_PRIO_MAX,
	C2_RPC_ITEM_PRIO_NR
};

#include "rpc/formation.h"

struct page;
struct c2_rpc;
struct c2_rpc_item;
struct c2_addb_rec;
struct c2_rpc_formation;
struct c2_rpc_conn;
struct c2_fop_type;
struct c2_fop_io_vec;
struct c2_rpc_group;
struct c2_rpcmachine;
struct c2_update_stream;
struct c2_update_stream_ops;
struct c2_rpc_frm_item_coalesced;

/** TBD in sessions header */
enum c2_update_stream_flags {
	/* one slot per one update stream */
	C2_UPDATE_STREAM_DEDICATED_SLOT = 0,
	/* several update streams share the same slot */
	C2_UPDATE_STREAM_SHARED_SLOT    = (1 << 0)
};

/* TBD: different callbacks called on events occured while processing
   in update stream */
struct c2_rpc_item_type_ops {
	/**
	   Called when given item's sent.
	   @param item reference to an RPC-item sent
	   @note ri_added() has been called before invoking this function.
	 */
	void (*rito_sent)(struct c2_rpc_item *item);
	/**
	   Called when item's added to an RPC
	   @param rpc reference to an RPC where item's added
	   @param item reference to an item added to rpc
	 */
	void (*rito_added)(struct c2_rpc *rpc, struct c2_rpc_item *item);

	/**
	   Called when given item's replied.
	   @param item reference to an RPC-item on which reply FOP was received.
	   @param rc error code <0 if failure
	   @note ri_added() and ri_sent() have been called before invoking
	   this function.
	 */
	void (*rito_replied)(struct c2_rpc_item *item, int rc);

	/**
	   Restore original IO vector of rpc item.
	 */
	void (*rito_iovec_restore)(struct c2_rpc_item *b_item,
			struct c2_fop *bkpfop);
	/**
	   Find out the size of rpc item.
	 */
	size_t (*rito_item_size)(const struct c2_rpc_item *item);

	/**
	   Find out if given rpc items belong to same type or not.
	 */
	bool (*rito_items_equal)(struct c2_rpc_item *item1, struct
			c2_rpc_item *item2);
	/**
	   Find out if given rpc items refer to same c2_fid struct or not.
	 */
	bool (*rito_fid_equal)(struct c2_rpc_item *item1,
			       struct c2_rpc_item *item2);
	/**
	  Return true iff item1 and item2 are equal.
	 */
	bool (*rito_eq)(const struct c2_rpc_item *i1,
			const struct c2_rpc_item *i2);
	/**
	   Find out the count of fragmented buffers.
	 */
	uint64_t (*rito_get_io_fragment_count)(struct c2_rpc_item *item);
	/**
	   Coalesce rpc items that share same fid and intent(read/write).
	 */
	int (*rito_io_coalesce)(struct c2_rpc_frm_item_coalesced *coalesced_item,
			struct c2_rpc_item *item);
	/**
	   Serialise @item on provided xdr stream @xdrs
	 */
	int (*rito_encode)(struct c2_rpc_item_type *item_type,
		           struct c2_rpc_item *item,
	                   struct c2_bufvec_cursor *cur);
	/**
	   Create in memory item from serialised representation of item
	 */
	int (*rito_decode)(struct c2_rpc_item_type *item_type,
			   struct c2_rpc_item **item,
			   struct c2_bufvec_cursor *cur);
	/**
	   Return the c2_net_buf_desc from io fop. 
	 */
	void (*rito_io_desc_get)(struct c2_rpc_item *item,
				 struct c2_net_buf_desc *desc);
};

struct c2_rpc_item_ops {
	/**
	   Called when given item's sent.
	   @param item reference to an RPC-item sent
	   @note ri_added() has been called before invoking this function.
	 */
	void (*rio_sent)(struct c2_rpc_item *item);
	/**
	   Called when item's added to an RPC
	   @param rpc reference to an RPC where item's added
	   @param item reference to an item added to rpc
	 */
	void (*rio_added)(struct c2_rpc *rpc, struct c2_rpc_item *item);

	/**
	   Called when given item's replied.
	   @param item reference to an RPC-item on which reply FOP was received.
	   @param rc error code <0 if failure
	   @note ri_added() and ri_sent() have been called before invoking this
	   function.
	 */
	void (*rio_replied)(struct c2_rpc_item	*item,
			   struct c2_rpc_item	*reply, int rc);
};

struct c2_update_stream_ops {
	/** Called when update stream enters UPDATE_STREAM_TIMEDOUT state */
	void (*uso_timeout)(struct c2_update_stream *us);
	/** Called when update stream exits UPDATE_STREAM_RECOVERY state */
	void (*uso_recovery_complete)(struct c2_update_stream *us);
};

enum c2_update_stream_state {
	/** Newly allocated object is in uninitialized state */
	UPDATE_STREAM_UNINITIALIZED = 0,
	/** Enters when update stream is initialized */
	UPDATE_STREAM_IDLE      = (1 << 0),
	/** Enters when items are being sent with update stream */
	UPDATE_STREAM_SENDING   = (1 << 1),
	/** Enters when sending operation is timed out */
	UPDATE_STREAM_TIMEDOUT  = (1 << 2),
	/** Enters when update stream recovers "lost" items */
	UPDATE_STREAM_RECOVERY  = (1 << 3),
	/** Enters when update stream is finalized */
	UPDATE_STREAM_FINALIZED = (1 << 4)
};

/**
   Update streams is an abstraction that serves the following goals:
   @li Hides the sessions and slots abstractions;
   @li Multiplexes several update streams which the the only one session;
   @li Signal about events (state transitions, etc.) happened in sessions layer.
 */
struct c2_update_stream {
	/* linkage to c2_rpcmachine::c2_rpc_processing::crp_us_list */
	struct c2_list_link		   us_linkage;

	uint64_t			   us_session_id;
	uint64_t		           us_slot_id;
	const struct c2_update_stream_ops *us_ops;
	struct c2_rpcmachine		  *us_mach;
	enum c2_update_stream_state        us_state;
        struct c2_mutex			   us_guard;
};

/**
   Possible values for flags from c2_rpc_item_type.
 */
#if 0
enum c2_rpc_item_type_flag {
	/** Item with valid session, slot and version number. */
	//C2_RPC_ITEM_BOUND = (1 << 0),
	/** Item with a session but no slot nor version number. */
	//C2_RPC_ITEM_UNBOUND = (1 << 1),
	/** Item similar to unbound item except it is always sent as
	    unbound item and it does not expect any reply. */
	//C2_RPC_ITEM_UNSOLICITED = (1 << 2),
};
#endif
/**
   Possible values for c2_rpc_item_type::rit_flags.
   Flags C2_RPC_ITEM_TYPE_REQUEST, C2_RPC_ITEM_TYPE_REPLY and
   C2_RPC_ITEM_TYPE_UNSOLICITED are mutually exclusive.
 */
enum c2_rpc_item_type_flags {
	/** Receiver of item is expected to send reply to item of this
	    type */
	C2_RPC_ITEM_TYPE_REQUEST = 1,
	/** Item of this type is reply to some item of C2_RPC_ITEM_TYPE_REQUEST
	    type. */
	C2_RPC_ITEM_TYPE_REPLY = (1 << 1),
	/** This is a one-way item. There is no reply for this type of
	    item */
	C2_RPC_ITEM_TYPE_UNSOLICITED = (1 << 2),
	/** Item of this type can modify file-system state on receiver. */
	C2_RPC_ITEM_TYPE_MUTABO = (1 << 3)
};

/**
   Definition is taken partly from 'DLD RPC FOP:core wire formats'
   (not submitted yet).
   Type of an RPC item.
   There is an instance of c2_rpc_item_type for each value of
   c2_rpc_opcode_t.
 */
struct c2_rpc_item_type {
	/** Unique operation code. */
	uint32_t			   rit_opcode;
	/** Operations that can be performed on the type */
	const struct c2_rpc_item_type_ops *rit_ops;
	/** see @c2_rpc_item_type_flags */
	uint64_t			   rit_flags;
};

#define C2_RPC_ITEM_TYPE_DEF(itype, opcode, flags, ops)  \
struct c2_rpc_item_type (itype) = {                      \
	.rit_opcode = (opcode),                          \
	.rit_flags = (flags),                            \
	.rit_ops = (ops)                                 \
};

int c2_rpc_unsolicited_item_post(const struct c2_rpc_conn *conn,
				 struct c2_rpc_item *item);

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unsolicited(const struct c2_rpc_item *item);

enum c2_rpc_item_state {
	/** Newly allocated object is in uninitialized state */
	RPC_ITEM_UNINITIALIZED = 0,
	/** After successful initialization item enters to "in use" state */
	RPC_ITEM_IN_USE = (1 << 0),
	/** After item's added to the formation cache */
	RPC_ITEM_SUBMITTED = (1 << 1),
	/** After item's added to an RPC it enters added state */
	RPC_ITEM_ADDED = (1 << 2),
	/** After item's sent  it enters sent state */
	RPC_ITEM_SENT = (1 << 3),
	/** After item's sent is failed, it enters send failed state */
	RPC_ITEM_SEND_FAILED = (1 << 4),
	/** After item's replied  it enters replied state */
	RPC_ITEM_REPLIED = (1 << 5),
	/** After finalization item enters finalized state*/
	RPC_ITEM_FINALIZED = (1 << 6)
};
/** Stages of item in slot */
enum c2_rpc_item_stage {
	/** the reply for the item was received and the receiver confirmed
	    that the item is persistent */
	RPC_ITEM_STAGE_PAST_COMMITTED = 1,
	/** the reply was received, but persistence confirmation wasn't */
	RPC_ITEM_STAGE_PAST_VOLATILE,
	/** the item was sent (i.e., placed into an rpc) and no reply is
	    received */
	RPC_ITEM_STAGE_IN_PROGRESS,
	/** the item is not sent */
	RPC_ITEM_STAGE_FUTURE,
};

enum {
	/** Maximum number of slots to which an rpc item can be associated */
	MAX_SLOT_REF = 1
};

/**
   A single RPC item, such as a FOP or ADDB Record.  This structure should be
   included in every item being sent via RPC layer core to emulate relationship
   similar to inheritance and to allow extening the set of rpc_items without
   modifying core rpc headers.
   @see c2_fop.
 */
struct c2_rpc_item {
	struct c2_chan			 ri_chan;
	enum c2_rpc_item_priority	 ri_prio;
	c2_time_t			 ri_deadline;
	struct c2_rpc_group		*ri_group;

	enum c2_rpc_item_state		 ri_state;
	enum c2_rpc_item_stage		 ri_stage;
	uint64_t			 ri_flags;
	struct c2_rpc_session		*ri_session;
	struct c2_rpc_slot_ref		 ri_slot_refs[MAX_SLOT_REF];
	/** Anchor to put item on c2_rpc_session::s_unbound_items list */
	struct c2_list_link		 ri_unbound_link;
	int32_t				 ri_error;
	/** Pointer to the type object for this item */
	struct c2_rpc_item_type		*ri_type;
	/** Linkage to the forming list, needed for formation */
	struct c2_list_link		 ri_rpcobject_linkage;
	/** Linkage to the unformed rpc items list, needed for formation */
	struct c2_list_link		 ri_unformed_linkage;
	/** Linkage to the group c2_rpc_group, needed for grouping */
	struct c2_list_link		 ri_group_linkage;
	/** Linkage to list of items which are coalesced, anchored
	    at c2_rpc_frm_item_coalesced::ic_member_list. */
	struct c2_list_link		 ri_coalesced_linkage;
	/** Timer associated with this rpc item.*/
	struct c2_timer			 ri_timer;
	/** reply item */
	struct c2_rpc_item		*ri_reply;
	/** item operations */
	const struct c2_rpc_item_ops	*ri_ops;
	/** Dummy queue linkage to dummy reqh */
	struct c2_queue_link		 ri_dummy_qlinkage;
	/** Time spent in rpc layer. */
	c2_time_t			 ri_rpc_time;
};

/** Enum to distinguish if the path is incoming or outgoing */
enum c2_rpc_item_path {
	C2_RPC_PATH_INCOMING = 0,
	C2_RPC_PATH_OUTGOING,
	C2_RPC_PATH_NR
};

/**
  Statistical data maintained for each item in the rpcmachine.
  It is upto the higher level layers to retrieve and process this data
 */
struct c2_rpc_stats {
	/** Number of items processed */
	uint64_t	rs_items_nr;
	/** Number of bytes processed */
	uint64_t	rs_bytes_nr;
	/** Cumulative latency. */
	c2_time_t	rs_cumu_lat;
	/** Min Latency */
	c2_time_t	rs_min_lat;
	/** Max Latency */
	c2_time_t	rs_max_lat;
	/** Number of rpc objects (used to calculate packing density) */
	uint64_t	rs_rpcs_nr;
};

/** Returns an rpc item type associated with a unique rpc
item type opcode */
struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode);

/**
   Associate an rpc with its corresponding rpc_item_type.
   Since rpc_item_type by itself can not be uniquely identified,
   rather it is tightly bound to its fop_type, the fop_type_code
   is passed, based on which the rpc_item is associated with its
   rpc_item_type.
 */
void c2_rpc_item_type_attach(struct c2_fop_type *fopt);

void c2_rpc_item_init(struct c2_rpc_item *item);

void c2_rpc_item_fini(struct c2_rpc_item *item);

void c2_rpc_item_fini(struct c2_rpc_item *item);

/**
   Returns true if item modifies file system state, false otherwise
 */
bool c2_rpc_item_is_update(const struct c2_rpc_item *item);

/**
   Returns true if item is request item. False if it is a reply item
 */
bool c2_rpc_item_is_request(const struct c2_rpc_item *item);

struct c2_rpc_group {
	struct c2_rpcmachine	*rg_mach;
	/** List of rpc items linked through c2_rpc_item:ri_group_linkage. */
	struct c2_list		 rg_items;
	/** expected number of items in the group */
	uint64_t		 rg_expected;
        /** lock protecting fields of the struct */
        struct c2_mutex		 rg_guard;
	/** signalled when a reply is received or an error happens
	     (usually a timeout). */
	struct c2_chan		 rg_chan;
};

/**
   Struct c2_rpc_chan provides information about a target network endpoint.
   An rpc machine (struct c2_rpcmachine) contains list of c2_rpc_chan structures
   targeting different net endpoints.
   Rationale A physical node can have multiple endpoints associated with it.
   And multiple services can share endpoints for transport.
   The rule of thumb is to use one transfer machine per endpoint.
   So to make sure that services using same endpoint,
   use the same transfer machine, this structure has been introduced.
   Struct c2_rpc_conn is used for a particular service and now it
   points to a struct c2_rpc_chan to identify the transfer machine
   it is working with.
 */
struct c2_rpc_chan {
	/** Linkage to the list maintained by c2_rpcmachine.*/
	struct c2_list_link		  rc_linkage;
	/** Number of c2_rpc_conn structures using this transfer machine.*/
	struct c2_ref			  rc_ref;
	/** Formation state machine associated with chan. */
	struct c2_rpc_frm_sm		  rc_frmsm;
	/** Destination end point to which rpcs will be sent. */
	struct c2_net_end_point		 *rc_destep;
	/** The rpcmachine, this chan structure is associated with.*/
	struct c2_rpcmachine		 *rc_rpcmachine;
};

/**
   RPC machine is an instance of RPC item (FOP/ADDB) processing context.
   Several such contexts might be existing simultaneously.
 */
struct c2_rpcmachine {
	/** Mutex protecting list of c2_rpc_chan structures. */
	struct c2_mutex			  cr_chan_mutex;
	/** List of c2_rpc_chan structures. */
	struct c2_list			  cr_chans;
	/** Transfer machine associated with this endpoint.*/
	struct c2_net_transfer_mc	  cr_tm;
	/** Pool of receive buffers associated with this transfer machine. */
	struct c2_net_buffer		**cr_rcv_buffers;
	/** Cob domain in which cobs related to session will be stored */
	struct c2_cob_domain		 *cr_dom;
	/** List of rpc connections
	    conn is in list if conn->c_state is not in {CONN_UNINITIALIZED,
	    CONN_FAILED, CONN_TERMINATED} */
	struct c2_list			  cr_incoming_conns;
	struct c2_list			  cr_outgoing_conns;
	/** mutex that protects [incoming|outgoing]_conns. Better name??? */
	struct c2_mutex			  cr_session_mutex;
	/** Mutex to protect list of ready slots. */
	struct c2_mutex			  cr_ready_slots_mutex;
	/** list of ready slots. */
	struct c2_list			  cr_ready_slots;
	/** ADDB context for this rpcmachine */
	struct c2_addb_ctx		  cr_rpc_machine_addb;
	/** Statistics for both incoming and outgoing paths */
	struct c2_rpc_stats		  cr_rpc_stats[C2_RPC_PATH_NR];
	/** Mutex to protect stats */
	struct c2_mutex			  cr_stats_mutex;
	/**
	    Request handler this rpcmachine belongs to.
	    @todo There needs to be  generic mechanism to register a
		request handler (or any other handler for future use)
		with the rpc machine and a ops vector specifying a
		method to be invoked for futher processing,
		e.g. c2_reqh_fop_handle(), in case of reqh.
	*/
	struct c2_reqh                         *cr_reqh;
};

/**
   Construct rpc core layer
   @return 0 success
   @return -ENOMEM failure
*/
int  c2_rpc_core_init(void);
/** Destruct rpc core layer */
void c2_rpc_core_fini(void);

/**
   Rpc machine is a running instance of rpc layer. A number of rpc machine
   structures can co-exist in rpc layer. With every rpc machine, a sessions
   module, a formation module, sending/receiving logic and statistics
   components are associated.

   @param machine Input rpcmachine object.
   @param dom cob domain that contains cobs representing slots
   @param net_dom Network domain, this rpcmachine is associated with.
   @param ep_addr Source end point address to associate with the transfer mc.
   @pre c2_rpc_core_init().
 */
int  c2_rpcmachine_init(struct c2_rpcmachine	*machine,
			struct c2_cob_domain	*dom,
			struct c2_net_domain	*net_dom,
			const char		*ep_addr,
			struct c2_reqh          *reqh);

/**
   Destruct rpcmachine
   @param machine rpcmachine operation applied to.
 */
void c2_rpcmachine_fini(struct c2_rpcmachine *machine);

/**
  Posts an unbound item to the rpc layer.

  The item will be sent through one of item->ri_session slots.

  The rpc layer will try to send the item out not later than
  item->ri_deadline and with priority of item->ri_priority.

  If this call returns without errors, the item's reply call-back is
  guaranteed to be called eventually.

  After successful call to c2_rpc_post(), user should not free the item.
  Rpc-layer will internally free the item when rpc-layer is sure that the item
  will not take part in recovery.

  Rpc layer does not provide any API, to "wait until reply is received".
  If item->ri_ops->rio_replied callback is set, then it will be called when
  reply to the item is received.

  If caller of this function, wants to wait until reply is received, then
  caller should:
  - provide implementation of item->ri_ops->rio_replied() callback.
    The implementation of ->rio_replied() should do at least two things:
    - Third argument of this callback gives error code. Copy this error code to
      req->ri_error (req is first argument of callback, and points to request
      item for which reply is received).
    - Broadcast on req->ri_chan.

  - add a clink to item->ri_chan and wait on the clink.
  - once out of wait, pointer to reply item can be retrieved from
    item->ri_reply, after checking item->ri_error.

  Note: setting item->ri_ops and adding clink to item->ri_chan MUST be done
  before calling c2_rpc_post(), because reply to the item can be received
  even before c2_rpc_post() returns.

  @pre item->ri_session != NULL
  @pre item->ri_priority is sane.
*/
int c2_rpc_post(struct c2_rpc_item *item);

#if 0
/**
  Posts an item bound to the update stream.
*/
int c2_rpc_update_stream_post(struct c2_update_stream *str,
                 struct c2_rpc_item *item);
#endif

/**
  Posts reply item on the same session on which the request item is received.

  After successful call to c2_rpc_reply_post(), user should not free the reply
  item. Rpc-layer will internally free the item when rpc-layer is sure that
  the corresponding request item will not take part in recovery.
 */
int c2_rpc_reply_post(struct c2_rpc_item *request,
		      struct c2_rpc_item *reply);

/**
   Generate group used to treat rpc items as a group.

   @param machine rpcmachine operation applied to.
   @param group returned from the function

   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @return 0 success
   @return -ENOMEM failure
 */
int c2_rpc_group_open(struct c2_rpcmachine *machine,
		      struct c2_rpc_group **group);

/**
   Tell RPC layer core that group is closed
   and it can be processed by RPC core processing

   @param machine rpcmachine operation applied to.
   @param group return value from the function

   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @return 0  success
   @return <0 failure
 */
int c2_rpc_group_close(struct c2_rpc_group *group);

/**
   Submit rpc item group into processing engine.
   or change parameters (priority, caching policy
   and group membership) of an already submitted item.

   @param group used treat rpc items as a group.
   @param item rpc item being sent
   @param us update stream used to send the group
   @param prio priority of processing of this item
   @param deadline maximum processing time of this item

   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @return 0  success
   @return <0 failure
 */
int c2_rpc_group_submit(struct c2_rpc_group		*group,
			struct c2_rpc_item		*item,
			struct c2_update_stream		*us,
			enum c2_rpc_item_priority	 prio,
			const c2_time_t			*deadline);

/**
   Wait for the reply on item being sent.

   @param The clink on which caller is waiting for item reply.
   @param timeout time to wait for item being sent
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   invoking this function
   @return 0 success
   @return ETIMEDOUT The wait timed out wihout being sent
 */
int c2_rpc_reply_timedwait(struct c2_clink *clink, const c2_time_t timeout);

/**
   Wait for the reply on group of items being sent.

   @param group used treat rpc items as a group.
   @param timeout time to wait for item being sent
   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @return 0 success
   @return ETIMEDOUT The wait timed out wihout being sent
 */
int c2_rpc_group_timedwait(struct c2_rpc_group *group, const c2_time_t *timeout);

/**
   Retrurns update stream associated with given service id.
   @param machine rpcmachine operation applied to.
   @param session_id session id for which update stream is being retrieved.
   @param flag specifies features of update stream, @see c2_update_stream_flags
   @param ops operations associated with the update stream
   @param out update associated with given session

   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   invoking this function
   @return 0  success
   @return <0 failure
 */
int c2_rpc_update_stream_get(struct c2_rpcmachine *machine,
			     struct c2_service_id *srvid,
			     enum c2_update_stream_flags flag,
			     const struct c2_update_stream_ops *ops,
			     struct c2_update_stream **out);

/**
   Releases given update stream.
   @param us update stream to be released
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before

*/
void c2_rpc_update_stream_put(struct c2_update_stream *us);

/**
   @name stat_ifs STATISTICS IFs
   Iterfaces, returning different properties of rpcmachine.
   @{
 */

/**
   Returns average time spent in the cache for one RPC-item
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   @param machine rpcmachine operation applied to.
   @param path Incoming or outgoing path of rpc item.
 */
c2_time_t c2_rpc_avg_item_time(struct c2_rpcmachine *machine,
			       const enum c2_rpc_item_path path);

/**
   Returns transmission speed in bytes per second.
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   @param machine rpcmachine operation applied to.
   @param path Incoming or outgoing path of rpc item.
 */
size_t c2_rpc_bytes_per_sec(struct c2_rpcmachine *machine,
			    const enum c2_rpc_item_path path);

/** @} end name stat_ifs */

/**
   @defgroup rpc_bulk Bulk IO support for RPC layer.
   @{

   Detailed Level Design for bulk IO interface from rpc layer.
   Colibri rpc layer, network layer and the underlying transport are
   supposed to constitute a zero-copy path for data IO.
   In order to do this, rpc layer needs to provide support for
   bulk interface exported by network layer which gives the capability
   to bundle IO buffers together and send/receive these buffer descriptors
   on demand. The underlying transport should have the capabilities
   to provide zero-copy path (e.g. RDMA).
   There are 2 major use cases here - read IO and write IO in which
   bulk interface is needed.
   The bulk IO interface from network layer provides abstractions like
   - c2_net_buffer (a generic buffer identified at network layer) and
   - c2_net_buf_desc (an identifier to point to a c2_net_buffer).

   Whenever data buffers are encountered in rpc layer, rpc layer
   (especially formation sub-component) is supposed to take care of
   segregating these rpc items and register c2_net_buffers where
   data buffers are encountered (during write request and read reply)
   and buffer descriptors are copied into rpc items after registering
   net buffers.
   These descriptors are sent to the other side which asks for
   buffers identified by the supplied buffer descriptors.
   Please find below, 2 particular use cases of using bulk interface.
   @verbatim

   Sequence of events in case of write IO call for rpc layer.
   Assumptions
   - Write request call has IO buffers associated with it.
   - Underlying transport supports zero-copy.

			Client			Server

- Init rpc machine.	  |			| - Init rpc machine.
			  |			|
- Init Transfer Mc.	  |			| - Init Transfer Mc.
			  |			|
- Start Transfer Mc.	  |			| - Start Transfer Mc.
			  |			|
- Add recv buffers.	  |			| - Add recv buffers.
			  |   Net buffer sent	|
- Incoming write req.	  |	     +--------->| - Net buffer received.
			  |	     |		|
- Rpc formation finds	  |	     |		| - Decode and retrieve rpc
  given item is write	  |	     |		|   items.
  IO request.		  |	     |		|
			  |	     |		|
- Remove data buffers	  |	     |		| - Call an rpc_item_type_op
  from rpc item & copy	  |	     |		|   which will act if item
  net_buf_desc which	  |	     |		|   is write IO and it contains
  are bundled with	  |	     |		|   c2_net_buf_desc. Buf desc
  given rpc item.	  |	     |		|   are decoded and are copied
  Net buffers are added	  |	     |		|   into recv buffers.
  for these data buffs	  |	     |		|   @see
  in C2_NET_QT_PASSIVE	  |	     |		|   c2_rpc_bulkio_desc_received
  BULK_SEND		  |	     |		|
  queue of TM. Buffer	  |	     |		|
  descriptors are	  |	     |		|
  encoded and packed	  |	     |		|
  with rpc.		  |	     |		|
  @see			  |	     |		|
c2_rpc_bulkio_desc_send   |	     |		|
			  |	     |		|
- Free the net_buf_desc	  |	     |		| - If item is write request,
  after bundling	  |	     |		|   allocate c2_net_buffer/s,
  with rpc item.	  |	     |		|   add it to TM in C2_NET_QT
			  |	     |		|   _ACTIVE_BULK_RECV queue.
			  |	     |		|
- Send rpc over wire.	  |--------->+		| - So server calls c2_rpc_
			  |		0-copy	|   zero_copy_init(active_buffs
			  |		 init	|   , passive_descs, bufs_nr)
			  |	    +<----------|   which should
			  |	    |		|   initiate zero copy operation
			  |	    |		|   at the transport level.
			  |	    |		|
- Transport zero copies	  |	    |		| - Proceed with the write FOM
  the IO buffers	  |	    |	 +----->|   and complete write IO
  identified by		  |<--------+	 |	|   request.
  passive_descs		  |		 |	|
  to active buffers	  |	    +----+	|
  on server.		  |	    |  0-copy	|
			  |-------->+ Complete	|
			  |			|
			  |			|
- Free net buffers used	  |	    +<----------| - Write IO complete. Send
  for write IO.		  |	    |  Net buf	|   write reply to rpc layer.
			  |	    |	sent	|
- Receive net buffer.	  |<--------+		|
			  |			|
- Send reply to write	  |			| - Free net buffers used
  FOM.			  |			|   for zero copy.
			  |			|

   Sequence of events in case of read IO call for rpc layer.
   Assumptions
   - Read request fop has IO buffers associated with it. These buffers are
     actually empty, they contain no user data. These buffers are replaced
     by c2_net_buf_desc and packed with rpc.
   - And read reply fop consists of number of bytes read.
   - Underlying transport supports zero-copy.

			Client			Server

- Init rpc machine.	  |			| - Init rpc machine.
			  |			|
- Init Transfer Mc.	  |			| - Init Transfer Mc.
			  |			|
- Start Transfer Mc.	  |			| - Start Transfer Mc.
			  |			|
- Add recv buffers.	  |			| - Add recv buffers.
			  |   Net buffer sent	|
- Incoming read req.	  |	      +-------->| - Net buffer received.
			  |	      |		|
- Remove data buffers	  |	      |		| - Decode and retrieve rpc
  from fop and replace	  |	      |		|   items.
  it by c2_net_buf_desc	  |	      |		|
  The net buffers are	  |	      |		|
  added to C2_NET_QT_	  |	      |		|
  PASSIVE_BULK_RECV	  |	      |		|
  queue of TM and rpc	  |	      |		|
  is sent over wire.	  |---------->+		|
			  |			|
- Transport zero copies	  |<----------+		| - Dispatch rpc item for
  the data into		  |	      |		|   execution and read FOM.
  destination net	  |	      |		|   starts.
  identified by source	  |	      |		|
  net buf descriptors.	  |---->+     |		|
			  |	|     |		|
- Net buffer received	  |<-+	|     |  0-copy	| - Read FOM allocates net
			  |  |	|     |	  init	|   buffers and registers them
			  |  |	|     |		|   with the net domain. This
			  |  |	|     |		|   makes sure that data path
			  |  |	|     |		|   complies with zero-copy.
			  |  |	|     |		|
- Rpc checks if rcvd	  |  |	|     +<--------| - Server initiates zero_copy
  item belongs to read	  |  |	|		|   by supplying the just
  request & deallocates	  |  |	+-----+		|   allocated net buffers and
  net buffers used for	  |  |	      |		|   source net buf descriptors.
  copying data.		  |  |	      |		|   The net buffers are added
			  |  |	      |		|   C2_NET_QT_ACTIVE_BULK_SEND
			  |  |	      |	0-copy	|   queue of TM.
			  |  |	      | complete|   The transport layer from
			  |  |	      |		|   client and server zero
			  |  |	      |		|   copies the read data from
			  |  |	      +-------->|   server to client buffers.
			  |  |			|
 - Send reply to read	  |  |			| - Read reply is posted to rpc
   FOM.			  |  +<-----------------|   & RPC is sent over wire.
			  |	  Net buffer	|
			  |	    sent	|
   @endverbatim

 */

/**
   A magic constant for sanity of struct c2_rpc_bulk.
 */
enum {
	C2_RPC_BULK_MAGIC = 0xfedcba0123456789ULL,
};

/**
   An abstract data structure that avails bulk transport for io operations.
   End users will register the io vectors using this structure and bulk
   transfer apis will take care of doing the data transfer in zero-copy
   fashion.
 */
struct c2_rpc_bulk {
	/** Magic to verify sanity of struct c2_rpc_bulk. */
	uint64_t		 rb_magic;
	/** Net buffer that will contain the io data. */
	struct c2_net_buffer	 rb_nbuf;
	/** Zero vector representing io data. */
	struct c2_0vec		 rb_zerovec;
	/** Channel to wait on rpc bulk to complete the io. */
	struct c2_chan		 rb_chan;
	/** Return value of results like addition of buffers to transfer
	    machine and zero-copy operation. This field is updated by
	    net buffer send/receive callbacks. */
	int32_t			 rb_rc;
};

/**
   Initialize a rpc bulk structure.
   @param rbulk rpc bulk structure to be initialized.
   @param segs_nr Number of segments to be contained by zero vector.
   @param seg_size Size of each segment contained by zero vector.
   @param netdom Net domain to which the zero vector belongs.
   @pre rbulk != NULL
 */
int c2_rpc_bulk_init(struct c2_rpc_bulk *rbulk,
		     const uint32_t segs_nr,
		     const c2_bcount_t seg_size,
		     struct c2_net_domain *netdom);

/**
   Finalize the rpc bulk structure.
   @pre rbulk != NULL
 */
void c2_rpc_bulk_fini(struct c2_rpc_bulk *rbulk);

/**
   Add a buffer/page to the zero vector referred by rpc bulk structure.
   @param rbulk rpc bulk structure to which a page/buffer will be added.
   @param pg Buffer referring to user data.
   @param index Index of target object to which io is targeted.
   @pre rbulk != NULL
   @pre pg != NULL
   @post zerovec_invariant(rbulk->rb_zerovec)
 */
int c2_rpc_bulk_page_add(struct c2_rpc_bulk *rbulk, struct page *pg,
			 const c2_bindex_t index);

/**
   Add a user space buffer to zero vector referred to by rpc bulk structure.
   @param rbulk rpc bulk structure to which user space buffer will be added.
   @param buf User space buffer starting address.
   @param count Number of bytes in user space buffer.
   @param index Index of target object to which io is targeted.
 */
int c2_rpc_bulk_buf_add(struct c2_rpc_bulk *rbulk,
			void *buf,
			const c2_bcount_t count,
			const c2_bindex_t index);

/**
   Stores the c2_net_buf_desc for the net buffer pointed to by c2_rpc_bulk
   structure in the provided buffer descriptor. This API is typically invoked
   from the sender side.
   @param rbulk Rpc bulk structure whose net buf descriptor is to be stored.
   @param desc Out parameter for net buf descriptor to be stored into.
 */
int c2_rpc_bulk_store(struct c2_rpc_bulk *rbulk, struct c2_net_buf_desc *desc);

/**
   Loads the c2_net_buf_desc pointing to the net buffer contained by
   c2_rpc_bulk structure and starts RDMA transfer of buffers.
   This API is typically used by receiver side.
   @param rbulk Rpc bulk structure whose net buffer is to be transferred.
   @param desc Net buf descriptor representing the net buffer on sender
   side which has to be transferred during RDMA transfer.
 */
int c2_rpc_bulk_load(struct c2_rpc_bulk *rbulk, struct c2_net_buf_desc *desc);

/** @} endgroup of rpc_bulk */

/** @} end group rpc_layer_core */
/* __COLIBRI_RPC_RPCCORE_H__  */
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
