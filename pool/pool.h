/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/15/2010
 */

#pragma once

#ifndef __COLIBRI_LAYOUT_POOL_H__
#define __COLIBRI_LAYOUT_POOL_H__

#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/tlist.h"

/**
   @defgroup pool Storage pools.

   @{
 */

/* import */
struct c2_stob_id;
struct c2_dtm;
struct c2_io_req;
struct c2_dtx;

/* export */
struct c2_pool;
struct c2_poolmach;

struct c2_pool {
	uint32_t            po_width;
	struct c2_poolmach *po_mach;
};

C2_INTERNAL int c2_pool_init(struct c2_pool *pool, uint32_t width);
C2_INTERNAL void c2_pool_fini(struct c2_pool *pool);

/**
   Allocates object id in the pool.

   @post ergo(result == 0, c2_stob_id_is_set(id))
 */
C2_INTERNAL int c2_pool_alloc(struct c2_pool *pool, struct c2_stob_id *id);

/**
   Releases object id back to the pool.

   @pre c2_stob_id_is_set(id)
 */
C2_INTERNAL void c2_pool_put(struct c2_pool *pool, struct c2_stob_id *id);

C2_INTERNAL int c2_pools_init(void);
C2_INTERNAL void c2_pools_fini(void);

/** @} end group pool */


/**
   @defgroup poolmach Pool machine
   @{
*/

/** pool version numer type */
enum c2_poolmach_version {
	PVE_READ,
	PVE_WRITE,
	PVE_NR
};

/**
 * A state that a pool node/device can be in.
 */
enum c2_pool_nd_state {
	/** a node/device is online and serving IO */
	C2_PNDS_ONLINE,

	/** a node/device is considered failed */
	C2_PNDS_FAILED,

	/** a node/device turned off-line by an administrative request */
	C2_PNDS_OFFLINE,

	/** a node/device is active in sns repair. */
	C2_PNDS_SNS_REPAIRING,

	/**
	 * a node/device completed sns repair. Its data is re-constructed
	 * on its corresponding spare space
	 */
	C2_PNDS_SNS_REPAIRED,

	/** a node/device is active in sns re-balance. */
	C2_PNDS_SNS_REBALANCING,

	/**
	 * a node/device completed sns re-rebalance. Its data is copyied
	 * back to its original location. This usually happens when a
	 * new device replaced a failed device and re-balance completed.
	 * After this, the device can be set to ONLINE, and its corresponding
	 * space space can be returned to pool.
	 */
	C2_PNDS_SNS_REBALANCED,

	/** number of state */
	C2_PNDS_NR
};

/**
 * pool node. Data structure representing a node in a pool.
 *
 * Pool node and pool server are two different views of the same physical
 * entity. A pool node is how a server looks "externally" to other nodes.
 * "struct poolnode" represents a particular server on other servers. E.g.,
 * when a new server is added to the pool, "struct poolnode" is created on
 * every server in the pool. "struct poolserver", on the other hand, represents
 * a server state machine locally on the server where it runs.
 *
 * @see pool server
 */
struct c2_poolnode {
	/** pool node state */
	enum c2_pool_nd_state pn_state;

	/** pool node identity */
	struct c2_server     *pn_id;
};

/**
 * pool device
 *
 * Data structure representing a storage device in a pool.
 */
struct c2_pooldev {
	/** device state (as part of pool machine state). This field is only
	    meaningful when c2_pooldev::pd_node.pn_state is PNS_ONLINE */
	enum c2_pool_nd_state pd_state;

	/** pool device identity */
	struct c2_device     *pd_id;

	/* a node this storage devie is attached to */
	struct c2_poolnode   *pd_node;
};

/** event owner type: node or device */
enum c2_pool_event_owner_type {
	C2_POOL_NODE,
	C2_POOL_DEVICE
};

/**
 * pool version numbers vector updated on a failure.
 *
 * Matching pool version numbers must be presented by a client to do IO
 * against the pool. Usually pool version numbers vector is delivered to
 * a client together with a layout or a lock. Version numbers change on
 * failures effectively invalidating layouts affected by the failure.
 *
 */
struct c2_pool_version_numbers {
	uint64_t pvn_version[PVE_NR];
};

enum {
	C2_POOL_EVENTS_LIST_MAGIC = 0x706f6f6c6c696e6bUL, /* poollink */
	C2_POOL_EVENTS_HEAD_MAGIC = 0x706f6f6c68656164UL, /* poolhead */
};

/**
 * Pool Event, which is used to change the state of a node or device.
 */
struct c2_pool_event {
	/** Event owner type */
	enum c2_pool_event_owner_type  pe_type;

	/** Event owner index */
	uint32_t                       pe_index;

	/** new state for this node/device */
	enum c2_pool_nd_state          pe_state;

};

/**
 * This link is used by pool machine to records all state change history.
 * All events hang on the c2_poolmach::pm_events_list, ordered.
 */
struct c2_pool_event_link {
	/** the event itself */
	struct c2_pool_event           pel_event;

	/**
	 * Pool machine's new version when this event handled
	 * This is used by pool machine. If event is generated by
	 * other module and passed to pool machine operations,
	 * it is not used and undefined at that moment.
	 */
	struct c2_pool_version_numbers pel_new_version;

	/**
	 * link to c2_poolmach::pm_events_list.
	 * Used internally in pool machine.
	 */
	struct c2_tlink                pel_linkage;

	uint64_t                       pel_magic;
};

/**
 * Tracking spare slot usage.
 * If spare slot is not used for repair/rebalance, its :psp_device_index is -1.
 */
struct c2_pool_spare_usage {
	/** index of the device to use this spare slot */
	uint32_t psp_device_index;

	/** state of the device to use this spare slot */
	enum c2_pool_nd_state psp_device_state;
};

/**
 * Persistent pool machine state.
 *
 * Copies of this struct are maintained by every node that thinks it is a part
 * of the pool. This state is updated by a quorum protocol.
 *
 * Pool machine state history is recorded in the ::pst_events_list as
 * a ordered collection of events.
 */
struct c2_poolmach_state {
	/** pool machine version numbers */
	struct c2_pool_version_numbers pst_version;

	/** number of nodes currently in the pool */
	uint32_t                       pst_nr_nodes;

	/** identities and states of every node in the pool */
	struct c2_poolnode            *pst_nodes_array;

	/** number of devices currently in the pool */
	uint32_t                       pst_nr_devices;

	/** identities and states of every device in the pool */
	struct c2_pooldev             *pst_devices_array;

	/** maximal number of node failures the pool is configured to sustain */
	uint32_t                       pst_max_node_failures;

	/**
	 * maximal number of device failures the pool is configured to
	 * sustain
	 */
	uint32_t                       pst_max_device_failures;

	/**
	 * Spare slot usage array.
	 * The size of this array is pst_max_device_failures;
	 */
	struct c2_pool_spare_usage    *pst_spare_usage_array;

	/**
	 * All Events ever happened to this pool machine, ordered by time.
	 */
	struct c2_tl                   pst_events_list;
};

/**
 * pool machine. Data structure representing replicated pool state machine.
 *
 * Concurrency control: pool machine state is protected by a single read-write
 * blocking lock. "Normal" operations, e.g., client IO, including degraded mode
 * IO, take this lock in a read mode, because they only inspect pool machine
 * state (e.g., version numbers vector) never modifying it. "Configuration"
 * events such as node or device failures, addition or removal of a node or
 * device and administrative actions against the pool, all took the lock in a
 * write mode.
 */
struct c2_poolmach {
	/** struct c2_persistent_sm  pm_mach; */
	struct c2_poolmach_state pm_state;

	/** this pool machine initialized or not */
	bool                     pm_is_initialised;

	/** read write lock to protect the whole pool machine */
	struct c2_rwlock         pm_lock;
};

C2_INTERNAL bool c2_poolmach_version_equal(const struct c2_pool_version_numbers
					   *v1,
					   const struct c2_pool_version_numbers
					   *v2);

C2_INTERNAL int c2_poolmach_init(struct c2_poolmach *pm,
				 struct c2_dtm *dtm,
				 uint32_t nr_nodes,
				 uint32_t nr_devices,
				 uint32_t max_node_failures,
				 uint32_t max_device_failures);
C2_INTERNAL void c2_poolmach_fini(struct c2_poolmach *pm);

/**
 * Change the pool machine state according to this event.
 *
 * @param event the event to drive the state change. This event
 *        will be copied into pool machine state, and it can
 *        be used or released by caller after call.
 */
C2_INTERNAL int c2_poolmach_state_transit(struct c2_poolmach *pm,
					  struct c2_pool_event *event);

/**
 * Query the state changes between the "from" and "to" version.
 *
 * The caller can apply the returned events to its copy of pool state machine.
 *
 * @param from the least version in the expected region.
 * @param to the most recent version in the expected region.
 * @param event_list_head the state changes in this region will be represented
 *        by events linked in this list.
 */
C2_INTERNAL int c2_poolmach_state_query(struct c2_poolmach *pm,
					const struct c2_pool_version_numbers
					*from,
					const struct c2_pool_version_numbers
					*to, struct c2_tl *event_list_head);

/**
 * Query the current version of a pool state.
 *
 * @param curr the returned current version number stored here.
 */
C2_INTERNAL int c2_poolmach_current_version_get(struct c2_poolmach *pm,
						struct c2_pool_version_numbers
						*curr);

/**
 * Query the current state of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param state_out the output state.
 */
C2_INTERNAL int c2_poolmach_device_state(struct c2_poolmach *pm,
					 uint32_t device_index,
					 enum c2_pool_nd_state *state_out);

/**
 * Query the current state of a specified device.
 * @param pm pool machine.
 * @param node_index the index of the node to query.
 * @param state_out the output state.
 */
C2_INTERNAL int c2_poolmach_node_state(struct c2_poolmach *pm,
				       uint32_t node_index,
				       enum c2_pool_nd_state *state_out);

/**
 * Query the {sns repair, spare slot} pair of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param spare_slot_out the output spair slot.
 */
C2_INTERNAL int c2_poolmach_sns_repair_spare_query(struct c2_poolmach *pm,
						   uint32_t device_index,
						   uint32_t *spare_slot_out);

/**
 * Query the {sns rebalance, spare slot} pair of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param spare_slot_out the output spair slot.
 */
C2_INTERNAL int c2_poolmach_sns_rebalance_spare_query(struct c2_poolmach *pm,
						      uint32_t device_index,
						      uint32_t *spare_slot_out);

/**
 * Return a copy of current pool machine state.
 *
 * The caller may send the current pool machine state to other services or
 * clients. The caller also can store the state in persistent storage.
 * The serialization and un-serialization is determined by caller.
 *
 * Note: The results must be freed by c2_poolmach_state_free().
 *
 * @param state_copy the returned state stored here.
 */
C2_INTERNAL int c2_poolmach_current_state_get(struct c2_poolmach *pm,
					      struct c2_poolmach_state
					      **state_copy);
/**
 * Frees the state copy returned from c2_poolmach_current_state_get().
 */
C2_INTERNAL void c2_poolmach_state_free(struct c2_poolmach *pm,
					struct c2_poolmach_state *state);

C2_TL_DESCR_DECLARE(poolmach_events, C2_EXTERN);
C2_TL_DECLARE(poolmach_events, C2_INTERNAL, struct c2_pool_event_link);
/** @} end of poolmach group */

/**
   @defgroup servermachine Server machine
   @{
*/

/**
   resource limit

   Data structure to describe the fraction of resource usage limitation:
   0  : resource cannot be used at all.
   100: resource can be used entirely without limitation.
   0 < value < 100: fraction of resources can be used.
*/
struct c2_rlimit {
       int rl_processor_throughput;
       int rl_memory;
       int rl_storage_throughput;
       int rl_network_throughput;
};

/**
   pool server

   Pool server represents a pool node plus its state machines, lives locally on
   the server where it runs.

   @see pool node
*/
struct c2_poolserver {
	struct c2_poolnode      ps_node;
	/* struct c2_persistent_sm ps_mach; */
	struct c2_rlimit	ps_rl_usage; /**< the current resource usage */
};

C2_INTERNAL int c2_poolserver_init(struct c2_poolserver *srv);
C2_INTERNAL void c2_poolserver_fini(struct c2_poolserver *srv);
C2_INTERNAL int c2_poolserver_reset(struct c2_poolserver *srv);
C2_INTERNAL int c2_poolserver_on(struct c2_poolserver *srv);
C2_INTERNAL int c2_poolserver_off(struct c2_poolserver *srv);
C2_INTERNAL int c2_poolserver_io_req(struct c2_poolserver *srv,
				     struct c2_io_req *req);
C2_INTERNAL int c2_poolserver_device_join(struct c2_poolserver *srv,
					  struct c2_pooldev *dev);
C2_INTERNAL int c2_poolserver_device_leave(struct c2_poolserver *srv,
					   struct c2_pooldev *dev);

/** @} end of servermachine group */


/* __COLIBRI_LAYOUT_POOL_H__ */
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
