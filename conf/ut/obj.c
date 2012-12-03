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

#include "conf/obj_ops.h"
#include "conf/reg.h"
#include "conf/preload.h"
#include "conf/onwire.h"  /* confx_object */
#include "lib/ut.h"
#include <stdio.h>

#define QUOTE(s) QUOTE_(s)
#define QUOTE_(s) #s

/* COLIBRI_CONFX_OBJ_CFG comes from CFLAGS; see conf/ut/Makefile.am */
#define CONFX_CFG QUOTE(COLIBRI_CONFX_OBJ_CFG)

static void confx_read(char *buf, size_t buf_size)
{
	FILE *f;
	int   n;

	f = fopen(CONFX_CFG, "r");
	C2_UT_ASSERT(f != NULL);

	n = fread(buf, 1, buf_size, f);
	C2_UT_ASSERT(n > 0);
	buf[n] = '\0';

	fclose(f);
}

void test_obj_xtors(void)
{
	struct c2_conf_obj  *obj;
	enum c2_conf_objtype t;

	C2_UT_ASSERT(C2_CO_DIR == 0);
	for (t = 0; t < C2_CO_NR; ++t) {
		obj = c2_conf_obj_create(t, &(const struct c2_buf)
					 C2_BUF_INITS("test"));
		C2_UT_ASSERT(obj != NULL);
		c2_conf_obj_delete(obj);
	}
}

void test_obj_find(void)
{
	struct c2_conf_reg  reg;
	int                 rc;
	const struct c2_buf id = C2_BUF_INITS("test");
	struct c2_conf_obj *p = NULL;
	struct c2_conf_obj *q = NULL;

	c2_conf_reg_init(&reg);

	rc = c2_conf_obj_find(&reg, C2_CO_PROFILE, &id, &p);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(p != NULL);

	rc = c2_conf_obj_find(&reg, C2_CO_PROFILE, &id, &q);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(q == p);

	rc = c2_conf_obj_find(&reg, C2_CO_DIR, &id, &q);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(q != p);

	c2_conf_reg_fini(&reg);
}

void test_obj_fill(void)
{
	enum { KB = 1 << 10 };
	struct c2_conf_reg  reg;
	char                buf[32 * KB] = {0};
	struct confx_object xobjs[64];
	int		    nr_objs;
	int                 i;
	struct c2_conf_obj *obj;
	int                 rc;

	c2_conf_reg_init(&reg);

	confx_read(buf, sizeof buf);

	nr_objs = c2_conf_parse(buf, xobjs, ARRAY_SIZE(xobjs));
	/* Note, that nr_objs is the number of parsed object
	 * descriptors, which only accidentally equals C2_CO_NR. */
	C2_UT_ASSERT(nr_objs == 8);

	for (i = 0; i < nr_objs; ++i) {
		rc = c2_conf_obj_find(&reg, xobjs[i].o_conf.u_type,
				      &xobjs[i].o_id, &obj);
		C2_UT_ASSERT(rc == 0);

		rc = c2_conf_obj_fill(obj, &xobjs[i], &reg);
		C2_UT_ASSERT(rc == 0);
	}
#if 0 /*XXX*/
	extern void c2_conf__reg2dot(const struct c2_conf_reg *reg);
	c2_conf__reg2dot(&reg);
#endif
	c2_confx_fini(xobjs, nr_objs);
	c2_conf_reg_fini(&reg);
}
