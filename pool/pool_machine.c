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

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "pool/pool.h"
#include "conf/confc.h"
#include "conf/helpers.h"  /* m0_conf_filter_cntv_diskv */
#include "conf/obj_ops.h"  /* m0_conf_dirval */
#include "conf/dir_iter.h" /* m0_conf_diter_init, m0_conf_diter_next_sync */

/**
   @addtogroup poolmach

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
				   struct m0_poolmach_event_link *event_link);

M0_TL_DESCR_DEFINE(poolmach_events, "pool machine events list", M0_INTERNAL,
		   struct m0_poolmach_event_link, pel_linkage, pel_magic,
		   M0_POOL_EVENTS_LIST_MAGIC, M0_POOL_EVENTS_HEAD_MAGIC);

M0_TL_DEFINE(poolmach_events, M0_INTERNAL, struct m0_poolmach_event_link);

M0_INTERNAL bool m0_poolmach_version_equal(const struct m0_poolmach_versions
					   *v1,
					   const struct m0_poolmach_versions
					   *v2)
{
	return !memcmp(v1, v2, sizeof *v1);
}

M0_INTERNAL bool m0_poolmach_version_before(const struct m0_poolmach_versions
					    *v1,
					    const struct m0_poolmach_versions
					    *v2)
{
	return
		v1->pvn_version[PVE_READ]  < v2->pvn_version[PVE_READ] ||
		v1->pvn_version[PVE_WRITE] < v2->pvn_version[PVE_WRITE];
}

M0_INTERNAL int m0_poolmach_init_by_conf(struct m0_poolmach *pm,
					 struct m0_conf_pver *pver)
{
	struct m0_conf_diter       it;
	struct m0_conf_objv       *ov;
	struct m0_conf_disk       *d;
	struct m0_conf_controller *c;
	struct m0_conf_obj        *obj;
	struct m0_confc           *confc;
	uint32_t                   node = 0;
	uint32_t                   dev = 0;
	int                        rc;

	confc = m0_confc_from_obj(&pver->pv_obj);
	rc = m0_conf_diter_init(&it, confc, &pver->pv_obj,
				M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID,
				M0_CONF_CTRLV_DISKVS_FID);
	if (rc != 0)
		return M0_RC(rc);

	while ((rc = m0_conf_diter_next_sync(&it,
			m0_conf_filter_cntv_diskv)) == M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE);
		ov = M0_CONF_CAST(obj, m0_conf_objv);
		M0_ASSERT(m0_conf_obj_type(ov->cv_real) ==
				&M0_CONF_CONTROLLER_TYPE ||
			  m0_conf_obj_type(ov->cv_real) ==
				&M0_CONF_DISK_TYPE);
		if (m0_conf_obj_type(ov->cv_real) == &M0_CONF_CONTROLLER_TYPE) {
			c = M0_CONF_CAST(ov->cv_real,
					 m0_conf_controller);
			pm->pm_state->pst_nodes_array[node].pn_id =
						c->cc_obj.co_id;
			M0_CNT_INC(node);
		}
		if (m0_conf_obj_type(ov->cv_real) == &M0_CONF_DISK_TYPE) {
			d = M0_CONF_CAST(ov->cv_real, m0_conf_disk);
			pm->pm_state->pst_devices_array[dev].pd_id =
						d->ck_dev->sd_obj.co_id;
			pm->pm_state->pst_devices_array[dev].pd_node =
				&pm->pm_state->pst_nodes_array[node];
			M0_CNT_INC(dev);
		}
	}

	m0_conf_diter_fini(&it);

	return M0_RC(rc);
}

M0_INTERNAL int m0_poolmach_init(struct m0_poolmach *pm,
				 struct m0_be_seg    *be_seg,
				 struct m0_sm_group  *sm_grp,
				 struct m0_dtm       *dtm,
				 uint32_t             nr_nodes,
				 uint32_t             nr_devices,
				 uint32_t             max_node_failures,
				 uint32_t             max_device_failures)
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
			return M0_ERR(-ENOMEM);

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
			return M0_ERR(-ENOMEM);
		}

		for (i = 0; i < state->pst_nr_nodes; i++) {
			state->pst_nodes_array[i].pn_state = M0_PNDS_ONLINE;
			M0_SET0(&state->pst_nodes_array[i].pn_id);
		}

		for (i = 0; i < state->pst_nr_devices; i++) {
			state->pst_devices_array[i].pd_state = M0_PNDS_ONLINE;
			/* TODO use real device id */
			M0_SET0(&state->pst_devices_array[i].pd_id);
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
	return M0_RC(rc);
}

M0_INTERNAL void m0_poolmach_fini(struct m0_poolmach *pm)
{
	struct m0_poolmach_event_link *scan;
	struct m0_poolmach_state      *state = pm->pm_state;

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
M0_INTERNAL int m0_poolmach_state_transit(struct m0_poolmach       *pm,
					  const struct m0_poolmach_event *event,
					  struct m0_be_tx            *tx)
{
	struct m0_poolmach_state      *state;
	struct m0_pool_spare_usage    *spare_array;
	struct m0_poolmach_event_link  event_link;
	enum m0_pool_nd_state          old_state = M0_PNDS_FAILED;
	int                            rc = 0;
	int                            i;

	M0_PRE(pm != NULL);
	M0_PRE(event != NULL);

	M0_SET0(&event_link);
	state = pm->pm_state;

	if (!M0_IN(event->pe_type, (M0_POOL_NODE, M0_POOL_DEVICE)))
		return M0_ERR(-EINVAL);

	if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE,
				     M0_PNDS_FAILED,
				     M0_PNDS_OFFLINE,
				     M0_PNDS_SNS_REPAIRING,
				     M0_PNDS_SNS_REPAIRED,
				     M0_PNDS_SNS_REBALANCING)))
		return M0_ERR(-EINVAL);

	if ((event->pe_type == M0_POOL_NODE &&
	     event->pe_index >= state->pst_nr_nodes) ||
	    (event->pe_type == M0_POOL_DEVICE &&
	     event->pe_index >= state->pst_nr_devices))
		return M0_ERR(-EINVAL);

	switch (event->pe_type) {
	case M0_POOL_NODE:
		old_state = state->pst_nodes_array[event->pe_index].pn_state;
		break;
	case M0_POOL_DEVICE:
		old_state = state->pst_devices_array[event->pe_index].pd_state;
		break;
	default:
		return M0_ERR(-EINVAL);
	}

	switch (old_state) {
	case M0_PNDS_ONLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_OFFLINE,
						M0_PNDS_FAILED)))
			return M0_ERR(-EINVAL);
		break;
	case M0_PNDS_OFFLINE:
		if (event->pe_state != M0_PNDS_ONLINE)
			return M0_ERR(-EINVAL);
		break;
	case M0_PNDS_FAILED:
		if (event->pe_state != M0_PNDS_SNS_REPAIRING)
			return M0_ERR(-EINVAL);
		break;
	case M0_PNDS_SNS_REPAIRING:
		if (event->pe_state != M0_PNDS_SNS_REPAIRED)
			return M0_ERR(-EINVAL);
		break;
	case M0_PNDS_SNS_REPAIRED:
		if (event->pe_state != M0_PNDS_SNS_REBALANCING)
			return M0_ERR(-EINVAL);
		break;
	case M0_PNDS_SNS_REBALANCING:
		if (event->pe_state != M0_PNDS_ONLINE)
			return M0_ERR(-EINVAL);
		break;
	default:
		return M0_ERR(-EINVAL);
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
		struct m0_poolmach_event_link *new_link;
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
	return M0_RC(rc);
}

M0_INTERNAL int m0_poolmach_state_query(struct m0_poolmach *pm,
					const struct m0_poolmach_versions
					*from,
					const struct m0_poolmach_versions
					*to, struct m0_tl *event_list_head)
{
	struct m0_poolmach_state      *state;
	struct m0_poolmach_versions    zero = { {0, 0} };
	struct m0_poolmach_event_link *scan;
	struct m0_poolmach_event_link *event_link;
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
			struct m0_poolmach_event_link *tmp;
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
	return M0_RC(rc);
}

M0_INTERNAL int
m0_poolmach_current_version_get(struct m0_poolmach *pm,
				struct m0_poolmach_versions *curr)
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
		return M0_ERR(-EINVAL);

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
		return M0_ERR(-EINVAL);

	m0_rwlock_read_lock(&pm->pm_lock);
	*state_out = pm->pm_state->pst_nodes_array[node_index].pn_state;
	m0_rwlock_read_unlock(&pm->pm_lock);

	return 0;
}

M0_INTERNAL bool
m0_poolmach_device_is_in_spare_usage_array(struct m0_poolmach *pm,
					   uint32_t device_index)
{
	return m0_exists(i, pm->pm_state->pst_max_device_failures,
		pm->pm_state->pst_spare_usage_array[i].psu_device_index ==
				device_index);
}

M0_INTERNAL int m0_poolmach_sns_repair_spare_query(struct m0_poolmach *pm,
						   uint32_t device_index,
						   uint32_t *spare_slot_out)
{
	struct m0_pool_spare_usage *spare_usage_array;
	enum m0_pool_nd_state       device_state;
	uint32_t                    i;
	int                         rc;

	M0_PRE(pm != NULL);
	M0_PRE(spare_slot_out != NULL);

	if (device_index >= pm->pm_state->pst_nr_devices)
		return M0_ERR(-EINVAL);

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

	return M0_RC(rc);
}

M0_INTERNAL bool
m0_poolmach_sns_repair_spare_contains_data(struct m0_poolmach *p,
					   uint32_t spare_slot,
					   bool check_state)
{
	const struct m0_pool_spare_usage *u =
		&p->pm_state->pst_spare_usage_array[spare_slot];
	return u->psu_device_index != POOL_PM_SPARE_SLOT_UNUSED &&
	       u->psu_device_state != (check_state ? M0_PNDS_SNS_REPAIRING :
						     M0_PNDS_SNS_REPAIRED);
}

M0_INTERNAL int m0_poolmach_sns_rebalance_spare_query(struct m0_poolmach *pm,
						      uint32_t device_index,
						      uint32_t *spare_slot_out)
{
	struct m0_pool_spare_usage *spare_usage_array;
	enum m0_pool_nd_state       device_state;
	uint32_t                    i;
	int                         rc;

	M0_PRE(pm != NULL);
	M0_PRE(spare_slot_out != NULL);

	if (device_index >= pm->pm_state->pst_nr_devices)
		return M0_ERR(-EINVAL);

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

	return M0_RC(rc);

}

static int lno = 0;

/* Change this value to make it more verbose, e.g. to M0_ERROR */
#define dump_level M0_DEBUG

M0_INTERNAL void m0_poolmach_version_dump(struct m0_poolmach_versions *v)
{
	M0_LOG(dump_level, "%4d:readv=%llx writev=%llx", lno,
			(unsigned long long)v->pvn_version[PVE_READ],
			(unsigned long long)v->pvn_version[PVE_WRITE]);
	lno++;
}

M0_INTERNAL void m0_poolmach_event_dump(struct m0_poolmach_event *e)
{
	M0_LOG(dump_level, "%4d:pe_type=%6s pe_index=%x, pe_state=%10d",
			lno,
			e->pe_type == M0_POOL_DEVICE ? "device":"node",
			e->pe_index, e->pe_state);
	lno++;
}

M0_INTERNAL void m0_poolmach_event_list_dump(struct m0_poolmach *pm)
{
	struct m0_tl                  *head = &pm->pm_state->pst_events_list;
	struct m0_poolmach_event_link *scan;

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

