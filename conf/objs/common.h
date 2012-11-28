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
#pragma once
#ifndef __MERO_CONF_OBJS_COMMON_H__
#define __MERO_CONF_OBJS_COMMON_H__

#include "conf/obj.h"     /* m0_conf_obj, m0_conf_objtype */
#include "conf/obj_ops.h" /* m0_conf_obj_ops */
#include "conf/onwire.h"  /* confx_object */
#include "conf/buf_ext.h"
#include "lib/memory.h"   /* m0_free */
#include "lib/errno.h"    /* ENOMEM, ENOENT */
#include "lib/misc.h"     /* memcpy, memcmp, strlen, M0_IN */

struct m0_conf_reg;
struct m0_buf;

#define MEMBER_PTR(ptr, member)                \
({                                             \
	typeof(ptr) __ptr = (ptr);             \
	__ptr == NULL ? NULL : &__ptr->member; \
})

#define FLAT_OBJ(ptr, abbrev)                                                 \
({                                                                            \
	const struct confx_object     *__ptr = (ptr);                         \
	const struct confx_ ## abbrev *__emb = &__ptr->o_conf.u.u_ ## abbrev; \
	__emb;                                                                \
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

#define M0_CONF__INVARIANT_DEFINE(name, type)                         \
static bool name(const struct m0_conf_obj *obj)                       \
{                                                                     \
	return type ## _bob_check(container_of(obj, struct type,      \
					       type ## _cast_field)); \
}                                                                     \
struct __ ## abbrev ## _semicolon_catcher

M0_INTERNAL bool parent_check(const struct m0_conf_obj *obj);

M0_INTERNAL bool child_check(const struct m0_conf_obj *obj,
			     const struct m0_conf_obj *child,
			     enum m0_conf_objtype child_type);

M0_INTERNAL void child_adopt(struct m0_conf_obj *parent,
			     struct m0_conf_obj *child);

/**
 * Creates new m0_conf_directory and populates it with stubs.
 *
 * @param dir_id         Directory identifier.
 * @param children_type  Type of entries.
 * @param src            Identifiers of the entries.
 * @param reg            Registry of cached objects.
 * @param[out] out       Resulting pointer.
 *
 * dir_new() is transactional: if it fails, the configuration cache
 * (i.e., the DAG of objects and the registry) is left unchanged.
 *
 * XXX @todo UT transactional property of dir_new().
 */
M0_INTERNAL int dir_new(const struct m0_buf *dir_id,
			enum m0_conf_objtype children_type,
			const struct arr_buf *src, struct m0_conf_reg *reg,
			struct m0_conf_dir **out);

M0_INTERNAL bool arrays_eq(const char **cached, const struct arr_buf *flat);
M0_INTERNAL int strings_copy(const char ***dest, const struct arr_buf *src);
M0_INTERNAL void strings_free(const char **arr);

#endif /* __MERO_CONF_OBJS_COMMON_H__ */
