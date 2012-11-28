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
#include "colibri/magic.h" /* C2_CONF_FILESYSTEM_MAGIC */

static bool filesystem_check(const void *bob)
{
	const struct c2_conf_filesystem *self = bob;
	const struct c2_conf_obj        *self_obj = &self->cf_obj;

	C2_PRE(self_obj->co_type == C2_CO_FILESYSTEM);

	return ergo(obj_is_stub(self_obj), self->cf_params == NULL) &&
		ergo(self_obj->co_mounted,
		     parent_check(self_obj) &&
		     C2_CONF_CAST(self_obj->co_parent,
				  c2_conf_profile)->cp_filesystem == self &&
		     child_check(self_obj,
				 MEMBER_PTR(self->cf_services, cd_obj),
				 C2_CO_DIR));
}

C2_CONF__BOB_DEFINE(c2_conf_filesystem, C2_CONF_FILESYSTEM_MAGIC,
		    filesystem_check);

C2_CONF__INVARIANT_DEFINE(filesystem_invariant, c2_conf_filesystem);

static int filesystem_fill(struct c2_conf_obj *dest,
			   const struct confx_object *src,
			   struct c2_conf_reg *reg)
{
	int rc;
	struct c2_conf_filesystem *d = C2_CONF_CAST(dest, c2_conf_filesystem);
	const struct confx_filesystem *s = FLAT_OBJ(src, filesystem);

#if 0 /* XXX Types of d->cf_rootfid and s->xf_rootfid are different:
       * c2_fid and fid, correspondingly. */
	d->cf_rootfid = s->xf_rootfid;
#else
	d->cf_rootfid.f_container = s->xf_rootfid.f_container;
	d->cf_rootfid.f_key = s->xf_rootfid.f_key;
#endif
	rc = strings_copy(&d->cf_params, &s->xf_params);
	if (rc != 0)
		return rc;

	rc = dir_new(&src->o_id, C2_CO_SERVICE, &s->xf_services, reg,
		     &d->cf_services);
	if (rc == 0) {
		child_adopt(dest, &d->cf_services->cd_obj);
		dest->co_mounted = true;
	}
	return rc;
}

static bool filesystem_match(const struct c2_conf_obj *cached,
			     const struct confx_object *flat)
{
	const struct confx_filesystem   *objx = &flat->o_conf.u.u_filesystem;
	const struct c2_conf_filesystem *obj = C2_CONF_CAST(cached,
							    c2_conf_filesystem);
	C2_IMPOSSIBLE("XXX TODO: compare dir elements");
	return arrays_eq(obj->cf_params, &objx->xf_params) &&
		obj->cf_rootfid.f_container == objx->xf_rootfid.f_container &&
		obj->cf_rootfid.f_key == objx->xf_rootfid.f_key;
}

static int filesystem_lookup(struct c2_conf_obj *parent,
			     const struct c2_buf *name,
			     struct c2_conf_obj **out)
{
	C2_PRE(parent->co_status == C2_CS_READY);

	if (!c2_buf_streq(name, "services"))
		return -ENOENT;

	*out = &C2_CONF_CAST(parent, c2_conf_filesystem)->cf_services->cd_obj;
	return 0;
}

static void filesystem_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_filesystem *x = C2_CONF_CAST(obj, c2_conf_filesystem);

	strings_free(x->cf_params);
	c2_conf_filesystem_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops filesystem_ops = {
	.coo_invariant = filesystem_invariant,
	.coo_fill      = filesystem_fill,
	.coo_match     = filesystem_match,
	.coo_lookup    = filesystem_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = filesystem_delete
};

C2_INTERNAL struct c2_conf_obj *c2_conf__filesystem_create(void)
{
	struct c2_conf_filesystem *x;
	struct c2_conf_obj        *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_filesystem_bob_init(x);

	ret = &x->cf_obj;
	ret->co_ops = &filesystem_ops;
	return ret;
}
