/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 05/03/2013
 */

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"      /* M0_LOG */
#include "lib/errno.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "pool/pool.h"
#include "lib/misc.h"
#include "lib/uuid.h"
#include "be/be.h"

/**
   @addtogroup pool

   @{
 */

struct m0_poolnode_rec {
	enum m0_pool_nd_state pn_state;
	struct m0_uint128     pn_node_id;
};

struct m0_pooldev_rec {
	enum m0_pool_nd_state pd_state;
	struct m0_uint128     pd_dev_id;
	struct m0_uint128     pd_node_id;
};

struct m0_pool_spare_usage_rec {
	uint32_t              psu_device_index;
	enum m0_pool_nd_state psu_device_state;
};

struct m0_poolmach_state_rec {
	struct m0_pool_version_numbers psr_version;
	uint32_t                       psr_nr_nodes;
	uint32_t                       psr_nr_devices;
	uint32_t                       psr_max_node_failures;
	uint32_t                       psr_max_device_failures;
};

struct m0_pool_event_rec {
	struct m0_pool_event           per_event;
};

#ifndef __KERNEL__

M0_INTERNAL void m0_poolmach_store_credit(struct m0_poolmach        *pm,
					  struct m0_be_tx_credit *accum)
{
	struct m0_pool_event_link *event_link;
	struct m0_poolmach_state  *state = pm->pm_state;

	m0_be_tx_credit_add(accum, &M0_BE_TX_CREDIT_TYPE(state));
	m0_be_tx_credit_add(accum,
			    &M0_BE_TX_CREDIT(state->pst_nr_nodes,
				sizeof (*state->pst_nodes_array)));
	m0_be_tx_credit_add(accum,
			    &M0_BE_TX_CREDIT(state->pst_nr_devices,
				sizeof (*state->pst_devices_array)));
	m0_be_tx_credit_add(accum,
			&M0_BE_TX_CREDIT(state->pst_max_device_failures,
				sizeof (*state->pst_spare_usage_array)));
	M0_BE_ALLOC_CREDIT_PTR(event_link, pm->pm_be_seg, accum);
	m0_be_tx_credit_add(accum, &M0_BE_TX_CREDIT_TYPE(*event_link));
	m0_be_tx_credit_add(accum, &M0_BE_TX_CREDIT_TYPE(struct m0_tlink));
	m0_be_tx_credit_add(accum, &M0_BE_TX_CREDIT_TYPE(struct m0_tlink));
}

/**
 * Store all pool machine state and events into db.
 *
 * Existing records are overwritten.
 */
M0_INTERNAL int m0_poolmach_store(struct m0_poolmach        *pm,
				  struct m0_be_tx           *tx,
				  struct m0_pool_event_link *event_link)
{
	struct m0_poolmach_state  *dest = pm->pm_state;
	struct m0_pool_event_link *new_link;
	int                        rc = 0;
	M0_ENTRY();

	M0_BE_ALLOC_PTR_SYNC(new_link, pm->pm_be_seg, tx);
	if (new_link == NULL)
		return -ENOMEM;

	*new_link = *event_link;
	poolmach_events_tlink_init_at_tail(new_link, &dest->pst_events_list);


	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx, dest);
	M0_BE_TX_CAPTURE_ARR(pm->pm_be_seg, tx, dest->pst_nodes_array,
			     dest->pst_nr_nodes);
	M0_BE_TX_CAPTURE_ARR(pm->pm_be_seg, tx, dest->pst_devices_array,
			     dest->pst_nr_devices);
	M0_BE_TX_CAPTURE_ARR(pm->pm_be_seg, tx, dest->pst_spare_usage_array,
			     dest->pst_max_device_failures);
	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx, new_link);
	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx,
			     new_link->pel_linkage.t_link.ll_prev);
	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx,
			     new_link->pel_linkage.t_link.ll_next);
	return rc;
}

static int m0_poolmach_load(struct m0_poolmach       *pm,
			    struct m0_poolmach_state *pm_state_on_disk,
			    uint32_t                  nr_nodes,
			    uint32_t                  nr_devices,
			    uint32_t                  max_node_failures,
			    uint32_t                  max_device_failures)
{
	int rc = 0;

	if (pm_state_on_disk->pst_nr_nodes != nr_nodes ||
	    pm_state_on_disk->pst_nr_devices != nr_devices + 1 ||
	    pm_state_on_disk->pst_max_node_failures != max_node_failures ||
	    pm_state_on_disk->pst_max_device_failures != max_device_failures) {
		M0_LOG(M0_ERROR, "Invalid pool configuration. Using stale pool "
				 "info? On-disk pool param: %u:%u:%u:%u, "
				 "Requested pool param: %u:%u:%u:%u",
				 pm_state_on_disk->pst_nr_nodes,
				 pm_state_on_disk->pst_nr_devices,
				 pm_state_on_disk->pst_max_node_failures,
				 pm_state_on_disk->pst_max_device_failures,
				 nr_nodes,
				 nr_devices,
				 max_node_failures,
				 max_device_failures);
		return -EINVAL;
	}

	return rc;
}

M0_INTERNAL int m0_poolmach_store_init(struct m0_poolmach *pm,
				       struct m0_be_seg   *be_seg,
				       struct m0_sm_group *sm_grp,
				       struct m0_dtm      *dtm,
				       uint32_t            nr_nodes,
				       uint32_t            nr_devices,
				       uint32_t            max_node_failures,
				       uint32_t            max_device_failures)
{
	struct m0_be_tx_credit      cred = {};
	struct m0_be_tx            *tx;
	struct m0_poolmach_state   *state;
	struct m0_poolnode         *nodes_array;
	struct m0_pooldev          *devices_array;
	struct m0_pool_spare_usage *spare_usage_array;
	const char                 *poolmach_name = "poolmach_state";
	int                         i;
	int                         rc;

	M0_PRE(!pm->pm_is_initialised);
	M0_PRE(be_seg != NULL);
	M0_ENTRY();

	M0_ALLOC_PTR(tx);
	if (tx == NULL)
		return -ENOMEM;

	rc = m0_be_seg_dict_lookup(be_seg, poolmach_name, (void**)&state);
	if (rc == 0) {
		rc = m0_poolmach_load(pm, state, nr_nodes, nr_devices,
				      max_node_failures, max_device_failures);
		if (rc == 0)
			pm->pm_state = state;
		goto out;
	} else if (rc != -ENOENT)
		goto out;

	/* Not found from disk. Let's allocate and insert it */
	m0_be_tx_init(tx, 0, be_seg->bs_domain, sm_grp, NULL, NULL, NULL, NULL);
	M0_BE_ALLOC_CREDIT_PTR(state, be_seg, &cred);
	M0_BE_ALLOC_CREDIT_ARR(nodes_array, nr_nodes, be_seg, &cred);
	M0_BE_ALLOC_CREDIT_ARR(devices_array, nr_devices + 1, be_seg, &cred);
	M0_BE_ALLOC_CREDIT_ARR(spare_usage_array, max_device_failures, be_seg,
			       &cred);
	m0_be_seg_dict_insert_credit(be_seg, poolmach_name, &cred);
	m0_be_tx_prep(tx, &cred);
	rc = m0_be_tx_open_sync(tx);
	if (rc == 0) {
		M0_BE_ALLOC_PTR_SYNC(state, be_seg, tx);
		M0_BE_ALLOC_ARR_SYNC(nodes_array, nr_nodes, be_seg, tx);
		M0_BE_ALLOC_ARR_SYNC(devices_array, nr_devices + 1, be_seg, tx);
		M0_BE_ALLOC_ARR_SYNC(spare_usage_array, max_device_failures,
				     be_seg, tx);
		M0_ASSERT(state != NULL);
		M0_ASSERT(nodes_array != NULL);
		M0_ASSERT(devices_array != NULL);
		M0_ASSERT(spare_usage_array != NULL);

		rc = m0_be_seg_dict_insert(be_seg, tx, poolmach_name,
					   state);
		M0_ASSERT(rc == 0);
		pm->pm_state = state;
		state->pst_nodes_array         = nodes_array;
		state->pst_devices_array       = devices_array;
		state->pst_spare_usage_array   = spare_usage_array;
		state->pst_nr_nodes            = nr_nodes;
		state->pst_nr_devices          = nr_devices + 1;
		state->pst_max_node_failures   = max_node_failures;
		state->pst_max_device_failures = max_device_failures;
		for (i = 0; i < state->pst_nr_nodes; i++) {
			state->pst_nodes_array[i].pn_state = M0_PNDS_ONLINE;
			state->pst_nodes_array[i].pn_id    = NULL;
		}

		for (i = 0; i < state->pst_nr_devices; i++) {
			state->pst_devices_array[i].pd_state = M0_PNDS_ONLINE;
			state->pst_devices_array[i].pd_id    = NULL;
			state->pst_devices_array[i].pd_node  = NULL;
		}

		for (i = 0; i < state->pst_max_device_failures; i++) {
			state->pst_spare_usage_array[i].psu_device_index =
						POOL_PM_SPARE_SLOT_UNUSED;
		}
		poolmach_events_tlist_init(&state->pst_events_list);

		M0_BE_TX_CAPTURE_PTR(be_seg, tx, state);
		M0_BE_TX_CAPTURE_ARR(be_seg, tx, nodes_array, nr_nodes);
		M0_BE_TX_CAPTURE_ARR(be_seg, tx, devices_array, nr_devices);
		M0_BE_TX_CAPTURE_ARR(be_seg, tx, spare_usage_array,
				     max_device_failures);
		m0_be_tx_close_sync(tx);
		M0_LOG(M0_DEBUG, "On-disk pool param: %u:%u:%u:%u",
				 state->pst_nr_nodes,
				 state->pst_nr_devices,
				 state->pst_max_node_failures,
				 state->pst_max_device_failures);
	}

	m0_be_tx_fini(tx);

out:
	m0_free(tx);
	return rc;
}

/**
 * Destroy pool machine state from persistent storage completely.
 */
M0_INTERNAL int m0_poolmach_store_destroy(struct m0_poolmach *pm,
					  struct m0_be_seg   *be_seg,
					  struct m0_sm_group *sm_grp,
					  struct m0_dtm      *dtm)
{
	struct m0_be_tx_credit      cred = {};
	struct m0_be_tx            *tx;
	struct m0_poolmach_state   *state;
	struct m0_poolnode         *nodes_array;
	struct m0_pooldev          *devices_array;
	struct m0_pool_spare_usage *spare_usage_array;
	const char                 *poolmach_name = "poolmach_state";
	struct m0_pool_event_link  *scan;
	struct m0_list_link        *prev;
	struct m0_list_link        *next;
	int                         rc;

	M0_PRE(pm->pm_is_initialised);
	M0_PRE(be_seg != NULL);
	M0_ENTRY();

	M0_ALLOC_PTR(tx);
	if (tx == NULL)
		return -ENOMEM;

	rc = m0_be_seg_dict_lookup(be_seg, poolmach_name, (void**)&state);
	if (rc != 0)
		goto out;

	M0_BE_FREE_CREDIT_PTR(scan, pm->pm_be_seg, &cred);
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT_TYPE(*scan));
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT_TYPE(struct m0_tlink));
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT_TYPE(struct m0_tlink));


	m0_tl_for(poolmach_events, &state->pst_events_list, scan) {
		/* save prev & next in temprary variables. */
		prev = scan->pel_linkage.t_link.ll_prev;
		next = scan->pel_linkage.t_link.ll_next;

		m0_be_tx_init(tx, 0, be_seg->bs_domain, sm_grp,
			      NULL, NULL, NULL, NULL);
		m0_be_tx_prep(tx, &cred);
		rc = m0_be_tx_open_sync(tx);
		M0_ASSERT(rc == 0);

		poolmach_events_tlink_del_fini(scan);
		M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx, scan);
		M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx, prev);
		M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx, next);

		M0_BE_FREE_PTR_SYNC(scan, be_seg, tx);
		m0_be_tx_close_sync(tx);
		m0_be_tx_fini(tx);
	} m0_tl_endfor;


	M0_SET0(&cred);
	m0_be_tx_init(tx, 0, be_seg->bs_domain, sm_grp, NULL, NULL, NULL, NULL);
	M0_BE_FREE_CREDIT_PTR(state, be_seg, &cred);
	M0_BE_FREE_CREDIT_ARR(nodes_array, state->pst_nr_nodes, be_seg, &cred);
	M0_BE_FREE_CREDIT_ARR(devices_array, state->pst_nr_devices, be_seg,
			      &cred);
	M0_BE_FREE_CREDIT_ARR(spare_usage_array, state->pst_max_device_failures,
			      be_seg, &cred);
	m0_be_seg_dict_delete_credit(be_seg, poolmach_name, &cred);
	m0_be_tx_prep(tx, &cred);
	rc = m0_be_tx_open_sync(tx);
	M0_ASSERT(rc == 0);
	poolmach_events_tlist_init(&state->pst_events_list);
	M0_BE_FREE_PTR_SYNC(state->pst_nodes_array, be_seg, tx);
	M0_BE_FREE_PTR_SYNC(state->pst_devices_array, be_seg, tx);
	M0_BE_FREE_PTR_SYNC(state->pst_spare_usage_array, be_seg, tx);
	M0_BE_FREE_PTR_SYNC(state, be_seg, tx);

	rc = m0_be_seg_dict_delete(be_seg, tx, poolmach_name);
	M0_ASSERT(rc == 0);
	m0_be_tx_close_sync(tx);
	m0_be_tx_fini(tx);

out:
	m0_free(tx);
	return rc;
}


#else

M0_INTERNAL int m0_poolmach_event_store(struct m0_poolmach *pm,
					struct m0_be_tx *tx,
					struct m0_pool_event_link *event_link)
{
	return 0;
}


M0_INTERNAL int m0_poolmach_store(struct m0_poolmach *pm,
				  struct m0_be_tx *tx)
{
	return 0;
}

M0_INTERNAL int m0_poolmach_store_init(struct m0_poolmach *pm,
				       struct m0_be_seg   *be_seg,
				       struct m0_sm_group *sm_grp,
				       struct m0_dtm      *dtm,
				       uint32_t            nr_nodes,
				       uint32_t            nr_devices,
				       uint32_t            max_node_failures,
				       uint32_t            max_device_failures)
{
	return 0;
}

#endif

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
