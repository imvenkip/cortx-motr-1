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

#ifndef __MERO_POOL_POOL_H__
#define __MERO_POOL_POOL_H__

#include "be/obj.h"      /* m0_be_obj_header */
#include "lib/rwlock.h"
#include "lib/tlist.h"
#include "be/be.h" /* struct m0_be_tx */

/**
   @defgroup pool Storage pools.

   @{
 */

/* import */
struct m0_dtm;
struct m0_io_req;
struct m0_dtx;
struct m0_be_tx_credit;

/* export */
struct m0_pool;
struct m0_poolmach;

enum {
	PM_DEFAULT_NR_NODES = 10,
	PM_DEFAULT_NR_DEV = 80,
	PM_DEFAULT_MAX_NODE_FAILURES = 1,
	PM_DEFAULT_MAX_DEV_FAILURES = 80
};

struct m0_pool {
	uint32_t            po_width;
	struct m0_poolmach *po_mach;
};

M0_INTERNAL int m0_pool_init(struct m0_pool *pool, uint32_t width);
M0_INTERNAL void m0_pool_fini(struct m0_pool *pool);

M0_INTERNAL int m0_pools_init(void);
M0_INTERNAL void m0_pools_fini(void);

/** @} end group pool */


/**
   @defgroup poolmach Pool machine
   @{
*/

/** pool version numer type */
enum m0_poolmach_version {
	PVE_READ,
	PVE_WRITE,
	PVE_NR
};

/**
 * A state that a pool node/device can be in.
 */
enum m0_pool_nd_state {
	/** a node/device is online and serving IO */
	M0_PNDS_ONLINE,

	/** a node/device is considered failed */
	M0_PNDS_FAILED,

	/** a node/device turned off-line by an administrative request */
	M0_PNDS_OFFLINE,

	/** a node/device is active in sns repair. */
	M0_PNDS_SNS_REPAIRING,

	/**
	 * a node/device completed sns repair. Its data is re-constructed
	 * on its corresponding spare space
	 */
	M0_PNDS_SNS_REPAIRED,

	/** a node/device is active in sns re-balance. */
	M0_PNDS_SNS_REBALANCING,

	/** number of state */
	M0_PNDS_NR
};

enum {
	/** Unused spare slot has this device index */
	POOL_PM_SPARE_SLOT_UNUSED = 0xffffffff
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
struct m0_poolnode {
	struct m0_be_obj_header pn_header;
	enum m0_pool_nd_state   pn_state;
	char                    pn_pad[4];
	struct m0_server       *pn_id;
	struct m0_be_obj_footer pn_footer;
};
M0_BASSERT(sizeof(enum m0_pool_nd_state) == 4);

/**
 * pool device
 *
 * Data structure representing a storage device in a pool.
 */
struct m0_pooldev {
	struct m0_be_obj_header pd_header;
	/** device state (as part of pool machine state). This field is only
	    meaningful when m0_pooldev::pd_node.pn_state is PNS_ONLINE */
	enum m0_pool_nd_state   pd_state;
	char                    pd_pad[4];
	/** pool device identity */
	struct m0_device       *pd_id;
	/* a node this storage devie is attached to */
	struct m0_poolnode     *pd_node;
	struct m0_be_obj_footer pd_footer;
};

/** event owner type: node or device */
enum m0_pool_event_owner_type {
	M0_POOL_NODE,
	M0_POOL_DEVICE
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
struct m0_pool_version_numbers {
	uint64_t pvn_version[PVE_NR];
};

/**
 * Pool Event, which is used to change the state of a node or device.
 */
struct m0_pool_event {
	/** Event owner type */
	enum m0_pool_event_owner_type  pe_type;

	/** Event owner index */
	uint32_t                       pe_index;

	/** new state for this node/device */
	enum m0_pool_nd_state          pe_state;

};

/**
 * This link is used by pool machine to records all state change history.
 * All events hang on the m0_poolmach::pm_events_list, ordered.
 */
struct m0_pool_event_link {
	/** the event itself */
	struct m0_pool_event           pel_event;

	/**
	 * Pool machine's new version when this event handled
	 * This is used by pool machine. If event is generated by
	 * other module and passed to pool machine operations,
	 * it is not used and undefined at that moment.
	 */
	struct m0_pool_version_numbers pel_new_version;

	/**
	 * link to m0_poolmach::pm_events_list.
	 * Used internally in pool machine.
	 */
	struct m0_tlink                pel_linkage;

	uint64_t                       pel_magic;
};

/**
 * Tracking spare slot usage.
 * If spare slot is not used for repair/rebalance, its :psp_device_index is -1.
 */
struct m0_pool_spare_usage {
	struct m0_be_obj_header psu_header;
	/** index of the device to use this spare slot */
	uint32_t                psu_device_index;

	/** state of the device to use this spare slot */
	enum m0_pool_nd_state   psu_device_state;
	struct m0_be_obj_footer psu_footer;
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
struct m0_poolmach_state {
	struct m0_be_obj_header        pst_header;
	/** pool machine version numbers */
	struct m0_pool_version_numbers pst_version;

	/** identities and states of every node in the pool */
	struct m0_poolnode            *pst_nodes_array;

	/** identities and states of every device in the pool */
	struct m0_pooldev             *pst_devices_array;

	/** number of nodes currently in the pool */
	uint32_t                       pst_nr_nodes;

	/** number of devices currently in the pool */
	uint32_t                       pst_nr_devices;

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
	struct m0_pool_spare_usage    *pst_spare_usage_array;

	/**
	 * All Events ever happened to this pool machine, ordered by time.
	 */
	struct m0_tl                   pst_events_list;
	struct m0_be_obj_footer        pst_footer;
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
struct m0_poolmach {
	/** struct m0_persistent_sm  pm_mach; */
	struct m0_poolmach_state *pm_state;

	/** the be_seg. if this is NULL, the poolmach is on client. */
	struct m0_be_seg         *pm_be_seg;

	struct m0_sm_group       *pm_sm_grp;

	/** this pool machine initialized or not */
	bool                      pm_is_initialised;

	/** read write lock to protect the whole pool machine */
	struct m0_rwlock          pm_lock;
};

M0_INTERNAL bool
m0_poolmach_version_equal(const struct m0_pool_version_numbers *v1,
			  const struct m0_pool_version_numbers *v2);
/**
 * Pool Machine version numbers are incremented upon event.
 * v1 before v2 means v2's version number is greater than v1's.
 */
M0_INTERNAL bool
m0_poolmach_version_before(const struct m0_pool_version_numbers *v1,
			   const struct m0_pool_version_numbers *v2);
/**
 * Initialises the pool machine.
 *
 * Pool machine will load its data from persistent storage. If this is the first
 * call, it will initialise the persistent data.
 */
M0_INTERNAL int m0_poolmach_init(struct m0_poolmach *pm,
				 struct m0_be_seg   *be_seg,
				 struct m0_sm_group *sm_grp,
				 struct m0_dtm      *dtm,
				 uint32_t            nr_nodes,
				 uint32_t            nr_devices,
				 uint32_t            max_node_failures,
				 uint32_t            max_device_failures);

/**
 * Finalises the pool machine.
 */
M0_INTERNAL void m0_poolmach_fini(struct m0_poolmach *pm);

/**
 * Calculate poolmach credit.
 */
M0_INTERNAL void m0_poolmach_store_credit(struct m0_poolmach        *pm,
					  struct m0_be_tx_credit *accum);

/**
 * Change the pool machine state according to this event.
 *
 * @param event the event to drive the state change. This event
 *        will be copied into pool machine state, and it can
 *        be used or released by caller after call.
 */
M0_INTERNAL int m0_poolmach_state_transit(struct m0_poolmach         *pm,
					  const struct m0_pool_event *event,
					  struct m0_be_tx            *tx);

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
M0_INTERNAL int m0_poolmach_state_query(struct m0_poolmach *pm,
					const struct m0_pool_version_numbers
					*from,
					const struct m0_pool_version_numbers
					*to, struct m0_tl *event_list_head);

/**
 * Query the current version of a pool state.
 *
 * @param curr the returned current version number stored here.
 */
M0_INTERNAL int m0_poolmach_current_version_get(struct m0_poolmach *pm,
						struct m0_pool_version_numbers
						*curr);

/**
 * Query the current state of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param state_out the output state.
 */
M0_INTERNAL int m0_poolmach_device_state(struct m0_poolmach *pm,
					 uint32_t device_index,
					 enum m0_pool_nd_state *state_out);

/**
 * Query the current state of a specified device.
 * @param pm pool machine.
 * @param node_index the index of the node to query.
 * @param state_out the output state.
 */
M0_INTERNAL int m0_poolmach_node_state(struct m0_poolmach *pm,
				       uint32_t node_index,
				       enum m0_pool_nd_state *state_out);


/**
 * Returns true if device is in the spare usage array of pool machine.
 * @param pm Pool machine pointer in which spare usage array is populated.
 * @param device_index Index of device which needs to be searched.
 */
M0_INTERNAL bool
m0_poolmach_device_is_in_spare_usage_array(struct m0_poolmach *pm,
					   uint32_t device_index);

/**
 * Query the {sns repair, spare slot} pair of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param spare_slot_out the output spair slot.
 */
M0_INTERNAL int m0_poolmach_sns_repair_spare_query(struct m0_poolmach *pm,
						   uint32_t device_index,
						   uint32_t *spare_slot_out);

/**
 * Returns true if the spare slot now contains data. This case would be true
 * when repair has already been invoked atleast once, due to which some failed
 * data unit has been repaired onto the given spare slot.
 * @param pm pool machine.
 * @param spare_slot the slot index which needs to be checked.
 * @param check_state check the device state before making the decision.
 */
M0_INTERNAL bool
m0_poolmach_sns_repair_spare_contains_data(struct m0_poolmach *pm,
					   uint32_t spare_slot,
					   bool check_state);


/**
 * Query the {sns rebalance, spare slot} pair of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param spare_slot_out the output spair slot.
 */
M0_INTERNAL int m0_poolmach_sns_rebalance_spare_query(struct m0_poolmach *pm,
						      uint32_t device_index,
						      uint32_t *spare_slot_out);

/**
 * Return a copy of current pool machine state.
 *
 * The caller may send the current pool machine state to other services or
 * clients. The caller also can store the state in persistent storage.
 * The serialization and un-serialization is determined by caller.
 *
 * Note: The results must be freed by m0_poolmach_state_free().
 *
 * @param state_copy the returned state stored here.
 */
M0_INTERNAL int m0_poolmach_current_state_get(struct m0_poolmach *pm,
					      struct m0_poolmach_state
					      **state_copy);
/**
 * Frees the state copy returned from m0_poolmach_current_state_get().
 */
M0_INTERNAL void m0_poolmach_state_free(struct m0_poolmach *pm,
					struct m0_poolmach_state *state);

M0_TL_DESCR_DECLARE(poolmach_events, M0_EXTERN);
M0_TL_DECLARE(poolmach_events, M0_INTERNAL, struct m0_pool_event_link);
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
struct m0_rlimit {
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
struct m0_poolserver {
	struct m0_poolnode      ps_node;
	/* struct m0_persistent_sm ps_mach; */
	struct m0_rlimit	ps_rl_usage; /**< the current resource usage */
};

M0_INTERNAL int m0_poolserver_init(struct m0_poolserver *srv);
M0_INTERNAL void m0_poolserver_fini(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_reset(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_on(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_off(struct m0_poolserver *srv);
M0_INTERNAL int m0_poolserver_io_req(struct m0_poolserver *srv,
				     struct m0_io_req *req);
M0_INTERNAL int m0_poolserver_device_join(struct m0_poolserver *srv,
					  struct m0_pooldev *dev);
M0_INTERNAL int m0_poolserver_device_leave(struct m0_poolserver *srv,
					   struct m0_pooldev *dev);

M0_INTERNAL void m0_poolmach_version_dump(struct m0_pool_version_numbers *v);
M0_INTERNAL void m0_poolmach_event_dump(struct m0_pool_event *e);
M0_INTERNAL void m0_poolmach_event_list_dump(struct m0_poolmach *pm);
M0_INTERNAL void m0_poolmach_device_state_dump(struct m0_poolmach *pm);

/**
 * State of SNS repair with respect to given global fid.
 * Used during degraded mode write IO.
 * During normal IO, the UNINITIALIZED enum value is used.
 * The next 2 states are used during degraded mode write IO.
 */
enum sns_repair_state {
	/**
	 * Used by IO requests done during healthy state of storage pool.
	 * Initialized to -1 in order to sync it with output of API
	 * m0_sns_cm_fid_repair_done().
	 * */
	SRS_UNINITIALIZED = 1,

	/**
	 * Assumes a distributed lock has been acquired on the associated
	 * global fid and SNS repair is yet to start on given global fid.
	 */
	SRS_REPAIR_NOTDONE,

	/**
	 * Assumes a distributed lock has been acquired on associated
	 * global fid and SNS repair has completed for given fid.
	 */
	SRS_REPAIR_DONE,

	SRS_NR,
};

/** @} end of servermachine group */
#endif /* __MERO_POOL_POOL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
