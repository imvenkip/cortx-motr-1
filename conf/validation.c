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
#include "conf/obj_ops.h"  /* m0_conf_dir_tl */
#include "lib/string.h"    /* m0_vsnprintf */
#include "lib/errno.h"     /* ENOENT */

extern const struct m0_conf_ruleset m0_ios_rules;
extern const struct m0_conf_ruleset m0_pool_rules;
static const struct m0_conf_ruleset conf_rules;

static const struct m0_conf_ruleset *conf_validity_checks[] = {
	&m0_ios_rules,
	&m0_pool_rules,
	&conf_rules,
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

/** @todo XXX: Rewrite using m0_conf_glob(). */
M0_INTERNAL char *m0_conf__path_validate(const struct m0_conf_cache *cache,
					 struct m0_conf_obj *start,
					 const struct m0_fid *path,
					 char *buf, size_t buflen)
{
	struct m0_conf_obj *obj;
	int                 rc;

	M0_PRE(m0_conf_cache_is_locked(cache));
	M0_PRE(start == NULL || start->co_cache == cache);

	obj = start ?: m0_conf_cache_lookup(cache, &M0_CONF_ROOT_FID);
	if (obj == NULL)
		return m0_vsnprintf(buf, buflen, "No root object");
	if (obj->co_status != M0_CS_READY)
		return m0_vsnprintf(buf, buflen, "Conf object is not ready: "
				    FID_F, FID_P(&obj->co_id));

	for (; m0_fid_is_set(path) /* !eop */; ++path) {
		if (m0_fid_eq(path, &M0_CONF_ANY_FID)) {
			const struct m0_conf_dir *dir =
				M0_CONF_CAST(obj, m0_conf_dir);
			char *err = NULL;

			if (!m0_tl_forall(m0_conf_dir, x, &dir->cd_items,
					  /* recursive call */
					  (err = m0_conf__path_validate(
						  cache, x, path + 1,
						  buf, buflen)) == NULL))
				return err;
			return NULL;
		}
		rc = obj->co_ops->coo_lookup(obj, path, &obj);
		if (rc == -ENOENT)
			return m0_vsnprintf(buf, buflen, "Unreachable path: "
					    FID_F "/" FID_F, FID_P(&obj->co_id),
					    FID_P(path));
		M0_ASSERT(rc == 0);
		if (obj->co_status != M0_CS_READY)
			return m0_vsnprintf(buf, buflen,
					    "Conf object is not ready: " FID_F,
					    FID_P(&obj->co_id));
	}
	return NULL;
}

/* ------------------------------------------------------------------
 * Auxiliary functions used by conf validation rules
 * ------------------------------------------------------------------ */

enum { CONF_GLOB_BATCH = 16 }; /* the value is arbitrary */

/**
 * Counts services of given filesystem.
 *
 * If svc_type == 0, conf_services_count() returns the number of all services.
 * Otherwise only services of specified type are counted.
 *
 * Returns negative value in case of error.
 */
static int conf_services_count(const struct m0_conf_filesystem *fs,
			       enum m0_conf_service_type svc_type,
			       char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[CONF_GLOB_BATCH];
	int                       rc;
	int                       n = 0;

	M0_PRE(0 <= svc_type && svc_type < M0_CST_NR);

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, fs->cf_obj.co_cache,
			  &fs->cf_obj, M0_CONF_FILESYSTEM_NODES_FID,
			  M0_CONF_ANY_FID, M0_CONF_NODE_PROCESSES_FID,
			  M0_CONF_ANY_FID, M0_CONF_PROCESS_SERVICES_FID,
			  M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv)) > 0)
		n += m0_count(i, rc, svc_type == 0 ||
			      M0_CONF_CAST(objv[i], m0_conf_service)->cs_type ==
			      svc_type);
	if (rc < 0) {
		(void)m0_conf_glob_error(&glob, buf, buflen);
		return M0_ERR(rc);
	}
	return n;
}

/* ------------------------------------------------------------------
 * Conf validation rules
 * ------------------------------------------------------------------ */

static char *conf_filesystem_error(const struct m0_conf_cache *cache,
				   char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *fs_obj;
	uint32_t                  redundancy;
	int                       nr_ios;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID);
	while ((rc = m0_conf_glob(&glob, 1, &fs_obj)) > 0) {
		redundancy = M0_CONF_CAST(fs_obj,
					  m0_conf_filesystem)->cf_redundancy;
		if (redundancy == 0)
			return m0_vsnprintf(
				buf, buflen, FID_F": `redundancy' is 0."
				" Should be in (0, nr_ioservices] range",
				FID_P(&fs_obj->co_id));
		nr_ios = conf_services_count(
			M0_CONF_CAST(fs_obj, m0_conf_filesystem), M0_CST_IOS,
			buf, buflen);
		if (nr_ios < 0)
			return buf;
		if (nr_ios == 0)
			return m0_vsnprintf(buf, buflen,
					    FID_F": no IO services",
					    FID_P(&fs_obj->co_id));
		if (redundancy > nr_ios)
			return m0_vsnprintf(
				buf, buflen, FID_F": `redundancy' exceeds"
				" the number of IO services (%u)",
				FID_P(&fs_obj->co_id), nr_ios);
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
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
	const struct m0_conf_obj *obj;
	char                     *err;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
			  M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID,
			  M0_CONF_NODE_PROCESSES_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, 1, &obj)) > 0) {
		err = conf_process_endpoint_error(
			M0_CONF_CAST(obj, m0_conf_process), buf, buflen);
		if (err != NULL)
			return err;
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static char *conf_profile_svc_type_error(const struct m0_conf_profile *prof,
					 char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[CONF_GLOB_BATCH];
	enum m0_conf_service_type svc_type;
	bool                      confd_p = false;
	bool                      mds_p = false;
	int                       i;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, NULL, &prof->cp_obj,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
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
	return m0_vsnprintf(buf, buflen, "No %s service defined for profile "
			    FID_F, confd_p ? "meta-data" : "confd",
			    FID_P(&prof->cp_obj.co_id));
}

static char *
conf_svc_type_error(const struct m0_conf_cache *cache, char *buf, size_t buflen)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *obj;
	char                     *err;
	int                       rc;

	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, 1, &obj)) > 0) {
		err = conf_profile_svc_type_error(
			M0_CONF_CAST(obj, m0_conf_profile), buf, buflen);
		if (err != NULL)
			return err;
	}
	return rc < 0 ? m0_conf_glob_error(&glob, buf, buflen) : NULL;
}

static const struct m0_conf_ruleset conf_rules = {
	.cv_name  = "m0_conf_rules",
	.cv_rules = {
#define _ENTRY(name) { #name, name }
		_ENTRY(conf_filesystem_error),
		_ENTRY(conf_endpoint_error),
		_ENTRY(conf_svc_type_error),
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
