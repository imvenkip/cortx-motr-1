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

#include "conf/reg.h"
#include "conf/obj_ops.h" /* m0_conf_obj_ops */
#include "lib/memory.h"   /* M0_ALLOC_PTR */
#include "lib/buf.h"      /* m0_buf, M0_BUF_INITS */
#include "lib/ut.h"

void test_reg(void)
{
	struct m0_conf_reg  reg;
	struct m0_conf_obj *obj;
	int                 rc;
	size_t              i;
	struct {
		enum m0_conf_objtype type;
		struct m0_buf        id;
	} samples[] = {
		{ M0_CO_PROFILE,    M0_BUF_INIT(7, "pr\0file") },
		{ M0_CO_FILESYSTEM, M0_BUF_INITS("filesystem") },
		{ M0_CO_DIR,        M0_BUF_INITS("dir") }
	};

	m0_conf_reg_init(&reg);

	for (i = 0; i < ARRAY_SIZE(samples); ++i) {
		obj = m0_conf_reg_lookup(&reg, samples[i].type, &samples[i].id);
		M0_UT_ASSERT(obj == NULL);

		obj = m0_conf_obj_create(NULL, samples[i].type, &samples[i].id);
		M0_UT_ASSERT(obj != NULL);
		rc = m0_conf_reg_add(&reg, obj);
		M0_UT_ASSERT(rc == 0);

		M0_UT_ASSERT(m0_conf_reg_lookup(&reg, samples[i].type,
						&samples[i].id) == obj);
	}

	m0_conf_reg_del(&reg, obj);
	M0_UT_ASSERT(m0_conf_reg_lookup(&reg, obj->co_type, &obj->co_id) ==
		     NULL);
	/* Unregistered object needs to be deleted explicitly. */
	m0_conf_obj_delete(obj);

	/* Duplicated identity. */
	obj = m0_conf_obj_create(NULL, samples[0].type, &samples[0].id);
	M0_UT_ASSERT(obj != NULL);
	rc = m0_conf_reg_add(&reg, obj);
	M0_UT_ASSERT(rc < 0);
	m0_conf_obj_delete(obj);

	m0_conf_reg_fini(&reg);
}
