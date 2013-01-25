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

#include "conf/obj_ops.h"  /* m0_conf_obj_create */
#include "conf/cache.h"
#include "conf/preload.h"  /* m0_confstr_parse, m0_confx_free */
#include "conf/onwire.h"   /* m0_confx_obj, m0_confx */
#include "conf/ut/file_helpers.h"
#include "lib/ut.h"

void test_obj_xtors(void)
{
	struct m0_conf_obj  *obj;
	enum m0_conf_objtype t;

	M0_CASSERT(M0_CO_DIR == 0);
	for (t = 0; t < M0_CO_NR; ++t) {
		obj = m0_conf_obj_create(NULL, t, &(const struct m0_buf)
					 M0_BUF_INITS("test"));
		M0_UT_ASSERT(obj != NULL);
		m0_conf_obj_delete(obj);
	}
}

void test_obj_find(void)
{
	struct m0_conf_cache cache;
	int                  rc;
	const struct m0_buf  id = M0_BUF_INITS("test");
	struct m0_conf_obj  *p = NULL;
	struct m0_conf_obj  *q = NULL;

	m0_conf_cache_init(&cache);
	m0_mutex_lock(&cache.ca_lock);

	rc = m0_conf_obj_find(&cache, M0_CO_PROFILE, &id, &p);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(p != NULL);

	rc = m0_conf_obj_find(&cache, M0_CO_PROFILE, &id, &q);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(q == p);

	rc = m0_conf_obj_find(&cache, M0_CO_DIR, &id, &q);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(q != p);

	m0_mutex_unlock(&cache.ca_lock);
	m0_conf_cache_fini(&cache);
}

void test_obj_fill(void)
{
	struct m0_confx     *enc;
	struct m0_conf_cache cache;
	struct m0_conf_obj  *obj;
	int                  i;
	int                  rc;
	char                 buf[1024] = {0};

	m0_confx_free(NULL); /* to make sure this can be done */

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf_xc.txt"), buf, sizeof buf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confstr_parse(buf, &enc);
	M0_UT_ASSERT(rc == 0);
        M0_UT_ASSERT(enc->cx_nr == 8); /* "conf_xc.txt" describes 8 objects */

	m0_conf_cache_init(&cache);

	m0_mutex_lock(&cache.ca_lock);
	for (i = 0; i < enc->cx_nr; ++i) {
		struct m0_confx_obj *xobj = &enc->cx_objs[i];

		rc = m0_conf_obj_find(&cache, xobj->o_conf.u_type, &xobj->o_id,
				      &obj) ?:
			m0_conf_obj_fill(obj, xobj, &cache);
		M0_UT_ASSERT(rc == 0);
	}
	m0_mutex_unlock(&cache.ca_lock);

	m0_conf_cache_fini(&cache);
	m0_confx_free(enc);
}
