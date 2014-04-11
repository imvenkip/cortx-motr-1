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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/15/2010
 */

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"      /* M0_LOG */
#include "lib/errno.h"
#include "lib/memory.h"
#include "pool/pool.h"
#include "pool/pool_fops.h"
#include "lib/misc.h"

/**
   @page pool_mach_store_replica DLD of Pool Machine store and replica

   - @ref pool_mach_store_replica-ovw
   - @ref pool_mach_store_replica-def
   - @ref pool_mach_store_replica-req
   - @ref pool_mach_store_replica-depends
   - @ref pool_mach_store_replica-fspec
   - @ref pool_mach_store_replica-lspec
      - @ref pool_mach_store_replica-lspec-comps
      - @ref pool_mach_store_replica-lspec-ds
   - @ref pool_mach_store_replica-conformance
   - @ref pool_mach_store_replica-ut
   - @ref pool_mach_store_replica-st
   - @ref pool_mach_store_replica-O
   - @ref pool_mach_store_replica-ref

   <hr>
   @section pool_mach_store_replica-ovw Overview
   Pool Machine state is stored on persistent storage. When system initializese
   the first time, pool machine state is configured from confc & confd. When
   system restarts, ioservice loads pool machine state data from persistent
   storage.

   Every ioservice has its own pool machine state stored on persistent storage.
   They are independent. Pool machine state transitions are triggered by pool
   machine events. Pool machine events, e.g. device failure event, SNS
   completion event, are delivered to all ioservices. So, these pool machine
   states are updated and the final pool machine states are identical on all
   nodes.

   Pool machine state is stored in BE.

   <hr>
   @section pool_mach_store_replica-def Definitions
   @see poolmach for failure vectors, events, etc.

   <hr>
   @section pool_mach_store_replica-req Requirements
   The following requirements should be meet:
   - @b R.DLD.P All pool machine states should be stored on persistent storage.
                When system restarts again, these states are loaded from
                persistent storage.
   - @b R.DLD.T Updates to the persistent storage should survive system failure.

   <hr>
   @section pool_mach_store_replica-depends Dependencies
   FOP.
   DTM.
   BE.
   Pool.
   Pool Machine.

   <hr>
   @section pool_mach_store_replica-fspec Functional Specification
   Pool Machine states, including failure vectors, event lists, spare slots
   usages, etc. are stored in BE. Updates to these storage will be
   protected by distributed transaction manager.

   Every ioservice has a pool machine. Pool machine update events are delivered
   to all ioservices. So all the pool machine there get state transitions
   according to these events. Finally the pool machine states on all ioservice
   nodes are persistent and identical.

   HA component broadcasts every event to all the pool machines in the cluster
   using m0poolmach utility. HA components can also use m0poolmach utility to
   query the current status of pool machine.

   HA component or Administrator may trigger SNS repair and/or SNS rebalance
   operation using m0repair utility. HA component or Administrator decide
   when to trigger these operations upon various conditions, like failure
   type, the elapsed time from failure detected, etc.

   <hr>
   @section pool_mach_store_replica-lspec Logical Specification

   - @ref pool_mach_store_replica-lspec-comps
   - @ref pool_mach_store_replica-lspec-ds
   - @ref pool_mach_store_replica-lspec-if

   @subsection pool_mach_store_replica-lspec-comps Component Overview
   Pool machine global information, like number of devices and number of nodes in
   this pool, current read and write version numbers are stored on persistent
   storage.

   Every pool machine contains an array of struct m0_pooldev. Every instance of
   struct m0_pooldev represents a device in the pool and maintains the
   corresponding device state. This device informatin represented by instance of
   struct m0_pooldev is written to persistent store as a record. Similarly, the
   node in the pool is represented by struct m0_poolnode. This is also written
   to persistent store as a record. The spare space usage in the pool, accounted
   by struct m0_pool_spare_usage, is recorded to persistent store. Every event
   is stored as a separate record on the persistent storage and its
   corresponding version numbers are used as the record keys to query or update
   a particular record on the persistent store.

   All events are stored on persistent storage. Each event is stored as a
   separate record, and its version number is key.

   @subsection pool_mach_store_replica-lspec-ds Data Structures
   The data structures of failure vector, failure vector version number,
   event, event list are in @ref poolmach module.

   The DB records for global pool state, state of node, state of device,
   events are defined in code. Struct m0_pool_version_numbers is used as keys
   for the above data structures.

   @subsection pool_mach_store_replica-lspec-if Interfaces
   The failure vector and version number operations are designed and listed
   in @ref poolmach.
   No new external interfaces are introduced by this feature. To implement
   the data store on persistent storage, BE interfaces are used.
   To send and handle pool machine update fop, rpc/reqh interfaces will be
   used.

   <hr>
   @section pool_mach_store_replica-conformance Conformance
   - @b I.DLD.P All pool machine states are stored on persistent storage, using
		BE interfaces. This states data can be loaded when
		system re-starts.
   - @b I.DLD.T Updates to the persistent storage will be protected by
		distributed transaction manager. This will insure the updates
		can survive system failures.

   <hr>
   @section pool_mach_store_replica-ut Unit Tests
   Unit test will cover the following case:
   - init BE storage.
   - updates to BE.
   - load from BE.
   - Sending updates to replicas.
   - Replicas handles updates from master.

   <hr>
   @section pool_mach_store_replica-st System Tests
   Pool machine and its replicas works well when new pool machine events happen.
   Pool machine works well when system re-starts.

   <hr>
   @section pool_mach_store_replica-O Analysis
   N/A

   <hr>
   @section pool_mach_store_replica-ref References
   - @ref cm
   - @ref agents
   - @ref poolmach
   - @ref DB
   - @ref BE
 */

/**
   @addtogroup pool

   @{
 */

/* Import following interfaces which are defined in pool/pool_store.c */
M0_INTERNAL int m0_poolmach_store_init(struct m0_poolmach *pm,
				       struct m0_be_seg   *be_seg,
				       struct m0_sm_group *sm_grp,
				       struct m0_dtm      *dtm,
				       uint32_t            nr_nodes,
				       uint32_t            nr_devices,
				       uint32_t            max_node_failures,
				       uint32_t            max_device_failures);
M0_INTERNAL int m0_poolmach_store(struct m0_poolmach        *pm,
				  struct m0_be_tx           *tx,
				  struct m0_pool_event_link *event_link);

M0_TL_DESCR_DEFINE(poolmach_events, "pool machine events list", M0_INTERNAL,
                   struct m0_pool_event_link, pel_linkage, pel_magic,
                   M0_POOL_EVENTS_LIST_MAGIC, M0_POOL_EVENTS_HEAD_MAGIC);

M0_TL_DEFINE(poolmach_events, M0_INTERNAL, struct m0_pool_event_link);

M0_INTERNAL int m0_pool_init(struct m0_pool *pool, uint32_t width)
{
	pool->po_width = width;
	return 0;
}

M0_INTERNAL void m0_pool_fini(struct m0_pool *pool)
{
}

M0_INTERNAL int m0_pools_init(void)
{
#ifndef __KERNEL__
	return m0_poolmach_fop_init();
#else
	return 0;
#endif
}

M0_INTERNAL void m0_pools_fini(void)
{
#ifndef __KERNEL__
	m0_poolmach_fop_fini();
#endif
}

M0_INTERNAL bool m0_poolmach_version_equal(const struct m0_pool_version_numbers
					   *v1,
					   const struct m0_pool_version_numbers
					   *v2)
{
	return !memcmp(v1, v2, sizeof *v1);
}

M0_INTERNAL bool m0_poolmach_version_before(const struct m0_pool_version_numbers
					    *v1,
					    const struct m0_pool_version_numbers
					    *v2)
{
	return
		v1->pvn_version[PVE_READ]  < v2->pvn_version[PVE_READ] ||
		v1->pvn_version[PVE_WRITE] < v2->pvn_version[PVE_WRITE];
}

M0_INTERNAL int m0_poolmach_init(struct m0_poolmach *pm,
				 struct m0_be_seg   *be_seg,
				 struct m0_sm_group *sm_grp,
				 struct m0_dtm      *dtm,
				 uint32_t            nr_nodes,
				 uint32_t            nr_devices,
				 uint32_t            max_node_failures,
				 uint32_t            max_device_failures)
{
	uint32_t i;
	int      rc = 0;
	M0_PRE(!pm->pm_is_initialised);

	M0_SET0(pm);
	m0_rwlock_init(&pm->pm_lock);
	pm->pm_be_seg = be_seg;
	pm->pm_sm_grp = sm_grp;

	if (be_seg == NULL) {
		struct m0_poolmach_state *state;
		/* This is On client, be_seg is NULL. */
		M0_ALLOC_PTR(state);
		if (state == NULL)
			return -ENOMEM;

		state->pst_version.pvn_version[PVE_READ]  = 0;
		state->pst_version.pvn_version[PVE_WRITE] = 0;
		state->pst_nr_nodes            = nr_nodes;
		/* nr_devices io devices and 1 md device. md uses container 0 */
		state->pst_nr_devices          = nr_devices + 1;
		state->pst_max_node_failures   = max_node_failures;
		state->pst_max_device_failures = max_device_failures;

		M0_ALLOC_ARR(state->pst_nodes_array, state->pst_nr_nodes);
		M0_ALLOC_ARR(state->pst_devices_array,
			     state->pst_nr_devices);
		M0_ALLOC_ARR(state->pst_spare_usage_array,
			     state->pst_max_device_failures);
		if (state->pst_nodes_array == NULL ||
		    state->pst_devices_array == NULL ||
		    state->pst_spare_usage_array == NULL) {
			/* m0_free(NULL) is valid */
			m0_free(state->pst_nodes_array);
			m0_free(state->pst_devices_array);
			m0_free(state->pst_spare_usage_array);
			m0_free(state);
			return -ENOMEM;
		}

		for (i = 0; i < state->pst_nr_nodes; i++) {
			state->pst_nodes_array[i].pn_state = M0_PNDS_ONLINE;
			/* TODO use real node id */
			state->pst_nodes_array[i].pn_id    = NULL;
		}

		for (i = 0; i < state->pst_nr_devices; i++) {
			state->pst_devices_array[i].pd_state = M0_PNDS_ONLINE;
			/* TODO use real device id */
			state->pst_devices_array[i].pd_id    = NULL;
			state->pst_devices_array[i].pd_node  = NULL;
		}

		for (i = 0; i < state->pst_max_device_failures; i++) {
			/* -1 means that this spare slot is not used */
			state->pst_spare_usage_array[i].psu_device_index =
						POOL_PM_SPARE_SLOT_UNUSED;
		}
		poolmach_events_tlist_init(&state->pst_events_list);
		pm->pm_state = state;
	} else {
		/* This is On server,  be_seg must be valid*/
		rc = m0_poolmach_store_init(pm, be_seg, sm_grp, dtm, nr_nodes,
					    nr_devices, max_node_failures,
					    max_device_failures);
	}
	if (rc == 0)
		pm->pm_is_initialised = true;
	else
		m0_poolmach_fini(pm);
	return rc;
}

M0_INTERNAL void m0_poolmach_fini(struct m0_poolmach *pm)
{
	struct m0_pool_event_link *scan;
	struct m0_poolmach_state  *state = pm->pm_state;

	M0_PRE(pm != NULL);

	m0_rwlock_write_lock(&pm->pm_lock);

	if (pm->pm_be_seg == NULL) {
		/* On client: iterate through events and free them */
		m0_tl_for(poolmach_events, &state->pst_events_list, scan) {
			poolmach_events_tlink_del_fini(scan);
			m0_free(scan);
		} m0_tl_endfor;

		m0_free(state->pst_spare_usage_array);
		m0_free(state->pst_devices_array);
		m0_free(state->pst_nodes_array);
		m0_free0(&pm->pm_state);
	}
	m0_rwlock_write_unlock(&pm->pm_lock);

	pm->pm_is_initialised = false;
	m0_rwlock_fini(&pm->pm_lock);
}

/**
 * The state transition path:
 *
 *       +--------> OFFLINE
 *       |            |
 *       |            |
 *       v            v
 *    ONLINE -----> FAILED --> SNS_REPAIRING --> SNS_REPAIRED
 *       ^                                            |
 *       |                                            |
 *       |                                            v
 *       |                                       SNS_REBALANCING
 *       |                                            |
 *       ^                                            |
 *       |                                            v
 *       +------------------<-------------------------+
 */
M0_INTERNAL int m0_poolmach_state_transit(struct m0_poolmach         *pm,
					  const struct m0_pool_event *event,
					  struct m0_be_tx            *tx)
{
	struct m0_poolmach_state   *state;
	struct m0_pool_spare_usage *spare_array;
	struct m0_pool_event_link   event_link;
	enum m0_pool_nd_state       old_state = M0_PNDS_FAILED;
	int                         rc = 0;
	int                         i;

	M0_PRE(pm != NULL);
	M0_PRE(event != NULL);

	M0_SET0(&event_link);
	state = pm->pm_state;

	if (!M0_IN(event->pe_type, (M0_POOL_NODE, M0_POOL_DEVICE)))
		return -EINVAL;

	if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE,
				     M0_PNDS_FAILED,
				     M0_PNDS_OFFLINE,
				     M0_PNDS_SNS_REPAIRING,
				     M0_PNDS_SNS_REPAIRED,
				     M0_PNDS_SNS_REBALANCING)))
		return -EINVAL;

	if ((event->pe_type == M0_POOL_NODE &&
	     event->pe_index >= state->pst_nr_nodes) ||
	    (event->pe_type == M0_POOL_DEVICE &&
	     event->pe_index >= state->pst_nr_devices))
		return -EINVAL;

	if (event->pe_type == M0_POOL_NODE) {
		old_state = state->pst_nodes_array[event->pe_index].pn_state;
	} else if (event->pe_type == M0_POOL_DEVICE) {
		old_state =
			state->pst_devices_array[event->pe_index].pd_state;
	}

	switch (old_state) {
	case M0_PNDS_ONLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_OFFLINE,
					     M0_PNDS_FAILED)))
			return -EINVAL;
		break;
	case M0_PNDS_OFFLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE)))
			return -EINVAL;
		break;
	case M0_PNDS_FAILED:
		if (!M0_IN(event->pe_state, (M0_PNDS_SNS_REPAIRING)))
			return -EINVAL;
		break;
	case M0_PNDS_SNS_REPAIRING:
		if (!M0_IN(event->pe_state, (M0_PNDS_SNS_REPAIRED)))
			return -EINVAL;
		break;
	case M0_PNDS_SNS_REPAIRED:
		if (!M0_IN(event->pe_state, (M0_PNDS_SNS_REBALANCING)))
			return -EINVAL;
		break;
	case M0_PNDS_SNS_REBALANCING:
		if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE)))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* Step 1: lock the poolmach */
	m0_rwlock_write_lock(&pm->pm_lock);

	/* step 2: Update the state according to event */
	event_link.pel_event = *event;
	if (event->pe_type == M0_POOL_NODE) {
		/* TODO if this is a new node join event, the index
		 * might larger than the current number. Then we need
		 * to create a new larger array to hold nodes info.
		 */
		state->pst_nodes_array[event->pe_index].pn_state =
				event->pe_state;
	} else if (event->pe_type == M0_POOL_DEVICE) {
		state->pst_devices_array[event->pe_index].pd_state =
				event->pe_state;
	}

	/* step 3: Increase the version */
	++ state->pst_version.pvn_version[PVE_READ];
	++ state->pst_version.pvn_version[PVE_WRITE];

	/* Step 4: copy new version into event */
	event_link.pel_new_version = state->pst_version;

	/* Step 5: Alloc or free a spare slot if necessary.*/
	spare_array = state->pst_spare_usage_array;
	switch (event->pe_state) {
	case M0_PNDS_ONLINE:
		/* clear spare slot usage if it is from rebalancing */
		for (i = 0; i < state->pst_max_device_failures; i++) {
			if (spare_array[i].psu_device_index == event->pe_index){
				M0_ASSERT(M0_IN(spare_array[i].psu_device_state,
					  (M0_PNDS_OFFLINE,
					   M0_PNDS_SNS_REBALANCING)));
				spare_array[i].psu_device_index =
						POOL_PM_SPARE_SLOT_UNUSED;
				break;
			}
		}
		break;
	case M0_PNDS_FAILED:
		/* alloc a sns repare spare slot */
		for (i = 0; i < state->pst_max_device_failures; i++) {
			if (spare_array[i].psu_device_index ==
						POOL_PM_SPARE_SLOT_UNUSED) {
				spare_array[i].psu_device_index =
							event->pe_index;
				spare_array[i].psu_device_state =
							M0_PNDS_SNS_REPAIRING;
				break;
			}
		}
		if (i == state->pst_max_device_failures) {
			/* No free spare space slot is found!!
			 * The pool is in DUD state!!
			 */
			/* TODO add ADDB error message here */
		}
		break;
	case M0_PNDS_SNS_REPAIRING:
	case M0_PNDS_SNS_REPAIRED:
	case M0_PNDS_SNS_REBALANCING:
		/* change the repair spare slot usage */
		for (i = 0; i < state->pst_max_device_failures; i++) {
			if (spare_array[i].psu_device_index == event->pe_index){
				spare_array[i].psu_device_state =
							event->pe_state;
				break;
			}
		}
		/* must found */
		M0_ASSERT(i < state->pst_max_device_failures);
		break;
	default:
		/* Do nothing */
		;
	}

	if (pm->pm_be_seg != NULL) {
		/* This poolmach is on server. Update to persistent storage. */
		M0_ASSERT(tx != NULL);
		rc = m0_poolmach_store(pm, tx, &event_link);
	} else {
		struct m0_pool_event_link *new_link;
		M0_ALLOC_PTR(new_link);
		if (new_link == NULL) {
			rc = -ENOMEM;
		} else {
			*new_link = event_link;
			poolmach_events_tlink_init_at_tail(new_link,
						   &state->pst_events_list);
		}
	}
	/* Finally: unlock the poolmach */
	m0_rwlock_write_unlock(&pm->pm_lock);
	return rc;
}

M0_INTERNAL int m0_poolmach_state_query(struct m0_poolmach *pm,
					const struct m0_pool_version_numbers
					*from,
					const struct m0_pool_version_numbers
					*to, struct m0_tl *event_list_head)
{
	struct m0_poolmach_state      *state;
	struct m0_pool_version_numbers zero = { {0, 0} };
	struct m0_pool_event_link     *scan;
	struct m0_pool_event_link     *event_link;
	int                            rc = 0;

	M0_PRE(pm != NULL);
	M0_PRE(event_list_head != NULL);

	state = pm->pm_state;
	m0_rwlock_read_lock(&pm->pm_lock);
	if (from != NULL && !m0_poolmach_version_equal(&zero, from)) {
		m0_tl_for(poolmach_events, &state->pst_events_list, scan){
			if (m0_poolmach_version_equal(&scan->pel_new_version,
						     from)) {
				/* skip the current one and move to next */
				scan = poolmach_events_tlist_next(
						&state->pst_events_list,
						scan);
				break;
			}
		} m0_tl_endfor;
	} else {
		scan = poolmach_events_tlist_head(&state->pst_events_list);
	}

	while (scan != NULL) {
		/* allocate a copy of the event and event link,
		 * add it to output list.
		 */
		M0_ALLOC_PTR(event_link);
		if (event_link == NULL) {
			struct m0_pool_event_link *tmp;
			rc = -ENOMEM;
			m0_tl_for(poolmach_events, event_list_head, tmp) {
				poolmach_events_tlink_del_fini(tmp);
				m0_free(tmp);
			} m0_tl_endfor;
			break;
		}
		*event_link = *scan;
		poolmach_events_tlink_init_at_tail(event_link, event_list_head);

		if (to != NULL &&
		    m0_poolmach_version_equal(&scan->pel_new_version, to))
			break;
		scan = poolmach_events_tlist_next(&state->pst_events_list,
						  scan);
	}

	m0_rwlock_read_unlock(&pm->pm_lock);
	return rc;
}


M0_INTERNAL int m0_poolmach_current_version_get(struct m0_poolmach *pm,
						struct m0_pool_version_numbers
						*curr)
{
	M0_PRE(pm != NULL);
	M0_PRE(curr != NULL);

	m0_rwlock_read_lock(&pm->pm_lock);
	*curr = pm->pm_state->pst_version;
	m0_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

M0_INTERNAL int m0_poolmach_device_state(struct m0_poolmach *pm,
					 uint32_t device_index,
					 enum m0_pool_nd_state *state_out)
{
	M0_PRE(pm != NULL);
	M0_PRE(state_out != NULL);

	if (device_index >= pm->pm_state->pst_nr_devices)
		return -EINVAL;

	m0_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state->pst_devices_array[device_index].pd_state;
	m0_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

M0_INTERNAL int m0_poolmach_node_state(struct m0_poolmach *pm,
				       uint32_t node_index,
				       enum m0_pool_nd_state *state_out)
{
	M0_PRE(pm != NULL);
	M0_PRE(state_out != NULL);

	if (node_index >= pm->pm_state->pst_nr_nodes)
		return -EINVAL;

	m0_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state->pst_nodes_array[node_index].pn_state;
	m0_rwlock_read_unlock(&pm->pm_lock);

	return 0;
}

M0_INTERNAL bool
m0_poolmach_device_is_in_spare_usage_array(struct m0_poolmach *pm,
					   uint32_t device_index)
{
        int i;

        for (i = 0; i < pm->pm_state->pst_max_device_failures; ++i) {
                if (pm->pm_state->pst_spare_usage_array[i].psu_device_index ==
                                device_index) {
                        return true;
                }
        }
        return false;
}

M0_INTERNAL int m0_poolmach_sns_repair_spare_query(struct m0_poolmach *pm,
						   uint32_t device_index,
						   uint32_t *spare_slot_out)
{
	struct m0_pool_spare_usage *spare_usage_array;
	enum m0_pool_nd_state       device_state;
	uint32_t i;
	int      rc;
	M0_PRE(pm != NULL);
	M0_PRE(spare_slot_out != NULL);

	if (device_index >= pm->pm_state->pst_nr_devices)
		return -EINVAL;

	rc = -ENOENT;
	m0_rwlock_read_lock(&pm->pm_lock);
	device_state = pm->pm_state->pst_devices_array[device_index].pd_state;
	if (!M0_IN(device_state, (M0_PNDS_FAILED, M0_PNDS_SNS_REPAIRING,
				  M0_PNDS_SNS_REPAIRED, M0_PNDS_SNS_REBALANCING)))
		goto out;

	spare_usage_array = pm->pm_state->pst_spare_usage_array;
	for (i = 0; i < pm->pm_state->pst_max_device_failures; i++) {
		if (spare_usage_array[i].psu_device_index == device_index) {
			M0_ASSERT(M0_IN(spare_usage_array[i].psu_device_state,
						(M0_PNDS_FAILED,
						 M0_PNDS_SNS_REPAIRING,
						 M0_PNDS_SNS_REPAIRED,
						 M0_PNDS_SNS_REBALANCING)));
			*spare_slot_out = i;
			rc = 0;
			break;
		}
	}
out:
	m0_rwlock_read_unlock(&pm->pm_lock);

	return rc;
}

M0_INTERNAL bool
m0_poolmach_sns_repair_spare_contains_data(struct m0_poolmach *p,
					   uint32_t spare_slot,
					   bool check_state)
{
	if (!check_state)
		return p->pm_state->pst_spare_usage_array[spare_slot].
		       psu_device_index != POOL_PM_SPARE_SLOT_UNUSED &&
		       p->pm_state->pst_spare_usage_array[spare_slot].
		       psu_device_state != M0_PNDS_SNS_REPAIRING;
	else
		return p->pm_state->pst_spare_usage_array[spare_slot].
		       psu_device_index != POOL_PM_SPARE_SLOT_UNUSED &&
		       p->pm_state->pst_spare_usage_array[spare_slot].
		       psu_device_state != M0_PNDS_SNS_REPAIRED;
}

M0_INTERNAL int m0_poolmach_sns_rebalance_spare_query(struct m0_poolmach *pm,
						      uint32_t device_index,
						      uint32_t *spare_slot_out)
{
	struct m0_pool_spare_usage *spare_usage_array;
	enum m0_pool_nd_state       device_state;
	uint32_t i;
	int      rc;
	M0_PRE(pm != NULL);
	M0_PRE(spare_slot_out != NULL);

	if (device_index >= pm->pm_state->pst_nr_devices)
		return -EINVAL;

	rc = -ENOENT;
	m0_rwlock_read_lock(&pm->pm_lock);
	device_state = pm->pm_state->pst_devices_array[device_index].pd_state;
	if (!M0_IN(device_state, (M0_PNDS_SNS_REBALANCING)))
		goto out;

	spare_usage_array = pm->pm_state->pst_spare_usage_array;
	for (i = 0; i < pm->pm_state->pst_max_device_failures; i++) {
		if (spare_usage_array[i].psu_device_index == device_index) {
			M0_ASSERT(M0_IN(spare_usage_array[i].psu_device_state,
						(M0_PNDS_SNS_REBALANCING)));
			*spare_slot_out = i;
			rc = 0;
			break;
		}
	}
out:
	m0_rwlock_read_unlock(&pm->pm_lock);

	return rc;

}

M0_INTERNAL int m0_poolmach_current_state_get(struct m0_poolmach *pm,
					      struct m0_poolmach_state
					      **state_copy)
{
	return -ENOENT;
}

M0_INTERNAL void m0_poolmach_state_free(struct m0_poolmach *pm,
					struct m0_poolmach_state *state)
{
}

static int lno = 0;

/* Change this value to make it more verbose, e.g. to M0_ERROR */
#define dump_level M0_DEBUG

M0_INTERNAL void m0_poolmach_version_dump(struct m0_pool_version_numbers *v)
{
	M0_LOG(dump_level, "%4d:readv = %llx writev = %llx", lno,
		(unsigned long long)v->pvn_version[PVE_READ],
		(unsigned long long)v->pvn_version[PVE_WRITE]);
	lno++;
}

M0_INTERNAL void m0_poolmach_event_dump(struct m0_pool_event *e)
{
	M0_LOG(dump_level, "%4d:pe_type = %6s, pe_index = %x, pe_state=%10d",
		lno,
		e->pe_type == M0_POOL_DEVICE ? "device":"node",
		e->pe_index, e->pe_state);
	lno++;
}

M0_INTERNAL void m0_poolmach_event_list_dump(struct m0_poolmach *pm)
{
	struct m0_tl *head = &pm->pm_state->pst_events_list;
	struct m0_pool_event_link *scan;

	M0_LOG(dump_level, ">>>>>");
	m0_rwlock_read_lock(&pm->pm_lock);
	m0_tl_for(poolmach_events, head, scan) {
		m0_poolmach_event_dump(&scan->pel_event);
		m0_poolmach_version_dump(&scan->pel_new_version);
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&pm->pm_lock);
	M0_LOG(dump_level, "=====");
}

M0_INTERNAL void m0_poolmach_device_state_dump(struct m0_poolmach *pm)
{
	int i;
	M0_LOG(dump_level, ">>>>>");
	for (i = 1; i < pm->pm_state->pst_nr_devices; i++) {
		M0_LOG(dump_level, "%04d:device[%d] state: %d",
			lno, i, pm->pm_state->pst_devices_array[i].pd_state);
		lno++;
	}
	M0_LOG(dump_level, "=====");
}

#undef dump_level

#undef M0_TRACE_SUBSYSTEM
/** @} end group pool */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
