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
#include "mero/magic.h" /* M0_CONF_FILESYSTEM_MAGIC */

static bool filesystem_check(const void *bob)
{
	const struct m0_conf_filesystem *self = bob;
	const struct m0_conf_obj        *self_obj = &self->cf_obj;

	M0_PRE(self_obj->co_type == M0_CO_FILESYSTEM);

	return ergo(m0_conf_obj_is_stub(self_obj), self->cf_params == NULL) &&
		ergo(self_obj->co_mounted,
		     parent_check(self_obj) &&
		     M0_CONF_CAST(self_obj->co_parent,
				  m0_conf_profile)->cp_filesystem == self &&
		     child_check(self_obj,
				 MEMBER_PTR(self->cf_services, cd_obj),
				 M0_CO_DIR));
}

M0_CONF__BOB_DEFINE(m0_conf_filesystem, M0_CONF_FILESYSTEM_MAGIC,
		    filesystem_check);

M0_CONF__INVARIANT_DEFINE(filesystem_invariant, m0_conf_filesystem);

static int filesystem_fill(struct m0_conf_obj *dest,
			   const struct confx_object *src,
			   struct m0_conf_reg *reg)
{
	int rc;
	struct m0_conf_filesystem *d = M0_CONF_CAST(dest, m0_conf_filesystem);
	const struct confx_filesystem *s = FLAT_OBJ(src, filesystem);

#if 0 /* XXX Types of d->cf_rootfid and s->xf_rootfid are different:
       * m0_fid and fid, correspondingly. */
	d->cf_rootfid = s->xf_rootfid;
#else
	d->cf_rootfid.f_container = s->xf_rootfid.f_container;
	d->cf_rootfid.f_key = s->xf_rootfid.f_key;
#endif
	rc = strings_copy(&d->cf_params, &s->xf_params);
	if (rc != 0)
		return rc;

	rc = dir_new(&src->o_id, M0_CO_SERVICE, &s->xf_services, reg,
		     &d->cf_services);
	if (rc == 0) {
		child_adopt(dest, &d->cf_services->cd_obj);
		dest->co_mounted = true;
	}
	return rc;
}

static bool filesystem_match(const struct m0_conf_obj *cached,
			     const struct confx_object *flat)
{
	const struct confx_filesystem   *objx = &flat->o_conf.u.u_filesystem;
	const struct m0_conf_filesystem *obj = M0_CONF_CAST(cached,
							    m0_conf_filesystem);
	M0_IMPOSSIBLE("XXX TODO: compare dir elements");
	return arrays_eq(obj->cf_params, &objx->xf_params) &&
		obj->cf_rootfid.f_container == objx->xf_rootfid.f_container &&
		obj->cf_rootfid.f_key == objx->xf_rootfid.f_key;
}

static int filesystem_lookup(struct m0_conf_obj *parent,
			     const struct m0_buf *name,
			     struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_buf_streq(name, "services"))
		return -ENOENT;

	*out = &M0_CONF_CAST(parent, m0_conf_filesystem)->cf_services->cd_obj;
	return 0;
}

static void filesystem_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_filesystem *x = M0_CONF_CAST(obj, m0_conf_filesystem);

	strings_free(x->cf_params);
	m0_conf_filesystem_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops filesystem_ops = {
	.coo_invariant = filesystem_invariant,
	.coo_fill      = filesystem_fill,
	.coo_match     = filesystem_match,
	.coo_lookup    = filesystem_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = filesystem_delete
};

M0_INTERNAL struct m0_conf_obj *m0_conf__filesystem_create(void)
{
	struct m0_conf_filesystem *x;
	struct m0_conf_obj        *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_filesystem_bob_init(x);

	ret = &x->cf_obj;
	ret->co_ops = &filesystem_ops;
	return ret;
}
