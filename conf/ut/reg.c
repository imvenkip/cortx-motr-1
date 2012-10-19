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

#include "conf/reg.h"
#include "conf/obj_ops.h" /* c2_conf_obj_ops */
#include "lib/memory.h"   /* C2_ALLOC_PTR */
#include "lib/buf.h"      /* c2_buf, C2_BUF_INITS */
#include "lib/ut.h"

void test_reg(void)
{
	struct c2_conf_reg  reg;
	struct c2_conf_obj *obj;
	int                 rc;
	size_t              i;
	struct {
		enum c2_conf_objtype type;
		struct c2_buf        id;
	} samples[] = {
		{ C2_CO_PROFILE,    C2_BUF_INIT(7, "pr\0file") },
		{ C2_CO_FILESYSTEM, C2_BUF_INITS("filesystem") },
		{ C2_CO_DIR,        C2_BUF_INITS("dir") }
	};

	c2_conf_reg_init(&reg);

	for (i = 0; i < ARRAY_SIZE(samples); ++i) {
		obj = c2_conf_reg_lookup(&reg, samples[i].type, &samples[i].id);
		C2_UT_ASSERT(obj == NULL);

		obj = c2_conf_obj_create(samples[i].type, &samples[i].id);
		C2_UT_ASSERT(obj != NULL);
		rc = c2_conf_reg_add(&reg, obj);
		C2_UT_ASSERT(rc == 0);

		C2_UT_ASSERT(c2_conf_reg_lookup(&reg, samples[i].type,
						&samples[i].id) == obj);
	}

	c2_conf_reg_del(&reg, obj);
	C2_UT_ASSERT(c2_conf_reg_lookup(&reg, obj->co_type, &obj->co_id) ==
		     NULL);
	/* Unregistered object needs to be deleted explicitly. */
	c2_conf_obj_delete(obj);

	/* Duplicated identity. */
	obj = c2_conf_obj_create(samples[0].type, &samples[0].id);
	C2_UT_ASSERT(obj != NULL);
	rc = c2_conf_reg_add(&reg, obj);
	C2_UT_ASSERT(rc < 0);
	c2_conf_obj_delete(obj);

	c2_conf_reg_fini(&reg);
}
