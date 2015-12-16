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

#include "lib/memory.h"    /* M0_ALLOC_PTR, M0_ALLOC_ARR, m0_free */
#include "lib/errno.h"     /* EINVAL */
#include "lib/string.h"    /* m0_strdup */
#include "lib/mutex.h"     /* m0_mutex */
#include "lib/chan.h"      /* m0_chan, m0_clink */
#include "reqh/reqh.h"     /* m0_reqh */
#include "conf/obj.h"
#include "conf/helpers.h"
#include "conf/confc.h"
#include "conf/obj_ops.h"  /* m0_conf_dirval */
#include "conf/diter.h"    /* m0_conf_diter_next_sync */
#include "ha/note.h"       /* m0_ha_nvec, m0_ha_state_accept, m0_ha_state_get */
#include "pool/flset.h"    /* m0_flset_pver_has_failed_dev */


M0_INTERNAL int m0_conf_fs_get(const struct m0_fid        *profile,
			       struct m0_confc            *confc,
			       struct m0_conf_filesystem **result)
{
	struct m0_conf_obj *obj;
	int                 rc;

	rc = m0_confc_open_sync(&obj, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID, *profile,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc == 0)
		*result = M0_CONF_CAST(obj, m0_conf_filesystem);
	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_device_get(struct m0_confc      *confc,
				   struct m0_fid        *fid,
				   struct m0_conf_sdev **sdev)
{
	struct m0_conf_obj *obj;
	int                 rc;

	rc = m0_conf_obj_find_lock(&confc->cc_cache, fid, &obj);
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

	rc = m0_conf_obj_find_lock(&confc->cc_cache, fid, &obj) ?:
		m0_confc_open_sync(&obj, obj, M0_FID0);
	if (rc == 0)
		*disk = M0_CONF_CAST(obj, m0_conf_disk);
	return M0_RC(rc);
}

M0_INTERNAL bool m0_obj_is_pver(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE;
}

M0_INTERNAL int m0_conf_poolversion_get(const struct m0_fid  *profile,
					struct m0_confc      *confc,
					struct m0_flset      *failure_set,
					struct m0_conf_pver **result)
{
	struct m0_conf_diter       it;
	struct m0_conf_filesystem *fs;
	struct m0_conf_pver       *pver;
	struct m0_conf_obj        *obj;
	int                        rc;

	rc = m0_conf_fs_get(profile, confc, &fs);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_POOLS_FID,
				M0_CONF_POOL_PVERS_FID);
	if (rc != 0) {
		m0_confc_close(&fs->cf_obj);
		return M0_ERR(rc);
	}

	while ((rc = m0_conf_diter_next_sync(&it, m0_obj_is_pver)) ==
		M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		M0_ASSERT(m0_conf_obj_type(obj) == &M0_CONF_PVER_TYPE);
		pver = M0_CONF_CAST(obj, m0_conf_pver);
		if (!m0_flset_pver_has_failed_dev(failure_set, pver)) {
			*result = pver;
			rc = 0;
			break;
		}
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);

	return M0_RC(*result == NULL ? -ENOENT : rc);
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

M0_INTERNAL int m0_conf_objs_ha_update(struct m0_rpc_session *ha_sess,
				       struct m0_ha_nvec     *nvec)
{
	struct m0_mutex       chan_lock;
	struct m0_chan        chan;
	struct m0_clink       clink;
	int                   rc;

	M0_PRE(nvec->nv_nr <= M0_HA_STATE_UPDATE_LIMIT);
	m0_mutex_init(&chan_lock);
	m0_chan_init(&chan, &chan_lock);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&chan, &clink);

	rc = m0_ha_state_get(ha_sess, nvec, &chan);
	if (rc == 0) {
		/*
		 * m0_ha_state_get() sends a fop to HA service caller.
		 * We need to wait for reply fop.
		 */
		m0_chan_wait(&clink);
		if (nvec->nv_nr >= 0)
			m0_ha_state_accept(nvec);
		else
			rc = M0_ERR(nvec->nv_nr);
	}

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_mutex_lock(&chan_lock);
	m0_chan_fini(&chan);
	m0_mutex_unlock(&chan_lock);
	m0_mutex_fini(&chan_lock);

	return M0_RC(rc);
}

M0_INTERNAL int m0_conf_obj_ha_update(struct m0_rpc_session *ha_sess,
				      const struct m0_fid   *obj_fid)
{
	struct m0_ha_nvec nvec;
	struct m0_ha_note note;

	note.no_id    = *obj_fid;
	note.no_state = M0_NC_UNKNOWN;
	nvec.nv_nr    = 1;
	nvec.nv_note  = &note;
	return M0_RC(m0_conf_objs_ha_update(ha_sess, &nvec));
}

static void __ha_nvec_reset(struct m0_ha_nvec *nvec, int32_t total)
{
	int i;
	for(i = 0; i < nvec->nv_nr; i++)
		nvec->nv_note[i] = (struct m0_ha_note){ M0_FID0, 0 };
	nvec->nv_nr = min32(total, M0_HA_STATE_UPDATE_LIMIT);
}

M0_INTERNAL int m0_conf_confc_ha_update(struct m0_rpc_session *ha_sess,
					struct m0_confc       *confc)
{
	struct m0_conf_cache *cache = &confc->cc_cache;
	struct m0_conf_obj   *obj;
	struct m0_ha_nvec     nvec;
	int32_t               total;
	int                   rc;
	int                   i = 0;

	if ((total = m0_conf_cache_tlist_length(&cache->ca_registry)) < 1)
		return M0_RC(-ENOENT);

	nvec.nv_nr = min32(total, M0_HA_STATE_UPDATE_LIMIT);
	M0_ALLOC_ARR(nvec.nv_note, nvec.nv_nr);
	if (nvec.nv_note == NULL)
		return M0_ERR(-ENOMEM);

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		nvec.nv_note[i].no_id = obj->co_id;
		nvec.nv_note[i++].no_state = M0_NC_UNKNOWN;
		if (nvec.nv_nr == i) {
			rc = m0_conf_objs_ha_update(ha_sess, &nvec);
			if (rc == 0) {
				total -= nvec.nv_nr;
				if (total > 0)
					__ha_nvec_reset(&nvec, total), i = 0;
				else
					break;
			} else {
				break;
			}
		}
	} m0_tlist_endfor;

	m0_free(nvec.nv_note);
	return M0_RC(rc);
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

static bool _obj_is_service(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
}

M0_INTERNAL int m0_conf_service_open(struct m0_confc            *confc,
				     const struct m0_fid        *profile,
				     const char                 *ep,
				     enum m0_conf_service_type   svc_type,
				     struct m0_conf_service    **result)
{
	struct m0_conf_filesystem *fs;
	struct m0_conf_obj        *obj;
	struct m0_conf_service    *s;
	struct m0_conf_diter       it;
	int                        rc;
	bool                       found = false;

	M0_PRE(result != NULL);
	rc = m0_conf_fs_get(profile, confc, &fs);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0) {
		m0_confc_close(&fs->cf_obj);
		return M0_ERR(rc);
	}

	while ((rc = m0_conf_diter_next_sync(&it, _obj_is_service)) ==
		M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		s = M0_CONF_CAST(obj, m0_conf_service);
		if ((ep == NULL || m0_streq(s->cs_endpoints[0], ep)) &&
		    s->cs_type == svc_type) {
			*result = s;
			m0_conf_cache_lock(&confc->cc_cache);
			m0_conf_obj_get(obj);
			m0_conf_cache_unlock(&confc->cc_cache);
			rc = 0;
			found = true;
			break;
		}
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);

	if (rc == 0 && !found)
		rc = -ENOENT;
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
	[M0_CST_SNS_REB] = "sns_rebalance",/* SNS repair */
	[M0_CST_ADDB2]   = "addb2",        /* Addb */
	[M0_CST_DS1]     = "ds1",          /* Dummy service 1*/
	[M0_CST_DS2]     = "ds2"           /* Dummy service 2 */
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

M0_INTERNAL bool m0_conf_is_pool_version_dirty(struct m0_confc     *confc,
					       const struct m0_fid *pver_fid)
{
	struct m0_conf_obj *obj;
	bool                dirty;

	m0_conf_cache_lock(&confc->cc_cache);
	dirty = m0_conf_obj_find(&confc->cc_cache, pver_fid, &obj) != 0 ||
		m0_conf_obj_is_stub(obj) ||
		M0_CONF_CAST(obj, m0_conf_pver)->pv_nfailed > 0;
	m0_conf_cache_unlock(&confc->cc_cache);
	return dirty;
}

M0_INTERNAL int m0_conf__obj_count(const struct m0_fid *profile,
				   struct m0_confc     *confc,
				   bool (*filter)(const struct m0_conf_obj *obj),
				   int                  *count,
				   int                   level,
				   const struct m0_fid  *path)
{
	struct m0_conf_diter       it;
	struct m0_conf_filesystem *fs = NULL;
	int                        rc;

	rc = m0_conf_fs_get(profile, confc, &fs);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf__diter_init(&it, confc, &fs->cf_obj, level, path);
	if (rc != 0)
		return M0_ERR(rc);

	while ((rc = m0_conf_diter_next_sync(&it, filter)) == M0_CONF_DIRNEXT)
			M0_CNT_INC(*count);

	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);
	return M0_RC(rc);
}

M0_INTERNAL struct m0_conf_pver **m0_conf_pvers(const struct m0_conf_obj *obj)
{
	const struct m0_conf_obj_type *obj_type = m0_conf_obj_type(obj);

	if (obj_type == &M0_CONF_RACK_TYPE)
		return M0_CONF_CAST(obj, m0_conf_rack)->cr_pvers;
	else if (obj_type == &M0_CONF_ENCLOSURE_TYPE)
		return M0_CONF_CAST(obj, m0_conf_enclosure)->ce_pvers;
	else if (obj_type == &M0_CONF_CONTROLLER_TYPE)
		return M0_CONF_CAST(obj, m0_conf_controller)->cc_pvers;
	else if (obj_type == &M0_CONF_DISK_TYPE)
		return M0_CONF_CAST(obj, m0_conf_disk)->ck_pvers;
	else
		M0_IMPOSSIBLE("");
}

#undef M0_TRACE_SUBSYSTEM
