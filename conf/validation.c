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
#include "conf/cache.h"
#include "conf/obj.h"
#include "conf/obj_ops.h"  /* m0_conf_dir_tl */
#include "lib/string.h"    /* m0_vsnprintf */
#include "lib/errno.h"     /* ENOENT */

extern const struct m0_conf_ruleset m0_ios_rules;
extern const struct m0_conf_ruleset m0_pool_rules;

static const struct m0_conf_ruleset *conf_validity_checks[] = {
	&m0_ios_rules,
	&m0_pool_rules,
};

char *m0_conf_validation_error(const struct m0_conf_cache *cache,
			       char *buf, size_t buflen)
{
	unsigned                   i;
	const struct m0_conf_rule *rule;
	int                        rc;
	char                      *_buf;
	size_t                     _buflen;
	char                      *err;

	M0_PRE(buf != NULL && buflen != 0);

	for (i = 0; i < ARRAY_SIZE(conf_validity_checks); ++i) {
		for (rule = &conf_validity_checks[i]->cv_rules[0];
		     rule->cvr_name != NULL;
		     ++rule) {
			rc = snprintf(buf, buflen, "%s.%s: ",
				      conf_validity_checks[i]->cv_name,
				      rule->cvr_name);
			M0_ASSERT(rc > 0 && (size_t)rc < buflen);
			_buflen = strlen(buf);
			_buf = buf + _buflen;
			err = rule->cvr_error(cache, _buf, _buflen);
			if (err == NULL)
				continue;
			return err == _buf ? buf : m0_vsnprintf(
				buf, buflen, "%s.%s: %s",
				conf_validity_checks[i]->cv_name,
				rule->cvr_name, err);
		}
	}
	return NULL;
}

M0_INTERNAL char *m0_conf__path_validate(const struct m0_conf_cache *cache,
					 struct m0_conf_obj *start,
					 const struct m0_fid *path,
					 char *buf, size_t buflen)
{
	struct m0_conf_obj *obj;
	int                 rc;

	M0_PRE(m0_conf_cache_is_locked(cache));

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
