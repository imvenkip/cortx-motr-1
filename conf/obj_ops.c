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
 * Original creation date: 09-Feb-2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/obj_ops.h"
#include "conf/onwire.h"    /* confx_object */
#include "conf/confc.h"     /* c2_confc */
#include "conf/buf_ext.h"   /* c2_buf_is_aimed */
#include "lib/cdefs.h"      /* IS_IN_ARRAY */
#include "lib/misc.h"       /* C2_IN */
#include "lib/arith.h"      /* C2_CNT_INC, C2_CNT_DEC */
#include "lib/errno.h"      /* ENOMEM */
#include "colibri/magic.h"  /* C2_CONF_OBJ_MAGIC */

/**
 * @defgroup conf_dlspec_objops Configuration Object Operations
 *
 * @see @ref conf, @ref conf-lspec
 *
 * @{
 */

static bool _generic_obj_invariant(const void *bob);
static bool _concrete_obj_invariant(const struct c2_conf_obj *obj);

static const struct c2_bob_type generic_obj_bob = {
	.bt_name         = "c2_conf_obj",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct c2_conf_obj, co_gen_magic),
	.bt_magix        = C2_CONF_OBJ_MAGIC,
	.bt_check        = _generic_obj_invariant
};
C2_BOB_DEFINE(static, &generic_obj_bob, c2_conf_obj);

C2_INTERNAL bool c2_conf_obj_invariant(const struct c2_conf_obj *obj)
{
	return c2_conf_obj_bob_check(obj) && _concrete_obj_invariant(obj);
}

static bool __obj_is_stub(const struct c2_conf_obj *obj)
{
	return obj->co_status != C2_CS_READY;
}

static bool _generic_obj_invariant(const void *bob)
{
	const struct c2_conf_obj *obj = bob;

	return 0 <= obj->co_type && obj->co_type < C2_CO_NR &&
		c2_buf_is_aimed(&obj->co_id) && obj->co_ops != NULL &&
		C2_IN(obj->co_status,
		      (C2_CS_MISSING, C2_CS_LOADING, C2_CS_READY)) &&
		ergo(__obj_is_stub(obj), obj->co_nrefs == 0);
}

static bool _concrete_obj_invariant(const struct c2_conf_obj *obj)
{
	bool ret = obj->co_ops->coo_invariant(obj);
	if (unlikely(!ret))
		C2_LOG(C2_ERROR, "Configuration object invariant doesn't hold: "
		       "type=%d, id={%lu, \"%s\"}", obj->co_type,
		       (unsigned long)obj->co_id.b_nob,
		       (const char *)obj->co_id.b_addr);
	return ret;
}

C2_INTERNAL struct c2_conf_obj *c2_conf__dir_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__profile_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__filesystem_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__service_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__node_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__nic_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__sdev_create(void);
C2_INTERNAL struct c2_conf_obj *c2_conf__partition_create(void);

static struct c2_conf_obj *(*concrete_ctors[C2_CO_NR])(void) = {
	[C2_CO_DIR]        = c2_conf__dir_create,
	[C2_CO_PROFILE]    = c2_conf__profile_create,
	[C2_CO_FILESYSTEM] = c2_conf__filesystem_create,
	[C2_CO_SERVICE]    = c2_conf__service_create,
	[C2_CO_NODE]       = c2_conf__node_create,
	[C2_CO_NIC]        = c2_conf__nic_create,
	[C2_CO_SDEV]       = c2_conf__sdev_create,
	[C2_CO_PARTITION]  = c2_conf__partition_create
};

C2_INTERNAL struct c2_conf_obj *c2_conf_obj_create(enum c2_conf_objtype type,
						   const struct c2_buf *id)
{
	struct c2_conf_obj *obj;
	int                 rc;

	C2_PRE(IS_IN_ARRAY(type, concrete_ctors));

	/* Allocate concrete object; initialise concrete fields. */
	obj = concrete_ctors[type]();
	if (obj == NULL)
		return NULL;

	/* Initialise generic fields. */
	rc = c2_buf_copy(&obj->co_id, id);
	if (rc != 0) {
		obj->co_ops->coo_delete(obj);
		return NULL;
	}
	obj->co_type = type;
	obj->co_status = C2_CS_MISSING;
	c2_chan_init(&obj->co_chan);
	c2_conf_reg_tlink_init(obj);
	c2_conf_dir_tlink_init(obj);
	c2_conf_obj_bob_init(obj);
	C2_ASSERT(obj->co_gen_magic == C2_CONF_OBJ_MAGIC);
	C2_ASSERT(obj->co_con_magic != 0);
	C2_ASSERT(!obj->co_mounted);

	C2_POST(c2_conf_obj_invariant(obj));
	return obj;
}

static int _stub_create(struct c2_conf_reg *reg, enum c2_conf_objtype type,
			const struct c2_buf *id, struct c2_conf_obj **out)
{
	int rc;

	*out = c2_conf_obj_create(type, id);
	if (*out == NULL)
		return -ENOMEM;

	rc = c2_conf_reg_add(reg, *out);
	if (rc != 0) {
		c2_conf_obj_delete(*out);
		*out = NULL;
	}
	return rc;
}

C2_INTERNAL int c2_conf_obj_find(struct c2_conf_reg *reg,
				 enum c2_conf_objtype type,
				 const struct c2_buf *id,
				 struct c2_conf_obj **out)
{
	*out = c2_conf_reg_lookup(reg, type, id);
	return *out == NULL ? _stub_create(reg, type, id, out) : 0;
}

static bool confc_is_unset_or_locked(const struct c2_conf_obj *obj)
{
	return obj->co_confc == NULL ||
		c2_mutex_is_locked(&obj->co_confc->cc_lock);
}

C2_INTERNAL void c2_conf_obj_delete(struct c2_conf_obj *obj)
{
	C2_PRE(c2_conf_obj_invariant(obj));
	C2_PRE(obj->co_nrefs == 0 && obj->co_status != C2_CS_LOADING);
	C2_PRE(!obj->co_mounted || confc_is_unset_or_locked(obj));

	/* Finalise generic fields. */
	c2_conf_obj_bob_fini(obj);
	c2_conf_dir_tlink_fini(obj);
	c2_conf_reg_tlink_fini(obj);
	c2_chan_fini(&obj->co_chan);
	c2_buf_free(&obj->co_id);

	/* Finalise concrete fields; free the object. */
	obj->co_ops->coo_delete(obj);
}

C2_INTERNAL void c2_conf_obj_get(struct c2_conf_obj *obj)
{
	C2_PRE(c2_conf_obj_invariant(obj));
	C2_PRE(obj->co_status == C2_CS_READY);
	C2_PRE(confc_is_unset_or_locked(obj));

	C2_CNT_INC(obj->co_nrefs);
}

C2_INTERNAL void c2_conf_obj_put(struct c2_conf_obj *obj)
{
	C2_PRE(c2_conf_obj_invariant(obj));
	C2_PRE(obj->co_status == C2_CS_READY);
	C2_PRE(confc_is_unset_or_locked(obj));

	C2_CNT_DEC(obj->co_nrefs);
	if (obj->co_nrefs == 0)
		c2_chan_broadcast(&obj->co_chan);
}

static bool confx_object_is_valid(const struct confx_object *src);

C2_INTERNAL int c2_conf_obj_fill(struct c2_conf_obj *dest,
				 const struct confx_object *src,
				 struct c2_conf_reg *reg)
{
	int rc;

	C2_PRE(c2_conf_obj_invariant(dest));
	C2_PRE(confc_is_unset_or_locked(dest));
	C2_PRE(__obj_is_stub(dest) && dest->co_nrefs == 0);
	C2_PRE(dest->co_type == src->o_conf.u_type);
	C2_PRE(c2_buf_eq(&dest->co_id, &src->o_id));
	C2_PRE(confx_object_is_valid(src));

	rc = dest->co_ops->coo_fill(dest, src, reg);
	dest->co_status = rc == 0 ? C2_CS_READY : C2_CS_MISSING;

	C2_POST(ergo(rc == 0, dest->co_mounted));
	C2_POST(confc_is_unset_or_locked(dest));
	C2_POST(c2_conf_obj_invariant(dest));
	return rc;
}

C2_INTERNAL bool c2_conf_obj_match(const struct c2_conf_obj *cached,
				   const struct confx_object *flat)
{
	C2_PRE(c2_conf_obj_invariant(cached));
	C2_PRE(confx_object_is_valid(flat));

	return cached->co_type == flat->o_conf.u_type &&
		c2_buf_eq(&cached->co_id, &flat->o_id) &&
		(__obj_is_stub(cached) ||
		 cached->co_ops->coo_match(cached, flat));
}

/** Performs sanity checking of given confx_object. */
static bool confx_object_is_valid(const struct confx_object *flat)
{
	/* XXX
	 * - All of c2_buf-s contained in `flat' are c2_buf_is_aimed();
	 * - all of arr_buf-s are populated with valid c2_buf-s;
	 * - etc.
	 */
	(void) flat; /* XXX */
	return true;
}

#undef C2_TRACE_SUBSYSTEM

/** @} conf_dlspec_objops */
