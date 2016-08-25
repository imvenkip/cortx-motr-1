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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "pool/pool.h"
#include "reqh/reqh.h"             /* m0_reqh */
#include "conf/confc.h"
#include "conf/diter.h"            /* m0_conf_diter_init */
#include "conf/obj_ops.h"          /* m0_conf_obj_get_lock */
#include "ioservice/fid_convert.h" /* m0_fid_convert_gob2cob */
#include "pool/pm_internal.h"
#include "ha/failvec.h"         /* m0_ha_failvec_fetch */

/**
   @addtogroup poolmach

   @{
 */
M0_TL_DESCR_DEFINE(poolmach_events, "pool machine events list", M0_INTERNAL,
		   struct m0_poolmach_event_link, pel_linkage, pel_magic,
		   M0_POOL_EVENTS_LIST_MAGIC, M0_POOL_EVENTS_HEAD_MAGIC);

M0_TL_DEFINE(poolmach_events, M0_INTERNAL, struct m0_poolmach_event_link);

struct poolmach_equeue_link {
	/* An event to be queued. */
	struct m0_poolmach_event pel_event;
	/* Likage into queue. m0_poolmach_state::pst_event_queue */
	struct m0_tlink          pel_linkage;
	uint64_t                 pel_magic;
};

M0_TL_DESCR_DEFINE(poolmach_equeue, "pool machine events queue", static,
		   struct poolmach_equeue_link, pel_linkage, pel_magic,
		   100, 101);

M0_TL_DEFINE(poolmach_equeue, static, struct poolmach_equeue_link);

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

static bool is_controllerv_or_diskv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE &&
		M0_IN(m0_conf_obj_type(
			      M0_CONF_CAST(obj, m0_conf_objv)->cv_real),
		      (&M0_CONF_CONTROLLER_TYPE, &M0_CONF_DISK_TYPE));
}

static int poolmach_state_update(struct m0_poolmach_state *st,
			         const struct m0_conf_obj *objv_real,
			         uint32_t                 *idx_nodes,
			         uint32_t                 *idx_devices)
{
	int rc = 0;

	M0_ENTRY();

	if (m0_conf_obj_type(objv_real) == &M0_CONF_CONTROLLER_TYPE) {
		st->pst_nodes_array[*idx_nodes].pn_id = objv_real->co_id;
		M0_CNT_INC(*idx_nodes);
	} else if (m0_conf_obj_type(objv_real) == &M0_CONF_DISK_TYPE) {
		struct m0_conf_disk      *d;
		struct m0_poolmach_event  pme;
		struct m0_pooldev        *pdev =
			&st->pst_devices_array[*idx_devices];

		d = M0_CONF_CAST(objv_real, m0_conf_disk);
		pdev->pd_id = d->ck_obj.co_id;
		pdev->pd_sdev_idx = d->ck_dev->sd_dev_idx;
		pdev->pd_index = *idx_devices;
		pdev->pd_node = &st->pst_nodes_array[*idx_nodes];
		m0_conf_obj_get_lock(&d->ck_obj);
		m0_pooldev_clink_add(&pdev->pd_clink, &d->ck_obj.co_ha_chan);

		pme.pe_type = M0_POOL_DEVICE;
		pme.pe_index = *idx_devices;
		pme.pe_state = m0_ha2pm_state_map(d->ck_obj.co_ha_state);

		M0_LOG(M0_DEBUG, "device:"FID_F"index:%d state:%d",
				FID_P(&pdev->pd_id), pdev->pd_index,
				pme.pe_state);
		rc = m0_poolmach_state_transit(pdev->pd_pm, &pme, NULL);

		M0_CNT_INC(*idx_devices);
	} else {
		M0_IMPOSSIBLE("Invalid conf_obj type");
	}
	return M0_RC(rc);
}

static bool poolmach_conf_expired_cb(struct m0_clink *clink)
{
	struct m0_pooldev        *dev;
	struct m0_clink          *cl;
	struct m0_conf_obj       *obj;
	struct m0_poolmach_state *state =
				   container_of(clink, struct m0_poolmach_state,
						pst_conf_exp);
	int                       i;

	M0_ENTRY();
	for (i = 0; i < state->pst_nr_devices; ++i) {
		dev = &state->pst_devices_array[i];
		cl = &dev->pd_clink;
		if (cl->cl_chan == NULL)
			continue;
		obj = container_of(cl->cl_chan, struct m0_conf_obj,
				   co_ha_chan);
		M0_ASSERT(m0_conf_obj_invariant(obj));
		M0_LOG(M0_INFO, "obj "FID_F, FID_P(&obj->co_id));
		m0_pooldev_clink_del(cl);
		m0_confc_close(obj);
		M0_SET0(cl);
	}
	M0_LEAVE();
	return true;
}

static bool poolmach_conf_ready_cb(struct m0_clink *clink)
{
	struct m0_pooldev        *dev;
	struct m0_poolmach_state *state =
				   container_of(clink, struct m0_poolmach_state,
						pst_conf_ready);
	struct m0_reqh           *reqh = container_of(clink->cl_chan,
						      struct m0_reqh,
						      rh_conf_cache_ready);
	struct m0_conf_cache     *cache = &reqh->rh_rconfc.rc_confc.cc_cache;
	struct m0_conf_obj       *obj;
	int                       i;

	M0_ENTRY();
	/*
	 * TODO: the code should process any updates in the configuration tree.
	 * Currently it expects that an interested object wasn't removed from
	 * the tree or new object (m0_conf_sdev) was added.
	 */
	for (i = 0; i < state->pst_nr_devices; ++i) {
		dev = &state->pst_devices_array[i];
		obj = m0_conf_cache_lookup(cache, &dev->pd_id);
		M0_ASSERT_INFO(_0C(obj != NULL) &&
			       _0C(m0_conf_obj_invariant(obj)),
			       "dev->pd_id "FID_F,
			       FID_P(&dev->pd_id));
		m0_pooldev_clink_add(&dev->pd_clink, &obj->co_ha_chan);
		m0_conf_obj_get_lock(obj);
	}
	M0_LEAVE();
	return true;
}

M0_INTERNAL int m0_poolmach_init_by_conf(struct m0_poolmach *pm,
					 struct m0_conf_pver *pver)
{
	struct m0_confc     *confc;
	struct m0_reqh      *reqh;
	struct m0_conf_diter it;
	uint32_t             idx_nodes = 0;
	uint32_t             idx_devices = 0;
	int                  rc;

	confc = m0_confc_from_obj(&pver->pv_obj);
	reqh = m0_confc2reqh(confc);
	rc = m0_conf_diter_init(&it, confc, &pver->pv_obj,
				M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID,
				M0_CONF_CTRLV_DISKVS_FID);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, is_controllerv_or_diskv)) ==
	       M0_CONF_DIRNEXT) {
		M0_LOG(M0_DEBUG, "rc:%d", rc);
		rc = poolmach_state_update(pm->pm_state,
			M0_CONF_CAST(m0_conf_diter_result(&it),
				     m0_conf_objv)->cv_real,
			&idx_nodes, &idx_devices);
		if (rc != 0)
			break;
	}

	m0_conf_diter_fini(&it);
	if (rc != 0)
		return M0_RC(rc);

	m0_clink_init(&pm->pm_state->pst_conf_exp, &poolmach_conf_expired_cb);
	m0_clink_init(&pm->pm_state->pst_conf_ready, &poolmach_conf_ready_cb);
	m0_clink_add_lock(&reqh->rh_conf_cache_exp,
			  &pm->pm_state->pst_conf_exp);
	m0_clink_add_lock(&reqh->rh_conf_cache_ready,
			  &pm->pm_state->pst_conf_ready);
	M0_LOG(M0_DEBUG, "nodes:%d devices: %d", idx_nodes, idx_devices);
	M0_POST(idx_devices <= pm->pm_state->pst_nr_devices);
	return M0_RC(rc);
}

M0_INTERNAL
void m0_poolmach__state_init(struct m0_poolmach_state   *state,
			     struct m0_poolnode         *nodes_array,
			     uint32_t                    nr_nodes,
			     struct m0_pooldev          *devices_array,
			     uint32_t                    nr_devices,
			     struct m0_pool_spare_usage *spare_usage_array,
			     uint32_t                    max_node_failures,
			     uint32_t                    max_device_failures,
			     struct m0_poolmach         *pm)
{
	int i;

	M0_ASSERT(state != NULL);
	M0_ASSERT(nodes_array != NULL);
	M0_ASSERT(devices_array != NULL);
	M0_ASSERT(spare_usage_array != NULL);

	state->pst_version.pvn_version[PVE_READ]  = 0;
	state->pst_version.pvn_version[PVE_WRITE] = 0;
	state->pst_nodes_array         = nodes_array;
	state->pst_devices_array       = devices_array;
	state->pst_spare_usage_array   = spare_usage_array;
	state->pst_nr_nodes            = nr_nodes;
	state->pst_nr_devices          = nr_devices;
	state->pst_max_node_failures   = max_node_failures;
	state->pst_max_device_failures = max_device_failures;

	for (i = 0; i < state->pst_nr_nodes; i++) {
		m0_format_header_pack(&state->pst_nodes_array[i].pn_header,
		    &(struct m0_format_tag){
			.ot_version = M0_POOLNODE_FORMAT_VERSION,
			.ot_type    = M0_FORMAT_TYPE_POOLNODE,
			.ot_footer_offset =
				offsetof(struct m0_poolnode, pn_footer)
		});
		state->pst_nodes_array[i].pn_state = M0_PNDS_ONLINE;
		M0_SET0(&state->pst_nodes_array[i].pn_id);
		m0_format_footer_update(&state->pst_nodes_array[i]);
	}

	for (i = 0; i < state->pst_nr_devices; i++) {
		m0_format_header_pack(&state->pst_devices_array[i].pd_header,
		    &(struct m0_format_tag){
			.ot_version = M0_POOLDEV_FORMAT_VERSION,
			.ot_type    = M0_FORMAT_TYPE_POOLDEV,
			.ot_footer_offset =
				offsetof(struct m0_pooldev, pd_footer)
		});
		state->pst_devices_array[i].pd_state = M0_PNDS_UNKNOWN;
		M0_SET0(&state->pst_devices_array[i].pd_id);
		state->pst_devices_array[i].pd_node = NULL;
		state->pst_devices_array[i].pd_sdev_idx = 0;
		state->pst_devices_array[i].pd_index = i;
		state->pst_devices_array[i].pd_pm = pm;
		pool_failed_devs_tlink_init(&state->pst_devices_array[i]);
		m0_format_footer_update(&state->pst_devices_array[i]);
	}

	for (i = 0; i < state->pst_max_device_failures; i++) {
		m0_format_header_pack(&state->pst_spare_usage_array[i].psu_header,
		    &(struct m0_format_tag){
			.ot_version = M0_POOL_SPARE_USAGE_FORMAT_VERSION,
			.ot_type    = M0_FORMAT_TYPE_POOL_SPARE_USAGE,
			.ot_footer_offset =
				offsetof(struct m0_pool_spare_usage, psu_footer)
		});
		state->pst_spare_usage_array[i].psu_device_index =
					POOL_PM_SPARE_SLOT_UNUSED;
		m0_format_footer_update(&state->pst_spare_usage_array[i]);
	}

	poolmach_events_tlist_init(&state->pst_events_list);
	poolmach_equeue_tlist_init(&state->pst_event_queue);
	/* Gets initialised only after init by configuration. */
	state->pst_su_initialised = false;
}

static void poolmach_init(struct m0_poolmach       *pm,
			  struct m0_pool_version   *pver,
			  struct m0_poolmach_state *pm_state,
			  struct m0_be_seg         *seg)
{
	M0_PRE(!pm->pm_is_initialised);

	M0_ENTRY();
	M0_SET0(pm);
	m0_rwlock_init(&pm->pm_lock);
	pm->pm_state = pm_state;
	pm->pm_be_seg = seg;
	pm->pm_is_initialised = true;
	pm->pm_pver = pver;
	M0_LEAVE();
}

M0_INTERNAL int m0_poolmach_init(struct m0_poolmach *pm,
				 struct m0_pool_version *pver,
				 uint32_t            nr_nodes,
				 uint32_t            nr_devices,
				 uint32_t            max_node_failures,
				 uint32_t            max_device_failures)
{
	struct m0_poolmach_state   *state             = NULL;
	struct m0_poolnode         *nodes_array       = NULL;
	struct m0_pooldev          *devices_array     = NULL;
	struct m0_pool_spare_usage *spare_usage_array = NULL;

	M0_ALLOC_PTR(state);
	M0_ALLOC_ARR(nodes_array, nr_nodes);
	M0_ALLOC_ARR(devices_array, nr_devices);
	M0_ALLOC_ARR(spare_usage_array, max_device_failures);
	if (state == NULL ||
	    nodes_array == NULL ||
	    devices_array == NULL ||
	    spare_usage_array == NULL) {
		m0_free(nodes_array);
		m0_free(devices_array);
		m0_free(spare_usage_array);
		m0_free(state);
		return M0_ERR(-ENOMEM);
	}

	m0_poolmach__state_init(state, nodes_array, nr_nodes, devices_array,
				nr_devices, spare_usage_array,
				max_node_failures, max_device_failures, pm);
	poolmach_init(pm, pver, state, NULL);

	return M0_RC(0);
}

M0_INTERNAL
int m0_poolmach_backed_init2(struct m0_poolmach *pm,
			     struct m0_pool_version *pver,
			     struct m0_be_seg   *be_seg,
			     struct m0_sm_group *sm_grp,
			     uint32_t            nr_nodes,
			     uint32_t            nr_devices,
			     uint32_t            max_node_failures,
			     uint32_t            max_device_failures)
{
#ifndef __KERNEL__
	struct m0_be_tx         tx = {};
	struct m0_be_tx_credit  cred = {};
	struct m0_be_domain    *bedom = be_seg->bs_domain;
	int                     rc;

	m0_be_tx_init(&tx, 0, bedom, sm_grp, NULL, NULL, NULL, NULL);
	m0_poolmach_store_init_creds_add(be_seg, nr_nodes, nr_devices,
					 max_device_failures, &cred);
	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_exclusive_open_sync(&tx);
	if (rc == 0) {
		rc = m0_poolmach_backed_init(pm, pver, be_seg, &tx, nr_nodes,
					     nr_devices, max_node_failures,
					     max_device_failures);
		m0_be_tx_close_sync(&tx);
	}
	m0_be_tx_fini(&tx);
	return M0_RC(rc);
#else
	return M0_ERR(-ENOSYS);
#endif
}

M0_INTERNAL
int m0_poolmach_backed_init(struct m0_poolmach *pm,
			    struct m0_pool_version *pver,
			    struct m0_be_seg   *be_seg,
			    struct m0_be_tx    *be_tx,
			    uint32_t            nr_nodes,
			    uint32_t            nr_devices,
			    uint32_t            max_node_failures,
			    uint32_t            max_device_failures)
{
	int                       rc;
	struct m0_poolmach_state *state;

	M0_ENTRY();
	M0_PRE(be_seg != NULL);
	M0_PRE(!pm->pm_is_initialised);

	rc = m0_poolmach__store_init(be_seg, be_tx, nr_nodes,
				     nr_devices, max_node_failures,
			             max_device_failures,
				     &state, pm);
	if (rc == 0)
		poolmach_init(pm, pver, state, be_seg);
	return M0_RC(rc);
}

M0_INTERNAL void m0_poolmach_fini(struct m0_poolmach *pm)
{
	struct m0_poolmach_event_link *scan;
	struct m0_poolmach_state      *state = pm->pm_state;
	struct m0_pooldev             *pd;
	struct m0_clink               *cl;
	struct m0_conf_obj            *obj;
	int                            i;

	M0_PRE(pm != NULL);

	m0_rwlock_write_lock(&pm->pm_lock);

	if (pm->pm_be_seg == NULL) {
		/*
		 * Pool machine has no persistent backing store.
		 * Free all in-memory structures.
		 */
		m0_tl_for(poolmach_events, &state->pst_events_list, scan) {
			poolmach_events_tlink_del_fini(scan);
			m0_free(scan);
		} m0_tl_endfor;

		for (i = 0; i < state->pst_nr_devices; ++i) {
			cl = &state->pst_devices_array[i].pd_clink;
			if (cl->cl_chan == NULL)
				continue;
			obj = container_of(cl->cl_chan, struct m0_conf_obj,
					   co_ha_chan);
			M0_ASSERT(m0_conf_obj_invariant(obj));
			m0_pooldev_clink_del(cl);
			M0_SET0(cl);
			m0_confc_close(obj);
			pd = &state->pst_devices_array[i];
			if (pool_failed_devs_tlink_is_in(pd))
				pool_failed_devs_tlist_del(pd);
			pool_failed_devs_tlink_fini(pd);
		}
		m0_clink_cleanup(&state->pst_conf_exp);
		m0_clink_cleanup(&state->pst_conf_ready);
		m0_clink_fini(&state->pst_conf_exp);
		m0_clink_fini(&state->pst_conf_ready);
		m0_free(state->pst_spare_usage_array);
		m0_free(state->pst_devices_array);
		m0_free(state->pst_nodes_array);
		m0_free0(&pm->pm_state);
	}
	m0_rwlock_write_unlock(&pm->pm_lock);

	pm->pm_is_initialised = false;
	m0_rwlock_fini(&pm->pm_lock);
}

M0_INTERNAL int m0_poolmach_event_post(struct m0_poolmach *pm, uint64_t dev_id,
				       enum m0_poolmach_event_owner_type et,
				       enum m0_pool_nd_state state,
				       struct m0_be_tx *tx)
{
	struct m0_poolmach_event    pme;
	struct m0_pooldev          *dev_array;
	bool                        transit = false;
	int                         rc = 0;

	dev_array   = pm->pm_state->pst_devices_array;
	switch (state) {
	case M0_PNDS_SNS_REPAIRING:
		if (dev_array[dev_id].pd_state == M0_PNDS_FAILED)
			transit = true;
		break;
	case M0_PNDS_SNS_REPAIRED:
		if (dev_array[dev_id].pd_state ==
				M0_PNDS_SNS_REPAIRING)
			transit = true;
		break;
	case M0_PNDS_SNS_REBALANCING:
		if (dev_array[dev_id].pd_state ==
				M0_PNDS_SNS_REPAIRED)
			transit = true;
		break;
	case M0_PNDS_ONLINE:
		if (dev_array[dev_id].pd_state != M0_PNDS_SNS_REPAIRED)
			transit = true;
		break;
	default:
		M0_IMPOSSIBLE("Bad state");
		break;
	}
	if (transit) {
		M0_SET0(&pme);
		pme.pe_type  = et;
		pme.pe_index = dev_id;
		pme.pe_state = state;
		rc = m0_poolmach_state_transit(pm, &pme, tx);
	}
	return rc;
}

static bool disk_is_in(struct m0_tl *head, struct m0_pooldev *pd)
{
	return m0_tl_exists(pool_failed_devs, d, head,
			    m0_fid_eq(&d->pd_id, &pd->pd_id));
}

static int poolmach_equeue_add(struct m0_poolmach *pm,
			       const struct m0_poolmach_event *event)
{
	struct poolmach_equeue_link *new_link;
	struct m0_poolmach_state    *state;
	int                          rc;

	m0_rwlock_write_lock(&pm->pm_lock);
	M0_ALLOC_PTR(new_link);
	if (new_link == NULL) {
		rc = -ENOMEM;
	} else {
		new_link->pel_event = *event;
		state = pm->pm_state;
		poolmach_equeue_tlink_init_at_tail(new_link,
						   &state->pst_event_queue);
		rc = 0;
	}
	m0_rwlock_write_unlock(&pm->pm_lock);
	return M0_RC(rc);
}

M0_INTERNAL uint32_t m0_poolmach_equeue_length(struct m0_poolmach *pm)
{
	return m0_tlist_length(&poolmach_equeue_tl,
			       &pm->pm_state->pst_event_queue);
}

static void spare_usage_arr_update(struct m0_poolmach *pm,
				   const struct m0_poolmach_event *event)
{
	struct m0_poolmach_state   *state;
	struct m0_pool_spare_usage *spare_array;
	uint32_t                    i;

	state = pm->pm_state;
	spare_array = state->pst_spare_usage_array;
	/* alloc a sns repare spare slot */
	for (i = 0; i < state->pst_max_device_failures; ++i) {
		if (spare_array[i].psu_device_index ==
		    POOL_PM_SPARE_SLOT_UNUSED) {
			spare_array[i].psu_device_index = event->pe_index;
			spare_array[i].psu_device_state = event->pe_state;
			break;
		}
	}
	if (i == state->pst_max_device_failures &&
	    i > 0 /* i == 0 in case of mdpool */) {
		M0_LOG(M0_ERROR, FID_F": No free spare space slot is found,"
			" this pool version is in DUD state;"
			" event_index=%d event_state=%d",
			FID_P(&pm->pm_pver->pv_id),
		       event->pe_index, event->pe_state);
		/* TODO add ADDB error message here */
	}
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
	struct m0_pool                *pool;
	struct m0_pooldev             *pd;
	enum m0_pool_nd_state          old_state = M0_PNDS_FAILED;
	uint32_t                       i;
	int                            rc = 0;

	M0_ENTRY();

	M0_PRE(pm != NULL);
	M0_PRE(event != NULL);

	M0_SET0(&event_link);
	state = pm->pm_state;
	pool = pm->pm_pver->pv_pool;

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
		if (!state->pst_su_initialised) {
			/*
			 * Failure vector is yet to be fetched from HA.
			 * Add the event to pending queue.
			 */
			return M0_RC(poolmach_equeue_add(pm, event));
		}
		break;
	default:
		return M0_ERR(-EINVAL);
	}

	if (old_state == event->pe_state)
		return M0_RC(0);

	switch (old_state) {
	case M0_PNDS_UNKNOWN:
		/*
		 * First state transition could be to any of the
		 * available states.
		 */
		break;
	case M0_PNDS_ONLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_OFFLINE, M0_PNDS_FAILED)))
			return M0_ERR(-EINVAL);
		break;
	case M0_PNDS_OFFLINE:
		if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE,
					     M0_PNDS_FAILED)))
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
		if (!M0_IN(event->pe_state, (M0_PNDS_ONLINE,
					     M0_PNDS_FAILED)))
			return M0_ERR(-EINVAL);
		break;
	default:
		return M0_ERR(-EINVAL);
	}
	/* Step 1: lock the poolmach */
	m0_rwlock_write_lock(&pm->pm_lock);

	/* Step 2: Update the state according to event */
	event_link.pel_event = *event;
	if (event->pe_type == M0_POOL_NODE) {
		/**
		 * @todo If this is a new node join event, the index
		 * might larger than the current number. Then we need
		 * to create a new larger array to hold nodes info.
		 */
		state->pst_nodes_array[event->pe_index].pn_state =
			event->pe_state;
	} else if (event->pe_type == M0_POOL_DEVICE) {
		state->pst_devices_array[event->pe_index].pd_state =
			event->pe_state;
	}

	/* Step 3: Increase the version */
	++ state->pst_version.pvn_version[PVE_READ];
	++ state->pst_version.pvn_version[PVE_WRITE];

	/* Step 4: copy new version into event */
	event_link.pel_new_version = state->pst_version;

	/* Step 5: Alloc or free a spare slot if necessary.*/
	spare_array = state->pst_spare_usage_array;
	pd = &state->pst_devices_array[event->pe_index];
	switch (event->pe_state) {
	case M0_PNDS_ONLINE:
		/* clear spare slot usage if it is from rebalancing */
		for (i = 0; i < state->pst_max_device_failures; i++) {
			if (spare_array[i].psu_device_index ==
			    event->pe_index) {
				M0_ASSERT(M0_IN(spare_array[i].psu_device_state,
						(M0_PNDS_OFFLINE,
						 M0_PNDS_SNS_REBALANCING)));
				spare_array[i].psu_device_index =
					POOL_PM_SPARE_SLOT_UNUSED;
				break;
			}
		}
		if (old_state == M0_PNDS_OFFLINE) {
			M0_ASSERT(!pool_failed_devs_tlink_is_in(pd));
			M0_CNT_DEC(state->pst_nr_failures);
		}
		if (pool_failed_devs_tlink_is_in(pd)) {
			M0_CNT_DEC(state->pst_nr_failures);
			pool_failed_devs_tlist_del(pd);
		}
		break;
	case M0_PNDS_OFFLINE:
		M0_CNT_INC(state->pst_nr_failures);
		M0_ASSERT(!pool_failed_devs_tlink_is_in(pd));
		break;
	case M0_PNDS_FAILED:
		 /*
		  * Alloc a sns repair spare slot only once for
		  * M0_PNDS_ONLINE->M0_PNDS_FAILED or
		  * M0_PNDS_OFFLINE->M0_PNDS_FAILED transition.
		  * A device can also transit to M0_PNDS_FAILED state
		  * from M0_PNDS_SNS_REBALANCING state as well.
		  */
		if (M0_IN(old_state, (M0_PNDS_UNKNOWN, M0_PNDS_ONLINE,
				      M0_PNDS_OFFLINE)))
			spare_usage_arr_update(pm, event);
		if (old_state != M0_PNDS_OFFLINE)
			M0_CNT_INC(state->pst_nr_failures);
		if (!pool_failed_devs_tlink_is_in(pd) &&
		    !disk_is_in(&pool->po_failed_devices, pd))
			pool_failed_devs_tlist_add_tail(
				&pool->po_failed_devices, pd);
		break;
	case M0_PNDS_SNS_REPAIRING:
	case M0_PNDS_SNS_REPAIRED:
	case M0_PNDS_SNS_REBALANCING:
		/* change the repair spare slot usage */
		for (i = 0; i < state->pst_max_device_failures; i++) {
			if (spare_array[i].psu_device_index ==
			    event->pe_index) {
				spare_array[i].psu_device_state =
					event->pe_state;
				break;
			}
		}
		if (i == state->pst_max_device_failures &&
		    i > 0 /* i == 0 in case of mdpool */)
			M0_LOG(M0_ERROR, FID_F": This pool is in DUD state;"
			       " event_index=%d event_state=%d",
			       FID_P(&pm->pm_pver->pv_id),
			       event->pe_index, event->pe_state);
		/* must be found */
		if (!pool_failed_devs_tlink_is_in(pd) &&
		    !disk_is_in(&pool->po_failed_devices, pd)) {
			M0_CNT_INC(state->pst_nr_failures);
			pool_failed_devs_tlist_add_tail(
				&pool->po_failed_devices, pd);
		}
		break;
	default:
		/* Do nothing */
		;
	}

	if (pm->pm_be_seg != NULL) {
		/* This poolmach is on server. Update to persistent storage. */
		M0_ASSERT(tx != NULL);
		rc = m0_poolmach__store(pm, tx, &event_link);
	} else {
		struct m0_poolmach_event_link *new_link;
		M0_ALLOC_PTR(new_link);
		if (new_link == NULL) {
			rc = M0_ERR(-ENOMEM);
		} else {
			*new_link = event_link;
			poolmach_events_tlink_init_at_tail(new_link,
					&state->pst_events_list);
		}
	}
	pm->pm_pver->pv_is_dirty = state->pst_nr_failures > 0;
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
		m0_tl_for (poolmach_events, &state->pst_events_list, scan) {
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
		/*
		 * Allocate a copy of the event and event link,
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

M0_INTERNAL void m0_poolmach_state_last_cancel(struct m0_poolmach *pm)
{
	struct m0_poolmach_state      *state;
	struct m0_poolmach_event_link *link;

	M0_PRE(pm != NULL);

	state = pm->pm_state;

	m0_rwlock_write_lock(&pm->pm_lock);

	link = poolmach_events_tlist_tail(&state->pst_events_list);
	if (link != NULL) {
		poolmach_events_tlink_del_fini(link);
		m0_free(link);
	}

	m0_rwlock_write_unlock(&pm->pm_lock);
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
		return M0_ERR_INFO(-EINVAL, "device index:%d total devices:%d",
				device_index, pm->pm_state->pst_nr_devices);

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
		(pm->pm_state->pst_spare_usage_array[i].psu_device_index ==
				device_index));
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
				  M0_PNDS_SNS_REPAIRED,
				  M0_PNDS_SNS_REBALANCING)))
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

M0_INTERNAL int m0_poolmach_fid_to_idx(struct m0_poolmach *pm,
				       struct m0_fid *fid, uint32_t *idx)
{
	uint32_t i;

	M0_LOG(M0_DEBUG, "note:"FID_F, FID_P(fid));
	for (i = 0; i < pm->pm_state->pst_nr_devices; ++i) {
		if (m0_fid_eq(&pm->pm_state->pst_devices_array[i].pd_id,
			      fid)) {
			*idx = pm->pm_state->pst_devices_array[i].pd_index;
			break;
		}
	}
	return i == pm->pm_state->pst_nr_devices ? -ENOENT : 0;
}

M0_INTERNAL void m0_poolmach_failvec_apply(struct m0_poolmach *pm,
                                           const struct m0_ha_nvec *nvec)
{
        struct m0_poolmach_event  pme;
        struct m0_poolmach_state *state;
        struct m0_pooldev        *pd;
        struct m0_pool           *pool;
        uint32_t                  i;
        uint32_t                  pd_idx;
        int                       rc;

        M0_PRE(!pm->pm_state->pst_su_initialised);

        pm->pm_state->pst_su_initialised = true;
        state = pm->pm_state;
        pool = pm->pm_pver->pv_pool;
        for (i = 0; i < nvec->nv_nr; ++i) {
                rc = m0_poolmach_fid_to_idx(pm, &nvec->nv_note[i].no_id,
					    &pd_idx);
		if (rc == -ENOENT)
			continue;
		M0_ASSERT(rc == 0);
                pme.pe_type = M0_POOL_DEVICE;
                pme.pe_index = pd_idx;
                pme.pe_state = m0_ha2pm_state_map(nvec->nv_note[i].no_state);
                if (!M0_IN(pme.pe_state, (M0_PNDS_FAILED, M0_PNDS_OFFLINE))) {
			/* Update the spare-usage-array. */
                        m0_rwlock_write_lock(&pm->pm_lock);
                        spare_usage_arr_update(pm, &pme);
                        pd = &state->pst_devices_array[pme.pe_index];
                        if (!pool_failed_devs_tlink_is_in(pd) &&
                            !disk_is_in(&pool->po_failed_devices, pd)) {
				M0_CNT_INC(state->pst_nr_failures);
                                pool_failed_devs_tlist_add_tail(
						&pool->po_failed_devices, pd);
			}
                        m0_rwlock_write_unlock(&pm->pm_lock);
                }
                rc = m0_poolmach_state_transit(pm, &pme, NULL);
                /*
		 * Failvec is applied only once, during initialisation.
                 * This operation should succeed.
                 */
                M0_ASSERT(rc == 0);
        }
        m0_poolmach_event_queue_apply(pm);
}

static int poolmach_spare_inherit(struct m0_poolmach *pm, struct m0_pool *pool)
{
        struct m0_poolmach_event  pme;
        struct m0_poolmach_state *state;
        struct m0_pooldev        *pd;
        uint32_t                  i;
        uint32_t                  pd_idx = 0;
        int                       rc = 0;

	M0_ENTRY();

        M0_PRE(!pm->pm_state->pst_su_initialised);

        pm->pm_state->pst_su_initialised = true;
        state = pm->pm_state;

	if (pool_failed_devs_tlist_is_empty(&pool->po_failed_devices)) {
		m0_poolmach_event_queue_apply(pm);
		M0_LEAVE("no failed devices");
		return M0_RC(0);
	}
	M0_LOG(M0_DEBUG, "length :%d", (int) pool_failed_devs_tlist_length(
				&pool->po_failed_devices));
	m0_tl_for (pool_failed_devs, &pool->po_failed_devices, pd) {
		for (i = 0; i < pm->pm_state->pst_nr_devices; ++i) {
			if (m0_fid_eq(&pm->pm_state->pst_devices_array[i].pd_id,
				      &pd->pd_id)) {
				pd_idx = state->pst_devices_array[i].pd_index;
				M0_LOG(M0_DEBUG, "failed device fid index:%d"
						FID_F, pd_idx,
						FID_P(&pm->pm_pver->pv_id));
				break;
			}
		}
		if (i == pm->pm_state->pst_nr_devices) {
			M0_LOG(M0_DEBUG, "Failed device:"FID_F"is not part of"
					"pool version:"FID_F, FID_P(&pd->pd_id),
					FID_P(&pm->pm_pver->pv_id));
			continue;
		}
                pme.pe_type = M0_POOL_DEVICE;
                pme.pe_index = pd_idx;
                pme.pe_state = pd->pd_state;
                /*
		 * Update the spare-usage-array in case the device state
                 * is other than failed.
                 */
                if (pme.pe_state != M0_PNDS_FAILED) {
                        m0_rwlock_write_lock(&pm->pm_lock);
                        spare_usage_arr_update(pm, &pme);
                        pd = &state->pst_devices_array[pme.pe_index];
			M0_CNT_INC(state->pst_nr_failures);
                        m0_rwlock_write_unlock(&pm->pm_lock);
                }
                rc = m0_poolmach_state_transit(pm, &pme, NULL);
                if (rc != 0)
			break;
	} m0_tl_endfor;
        m0_poolmach_event_queue_apply(pm);

	return M0_RC(rc);
}

M0_INTERNAL int m0_poolmach_spare_build(struct m0_poolmach *mach,
					struct m0_pool *pool,
					enum m0_conf_pver_kind kind)
{
	int              rc = 0;
	struct m0_mutex  chan_lock;
	struct m0_chan   chan;
	struct m0_clink  clink;

	if (kind == M0_CONF_PVER_ACTUAL) {
		m0_mutex_init(&chan_lock);
		m0_chan_init(&chan, &chan_lock);
		m0_clink_init(&clink, NULL);
		m0_clink_add_lock(&chan, &clink);
		rc = m0_ha_failvec_fetch(&pool->po_id, mach, &chan);
		if (rc != 0)
			goto end;
		/* Waiting to receive a failure vector from HA. */
		m0_chan_wait(&clink);
	} else if (kind == M0_CONF_PVER_VIRTUAL) {
		rc = poolmach_spare_inherit(mach, pool);
		return M0_RC(rc);
	}
end:
	if (kind == M0_CONF_PVER_ACTUAL) {
		m0_clink_del_lock(&clink);
		m0_clink_fini(&clink);
		m0_chan_fini_lock(&chan);
		m0_mutex_fini(&chan_lock);
	}
	return M0_RC(rc);
}


M0_INTERNAL void m0_poolmach_event_queue_apply(struct m0_poolmach *pm)
{
	struct m0_tl                *head  = &pm->pm_state->pst_event_queue;
	struct m0_poolmach_event    *event;
	struct poolmach_equeue_link *scan;

	M0_PRE(pm->pm_state->pst_su_initialised);

	m0_tl_for (poolmach_equeue, head, scan) {
		event = &scan->pel_event;
		m0_poolmach_state_transit(pm, event, NULL);
		poolmach_equeue_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;
}

static int lno = 0;

/* Change this value to make it more verbose, e.g. to M0_ERROR */
#define POOL_TRACE_LEVEL M0_DEBUG

M0_INTERNAL void m0_poolmach_version_dump(struct m0_poolmach_versions *v)
{
	M0_LOG(POOL_TRACE_LEVEL, "%4d:readv=%llx writev=%llx", lno,
	       (unsigned long long)v->pvn_version[PVE_READ],
	       (unsigned long long)v->pvn_version[PVE_WRITE]);
	lno++;
}

M0_INTERNAL void m0_poolmach_event_dump(const struct m0_poolmach_event *e)
{
	M0_LOG(POOL_TRACE_LEVEL, "%4d:pe_type=%6s pe_index=%x, pe_state=%10d",
	       lno,
	       e->pe_type == M0_POOL_DEVICE ? "device":"node",
	       e->pe_index, e->pe_state);
	lno++;
}

M0_INTERNAL void m0_poolmach_event_list_dump(struct m0_poolmach *pm)
{
	struct m0_tl                  *head = &pm->pm_state->pst_events_list;
	struct m0_poolmach_event_link *scan;

	M0_LOG(POOL_TRACE_LEVEL, ">>>>>");
	m0_rwlock_read_lock(&pm->pm_lock);
	m0_tl_for(poolmach_events, head, scan) {
		m0_poolmach_event_dump(&scan->pel_event);
		m0_poolmach_version_dump(&scan->pel_new_version);
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&pm->pm_lock);
	M0_LOG(POOL_TRACE_LEVEL, "=====");
}

M0_INTERNAL void m0_poolmach_device_state_dump(struct m0_poolmach *pm)
{
	int i;
	M0_LOG(POOL_TRACE_LEVEL, ">>>>>");
	for (i = 0; i < pm->pm_state->pst_nr_devices; i++) {
		M0_LOG(POOL_TRACE_LEVEL, "%04d:device[%d]"FID_F"state: %d",
		       lno, i, FID_P(&pm->pm_state->pst_devices_array[i].pd_id),
		       pm->pm_state->pst_devices_array[i].pd_state);
		lno++;
	}
	M0_LOG(POOL_TRACE_LEVEL, "=====");
}

M0_INTERNAL uint64_t m0_poolmach_nr_dev_failures(struct m0_poolmach *pm)
{
	struct m0_pool_spare_usage *spare_array;

	spare_array = pm->pm_state->pst_spare_usage_array;
	return m0_count(i, pm->pm_state->pst_max_device_failures,
			spare_array[i].psu_device_index !=
			POOL_PM_SPARE_SLOT_UNUSED);
}

M0_INTERNAL void m0_poolmach_gob2cob(struct m0_poolmach *pm,
				     const struct m0_fid *gfid,
				     uint32_t idx,
				     struct m0_fid *cob_fid)
{
	struct m0_poolmach_state *pms;

	M0_PRE(pm != NULL);

	pms = pm->pm_state;
	m0_fid_convert_gob2cob(gfid, cob_fid,
			       pms->pst_devices_array[idx].pd_sdev_idx);

	M0_LOG(M0_DEBUG, "gob fid "FID_F" @%d = cob fid "FID_F, FID_P(gfid),
			idx, FID_P(cob_fid));
}

#undef POOL_TRACE_LEVEL
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

