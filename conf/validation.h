/* -*- C -*- */
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
#pragma once
#ifndef __MERO_CONF_VALIDATION_H__
#define __MERO_CONF_VALIDATION_H__

#include "lib/types.h"  /* bool */

struct m0_conf_cache;
struct m0_conf_obj;
struct m0_fid;

/**
 * @defgroup conf_validation
 *
 * Mero subsystems that use confc API (m0t1fs, m0d, ioservice, &c.)
 * have certain expectations of the configuration objects they work with.
 * Subsystem developers specify these expectations in the form of "rules",
 * which valid configuration data should conform to.
 *
 * @{
 */

/**
 * Performs semantic validation of the DAG of configuration objects.
 *
 * If m0_conf_validation_error() finds a problem with configuration
 * data, it returns a pointer to a string that describes the problem.
 * This may be either a pointer to a string that the function stores
 * in `buf', or a pointer to some (imutable) static string (in which
 * case `buf' is unused).  If the function stores a string in `buf',
 * then at most `buflen' bytes are stored (the string may be truncated
 * if `buflen' is too small).  The string always includes a terminating
 * null byte ('\0').
 *
 * If no issues with configuration data are found, m0_conf_validation_error()
 * returns NULL.
 *
 * @pre  buf != NULL && buflen != 0
 * @pre  m0_conf_cache_is_locked(cache)
 */
char *m0_conf_validation_error(const struct m0_conf_cache *cache,
			       char *buf, size_t buflen);

/** Validation rule. */
struct m0_conf_rule {
	/*
	 * Use the name of the function that .cvr_error points at.
	 * This simplifies finding the rule that failed.
	 */
	const char *cvr_name;
	/**
	 * @see m0_conf_validation_error() for arguments' description.
	 *
	 * @pre  m0_conf_cache_is_locked(cache)
	 * (This precondition is enforced by m0_conf_validation_error().)
	 */
	char     *(*cvr_error)(const struct m0_conf_cache *cache,
			       char *buf, size_t buflen);
};

/** Maximal number of rules in a m0_conf_ruleset. */
enum { M0_CONF_RULES_MAX = 32 };

/** Named set of validation rules. */
struct m0_conf_ruleset {
	/*
	 * Use the name of m0_conf_ruleset variable. This simplifies
	 * finding the rule that failed.
	 */
	const char         *cv_name;
	/*
	 * This array must end with { NULL, NULL }.
	 */
	struct m0_conf_rule cv_rules[M0_CONF_RULES_MAX];
};

/**
 * Checks whether configuration path is passable.
 *
 * Returns NULL if the path can be traversed in full, i.e., if all
 * configuration objects along the pass are accessible and M0_CS_READY.
 * Otherwise m0_conf_path_validate() returns a pointer to a string that
 * describes the problem.
 *
 * @param buf     Error message buffer; see m0_conf_validation_error().
 * @param buflen  The length of `buf'; see m0_conf_validation_error().
 * @param cache   Configuration cache.
 * @param start   Configuration object to start traversal from.
 * @param ...     Path to validate. The path is relative to `start' object.
 *
 * If a path component leads to a m0_conf_dir object, then the next path
 * component (if there is any) should be either fid of a particular dir item,
 * or M0_CONF_ANY_FID. The latter makes m0_conf_path_validate() check the
 * remaining path for every item in the dir --- this is usually what user
 * wants to do.
 *
 * Example:
 * @code
 * // Check if all m0_conf_profile, m0_conf_filesystem, and m0_conf_node
 * // objects are ready.
 * err = m0_conf_path_validate(buf, buflen, cache, NULL,
 *                             M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
 *                             M0_CONF_PROFILE_FILESYSTEM_FID,
 *                             M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID);
 * @endcode
 *
 * @pre  m0_conf_cache_is_locked(cache)
 * @pre  start == NULL || start->co_cache == cache
 */
#define m0_conf_path_validate(buf, buflen, cache, start, ...)     \
	m0_conf__path_validate(                                   \
		(cache), (start),                                 \
		(const struct m0_fid []){ __VA_ARGS__, M0_FID0 }, \
		(buf), (buflen))
M0_INTERNAL char *m0_conf__path_validate(const struct m0_conf_cache *cache,
					 struct m0_conf_obj *start,
					 const struct m0_fid *path,
					 char *buf, size_t buflen);

/** @} */
#endif /* __MERO_CONF_VALIDATION_H__ */

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
