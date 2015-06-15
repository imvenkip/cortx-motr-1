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
#pragma once
#ifndef __MERO_CONF_OBJS_COMMON_H__
#define __MERO_CONF_OBJS_COMMON_H__

#include "conf/obj.h"     /* m0_conf_obj */
#include "conf/obj_ops.h" /* m0_conf_obj_ops */
#include "conf/onwire.h"  /* m0_confx_obj */
#include "lib/memory.h"   /* m0_free */
#include "lib/errno.h"    /* ENOMEM, ENOENT */
#include "lib/misc.h"     /* M0_IN */

#define M0_CONF__BOB_DEFINE(type, magic, check)                               \
const struct m0_bob_type type ## _bob = {                                     \
	.bt_name         = #type,                                             \
	.bt_magix_offset = M0_MAGIX_OFFSET(struct type,                       \
					   type ## _cast_field.co_con_magic), \
	.bt_magix        = magic,                                             \
	.bt_check        = check                                              \
};                                                                            \
M0_BOB_DEFINE(static, &type ## _bob, type)

#define M0_CONF__INVARIANT_DEFINE(name, type)                    \
static bool name(const struct m0_conf_obj *obj)                  \
{                                                                \
	return type ## _bob_check(container_of(obj, struct type, \
				    type ## _cast_field));       \
}                                                                \
struct __ ## type ## _semicolon_catcher

#define M0_CONF__CTOR_DEFINE(name, type, ops, ha_cb) \
static struct m0_conf_obj *name(void)         \
{                                             \
	struct type        *x;                \
	struct m0_conf_obj *ret;              \
					      \
	M0_ALLOC_PTR(x);                      \
	if (x == NULL)                        \
		return NULL;                  \
					      \
	type ## _bob_init(x);                 \
	ret = &x->type ## _cast_field;        \
	ret->co_ops = ops;                    \
	ret->co_ha_callback = ha_cb;          \
	return ret;                           \
}                                             \
struct __ ## type ## _semicolon_catcher

M0_INTERNAL void child_adopt(struct m0_conf_obj *parent,
			     struct m0_conf_obj *child);

/**
 * Creates new m0_conf_directory and populates it with stubs.
 *
 * @param cache          Configuration cache.
 * @param dir_id         Directory identifier.
 * @param children_type  Type of entries.
 * @param src            Identifiers of the entries.
 * @param[out] out       Resulting pointer.
 *
 * dir_new() is transactional: if it fails, the configuration cache
 * (i.e., the DAG of objects and the registry) is left unchanged.
 *
 * XXX @todo UT transactional property of dir_new().
 */
M0_INTERNAL int dir_new(struct m0_conf_cache *cache,
			const struct m0_fid *dir_id,
			const struct m0_fid *relfid,
			const struct m0_conf_obj_type *children_type,
			const struct arr_fid *src,
			struct m0_conf_dir **out);

struct conf_dir_entries {
	const struct m0_fid           *de_relfid;
	const struct m0_conf_obj_type *de_entry_type;
	const struct arr_fid          *de_entries;
};
#define CONF_DIR_ENTRIES(relfid, entry_type, entries) \
	((struct conf_dir_entries){ (relfid), (entry_type), (entries) })

M0_INTERNAL int dir_create_and_populate(struct m0_conf_dir **result,
					const struct conf_dir_entries *de,
					struct m0_conf_obj *dir_parent,
					struct m0_conf_cache *cache);

struct conf_dir_encoding_pair {
	const struct m0_conf_dir *dep_src;
	struct arr_fid           *dep_dest;
};

M0_INTERNAL int conf_dirs_encode(const struct conf_dir_encoding_pair *how,
				 size_t how_nr);

struct conf_dir_relation {
	struct m0_conf_dir  *dr_dir;
	const struct m0_fid *dr_relfid;
};

M0_INTERNAL int conf_dirs_lookup(struct m0_conf_obj            **out,
				 const struct m0_fid            *name,
				 const struct conf_dir_relation *rels,
				 size_t                          nr_rels);

M0_INTERNAL bool arrays_eq(const char **cached, const struct m0_bufs *flat);

M0_INTERNAL int arrfid_from_dir(struct arr_fid *dest,
				const struct m0_conf_dir *dir);
M0_INTERNAL void arrfid_free(struct arr_fid *arr);

M0_INTERNAL void confx_encode(struct m0_confx_obj *dest,
			      const struct m0_conf_obj *src);

M0_INTERNAL int u32arr_decode(const struct arr_u32 *src, uint32_t **dest);
M0_INTERNAL int u32arr_encode(struct arr_u32 *dest, const uint32_t *src,
			      uint32_t src_nr);
M0_INTERNAL bool u32arr_cmp(const struct arr_u32 *a1, const uint32_t *a2,
			    uint32_t a2_nr);
M0_INTERNAL void u32arr_free(struct arr_u32 *arr);

M0_INTERNAL int conf_pvers_decode(struct m0_conf_cache  *cache,
				  const struct arr_fid  *src,
				  struct m0_conf_pver  ***pvers,
				  int                   *nr);

M0_INTERNAL int conf_pvers_encode(struct m0_conf_pver **pvers,
				  int                   nr,
				  struct arr_fid       *dest);

#endif /* __MERO_CONF_OBJS_COMMON_H__ */
