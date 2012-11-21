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
#include "colibri/magic.h" /* C2_CONF_PROFILE_MAGIC */

static bool profile_check(const void *bob)
{
	const struct c2_conf_profile *self = bob;
	const struct c2_conf_obj     *self_obj = &self->cp_obj;

	C2_PRE(self_obj->co_type == C2_CO_PROFILE);

	return
#if 0 /*XXX*/
		ergo(self_obj->co_confc != NULL,
		     self_obj->co_confc->cc_root == self_obj) &&
#else
		true &&
#endif
		/* c2_conf_profile is the topmost object in the DAG */
		self_obj->co_parent == NULL &&
		ergo(self_obj->co_mounted,
		     child_check(self_obj,
				 MEMBER_PTR(self->cp_filesystem, cf_obj),
				 C2_CO_FILESYSTEM));
}

C2_CONF__BOB_DEFINE(c2_conf_profile, C2_CONF_PROFILE_MAGIC, profile_check);

C2_CONF__INVARIANT_DEFINE(profile_invariant, c2_conf_profile);

static int profile_fill(struct c2_conf_obj *dest,
			const struct confx_object *src, struct c2_conf_reg *reg)
{
	int                     rc;
	struct c2_conf_obj     *child;
	struct c2_conf_profile *d = C2_CONF_CAST(dest, c2_conf_profile);

	rc = c2_conf_obj_find(reg, C2_CO_FILESYSTEM,
			      &FLAT_OBJ(src, profile)->xp_filesystem, &child);
	if (rc == 0) {
		d->cp_filesystem = C2_CONF_CAST(child, c2_conf_filesystem);
		child_adopt(dest, child);
		dest->co_mounted = true;
	}
	return rc;
}

static bool
profile_match(const struct c2_conf_obj *cached, const struct confx_object *flat)
{
	const struct confx_profile      *objx = &flat->o_conf.u.u_profile;
	const struct c2_conf_filesystem *child =
		C2_CONF_CAST(cached, c2_conf_profile)->cp_filesystem;

	return c2_buf_eq(&child->cf_obj.co_id, &objx->xp_filesystem);
}

static int profile_lookup(struct c2_conf_obj *parent, const struct c2_buf *name,
			  struct c2_conf_obj **out)
{
	C2_PRE(parent->co_status == C2_CS_READY);

	if (!c2_buf_streq(name, "filesystem"))
		return -ENOENT;

	*out = &C2_CONF_CAST(parent, c2_conf_profile)->cp_filesystem->cf_obj;
	return 0;
}

static void profile_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_profile *x = C2_CONF_CAST(obj, c2_conf_profile);

	c2_conf_profile_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops profile_ops = {
	.coo_invariant = profile_invariant,
	.coo_fill      = profile_fill,
	.coo_match     = profile_match,
	.coo_lookup    = profile_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = profile_delete
};

C2_INTERNAL struct c2_conf_obj *c2_conf__profile_create(void)
{
	struct c2_conf_profile *x;
	struct c2_conf_obj     *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_profile_bob_init(x);

	ret = &x->cp_obj;
	ret->co_ops = &profile_ops;
	return ret;
}
