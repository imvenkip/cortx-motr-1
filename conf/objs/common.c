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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/cache.h"
#include "conf/objs/common.h"
#include "conf/cache.h"

#define X_CONF(name, key)                              \
	const struct m0_fid M0_CONF_ ## name ## _FID = \
		M0_FID_TINIT('/', 0, (key))
M0_CONF_REL_FIDS;
#undef X_CONF

M0_INTERNAL void
child_adopt(struct m0_conf_obj *parent, struct m0_conf_obj *child)
{
	/* Root cannot be a child, because it is the topmost object. */
	M0_PRE(m0_conf_obj_type(child) != &M0_CONF_ROOT_TYPE);
	M0_PRE(child->co_cache == parent->co_cache);

	if (child->co_parent != child)
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
	*out = *id;
	m0_fid_tassume(out, &M0_CONF_DIR_TYPE.cot_ftype);
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
		return M0_ERR(-ENOMEM);
	*out = M0_CONF_CAST(dir, m0_conf_dir);

	(*out)->cd_item_type = children_type;
	(*out)->cd_relfid    = *relfid;

	for (rc = 0, i = 0; i < src->af_count; ++i) {
		rc = m0_conf_obj_find(cache, &src->af_elems[i], &child);
		if (rc != 0)
			break;

		if (m0_conf_dir_tlist_contains(&(*out)->cd_items, child)) {
			rc = -EEXIST; /* ban duplicates */
			break;
		}

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
	return M0_RC(rc);
}

M0_INTERNAL int dir_create_and_populate(struct m0_conf_dir **result,
					const struct conf_dir_entries *de,
					struct m0_conf_obj *dir_parent,
					struct m0_conf_cache *cache)
{
	int rc;

	rc = dir_new(cache, &dir_parent->co_id, de->de_relfid,
		     de->de_entry_type, de->de_entries, result);
	if (rc != 0)
		return M0_ERR(rc);
	child_adopt(dir_parent, &(*result)->cd_obj);
	return 0;
}

M0_INTERNAL int
conf_dirs_encode(const struct conf_dir_encoding_pair *how, size_t how_nr)
{
	const struct conf_dir_encoding_pair *p;
	size_t                               i;
	int                                  rc = 0;

	for (i = 0; i < how_nr; ++i) {
		p = &how[i];
		M0_ASSERT(_0C(p->dep_dest->af_count == 0) &&
			  _0C(p->dep_dest->af_elems == NULL));
		rc = arrfid_from_dir(p->dep_dest, p->dep_src);
		if (rc != 0)
			break;
	}
	if (rc != 0) {
		while (i > 0)
			arrfid_free(how[--i].dep_dest);
	}
	return M0_RC(rc);
}

M0_INTERNAL int conf_dirs_lookup(struct m0_conf_obj            **out,
				 const struct m0_fid            *name,
				 const struct conf_dir_relation *rels,
				 size_t                          nr_rels)
{
	size_t i;

	for (i = 0; i < nr_rels; ++i) {
		if (m0_fid_eq(rels[i].dr_relfid, name)) {
			*out = &rels[i].dr_dir->cd_obj;
			M0_POST(m0_conf_obj_invariant(*out));
			return 0;
		}
	}
	return -ENOENT;
}

M0_INTERNAL void strings_free(const char **arr)
{
	if (arr != NULL) {
		const char **p;
		for (p = arr; *p != NULL; ++p)
			m0_free((void *)*p);
		m0_free0(&arr);
	}
}

M0_INTERNAL int
arrfid_from_dir(struct arr_fid *dest, const struct m0_conf_dir *dir)
{
	struct m0_conf_obj *obj;
	size_t              i;

	dest->af_elems = NULL;
	dest->af_count = m0_conf_dir_tlist_length(&dir->cd_items);

	if (dest->af_count == 0)
		return 0;

	M0_ALLOC_ARR(dest->af_elems, dest->af_count);
	if (dest->af_elems == NULL)
		return M0_ERR(-ENOMEM);

	i = 0;
	m0_tl_for(m0_conf_dir, &dir->cd_items, obj) {
		dest->af_elems[i++] = obj->co_id;
	} m0_tl_endfor;

	return 0;
}

M0_INTERNAL void arrfid_free(struct arr_fid *arr)
{
	m0_free0(&arr->af_elems);
}

M0_INTERNAL void
confx_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	dest->xo_u.u_header.ch_id = src->co_id;
	dest->xo_type = m0_conf_obj_type(src)->cot_ftype.ft_id;
}

M0_INTERNAL int u32arr_decode(const struct arr_u32 *src, uint32_t **dest)
{
	M0_PRE(src->au_count != 0 && src->au_elems != NULL);

	M0_ALLOC_ARR(*dest, src->au_count);
	if (*dest == NULL)
		return M0_ERR(-ENOMEM);

	memcpy(*dest, src->au_elems, src->au_count * sizeof *dest[0]);
	return 0;
}

M0_INTERNAL int
u32arr_encode(struct arr_u32 *dest, const uint32_t *src, uint32_t src_nr)
{
	M0_PRE((src == NULL) == (src_nr == 0));

	if (src != NULL) {
		M0_ALLOC_ARR(dest->au_elems, src_nr);
		if (dest->au_elems == NULL)
			return M0_ERR(-ENOMEM);
		dest->au_count = src_nr;
		memcpy(dest->au_elems, src,
		       dest->au_count * sizeof dest->au_elems[0]);
	}
	return 0;
}

M0_INTERNAL bool
u32arr_cmp(const struct arr_u32 *a1, const uint32_t *a2, uint32_t a2_nr)
{
	return a1->au_count == a2_nr &&
		m0_forall(i, a2_nr, a1->au_elems[i] == a2[i]);
}

M0_INTERNAL void u32arr_free(struct arr_u32 *arr)
{
	m0_free0(&arr->au_elems);
	arr->au_count = 0;
}

#undef M0_TRACE_SUBSYSTEM
