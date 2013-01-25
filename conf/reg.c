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
 * Original creation date: 11-Mar-2012
 */

#include "conf/reg.h"
#include "conf/obj_ops.h"   /* m0_conf_obj_delete */
#include "mero/magic.h"  /* M0_CONF_OBJ_MAGIC, M0_CONF_REG_MAGIC */
#include "lib/errno.h"      /* EEXIST */

/**
 * @defgroup conf_dlspec_reg Registry of Cached Configuration Objects
 *
 * The implementation of a registry is based on linked list data structure.
 * Rationale: a registry is not expected to be queried frequently.
 *
 * @see @ref conf, @ref conf-lspec
 *
 * @{
 */

M0_TL_DESCR_DEFINE(m0_conf_reg, "registered m0_conf_obj-s", M0_INTERNAL,
		   struct m0_conf_obj, co_reg_link, co_gen_magic,
		   M0_CONF_OBJ_MAGIC, M0_CONF_REG_MAGIC);
M0_TL_DEFINE(m0_conf_reg, M0_INTERNAL, struct m0_conf_obj);

M0_INTERNAL void m0_conf_reg_init(struct m0_conf_reg *reg)
{
	m0_conf_reg_tlist_init(&reg->r_objs);
}

M0_INTERNAL int m0_conf_reg_add(struct m0_conf_reg *reg,
				struct m0_conf_obj *obj)
{
	const struct m0_conf_obj *x;
	M0_PRE(!m0_conf_reg_tlink_is_in(obj));

	x = m0_conf_reg_lookup(reg, obj->co_type, &obj->co_id);
	if (x == NULL) {
		m0_conf_reg_tlist_add(&reg->r_objs, obj);
		return 0;
	}
	return -EEXIST;
}

M0_INTERNAL struct m0_conf_obj *m0_conf_reg_lookup(const struct m0_conf_reg
						   *reg,
						   enum m0_conf_objtype type,
						   const struct m0_buf *id)
{
	struct m0_conf_obj *obj;

	m0_tl_for(m0_conf_reg, &reg->r_objs, obj) {
		if (obj->co_type == type && m0_buf_eq(&obj->co_id, id))
			break;
	} m0_tl_endfor;

	return obj;
}

M0_INTERNAL void m0_conf_reg_del(const struct m0_conf_reg *reg,
				 struct m0_conf_obj *obj)
{
	M0_PRE(m0_conf_reg_tlist_contains(&reg->r_objs, obj));
	m0_conf_reg_tlist_del(obj);
}

M0_INTERNAL void m0_conf_reg_fini(struct m0_conf_reg *reg)
{
	struct m0_conf_obj *obj;

	m0_tl_for(m0_conf_reg, &reg->r_objs, obj) {
		m0_conf_reg_tlist_del(obj);

		/* Don't let concrete invariants check relations. */
		obj->co_mounted = false;

		m0_conf_obj_delete(obj);
	} m0_tl_endfor;

	m0_conf_reg_tlist_fini(&reg->r_objs);
}

/** @} conf_dlspec_reg */
