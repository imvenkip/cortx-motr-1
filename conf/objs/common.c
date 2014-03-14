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
 * Original creation date: 30-Aug-2012
 */

#include "conf/objs/common.h"
#include "conf/cache.h"

static bool mounted_as(const struct m0_conf_obj *obj,
		       const struct m0_conf_obj_type *type)
{
	return m0_conf_obj_type(obj) == type;
}

M0_INTERNAL bool child_check(const struct m0_conf_obj *obj,
			     const struct m0_conf_obj *child,
			     const struct m0_conf_obj_type *child_type)
{
	/* Profile is a topmost object, it cannot be a child. */
	M0_ASSERT(child == NULL ||
		  m0_conf_obj_type(child) != &M0_CONF_PROFILE_TYPE);

	return ergo(obj->co_status == M0_CS_READY,
		    _0C(mounted_as(child, child_type)) &&
		    _0C(child->co_parent ==
			(m0_conf_obj_type(child) == &M0_CONF_NODE_TYPE ?
			 NULL : obj)));
}

M0_INTERNAL void
child_adopt(struct m0_conf_obj *parent, struct m0_conf_obj *child)
{
	/* Profile cannot be a child, because it is the topmost object. */
	M0_PRE(m0_conf_obj_type(child) != &M0_CONF_PROFILE_TYPE);
	M0_ASSERT(child->co_cache == parent->co_cache);

	if (m0_conf_obj_type(child) == &M0_CONF_NODE_TYPE)
		M0_ASSERT(child->co_parent == NULL);
	else
		child->co_parent = parent;
}

/**
 * Constructs directory identifier.
 *
 * @todo This would produce non-unique identifier, if an object has two
 * different directories with the same children types. Perhaps relation fid
 * should be factored in somehow.
 */
static void dir_id_build(struct m0_fid *out, const struct m0_fid *id,
			 const struct m0_conf_obj_type *children_type)
{
	m0_fid_tset(out, M0_CONF_DIR_TYPE.cot_ftype.ft_id,
		    id->f_container, id->f_key);
	/* clear the next 16 bits after fid type... */
	out->f_container &= ~0x00ffff00000000ULL;
	/* ... place parent type there... */
	out->f_container |= ((uint64_t)m0_fid_type_getfid(id)->ft_id) << 48;
	/* ... and place parent type there. */
	out->f_container |= ((uint64_t)children_type->cot_ftype.ft_id) << 40;
}

M0_INTERNAL int dir_new(struct m0_conf_cache *cache,
			const struct m0_fid *id,
			const struct m0_fid *relfid,
			const struct m0_conf_obj_type *children_type,
			const struct arr_fid *src, struct m0_conf_dir **out)
{
	struct m0_conf_obj *child;
	uint32_t            i;
	int                 rc;
	struct m0_conf_obj *dir;
	struct m0_fid       dir_id;

	M0_PRE(m0_fid_type_getfid(relfid) == &M0_CONF_RELFID_TYPE);
	M0_PRE(*out == NULL);

	dir_id_build(&dir_id, id, children_type);
	M0_ASSERT(m0_conf_cache_lookup(cache, &dir_id) == NULL);

	dir = m0_conf_obj_create(cache, &dir_id);
	if (dir == NULL)
		return -ENOMEM;
	*out = M0_CONF_CAST(dir, m0_conf_dir);

	(*out)->cd_item_type = children_type;
	(*out)->cd_relfid    = *relfid;

	for (rc = 0, i = 0; i < src->af_count; ++i) {
		child = m0_conf_cache_lookup(cache, &src->af_elems[i]);
		if (child != NULL) {
			rc = -EEXIST; /* ban duplicates */
			break;
		}

		rc = m0_conf_obj_find(cache, &src->af_elems[i], &child);
		if (rc != 0)
			break;

		/* Link the directory and its element together. */
		child_adopt(dir, child);
		m0_conf_dir_tlist_add_tail(&(*out)->cd_items, child);
	}

	rc = rc ?: m0_conf_cache_add(cache, dir);

	if (rc == 0) {
		dir->co_status = M0_CS_READY;
	} else {
		/* Restore consistency. */
		m0_tl_teardown(m0_conf_dir, &(*out)->cd_items, child) {
			m0_conf_cache_del(cache, child);
		}
		m0_conf_obj_delete(dir);
		*out = NULL;
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

M0_INTERNAL int
strings_from_arrbuf(const char ***dest, const struct arr_buf *src)
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

M0_INTERNAL void strings_free(const char **arr)
{
	if (arr != NULL) {
		const char **p;
		for (p = arr; *p != NULL; ++p)
			m0_free((void *)*p);
		m0_free(arr);
	}
}

M0_INTERNAL int arrbuf_from_strings(struct arr_buf *dest, const char **src)
{
	size_t i;
	int    rc;

	M0_SET0(dest);

	if (src == NULL)
		return 0;

	for (; src[dest->ab_count] != NULL; ++dest->ab_count)
		; /* measuring */

	if (dest->ab_count == 0)
		return 0;

	M0_ALLOC_ARR(dest->ab_elems, dest->ab_count);
	if (dest->ab_elems == NULL)
		return -ENOMEM;

	for (i = 0; i < dest->ab_count; ++i) {
		rc = m0_buf_copy(&dest->ab_elems[i],
				 &M0_BUF_INITS((char *)src[i]));
		if (rc != 0) {
			arrbuf_free(dest);
			return -ENOMEM;
		}
	}

	return 0;
}

M0_INTERNAL int arrfid_from_dir(struct arr_fid *dest,
				const struct m0_conf_dir *dir)
{
	struct m0_conf_obj *obj;
	size_t              i;

	dest->af_elems = NULL;
	dest->af_count = m0_conf_dir_tlist_length(&dir->cd_items);

	if (dest->af_count == 0)
		return 0;

	M0_ALLOC_ARR(dest->af_elems, dest->af_count);
	if (dest->af_elems == NULL)
		return -ENOMEM;

	i = 0;
	m0_tl_for(m0_conf_dir, &dir->cd_items, obj) {
		dest->af_elems[i++] = obj->co_id;
	} m0_tl_endfor;

	return 0;
}

M0_INTERNAL void arrbuf_free(struct arr_buf *arr)
{
	while (arr->ab_count > 0)
		m0_buf_free(&arr->ab_elems[--arr->ab_count]);
	m0_free0(&arr->ab_elems);
	M0_POST(arr->ab_count == 0);
}

M0_INTERNAL void arrfid_free(struct arr_fid *arr)
{
	m0_free0(&arr->af_elems);
}

M0_INTERNAL void confx_encode(struct m0_confx_obj *dest,
			      const struct m0_conf_obj *src)
{
	dest->xo_u.u_header.ch_id = src->co_id;
	dest->xo_type = m0_conf_obj_type(src)->cot_ftype.ft_id;
}
