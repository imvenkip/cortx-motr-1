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
 * Original creation date: 15-Feb-2016
 */

#include "conf/glob.h"
#include "conf/preload.h"  /* m0_conf_cache_from_string */
#include "conf/obj.h"      /* m0_conf_service */
#include "lib/fs.h"        /* m0_file_read */
#include "lib/memory.h"    /* m0_free */
#include "lib/errno.h"     /* ENOENT */
#include "lib/string.h"    /* m0_streq */
#include "ut/misc.h"       /* M0_UT_PATH */
#include "ut/ut.h"

static struct m0_conf_cache g_cache;
static struct m0_mutex      g_cache_lock;
static struct err_entry {
	int                  ee_errno;
	const struct m0_fid *ee_objid;
	const struct m0_fid *ee_elem;
} g_err_accum[8];

static void cache_load(struct m0_conf_cache *cache, const char *path);
static int errfunc(int errcode, const struct m0_conf_obj *obj,
		   const struct m0_fid *path);

static void test_conf_glob(void)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[16];
	int                       rc;

	m0_conf_cache_lock(&g_cache);
	/*
	 * origin == NULL
	 */
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, &g_cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
			  M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID,
			  M0_CONF_NODE_PROCESSES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROCESS_SERVICES_FID, M0_CONF_ANY_FID);
	rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv);
	M0_UT_ASSERT(rc == 11);
	/* check the types of returned objects */
	m0_forall(i, rc, M0_CONF_CAST(objv[i], m0_conf_service));

	/*
	 * origin != NULL
	 */
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL,
			  /* `cache' may be omitted if `origin' is provided */
			  NULL,
			  m0_conf_cache_lookup(&g_cache, /* pool-4 */
					       &M0_FID_TINIT('o', 1, 4)),
			  M0_CONF_POOL_PVERS_FID, M0_CONF_ANY_FID,
			  M0_CONF_PVER_RACKVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_RACKV_ENCLVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_ENCLV_CTRLVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_CTRLV_DISKVS_FID, M0_CONF_ANY_FID);
	while ((rc = m0_conf_glob(&glob, 1, objv)) > 0)
		(void)M0_CONF_CAST(objv[0], m0_conf_objv);
	M0_UT_ASSERT(rc == 0);

	/*
	 * the longest path possible
	 */
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, NULL, &g_cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
			  M0_CONF_FILESYSTEM_POOLS_FID, M0_CONF_ANY_FID,
			  M0_CONF_POOL_PVERS_FID, M0_CONF_ANY_FID,
			  M0_CONF_PVER_RACKVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_RACKV_ENCLVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_ENCLV_CTRLVS_FID, M0_CONF_ANY_FID,
			  M0_CONF_CTRLV_DISKVS_FID, M0_CONF_ANY_FID);
	rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv);
	M0_UT_ASSERT(rc == 10);
	/* check the types of returned objects */
	m0_forall(i, rc, M0_CONF_CAST(objv[i], m0_conf_objv));

	m0_conf_cache_unlock(&g_cache);
}

static void test_conf_glob_errors(void)
{
	struct m0_conf_glob       glob;
	const struct m0_conf_obj *objv[16];
	struct err_entry         *e;
	char                      errbuf[64];
	const char               *err;
	uint32_t                  i;
	int                       rc;
	const struct m0_fid       bad_fs = M0_FID_TINIT('f', 0x99, 0x99);
	const struct m0_fid       profile = M0_FID_TINIT('p', 1, 0);
	const struct m0_fid       missing[] = {
		/* service-9; first item of a conf_dir */
		M0_FID_TINIT('s', 1, 9),
		/* service-22; intermediate item of a conf_dir */
		M0_FID_TINIT('s', 1, 22),
		/* node-48; last item of a conf_dir */
		M0_FID_TINIT('n', 1, 48)
	};

	m0_conf_cache_lock(&g_cache);
	/*
	 * -ENOENT
	 */
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, errfunc, &g_cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID, bad_fs);
	rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv);
	M0_UT_ASSERT(rc == -ENOENT);
	err = m0_conf_glob_error(&glob, errbuf, sizeof errbuf);
	M0_UT_ASSERT(m0_streq(err, "Unreachable path:"
			      " <7000000000000001:0>/<6600000000000099:99>"));
	M0_SET0(&g_err_accum[0]);

	/*
	 * -EPERM, ->coo_lookup()
	 */
	m0_conf_cache_lookup(&g_cache, &profile)->co_status = M0_CS_LOADING;
	m0_conf_glob_init(&glob, M0_CONF_GLOB_ERR, errfunc, &g_cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, profile);
	rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv);
	M0_UT_ASSERT(rc == -EPERM);
	err = m0_conf_glob_error(&glob, errbuf, sizeof errbuf);
	M0_UT_ASSERT(m0_streq(err, "Conf object is not ready:"
			      " <7000000000000001:0>"));
	m0_conf_cache_lookup(&g_cache, &profile)->co_status = M0_CS_READY;
	M0_SET0(&g_err_accum[0]);

	/*
	 * -EPERM, conf_dir items
	 */
	for (i = 0; i < ARRAY_SIZE(missing); ++i)
		m0_conf_cache_lookup(&g_cache, &missing[i])->co_status =
			M0_CS_MISSING;
	m0_conf_glob_init(&glob, 0, errfunc, &g_cache, NULL,
			  M0_CONF_ROOT_PROFILES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROFILE_FILESYSTEM_FID,
			  M0_CONF_FILESYSTEM_NODES_FID, M0_CONF_ANY_FID,
			  M0_CONF_NODE_PROCESSES_FID, M0_CONF_ANY_FID,
			  M0_CONF_PROCESS_SERVICES_FID, M0_CONF_ANY_FID,
			  M0_CONF_SERVICE_SDEVS_FID, M0_CONF_ANY_FID);
	rc = m0_conf_glob(&glob, ARRAY_SIZE(objv), objv);
	M0_UT_ASSERT(rc == 4);
	/* check the types of returned objects */
	m0_forall(i, rc, M0_CONF_CAST(objv[i], m0_conf_sdev));
	for (i = 0; i < ARRAY_SIZE(missing); ++i) {
		/* check error */
		e = &g_err_accum[i];
		M0_UT_ASSERT(e->ee_errno == -EPERM);
		M0_UT_ASSERT(m0_fid_eq(e->ee_objid, &missing[i]));
		M0_UT_ASSERT(e->ee_elem == NULL);
		M0_SET0(e); /* clear g_err_accum[] slot */
		/* restore status */
		m0_conf_cache_lookup(&g_cache, &missing[i])->co_status =
			M0_CS_READY;
	}
	M0_UT_ASSERT(g_err_accum[i].ee_errno == 0);

	m0_conf_cache_unlock(&g_cache);
}

static int
errfunc(int errcode, const struct m0_conf_obj *obj, const struct m0_fid *path)
{
	struct err_entry *x;

	M0_PRE(M0_IN(errcode, (-EPERM, -ENOENT)));

	M0_UT_ASSERT(m0_exists(i, ARRAY_SIZE(g_err_accum),
			       (x = &g_err_accum[i])->ee_errno == 0));
	/* An empty slot found. Fill it with data. */
	*x = (struct err_entry){
		.ee_errno = errcode,
		.ee_objid = &obj->co_id,
		.ee_elem = errcode == -ENOENT ? path : NULL
	};
	return 0; /* do not abort the execution */
}

static void cache_load(struct m0_conf_cache *cache, const char *path)
{
	char *confstr = NULL;
	int   rc;

	M0_PRE(path != NULL && *path != '\0');

	rc = m0_file_read(path, &confstr);
	M0_UT_ASSERT(rc == 0);

	m0_conf_cache_lock(cache);
	rc = m0_conf_cache_from_string(cache, confstr);
	M0_UT_ASSERT(rc == 0);
	m0_conf_cache_unlock(cache);

	m0_free(confstr);
}

static int conf_glob_ut_init(void)
{
	m0_mutex_init(&g_cache_lock);
	m0_conf_cache_init(&g_cache, &g_cache_lock);
	cache_load(&g_cache, M0_UT_PATH("conf.xc"));
	return 0;
}

static int conf_glob_ut_fini(void)
{
	m0_conf_cache_fini(&g_cache);
	m0_mutex_fini(&g_cache_lock);
	return 0;
}

struct m0_ut_suite conf_glob_ut = {
	.ts_name  = "conf-glob-ut",
	.ts_init  = conf_glob_ut_init,
	.ts_fini  = conf_glob_ut_fini,
	.ts_tests = {
		{ "glob",        test_conf_glob },
		{ "glob-errors", test_conf_glob_errors },
		{ NULL, NULL }
	}
};
