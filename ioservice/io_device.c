/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 18/06/2011
 */

/**
   @page io_calls_params_dld DLD of I/O calls Parameters Server-side

   - @ref io_calls_params_dld-ovw
   - @ref io_calls_params_dld-def
   - @ref io_calls_params_dld-req
   - @ref io_calls_params_dld-depends
   - @subpage io_calls_params_dld-fspec
   - @ref io_calls_params_dld-lspec
      - @ref io_calls_params_dld-lspec-comps
      - @ref io_calls_params_dld-lspec-ds
   - @ref io_calls_params_dld-conformance
   - @ref io_calls_params_dld-ut
   - @ref io_calls_params_dld-st
   - @ref io_calls_params_dld-O
   - @ref io_calls_params_dld-ref

   <hr>
   @section io_calls_params_dld-ovw Overview
   Read or write of a file stored according to a parity de-clustered layout
   involves operations on component objects stored on a potentially large
   number of devices attached to multiple network nodes (together comprising a
   storage pool). Devices and nodes can be unavailable for various reasons,
   including:
     - hardware failure,
     - network partitioning,
     - administrative decision,
     - software failure.

   To maintain desired levels of availability, IO must continue when some
   elements (devices or nodes) are unavailable. Parity de-clustering layout
   provides necessary redundancy to tolerate failures. This module provides
   data-structures and interfaces to report changes in pool availability
   characteristics to clients. Clients use this information to guide transitions
   between various IO regimes:
     - normal, when all devices and nodes in the pool are available,
     - degraded, when a client uses redundancy ("parity") to continue IO in the
       face of unavailability,
     - non-blocking availability (NBA), when a client redirects write operations
       to a different pool.
   The decision to switch to either degraded or NBA regime is made by a policy
   outside of this module.

   The key data-structure describing availability characteristics of a pool is
   its "failure vector", which is a collection of devices and nodes states,
   the states transitions history. Failure vector is maintained by pool manager
   and updated through distributed consensus algorithm, so that all non-faulty
   services in the pool have the same idea about the failure vector. The failure
   vector is shared and used by io services, SNS repair copy machines or other
   components.

   Clients receive failure vector together with the file layout from metadata
   services, and additionally as through unsolicited failure vector change
   notifications, sent by servers. A client uses failure vector to guide its
   IO requests. Failure vector has a version number, incremented on each vector
   update which leads to devices or nodes state transitions. Each client IO
   request is tagged with the version of failure vector used to guide this
   request. When ioservice receives an IO request with a stale failure vector
   version, the request is denied. Client has to pro-actively update its failure
   vector from ioservice.

   <hr>
   @section io_calls_params_dld-def Definitions
   - Failure vector. This is a data structures to keep track of device and node
     availability characteristics in a pool. Device/node join and leave will
     generate events to update the failure vector.

   - Failure vector version number. Failure vector is changing due to device
     and/or node join/leave. Failure vector is cached by clients, md services
     and other components. To keep the failure vector coherent in these
     components, a version number is introduced to failure vector.

   - Failure vector update event. This is a device or node event which will
     cause failure vector to update its internal state. When faiure vector
     is updated on server, clients will get un-solicited notification. Client
     should fetch the whole failure vector ( at the init stage ) or fetch the
     incremental updates ( when clients already have cache ).

   <hr>
   @section io_calls_params_dld-req Requirements
   The following requirements should be meet:
   - @b R.DLD.FV Failure vector is stored in persistent storage. It can be
                 shared in multiple components.
   - @b R.DLD.FV_Update Failure vector can be updated by device/node events.
                 Events are device/node failure, new device join/node join,
                 device starting recovering, device/node going offline,
                 device/node going online, etc.
   - @b R.DLD.FV_Query Failure vector can be queried, by whole, or for a
                 specified region marked by version number.
   - @b R.DLD.FV_Notification Failure vector update will generate un-solicited
                 notification to other services and components, like mdservice,
                 client, or SNS repair copy machine.
		      - When Clients/mdservice get this notification, they compare
			their cached failure vector version number with the
			lastest one. If not match, client or mdservice will
			update its failure vector.
		      - When SNS repair copy machine get this notification,
			it will take proper action, such as start a SNS repair.
   - @b R.DLD.FV_version Failure vector version number is embedded to client
                 I/O request.
   - @b R.DLD.FV_Fetch Failure vector can be fectched to client or mdservice
                 or other services in reply message. This can be the whole
                 failure vector, or the recent changes between specific version.

   <hr>
   @section io_calls_params_dld-depends Dependencies
   - Layout.
   - SNS.

   <hr>
   @section io_calls_params_dld-lspec Logical Specification

   - @ref io_calls_params_dld-lspec-comps
   - @ref io_calls_params_dld-lspec-ds
   - @ref io_calls_params_dld-lspec-if

   @subsection io_calls_params_dld-lspec-comps Component Overview
   All I/O requests (including read, write, create, delete, etc.) will be modified
   to embed the client known failure vector version numbers.

   I/O replies are extended to embed failure vector updates (not the whole
   failure vector, but the delta between the last known to current version) to
   client.

   A unsolicited notification fop is introduced. This fop will be sent to client
   by server. This fop contains the latest failure vector version number. When
   clients or other services get this notification, they should fetch the failure
   vector updates.

   @subsection io_calls_params_dld-lspec-ds Data Structures
   The data structures of failure vector, failure vector version number are in
   @ref poolmach module.

   I/O request fop will be extended to embed the its known failure vector
   version number. I/O replies will embed failure vector updates. Please refer
   to the io_fops.h file for detailed design.

   Failure vector will be stored in reqh as a shared key. By this, latest
   failure vectors and its version number information can be shared between
   multiple modules in the same request handler.

   @subsection io_calls_params_dld-lspec-if Interfaces
   The failure vector and version number operations are designed and listed
   in @ref poolmach.

   <hr>
   @section io_calls_params_dld-conformance Conformance
   - @b I.DLD.FV Failure vector is stored in persistent storage by pool manager.
		 That will be done later and is out of the scope of this module.
                 Failure vector can be shared in multiple components by manage it
                 as a reqh key.

   - @b I.DLD.FV_Update Failure vector can be updated by events. These events
                 can be device or node join, leave, or others.

   - @b I.DLD.FV_Query Failure vector can be queried, by whole, or by delta.
		 Pool machine provide query interfaces.

   - @b I.DLD.FV_Notification Upon failure vector updates, notification will
                 be sent unsolicitedly to related services. These notification
                 carry the latest version number. Recipients will fetch failure
                 vector update based on its known version number.

   - @b I.DLD.FV_version Failure vector version number is embedded to client
                 I/O request.

   - @b I.DLD.FV_Fetch Failure vector can be fetched to client or mdservice
                 or other services in reply message. This can be the whole
                 failure vector, or the recent changes between specific version.

   <hr>
   @section io_calls_params_dld-ut Unit Tests
   Unit tests will cover the following cases:
   - client requests are tagged with valid failure vector version number.
   - client requests with valid failure version number are handles by ioservice
     correctly.
   - client requests are denied (returned special error code, that is
     M0_IOP_ERROR_FAILURE_VECTOR_VERSION_MISMATCH) if their failure
     vector version number are mismatch with the server known failure vector
     version number. Failure vector version number updates are serialized into
     replies.
   - failure vector changed on server. An un-solicited notification is sent to
     relative services, including clients.

   <hr>
   @section io_calls_params_dld-st System Tests
   Clients perform regular file system operations, meanwhile the I/O servers
   have some device failed, off-line, SNS repair, etc. operations. Clients
   should be able to continue operations without error.

   <hr>
   @section io_calls_params_dld-O Analysis
   If new location is replied to client, client will contact the new device.
   This is an extra request. But in a normal system load, the performance
   should be almost the same as normal.

   <hr>
   @section io_calls_params_dld-ref References
   - <a href="https://docs.google.com/a/seagate.com/document/d/1Wvw8CTXOpH9ztFCD
ysXAXAgJ5lQoMcOkbBNBW9Nz9OM/edit#heading=h.650bad0e414a"> HLD of SNS repair </a>
   - @ref cm
   - @ref agents
   - @ref poolmach
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/lockers.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"
#include "conf/diter.h" /* m0_conf_diter */
#include "conf/obj.h"
#include "conf/obj_ops.h" /* M0_CONF_DIRNEXT */
#include "conf/validation.h" /* m0_conf_ruleset */
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "ioservice/io_fops.h"
#include "mero/setup.h"
#include "lib/locality.h"

/**
   @addtogroup io_calls_params_dldDFS
   @{
 */

/**
 * Please See ioservice/io_service.c
 * The key is alloted in m0_ios_register().
 */
M0_EXTERN unsigned poolmach_key;

static bool is_conf_controller(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_CONTROLLER_TYPE;
}

static int ios_poolmach_devices_add(struct m0_poolmach        *poolmach,
				    struct m0_poolnode        *poolnode,
				    struct m0_conf_controller *ctrl)
{
	struct m0_conf_disk *disk;
	struct m0_pooldev   *pooldev;
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_obj  *disks_dir = &ctrl->cc_disks->cd_obj;
	uint32_t             device_idx = 0;
	int                  rc;

	M0_ENTRY();

	rc = m0_confc_open_sync(&disks_dir, disks_dir, M0_FID0);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_confc_readdir_sync(disks_dir, &obj)) > 0) {
		if (m0_is_ios_disk(obj)) {
			disk = M0_CONF_CAST(obj, m0_conf_disk);
			M0_ASSERT(device_idx <
				  poolmach->pm_state->pst_nr_devices);
			pooldev = &poolmach->pm_state->pst_devices_array[
				device_idx];
			pooldev->pd_sdev_fid = disk->ck_dev->sd_obj.co_id;
			pooldev->pd_sdev_idx = disk->ck_dev->sd_dev_idx;
			pooldev->pd_index = device_idx;
			pooldev->pd_id = disk->ck_obj.co_id;
			pooldev->pd_node = poolnode;
			m0_pooldev_clink_add(&pooldev->pd_clink,
					     &obj->co_ha_chan);
			M0_CNT_INC(device_idx);
		}
	}
	m0_confc_close(obj);
	m0_confc_close(disks_dir);
	return M0_RC(0);
}

static int ios_poolmach_devices_count(struct m0_conf_controller *ctrl)
{
	struct m0_conf_obj  *obj = NULL;
	struct m0_conf_obj  *disks_dir = &ctrl->cc_disks->cd_obj;
	unsigned int         nr_sdevs = 0;
	int                  rc;

	M0_PRE(disks_dir != NULL);

	M0_ENTRY();
	rc = m0_confc_open_sync(&disks_dir, disks_dir, M0_FID0);
	if (rc != 0)
		return M0_ERR(rc);
	while ((rc = m0_confc_readdir_sync(disks_dir, &obj)) > 0) {
		if (m0_is_ios_disk(obj))
			M0_CNT_INC(nr_sdevs);
	}
	m0_confc_close(obj);
	m0_confc_close(disks_dir);
	return M0_RC(nr_sdevs);
}

static int ios_poolmach_args_init(struct m0_confc             *confc,
				  struct m0_conf_filesystem   *fs,
				  struct m0_ios_poolmach_args *args)
{
	struct m0_conf_diter       it;
	struct m0_conf_controller *ctrl;
	int                        rc;

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_RACKS_FID,
				M0_CONF_RACK_ENCLS_FID,
				M0_CONF_ENCLOSURE_CTRLS_FID);
	if (rc != 0)
		return M0_ERR(rc);
	args->nr_nodes = args->nr_sdevs = 0;
	while ((rc = m0_conf_diter_next_sync(&it, is_conf_controller)) ==
		M0_CONF_DIRNEXT) {
		ctrl = M0_CONF_CAST(m0_conf_diter_result(&it),
				    m0_conf_controller);
		args->nr_sdevs += ios_poolmach_devices_count(ctrl);
		M0_CNT_INC(args->nr_nodes);
	}
	m0_conf_diter_fini(&it);
	M0_POST(rc == 0);
	return M0_RC(0);
}

static int ios_poolmach_objs_fill(struct m0_confc           *confc,
				  struct m0_conf_filesystem *fs,
				  struct m0_poolmach        *poolmach)
{
	struct m0_conf_diter       it;
	struct m0_poolnode        *poolnode;
	struct m0_conf_controller *ctrl;
	uint32_t                   node_idx;
	int                        rc;

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_RACKS_FID,
				M0_CONF_RACK_ENCLS_FID,
				M0_CONF_ENCLOSURE_CTRLS_FID);
	if (rc != 0)
		return M0_ERR(rc);

	node_idx = 0;
	while ((rc = m0_conf_diter_next_sync(&it, is_conf_controller)) ==
	       M0_CONF_DIRNEXT) {
		ctrl = M0_CONF_CAST(m0_conf_diter_result(&it),
				    m0_conf_controller);
		M0_ASSERT(node_idx < poolmach->pm_state->pst_nr_nodes);
		poolnode = &poolmach->pm_state->pst_nodes_array[node_idx];
		poolnode->pn_id = ctrl->cc_obj.co_id;
		ios_poolmach_devices_add(poolmach, poolnode, ctrl);
		M0_CNT_INC(node_idx);
	}
	m0_conf_diter_fini(&it);
	M0_POST(rc == 0);
	return M0_RC(0);
}

M0_INTERNAL int m0_ios_poolmach_init(struct m0_reqh_service *service)
{
	struct m0_poolmach            *poolmach;
	struct m0_reqh                *reqh = service->rs_reqh;
        struct m0_pools_common        *pc = reqh->rh_pools;
        struct m0_pool                *pool = pools_tlist_head(&pc->pc_pools);
        struct m0_pool_version        *pver = pool_version_tlist_head(&pool->po_vers);
	struct m0_confc               *confc = &reqh->rh_confc;
	struct m0_sm_group            *grp = m0_locality0_get()->lo_grp;
	struct m0_ios_poolmach_args    ios_poolmach_args;
	struct m0_conf_filesystem     *fs;
	int                            rc;

	M0_PRE(service != NULL);
	M0_PRE(service->rs_reqh_ctx != NULL);
	M0_PRE(service->rs_reqh_ctx->rc_mero != NULL);
	M0_PRE(reqh != NULL);
	M0_PRE(m0_reqh_lockers_is_empty(reqh, poolmach_key));

	/* We maintain two types of pool-machines on server-side.
	 * The first one is the global pool machine that includes all the
	 * available devices in a pool. Apart from this, each pool-version
	 * includes its own pool machine that includes devices present in the
	 * pool version. When state of a device changes, the poolmachine utility
	 * updates the global pool machine as well as the other pool machines
	 * associated with the failed device.
	 */
	poolmach = m0_alloc(sizeof *poolmach);
	if (poolmach == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto exit;
	}
	rc = m0_conf_fs_get(&reqh->rh_profile, confc, &fs);
	if (rc != 0)
		goto poolmach_free;

	rc = ios_poolmach_args_init(confc, fs, &ios_poolmach_args);
	if (rc != 0)
		goto fs_close;
	/* We are not using reqh->rh_sm_grp here, otherwise deadlock */
	m0_sm_group_lock(grp);
	rc = m0_poolmach_backed_init2(poolmach, pver, reqh->rh_beseg, grp,
				      ios_poolmach_args.nr_nodes,
				      ios_poolmach_args.nr_sdevs,
				      PM_DEFAULT_MAX_NODE_FAILURES,
				      ios_poolmach_args.nr_sdevs);
	m0_sm_group_unlock(grp);
	if (rc != 0)
		goto fs_close;
	rc = ios_poolmach_objs_fill(confc, fs, poolmach);
	m0_confc_close(&fs->cf_obj);
	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_lockers_set(reqh, poolmach_key, poolmach);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	M0_LOG(M0_DEBUG, "key init for reqh=%p, key=%d", reqh, poolmach_key);
	return M0_RC(rc);
fs_close:
	m0_confc_close(&fs->cf_obj);
poolmach_free:
	m0_free(poolmach);
exit:
	return M0_ERR(rc);
}

M0_INTERNAL struct m0_poolmach *m0_ios_poolmach_get(struct m0_reqh *reqh)
{
        struct m0_pools_common *pc = reqh->rh_pools;
        struct m0_pool         *pool = pools_tlist_head(&pc->pc_pools);
        struct m0_pool_version *pver = pool_version_tlist_head(&pool->po_vers);
	struct m0_poolmach     *pm;

	/* XXX: it floods the log:
	M0_LOG(M0_DEBUG, "key get for reqh=%p, key=%d", reqh, poolmach_key);*/
	M0_PRE(reqh != NULL);
	M0_PRE(!m0_reqh_lockers_is_empty(reqh, poolmach_key));

/*
	XXX Cleanup as part of ios pool machine removal.
	m0_rwlock_read_lock(&reqh->rh_rwlock);
	pm = m0_reqh_lockers_get(reqh, poolmach_key);
	m0_rwlock_read_unlock(&reqh->rh_rwlock);
*/
	pm = &pver->pv_mach;
	M0_POST(pm != NULL);
	return pm;
}

M0_INTERNAL void m0_ios_poolmach_fini(struct m0_reqh_service *service)
{
	struct m0_poolmach *pm;
	struct m0_reqh     *reqh;

	M0_PRE(service != NULL);
	reqh = service->rs_reqh;
	M0_PRE(reqh != NULL);

	M0_LOG(M0_DEBUG, "key fini for reqh=%p, key=%d", reqh, poolmach_key);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	pm = m0_reqh_lockers_get(reqh, poolmach_key);
	m0_reqh_lockers_clear(reqh, poolmach_key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
	m0_poolmach_fini(pm);
	m0_free(pm);
}

M0_INTERNAL int
m0_ios_poolmach_version_updates_pack(struct m0_poolmach         *pm,
				     const struct m0_fv_version *cli,
				     struct m0_fv_version       *version,
				     struct m0_fv_updates       *updates)
{
	struct m0_poolmach_versions    curr;
	struct m0_poolmach_versions   *verp;
	struct m0_tl                   events_list;
	struct m0_poolmach_event_link *scan;
	uint32_t                       count;
	uint32_t                       index;
	int                            rc;

	updates->fvu_count  = 0;
	updates->fvu_events = NULL;

	m0_poolmach_current_version_get(pm, &curr);
	verp = (struct m0_poolmach_versions*)version;
	*verp = curr;
	if (!m0_poolmach_version_before((struct m0_poolmach_versions *)cli,
					&curr))
		return 0;

	poolmach_events_tlist_init(&events_list);
	rc = m0_poolmach_state_query(pm, (struct m0_poolmach_versions *)cli,
				     &curr, &events_list);
	if (rc != 0)
		goto out;

	count = poolmach_events_tlist_length(&events_list);
	/* Allocate memory for updates sequence object.
	 * XXX When and where to free these memory?
	 */
	updates->fvu_count  = count;
	updates->fvu_events = m0_alloc(count * sizeof(struct m0_fv_event));
	if (updates->fvu_events == NULL) {
		rc = -ENOMEM;
		m0_tl_for(poolmach_events, &events_list, scan) {
			poolmach_events_tlink_del_fini(scan);
			m0_free(scan);
		} m0_tl_endfor;
		goto out;
	}

	index = 0;
	m0_tl_for(poolmach_events, &events_list, scan) {
		updates->fvu_events[index].fve_type  = scan->pel_event.pe_type;
		updates->fvu_events[index].fve_index = scan->pel_event.pe_index;
		updates->fvu_events[index].fve_state = scan->pel_event.pe_state;
		index++;
		poolmach_events_tlink_del_fini(scan);
		m0_free(scan);
	} m0_tl_endfor;

out:
	poolmach_events_tlist_fini(&events_list);
	return M0_RC(rc);

}

/* ------------------------------------------------------------------
 * conf validation
 * ------------------------------------------------------------------ */

static char *ios_paths_conf_error(const struct m0_conf_cache *cache,
				  char *buf, size_t buflen)
{
#if 0 /* XXX WIP */
	char *err;

	/* for profile in "root/profiles": */
	err = m0_conf_path_validate(profile, buf, buflen,
				    M0_CONF_PROFILE_FILESYSTEM_FID,
				    M0_CONF_FILESYSTEM_RACKS_FID,
				    M0_CONF_RACK_ENCLS_FID,
				    M0_CONF_ENCLOSURE_CTRLS_FID);
	if (err != NULL)
		return err;
#else
	/*
	 * - "filesystem/racks/enclosures/controllers" path is available.
	 */
	return NULL;
#endif
}

/** Ensures that the conf DAG can be used by ios_poolmach_args_init(). */
static char *ios_args_init_conf_error(const struct m0_conf_cache *cache,
				      char *buf, size_t buflen)
{
	/*
	 * - for ctrl in controllers:
	 *       for disk in ctrl.cc_disks:
	 *           ## m0_is_ios_disk() calls disk2service().
	 *           assert type(disk.ck_dev) is m0_conf_sdev
	 *           assert m0_conf_obj_type(m0_conf_obj_grandparent(
	 *               disk.ck_dev)) == &M0_CONF_SERVICE_TYPE
	 */
	return NULL;
}

/** Ensures that the conf DAG can be used by ios_poolmach_objs_fill(). */
static char *ios_objs_fill_conf_error(const struct m0_conf_cache *cache,
				      char *buf, size_t buflen)
{
	/*
	 * - len(controllers) <= poolmach.pm_state.pst_nr_nodes
	 *
	 * - nr_ios_disks = 0
	 *   for ctrl in controllers:
	 *       nr_disks += len(ctrl.cc_disks)
	 *       for disk in ctrl.cc_disks:
	 *           if m0_is_ios_disk(disk):
	 *               ++nr_ios_disks
	 *   assert nr_ios_disks <= poolmach.pm_state.pst_nr_devices
	 */
	return NULL;
}

const struct m0_conf_ruleset m0_ios_rules = {
	.cv_name  = "m0_ios_rules",
	.cv_rules = {
#define _ENTRY(name) { #name, name }
		_ENTRY(ios_paths_conf_error),
		_ENTRY(ios_args_init_conf_error),
		_ENTRY(ios_objs_fill_conf_error),
#undef _ENTRY
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
/** @} end of io_calls_params_dldDFS */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
