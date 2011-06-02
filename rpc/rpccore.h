#include "cob/cob.h"
#include "fol/fol.h"
#include "fop/fop.h"
#include "rpc/session_int.h"

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

   @code typical-usage
   static void us_callback(struct c2_update_stream *us,
                           struct c2_rpc_item *item)             { ... }
   static const struct c2_update_stream_ops us_ops { .uso_event_cb = us_callback };
   //...
   int ret;
   int i;
   struct c2_rpcmachine mach;
   struct c2_service_id srvid = EXISTING_ID;
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
   ret = c2_rpcmachine_init(&mach);
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

   RPC-item state machine:
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

/*Macro to enable RPC grouping test and debug code */

#ifdef RPC_GRP_DEBUG
#define MAX_RPC_ITEMS 6
#define NO_OF_ENDPOINTS 3
int32_t rpc_arr_index;
int seed_val;
#endif

struct c2_fop;
struct c2_rpc;
struct c2_rpc_conn;
struct c2_rpc_item;
struct c2_addb_rec;
struct c2_update_stream;
struct c2_rpc_connectivity;
struct c2_update_stream_ops;

struct c2_net_end_point {
        /** Keeps track of usage */
        struct c2_ref          nep_ref;
        /** Pointer to the network domain */
        //struct c2_net_domain  *nep_dom;
        /** Linkage in the domain list */
        struct c2_list_link    nep_dom_linkage;
        /** Transport specific printable representation of the
            end point address.
        */
        const char            *nep_addr;
};

/*Just a placeholder for endpoint, will be removed later */
struct c2_net_endpoint {
	int endpoint_val;
};
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
	   @pre rio_added() called.
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
	   @pre rio_added() called.
	   @pre rio_sent() called.
	 */
	void (*rio_replied)(struct c2_rpc_item *item, int rc);
	/**
	   Find out the size of rpc item.
	 */
	uint64_t (*rio_item_size)(struct c2_rpc_item *item);
	/**
	   Find out if the item belongs to an IO request or not.
	 */
	bool (*rio_is_io_req)(struct c2_rpc_item *item);
	/**
	   Find out the count of fragmented buffers.
	 */
	uint64_t (*rio_get_io_fragment_count)(struct c2_rpc_item *item);
	/**
	   Find out if the IO is read or write.
	 */
	int (*rio_io_get_opcode)(struct c2_rpc_item *item);
	/**
	   Return the IO vector from the IO request. 
	 */
	void *(*rio_io_get_vector)(struct c2_rpc_item *item);
	/**
	   Get new coalesced rpc item.
	 */
	int (*rio_get_new_io_item)(struct c2_rpc_item *item1,
			struct c2_rpc_item *item2, void *pvt);
	/**
	   Return the fid of request.
	 */
	void *(*rio_io_get_fid)(struct c2_rpc_item *item);
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
	struct c2_list_link	r_linkage;
	struct c2_list		r_items;

	/** Items in this container should be sent via this session */
	struct c2_rpc_session  *r_session;
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
};


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

enum c2_rpc_item_priority {
	C2_RPC_ITEM_PRIO_MIN,
	C2_RPC_ITEM_PRIO_MID,
	C2_RPC_ITEM_PRIO_MAX,
	C2_RPC_ITEM_PRIO_NR
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
	struct c2_rpcmachine *ri_mach;
	/** linakge to list of rpc items in a c2_rpc_formation_list */
	struct c2_list_link ri_linkage;
	struct c2_ref ri_ref;

	enum c2_rpc_item_priority  ri_prio;
	c2_time_t	   ri_deadline;
	struct c2_rpc_group       *ri_group;

	enum c2_rpc_item_state     ri_state;

	struct c2_service_id		*ri_service_id;
	/** Session related fields. Should be included in on wire rpc-item */
	uint64_t			ri_sender_id;
	uint64_t			ri_session_id;
	uint32_t			ri_slot_id;
	uint64_t			ri_slot_generation;
	/** ri_verno acts as sequence counter */
	struct c2_verno			ri_verno;
	/** link used to store item in c2_rpc_snd_slot::ss_ready_list or
	    on c2_rpc_snd_slot::ss_replay_list */
	struct c2_list_link		ri_slot_link;
	/** XXX temporary field to put item on in-core reply cache list */
	struct c2_list_link			ri_rc_link;
	
	/** Pointer to the type object for this item */
	struct c2_rpc_item_type *ri_type;
	struct c2_chan ri_chan;
	/* An item is assigned "a xid" by the sessions module
	   once it is bound to a particular slot. */
	uint64_t ri_xid;
	/** Linkage to the forming list, needed for formation */
	struct c2_list_link	ri_rpcobject_linkage;
	/** Linkage to the unformed rpc items list, needed for formation */
	struct c2_list_link	ri_unformed_linkage;
	/** Linkage to the group c2_rpc_group, needed for grouping */
	struct c2_list_link	ri_group_linkage;
	/** Destination endpoint. */
	struct c2_net_end_point	ri_endp;
	/** Timer associated with this rpc item.*/
	struct c2_timer		ri_timer;
};

/**
   Initialize RPC item.
   Finalization of the item is done using ref counters, so no public fini IF.
 */
int c2_rpc_item_init(struct c2_rpc_item *item,
		     struct c2_rpcmachine *mach);

/** DEFINITIONS of RPC layer core DLD: */

/** c2_rpc_formation_list is a structure which represents groups
    of c2_rpc_items, sorted by some criteria (endpoint,...) */
struct c2_rpc_formation_list {
	/* linkage into list of all classification lists in a c2_rpc_processing */
	struct c2_list_link re_linkage;

	/** listss of c2_rpc_items going to the same endpoint */
	struct c2_list re_items;
	struct c2_net_endpoint *endpoint;
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

/** TBD: add parameters of RPC core layer,
    algorithms should rely on here. */
struct c2_rpc_statistics {
};

/**
   RPC machine is an instance of RPC item (FOP/ADDB) processing context.
   Several such contexts might be existing simultaneously.
 */
struct c2_rpcmachine {
	struct c2_rpc_processing   cr_processing;
	/* XXX: for now: struct c2_rpc_connectivity cr_connectivity; */
	struct c2_rpc_statistics   cr_statistics;
	/** List of rpc connections
	    conn is in list if conn->c_state is not in {CONN_UNINITIALIZED,
	    CONN_FAILED, CONN_TERMINATED} */
	struct c2_list		   cr_rpc_conn_list;
	/** mutex that protects conn_list */
	struct c2_mutex		   cr_session_mutex;
	struct c2_rpc_reply_cache  cr_rcache;
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
   Construct rpcmachine.

   @param machine rpcmachine operation applied to.
   @param dom cob domain that contains cobs representing slots
   @param fol reply items are cached in fol
   @pre c2_rpc_core_init().
   @return 0 success
   @return -ENOMEM failure
 */
int  c2_rpcmachine_init(struct c2_rpcmachine	*machine,
			struct c2_cob_domain	*dom,
			struct c2_fol		*fol);

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
		  struct c2_rpc_item 		*item,
		  enum c2_rpc_item_priority	prio,
		  const c2_time_t		*deadline);

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
			enum c2_rpc_item_priority	prio,
			const c2_time_t 		*deadline);

/**
   Wait for the reply on item being sent.

   @param item rpc item being sent
   @param timeout time to wait for item being sent
   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
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


   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()

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

   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()

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
   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @param machine rpcmachine operation applied to.
   @param prio priority of cache

   @return itmes count in cache selected by priority
 */
size_t c2_rpc_cache_item_count(struct c2_rpcmachine *machine,
			       enum c2_rpc_item_priority prio);

/**
   Returns count of RPC items in processing
   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @param machine rpcmachine operation applied to.

   @return count of RPCs in processing
 */
size_t c2_rpc_rpc_count(struct c2_rpcmachine *machine);

/**
   Returns average time spent in the cache for one RPC-item
   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @param machine rpcmachine operation applied to.
   @param time[out] average time spent in processing on one RPC
 */
void c2_rpc_avg_rpc_item_time(struct c2_rpcmachine *machine,
			      c2_time_t		   *time);

/**
   Returns transmission speed in bytes per second.
   @pre c2_rpc_core_init()
   @pre c2_rpcmachine_init()
   @param machine rpcmachine operation applied to.
 */
size_t c2_rpc_bytes_per_sec(struct c2_rpcmachine *machine);

/** @} end name stat_ifs */


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
