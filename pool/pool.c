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

#include "lib/errno.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "pool/pool.h"
#include "lib/misc.h"

/**
   @addtogroup pool

   XXX Stub code for now.
   @{
 */

C2_TL_DESCR_DEFINE(poolmach_events, "pool machine events list", C2_INTERNAL,
                   struct c2_pool_event_link, pel_linkage, pel_magic,
                   C2_POOL_EVENTS_LIST_MAGIC, C2_POOL_EVENTS_HEAD_MAGIC);

C2_TL_DEFINE(poolmach_events, C2_INTERNAL, struct c2_pool_event_link);

enum {
	/* Unused spare slot has this device index */
	POOL_PM_SPARE_SLOT_UNUSED = 0xFFFFFFFF
};


C2_INTERNAL int c2_pool_init(struct c2_pool *pool, uint32_t width)
{
	pool->po_width = width;
	return 0;
}

C2_INTERNAL void c2_pool_fini(struct c2_pool *pool)
{
}

C2_INTERNAL int c2_pool_alloc(struct c2_pool *pool, struct c2_stob_id *id)
{
	static uint64_t seq = 3;

	id->si_bits.u_hi = (uint64_t)pool;
	id->si_bits.u_lo  = seq++;
	C2_POST(c2_stob_id_is_set(id));
	return 0;
}

C2_INTERNAL void c2_pool_put(struct c2_pool *pool, struct c2_stob_id *id)
{
	C2_PRE(c2_stob_id_is_set(id));
}

C2_INTERNAL int c2_pools_init(void)
{
	return 0;
}

C2_INTERNAL void c2_pools_fini(void)
{
}

C2_INTERNAL bool c2_poolmach_version_equal(const struct c2_pool_version_numbers
					   *v1,
					   const struct c2_pool_version_numbers
					   *v2)
{
	return !memcmp(v1, v2, sizeof *v1);
}

C2_INTERNAL int c2_poolmach_init(struct c2_poolmach *pm,
				 struct c2_dtm *dtm,
				 uint32_t nr_nodes,
				 uint32_t nr_devices,
				 uint32_t max_node_failures,
				 uint32_t max_device_failures)
{
	uint32_t i;
	C2_PRE(!pm->pm_is_initialised);

	/* TODO Init pool machine, build its state from persistent storage
	 * This involves to read latest pool machine state from storage,
	 * and build its state transit history (represented by list of events).
	 *
	 * XXX temoprarily, we build this from scratch.
	 */
	C2_SET0(&pm->pm_state);
	pm->pm_state.pst_version.pvn_version[PVE_READ]  = 0;
	pm->pm_state.pst_version.pvn_version[PVE_WRITE] = 0;
	pm->pm_state.pst_nr_nodes = nr_nodes;
	pm->pm_state.pst_nr_devices = nr_devices;
	pm->pm_state.pst_max_node_failures = max_node_failures;
	pm->pm_state.pst_max_device_failures = max_device_failures;
	pm->pm_state.pst_nodes_array = c2_alloc(pm->pm_state.pst_nr_nodes *
						sizeof (struct c2_poolnode));
	pm->pm_state.pst_devices_array = c2_alloc(pm->pm_state.pst_nr_devices *
						  sizeof (struct c2_pooldev));

	pm->pm_state.pst_spare_usage_array
			= c2_alloc(pm->pm_state.pst_max_device_failures *
				   sizeof (struct c2_pool_spare_usage));
	if (pm->pm_state.pst_nodes_array == NULL ||
	    pm->pm_state.pst_devices_array == NULL ||
	    pm->pm_state.pst_spare_usage_array == NULL) {
		/* c2_free(NULL) is valid */
		c2_free(pm->pm_state.pst_nodes_array);
		c2_free(pm->pm_state.pst_devices_array);
		c2_free(pm->pm_state.pst_spare_usage_array);
		return -ENOMEM;
	}

	for (i = 0; i < pm->pm_state.pst_nr_nodes; i++) {
		pm->pm_state.pst_nodes_array[i].pn_state = C2_PNDS_ONLINE;
		/* TODO use real node id */
		pm->pm_state.pst_nodes_array[i].pn_id    = NULL;
	}

	for (i = 0; i < pm->pm_state.pst_nr_devices; i++) {
		pm->pm_state.pst_devices_array[i].pd_state = C2_PNDS_ONLINE;
		/* TODO use real device id */
		pm->pm_state.pst_devices_array[i].pd_id    = NULL;
		pm->pm_state.pst_devices_array[i].pd_node  = NULL;
	}

	for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
		/* -1 means that this spare slot is not used */
		pm->pm_state.pst_spare_usage_array[i].psp_device_index =
						POOL_PM_SPARE_SLOT_UNUSED;
	}

	poolmach_events_tlist_init(&pm->pm_state.pst_events_list);

	c2_rwlock_init(&pm->pm_lock);
	pm->pm_is_initialised = true;
	return 0;
}

C2_INTERNAL void c2_poolmach_fini(struct c2_poolmach *pm)
{
	struct c2_pool_event_link *scan;

	C2_PRE(pm != NULL);

	c2_rwlock_write_lock(&pm->pm_lock);
	/* TODO Sync the pool machine state onto persistent storage */

	/* iterate through events and free them */
	c2_tl_for(poolmach_events, &pm->pm_state.pst_events_list, scan) {
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan);
	} c2_tl_endfor;

	c2_free(pm->pm_state.pst_spare_usage_array);
	c2_free(pm->pm_state.pst_devices_array);
	c2_free(pm->pm_state.pst_nodes_array);
	c2_rwlock_write_unlock(&pm->pm_lock);

	pm->pm_is_initialised = false;
	c2_rwlock_fini(&pm->pm_lock);
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
 *       |                                       SNS_REBALANCED
 *       |                                            |
 *       |                                            v
 *       +------------------<-------------------------+
 */
C2_INTERNAL int c2_poolmach_state_transit(struct c2_poolmach *pm,
					  struct c2_pool_event *event)
{
	struct c2_poolmach_state   *pm_state;
	struct c2_pool_spare_usage *spare_array;
	struct c2_pool_event_link  *event_link;
	enum c2_pool_nd_state       old_state;
	int                         rc = 0;
	int                         i;

	C2_PRE(pm != NULL);
	C2_PRE(event != NULL);

	pm_state = &pm->pm_state;

	if (!C2_IN(event->pe_type, (C2_POOL_NODE, C2_POOL_DEVICE)))
		return -EINVAL;

	if (!C2_IN(event->pe_state, (C2_PNDS_ONLINE,
				     C2_PNDS_FAILED,
				     C2_PNDS_OFFLINE,
				     C2_PNDS_SNS_REPAIRING,
				     C2_PNDS_SNS_REPAIRED,
				     C2_PNDS_SNS_REBALANCING,
				     C2_PNDS_SNS_REBALANCED)))
		return -EINVAL;

	if ((event->pe_type == C2_POOL_NODE &&
	     event->pe_index >= pm_state->pst_nr_nodes) ||
	    (event->pe_type == C2_POOL_DEVICE &&
	     event->pe_index >= pm_state->pst_nr_devices))
		return -EINVAL;

	if (event->pe_type == C2_POOL_NODE) {
		old_state = pm_state->pst_nodes_array[event->pe_index].pn_state;
	} else if (event->pe_type == C2_POOL_DEVICE) {
		old_state =
			pm_state->pst_devices_array[event->pe_index].pd_state;
	}

	switch (old_state) {
	case C2_PNDS_ONLINE:
		if (!C2_IN(event->pe_state, (C2_PNDS_OFFLINE,
					     C2_PNDS_FAILED)))
			return -EINVAL;
		break;
	case C2_PNDS_OFFLINE:
		if (!C2_IN(event->pe_state, (C2_PNDS_ONLINE,
					     C2_PNDS_FAILED)))
			return -EINVAL;
		break;
	case C2_PNDS_FAILED:
		if (!C2_IN(event->pe_state, (C2_PNDS_SNS_REPAIRING)))
			return -EINVAL;
		break;
	case C2_PNDS_SNS_REPAIRING:
		if (!C2_IN(event->pe_state, (C2_PNDS_SNS_REPAIRED)))
			return -EINVAL;
		break;
	case C2_PNDS_SNS_REPAIRED:
		if (!C2_IN(event->pe_state, (C2_PNDS_SNS_REBALANCING)))
			return -EINVAL;
		break;
	case C2_PNDS_SNS_REBALANCING:
		if (!C2_IN(event->pe_state, (C2_PNDS_SNS_REBALANCED)))
			return -EINVAL;
		break;
	case C2_PNDS_SNS_REBALANCED:
		if (!C2_IN(event->pe_state, (C2_PNDS_ONLINE)))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* Step 1: lock the poolmach */
	c2_rwlock_write_lock(&pm->pm_lock);

	/* step 2: Update the state according to event */
	event_link = c2_alloc(sizeof *event_link);
	if (event_link == NULL) {
		rc = -ENOMEM;
		goto out_unlock;
	}
	event_link->pel_event = *event;
	if (event->pe_type == C2_POOL_NODE) {
		/* TODO if this is a new node join event, the index
		 * might larger than the current number. Then we need
		 * to create a new larger array to hold nodes info.
		 */
		pm_state->pst_nodes_array[event->pe_index].pn_state =
				event->pe_state;
	} else if (event->pe_type == C2_POOL_DEVICE) {
		pm_state->pst_devices_array[event->pe_index].pd_state =
				event->pe_state;
	}

	/* step 3: Increase the version */
	++ pm_state->pst_version.pvn_version[PVE_READ];
	++ pm_state->pst_version.pvn_version[PVE_WRITE];

	/* Step 4: copy new version into event, and link it into list */
	event_link->pel_new_version = pm_state->pst_version;
	poolmach_events_tlink_init_at_tail(event_link,
					   &pm_state->pst_events_list);

	/* Step 5: Alloc or free a spare slot if necessary.*/
	spare_array = pm->pm_state.pst_spare_usage_array;
	switch (event->pe_state) {
	case C2_PNDS_ONLINE:
		/* clear spare slot usage if it is from rebalanced */
		for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
			if (spare_array[i].psp_device_index == event->pe_index){
				C2_ASSERT(C2_IN(spare_array[i].psp_device_state,
						     (C2_PNDS_SNS_REBALANCED)));
				spare_array[i].psp_device_index =
						POOL_PM_SPARE_SLOT_UNUSED;
			break;
			}
		}
		break;
	case C2_PNDS_SNS_REPAIRING:
		/* alloc a sns repare spare slot */
		for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
			if (spare_array[i].psp_device_index ==
						POOL_PM_SPARE_SLOT_UNUSED) {
				spare_array[i].psp_device_index =
							event->pe_index;
				spare_array[i].psp_device_state =
							C2_PNDS_SNS_REPAIRING;
				break;
			}
		}
		if (i == pm->pm_state.pst_max_device_failures) {
			/* No free spare space slot is found!!
			 * The pool is in DUD state!!
			 */
			/* TODO add ADDB error message here */
		}
		break;
	case C2_PNDS_SNS_REPAIRED:
	case C2_PNDS_SNS_REBALANCING:
	case C2_PNDS_SNS_REBALANCED:
		/* change the repair spare slot usage */
		for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
			if (spare_array[i].psp_device_index == event->pe_index){
				spare_array[i].psp_device_state =
							event->pe_state;
				break;
			}
		}
		/* must found */
		C2_ASSERT(i < pm->pm_state.pst_max_device_failures);
		break;
	default:
		/* Do nothing */
		;
	}



out_unlock:
	/* Finally: unlock the poolmach */
	c2_rwlock_write_unlock(&pm->pm_lock);
	return rc;
}

C2_INTERNAL int c2_poolmach_state_query(struct c2_poolmach *pm,
					const struct c2_pool_version_numbers
					*from,
					const struct c2_pool_version_numbers
					*to, struct c2_tl *event_list_head)
{
	struct c2_pool_version_numbers zero = { {0, 0} };
	struct c2_pool_event_link     *scan;
	struct c2_pool_event_link     *event_link;
	int                            rc = 0;

	C2_PRE(pm != NULL);
	C2_PRE(event_list_head != NULL);

	c2_rwlock_read_lock(&pm->pm_lock);
	if (from != NULL && !c2_poolmach_version_equal(&zero, from)) {
		c2_tl_for(poolmach_events, &pm->pm_state.pst_events_list, scan){
			if (c2_poolmach_version_equal(&scan->pel_new_version,
						     from)) {
				/* skip the current one and move to next */
				scan = poolmach_events_tlist_next(
						&pm->pm_state.pst_events_list,
						scan);
				break;
			}
		} c2_tl_endfor;
	} else {
		scan = poolmach_events_tlist_head(&pm->pm_state.pst_events_list);
	}

	while (scan != NULL) {
		/* allocate a copy of the event and event link,
		 * add it to output list.
		 */
		event_link = c2_alloc(sizeof *event_link);
		if (event_link == NULL) {
			struct c2_pool_event_link *tmp;
			rc = -ENOMEM;
			c2_tl_for(poolmach_events, event_list_head, tmp) {
				poolmach_events_tlink_del_fini(tmp);
				c2_free(tmp);
			} c2_tl_endfor;
			break;
		}
		*event_link = *scan;
		poolmach_events_tlink_init_at_tail(event_link, event_list_head);

		if (to != NULL &&
		    c2_poolmach_version_equal(&scan->pel_new_version, to))
			break;
		scan = poolmach_events_tlist_next(&pm->pm_state.pst_events_list,
						  scan);
	}

	c2_rwlock_read_unlock(&pm->pm_lock);
	return rc;
}


C2_INTERNAL int c2_poolmach_current_version_get(struct c2_poolmach *pm,
						struct c2_pool_version_numbers
						*curr)
{
	C2_PRE(pm != NULL);
	C2_PRE(curr != NULL);

	c2_rwlock_read_lock(&pm->pm_lock);
	*curr = pm->pm_state.pst_version;
	c2_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

C2_INTERNAL int c2_poolmach_device_state(struct c2_poolmach *pm,
					 uint32_t device_index,
					 enum c2_pool_nd_state *state_out)
{
	C2_PRE(pm != NULL);
	C2_PRE(state_out != NULL);

	if (device_index >= pm->pm_state.pst_nr_devices)
		return -EINVAL;

	c2_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state.pst_devices_array[device_index].pd_state;
	c2_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

C2_INTERNAL int c2_poolmach_node_state(struct c2_poolmach *pm,
				       uint32_t node_index,
				       enum c2_pool_nd_state *state_out)
{
	C2_PRE(pm != NULL);
	C2_PRE(state_out != NULL);

	if (node_index >= pm->pm_state.pst_nr_nodes)
		return -EINVAL;

	c2_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state.pst_nodes_array[node_index].pn_state;
	c2_rwlock_read_unlock(&pm->pm_lock);

	return 0;
}

C2_INTERNAL int c2_poolmach_sns_repair_spare_query(struct c2_poolmach *pm,
						   uint32_t device_index,
						   uint32_t *spare_slot_out)
{
	struct c2_pool_spare_usage *spare_usage_array;
	enum c2_pool_nd_state       device_state;
	uint32_t i;
	int      rc;
	C2_PRE(pm != NULL);
	C2_PRE(spare_slot_out != NULL);

	if (device_index >= pm->pm_state.pst_nr_devices)
		return -EINVAL;

	rc = -ENOENT;
	c2_rwlock_read_lock(&pm->pm_lock);
	device_state = pm->pm_state.pst_devices_array[device_index].pd_state;
	if (!C2_IN(device_state, (C2_PNDS_SNS_REPAIRING, C2_PNDS_SNS_REPAIRED)))
		goto out;

	spare_usage_array = pm->pm_state.pst_spare_usage_array;
	for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
		if (spare_usage_array[i].psp_device_index == device_index) {
			C2_ASSERT(C2_IN(spare_usage_array[i].psp_device_state,
						(C2_PNDS_SNS_REPAIRING,
						 C2_PNDS_SNS_REPAIRED)));
			*spare_slot_out = i;
			rc = 0;
			break;
		}
	}
out:
	c2_rwlock_read_unlock(&pm->pm_lock);

	return rc;
}

C2_INTERNAL int c2_poolmach_sns_rebalance_spare_query(struct c2_poolmach *pm,
						      uint32_t device_index,
						      uint32_t *spare_slot_out)
{
	struct c2_pool_spare_usage *spare_usage_array;
	enum c2_pool_nd_state       device_state;
	uint32_t i;
	int      rc;
	C2_PRE(pm != NULL);
	C2_PRE(spare_slot_out != NULL);

	if (device_index >= pm->pm_state.pst_nr_devices)
		return -EINVAL;

	rc = -ENOENT;
	c2_rwlock_read_lock(&pm->pm_lock);
	device_state = pm->pm_state.pst_devices_array[device_index].pd_state;
	if (!C2_IN(device_state, (C2_PNDS_SNS_REBALANCING,
				  C2_PNDS_SNS_REBALANCED)))
		goto out;

	spare_usage_array = pm->pm_state.pst_spare_usage_array;
	for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
		if (spare_usage_array[i].psp_device_index == device_index) {
			C2_ASSERT(C2_IN(spare_usage_array[i].psp_device_state,
						(C2_PNDS_SNS_REBALANCING,
						 C2_PNDS_SNS_REBALANCED)));
			*spare_slot_out = i;
			rc = 0;
			break;
		}
	}
out:
	c2_rwlock_read_unlock(&pm->pm_lock);

	return rc;

}

C2_INTERNAL int c2_poolmach_current_state_get(struct c2_poolmach *pm,
					      struct c2_poolmach_state
					      **state_copy)
{
	return -ENOENT;
}

C2_INTERNAL void c2_poolmach_state_free(struct c2_poolmach *pm,
					struct c2_poolmach_state *state)
{
}

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
