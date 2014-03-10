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
				 M0_MEMBER_PTR(self->cf_services, cd_obj),
				 M0_CO_DIR));
}

M0_CONF__BOB_DEFINE(m0_conf_filesystem, M0_CONF_FILESYSTEM_MAGIC,
		    filesystem_check);

M0_CONF__INVARIANT_DEFINE(filesystem_invariant, m0_conf_filesystem);

const struct m0_fid M0_CONF_FILESYSTEM_SERVICES_FID = { 0, 3 };
static int filesystem_decode(struct m0_conf_obj *dest,
			     const struct m0_confx_obj *src,
			     struct m0_conf_cache *cache)
{
	int rc;
	struct m0_conf_filesystem *d = M0_CONF_CAST(dest, m0_conf_filesystem);
	const struct m0_confx_filesystem *s = FLAT_OBJ(src, filesystem);

	d->cf_rootfid = s->xf_rootfid;
	rc = strings_from_arrbuf(&d->cf_params, &s->xf_params);
	if (rc != 0)
		return rc;

	rc = dir_new(cache, &src->o_id, M0_CO_SERVICE, &s->xf_services,
		     &d->cf_services);
	if (rc == 0) {
		child_adopt(dest, &d->cf_services->cd_obj);
		dest->co_mounted = true;
	} else {
		strings_free(d->cf_params);
		d->cf_params = NULL; /* make invariant happy */
	}
	return rc;
}

static int
filesystem_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	int                         rc;
	struct m0_conf_filesystem  *s = M0_CONF_CAST(src, m0_conf_filesystem);
	struct m0_confx_filesystem *d = &dest->o_conf.u.u_filesystem;

	confx_encode(dest, src);
	d->xf_rootfid = s->cf_rootfid;

	rc = arrbuf_from_strings(&d->xf_params, s->cf_params);
	if (rc != 0)
		return rc;

	rc = arrfid_from_dir(&d->xf_services, s->cf_services);
	if (rc != 0)
		arrbuf_free(&d->xf_params);
	return rc;
}

static bool filesystem_match(const struct m0_conf_obj *cached,
			     const struct m0_confx_obj *flat)
{
	const struct m0_confx_filesystem *xobj = &flat->o_conf.u.u_filesystem;
	const struct m0_conf_filesystem  *obj =
		M0_CONF_CAST(cached, m0_conf_filesystem);

	M0_IMPOSSIBLE("XXX TODO: compare dir elements");
	return arrays_eq(obj->cf_params, &xobj->xf_params) &&
		obj->cf_rootfid.f_container == xobj->xf_rootfid.f_container &&
		obj->cf_rootfid.f_key == xobj->xf_rootfid.f_key;
}

static int filesystem_lookup(struct m0_conf_obj *parent,
			     const struct m0_fid *name,
			     struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_FILESYSTEM_SERVICES_FID))
		return -ENOENT;

	*out = &M0_CONF_CAST(parent, m0_conf_filesystem)->cf_services->cd_obj;
	M0_POST(m0_conf_obj_invariant(*out));
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
	.coo_decode    = filesystem_decode,
	.coo_encode    = filesystem_encode,
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
