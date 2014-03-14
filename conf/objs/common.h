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
#include "conf/onwire.h"  /* m0_confx_obj, arr_buf */
#include "lib/memory.h"   /* m0_free */
#include "lib/errno.h"    /* ENOMEM, ENOENT */
#include "lib/misc.h"     /* M0_IN */

#define FLAT_OBJ(ptr, abbrev)                            \
({                                                       \
	const struct m0_confx_obj        *__ptr = (ptr); \
	const struct m0_confx_ ## abbrev *__emb =        \
		&__ptr->o_conf.u.u_ ## abbrev;           \
	__emb;                                           \
})

#define M0_CONF__BOB_DEFINE(type, magic, check)                               \
const struct m0_bob_type type ## _bob = {                                     \
	.bt_name         = #type,                                             \
	.bt_magix_offset = M0_MAGIX_OFFSET(struct type,                       \
					   type ## _cast_field.co_con_magic), \
	.bt_magix        = magic,                                             \
	.bt_check        = check                                              \
};                                                                            \
M0_BOB_DEFINE(static, &type ## _bob, type)

#define M0_CONF__INVARIANT_DEFINE(name, type)				\
static bool name(const struct m0_conf_obj *obj)				\
{									\
	return type ## _bob_check(container_of(obj, struct type,	\
				    type ## _cast_field));		\
}									\
struct __ ## abbrev ## _semicolon_catcher

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

M0_INTERNAL bool arrays_eq(const char **cached, const struct arr_buf *flat);

M0_INTERNAL int strings_from_arrbuf(const char ***dest,
				    const struct arr_buf *src);
M0_INTERNAL void strings_free(const char **arr);

M0_INTERNAL int arrbuf_from_strings(struct arr_buf *dest, const char **src);
M0_INTERNAL int arrfid_from_dir(struct arr_fid *dest,
				const struct m0_conf_dir *dir);
M0_INTERNAL void arrbuf_free(struct arr_buf *arr);
M0_INTERNAL void arrfid_free(struct arr_fid *arr);

M0_INTERNAL void confx_encode(struct m0_confx_obj *dest,
			      const struct m0_conf_obj *src);

#endif /* __MERO_CONF_OBJS_COMMON_H__ */
