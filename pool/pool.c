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
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"      /* M0_LOG */
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

M0_TL_DESCR_DEFINE(poolmach_events, "pool machine events list", M0_INTERNAL,
                   struct m0_pool_event_link, pel_linkage, pel_magic,
                   M0_POOL_EVENTS_LIST_MAGIC, M0_POOL_EVENTS_HEAD_MAGIC);

M0_TL_DEFINE(poolmach_events, M0_INTERNAL, struct m0_pool_event_link);

enum {
	/* Unused spare slot has this device index */
	POOL_PM_SPARE_SLOT_UNUSED = 0xFFFFFFFF
};


M0_INTERNAL int m0_pool_init(struct m0_pool *pool, uint32_t width)
{
	pool->po_width = width;
	return 0;
}

M0_INTERNAL void m0_pool_fini(struct m0_pool *pool)
{
}

M0_INTERNAL int m0_pool_alloc(struct m0_pool *pool, struct m0_stob_id *id)
{
	static uint64_t seq = 3;

	id->si_bits.u_hi = (uint64_t)pool;
	id->si_bits.u_lo  = seq++;
	M0_POST(m0_stob_id_is_set(id));
	return 0;
}

M0_INTERNAL void m0_pool_put(struct m0_pool *pool, struct m0_stob_id *id)
{
	M0_PRE(m0_stob_id_is_set(id));
}

M0_INTERNAL int m0_pools_init(void)
{
	return 0;
}

M0_INTERNAL void m0_pools_fini(void)
{
}

M0_INTERNAL bool m0_poolmach_version_equal(const struct m0_pool_version_numbers
					   *v1,
					   const struct m0_pool_version_numbers
					   *v2)
{
	return !memcmp(v1, v2, sizeof *v1);
}

M0_INTERNAL int m0_poolmach_init(struct m0_poolmach *pm,
				 struct m0_dtm *dtm,
				 uint32_t nr_nodes,
				 uint32_t nr_devices,
				 uint32_t max_node_failures,
				 uint32_t max_device_failures)
{
	uint32_t i;
	M0_PRE(!pm->pm_is_initialised);

	/* TODO Init pool machine, build its state from persistent storage
	 * This involves to read latest pool machine state from storage,
	 * and build its state transit history (represented by list of events).
	 *
	 * XXX temoprarily, we build this from scratch.
	 */
	M0_SET0(&pm->pm_state);
	pm->pm_state.pst_version.pvn_version[PVE_READ]  = 0;
	pm->pm_state.pst_version.pvn_version[PVE_WRITE] = 0;
	pm->pm_state.pst_nr_nodes = nr_nodes;
	/* nr_devices io devices and 1 md device. md uses container 0 */
	pm->pm_state.pst_nr_devices = nr_devices + 1;
	pm->pm_state.pst_max_node_failures = max_node_failures;
	pm->pm_state.pst_max_device_failures = max_device_failures;
	pm->pm_state.pst_nodes_array = m0_alloc(pm->pm_state.pst_nr_nodes *
						sizeof (struct m0_poolnode));
	pm->pm_state.pst_devices_array = m0_alloc(pm->pm_state.pst_nr_devices *
						  sizeof (struct m0_pooldev));

	pm->pm_state.pst_spare_usage_array
			= m0_alloc(pm->pm_state.pst_max_device_failures *
				   sizeof (struct m0_pool_spare_usage));
	if (pm->pm_state.pst_nodes_array == NULL ||
	    pm->pm_state.pst_devices_array == NULL ||
	    pm->pm_state.pst_spare_usage_array == NULL) {
		/* m0_free(NULL) is valid */
		m0_free(pm->pm_state.pst_nodes_array);
		m0_free(pm->pm_state.pst_devices_array);
		m0_free(pm->pm_state.pst_spare_usage_array);
		return -ENOMEM;
	}

	for (i = 0; i < pm->pm_state.pst_nr_nodes; i++) {
		pm->pm_state.pst_nodes_array[i].pn_state = M0_PNDS_ONLINE;
		/* TODO use real node id */
		pm->pm_state.pst_nodes_array[i].pn_id    = NULL;
	}

	for (i = 0; i < pm->pm_state.pst_nr_devices; i++) {
		pm->pm_state.pst_devices_array[i].pd_state = M0_PNDS_ONLINE;
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

	m0_rwlock_init(&pm->pm_lock);
	pm->pm_is_initialised = true;
	return 0;
}

M0_INTERNAL void m0_poolmach_fini(struct m0_poolmach *pm)
{
	struct m0_pool_event_link *scan;

	M0_PRE(pm != NULL);

	m0_rwlock_write_lock(&pm->pm_lock);
	/* TODO Sync the pool machine state onto persistent storage */

	/* iterate through events and free them */
	m0_tl_for(poolmach_events, &pm->pm_state.pst_events_list, scan) {
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

	m0_free(pm->pm_state.pst_spare_usage_array);
	m0_free(pm->pm_state.pst_devices_array);
	m0_free(pm->pm_state.pst_nodes_array);
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
 *       |                                       SNS_REBALANCED
 *       |                                            |
 *       |                                            v
 *       +------------------<-------------------------+
 */
M0_INTERNAL int m0_poolmach_state_transit(struct m0_poolmach *pm,
					  struct m0_pool_event *event)
{
	struct m0_poolmach_state   *pm_state;
	struct m0_pool_spare_usage *spare_array;
	struct m0_pool_event_link  *event_link;
	enum m0_pool_nd_state       old_state;
	int                         rc = 0;
	int                         i;

	M0_PRE(pm != NULL);
	M0_PRE(event != NULL);

	pm_state = &pm->pm_state;

	if (!M0_IN(event->pe_type, (M0_POOL_NODE, M0_POOL_DEVICE)))
		return -EINVAL;

	if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE,
				     M0_PNDS_FAILED,
				     M0_PNDS_OFFLINE,
				     M0_PNDS_SNS_REPAIRING,
				     M0_PNDS_SNS_REPAIRED,
				     M0_PNDS_SNS_REBALANCING,
				     M0_PNDS_SNS_REBALANCED)))
		return -EINVAL;

	if ((event->pe_type == M0_POOL_NODE &&
	     event->pe_index >= pm_state->pst_nr_nodes) ||
	    (event->pe_type == M0_POOL_DEVICE &&
	     event->pe_index >= pm_state->pst_nr_devices))
		return -EINVAL;

	if (event->pe_type == M0_POOL_NODE) {
		old_state = pm_state->pst_nodes_array[event->pe_index].pn_state;
	} else if (event->pe_type == M0_POOL_DEVICE) {
		old_state =
			pm_state->pst_devices_array[event->pe_index].pd_state;
	}

	switch (old_state) {
	case M0_PNDS_ONLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_OFFLINE,
					     M0_PNDS_FAILED)))
			return -EINVAL;
		break;
	case M0_PNDS_OFFLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE,
					     M0_PNDS_FAILED)))
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
		if (!M0_IN(event->pe_state, (M0_PNDS_SNS_REBALANCED)))
			return -EINVAL;
		break;
	case M0_PNDS_SNS_REBALANCED:
		if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE)))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* Step 1: lock the poolmach */
	m0_rwlock_write_lock(&pm->pm_lock);

	/* step 2: Update the state according to event */
	event_link = m0_alloc(sizeof *event_link);
	if (event_link == NULL) {
		rc = -ENOMEM;
		goto out_unlock;
	}
	event_link->pel_event = *event;
	if (event->pe_type == M0_POOL_NODE) {
		/* TODO if this is a new node join event, the index
		 * might larger than the current number. Then we need
		 * to create a new larger array to hold nodes info.
		 */
		pm_state->pst_nodes_array[event->pe_index].pn_state =
				event->pe_state;
	} else if (event->pe_type == M0_POOL_DEVICE) {
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
	case M0_PNDS_ONLINE:
		/* clear spare slot usage if it is from rebalanced */
		for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
			if (spare_array[i].psp_device_index == event->pe_index){
				M0_ASSERT(M0_IN(spare_array[i].psp_device_state,
						     (M0_PNDS_SNS_REBALANCED)));
				spare_array[i].psp_device_index =
						POOL_PM_SPARE_SLOT_UNUSED;
			break;
			}
		}
		break;
	case M0_PNDS_SNS_REPAIRING:
		/* alloc a sns repare spare slot */
		for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
			if (spare_array[i].psp_device_index ==
						POOL_PM_SPARE_SLOT_UNUSED) {
				spare_array[i].psp_device_index =
							event->pe_index;
				spare_array[i].psp_device_state =
							M0_PNDS_SNS_REPAIRING;
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
	case M0_PNDS_SNS_REPAIRED:
	case M0_PNDS_SNS_REBALANCING:
	case M0_PNDS_SNS_REBALANCED:
		/* change the repair spare slot usage */
		for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
			if (spare_array[i].psp_device_index == event->pe_index){
				spare_array[i].psp_device_state =
							event->pe_state;
				break;
			}
		}
		/* must found */
		M0_ASSERT(i < pm->pm_state.pst_max_device_failures);
		break;
	default:
		/* Do nothing */
		;
	}



out_unlock:
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
	struct m0_pool_version_numbers zero = { {0, 0} };
	struct m0_pool_event_link     *scan;
	struct m0_pool_event_link     *event_link;
	int                            rc = 0;

	M0_PRE(pm != NULL);
	M0_PRE(event_list_head != NULL);

	m0_rwlock_read_lock(&pm->pm_lock);
	if (from != NULL && !m0_poolmach_version_equal(&zero, from)) {
		m0_tl_for(poolmach_events, &pm->pm_state.pst_events_list, scan){
			if (m0_poolmach_version_equal(&scan->pel_new_version,
						     from)) {
				/* skip the current one and move to next */
				scan = poolmach_events_tlist_next(
						&pm->pm_state.pst_events_list,
						scan);
				break;
			}
		} m0_tl_endfor;
	} else {
		scan = poolmach_events_tlist_head(&pm->pm_state.pst_events_list);
	}

	while (scan != NULL) {
		/* allocate a copy of the event and event link,
		 * add it to output list.
		 */
		event_link = m0_alloc(sizeof *event_link);
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
		scan = poolmach_events_tlist_next(&pm->pm_state.pst_events_list,
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
	*curr = pm->pm_state.pst_version;
	m0_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

M0_INTERNAL int m0_poolmach_device_state(struct m0_poolmach *pm,
					 uint32_t device_index,
					 enum m0_pool_nd_state *state_out)
{
	M0_PRE(pm != NULL);
	M0_PRE(state_out != NULL);

	if (device_index >= pm->pm_state.pst_nr_devices)
		return -EINVAL;

	m0_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state.pst_devices_array[device_index].pd_state;
	m0_rwlock_read_unlock(&pm->pm_lock);
	return 0;
}

M0_INTERNAL int m0_poolmach_node_state(struct m0_poolmach *pm,
				       uint32_t node_index,
				       enum m0_pool_nd_state *state_out)
{
	M0_PRE(pm != NULL);
	M0_PRE(state_out != NULL);

	if (node_index >= pm->pm_state.pst_nr_nodes)
		return -EINVAL;

	m0_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state.pst_nodes_array[node_index].pn_state;
	m0_rwlock_read_unlock(&pm->pm_lock);

	return 0;
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

	if (device_index >= pm->pm_state.pst_nr_devices)
		return -EINVAL;

	rc = -ENOENT;
	m0_rwlock_read_lock(&pm->pm_lock);
	device_state = pm->pm_state.pst_devices_array[device_index].pd_state;
	if (!M0_IN(device_state, (M0_PNDS_SNS_REPAIRING, M0_PNDS_SNS_REPAIRED)))
		goto out;

	spare_usage_array = pm->pm_state.pst_spare_usage_array;
	for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
		if (spare_usage_array[i].psp_device_index == device_index) {
			M0_ASSERT(M0_IN(spare_usage_array[i].psp_device_state,
						(M0_PNDS_SNS_REPAIRING,
						 M0_PNDS_SNS_REPAIRED)));
			*spare_slot_out = i;
			rc = 0;
			break;
		}
	}
out:
	m0_rwlock_read_unlock(&pm->pm_lock);

	return rc;
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

	if (device_index >= pm->pm_state.pst_nr_devices)
		return -EINVAL;

	rc = -ENOENT;
	m0_rwlock_read_lock(&pm->pm_lock);
	device_state = pm->pm_state.pst_devices_array[device_index].pd_state;
	if (!M0_IN(device_state, (M0_PNDS_SNS_REBALANCING,
				  M0_PNDS_SNS_REBALANCED)))
		goto out;

	spare_usage_array = pm->pm_state.pst_spare_usage_array;
	for (i = 0; i < pm->pm_state.pst_max_device_failures; i++) {
		if (spare_usage_array[i].psp_device_index == device_index) {
			M0_ASSERT(M0_IN(spare_usage_array[i].psp_device_state,
						(M0_PNDS_SNS_REBALANCING,
						 M0_PNDS_SNS_REBALANCED)));
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
	M0_LOG(dump_level, "%4d:readv = %llx writev = %llx\n", lno++,
		(unsigned long long)v->pvn_version[PVE_READ],
		(unsigned long long)v->pvn_version[PVE_WRITE]);
}

M0_INTERNAL void m0_poolmach_event_dump(struct m0_pool_event *e)
{
	M0_LOG(dump_level, "%4d:pe_type  = %10s pe_index = %2x pe_state=%10s\n",
		lno++,
		e->pe_type == M0_POOL_DEVICE ? "device":"node",
		e->pe_index,
		e->pe_state == M0_PNDS_ONLINE? "ONLINE" :
		    e->pe_state == M0_PNDS_FAILED? "FAILED" :
			e->pe_state == M0_PNDS_OFFLINE? "OFFLINE" :
				"RECOVERING"
	);
}

M0_INTERNAL void m0_poolmach_event_list_dump(struct m0_tl *head)
{
	struct m0_pool_event_link *scan;

	m0_tl_for(poolmach_events, head, scan) {
		m0_poolmach_event_dump(&scan->pel_event);
		m0_poolmach_version_dump(&scan->pel_new_version);
	} m0_tl_endfor;
	M0_LOG(dump_level, "=====\n");
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
