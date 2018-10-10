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
#include "lib/finject.h"      /* M0_FI_ENABLED */
#include "stob/stob.h"
#include "pool/pool.h"
#include "conf/confc.h"       /* m0_confc_close */
#include "conf/helpers.h"     /* m0_conf_drive_get */
#include "lib/misc.h"         /* M0_SET0 */
#include "lib/uuid.h"
#include "be/seg0.h"
#include "be/op.h"            /* M0_BE_OP_SYNC */
#include "module/instance.h"
#include "pool/pm_internal.h" /* m0_poolmach__state_init */

/**
   @addtogroup pool

   @{
 */

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
static const char *s_suffix = "000000001";

M0_INTERNAL void m0_poolmach_store_credit(struct m0_poolmach     *pm,
					  struct m0_be_tx_credit *accum)
{
	struct m0_poolmach_event_link *event_link;
	struct m0_poolmach_state      *state = pm->pm_state;

	M0_PRE(state != NULL);
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

static void poolmach_state_capture(struct m0_be_seg         *seg,
				   struct m0_be_tx          *tx,
				   struct m0_poolmach_state *state)
{
	M0_BE_TX_CAPTURE_PTR(seg, tx, state);
	M0_BE_TX_CAPTURE_ARR(seg, tx, state->pst_nodes_array,
			     state->pst_nr_nodes);
	M0_BE_TX_CAPTURE_ARR(seg, tx, state->pst_devices_array,
			     state->pst_nr_devices);
	M0_BE_TX_CAPTURE_ARR(seg, tx, state->pst_spare_usage_array,
			     state->pst_max_device_failures);
}

/**
 * Store all pool machine state and events into db.
 *
 * Existing records are overwritten.
 */
M0_INTERNAL int m0_poolmach__store(struct m0_poolmach            *pm,
				   struct m0_be_tx               *tx,
				   struct m0_poolmach_event_link *event_link)
{
	struct m0_poolmach_state      *dest = pm->pm_state;
	struct m0_poolmach_event_link *new_link;

	M0_ENTRY();

	M0_BE_ALLOC_PTR_SYNC(new_link, pm->pm_be_seg, tx);
	if (new_link == NULL)
		return M0_ERR(-ENOMEM);

	*new_link = *event_link;
	poolmach_events_tlink_init_at_tail(new_link, &dest->pst_events_list);

	poolmach_state_capture(pm->pm_be_seg, tx, dest);
	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx, new_link);
	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx,
			     new_link->pel_linkage.t_link.ll_prev);
	M0_BE_TX_CAPTURE_PTR(pm->pm_be_seg, tx,
			     new_link->pel_linkage.t_link.ll_next);
	return M0_RC(0);
}

static bool poolmach_check(struct m0_poolmach_state *pm_state_on_disk,
			   uint32_t                  nr_nodes,
			   uint32_t                  nr_devices,
			   uint32_t                  max_node_failures,
			   uint32_t                  max_device_failures)
{
	if (pm_state_on_disk->pst_nr_nodes != nr_nodes ||
	    pm_state_on_disk->pst_nr_devices != nr_devices ||
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
		return false;
	}

	return true;
}

M0_INTERNAL
void m0_poolmach_store_init_creds_add(struct m0_be_seg       *be_seg,
				      uint32_t                nr_nodes,
				      uint32_t                nr_devices,
				      uint32_t                max_dev_fails,
				      struct m0_be_tx_credit *cred)
{
	struct m0_be_allocator *be_alloc = m0_be_seg_allocator(be_seg);
	struct m0_buf           data = {};

	M0_ENTRY();
	m0_be_allocator_credit(be_alloc, M0_BAO_ALLOC,
		       sizeof(struct m0_poolmach_state), 0, cred);
	m0_be_allocator_credit(be_alloc, M0_BAO_ALLOC,
		       nr_nodes * sizeof(struct m0_poolnode), 0, cred);
	m0_be_allocator_credit(be_alloc, M0_BAO_ALLOC,
		       nr_devices * sizeof(struct m0_pooldev), 0, cred);
	m0_be_allocator_credit(be_alloc, M0_BAO_ALLOC,
		       max_dev_fails * sizeof(struct m0_pool_spare_usage), 0,
		       cred);
	m0_be_0type_add_credit(be_seg->bs_domain, &m0_be_pool0, s_suffix, &data,
		       cred);
	M0_LEAVE("cred = "BETXCR_F, BETXCR_P(cred));
}

static int poolmach_store_create(struct m0_be_seg   *be_seg,
				 struct m0_be_tx    *be_tx,
				 uint32_t            nr_nodes,
				 uint32_t            nr_devices,
				 uint32_t            max_node_failures,
				 uint32_t            max_device_failures,
				 struct m0_poolmach *pm)
{
	struct m0_poolmach_state   *state;
	struct m0_poolnode         *nodes_array;
	struct m0_pooldev          *devices_array;
	struct m0_pool_spare_usage *spare_usage_array;
	struct m0_be_domain        *bedom = be_seg->bs_domain;
	struct m0_buf               data = {};
	int                         rc;

	M0_BE_ALLOC_PTR_SYNC(state, be_seg, be_tx);
	M0_BE_ALLOC_ARR_SYNC(nodes_array, nr_nodes, be_seg, be_tx);
	M0_BE_ALLOC_ARR_SYNC(devices_array, nr_devices,
			     be_seg, be_tx);
	M0_BE_ALLOC_ARR_SYNC(spare_usage_array, max_device_failures,
			     be_seg, be_tx);
	M0_ASSERT(state != NULL);
	M0_ASSERT(nodes_array != NULL);
	M0_ASSERT(devices_array != NULL);
	M0_ASSERT(spare_usage_array != NULL);

	m0_poolmach__state_init(state, nodes_array, nr_nodes, devices_array,
				nr_devices, spare_usage_array,
				max_node_failures, max_device_failures, pm);
	poolmach_state_capture(be_seg, be_tx, state);

	data = M0_BUF_INIT_PTR(&state);
	rc = m0_be_0type_add(&m0_be_pool0, bedom, be_tx, s_suffix, &data);
	M0_ASSERT(rc == 0);

	M0_LOG(M0_DEBUG, "On-disk pool param: %u:%u:%u:%u",
			 state->pst_nr_nodes,
			 state->pst_nr_devices,
			 state->pst_max_node_failures,
			 state->pst_max_device_failures);

	return M0_RC(0);
}

M0_INTERNAL
int m0_poolmach__store_init(struct m0_be_seg          *be_seg,
			    struct m0_be_tx           *be_tx,
		            uint32_t                   nr_nodes,
			    uint32_t                   nr_devices,
			    uint32_t                   max_node_failures,
			    uint32_t                   max_device_failures,
			    struct m0_poolmach_state **state,
			    struct m0_poolmach        *pm)
{
	int rc;

	M0_PRE(be_seg != NULL);
	M0_ENTRY("sid: %"PRIu64, be_seg->bs_id);

	if (M0_FI_ENABLED("recreate_pm_store"))
		/**
		 * Workaround over several pms for UTs.
		 * In real systems there is only one pm with backing store.
		 */
		m0_get()->i_pool_module = NULL;

	if (m0_get()->i_pool_module != NULL)
		rc = poolmach_check(m0_get()->i_pool_module, nr_nodes,
			            nr_devices, max_node_failures,
			            max_device_failures) &&
	             m0_be_seg_contains(be_seg, m0_get()->i_pool_module) ?
		     0 : -EINVAL;
	else
		rc = poolmach_store_create(be_seg, be_tx, nr_nodes, nr_devices,
					   max_node_failures,
					   max_device_failures, pm);
	if (rc == 0)
		*state = m0_get()->i_pool_module;
	return M0_RC(rc);
}

static int poolmach_store_destroy(struct m0_be_seg   *be_seg,
				  struct m0_sm_group *sm_grp)
{
	struct m0_be_tx_credit         cred = {};
	struct m0_be_tx               *tx;
	struct m0_poolmach_state      *state;
	struct m0_poolnode            *nodes_array;
	struct m0_pooldev             *devices_array;
	struct m0_pool_spare_usage    *spare_usage_array;
	struct m0_poolmach_event_link *scan;
	struct m0_list_link           *prev;
	struct m0_list_link           *next;
	const char                    *id = "000000001";
	int                            rc;

	M0_PRE(be_seg != NULL);
	M0_ENTRY("sid: %"PRIu64, be_seg->bs_id);

	state = m0_get()->i_pool_module;
	if (state == NULL)
		return M0_ERR(-ENOENT);

	M0_ALLOC_PTR(tx);
	if (tx == NULL)
		return M0_ERR(-ENOMEM);

	M0_BE_FREE_CREDIT_PTR(scan, be_seg, &cred);
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT_TYPE(*scan));
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT_TYPE(struct m0_tlink));
	m0_be_tx_credit_add(&cred, &M0_BE_TX_CREDIT_TYPE(struct m0_tlink));


	m0_tl_for(poolmach_events, &state->pst_events_list, scan) {
		/* save prev & next in temprary variables. */
		prev = scan->pel_linkage.t_link.ll_prev;
		next = scan->pel_linkage.t_link.ll_next;

		M0_SET0(tx);
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
	M0_SET0(tx);
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

	return M0_RC(rc);
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

M0_INTERNAL int m0_poolmach_credit_calc(struct m0_poolmach *pm,
					struct m0_confc *confc,
					struct m0_pools_common *cc_pools,
					struct m0_be_tx_credit *tx_cred)
{

	struct m0_pool_spare_usage  *spare_array;
	struct m0_pooldev           *dev_array;
	struct m0_conf_drive        *disk;
	struct m0_pool_version      *pool_ver;
	struct m0_conf_pver        **conf_pver;
	struct m0_poolmach          *pv_pm;
	struct m0_fid               *dev_fid;
	uint32_t                     dev_id;
	uint32_t                     i;
	uint32_t                     j;
	uint64_t                     max_failures;
	int                          rc = 0;

	/* Calculate credits for pool-machines in
	 * different pool-versions.
	 */
	spare_array  = pm->pm_state->pst_spare_usage_array;
	dev_array    = pm->pm_state->pst_devices_array;
	max_failures = pm->pm_state->pst_max_device_failures;
	for (i = 0; i < max_failures; ++i) {
		dev_id    = spare_array[i].psu_device_index;
		if (dev_id == POOL_PM_SPARE_SLOT_UNUSED)
			continue;
		dev_fid = &dev_array[dev_id].pd_id;
		rc = m0_conf_drive_get(confc, dev_fid, &disk);
		if (rc != 0)
			return M0_RC(rc);
		conf_pver = disk->ck_pvers;
		if (conf_pver == NULL) {
			m0_confc_close(&disk->ck_obj);
			return M0_ERR(-EINVAL);
		}
		for (j = 0; conf_pver[j] != NULL; ++j) {
			pool_ver =
				m0_pool_version_find(cc_pools,
						&conf_pver[j]->pv_obj.co_id);
			pv_pm = &pool_ver->pv_mach;
			m0_poolmach_store_credit(pv_pm,
					tx_cred);
		}
		m0_confc_close(&disk->ck_obj);
	}
	return M0_RC(rc);
}

#else /* __KERNEL__ */

M0_INTERNAL int m0_poolmach_event_store(struct m0_poolmach *pm,
					struct m0_be_tx *tx,
					struct m0_poolmach_event_link *event_link)
{
	return 0;
}

M0_INTERNAL int m0_poolmach__store(struct m0_poolmach            *pm,
				   struct m0_be_tx               *tx,
				   struct m0_poolmach_event_link *event_link)
{
	return 0;
}

M0_INTERNAL
int m0_poolmach__store_init(struct m0_be_seg          *be_seg,
			    struct m0_be_tx           *be_tx,
		            uint32_t                   nr_nodes,
			    uint32_t                   nr_devices,
			    uint32_t                   max_node_failures,
			    uint32_t                   max_device_failures,
			    struct m0_poolmach_state **state,
			    struct m0_poolmach        *pm)
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
