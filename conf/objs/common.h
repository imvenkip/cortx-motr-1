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
#ifndef __COLIBRI_CONF_OBJS_COMMON_H__
#define __COLIBRI_CONF_OBJS_COMMON_H__

#include "conf/obj.h"     /* c2_conf_obj, c2_conf_objtype */
#include "conf/obj_ops.h" /* c2_conf_obj_ops */
#include "conf/onwire.h"  /* confx_object */
#include "conf/buf_ext.h"
#include "lib/memory.h"   /* c2_free */
#include "lib/errno.h"    /* ENOMEM, ENOENT */
#include "lib/misc.h"     /* memcpy, memcmp, strlen, C2_IN */

struct c2_conf_reg;
struct c2_buf;

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

#define C2_CONF__BOB_DEFINE(type, magic, check)                               \
const struct c2_bob_type type ## _bob = {                                     \
	.bt_name         = #type,                                             \
	.bt_magix_offset = C2_MAGIX_OFFSET(struct type,                       \
					   type ## _cast_field.co_con_magic), \
	.bt_magix        = magic,                                             \
	.bt_check        = check                                              \
};                                                                            \
C2_BOB_DEFINE(static, &type ## _bob, type)

#define C2_CONF__INVARIANT_DEFINE(name, type)                         \
static bool name(const struct c2_conf_obj *obj)                       \
{                                                                     \
	return type ## _bob_check(container_of(obj, struct type,      \
					       type ## _cast_field)); \
}                                                                     \
struct __ ## abbrev ## _semicolon_catcher

C2_INTERNAL bool obj_is_stub(const struct c2_conf_obj *obj);

C2_INTERNAL bool parent_check(const struct c2_conf_obj *obj);

C2_INTERNAL bool child_check(const struct c2_conf_obj *obj,
			     const struct c2_conf_obj *child,
			     enum c2_conf_objtype child_type);

C2_INTERNAL void child_adopt(struct c2_conf_obj *parent,
			     struct c2_conf_obj *child);

/**
 * Creates new c2_conf_directory and populates it with stubs.
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
C2_INTERNAL int dir_new(const struct c2_buf *dir_id,
			enum c2_conf_objtype children_type,
			const struct arr_buf *src, struct c2_conf_reg *reg,
			struct c2_conf_dir **out);

C2_INTERNAL bool arrays_eq(const char **cached, const struct arr_buf *flat);

#endif /* __COLIBRI_CONF_OBJS_COMMON_H__ */
