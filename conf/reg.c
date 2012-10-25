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
 * Original creation date: 11-Mar-2012
 */

#include "conf/reg.h"
#include "conf/obj_ops.h"   /* c2_conf_obj_delete */
#include "colibri/magic.h"  /* C2_CONF_OBJ_MAGIC, C2_CONF_REG_MAGIC */
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

C2_TL_DESCR_DEFINE(c2_conf_reg, "registered c2_conf_obj-s", ,
		   struct c2_conf_obj, co_reg_link, co_gen_magic,
		   C2_CONF_OBJ_MAGIC, C2_CONF_REG_MAGIC);
C2_TL_DEFINE(c2_conf_reg, , struct c2_conf_obj);

C2_INTERNAL void c2_conf_reg_init(struct c2_conf_reg *reg)
{
	c2_conf_reg_tlist_init(&reg->r_objs);
}

C2_INTERNAL int c2_conf_reg_add(struct c2_conf_reg *reg,
				struct c2_conf_obj *obj)
{
	const struct c2_conf_obj *x;
	C2_PRE(!c2_conf_reg_tlink_is_in(obj));

	x = c2_conf_reg_lookup(reg, obj->co_type, &obj->co_id);
	if (x == NULL) {
		c2_conf_reg_tlist_add(&reg->r_objs, obj);
		return 0;
	}
	return -EEXIST;
}

C2_INTERNAL struct c2_conf_obj *c2_conf_reg_lookup(const struct c2_conf_reg
						   *reg,
						   enum c2_conf_objtype type,
						   const struct c2_buf *id)
{
	struct c2_conf_obj *obj;

	c2_tl_for(c2_conf_reg, &reg->r_objs, obj) {
		if (obj->co_type == type && c2_buf_eq(&obj->co_id, id))
			break;
	} c2_tl_endfor;

	return obj;
}

C2_INTERNAL void c2_conf_reg_del(const struct c2_conf_reg *reg,
				 struct c2_conf_obj *obj)
{
	C2_PRE(c2_conf_reg_tlist_contains(&reg->r_objs, obj));
	c2_conf_reg_tlist_del(obj);
}

C2_INTERNAL void c2_conf_reg_fini(struct c2_conf_reg *reg)
{
	struct c2_conf_obj *obj;

	c2_tl_for(c2_conf_reg, &reg->r_objs, obj) {
		c2_conf_reg_tlist_del(obj);

		/* Don't let concrete invariants check relations. */
		obj->co_mounted = false;

		c2_conf_obj_delete(obj);
	} c2_tl_endfor;

	c2_conf_reg_tlist_fini(&reg->r_objs);
}

/** @} conf_dlspec_reg */
