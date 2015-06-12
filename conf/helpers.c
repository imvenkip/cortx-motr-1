/* -*- c -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@seagate.com>
 * Original creation date: 05-Dec-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "lib/memory.h"    /* M0_ALLOC_PTR */
#include "lib/errno.h"     /* EINVAL */
#include "lib/string.h"    /* m0_strdup */
#include "reqh/reqh.h"     /* m0_reqh */
#include "conf/obj.h"
#include "conf/helpers.h"
#include "conf/confc.h"
#include "conf/obj_ops.h"  /* m0_conf_dirval */
#include "conf/diter.h"    /* m0_conf_diter_next_sync */

M0_TL_DESCR_DEFINE(m0_conf_failure_sets, "failed resources", M0_INTERNAL,
		   struct m0_conf_obj, co_fs_link, co_gen_magic,
		   M0_CONF_OBJ_MAGIC, M0_CONF_FAILURE_SETS_MAGIC);
M0_TL_DEFINE(m0_conf_failure_sets, M0_INTERNAL, struct m0_conf_obj);

M0_INTERNAL int m0_conf_fs_get(const char *profile,
			       struct m0_confc *confc,
			       struct m0_conf_filesystem **fs)
{
	struct m0_conf_obj *fs_obj = NULL;
	struct m0_fid       prof_fid;
	int                 rc;

	rc = m0_fid_sscanf(profile, &prof_fid);
	if (rc != 0)
		return M0_ERR_INFO(rc, "Cannot parse profile `%s'", profile);

	rc = m0_confc_open_sync(&fs_obj, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID, prof_fid,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc != 0) {
		M0_LOG(M0_FATAL, "m0_confc_open_sync() failed: rc=%d", rc);
		m0_confc_fini(confc);
		return M0_ERR(rc);
	}
	*fs = M0_CONF_CAST(fs_obj, m0_conf_filesystem);

	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_device_get(struct m0_confc      *confc,
				   struct m0_fid        *fid,
				   struct m0_conf_sdev **sdev)
{
	struct m0_conf_obj *obj;
	int                 rc;

	M0_PRE(confc != NULL);
	M0_PRE(fid != NULL);

	M0_LOG(M0_DEBUG, FID_F, FID_P(fid));
	m0_conf_cache_lock(&confc->cc_cache);
	rc = m0_conf_obj_find(&confc->cc_cache, fid, &obj);
	m0_conf_cache_unlock(&confc->cc_cache);

	if (rc == 0 && m0_conf_obj_is_stub(obj))
		rc = m0_confc_open_sync(&obj, obj, M0_FID0);
	if (rc == 0)
		*sdev = M0_CONF_CAST(obj, m0_conf_sdev);

	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_disk_get(struct m0_confc      *confc,
				 struct m0_fid        *fid,
				 struct m0_conf_disk **disk)
{
	struct m0_conf_obj *obj;
	int                 rc;

	M0_PRE(confc != NULL);
	M0_PRE(fid != NULL);

	m0_conf_cache_lock(&confc->cc_cache);
	rc = m0_conf_obj_find(&confc->cc_cache, fid, &obj);
	m0_conf_cache_unlock(&confc->cc_cache);

	if (rc == 0)
		rc = m0_confc_open_sync(&obj, obj, M0_FID0);
	if (rc == 0)
		*disk = M0_CONF_CAST(obj, m0_conf_disk);

	return M0_RC(rc);
}

M0_INTERNAL bool m0_obj_is_pver(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE;
}

/*
 * Simplistic implementation to make progress.
 * XXX TODO : Use failure set to find a valid pool version which does not
 * contain any device from the @failure_set.
 * This will be done as part of MERO-617
 */
M0_INTERNAL int m0_conf_poolversion_get(struct m0_conf_filesystem *fs,
					struct m0_tl              *failure_set,
					struct m0_conf_pver      **pver)
{
	struct m0_conf_diter  it;
	struct m0_conf_pver  *pv;
	struct m0_confc      *confc;
	struct m0_conf_obj   *obj;
	int                   rc;

	confc = m0_confc_from_obj(&fs->cf_obj);
	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_POOLS_FID,
				M0_CONF_POOL_PVERS_FID);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, m0_obj_is_pver)) ==
		M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE);
		pv = M0_CONF_CAST(obj, m0_conf_pver);
		if (pv->pv_state == M0_CONF_PVER_ONLINE) {
			*pver = pv;
			rc = 0;
			break;
		}
	}

	m0_conf_diter_fini(&it);

	return M0_RC(*pver == NULL ? -ENOENT : rc);
}

static int _conf_load(struct m0_conf_filesystem *fs,
		      const struct m0_fid       *path,
		      uint32_t                   nr_levels)
{

	struct m0_conf_diter  it;
	struct m0_conf_obj   *fs_obj = &fs->cf_obj;
	int                   rc;

	M0_PRE(path != NULL);

	rc = m0_conf__diter_init(&it, m0_confc_from_obj(fs_obj), fs_obj,
				 nr_levels, path);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, NULL)) == M0_CONF_DIRNEXT)
		/*
		 * We travers configuration DAG in order for conf objects to
		 * be cached.
		 */
		;

	m0_conf_diter_fini(&it);

	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_full_load(struct m0_conf_filesystem *fs)
{
	const struct m0_fid fs_to_disks[]  = {M0_CONF_FILESYSTEM_RACKS_FID,
					      M0_CONF_RACK_ENCLS_FID,
					      M0_CONF_ENCLOSURE_CTRLS_FID,
					      M0_CONF_CONTROLLER_DISKS_FID};
	const struct m0_fid fs_to_sdevs[]  = {M0_CONF_FILESYSTEM_NODES_FID,
					      M0_CONF_NODE_PROCESSES_FID,
					      M0_CONF_PROCESS_SERVICES_FID,
					      M0_CONF_SERVICE_SDEVS_FID};
	const struct m0_fid fs_to_diskvs[] = {M0_CONF_FILESYSTEM_POOLS_FID,
					      M0_CONF_POOL_PVERS_FID,
					      M0_CONF_PVER_RACKVS_FID,
					      M0_CONF_RACKV_ENCLVS_FID,
					      M0_CONF_ENCLV_CTRLVS_FID,
					      M0_CONF_CTRLV_DISKVS_FID};

        return M0_RC(_conf_load(fs, fs_to_sdevs, ARRAY_SIZE(fs_to_sdevs)) ?:
		     _conf_load(fs, fs_to_disks, ARRAY_SIZE(fs_to_disks)) ?:
		     _conf_load(fs, fs_to_diskvs, ARRAY_SIZE(fs_to_diskvs)));
}

/**
 * Update configuration objects hs state from ha service.
 * Fetches HA state of configuration objects from HA service and
 * updates local configuration cache.
 */
static int conf_ha_state_update(struct m0_rpc_session     *session,
				struct m0_conf_filesystem *fs)
{
	struct m0_confc      *confc = m0_confc_from_obj(&fs->cf_obj);
	struct m0_conf_cache *cache = &confc->cc_cache;
	struct m0_conf_obj   *obj;
	struct m0_ha_nvec    *nvec;
	struct m0_mutex       chan_lock;
	struct m0_chan        chan;
	struct m0_clink       clink;
	int                   rc;
	int                   i = 0;

	M0_ALLOC_PTR(nvec);
	if (nvec == NULL)
		return M0_ERR(-ENOMEM);

	nvec->nv_nr = m0_conf_cache_tlist_length(&cache->ca_registry);
	M0_ALLOC_ARR(nvec->nv_note, nvec->nv_nr);
	if (nvec->nv_note == NULL) {
		rc = -ENOMEM;
		goto end;
	}

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		nvec->nv_note[i].no_id = obj->co_id;
		nvec->nv_note[i++].no_state = M0_NC_UNKNOWN;
	} m0_tlist_endfor;

	m0_mutex_init(&chan_lock);
	m0_chan_init(&chan, &chan_lock);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&chan, &clink);

	rc = m0_ha_state_get(session, nvec, &chan);
	if (rc == 0) {
		/*
		 * m0_ha_state_get() sends a fop to HA service caller.
		 * We need to wait for reply fop.
		 */
		m0_chan_wait(&clink);
		m0_ha_state_accept(confc, nvec);
	}

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_mutex_lock(&chan_lock);
	m0_chan_fini(&chan);
	m0_mutex_unlock(&chan_lock);
	m0_mutex_fini(&chan_lock);

	m0_free(nvec->nv_note);
end:
	m0_free(nvec);
	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_failure_sets_build(struct m0_rpc_session *session,
					   struct m0_conf_filesystem *fs,
					   struct m0_tl *failure_sets)
{
	m0_conf_failure_sets_tlist_init(failure_sets);
	return M0_RC(conf_ha_state_update(session, fs));
}

M0_INTERNAL void m0_conf_failure_sets_destroy(struct m0_tl *failure_sets)
{
	struct m0_conf_obj *obj;

	m0_tl_for(m0_conf_failure_sets, failure_sets, obj) {
		m0_conf_failure_sets_tlist_del(obj);
	} m0_tlist_endfor;
	m0_conf_failure_sets_tlist_fini(failure_sets);
}

M0_INTERNAL int m0_conf_root_open(struct m0_confc      *confc,
				  struct m0_conf_root **root)
{
	struct m0_conf_obj *root_obj;
	int                 rc;

	M0_ENTRY();
	M0_PRE(confc->cc_root != NULL);

	rc = m0_confc_open_sync(&root_obj, confc->cc_root, M0_FID0);
	if (rc == 0)
		*root = M0_CONF_CAST(root_obj, m0_conf_root);
	return M0_RC(rc);
}

static const char *service_name[] = {
	[0]              = NULL,           /* unused, enum declarations start
					    *  from 1
					    */
	[M0_CST_MDS]     = "mdservice",    /* Meta-data service. */
	[M0_CST_IOS]     = "ioservice",    /* IO/data service. */
	[M0_CST_MGS]     = "confd",        /* Management service (confd). */
	[M0_CST_RMS]     = "rmservice",    /* RM service. */
	[M0_CST_STS]     = "stats",        /* Stats service */
	[M0_CST_HA]      = "haservice",    /* HA service */
	[M0_CST_SSS]     = "sss",          /* Start/stop service */
	[M0_CST_SNS_REP] = "sns_repair",   /* SNS repair */
	[M0_CST_SNS_REB] = "sns_rebalance" /* SNS repair */
};

M0_INTERNAL char *m0_conf_service_name_dup(const struct m0_conf_service *svc)
{
	M0_PRE(IS_IN_ARRAY(svc->cs_type, service_name));
	return m0_strdup(service_name[svc->cs_type]);
}

static struct m0_confc *conf_obj2confc(const struct m0_conf_obj *obj)
{
	M0_PRE(obj != NULL && obj->co_cache != NULL);
	return container_of(obj->co_cache, struct m0_confc, cc_cache);
}

M0_INTERNAL struct m0_reqh *m0_conf_obj2reqh(const struct m0_conf_obj *obj)
{
	struct m0_confc *confc = conf_obj2confc(obj);

	M0_PRE(confc != NULL);
	return container_of(confc, struct m0_reqh, rh_confc);
}

static void pool_versions_state_set(struct m0_conf_obj *obj)
{
	struct m0_conf_pver **pvers;
	uint32_t              i;

	if (m0_conf_obj_type(obj) == &M0_CONF_RACK_TYPE)
		pvers = M0_CONF_CAST(obj, m0_conf_rack)->cr_pvers;
	else if (m0_conf_obj_type(obj) == &M0_CONF_ENCLOSURE_TYPE)
		pvers = M0_CONF_CAST(obj, m0_conf_enclosure)->ce_pvers;
	else if (m0_conf_obj_type(obj) == &M0_CONF_CONTROLLER_TYPE)
		pvers = M0_CONF_CAST(obj, m0_conf_controller)->cc_pvers;
	else
		M0_IMPOSSIBLE("");

	if (obj->co_ha_state == M0_NC_FAILED)
		for (i = 0; pvers[i] != NULL; ++i)
			pvers[i]->pv_state = M0_CONF_PVER_FAILED;
	/**
	 * @todo XXX:
	 *           Need to make poolvesion again ONLINE if all
	 *           components becomes online ?
	 */
}

static void
failure_sets_update(struct m0_tl *failure_sets, struct m0_conf_obj *obj)
{
	if (obj->co_ha_state == M0_NC_ONLINE &&
	    m0_conf_failure_sets_tlist_contains(failure_sets, obj))
		m0_conf_failure_sets_tlist_del(obj);
	else if (obj->co_ha_state == M0_NC_FAILED &&
		 !m0_conf_failure_sets_tlist_contains(failure_sets, obj))
		m0_conf_failure_sets_tlist_add_tail(failure_sets, obj);
}

M0_INTERNAL void m0_conf_failure_sets_update(struct m0_conf_obj *obj)
{
	struct m0_tl *failure_sets = &m0_conf_obj2reqh(obj)->rh_failure_sets;

	M0_PRE(M0_IN(m0_conf_obj_type(obj), (&M0_CONF_RACK_TYPE,
					     &M0_CONF_ENCLOSURE_TYPE,
					     &M0_CONF_CONTROLLER_TYPE)));

	failure_sets_update(failure_sets, obj);
	pool_versions_state_set(obj);
}
#undef M0_TRACE_SUBSYSTEM
