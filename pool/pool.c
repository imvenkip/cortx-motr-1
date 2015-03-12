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

#include "conf/confc.h"    /* m0_confc_from_obj */
#include "conf/schema.h"   /* M0_CST_IOS, M0_CST_MDS */
#include "conf/dir_iter.h" /* m0_conf_diter_init, m0_conf_diter_next_sync */
#include "conf/obj_ops.h"  /* m0_conf_dirval */
#include "conf/helpers.h"  /* m0_obj_is_pver */

#include "reqh/reqh_service.h" /* m0_reqh_service_ctx */
#include "reqh/reqh.h"

#include "pool/pool.h"
#include "pool/pool_fops.h"

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

	M0_LEAVE();
	return 0;
}

M0_INTERNAL void m0_pool_fini(struct m0_pool *pool)
{
	pools_tlink_fini(pool);
	pool_version_tlist_fini(&pool->po_vers);
}

static bool pools_common_invariant(const struct m0_pools_common *pc)
{
	return _0C(pc != NULL) && _0C(pc->pc_confc != NULL);
}

static bool pool_version_invariant(const struct m0_pool_version *pv)
{
	return _0C(pv != NULL) && _0C(m0_pool_version_bob_check(pv)) &&
	       _0C(m0_fid_is_set(&pv->pv_id)) && _0C(pv->pv_pool != NULL) &&
	       _0C(ergo(pv->pv_dev_to_ios_map != NULL && pv->pv_pc != NULL,
		   m0_forall(i, pv->pv_pc->pc_nr_svcs[M0_CST_IOS],
		pools_common_svc_ctx_tlist_contains(&pv->pv_pc->pc_svc_ctxs,
						pv->pv_dev_to_ios_map[i]))));
}

static bool obj_is_service(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
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
		return M0_RC(rc);

	while ((rc = m0_conf_diter_next_sync(&it, obj_is_service)) ==
							M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE);
		s = M0_CONF_CAST(obj, m0_conf_service);
		ctx = m0_tl_find(pools_common_svc_ctx, ctx,
				 &pc->pc_svc_ctxs,
				 m0_fid_eq(&s->cs_obj.co_id,
					   &ctx->sc_fid) &&
				 ctx->sc_type == s->cs_type);
		pc->pc_mds_map[idx++] = ctx;
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
	struct m0_conf_diter       it;
	struct m0_conf_objv       *ov;
	struct m0_conf_controller *c;
	struct m0_conf_obj        *obj;
	int                        rc;

	M0_ENTRY();
	M0_PRE(!pools_common_svc_ctx_tlist_is_empty(&pc->pc_svc_ctxs));
	M0_PRE(pc->pc_mds_map != NULL);

	rc = m0_conf_diter_init(&it, pc->pc_confc, &fs->cf_md_pool->pl_obj,
				M0_CONF_POOL_PVERS_FID, M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID);
	if (rc != 0)
		return M0_RC(rc);

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

	return M0_RC(rc);
}

static bool obj_is_diskv(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE &&
	       m0_conf_obj_type(M0_CONF_CAST(obj, m0_conf_objv)->cv_real) ==
	       &M0_CONF_DISK_TYPE;
}

M0_INTERNAL int m0_pool_version_device_map_init(struct m0_pool_version *pv,
						struct m0_conf_pver *pver,
						struct m0_pools_common *pc)
{
	struct m0_conf_diter        it;
	struct m0_conf_objv        *ov;
	struct m0_conf_disk        *d;
	struct m0_conf_service     *s;
	struct m0_reqh_service_ctx *ctx;
	struct m0_conf_obj         *obj;
	uint64_t                    dev_idx = 0;
	int                         rc;

	M0_ENTRY();
	M0_PRE(!pools_common_svc_ctx_tlist_is_empty(&pc->pc_svc_ctxs));
	M0_PRE(pv->pv_dev_to_ios_map != NULL);

	rc = m0_conf_diter_init(&it, pc->pc_confc, &pver->pv_obj,
				M0_CONF_PVER_RACKVS_FID,
				M0_CONF_RACKV_ENCLVS_FID,
				M0_CONF_ENCLV_CTRLVS_FID,
				M0_CONF_CTRLV_DISKVS_FID);
	if (rc != 0)
		return M0_RC(rc);

	/*
	 * XXX TODO: Replace m0_conf_diter_next_sync() with
	 * m0_conf_diter_next().
	 */
	while ((rc = m0_conf_diter_next_sync(&it, obj_is_diskv)) ==
							M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_OBJV_TYPE);
		ov = M0_CONF_CAST(obj, m0_conf_objv);
		M0_ASSERT(m0_conf_obj_type(ov->cv_real) == &M0_CONF_DISK_TYPE);
		d = M0_CONF_CAST(ov->cv_real, m0_conf_disk);
		s = M0_CONF_CAST(d->ck_dev->sd_obj.co_parent->co_parent,
				 m0_conf_service);
		ctx = m0_tl_find(pools_common_svc_ctx, ctx,
				 &pc->pc_svc_ctxs,
				 m0_fid_eq(&s->cs_obj.co_id,
					   &ctx->sc_fid) &&
				 ctx->sc_type == s->cs_type);
		M0_ASSERT(ctx != NULL);
		pv->pv_dev_to_ios_map[dev_idx] = ctx;
		M0_CNT_INC(dev_idx);
	}

	m0_conf_diter_fini(&it);

	M0_POST(dev_idx <= pv->pv_attr.pa_P);
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
	M0_ENTRY();

	pv->pv_id = *id;
	pv->pv_attr.pa_N = nr_data;
	pv->pv_attr.pa_K = nr_failures;
	pv->pv_attr.pa_P = pool_width;
	pv->pv_pool = pool;
	pv->pv_nr_nodes = nr_nodes;
	M0_ALLOC_ARR(pv->pv_dev_to_ios_map, pool_width);
	if (pv->pv_dev_to_ios_map == NULL)
		return M0_RC(-ENOMEM);
	m0_poolmach_init(&pv->pv_mach, be_seg, sm_grp, dtm,
			 pv->pv_nr_nodes, pv->pv_attr.pa_P, pv->pv_nr_nodes,
			 pv->pv_attr.pa_K);
	m0_pool_version_bob_init(pv);
	pool_version_tlink_init(pv);

	M0_POST(pool_version_invariant(pv));
	M0_LEAVE();

	return 0;
}

M0_INTERNAL struct m0_pool_version *
m0__pool_version_find(struct m0_pool *pool, const struct m0_fid *id)
{
	return m0_tl_find(pool_version, pv, &pool->po_vers,
			  m0_fid_eq(&pv->pv_id, id));
}

M0_INTERNAL struct m0_pool_version *
m0_pool_version_find(struct m0_pools_common *pc, const struct m0_fid *id)
{
	struct m0_pool         *p;
	struct m0_pool_version *pver;

	M0_ENTRY();
	M0_PRE(pc != NULL);

	m0_tl_for(pools, &pc->pc_pools, p) {
		pver = m0__pool_version_find(p, id);
		if (pver != NULL)
			return pver;
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
		return M0_RC(rc);

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
	int      rc;

	M0_ENTRY();
	M0_PRE(pv != NULL && pver != NULL && pool != NULL && pc != NULL);

	/*
	 * XXX TODO: Remove this once pool width is encoded to
	 * struct m0_conf_pver.
	 */
	rc = _nodes_count(pver, &nodes);
	if (rc != 0)
		return M0_RC(rc);
	rc = m0_pool_version_init(pv, &pver->pv_obj.co_id, pool,
				  pver->pv_attr.pa_P, nodes, pver->pv_attr.pa_N,
				  pver->pv_attr.pa_K, be_seg, sm_grp, dtm) ?:
	     m0_pool_version_device_map_init(pv, pver, pc) ?:
	     m0_poolmach_init_by_conf(&pv->pv_mach, pver);
	if (rc == 0) {
		pool_version_tlist_add_tail(&pool->po_vers, pv);
		pv->pv_pc = pc;
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
	m0_free(pv->pv_dev_to_ios_map);
	m0_poolmach_fini(&pv->pv_mach);

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

	M0_ENTRY();

	m0_tl_teardown(pools_common_svc_ctx, &pc->pc_svc_ctxs, ctx) {
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

	M0_PRE(M0_CONF_SVC_TYPE_IS_VALID(cs->cs_type));
	M0_PRE((pc->pc_rmach != NULL) == services_connect);

	for (endpoint = cs->cs_endpoints; *endpoint != NULL; ++endpoint) {
		rc = m0_reqh_service_ctx_create(&cs->cs_obj.co_id, pc->pc_rmach,
						cs->cs_type, *endpoint, &ctx,
						services_connect);
		if (rc != 0)
			return M0_ERR(rc);
		pools_common_svc_ctx_tlink_init_at_tail(ctx, &pc->pc_svc_ctxs);
	}
	M0_CNT_INC(pc->pc_nr_svcs[cs->cs_type]);

	return M0_RC(rc);
}

static int service_ctxs_create(struct m0_pools_common *pc,
			       struct m0_conf_filesystem *fs,
			       bool service_connect)
{
	struct m0_conf_diter    it;
	struct m0_conf_service *s;
	struct m0_conf_obj     *obj;
	int                     rc;

	M0_ENTRY();

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
		rc = __service_ctx_create(pc, s, service_connect);
		if (rc != 0)
			break;
	}

	m0_conf_diter_fini(&it);

	if (rc != 0)
		service_ctxs_destroy(pc);

	return M0_RC(rc);
}

M0_INTERNAL struct m0_reqh_service_ctx *
m0_pools_common_service_ctx_find_by_type(const struct m0_pools_common *pc,
					 enum m0_conf_service_type type)
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

static int pc_service_find(struct m0_reqh_service_ctx **dest,
			   const struct m0_tl          *svc_ctxs,
			   enum m0_conf_service_type    type)
{
	M0_PRE(M0_IN(type, (M0_CST_RMS, M0_CST_STS)));

	*dest = m0_tl_find(pools_common_svc_ctx, ctx, svc_ctxs,
			   ctx->sc_type == type);
	if (*dest != NULL)
		return M0_RC(0);

	return M0_ERR_INFO(-EPROTO, "The mandatory %s service is missing."
			   " Make sure it is specified in the conf db.",
			   type == M0_CST_STS ? "STS" : "RM");
}

M0_INTERNAL void m0_pools_common_init(struct m0_pools_common *pc,
				      struct m0_rpc_machine *rmach,
				      struct m0_conf_filesystem *fs)
{

	M0_ENTRY();
	M0_PRE(pc != NULL && fs != NULL);

	*pc = (struct m0_pools_common){
				.pc_rmach = rmach,
				.pc_md_redundancy = fs->cf_redundancy,
				.pc_confc = m0_confc_from_obj(&fs->cf_obj)};

	pools_common_svc_ctx_tlist_init(&pc->pc_svc_ctxs);
	pools_tlist_init(&pc->pc_pools);
	M0_LEAVE();
}

M0_INTERNAL int m0_pools_service_ctx_create(struct m0_pools_common *pc,
					    struct m0_conf_filesystem *fs)
{
	int rc;

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

	rc = pc_service_find(&pc->pc_rm_ctx, &pc->pc_svc_ctxs, M0_CST_RMS) ?:
	     pc_service_find(&pc->pc_ss_ctx, &pc->pc_svc_ctxs, M0_CST_STS);
	if (rc != 0)
		goto err;

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
		pool_id = &pver_obj->pv_obj.co_parent->co_parent->co_id;
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

	pc->pc_md_pool = m0_pool_find(pc, &fs->cf_md_pool->pl_obj.co_id);
	M0_ASSERT(pc->pc_md_pool != NULL);

	m0_conf_diter_fini(&it);
	if (rc != 0)
		m0_pools_destroy(pc);

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
