/* -*- c -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/string.h"    /* strlen */
#include "conf/reg.h"
#include "conf/onwire.h"   /* confx_object */
#include "conf/preload.h"  /* m0_confx_fini */
#include "lib/memory.h"    /* m0_free */
#include "conf/ut/file_helpers.h"
#include "lib/ut.h"

void test_obj_xtors(void)
{
	struct m0_conf_obj  *obj;
	enum m0_conf_objtype t;

	M0_CASSERT(M0_CO_DIR == 0);
	for (t = 0; t < M0_CO_NR; ++t) {
		obj = m0_conf_obj_create(t, &(const struct m0_buf)
					 M0_BUF_INITS("test"));
		M0_UT_ASSERT(obj != NULL);
		m0_conf_obj_delete(obj);
	}
}

void test_obj_find(void)
{
	struct m0_conf_reg  reg;
	int                 rc;
	const struct m0_buf id = M0_BUF_INITS("test");
	struct m0_conf_obj *p = NULL;
	struct m0_conf_obj *q = NULL;

	m0_conf_reg_init(&reg);

	rc = m0_conf_obj_find(&reg, M0_CO_PROFILE, &id, &p);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(p != NULL);

	rc = m0_conf_obj_find(&reg, M0_CO_PROFILE, &id, &q);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(q == p);

	rc = m0_conf_obj_find(&reg, M0_CO_DIR, &id, &q);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(q != p);

	m0_conf_reg_fini(&reg);
}

void test_obj_fill(void)
{
	enum { KB = 1 << 10 };
	struct m0_conf_reg  reg;
	char                buf[32 * KB] = {0};
	struct confx_object xobjs[64];
	int		    nr_objs;
	int                 i;
	struct m0_conf_obj *obj;
	int                 rc;

	m0_conf_reg_init(&reg);

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf_xc.txt"), buf, sizeof buf);
	M0_UT_ASSERT(rc == 0);

	nr_objs = m0_conf_parse(buf, xobjs, ARRAY_SIZE(xobjs));
	/* Note, that nr_objs is the number of parsed object
	 * descriptors, which only accidentally equals M0_CO_NR. */
	/* M0_UT_ASSERT(nr_objs == 32); */

	for (i = 0; i < nr_objs; ++i) {
		rc = m0_conf_obj_find(&reg, xobjs[i].o_conf.u_type,
				      &xobjs[i].o_id, &obj);
		M0_UT_ASSERT(rc == 0);

		rc = m0_conf_obj_fill(obj, &xobjs[i], &reg);
		M0_UT_ASSERT(rc == 0);
	}

	m0_confx_fini(xobjs, nr_objs);
	m0_conf_reg_fini(&reg);
}
