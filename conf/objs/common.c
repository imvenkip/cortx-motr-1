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
#include "conf/buf_ext.h" /* m0_buf_is_aimed */

static bool mounted_as(const struct m0_conf_obj *obj, enum m0_conf_objtype type)
{
	return obj->co_mounted && obj->co_type == type;
}

M0_INTERNAL bool parent_check(const struct m0_conf_obj *obj)
{
	enum { _NOT_SURE = -9, _ORPHAN = -1 };
	static const enum m0_conf_objtype expected[M0_CO_NR] = {
		[M0_CO_DIR]        = _NOT_SURE, /* filesystem | node | sdev */
		[M0_CO_PROFILE]    = _ORPHAN,
		[M0_CO_FILESYSTEM] = M0_CO_PROFILE,
		[M0_CO_SERVICE]    = M0_CO_DIR,
		[M0_CO_NODE]       = _ORPHAN,
		[M0_CO_NIC]        = M0_CO_DIR,
		[M0_CO_SDEV]       = M0_CO_DIR,
		[M0_CO_PARTITION]  = M0_CO_DIR
	};
	const struct m0_conf_obj *parent = obj->co_parent;
	enum m0_conf_objtype actual =
		parent == NULL ? _ORPHAN : parent->co_type;

	M0_PRE(obj->co_mounted && obj->co_type != actual);

	return M0_IN(obj->co_type, (M0_CO_PROFILE, M0_CO_NODE)) ?
		parent == NULL :
		parent != NULL && parent->co_mounted &&
		parent->co_status == M0_CS_READY &&
		0 <= actual && actual < M0_CO_NR &&
		actual == expected[obj->co_type] &&
		(obj->co_type == M0_CO_DIR) == M0_IN(actual, (M0_CO_FILESYSTEM,
							      M0_CO_NODE,
							      M0_CO_SDEV)) &&
		ergo(actual == M0_CO_DIR,
		     /* Parent is a directory. Ensure that it may
		      * contain objects of given type. */
		     M0_CONF_CAST(parent, m0_conf_dir)->cd_item_type ==
		     obj->co_type);
}

M0_INTERNAL bool child_check(const struct m0_conf_obj *obj,
			     const struct m0_conf_obj *child,
			     enum m0_conf_objtype child_type)
{
	M0_PRE(obj->co_mounted);

	/* Profile is a topmost object. It cannot be a child. */
	M0_ASSERT(child == NULL || child->co_type != M0_CO_PROFILE);

	return ergo(obj->co_status == M0_CS_READY,
		    mounted_as(child, child_type) &&
		    child->co_parent == (child->co_type == M0_CO_NODE ? NULL :
					 obj));
}

M0_INTERNAL void child_adopt(struct m0_conf_obj *parent,
			     struct m0_conf_obj *child)
{
	/* Profile cannot be a child, because it is the topmost object. */
	M0_PRE(child->co_type != M0_CO_PROFILE);

	M0_ASSERT(equi(child->co_parent == NULL,
		       !child->co_mounted || child->co_type == M0_CO_NODE));
	M0_ASSERT(ergo(child->co_mounted, child->co_parent != NULL ||
		       child->co_type == M0_CO_NODE));

	if (child->co_type == M0_CO_NODE)
		M0_ASSERT(child->co_parent == NULL);
	else
		child->co_parent = parent;

	/* confc is unable to set ->co_confc of directory objects. */
	if (child->co_type == M0_CO_DIR)
		child->co_confc = parent->co_confc;

	child->co_mounted = true;
}

M0_INTERNAL int dir_new(const struct m0_buf *dir_id,
			enum m0_conf_objtype children_type,
			const struct arr_buf *src, struct m0_conf_reg *reg,
			struct m0_conf_dir **out)
{
	struct m0_conf_obj *dir;
	struct m0_conf_obj *child;
	uint32_t            i;
	int                 rc;

	M0_PRE(*out == NULL);

	dir = m0_conf_obj_create(M0_CO_DIR, dir_id);
	if (dir == NULL)
		return -ENOMEM;
	*out = M0_CONF_CAST(dir, m0_conf_dir);

	(*out)->cd_item_type = children_type;

	for (rc = 0, i = 0; i < src->ab_count; ++i) {
		rc = m0_conf_obj_find(reg, children_type, &src->ab_elems[i],
				      &child);
		if (rc != 0)
			break;

		/* Link the directory and its element together. */
		child_adopt(dir, child);
		m0_conf_dir_tlist_add(&(*out)->cd_items, child);
	}

	rc = rc ?: m0_conf_reg_add(reg, dir);

	if (rc == 0) {
		dir->co_status = M0_CS_READY;
	} else {
		/* Restore consistency. */
		m0_tl_for(m0_conf_dir, &(*out)->cd_items, child) {
			m0_conf_reg_del(reg, child);
			m0_conf_obj_delete(child);
		} m0_tl_endfor;
		m0_conf_obj_delete(dir);
	}

	return rc;
}

M0_INTERNAL bool arrays_eq(const char **cached, const struct arr_buf *flat)
{
	uint32_t i;

	M0_PRE(flat->ab_count != 0); /* `flat' is known to be valid */

	for (i = 0; cached[i] != NULL; ++i) {
		if (i >= flat->ab_count || !m0_buf_streq(&flat->ab_elems[i],
							 cached[i]))
			return false;
	}
	return i == flat->ab_count;
}

int strings_copy(const char ***dest, const struct arr_buf *src)
{
	uint32_t i;

	M0_PRE(*dest == NULL);
	M0_PRE(equi(src->ab_count == 0, src->ab_elems == NULL));

	if (src->ab_count == 0)
		return 0; /* there is nothing to copy */

	M0_ALLOC_ARR(*dest, src->ab_count + 1);
	if (*dest == NULL)
		return -ENOMEM;

	for (i = 0; i < src->ab_count; ++i) {
		(*dest)[i] = m0_buf_strdup(&src->ab_elems[i]);
		if ((*dest)[i] == NULL)
			goto fail;
	}
	(*dest)[i] = NULL; /* end of list */

	return 0;
fail:
	for (; i != 0; --i)
		m0_free((void *)(*dest)[i]);
	m0_free(*dest);
	return -ENOMEM;
}

void strings_free(const char **arr)
{
	if (arr != NULL) {
		const char **p;
		for (p = arr; *p != NULL; ++p)
			m0_free((void *)*p);
		m0_free(arr);
	}
}
