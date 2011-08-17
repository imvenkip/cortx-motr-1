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
   ret = c2_rpc_reply_timedwait(&item, &timeout);

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

struct c2_rpc_item;

#ifndef __KERNEL__
#include <rpc/xdr.h>
#endif
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
#include "fol/fol.h"
#include "fop/fop_base.h"
#include "rpc/session_internal.h"
#include "rpc/session.h"
#include "addb/addb.h"

/*Macro to enable RPC grouping test and debug code */

#ifdef RPC_GRP_DEBUG
#define MAX_RPC_ITEMS 6
#define NO_OF_ENDPOINTS 3
int32_t rpc_arr_index;
int seed_val;
#endif

/* Number of default receive c2_net_buffers to be used with
   each transfer machine.*/
enum {
	C2_RPC_TM_RECV_BUFFERS_NR = 8,
};

struct c2_rpc;
struct c2_addb_rec;
struct c2_rpc_formation;
struct c2_rpc_conn;
union c2_io_iovec;
struct c2_rpc_group;
struct c2_rpcmachine;
struct c2_update_stream;
struct c2_rpc_connectivity;
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
			union c2_io_iovec *vec);
	/**
	   Find out the size of rpc item.
	 */
	uint64_t (*rito_item_size)(struct c2_rpc_item *item);
	/**
	   Find out if given rpc items belong to same type or not.
	 */
	bool (*rito_items_equal)(struct c2_rpc_item *item1, struct
			c2_rpc_item *item2);
	/**
	   Return the opcode of fop carried by given rpc item.
	 */
	int (*rito_io_get_opcode)(struct c2_rpc_item *item);
	/**
	   Return the fid of request.
	 */
	struct c2_fid (*rito_io_get_fid)(struct c2_rpc_item *item);
	/**
	   Find out if the item belongs to an IO request or not.
	 */
	bool (*rito_is_io_req)(struct c2_rpc_item *item);
	/**
	   Find out the count of fragmented buffers.
	 */
	uint64_t (*rito_get_io_fragment_count)(struct c2_rpc_item *item);
	/**
	   Coalesce rpc items that share same fid and intent(read/write).
	 */
	int (*rito_io_coalesce)(struct c2_rpc_frm_item_coalesced *coalesced_item,
			struct c2_rpc_item *item);
#ifndef __KERNEL__
	/**
	   Serialise @item on provided xdr stream @xdrs
	 */
	int (*rito_encode)(struct c2_rpc_item *item, XDR *xdrs);
	/**
	   Create in memory item from serialised representation of item
	 */
	int (*rito_decode)(struct c2_rpc_item *item, XDR *xdrs);
#endif
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

/** TBD in 'DLD RPC FOP:core wire formats':
    c2_rpc is a container of c2_rpc_items. */
struct c2_rpc {
	/** Linkage into list of rpc objects just formed or into the list
	    of rpc objects which are ready to be sent on wire. */
	struct c2_list_link	r_linkage;
	struct c2_list		r_items;

	/** Items in this container should be sent via this session */
	struct c2_rpc_session  *r_session;
};

/**
   Initialize an rpc object.
   @param rpc - rpc object to be initialized
 */
void c2_rpc_rpcobj_init(struct c2_rpc *rpc);

/**
   Finalize an rpc object.
   @param rpc - rpc object to be finalized 
 */
void c2_rpc_rpcobj_fini(struct c2_rpc *rpc);

/**
   Possible values for flags from c2_rpc_item_type.
 */
enum c2_rpc_item_type_flag {
	/** Item with valid session, slot and version number. */
	C2_RPC_ITEM_BOUND = (1 << 0),
	/** Item with a session but no slot nor version number. */
	C2_RPC_ITEM_UNBOUND = (1 << 1),
	/** Item similar to unbound item except it is always sent as
	    unbound item and it does not expect any reply. */
	C2_RPC_ITEM_UNSOLICITED = (1 << 2),
};

/**
   Definition is taken partly from 'DLD RPC FOP:core wire formats' (not submitted yet).
   Type of an RPC item.
   There is an instance of c2_rpc_item_type for each value of
   c2_rpc_opcode_t.
 */
struct c2_rpc_item_type {
	/** Unique operation code. */
	/* XXX: for now: enum c2_rpc_opcode_t rit_opcode; */
	/** Operations that can be performed on the type */
	const struct c2_rpc_item_type_ops *rit_ops;
	/** true if item is request item. false if item is reply item */
	bool				   rit_item_is_req;
	/** true if the item of this type modifies file-system state */
	bool				   rit_mutabo;
	/** Flag to distinguish unsolicited item from unbound one. */
	uint64_t			   rit_flags;
};

/**
   Post an unsolicited item to rpc layer.
   @param conn - c2_rpc_conn structure from which this item will be posted.
   @param item - input rpc item.
   @retval - 0 if routine succeeds, -ve with proper error code otherwise.
 */
int c2_rpc_unsolicited_item_post(struct c2_rpc_conn *conn,
		struct c2_rpc_item *item);

/**
   Tell whether given item is bound.
   @param item - Input rpc item
   @retval - TRUE if bound, FALSE otherwise.
 */
bool c2_rpc_item_is_bound(struct c2_rpc_item *item);

/**
   Tell whether given item is unbound.
   @param item - Input rpc item
   @retval - TRUE if unbound, FALSE otherwise.
 */
bool c2_rpc_item_is_unbound(struct c2_rpc_item *item);

/**
   Tell whether given item is unsolicited.
   @param item - Input rpc item
   @retval - TRUE if unsolicited, FALSE otherwise.
 */
bool c2_rpc_item_is_unsolicited(struct c2_rpc_item *item);

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
	/** After item's replied  it enters replied state */
	RPC_ITEM_REPLIED = (1 << 4),
	/** After finalization item enters finalized state*/
	RPC_ITEM_FINALIZED = (1 << 5)
};
/** transmission state of item */
enum c2_rpc_item_tstate {
	/** the reply for the item was received and the receiver confirmed
	    that the item is persistent */
	RPC_ITEM_PAST_COMMITTED = 1,
	/** the reply was received, but persistence confirmation wasn't */
	RPC_ITEM_PAST_VOLATILE,
	/** the item was sent (i.e., placed into an rpc) and no reply is
	    received */
	RPC_ITEM_IN_PROGRESS,
	/** the item is not sent */
	RPC_ITEM_FUTURE,
};

enum c2_rpc_item_priority {
	C2_RPC_ITEM_PRIO_MIN,
	C2_RPC_ITEM_PRIO_MID,
	C2_RPC_ITEM_PRIO_MAX,
	C2_RPC_ITEM_PRIO_NR
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

   Example:
   struct c2_fop {
	//...
	struct c2_rpc_item f_item;
	//...
   };
 */
struct c2_rpc_item {
	struct c2_rpcmachine		*ri_mach;
	struct c2_chan			 ri_chan;
	/** linakge to list of rpc items in a c2_rpc_formation_list */
	struct c2_list_link		 ri_linkage;
	struct c2_ref			 ri_ref;

	enum c2_rpc_item_priority	 ri_prio;
	c2_time_t			 ri_deadline;
	struct c2_rpc_group		*ri_group;

	enum c2_rpc_item_state		 ri_state;
	enum c2_rpc_item_tstate		 ri_tstate;
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
	/** Destination endpoint. */
	struct c2_net_end_point		 ri_endp;
	/** Timer associated with this rpc item.*/
	struct c2_timer			 ri_timer;
	/** reply item */
	struct c2_rpc_item		*ri_reply;
	/** For a received item, it gives source end point */
	struct c2_net_end_point		*ri_src_ep;
	/** item operations */
	const struct c2_rpc_item_ops	*ri_ops;
	/** Dummy queue linkage to dummy reqh */
	struct c2_queue_link		 ri_dummy_qlinkage;
	/** Entry time into rpc layer */
	c2_time_t			 ri_rpc_entry_time;
	/** Entry time into rpc layer */
	c2_time_t			 ri_rpc_exit_time;
};

/** Enum to distinguish if the path is incoming or outgoing */
enum c2_rpc_item_path {
	C2_RPC_PATH_INCOMING = 0,
	C2_RPC_PATH_OUTGOING,
	C2_RPC_PATH_NR
};

/**
  Set the stats for outgoing rpc item
  @param item - incoming or outgoing rpc item
  @param path - enum distinguishing whether the item is incoming or outgoing
 */
void c2_rpc_item_exit_stats_set(struct c2_rpc_item *item,
		enum c2_rpc_item_path path);

/**
  Statistical data maintained for each item in the rpcmachine.
  It is upto the higher level layers to retrieve and process this data
 */
struct c2_rpc_stats {
	/** Number of items processed */
	uint64_t	rs_items_nr;
	/** Number of bytes processed */
	uint64_t	rs_bytes_nr;
	/** Instanteneous Latency */
	c2_time_t	rs_i_lat;
	/** Average Latency */
	c2_time_t	rs_avg_lat;
	/** Min Latency */
	c2_time_t	rs_min_lat;
	/** Max Latency */
	c2_time_t	rs_max_lat;
};

/**
   Associate an rpc with its corresponding rpc_item_type.
   Since rpc_item_type by itself can not be uniquely identified,
   rather it is tightly bound to its fop_type, the fop_type_code
   is passed, based on which the rpc_item is associated with its
   rpc_item_type.
 */
void c2_rpc_item_type_attach(struct c2_fop_type *fopt);

/**
   Attach the given rpc item with its corresponding item type.
   @param item - given rpc item.
 */
void c2_rpc_item_attach(struct c2_rpc_item *item);

/**
   Initialize RPC item.
   Finalization of the item is done using ref counters, so no public fini IF.
 */
int c2_rpc_item_init(struct c2_rpc_item *item);

/**
   Returns true if item modifies file system state, false otherwise
 */
bool c2_rpc_item_is_update(struct c2_rpc_item	*item);

/**
   Returns true if item is request item. False if it is a reply item
 */
bool c2_rpc_item_is_request(struct c2_rpc_item *item);

/** DEFINITIONS of RPC layer core DLD: */

/** c2_rpc_formation_list is a structure which represents groups
    of c2_rpc_items, sorted by some criteria (endpoint,...) */
struct c2_rpc_formation_list {
	/* linkage into list of all classification lists in a c2_rpc_processing */
	struct c2_list_link re_linkage;

	/** listss of c2_rpc_items going to the same endpoint */
	struct c2_list re_items;
	struct c2_net_end_point *endpoint;
	/*Mutex to guard this list */
	struct c2_mutex re_guard;
};

/** Group of rpc items to be transmitted in the same
    update stream
 */
struct c2_rpc_group {
	struct c2_rpcmachine *rg_mach;
	/** c2_list<c2_rpc_item> list of rpc items */
	struct c2_list rg_items;
	/** expected number of items in the group */
	size_t rg_expected;
        /** number of items for which no reply is yet received. */
        uint32_t nr_residual;
        /** set to a (negated) error code when there was an error in
	    processing of an item in the group. */
        int32_t rg_rc;
        /** lock protecting fields of the struct */
        struct c2_mutex rg_guard;
	/** signalled when a reply is received or an error happens
	     (usually a timeout). */
	struct c2_chan rg_chan;
	/** Flag to debug */
	int rg_grpid;
};

/**
   Different settings and presets of c2_rpc_processing.
 */
struct c2_rpc_processing_ctl {
};

/**
   RPC processing context used in grouping/formation/output stages.
 */
struct c2_rpc_processing {
	struct c2_rpc_processing_ctl crp_ctl;

	/** GROUPING RELATED DATA: */
	/**  c2_list<c2_rpc_formation_list>: list of groups sorted by endpoints */
	struct c2_list  crp_formation_lists;

	/** FORMATION RELATED DATA: */
	/** c2_list<c2_rpc>: list of formed RPC items */
	struct c2_list crp_form;

	struct c2_list crp_us_list; /* list of update streams in this machine */

	struct c2_mutex crp_guard; /* lock protecting fields of the struct */

	/** OUTPUT RELATED DATA: */
};

/**
   An API to create a c2_net_buffer with given c2_net_domain.
   The rpc core component allocates a pool of buffers in advance to
   receive incoming messages. This is necessary for asynchronous
   behavior of system. This buffer is deallocated when the transfer
   machine to which this buffer was added, gets destroyed.
   @pre - net domain should be initialized.

   @param net_dom - the net domain in which buffers should be registered.
 */
struct c2_net_buffer *c2_rpc_net_recv_buffer_allocate(
		struct c2_net_domain *net_dom);

/**
   Allocate C2_RPC_TM_RECV_BUFFERS_NR number of buffer and add each of
   them to transfer machine's RECV queue.
   @pre net domain should be initialized.
   @pre tm should be initialized and started.

   @param net_dom - net domain in which nr number of buffers will be registered.
   @param tm - transfer machine to which nr number of buffers will be added.
 */
int c2_rpc_net_recv_buffer_allocate_nr(struct c2_net_domain *net_dom,
		struct c2_net_transfer_mc *tm);

/**
   Delete and deregister the buffer meant for receiving messages from the
   queue of net domain and transfer machine respectively and then
   deallocate it.
   @pre nb should be a valid and enqueued net buffer.
   @pre tm should be initialized and started.
   @pre net domain should be initialized.

   @param nb - net buffer to be deallocated.
   @param chan - Concerned c2_rpc_chan structure.
   @param tm_active - boolean indicating whether associated TM is
   active or not.
 */
int c2_rpc_net_recv_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_rpc_chan *chan, bool tm_active);

/**
   Delete and deregister C2_RPC_TM_RECV_BUFFERS_NR number of buffers from
   queues of transfer machine and net domain respectively and then
   deallocate each of the buffer.

   @pre tm should be initialized and started.
   @pre net domain should have been initialized.

   @param chan - Concerned c2_rpc_chan structure.
   @param tm_active - boolean indicating whether associated TM is
   active or not.
 */
int c2_rpc_net_recv_buffer_deallocate_nr(struct c2_rpc_chan *chan,
		bool tm_active);

/**
   Allocate a buffer for sending messages from rpc formation component.
   @pre net domain should be initialized.
   @pre net buffer should not be allocated.
   @post net buffer gets allocated.

   @param net_dom - network domain to which buffers will be registered.
   @param nb - network buffer to be allocated.
 */
void c2_rpc_net_send_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer *nb);

/**
   Deallocate a net buffer meant for sending messages.
   @pre net buffer should be allocated and registered.
   @pre net domain should be initialized.

   @param nb - network buffer which will be deallocated.
   @param net_dom - network domain from which buffer will be deregistered.
 */
int c2_rpc_net_send_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_net_domain *net_dom);

/**
   Transfer machine callback vector for transfer machines created by
   rpc layer.
 */
extern struct c2_net_tm_callbacks c2_rpc_tm_callbacks;

/**
   This structure contains list of {endpoint, transfer_mc} tuples
   used by rpc layer. c2_rpcmachine refers to this structure.
   This structure is created keeping in mind that there could be
   multiple c2_rpcmachine structures co-existing within a colibri
   address space. And multiple c2_rpc_conns within the rpcmachine
   might use same endpoint to communicate.
 */
struct c2_rpc_ep_aggr {
	/** Mutex to protect the list.*/
	struct c2_mutex		ea_mutex;
	/** List of c2_rpc_chan structures. */
	struct c2_list		ea_chan_list;
};

/**
   A physical node can have multiple endpoints associated with it.
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
	/** Transfer machine associated with this endpoint.*/
	struct c2_net_transfer_mc	  rc_tm;
	/** Pool of receive buffers associated with this transfer machine. */
	struct c2_net_buffer		**rc_rcv_buffers;
	/** Number of c2_rpc_conn structures using this transfer machine.*/
	struct c2_ref			  rc_ref;
	/** The rpcmachine, this chan structure is associated with.*/
	struct c2_rpcmachine		 *rc_rpcmachine;
};

/**
   Create a new c2_rpc_chan structure, populate it and add it to
   the list in struct c2_rpc_ep_aggr.
   @param chan - c2_rpc_chan structure to be created.
   @param machine - concerned c2_rpcmachine structure.
   @param net_dom - Network domain associated with given rpcmachine.
   @param ep_addr - End point address to associate with the transfer mc.
 */
int c2_rpc_chan_create(struct c2_rpc_chan **chan, struct c2_rpcmachine *machine,
		struct c2_net_domain *net_dom, const char *ep_addr);

/**
   Destroy the given c2_rpc_chan structure and remove it from the list
   since no one is referring to it any more.
   @param machine - concerned rpc machine.
   @param chan - c2_rpc_chan structure to be destroyed.
 */
void c2_rpc_chan_destroy(struct c2_rpcmachine *machine,
		struct c2_rpc_chan *chan);

/**
   Return a c2_rpc_chan structure given the endpoint.
   Refcount is incremented on the returned c2_rpc_chan.
   This API will be typically used by c2_rpc_conn_establish method
   to get a source endpoint and eventually a transfer machine to
   associate with.
   @param machine - concerned c2_rpcmachine from which new c2_rpc_chan
   structure will be assigned.
 */
struct c2_rpc_chan *c2_rpc_chan_get(struct c2_rpcmachine *machine);

/**
   Release the c2_rpc_chan structure being used.
   This will decrement the refcount of given c2_rpc_chan structure.
   c2_rpc_conn_terminate_reply_received will use this method to
   release the reference to the transfer machine it was using.
   @param chan - chan on which reference will be released.
 */
void c2_rpc_chan_put(struct c2_rpc_chan *chan);

/**
   RPC machine is an instance of RPC item (FOP/ADDB) processing context.
   Several such contexts might be existing simultaneously.
 */
struct c2_rpcmachine {
	/* List of transfer machine used by conns from this rpcmachine. */
	struct c2_rpc_ep_aggr		 cr_ep_aggr;
	/* Formation module associated with this rpcmachine. */
	struct c2_rpc_formation		*cr_formation;
	struct c2_rpc_processing	 cr_processing;
	/** Cob domain in which cobs related to session will be stored */
	struct c2_cob_domain		*cr_dom;
	/** List of rpc connections
	    conn is in list if conn->c_state is not in {CONN_UNINITIALIZED,
	    CONN_FAILED, CONN_TERMINATED} */
	struct c2_list			 cr_incoming_conns;
	struct c2_list			 cr_outgoing_conns;
	/** mutex that protects [incoming|outgoing]_conns. Better name??? */
	struct c2_mutex			 cr_session_mutex;
	/** Mutex to protect list of ready slots. */
	struct c2_mutex			 cr_ready_slots_mutex;
	/** list of ready slots. */
	struct c2_list			 cr_ready_slots;
	/** ADDB context for this rpcmachine */
	struct c2_addb_ctx		 cr_rpc_machine_addb;
	/** Statistics for both incoming and outgoing paths */
	struct c2_rpc_stats		 cr_rpc_stats[C2_RPC_PATH_NR];
	/** Mutex to protect stats */
	struct c2_mutex			 cr_stats_mutex;
};

/**
   This routine does all the network activities associated with given
   rpc machine. This includes creation of new c2_rpc_chan structure
   which internally initializes a transfer mc, then starting the
   transfer mc and allocate some net buffers meant to receive messages.
   @param machine - concerned c2_rpcmachine.
   @param net_dom - Network domain associated with given rpcmachine.
   @param ep_addr - End point address to associate with the transfer mc.
 */
int c2_rpcmachine_net_init(struct c2_rpcmachine *machine,
		struct c2_net_domain *net_dom,
		const char *ep_addr);

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

   @param machine - Input rpcmachine object.
   @param dom - cob domain that contains cobs representing slots
   @param net_dom - Network domain, this rpcmachine is associated with.
   @param ep_addr - End point address to associate with the transfer mc.
   @pre c2_rpc_core_init().
   @return 0 success
   @return -ENOMEM failure
 */
int  c2_rpcmachine_init(struct c2_rpcmachine	*machine,
			struct c2_cob_domain	*dom,
			struct c2_net_domain	*net_dom,
			const char		*ep_addr);

/**
   Destruct rpcmachine
   @param machine rpcmachine operation applied to.
 */
void c2_rpcmachine_fini(struct c2_rpcmachine *machine);

/* @name processing_if PROCESSING IFs: @{ */

/**
   Submit rpc item into processing engine
   or change parameters (priority, caching policy
   and group membership) of an already submitted item.

   @param us update stream used to send the group or NULL for
	  "unbounded items" that don't need update stream semantics.
   @param item rpc item being sent
   @param prio priority of processing of this item
   @param deadline maximum processing time of this item


   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @return 0  success
   @return <0 failure
 */
int c2_rpc_submit(struct c2_service_id		*srvid,
		  struct c2_update_stream	*us,
		  struct c2_rpc_item		*item,
		  enum c2_rpc_item_priority	prio,
		  const c2_time_t		*deadline);

int c2_rpc_reply_submit(struct c2_rpc_item	*request,
			struct c2_rpc_item	*reply,
			struct c2_db_tx		*tx);

/**
  Posts an unbound item to the rpc layer.

  The item will be send trough one of item->ri_session slots.

  The rpc layer will try to send the item out not later than
  item->ri_deadline and with priority of item->ri_priority.

  If this call returns without errors, the item's reply call-back is
  guaranteed to be called eventually.

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

int c2_rpc_reply_post(struct c2_rpc_item *request,
		      struct c2_rpc_item *reply);

/**
   Cancel submitted RPC-item
   @param item rpc item being sent

   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @return 0  success
   @return -EBUSY if item is in SENT, REPLIED or FINALIZED state
 */
int c2_rpc_cancel(struct c2_rpc_item *item);

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
int c2_rpc_group_close(struct c2_rpcmachine *machine, struct c2_rpc_group *group);

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

   @param item rpc item being sent
   @param timeout time to wait for item being sent
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   invoking this function
   @return 0 success
   @return ETIMEDOUT The wait timed out wihout being sent
 */
int c2_rpc_reply_timedwait(struct c2_rpc_item *item, const c2_time_t *timeout);

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

/** @} end name processing_if */

/**
   @name stat_ifs STATISTICS IFs
    Iterfaces, returning different properties of rpcmachine.
    @{
 */

/**
   Returns the count of items in the cache selected by priority
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   @param machine rpcmachine operation applied to.
   @param prio priority of cache

   @return itmes count in cache selected by priority
 */
size_t c2_rpc_cache_item_count(struct c2_rpcmachine *machine,
			       enum c2_rpc_item_priority prio);

/**
   Returns count of RPC items in processing
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   @param machine rpcmachine operation applied to.

   @return count of RPCs in processing
 */
size_t c2_rpc_rpc_count(struct c2_rpcmachine *machine);

/**
   Returns average time spent in the cache for one RPC-item
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   @param machine rpcmachine operation applied to.
   @param time[out] average time spent in processing on one RPC
 */
void c2_rpc_avg_rpc_item_time(struct c2_rpcmachine *machine,
			      c2_time_t		   *time);

/**
   @todo rio_replied op from rpc type ops.
   If this is an IO request, free the IO vector
   and free the fop.
 */
void c2_rpc_item_replied(struct c2_rpc_item *item, int rc);

/**
   Returns transmission speed in bytes per second.
   @note c2_rpc_core_init() and c2_rpcmachine_init() have been called before
   @param machine rpcmachine operation applied to.
 */
size_t c2_rpc_bytes_per_sec(struct c2_rpcmachine *machine);

/** @} end name stat_ifs */

/** DUMMY REQH for RPC IT. Queue of RPC items */
extern struct c2_queue		exec_queue;
extern struct c2_chan		exec_chan;

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
