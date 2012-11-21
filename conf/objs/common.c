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
 * Original creation date: 30-Aug-2012
 */

#include "conf/objs/common.h"
#include "conf/reg.h"
#include "conf/buf_ext.h" /* c2_buf_is_aimed */

C2_INTERNAL bool obj_is_stub(const struct c2_conf_obj *obj)
{
	return obj->co_status != C2_CS_READY;
}

static bool mounted_as(const struct c2_conf_obj *obj, enum c2_conf_objtype type)
{
	return obj->co_mounted && obj->co_type == type;
}

C2_INTERNAL bool parent_check(const struct c2_conf_obj *obj)
{
	enum { _NOT_SURE = -9, _ORPHAN = -1 };
	static const enum c2_conf_objtype expected[C2_CO_NR] = {
		[C2_CO_DIR]        = _NOT_SURE, /* filesystem | node | sdev */
		[C2_CO_PROFILE]    = _ORPHAN,
		[C2_CO_FILESYSTEM] = C2_CO_PROFILE,
		[C2_CO_SERVICE]    = C2_CO_DIR,
		[C2_CO_NODE]       = _ORPHAN,
		[C2_CO_NIC]        = C2_CO_DIR,
		[C2_CO_SDEV]       = C2_CO_DIR,
		[C2_CO_PARTITION]  = C2_CO_DIR
	};
	const struct c2_conf_obj *parent = obj->co_parent;
	enum c2_conf_objtype actual =
		parent == NULL ? _ORPHAN : parent->co_type;

	C2_PRE(obj->co_mounted && obj->co_type != actual);

	return C2_IN(obj->co_type, (C2_CO_PROFILE, C2_CO_NODE)) ?
		parent == NULL :
		parent != NULL && parent->co_mounted &&
		parent->co_status == C2_CS_READY &&
		0 <= actual && actual < C2_CO_NR &&
		actual == expected[obj->co_type] &&
		(obj->co_type == C2_CO_DIR) == C2_IN(actual, (C2_CO_FILESYSTEM,
							      C2_CO_NODE,
							      C2_CO_SDEV)) &&
		ergo(actual == C2_CO_DIR,
		     /* Parent is a directory. Ensure that it may
		      * contain objects of given type. */
		     C2_CONF_CAST(parent, c2_conf_dir)->cd_item_type ==
		     obj->co_type);
}

C2_INTERNAL bool child_check(const struct c2_conf_obj *obj,
			     const struct c2_conf_obj *child,
			     enum c2_conf_objtype child_type)
{
	C2_PRE(obj->co_mounted);

	/* Profile is a topmost object. It cannot be a child. */
	C2_ASSERT(child == NULL || child->co_type != C2_CO_PROFILE);

	return ergo(obj->co_status == C2_CS_READY,
		    mounted_as(child, child_type) &&
		    child->co_parent == (child->co_type == C2_CO_NODE ? NULL :
					 obj));
}

C2_INTERNAL void child_adopt(struct c2_conf_obj *parent,
			     struct c2_conf_obj *child)
{
	/* Profile cannot be a child, because it is a topmost object. */
	C2_PRE(child->co_type != C2_CO_PROFILE);

	C2_ASSERT(equi(child->co_parent == NULL,
		       !child->co_mounted || child->co_type == C2_CO_NODE));
	C2_ASSERT(ergo(child->co_mounted, child->co_parent != NULL ||
		       child->co_type == C2_CO_NODE));

	if (child->co_type == C2_CO_NODE)
		C2_ASSERT(child->co_parent == NULL);
	else
		child->co_parent = parent;

	child->co_mounted = true;
}

C2_INTERNAL int dir_new(const struct c2_buf *dir_id,
			enum c2_conf_objtype children_type,
			const struct arr_buf *src, struct c2_conf_reg *reg,
			struct c2_conf_dir **out)
{
	struct c2_conf_obj *dir;
	struct c2_conf_obj *child;
	uint32_t            i;
	int                 rc;

	C2_PRE(*out == NULL);

	dir = c2_conf_obj_create(C2_CO_DIR, dir_id);
	if (dir == NULL)
		return -ENOMEM;
	*out = C2_CONF_CAST(dir, c2_conf_dir);

	(*out)->cd_item_type = children_type;

	for (rc = 0, i = 0; i < src->ab_count; ++i) {
		rc = c2_conf_obj_find(reg, children_type, &src->ab_elems[i],
				      &child);
		if (rc != 0)
			break;

		/* Link the directory and its element together. */
		child_adopt(dir, child);
		c2_conf_dir_tlist_add(&(*out)->cd_items, child);
	}

	rc = rc ?: c2_conf_reg_add(reg, dir);

	if (rc == 0) {
		dir->co_status = C2_CS_READY;
	} else {
		/* Restore consistency. */
		c2_tl_for(c2_conf_dir, &(*out)->cd_items, child) {
			c2_conf_reg_del(reg, child);
			c2_conf_obj_delete(child);
		} c2_tl_endfor;
		c2_conf_obj_delete(dir);
	}

	return rc;
}

C2_INTERNAL bool arrays_eq(const char **cached, const struct arr_buf *flat)
{
	uint32_t i;

	C2_PRE(flat->ab_count != 0); /* `flat' is known to be valid */

	for (i = 0; cached[i] != NULL; ++i) {
		if (i >= flat->ab_count || !c2_buf_streq(&flat->ab_elems[i],
							 cached[i]))
			return false;
	}
	return i == flat->ab_count;
}
