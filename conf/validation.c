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

extern const struct m0_conf_ruleset m0_ios_rules;
extern const struct m0_conf_ruleset m0_pool_rules;

static const struct m0_conf_ruleset *conf_validity_checks[] = {
	&m0_ios_rules,
	&m0_pool_rules,
};

M0_INTERNAL char *m0_conf_validation_error(const struct m0_conf_cache *cache,
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
			if (err != _buf) {
				rc = snprintf(buf, buflen, "%s.%s: %s",
					      conf_validity_checks[i]->cv_name,
					      rule->cvr_name,
					      err);
				M0_ASSERT(rc > 0);
				/* Do not assert that rc < buflen,
				 * let the string be truncated. */
			}
			buf[buflen - 1] = '\0';
			return buf;
		}
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
