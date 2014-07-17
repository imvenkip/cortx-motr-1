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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "pool/pool.h"
#include "lib/misc.h"
#include "lib/uuid.h"
#include "be/be.h"
#include "be/seg0.h"
#include "module/instance.h"

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
	struct m0_pool_event per_event;
};

static int pool0_init(struct m0_be_domain *dom, const char *suffix,
		      const struct m0_buf *data)
{
	struct m0 *m0inst = m0_get();
	struct m0_poolmach_state **state;

	M0_PRE(m0inst->i_pool_module == NULL);
	M0_ENTRY();
	state = data->b_addr;
	m0inst->i_pool_module = *state;
	return M0_RC(0);
}

static void pool0_fini(struct m0_be_domain *dom, const char *suffix,
		       const struct m0_buf *data)
{
	struct m0 *m0inst = m0_get();
	M0_ENTRY();
	m0inst->i_pool_module = NULL;
	M0_LEAVE();
}

struct m0_be_0type m0_be_pool0 = {
	.b0_name = "M0_BE:POOL",
	.b0_init = pool0_init,
	.b0_fini = pool0_fini,
};

#ifndef __KERNEL__

M0_INTERNAL void m0_poolmach_store_credit(struct m0_poolmach     *pm,
					  struct m0_be_tx_credit *accum)
{
	struct m0_pool_event_link *event_link;
	struct m0_poolmach_state  *state = pm->pm_state;

	m0_be_tx_credit_add(accum, &M0_BE_TX_CREDIT_TYPE(state));
	m0_be_tx_credit_add(accum,
			    &M0_BE_TX_CREDIT(state->pst_nr_nodes,
				sizeof *state->pst_nodes_array *
				state->pst_nr_nodes));
	m0_be_tx_credit_add(accum,
			    &M0_BE_TX_CREDIT(state->pst_nr_devices,
				sizeof *state->pst_devices_array *
				state->pst_nr_devices));
	m0_be_tx_credit_add(accum,
			&M0_BE_TX_CREDIT(state->pst_max_device_failures,
				sizeof *state->pst_spare_usage_array *
				state->pst_max_device_failures));
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

static int poolmach_store_create(struct m0_be_seg   *be_seg,
				 struct m0_sm_group *sm_grp,
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
	struct m0_be_domain        *bedom = be_seg->bs_domain;
	struct m0_buf               data = {};
	const char                 *id = "000000001";
	int                         i;
	int                         rc;

	M0_ALLOC_PTR(tx);
	if (tx == NULL)
		return -ENOMEM;

	/* Not found from disk. Let's allocate and insert it */
	m0_be_tx_init(tx, 0, bedom, sm_grp, NULL, NULL, NULL, NULL);
	M0_BE_ALLOC_CREDIT_PTR(state, be_seg, &cred);
	M0_BE_ALLOC_CREDIT_ARR(nodes_array, nr_nodes, be_seg, &cred);
	M0_BE_ALLOC_CREDIT_ARR(devices_array, nr_devices + 1, be_seg, &cred);
	M0_BE_ALLOC_CREDIT_ARR(spare_usage_array, max_device_failures, be_seg,
			       &cred);
	m0_be_0type_add_credit(bedom, &m0_be_pool0, id, &data, &cred);
	m0_be_tx_prep(tx, &cred);
	rc = m0_be_tx_exclusive_open_sync(tx);
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

		data = M0_BUF_INIT_PTR(&state);
		rc = m0_be_0type_add(&m0_be_pool0, bedom, tx, id, &data);
		M0_ASSERT(rc == 0);

		m0_be_tx_close_sync(tx);
		M0_LOG(M0_DEBUG, "On-disk pool param: %u:%u:%u:%u",
				 state->pst_nr_nodes,
				 state->pst_nr_devices,
				 state->pst_max_node_failures,
				 state->pst_max_device_failures);
	}

	m0_be_tx_fini(tx);
	m0_free(tx);
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
	int rc;

	M0_PRE(!pm->pm_is_initialised);
	M0_PRE(be_seg != NULL);

	M0_ENTRY("sid: %"PRIu64, be_seg->bs_id);

	/* XXX: Workaround over serveral pms. In real system has to be only one. */
	if (!m0_be_seg_contains(be_seg, m0_get()->i_pool_module))
		m0_get()->i_pool_module = NULL;

	rc = m0_get()->i_pool_module != NULL ?
		m0_poolmach_load(pm, m0_get()->i_pool_module, nr_nodes,
				 nr_devices, max_node_failures,
				 max_device_failures) :
		poolmach_store_create(be_seg, sm_grp, nr_nodes, nr_devices,
				      max_node_failures, max_device_failures);

	if (rc == 0)
		pm->pm_state = m0_get()->i_pool_module;

	return rc;
}

static int poolmach_store_destroy(struct m0_be_seg   *be_seg,
				  struct m0_sm_group *sm_grp)
{
	struct m0_be_tx_credit      cred = {};
	struct m0_be_tx            *tx;
	struct m0_poolmach_state   *state;
	struct m0_poolnode         *nodes_array;
	struct m0_pooldev          *devices_array;
	struct m0_pool_spare_usage *spare_usage_array;
	struct m0_pool_event_link  *scan;
	struct m0_list_link        *prev;
	struct m0_list_link        *next;
	const char                 *id = "000000001";
	int                         rc;

	M0_PRE(be_seg != NULL);
	M0_ENTRY("sid: %"PRIu64, be_seg->bs_id);

	state = m0_get()->i_pool_module;
	if (state == NULL)
		return -ENOENT;

	M0_ALLOC_PTR(tx);
	if (tx == NULL)
		return -ENOMEM;

	M0_BE_FREE_CREDIT_PTR(scan, be_seg, &cred);
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
		M0_BE_TX_CAPTURE_PTR(be_seg, tx, scan);
		M0_BE_TX_CAPTURE_PTR(be_seg, tx, prev);
		M0_BE_TX_CAPTURE_PTR(be_seg, tx, next);

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
	m0_be_0type_del_credit(be_seg->bs_domain, &m0_be_pool0, id, &cred);
	m0_be_tx_prep(tx, &cred);
	rc = m0_be_tx_exclusive_open_sync(tx);
	M0_ASSERT(rc == 0);
	poolmach_events_tlist_init(&state->pst_events_list);
	M0_BE_FREE_PTR_SYNC(state->pst_nodes_array, be_seg, tx);
	M0_BE_FREE_PTR_SYNC(state->pst_devices_array, be_seg, tx);
	M0_BE_FREE_PTR_SYNC(state->pst_spare_usage_array, be_seg, tx);
	M0_BE_FREE_PTR_SYNC(state, be_seg, tx);

	rc = m0_be_0type_del(&m0_be_pool0, be_seg->bs_domain, tx, id);
	M0_ASSERT(rc == 0);

	m0_be_tx_close_sync(tx);
	m0_be_tx_fini(tx);
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
	M0_PRE(be_seg != NULL);
	M0_PRE(pm->pm_is_initialised);

	M0_ENTRY();
	return M0_RC(poolmach_store_destroy(be_seg, sm_grp));
}

#else /* __KERNEL__ */

M0_INTERNAL int m0_poolmach_event_store(struct m0_poolmach *pm,
					struct m0_be_tx *tx,
					struct m0_pool_event_link *event_link)
{
	return 0;
}

M0_INTERNAL int m0_poolmach_store(struct m0_poolmach *pm, struct m0_be_tx *tx)
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

/** @} end group pool */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
