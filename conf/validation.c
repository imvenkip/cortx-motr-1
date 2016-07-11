/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery.vorotyntsev@seagate.com>
 * Original creation date: 6-Jan-2016
 */

/**
 * @addtogroup conf_validation
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/validation.h"
#include "conf/glob.h"
#include "conf/dir.h"      /* m0_conf_dir_tl */
#include "lib/string.h"    /* m0_vsnprintf */
#include "lib/errno.h"     /* ENOENT */
#include "lib/memory.h"    /* M0_ALLOC_ARR */
#include "net/net.h"       /* m0_net_endpoint_is_valid */

enum { CONF_GLOB_BATCH = 16 }; /* the value is arbitrary */

/** @see conf_filesystem_stats_get() */
struct conf_filesystem_stats {
	uint32_t cs_nr_ioservices;
	uint32_t cs_nr_iodevices;  /* pool width */
};

static const struct m0_conf_ruleset conf_rules;

static const struct m0_conf_ruleset *conf_validity_checks[] = {
	&conf_rules,
	/*
	 * Mero modules may define their own conf validation rules and add
	 * them here.
	 */
};

char *
m0_conf_validation_error(struct m0_conf_cache *cache, char *buf, size_t buflen)
{
	char *err;
	M0_PRE(buf != NULL && buflen != 0);

	m0_conf_cache_lock(cache);
	err = m0_conf_validation_error_locked(cache, buf, buflen);
	m0_conf_cache_unlock(cache);
	return err;
}

M0_INTERNAL char *
m0_conf_validation_error_locked(const struct m0_conf_cache *cache,
				char *buf, size_t buflen)
{
	unsigned                   i;
	const struct m0_conf_rule *rule;
	int                        rc;
	char                      *_buf;
	size_t                     _buflen;
	char                      *err;

	M0_PRE(buf != NULL && buflen != 0);
	M0_PRE(m0_conf_cache_is_locked(cache));

	for (i = 0; i < ARRAY_SIZE(conf_validity_checks); ++i) {
		for (rule = &conf_validity_checks[i]->cv_rules[0];
		     rule->cvr_name != NULL;
		     ++rule) {
			rc = snprintf(buf, buflen, "[%s.%s] ",
				      conf_validity_checks[i]->cv_name,
				      rule->cvr_name);
			M0_ASSERT(rc > 0 && (size_t)rc < buflen);
			_buflen = strlen(buf);
			_buf = buf + _buflen;
			err = rule->cvr_error(cache, _buf, buflen - _buflen);
			if (err == NULL)
				continue;
			return err == _buf ? buf : m0_vsnprintf(
				buf, buflen, "[%s.%s] %s",
				conf_validity_checks[i]->cv_name,
				rule->cvr_name, err);
		}
	}
	return NULL;
}

static char *conf_orphans_error(const struct m0_conf_cache *cache,
				char *buf, size_t buflen)
{
	const struct m0_conf_obj *obj;

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		if (obj->co_status != M0_CS_READY)
			return m0_vsnprintf(buf, buflen, FID_F" is not defined",
					    FID_P(&obj->co_id));
		if (obj->co_parent == NULL &&
		    m0_conf_obj_type(obj) != &M0_CONF_ROOT_TYPE)
			return m0_vsnprintf(buf, buflen, "Dangling object: "
					    FID_F, FID_P(&obj->co_id));
	} m0_tl_endfor;
	return NULL;
}

/**
 * Applies `func' to each m0_conf_filesystem object in the `cache'.
 *
 * Returns error message in case of error, NULL otherwise.
 */
static char *conf_fs_apply(char *(*func)(const struct m0_conf_filesystem *fs,
					 char *buf, size_t buflen),
			   const struct m0_conf_cache *cache,
			   char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *obj;
	char                     *err;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID);
	while ((rc = m0_conf_glob(&glob, 1, &obj)) > 0) {
		err = func(M0_CONF_CAST(obj, m0_conf_filesystem), buf, buflen);
		if (err != NULL)
			return err;
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *conf_filesystem_stats_get(const struct m0_conf_filesystem *fs,
				       struct conf_filesystem_stats *stats,
				       char *buf, size_t buflen)
{
	struct m0_conf_glob           glob;
	const struct m0_conf_obj     *objv[CONF_GLOB_BATCH];
	const struct m0_conf_service *svc;
	int                           i;
	int                           rc;

	M0_SET0(stats);
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, fs->cf_obj.co_cache,
			  &fs->cf_obj, M0_CONF_FILESYSTEM_NODES_FID,
			  M0_CONF_ANY_FID, M0_CONF_NODE_PROCESSES_FID,
			  M0_CONF_ANY_FID, M0_CONF_PROCESS_SERVICES_FID,
			  M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			svc = M0_CONF_CAST(objv[i], m0_conf_service);
			if (svc->cs_type == M0_CST_IOS) {
				++stats->cs_nr_ioservices;
				stats->cs_nr_iodevices +=
					m0_conf_dir_len(svc->cs_sdevs);
			}
		}
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *_conf_filesystem_error(const struct m0_conf_filesystem *fs,
				    char *buf, size_t buflen)
{
	struct conf_filesystem_stats stats;
	const struct m0_conf_obj    *obj;
	char                        *err;

	if (fs->cf_redundancy == 0)
		/*
		 * Non-oostore mode: meta-data is stored at MD services,
		 * not IO services. No special MD pool is required.
		 */
		return NULL;

	err = conf_filesystem_stats_get(fs, &stats, buf, buflen);
	if (err != NULL)
		return err;
	if (stats.cs_nr_ioservices == 0)
		return m0_vsnprintf(buf, buflen, FID_F": No IO services",
				    FID_P(&fs->cf_obj.co_id));
	if (fs->cf_redundancy > stats.cs_nr_ioservices)
		return m0_vsnprintf(
			buf, buflen, FID_F": `redundancy' (%u) exceeds"
			" the number of IO services (%u)",
			FID_P(&fs->cf_obj.co_id), fs->cf_redundancy,
			stats.cs_nr_ioservices);
	obj = m0_conf_cache_lookup(fs->cf_obj.co_cache, &fs->cf_mdpool);
	if (obj == NULL)
		return m0_vsnprintf(
			buf, buflen, FID_F": `mdpool' "FID_F" is missing",
			FID_P(&fs->cf_obj.co_id), FID_P(&fs->cf_mdpool));
	if (m0_conf_obj_grandparent(obj) != &fs->cf_obj)
		return m0_vsnprintf(
			buf, buflen, FID_F": `mdpool' "FID_F" belongs another"
			" filesystem", FID_P(&fs->cf_obj.co_id),
			FID_P(&fs->cf_mdpool));
	return NULL;
}

static char *conf_filesystem_error(const struct m0_conf_cache *cache,
				   char *buf, size_t buflen)
{
	return conf_fs_apply(_conf_filesystem_error, cache, buf, buflen);
}

static const struct m0_conf_node *
conf_node_from_sdev(const struct m0_conf_sdev *sdev)
{
	return M0_CONF_CAST(
		m0_conf_obj_grandparent(
			m0_conf_obj_grandparent(
				m0_conf_obj_grandparent(&sdev->sd_obj))),
		m0_conf_node);
}

static char *conf_iodev_error(const struct m0_conf_sdev *sdev,
			      const struct m0_conf_sdev **iodevs,
			      uint32_t nr_iodevs, char *buf, size_t buflen)
{
	uint32_t j;

	if (iodevs[sdev->sd_dev_idx] != sdev &&
	    M0_CONF_CAST(m0_conf_obj_grandparent(&sdev->sd_obj),
			 m0_conf_service)->cs_type == M0_CST_IOS) {
		if (sdev->sd_dev_idx >= nr_iodevs)
			return m0_vsnprintf(
				buf, buflen, FID_F": dev_idx (%u) does not"
				" belong [0, P) range; P=%u",
				FID_P(&sdev->sd_obj.co_id), sdev->sd_dev_idx,
				nr_iodevs);
		if (iodevs[sdev->sd_dev_idx] != NULL)
			return m0_vsnprintf(
				buf, buflen, FID_F": dev_idx is not unique,"
				" duplicates that of "FID_F,
				FID_P(&sdev->sd_obj.co_id),
				FID_P(&iodevs[sdev->sd_dev_idx]->sd_obj.co_id));
		if (m0_exists(i, nr_iodevs, iodevs[j = i] != NULL &&
			      m0_streq(iodevs[i]->sd_filename,
				       sdev->sd_filename) &&
			      conf_node_from_sdev(sdev) ==
			      conf_node_from_sdev(iodevs[i])))
			return m0_vsnprintf(
				buf, buflen, FID_F": filename is not unique,"
				" duplicates that of "FID_F,
				FID_P(&sdev->sd_obj.co_id),
				FID_P(&iodevs[j]->sd_obj.co_id));
		iodevs[sdev->sd_dev_idx] = sdev;
	}
	return NULL;
}

/**
 * Checks that P attribute of the pool version (pver) does not exceed
 * the total number of IO storage devices.
 * Also validates .sd_dev_idx and .sd_filename attributes of IO storage
 * devices, reachable from this pver.
 */
static char *conf_pver_error(const struct m0_conf_pver *pver,
			     const struct m0_conf_sdev **iodevs,
			     uint32_t nr_iodevs, char *buf, size_t buflen)
{
	struct m0_conf_glob        glob;
	const struct m0_conf_obj  *objv[CONF_GLOB_BATCH];
	const struct m0_conf_objv *diskv;
	char                      *err;
	int                        i;
	int                        rc;

	if (pver->pv_u.subtree.pvs_attr.pa_P > nr_iodevs)
		return m0_vsnprintf(buf, buflen,
				    FID_F": Pool width (%u) exceeds total"
				    " number of IO devices (%u)",
				    FID_P(&pver->pv_obj.co_id),
				    pver->pv_u.subtree.pvs_attr.pa_P,
				    nr_iodevs);
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, NULL, &pver->pv_obj,
			  M0_CONF_PVER_RACKVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_RACKV_ENCLVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_ENCLV_CTRLVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_CTRLV_DISKVS_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			diskv = M0_CONF_CAST(objv[i], m0_conf_objv);
			err = conf_iodev_error(
				M0_CONF_CAST(diskv->cv_real,
					     m0_conf_disk)->ck_dev,
				iodevs, nr_iodevs, buf, buflen);
			if (err != NULL)
				return err;
		}
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *
_conf_pvers_error(const struct m0_conf_filesystem *fs, char *buf, size_t buflen)
{
	struct conf_filesystem_stats stats;
	const struct m0_conf_sdev  **iodevs;
	struct m0_conf_glob          glob;
	const struct m0_conf_obj    *objv[CONF_GLOB_BATCH];
	char                        *err = NULL;
	int                          i;
	int                          rc;

	/*
	 * XXX TODO: Return validation error if any of the following
	 * conditions is violated:
	 *
	 * 1. m0_conf_pver_subtree::pvs_tolerance vector does not
	 *    tolerate more failures than there are objvs at the
	 *    corresponding level of pver subtree.
	 *
	 * 2. m0_conf_pver_formulaic::pvf_base refers to an existing
	 *    actual pver.
	 *
	 * 3. m0_conf_pver_formulaic::pvf_id is unique per cluster.
	 *
	 * 4. m0_conf_pver_formulaic::pvf_allowance vector does not
	 *    tolerate more failures than there are objvs at the
	 *    corresponding level of base pver's subtree.
	 *
	 * 5. .pvf_allowance[M0_CONF_PVER_LVL_DISKS] <= P - (N + 2K),
	 *    where P, N, and K are taken from .pvs_attr of the base pver.
	 *
	 * 6. m0_conf_objv::cv_real pointers are unique.
	 *
	 * 7. Subtree of base pver does not contain childless rackvs,
	 *    enclvs, or ctrlvs.
	 */

	err = conf_filesystem_stats_get(fs, &stats, buf, buflen);
	if (err != NULL)
		return err;
	if (stats.cs_nr_iodevices == 0)
		return m0_vsnprintf(buf, buflen, FID_F": No IO devices",
				    FID_P(&fs->cf_obj.co_id));

	M0_ALLOC_ARR(iodevs, stats.cs_nr_iodevices);
	if (iodevs == NULL)
		return m0_vsnprintf(buf, buflen, "Insufficient memory");

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, NULL, &fs->cf_obj,
			  M0_CONF_FILESYSTEM_POOLS_FID, M0_CONF_ANY_FID,
			  M0_CONF_POOL_PVERS_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			err = conf_pver_error(M0_CONF_CAST(objv[i],
							   m0_conf_pver),
					      iodevs, stats.cs_nr_iodevices,
					      buf, buflen);
			if (err != NULL)
				break;
		}
	}
	m0_free(iodevs);
	if (err != NULL)
		return err;
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *
conf_pvers_error(const struct m0_conf_cache *cache, char *buf, size_t buflen)
{
	return conf_fs_apply(_conf_pvers_error, cache, buf, buflen);
}

static char *conf_process_endpoint_error(const struct m0_conf_process *proc,
					 char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[CONF_GLOB_BATCH];
	const char              **epp;
	int                       i;
	int                       rc;

	if (!m0_net_endpoint_is_valid(proc->pc_endpoint))
		return m0_vsnprintf(buf, buflen, FID_F": Invalid endpoint: %s",
				    FID_P(&proc->pc_obj.co_id),
				    proc->pc_endpoint);

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, NULL, &proc->pc_obj,
			  M0_CONF_PROCESS_SERVICES_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			for (epp = M0_CONF_CAST(objv[i],
						m0_conf_service)->cs_endpoints;
			     *epp != NULL; ++epp) {
				if (!m0_net_endpoint_is_valid(*epp))
					return m0_vsnprintf(
						buf, buflen,
						FID_F": Invalid endpoint: %s",
						FID_P(&objv[i]->co_id), *epp);
			}
		}
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *
conf_endpoint_error(const struct m0_conf_cache *cache, char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[CONF_GLOB_BATCH];
	char                     *err;
	int                       i;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
			  M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID,
			  M0_CONF_NODE_PROCESSES_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			err = conf_process_endpoint_error(
				M0_CONF_CAST(objv[i], m0_conf_process),
				buf, buflen);
			if (err != NULL)
				return err;
		}
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *_conf_service_type_error(const struct m0_conf_filesystem *fs,
				      char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[CONF_GLOB_BATCH];
	enum m0_conf_service_type svc_type;
	bool                      confd_p = false;
	bool                      mds_p = false;
	int                       i;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, NULL, &fs->cf_obj,
			  M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID,
			  M0_CONF_NODE_PROCESSES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROCESS_SERVICES_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			svc_type = M0_CONF_CAST(objv[i],
						m0_conf_service)->cs_type;
			if (!m0_conf_service_type_is_valid(svc_type))
				return m0_vsnprintf(
					buf, buflen,
					FID_F": Invalid service type: %d",
					FID_P(&objv[i]->co_id), svc_type);
			if (svc_type == M0_CST_MGS)
				confd_p = true;
			else if (svc_type == M0_CST_MDS)
				mds_p = true;
		}
	}
	if (rc < 0)
		return m0_conf_glob_error(&glob, buf, buflen);
	if (confd_p && mds_p)
		return NULL;
	return m0_vsnprintf(buf, buflen, "No %s service defined for filesystem "
			    FID_F, confd_p ? "meta-data" : "confd",
			    FID_P(&fs->cf_obj.co_id));
}

static char *conf_service_type_error(const struct m0_conf_cache *cache,
				     char *buf, size_t buflen)
{
	return conf_fs_apply(_conf_service_type_error, cache, buf, buflen);
}

static char *conf_service_sdevs_error(const struct m0_conf_cache *cache,
				      char *buf, size_t buflen)
{
	struct m0_conf_glob           glob;
	const struct m0_conf_obj     *objv[CONF_GLOB_BATCH];
	const struct m0_conf_service *svc;
	int                           i;
	int                           rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
			  M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID,
			  M0_CONF_NODE_PROCESSES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROCESS_SERVICES_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0) {
		for (i = 0; i < rc; ++i) {
			svc = M0_CONF_CAST(objv[i], m0_conf_service);
			if ((svc->cs_type == M0_CST_IOS) ==
			    m0_conf_dir_tlist_is_empty(
				    &svc->cs_sdevs->cd_items))
				return m0_vsnprintf(
					buf, buflen,
					FID_F": `sdevs' of %s be empty",
					FID_P(&objv[i]->co_id),
					(svc->cs_type == M0_CST_IOS ?
					 "an IO service may not" :
					 "a non-IO service must"));
		}
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static const struct m0_conf_ruleset conf_rules = {
	.cv_name  = "m0_conf_rules",
	.cv_rules = {
#define _ENTRY(name) { #name, name }
		_ENTRY(conf_orphans_error),
		_ENTRY(conf_filesystem_error),
		_ENTRY(conf_endpoint_error),
		_ENTRY(conf_service_type_error),
		_ENTRY(conf_service_sdevs_error),
		_ENTRY(conf_pvers_error),
#undef _ENTRY
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
