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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_POOL
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/assert.h"
#include "lib/hash.h"      /* m0_hash */
#include "lib/string.h"    /* m0_streq */
#include "conf/confc.h"    /* m0_confc_from_obj */
#include "conf/schema.h"   /* M0_CST_IOS, M0_CST_MDS */
#include "conf/diter.h"    /* m0_conf_diter_next_sync */
#include "conf/obj_ops.h"  /* m0_conf_dirval */
#include "conf/helpers.h"  /* m0_obj_is_pver */
#include "conf/validation.h" /* m0_conf_ruleset */
#include "ioservice/io_device.h"  /* m0_ios_poolmach_get */
#include "reqh/reqh_service.h" /* m0_reqh_service_ctx */
#include "reqh/reqh.h"

#include "pool/pool.h"
#include "pool/pool_fops.h"

#include "fd/fd.h"             /* m0_fd_tile_build m0_fd_tree_build */

#ifndef __KERNEL__
#include "mero/setup.h"
#endif

#include "lib/finject.h" /* M0_FI_ENABLED */
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
   Pool Machine state is stored on persistent storage. When system initializes
   the first time, pool machine state is configured from confc & confd. When
   system restarts, ioservice loads pool machine state data from persistent
   storage.

   Every pool version has its own pool machine state stored on persistent
   storage.
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

   Every pool version has a pool machine. Pool machine update events are
   delivered to all ioservices. So all the pool machine there get state
   transitions according to these events. Finally the pool machine states on
   all ioservice nodes are persistent and identical.

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

enum {
	/**
	 * Status "Device not found in Pool machine"
	 */
	POOL_DEVICE_INDEX_INVALID = -1
};

/**
 * tlist descriptor for list of m0_reqh_service_ctx objects placed
 * in m0_pools_common::pc_svc_ctxs list using sc_link.
 */
M0_TL_DESCR_DEFINE(pools_common_svc_ctx, "Service contexts", M0_INTERNAL,
		   struct m0_reqh_service_ctx, sc_link, sc_magic,
		   M0_REQH_SVC_CTX_MAGIC, M0_POOL_SVC_CTX_HEAD_MAGIC);

M0_TL_DEFINE(pools_common_svc_ctx, M0_INTERNAL, struct m0_reqh_service_ctx);

/**
 * tlist descriptor for list of m0_pool objects placed
 * in m0t1fs_sb::csb_pools list using sc_link.
 */
M0_TL_DESCR_DEFINE(pools, "pools", M0_INTERNAL,
		   struct m0_pool, po_linkage, po_magic,
		   M0_POOL_MAGIC, M0_POOLS_HEAD_MAGIC);

M0_TL_DEFINE(pools, M0_INTERNAL, struct m0_pool);

M0_TL_DESCR_DEFINE(pool_version, "pool versions", M0_INTERNAL,
		   struct m0_pool_version, pv_linkage, pv_magic,
		   M0_POOL_VERSION_MAGIC, M0_POOL_VERSION_HEAD_MAGIC);

M0_TL_DEFINE(pool_version, M0_INTERNAL, struct m0_pool_version);

M0_TL_DESCR_DEFINE(pool_failed_devs, "pool failed devices", M0_INTERNAL,
		   struct m0_pooldev, pd_fail_linkage, pd_footer.ft_magic,
		   M0_POOL_DEV_MAGIC, M0_POOL_DEVICE_HEAD_MAGIC);
M0_TL_DEFINE(pool_failed_devs, M0_INTERNAL, struct m0_pooldev);

static const struct m0_bob_type pver_bob = {
        .bt_name         = "m0_pool_version",
        .bt_magix_offset = M0_MAGIX_OFFSET(struct m0_pool_version,
                                           pv_magic),
        .bt_magix        = M0_POOL_VERSION_MAGIC,
        .bt_check        = NULL
};
M0_BOB_DEFINE(static, &pver_bob, m0_pool_version);

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

M0_INTERNAL int m0_pool_init(struct m0_pool *pool, struct m0_fid *id)
{
	M0_ENTRY();

	pool->po_id = *id;
	pools_tlink_init(pool);
	pool_version_tlist_init(&pool->po_vers);
	pool_failed_devs_tlist_init(&pool->po_failed_devices);

	M0_LEAVE();
	return 0;
}

M0_INTERNAL void m0_pool_fini(struct m0_pool *pool)
{
	struct m0_pooldev *pd;

	m0_tl_teardown(pool_failed_devs, &pool->po_failed_devices, pd);
	pools_tlink_fini(pool);
	pool_version_tlist_fini(&pool->po_vers);
	pool_failed_devs_tlist_fini(&pool->po_failed_devices);
}

static bool pools_common_invariant(const struct m0_pools_common *pc)
{
	return _0C(pc != NULL) && _0C(pc->pc_confc != NULL);
}

static bool pool_version_invariant(const struct m0_pool_version *pv)
{
	return _0C(pv != NULL) && _0C(m0_pool_version_bob_check(pv)) &&
	       _0C(m0_fid_is_set(&pv->pv_id)) && _0C(pv->pv_pool != NULL);
}

static bool obj_is_service(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
}

static bool is_mds(const struct m0_conf_obj *obj)
{
	return obj_is_service(obj) &&
	       M0_CONF_CAST(obj, m0_conf_service)->cs_type == M0_CST_MDS;
}

static int __mds_map(struct m0_conf_controller *c, struct m0_pools_common *pc)
{
	struct m0_conf_diter        it;
	struct m0_reqh_service_ctx *ctx;
	struct m0_conf_service     *s;
	struct m0_conf_obj         *obj;
	uint64_t                    idx = 0;
	int                         rc;

	M0_ENTRY();
	rc = m0_conf_diter_init(&it, pc->pc_confc, &c->cc_node->cn_obj,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, is_mds)) == M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		s = M0_CONF_CAST(obj, m0_conf_service);
		ctx = m0_tl_find(pools_common_svc_ctx, ctx,
				 &pc->pc_svc_ctxs,
				 m0_fid_eq(&s->cs_obj.co_id,
					   &ctx->sc_fid));
		pc->pc_mds_map[idx++] = ctx;
		M0_LOG(M0_DEBUG, "mds index:%d, no. of mds:%d", (int)idx,
			(int)pc->pc_nr_svcs[M0_CST_MDS]);
		M0_ASSERT(idx <= pc->pc_nr_svcs[M0_CST_MDS]);
	}

	m0_conf_diter_fini(&it);

	return M0_RC(rc);
}

static bool obj_is_controllerv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE &&
	       m0_conf_obj_type(M0_CONF_CAST(obj, m0_conf_objv)->cv_real) ==
	       &M0_CONF_CONTROLLER_TYPE;
}

M0_INTERNAL int m0_pool_mds_map_init(struct m0_conf_filesystem *fs,
				     struct m0_pools_common *pc)
{
	struct m0_conf_obj        *mdpool;
	struct m0_conf_diter       it;
	struct m0_conf_objv       *ov;
	struct m0_conf_controller *c;
	struct m0_conf_obj        *obj;
	int                        rc;

	M0_ENTRY();
	M0_PRE(!pools_common_svc_ctx_tlist_is_empty(&pc->pc_svc_ctxs));
	M0_PRE(pc->pc_mds_map != NULL);

	rc = m0_confc_open_sync(&mdpool, &fs->cf_obj,
				M0_CONF_FILESYSTEM_POOLS_FID, fs->cf_mdpool);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf_diter_init(&it, pc->pc_confc, mdpool,
				M0_CONF_POOL_PVERS_FID, M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID);
	if (rc != 0)
		goto end;

	while ((rc = m0_conf_diter_next_sync(&it, obj_is_controllerv)) ==
		M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE);
		ov = M0_CONF_CAST(obj, m0_conf_objv);
		M0_ASSERT(m0_conf_obj_type(ov->cv_real) ==
						&M0_CONF_CONTROLLER_TYPE);
		c = M0_CONF_CAST(ov->cv_real, m0_conf_controller);
		rc = __mds_map(c, pc);
		if (rc != 0)
			break;
	}
	m0_conf_diter_fini(&it);
end:
	m0_confc_close(mdpool);
	return M0_RC(rc);
}

static bool obj_is_ios_diskv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE &&
	       m0_is_ios_disk(M0_CONF_CAST(obj, m0_conf_objv)->cv_real);
}

M0_INTERNAL int m0_pool_version_device_map_init(struct m0_pool_version *pv,
						struct m0_conf_pver *pver,
						struct m0_pools_common *pc)
{
	struct m0_conf_diter              it;
	struct m0_conf_disk              *disk;
	struct m0_conf_sdev              *sdev;
	struct m0_conf_service           *svc;
	struct m0_reqh_service_ctx       *ctx;
	struct m0_pool_device_to_service *dev_ios;
	uint32_t                          nr_sdevs = 0;
	int                               rc;

	M0_ENTRY();
	M0_PRE(pc != NULL && pc->pc_dev2ios != NULL);
	M0_PRE(!pools_common_svc_ctx_tlist_is_empty(&pc->pc_svc_ctxs));

	rc = m0_conf_diter_init(&it, pc->pc_confc, &pver->pv_obj,
				M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID,
				M0_CONF_CTRLV_DISKVS_FID);
	if (rc != 0)
		return M0_ERR(rc);
	/*
	 * XXX TODO: Replace m0_conf_diter_next_sync() with
	 * m0_conf_diter_next().
	 */
	while ((rc = m0_conf_diter_next_sync(&it, obj_is_ios_diskv)) ==
	       M0_CONF_DIRNEXT) {
		/*
		 * Assign helper pointers.
		 */
		disk = M0_CONF_CAST(M0_CONF_CAST(m0_conf_diter_result(&it),
						 m0_conf_objv)->cv_real,
				    m0_conf_disk);
		sdev = disk->ck_dev;
		svc = M0_CONF_CAST(m0_conf_obj_grandparent(&sdev->sd_obj),
				   m0_conf_service);
		/*
		 * Find a m0_reqh_service_ctx that corresponds to `svc'.
		 */
		ctx = m0_tl_find(pools_common_svc_ctx, ctx, &pc->pc_svc_ctxs,
				 m0_fid_eq(&ctx->sc_fid, &svc->cs_obj.co_id) &&
				 ctx->sc_type == svc->cs_type);
		M0_ASSERT(ctx != NULL);

		M0_LOG(M0_DEBUG, "dev_idx=%d service="FID_F" disk="FID_F
		       " sdev_fid="FID_F, sdev->sd_dev_idx,
		       FID_P(&svc->cs_obj.co_id), FID_P(&disk->ck_obj.co_id),
		       FID_P(&sdev->sd_obj.co_id));
		M0_ASSERT(sdev->sd_dev_idx < pc->pc_nr_devices);
		/*
		 * Set "(m0_reqh_service_ctx, sdev_fid)" tuple, associated
		 * with this dev_idx, or make sure it is set to correct
		 * values.
		 */
		dev_ios = &pc->pc_dev2ios[sdev->sd_dev_idx];
		if (dev_ios->pds_ctx == NULL) {
			dev_ios->pds_sdev_fid = sdev->sd_obj.co_id;
			dev_ios->pds_ctx = ctx;
		} else {
			M0_ASSERT(m0_fid_eq(&dev_ios->pds_sdev_fid,
					    &sdev->sd_obj.co_id));
			M0_ASSERT(dev_ios->pds_ctx == ctx);
		}
		M0_CNT_INC(nr_sdevs);
	}

	m0_conf_diter_fini(&it);
	M0_POST(nr_sdevs <= pc->pc_nr_devices && nr_sdevs == pv->pv_attr.pa_P);
	return M0_RC(rc);
}

M0_INTERNAL int m0_pool_version_init(struct m0_pool_version *pv,
				     const struct m0_fid *id,
				     struct m0_pool *pool,
				     uint32_t pool_width,
				     uint32_t nr_nodes,
				     uint32_t nr_data,
				     uint32_t nr_failures,
				     struct m0_be_seg *be_seg,
				     struct m0_sm_group  *sm_grp,
				     struct m0_dtm       *dtm)
{
	int rc;

	M0_ENTRY();

	pv->pv_id = *id;
	pv->pv_attr.pa_N = nr_data;
	pv->pv_attr.pa_K = nr_failures;
	pv->pv_attr.pa_P = pool_width;
	pv->pv_pool = pool;
	pv->pv_nr_nodes = nr_nodes;
	if (be_seg != NULL)
		rc = m0_poolmach_backed_init2(&pv->pv_mach, pv, be_seg, sm_grp,
					      pv->pv_nr_nodes, pv->pv_attr.pa_P,
					      pv->pv_nr_nodes, pv->pv_attr.pa_K);
	else
		rc = m0_poolmach_init(&pv->pv_mach, pv, pv->pv_nr_nodes,
				      pv->pv_attr.pa_P, pv->pv_nr_nodes,
				      pv->pv_attr.pa_K);
	m0_pool_version_bob_init(pv);
	pool_version_tlink_init(pv);

	M0_POST(pool_version_invariant(pv));
	M0_LEAVE();

	return M0_RC(rc);
}

M0_INTERNAL struct m0_pool_version *
m0__pool_version_find(struct m0_pool *pool, const struct m0_fid *id)
{
	return m0_tl_find(pool_version, pv, &pool->po_vers,
			  m0_fid_eq(&pv->pv_id, id));
}

M0_INTERNAL struct m0_pool_version *
m0_pool_version_find(const struct m0_pools_common *pc, const struct m0_fid *id)
{
	struct m0_pool         *p;
	struct m0_pool_version *pver;

	M0_ENTRY(FID_F, FID_P(id));
	M0_PRE(pc != NULL);
	m0_tl_for(pools, &pc->pc_pools, p) {
		pver = m0__pool_version_find(p, id);
		if (pver != NULL) {
			M0_LOG(M0_DEBUG, FID_F, FID_P(&pver->pv_id));
			return pver;
		}
	} m0_tl_endfor;

	return NULL;
}

static int _nodes_count(struct m0_conf_pver *pver, uint32_t *nodes)
{
	struct m0_conf_diter  it;
	struct m0_confc      *confc;
	uint32_t              nr_nodes = 0;
	int                   rc;

	confc = m0_confc_from_obj(&pver->pv_obj);
	rc = m0_conf_diter_init(&it, confc, &pver->pv_obj,
				M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID,
				M0_CONF_CTRLV_DISKVS_FID);
	if (rc != 0)
		return M0_ERR(rc);

	/*
	 * XXX TODO: Replace m0_conf_diter_next_sync() with
	 * m0_conf_diter_next().
	 */
	while ((rc = m0_conf_diter_next_sync(&it, obj_is_controllerv)) ==
		M0_CONF_DIRNEXT) {
		/* We filter only controllerv objects. */
		M0_CNT_INC(nr_nodes);
	}

	*nodes = nr_nodes;
	m0_conf_diter_fini(&it);

	return M0_RC(rc);
}

M0_INTERNAL int m0_pool_version_init_by_conf(struct m0_pool_version *pv,
					     struct m0_conf_pver *pver,
					     struct m0_pool *pool,
					     struct m0_pools_common *pc,
					     struct m0_be_seg *be_seg,
					     struct m0_sm_group *sm_grp,
					     struct m0_dtm *dtm)
{
	uint32_t nodes = 0;
	uint64_t failure_level;
	int      rc;

	M0_ENTRY();
	M0_PRE(pv != NULL && pver != NULL && pool != NULL && pc != NULL);

	rc = _nodes_count(pver, &nodes);
	if (rc != 0)
		return M0_ERR(rc);
	rc = m0_pool_version_init(pv, &pver->pv_obj.co_id, pool,
				  pver->pv_attr.pa_P, nodes, pver->pv_attr.pa_N,
				  pver->pv_attr.pa_K, be_seg, sm_grp, dtm) ?:
	     m0_pool_version_device_map_init(pv, pver, pc) ?:
	     m0_poolmach_init_by_conf(&pv->pv_mach, pver);
	if (rc == 0) {
		pv->pv_pc = pc;
		pv->pv_mach.pm_pver = pv;
		memcpy(pv->pv_fd_tol_vec, pver->pv_nr_failures,
		       M0_FTA_DEPTH_MAX * sizeof pver->pv_nr_failures[0]);
		rc = m0_fd_tile_build(pver, pv, &failure_level) ?:
			m0_fd_tree_build(pver, &pv->pv_fd_tree);
		if (rc == 0)
			pool_version_tlist_add_tail(&pool->po_vers, pv);
	}

	M0_POST(pool_version_invariant(pv));
	return M0_RC(rc);
}

M0_INTERNAL void m0_pool_version_fini(struct m0_pool_version *pv)
{
	M0_ENTRY();
	M0_PRE(pool_version_invariant(pv));

	pool_version_tlink_fini(pv);
	m0_pool_version_bob_fini(pv);
	m0_poolmach_fini(&pv->pv_mach);
	pv->pv_mach.pm_pver = NULL;
	m0_fd_tile_destroy(&pv->pv_fd_tile);
	m0_fd_tree_destroy(&pv->pv_fd_tree);

	M0_LEAVE();
}

M0_INTERNAL void m0_pool_versions_fini(struct m0_pool *pool)
{
	struct m0_pool_version *pv;

	m0_tl_teardown(pool_version, &pool->po_vers, pv) {
		m0_pool_version_fini(pv);
		m0_free(pv);
	}
}

static void service_ctxs_destroy(struct m0_pools_common *pc)
{
	struct m0_reqh_service_ctx *ctx;
	int                         rc;

	M0_ENTRY();

	/* Disconnect from all services asynchronously. */
	m0_tl_for(pools_common_svc_ctx, &pc->pc_svc_ctxs, ctx) {
		if (ctx->sc_is_connected) {
			m0_reqh_service_ctx_unsubscribe(ctx);
			m0_reqh_service_disconnect(ctx);
		}
	} m0_tl_endfor;

	m0_tl_teardown(pools_common_svc_ctx, &pc->pc_svc_ctxs, ctx) {
		if (ctx->sc_is_connected) {
			rc = m0_reqh_service_disconnect_wait(ctx);
			/* XXX Current function doesn't fail. */
			M0_ASSERT(rc == 0);
		}
		m0_reqh_service_ctx_destroy(ctx);
	}

	M0_POST(pools_common_svc_ctx_tlist_is_empty(&pc->pc_svc_ctxs));
	M0_LEAVE();
}

/**
 * Creates service contexts from given struct m0_conf_service.
 * Creates service context for each endpoint in m0_conf_service::cs_endpoints.
 */
static int __service_ctx_create(struct m0_pools_common *pc,
				struct m0_conf_service *cs,
				bool services_connect)
{
	struct m0_reqh_service_ctx  *ctx;
	const char                 **endpoint;
	int                          rc = 0;
	bool                         connect;

	M0_PRE(m0_conf_service_type_is_valid(cs->cs_type));
	M0_PRE((pc->pc_rmach != NULL) == services_connect);

	connect = cs->cs_obj.co_ha_state == M0_NC_ONLINE && services_connect;
	for (endpoint = cs->cs_endpoints; *endpoint != NULL; ++endpoint) {
		rc = m0_reqh_service_ctx_create(&cs->cs_obj, cs->cs_type, &ctx);
		if (rc == 0 && connect) {
			rc = m0_reqh_service_connect(ctx, pc->pc_rmach,
					*endpoint, POOL_MAX_RPC_NR_IN_FLIGHT);
			if (rc != 0)
				m0_reqh_service_ctx_destroy(ctx);
		}
		if (rc != 0)
			return M0_ERR(rc);
		ctx->sc_is_connected = connect;
		ctx->sc_pc = pc;
		pools_common_svc_ctx_tlink_init_at_tail(ctx, &pc->pc_svc_ctxs);
		/*
		 * TODO Subscription is done here to prevent the handlers to be
		 * called when ctx->sc_rlink isn't fully established. But in
		 * such way we loose notifications from HA before this point.
		 * Subscription should be done during `ctx' initialisation and
		 * handlers must not rely on that ctx->sc_rlink is established.
		 */
		if (ctx->sc_is_connected)
			m0_reqh_service_ctx_subscribe(ctx);
	}
	M0_CNT_INC(pc->pc_nr_svcs[cs->cs_type]);

	return M0_RC(rc);
}

static bool is_local_rms(const struct m0_conf_obj *obj)
{
	const char                   *lep;
	const struct m0_conf_service *svc;
	const struct m0_conf_process *p;
	struct m0_rpc_machine        *rm;
	struct m0_reqh               *reqh;

	if (m0_conf_obj_type(obj) != &M0_CONF_SERVICE_TYPE)
		return false;
	svc = M0_CONF_CAST(obj, m0_conf_service);
	if (svc->cs_type != M0_CST_RMS)
		return false;
	p = M0_CONF_CAST(m0_conf_obj_grandparent(&svc->cs_obj),
			 m0_conf_process);
	reqh = m0_conf_obj2reqh(obj);
	rm = m0_reqh_rpc_mach_tlist_head(&reqh->rh_rpc_machines);
	lep = m0_rpc_machine_ep(rm);
	M0_LOG(M0_DEBUG, "lep: %s svc ep: %s type:%d process:"FID_F"service:"
			 FID_F, lep,
			 p->pc_endpoint, svc->cs_type,
			 FID_P(&p->pc_obj.co_id),
			 FID_P(&svc->cs_obj.co_id));
	return m0_streq(lep, p->pc_endpoint);
}

static int active_rms_fid_copy(struct m0_pools_common *pc,
			       struct m0_fid          *active_rm)
{
	struct m0_rconfc *rconfc;
	struct m0_fop    *entry;
	int               rc = 0;

	rconfc = container_of(pc->pc_confc, struct m0_rconfc, rc_confc);
	/*
	 * Connect to RM service returned by HA on entrypoint.
	 * HA is responsible to keep active RM service and
	 * consistency of RMS states  across the entire cluster
	 * by supporting not more than one active RM at a time.
	 */
	if (!m0_rconfc_is_preloaded(rconfc)) {
		m0_rconfc_rm_fid(rconfc, active_rm);
	} else if (m0_ha_session_get() != NULL) {
		/*
		 * rconfc may be pre-loaded, but HA session still exist anyway,
		 * so real RMS fid remains discoverable via entrypoint request
		 */
		rc = m0_ha_entrypoint_get(&entry);
		if (rc == 0) {
			struct m0_rpc_item *rep = entry->f_item.ri_reply;
			struct m0_ha_entrypoint_rep *epr;

			epr = m0_fop_data(m0_rpc_item_to_fop(rep));
			*active_rm = epr->hbp_active_rm_fid;
			/* dismiss entry fop */
			m0_free(m0_fop_data(entry));
			entry->f_data.fd_data = NULL;
			m0_fop_put_lock(entry);
		}
	}
	return M0_RC(rc);
}

/**
 * @todo : This needs to be converted to process (m0d) context since
 *         it connects to specific process endpoint.
 */
static int service_ctxs_create(struct m0_pools_common *pc,
			       struct m0_conf_filesystem *fs,
			       bool service_connect)
{
	struct m0_conf_diter    it;
	struct m0_conf_service *s;
	struct m0_conf_obj     *obj;
	struct m0_fid           active_rm = M0_FID0;
	int                     rc;

	M0_ENTRY();

	rc = active_rms_fid_copy(pc, &active_rm);
	if (rc != 0)
		return M0_ERR(rc);
	/*
	 * Please note, zero may be returned here in rc even with active rm not
	 * found. Non-zero code is to indicate an error occurred in the course
	 * of calling m0_ha_entrypoint_get(), but not a failure in finding the
	 * fid itself. In this situation a fallback is taking local rms for the
	 * purpose of context creation (see conf iteration below).
	 */
	if (m0_fid_is_set(&active_rm)) {
		rc = m0_conf_service_get(pc->pc_confc, &active_rm, &s) ? :
			__service_ctx_create(pc, s, service_connect);
		if (rc != 0)
			return M0_ERR(rc);
	}

	rc = m0_conf_diter_init(&it, pc->pc_confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0)
		return M0_ERR(rc);

	/*
	 * XXX TODO: Replace m0_conf_diter_next_sync() with
	 * m0_conf_diter_next().
	 */
	while ((rc = m0_conf_diter_next_sync(&it, obj_is_service)) ==
		M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE);
		s = M0_CONF_CAST(obj, m0_conf_service);
		/*
		 * Connection to confd is managed by configuration client.
		 * confd is already connected in m0_confc_init() to the
		 * endpoint provided by the command line argument -C.
		 *
		 * Connection to RMS is skipped here except local RMS
		 * if local configuration preloaded. Nodes will only
		 * use RM services returned by HA entrypoint.
		 */
		if ((!m0_fid_is_set(&active_rm) && is_local_rms(obj)) ||
		    (s->cs_type != M0_CST_MGS && s->cs_type != M0_CST_RMS)) {
			rc = __service_ctx_create(pc, s, service_connect);
				if (rc != 0)
					break;
		}
	}

	m0_conf_diter_fini(&it);

	if (rc != 0)
		service_ctxs_destroy(pc);

	return M0_RC(rc);
}

static struct m0_reqh_service_ctx *
service_ctx_find_by_type(const struct m0_pools_common *pc,
			 enum m0_conf_service_type     type)
{
	return m0_tl_find(pools_common_svc_ctx, ctx, &pc->pc_svc_ctxs,
			  ctx->sc_type == type);
}

M0_INTERNAL struct m0_reqh_service_ctx *
m0_pools_common_service_ctx_find(const struct m0_pools_common *pc,
				 const struct m0_fid *id,
				 enum m0_conf_service_type type)
{
	return m0_tl_find(pools_common_svc_ctx, ctx, &pc->pc_svc_ctxs,
			  m0_fid_eq(id, &ctx->sc_fid) && ctx->sc_type == type);
}

M0_INTERNAL int m0_pools_common_init(struct m0_pools_common *pc,
				     struct m0_rpc_machine *rmach,
				     struct m0_conf_filesystem *fs)
{
	int      rc;
	uint32_t nr_devices = 0;

	M0_ENTRY();
	M0_PRE(pc != NULL && fs != NULL);

	*pc = (struct m0_pools_common){
				.pc_rmach = rmach,
				.pc_md_redundancy = fs->cf_redundancy,
				.pc_confc = m0_confc_from_obj(&fs->cf_obj)};

	rc = m0_conf_ios_devices_count(&fs->cf_obj.co_parent->co_id,
				       pc->pc_confc, &nr_devices);
	if (rc != 0)
		return M0_ERR(rc);
	M0_LOG(M0_DEBUG, "ioservices device count:%d", (int)nr_devices);
	pc->pc_nr_devices = nr_devices;
	M0_ALLOC_ARR(pc->pc_dev2ios, pc->pc_nr_devices);
	if (pc->pc_dev2ios == NULL)
		return M0_ERR(-ENOMEM);
	pools_common_svc_ctx_tlist_init(&pc->pc_svc_ctxs);
	pools_tlist_init(&pc->pc_pools);

	return M0_RC(rc);
}

M0_INTERNAL int m0_pools_service_ctx_create(struct m0_pools_common *pc,
					    struct m0_conf_filesystem *fs)
{
	int               rc;
	struct m0_rconfc *rconfc;

	M0_ENTRY();
	M0_PRE(pc != NULL && fs != NULL);

	rc = service_ctxs_create(pc, fs, pc->pc_rmach != NULL);
	if (rc != 0)
		return M0_ERR(rc);

	M0_ALLOC_ARR(pc->pc_mds_map,
		     pools_common_svc_ctx_tlist_length(&pc->pc_svc_ctxs));
	rc = pc->pc_mds_map == NULL ? -ENOMEM : m0_pool_mds_map_init(fs, pc);
	if (rc != 0)
		goto err;

        rconfc = container_of(pc->pc_confc, struct m0_rconfc, rc_confc);
	pc->pc_rm_ctx = service_ctx_find_by_type(pc, M0_CST_RMS);
	pc->pc_ha_ctx = service_ctx_find_by_type(pc, M0_CST_HA);
	if (pc->pc_ha_ctx == NULL ||
	    (!m0_rconfc_is_preloaded(rconfc) && pc->pc_rm_ctx == NULL)) {
		rc = M0_ERR_INFO(-ENOENT, "The mandatory %s service is missing."
				 "Make sure this is specified in the conf db.",
				 pc->pc_rm_ctx == NULL ? "RM" : "HA");
		goto err;
	}

	M0_POST(pools_common_invariant(pc));
	return M0_RC(rc);
err:
	m0_free(pc->pc_mds_map);
	service_ctxs_destroy(pc);
	return M0_ERR(rc);
}

M0_INTERNAL void m0_pools_common_fini(struct m0_pools_common *pc)
{
	M0_ENTRY();
	M0_PRE(pools_common_invariant(pc));

	pools_common_svc_ctx_tlist_fini(&pc->pc_svc_ctxs);
	pools_tlist_fini(&pc->pc_pools);
	m0_free(pc->pc_dev2ios);

	M0_LEAVE();
}

M0_INTERNAL void m0_pools_service_ctx_destroy(struct m0_pools_common *pc)
{
	M0_ENTRY();
	M0_PRE(pools_common_invariant(pc));

	service_ctxs_destroy(pc);
	m0_free(pc->pc_mds_map);
	M0_LEAVE();
}

static bool obj_is_pool(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_POOL_TYPE;
}

M0_INTERNAL int m0_pool_versions_setup(struct m0_pools_common    *pc,
				       struct m0_conf_filesystem *fs,
				       struct m0_be_seg          *be_seg,
				       struct m0_sm_group        *sm_grp,
				       struct m0_dtm             *dtm)
{
	struct m0_confc        *confc;
	struct m0_conf_diter    it;
	struct m0_pool_version *pver;
	struct m0_conf_pver    *pver_obj;
	struct m0_fid          *pool_id;
	struct m0_pool         *pool;
	int                     rc;

	M0_ENTRY();

	confc = m0_confc_from_obj(&fs->cf_obj);
	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_POOLS_FID,
				M0_CONF_POOL_PVERS_FID);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, m0_obj_is_pver)) ==
		M0_CONF_DIRNEXT) {
		M0_ALLOC_PTR(pver);
		if (pver == NULL) {
			rc = -ENOMEM;
			break;
		}
		pver_obj = M0_CONF_CAST(m0_conf_diter_result(&it),
					m0_conf_pver);
		pool_id = &m0_conf_obj_grandparent(&pver_obj->pv_obj)->co_id;
		pool = m0_tl_find(pools, pool, &pc->pc_pools,
			  m0_fid_eq(&pool->po_id, pool_id));

		M0_ASSERT(m0_fid_eq(&pool->po_id, pool_id));
		rc = m0_pool_version_init_by_conf(pver, pver_obj, pool, pc,
						  be_seg, sm_grp, dtm);
		if (rc != 0)
			break;
	}

	m0_conf_diter_fini(&it);
	if (rc != 0)
		m0_pools_destroy(pc);

	return M0_RC(rc);
}

M0_INTERNAL int m0_pools_setup(struct m0_pools_common    *pc,
			       struct m0_conf_filesystem *fs,
			       struct m0_be_seg          *be_seg,
			       struct m0_sm_group        *sm_grp,
			       struct m0_dtm             *dtm)
{
	struct m0_confc      *confc;
	struct m0_conf_diter  it;
	struct m0_pool       *pool;
	int                   rc;

	M0_ENTRY();

	confc = m0_confc_from_obj(&fs->cf_obj);
	M0_LOG(M0_DEBUG, "file system:"FID_F"profile"FID_F,
			FID_P(&fs->cf_obj.co_id),
			FID_P(&fs->cf_obj.co_parent->co_id));
	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_POOLS_FID);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, obj_is_pool)) ==
		M0_CONF_DIRNEXT) {
		M0_ALLOC_PTR(pool);
		if (pool == NULL) {
			rc = -ENOMEM;
			break;
		}
		rc = m0_pool_init(pool, &m0_conf_diter_result(&it)->co_id);
		if (rc != 0)
			break;
		pools_tlink_init_at_tail(pool, &pc->pc_pools);
	}

	m0_conf_diter_fini(&it);
	if (rc != 0)
		m0_pools_destroy(pc);
	pc->pc_md_pool = m0_pool_find(pc, &fs->cf_mdpool);
	M0_ASSERT(pc->pc_md_pool != NULL);

	return M0_RC(rc);
}

M0_INTERNAL void m0_pool_versions_destroy(struct m0_pools_common *pc)
{
        struct m0_pool *p;

	M0_ENTRY();
        m0_tl_teardown(pools, &pc->pc_pools, p) {
                m0_pool_versions_fini(p);
        }
	M0_LEAVE();
}

M0_INTERNAL void m0_pools_destroy(struct m0_pools_common *pc)
{
        struct m0_pool *p;

	M0_ENTRY();
        m0_tl_teardown(pools, &pc->pc_pools, p) {
                m0_pool_fini(p);
                m0_free(p);
        }
	M0_LEAVE();
}

M0_INTERNAL struct m0_pool *m0_pool_find(struct m0_pools_common *pc,
					 const struct m0_fid *id)
{
	return m0_tl_find(pools, pool, &pc->pc_pools,
			  m0_fid_eq(&pool->po_id, id));
}

M0_INTERNAL uint64_t
m0_pool_version2layout_id(const struct m0_fid *pv_fid, uint64_t lid)
{
	return m0_hash(m0_fid_hash(pv_fid) + lid);
}

M0_INTERNAL uint32_t m0_ha2pm_state_map(enum m0_ha_obj_state hastate)
{
	uint32_t ha2pm_statemap [] = {
		[M0_NC_ONLINE]         = M0_PNDS_ONLINE,
		[M0_NC_FAILED]         = M0_PNDS_FAILED,
		[M0_NC_TRANSIENT]      = M0_PNDS_OFFLINE,
		[M0_NC_REPAIR]         = M0_PNDS_SNS_REPAIRING,
		[M0_NC_REPAIRED]       = M0_PNDS_SNS_REPAIRED,
		[M0_NC_REBALANCE]      = M0_PNDS_SNS_REBALANCING,
	};
	M0_ASSERT (hastate < M0_NC_NR);
	return ha2pm_statemap[hastate];
}

static bool disks_poolmach_state_update_cb(struct m0_clink *cl)
{
	struct m0_poolmach_event  pme;
	struct m0_conf_obj       *obj;
	struct m0_pooldev        *pdev;

	M0_ENTRY();
	obj = container_of(cl->cl_chan, struct m0_conf_obj, co_ha_chan);
	M0_PRE(obj != NULL &&
	       m0_conf_fid_type(&obj->co_id) == &M0_CONF_DISK_TYPE);
	M0_ASSERT(obj != NULL);
	pdev = container_of(cl, struct m0_pooldev, pd_clink);
	M0_ASSERT(pdev != NULL);
	pme.pe_type = M0_POOL_DEVICE;
	pme.pe_index = pdev->pd_index;
	pme.pe_state = m0_ha2pm_state_map(obj->co_ha_state);
	M0_LOG(M0_DEBUG, "pe_type=%6s pe_index=%x, pe_state=%10d",
			 pme.pe_type == M0_POOL_DEVICE ? "device":"node",
			 pme.pe_index, pme.pe_state);
	return M0_RC(m0_poolmach_state_transit(pdev->pd_pm, &pme, NULL));
}

M0_INTERNAL void m0_pooldev_clink_del(struct m0_clink *cl)
{
	if (M0_FI_ENABLED("do_nothing_for_poolmach-ut")) {
		/*
		 * The poolmach-ut does not add/register clink in pooldev.
		 * So need to skip deleting the links if this is called
		 * during poolmach-ut.
		 * TODO: This is workaround & can be addressed differently.
		 */
		return;
	}
	m0_clink_del_lock(cl);
	m0_clink_fini(cl);
}

M0_INTERNAL void m0_pooldev_clink_add(struct m0_clink *link,
				      struct m0_chan  *chan)
{
	m0_clink_init(link, disks_poolmach_state_update_cb);
	m0_clink_add_lock(chan, link);
}

#ifndef __KERNEL__
/**
 * Find out device ids of the REPAIRED devices in the given pool machine
 * and call m0_mero_stob_reopen() on each of them.
 */
M0_INTERNAL int m0_pool_device_reopen(struct m0_poolmach *pm,
				      struct m0_reqh *reqh)
{
	struct m0_pool_spare_usage *spare_array;
	struct m0_pooldev          *dev_array;
	uint32_t                    dev_id;
	int                         i;
	int                         rc = 0;

	dev_array = pm->pm_state->pst_devices_array;
	spare_array = pm->pm_state->pst_spare_usage_array;
	for (i = 0; spare_array[i].psu_device_index !=
	     POOL_PM_SPARE_SLOT_UNUSED; ++i) {
		dev_id = spare_array[i].psu_device_index;
		if (dev_array[dev_id].pd_state == M0_PNDS_SNS_REPAIRED) {
			rc = m0_mero_stob_reopen(reqh, pm, dev_id);
			if (rc != 0)
				return M0_ERR(rc);
		}
	}
	return M0_RC(rc);
}

static int pool_device_index(struct m0_poolmach *pm, struct m0_fid *fid)
{
	int dev;

	for (dev = 0; dev < pm->pm_state->pst_nr_devices; ++dev)
		if (m0_fid_eq(&pm->pm_state->pst_devices_array[dev].pd_id, fid))
			return dev;
	return POOL_DEVICE_INDEX_INVALID;
}


static void pool_device_state_last_revert(struct m0_pools_common *pc,
					  struct m0_fid          *dev_fid,
					  struct m0_poolmach     *pm_stop)
{
	struct m0_pool         *pool;
	struct m0_pool_version *pver;
	struct m0_poolmach     *pm;
	int                     dev_idx;

	m0_tl_for(pools, &pc->pc_pools, pool) {
		m0_tl_for(pool_version, &pool->po_vers, pver) {
			pm = &pver->pv_mach;
			if (pm == pm_stop)
				return;
			dev_idx = pool_device_index(pm, dev_fid);
			if (dev_idx != POOL_DEVICE_INDEX_INVALID)
				m0_poolmach_state_last_cancel(pm);
		} m0_tl_endfor;
	} m0_tl_endfor;
}

/**
 * Iterate over all pool versions and update corresponding poolmachines
 * containing provided disk.
 */
M0_INTERNAL int m0_pool_device_state_update(struct m0_reqh        *reqh,
					    struct m0_be_tx       *tx,
					    struct m0_fid         *dev_fid,
					    enum m0_pool_nd_state  new_state)
{
	struct m0_pools_common   *pc = reqh->rh_pools;
	struct m0_pool           *pool;
	struct m0_pool_version   *pver;
	struct m0_poolmach       *pm;
	int                       rc;
	int                       dev_idx;
	struct m0_poolmach_event  pme;

	m0_tl_for(pools, &pc->pc_pools, pool) {
		m0_tl_for(pool_version, &pool->po_vers, pver) {
			pm = &pver->pv_mach;
			dev_idx = pool_device_index(pm, dev_fid);
			if (dev_idx != POOL_DEVICE_INDEX_INVALID) {
				pme.pe_type  = M0_POOL_DEVICE;
				pme.pe_index = dev_idx;
				pme.pe_state = new_state;
				rc = m0_poolmach_state_transit(pm, &pme, NULL);
				if (rc != 0) {
					pool_device_state_last_revert(pc,
								      dev_fid,
								      pm);
					return M0_ERR(rc);
				}
			}
		} m0_tl_endfor;
	} m0_tl_endfor;

	/* update ios poolmachine */
	if (rc == 0) {
		pm = m0_ios_poolmach_get(reqh);
		dev_idx = pool_device_index(pm, dev_fid);
		if (dev_idx != POOL_DEVICE_INDEX_INVALID) {
			pme.pe_type  = M0_POOL_DEVICE;
			pme.pe_index = dev_idx;
			pme.pe_state = new_state;
			rc = m0_poolmach_state_transit(pm, &pme, tx);
			if (rc != 0) {
				/* Revert all changes in pc */
				pool_device_state_last_revert(pc,
							      dev_fid,
							      NULL);
				return M0_ERR(rc);
			}
		}
	}
	return M0_RC(0);
}

#endif /* !__KERNEL__ */

/* ------------------------------------------------------------------
 * conf validation
 * ------------------------------------------------------------------ */

static char *pool_paths_conf_error(const struct m0_conf_cache *cache,
				   char *buf, size_t buflen)
{
	/*
	 * - "filesystem/pools/pvers/rackvs/enclvs/ctrlvs/diskvs" path
	 *   is available;
	 *
	 * - "filesystem/nodes/processes/services" path is available.
	 */
	return NULL;
}

/** Ensures that the conf DAG can be used by m0_pool_mds_map_init(). */
static char *pool_mds_map_init_conf_error(const struct m0_conf_cache *cache,
					  char *buf, size_t buflen)
{
	/*
	 * - for ctrlv in controllervs:
	 *       assert ctrlv.cv_real != NULL
	 *       assert type(ctrlv.cv_real) is CONTROLLER_TYPE
	 */
	return NULL;
}

/** Ensures that the conf DAG can be used by __mds_map(). */
static char *pool_mds_map_conf_error(const struct m0_conf_cache *cache,
				     char *buf, size_t buflen)
{
	/*
	 * - assert len(services) <= pc->pc_nr_svcs[M0_CST_MDS].
	 */
	return NULL;
}

/**
 * Ensures that the conf DAG can be used by m0_pool_version_device_map_init().
 */
static char *
pool_version_device_map_conf_error(const struct m0_conf_cache *cache,
				   char *buf, size_t buflen)
{
	/*
	 * - nr_devices = 0
	 *   for diskv in diskvs:
	 *       assert m0_conf_obj_type(diskv.cv_real) == &M0_CONF_DISK_TYPE
	 *       disk = diskv.cv_real
	 *       assert type(disk.ck_dev) is m0_conf_sdev
	 *       assert m0_conf_obj_type(m0_conf_obj_grandparent(disk.ck_dev))
	 *           == &M0_CONF_SERVICE_TYPE
	 *       ++nr_devices
	 *   assert nr_devices == pv->pv_attr.pa_P
	 */
	return NULL;
}

/** Ensures that the conf DAG can be used by m0_pools_setup(). */
static char *pool_setup_conf_error(const struct m0_conf_cache *cache,
				   char *buf, size_t buflen)
{
	/*
	 * - assert filesystem.cf_mdpool refers to one of the pools in
	 *          filesystem.cf_pools.
	 */
	return NULL;
}

const struct m0_conf_ruleset m0_pool_rules = {
	.cv_name  = "m0_pool_rules",
	.cv_rules = {
#define _ENTRY(name) { #name, name }
		_ENTRY(pool_paths_conf_error),
		_ENTRY(pool_mds_map_init_conf_error),
		_ENTRY(pool_mds_map_conf_error),
		_ENTRY(pool_version_device_map_conf_error),
		_ENTRY(pool_setup_conf_error),
#undef _ENTRY
		{ NULL, NULL }
	}
};

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
