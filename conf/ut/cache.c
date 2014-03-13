/* -*- c -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 26-Jul-2012
 */

#include "conf/cache.h"
#include "conf/obj_ops.h"  /* m0_conf_obj_create */
#include "conf/preload.h"  /* m0_confstr_parse, m0_confx_free */
#include "conf/onwire.h"   /* m0_confx_obj, m0_confx */
#include "conf/ut/file_helpers.h"
#include "lib/buf.h"       /* m0_buf, M0_BUF_INITS */
#include "ut/ut.h"

struct m0_mutex      g_lock;
struct m0_conf_cache g_cache;

void test_obj_xtors(void)
{
	struct m0_conf_obj            *obj;
	const struct m0_conf_obj_type *t = NULL;

	while ((t = m0_conf_obj_type_next(t)) != NULL) {
		const struct m0_fid fid = M0_FID_TINIT(t->cot_ftype.ft_id ,
						       1, 0);
		M0_ASSERT(m0_conf_fid_is_valid(&fid));
		obj = m0_conf_obj_create(&g_cache, &fid);
		M0_UT_ASSERT(obj != NULL);

		m0_mutex_lock(&g_lock);
		m0_conf_obj_delete(obj);
		m0_mutex_unlock(&g_lock);
	}
}

void test_cache(void)
{
	struct m0_conf_obj  *obj;
	size_t               i;
	int                  rc;
	struct {
		struct m0_fid id;
	} samples[] = {
		{ /* profile */     M0_FID_TINIT('p', ~0, 0) },
		{ /* file-system */ M0_FID_TINIT('f', 7, 2) },
		{ /* directory */   M0_FID_TINIT('D', 7, 3) }
	};

	for (i = 0; i < ARRAY_SIZE(samples); ++i) {
		obj = m0_conf_cache_lookup(&g_cache, &samples[i].id);
		M0_UT_ASSERT(obj == NULL);

		obj = m0_conf_obj_create(&g_cache, &samples[i].id);
		M0_UT_ASSERT(obj != NULL);
		M0_UT_ASSERT(m0_fid_eq(&obj->co_id, &samples[i].id));

		m0_mutex_lock(&g_lock);
		rc = m0_conf_cache_add(&g_cache, obj);
		m0_mutex_unlock(&g_lock);
		M0_UT_ASSERT(rc == 0);

		M0_UT_ASSERT(m0_conf_cache_lookup(&g_cache,
						  &samples[i].id) == obj);
	}

	m0_mutex_lock(&g_lock);
	m0_conf_cache_del(&g_cache, obj);
	m0_mutex_unlock(&g_lock);

	M0_UT_ASSERT(m0_conf_cache_lookup(&g_cache, &obj->co_id) == NULL);

	/* Duplicated identity. */
	obj = m0_conf_obj_create(&g_cache, &samples[0].id);
	M0_UT_ASSERT(obj != NULL);

	m0_mutex_lock(&g_lock);
	rc = m0_conf_cache_add(&g_cache, obj);
	m0_mutex_unlock(&g_lock);
	M0_UT_ASSERT(rc == -EEXIST);

	m0_mutex_lock(&g_lock);
	m0_conf_obj_delete(obj);
	m0_mutex_unlock(&g_lock);
}

void test_obj_find(void)
{
	int                 rc;
	const struct m0_fid id = M0_FID_TINIT('p', 1, 0);
	struct m0_conf_obj *p = NULL;
	struct m0_conf_obj *q = NULL;

	m0_mutex_lock(&g_lock);

	rc = m0_conf_obj_find(&g_cache, &id, &p);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(p != NULL);

	rc = m0_conf_obj_find(&g_cache, &id, &q);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(q == p);

	rc = m0_conf_obj_find(&g_cache, &M0_FID_TINIT('d', 1, 0), &q);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(q != p);

	m0_mutex_unlock(&g_lock);
}

void test_obj_fill(void)
{
	struct m0_confx    *enc;
	struct m0_conf_obj *obj;
	int                 i;
	int                 rc;
	char                buf[4096] = {0};

	m0_confx_free(NULL); /* to make sure this can be done */

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf_xc.txt"), buf, sizeof buf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confstr_parse(buf, &enc);
	M0_UT_ASSERT(rc == 0);
        M0_UT_ASSERT(enc->cx_nr == 7); /* "conf_xc.txt" describes 8 objects */

	m0_mutex_lock(&g_lock);
	for (i = 0; i < enc->cx_nr; ++i) {
		struct m0_confx_obj *xobj = &enc->cx_objs[i];

		rc = m0_conf_obj_find(&g_cache, &xobj->o_id, &obj) ?:
			m0_conf_obj_fill(obj, xobj, &g_cache);
		M0_UT_ASSERT(rc == 0);
	}
	m0_mutex_unlock(&g_lock);

	m0_confx_free(enc);
}

/* ------------------------------------------------------------------ */

static int cache_init(void)
{
	m0_mutex_init(&g_lock);
	m0_conf_cache_init(&g_cache, &g_lock);
	return 0;
}

static int cache_fini(void)
{
	m0_conf_cache_fini(&g_cache);
	m0_mutex_fini(&g_lock);
	return 0;
}

const struct m0_test_suite conf_ut = {
	.ts_name  = "conf-ut",
	.ts_init  = cache_init,
	.ts_fini  = cache_fini,
	.ts_tests = {
		{ "obj-xtors", test_obj_xtors },
		{ "cache",     test_cache     },
		{ "obj-find",  test_obj_find  },
		{ "obj-fill",  test_obj_fill  },
		{ NULL, NULL }
	}
};
