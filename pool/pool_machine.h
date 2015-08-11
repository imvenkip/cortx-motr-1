/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <hua.huang@seagate.com>
 * Revision       : Mandar Sawant <mandar.sawant@seagate.com>
 * Original creation date: 05/01/2015
 */

#pragma once

#ifndef __MERO_POOL_MACHINE_H__
#define __MERO_POOL_MACHINE_H__

#include "lib/tlist.h"

/**
   @defgroup poolmach Pool machine
   @{
*/

/* import */
struct m0_dtm;
struct m0_dtx;
struct m0_be_tx_credit;
struct m0_poolnode;
struct m0_pooldev;
struct m0_pool_spare_usage;
struct m0_poolmach_event;
struct m0_poolmach_event_link;

/** pool version numer type */
enum m0_poolmach_version {
	PVE_READ,
	PVE_WRITE,
	PVE_NR
};

enum {
	/* Unused spare slot has this device index */
	POOL_PM_SPARE_SLOT_UNUSED = 0xFFFFFFFF
};

/**
 * Pool version numbers vector updated on a failure.
 *
 * Matching pool version numbers must be presented by a client to do IO
 * against the pool. Usually pool version numbers vector is delivered to
 * a client together with a layout or a lock. Version numbers change on
 * failures effectively invalidating layouts affected by the failure.
 *
 */
struct m0_poolmach_versions {
	uint64_t pvn_version[PVE_NR];
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
	/** Pool machine version numbers. */
	struct m0_poolmach_versions pst_version;

	/** Number of nodes currently in the pool. */
	uint32_t                    pst_nr_nodes;

	/** Identities and states of every node in the pool. */
	struct m0_poolnode         *pst_nodes_array;

	/** Number of devices currently in the pool. */
	uint32_t                    pst_nr_devices;

	/** Identities and states of every device in the pool. */
	struct m0_pooldev          *pst_devices_array;

	/**
	 * Maximal number of node failures the pool is configured to
	 * sustain.
	 */
	uint32_t                    pst_max_node_failures;

	/**
	 * Maximal number of device failures the pool is configured to
	 * sustain.
	 */
	uint32_t                    pst_max_device_failures;

	/**
	 * Spare slot usage array.
	 * The size of this array is pst_max_device_failures.
	 */
	struct m0_pool_spare_usage *pst_spare_usage_array;

	/**
	 * All Events ever happened to this pool machine, ordered by time.
	 */
	struct m0_tl                pst_events_list;
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
	struct m0_poolmach_state *pm_state;

	/** The be_seg. If this is NULL, the poolmach is on client. */
	struct m0_be_seg          *pm_be_seg;

	struct m0_sm_group        *pm_sm_grp;

	/** Current pool version associated with this pool machine. */
	struct m0_pool_version    *pm_pver;

	/** This pool machine initialized or not. */
	bool                       pm_is_initialised;

	/** Read write lock to protect the whole pool machine. */
	struct m0_rwlock           pm_lock;
};

/** Event owner type, node or device. */
enum m0_poolmach_event_owner_type {
        M0_POOL_NODE,
        M0_POOL_DEVICE
};

/**
 * Pool event that is used to change the state of a node or device.
 */
struct m0_poolmach_event {
        /** Event owner type. */
        enum m0_poolmach_event_owner_type  pe_type;

        /** Event owner index. */
        uint32_t                           pe_index;

        /** New state for this node or device. */
        uint32_t                           pe_state;
};

/**
 * This link is used by pool machine to record all state change history.
 * All events hang on the m0_poolmach::pm_events_list, ordered.
 */
struct m0_poolmach_event_link {
        /** The event itself. */
        struct m0_poolmach_event    pel_event;

        /**
         * Pool machine's new version when this event handled
         * This is used by pool machine. If event is generated by
         * other module and passed to pool machine operations,
         * it is not used and undefined at that moment.
         */
        struct m0_poolmach_versions pel_new_version;

        /**
         * Link into m0_poolmach::pm_events_list.
         * Used internally in pool machine.
         */
        struct m0_tlink             pel_linkage;

        uint64_t                    pel_magic;
};

M0_INTERNAL bool
m0_poolmach_version_equal(const struct m0_poolmach_versions *v1,
			  const struct m0_poolmach_versions *v2);
/**
 * Pool Machine version numbers are incremented upon event.
 * v1 before v2 means v2's version number is greater than v1's.
 */
M0_INTERNAL bool
m0_poolmach_version_before(const struct m0_poolmach_versions *v1,
			   const struct m0_poolmach_versions *v2);
/**
 * Initialises the pool machine.
 *
 * Pool machine will load its data from persistent storage. If this is the first
 * call, it will initialise the persistent data.
 */
M0_INTERNAL int m0_poolmach_init(struct m0_poolmach *pm,
				 struct m0_be_seg *be_seg,
				 struct m0_sm_group *sm_grp,
				 struct m0_dtm *dtm,
				 uint32_t nr_nodes,
				 uint32_t nr_devices,
				 uint32_t max_node_failures,
				 uint32_t max_device_failures);

M0_INTERNAL int m0_poolmach_init_by_conf(struct m0_poolmach *pm,
					 struct m0_conf_pver *pver);

/**
 * Finalises the pool machine.
 */
M0_INTERNAL void m0_poolmach_fini(struct m0_poolmach *pm);

/**
 * Calculate poolmach credit.
 */
M0_INTERNAL void m0_poolmach_store_credit(struct m0_poolmach *pm,
					  struct m0_be_tx_credit *accum);

/**
 * Change the pool machine state according to this event.
 *
 * @param event the event to drive the state change. This event
 *        will be copied into pool machine state, and it can
 *        be used or released by caller after call.
 */
M0_INTERNAL int m0_poolmach_state_transit(struct m0_poolmach *pm,
					  const struct m0_poolmach_event *event,
					  struct m0_be_tx *tx);

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
					const struct m0_poolmach_versions *from,
					const struct m0_poolmach_versions *to,
					struct m0_tl *event_list_head);

/**
 * Query the current version of a pool state.
 *
 * @param curr the returned current version number stored here.
 */
M0_INTERNAL int
m0_poolmach_current_version_get(struct m0_poolmach *pm,
				struct m0_poolmach_versions *curr);

/**
 * Query the current state of a specified device.
 * @param pm pool machine.
 * @param device_index the index of the device to query.
 * @param state_out the output state.
 */
M0_INTERNAL int m0_poolmach_device_state(struct m0_poolmach *pm,
					 uint32_t device_index,
					 uint32_t *state_out);

/**
 * Query the current state of a specified device.
 * @param pm pool machine.
 * @param node_index the index of the node to query.
 * @param state_out the output state.
 */
M0_INTERNAL int m0_poolmach_node_state(struct m0_poolmach *pm,
				       uint32_t node_index,
				       uint32_t *state_out);


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

M0_INTERNAL void m0_poolmach_version_dump(struct m0_poolmach_versions *v);
M0_INTERNAL void m0_poolmach_event_dump(struct m0_poolmach_event *e);
M0_INTERNAL void m0_poolmach_event_list_dump(struct m0_poolmach *pm);
M0_INTERNAL void m0_poolmach_device_state_dump(struct m0_poolmach *pm);
M0_INTERNAL uint64_t m0_poolmach_nr_dev_failures(struct m0_poolmach *pm);

M0_TL_DESCR_DECLARE(poolmach_events, M0_EXTERN);
M0_TL_DECLARE(poolmach_events, M0_INTERNAL, struct m0_poolmach_event_link);
/** @} end of poolmach group */
#endif /* __MERO_POOL_PVER_MACHINE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

