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
#include "conf/confc.h"  /* m0_confc */
#include "mero/magic.h"  /* M0_CONF_PROFILE_MAGIC */

static bool profile_check(const void *bob)
{
	const struct m0_conf_profile *self = bob;
	const struct m0_conf_obj     *self_obj = &self->cp_obj;

	M0_PRE(self_obj->co_type == M0_CO_PROFILE);

	return  /* profile is the topmost object of a DAG */
		self_obj->co_parent == NULL &&
		ergo(self_obj->co_mounted,
		     child_check(self_obj,
				 MEMBER_PTR(self->cp_filesystem, cf_obj),
				 M0_CO_FILESYSTEM));
}

M0_CONF__BOB_DEFINE(m0_conf_profile, M0_CONF_PROFILE_MAGIC, profile_check);

M0_CONF__INVARIANT_DEFINE(profile_invariant, m0_conf_profile);

static int profile_decode(struct m0_conf_obj *dest,
			  const struct m0_confx_obj *src,
			  struct m0_conf_cache *cache)
{
	int                     rc;
	struct m0_conf_obj     *child;
	struct m0_conf_profile *d = M0_CONF_CAST(dest, m0_conf_profile);

	rc = m0_conf_obj_find(cache, M0_CO_FILESYSTEM,
			      &FLAT_OBJ(src, profile)->xp_filesystem, &child);
	if (rc == 0) {
		d->cp_filesystem = M0_CONF_CAST(child, m0_conf_filesystem);
		child_adopt(dest, child);
		dest->co_mounted = true;
	}
	return rc;
}

static int
profile_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	int rc;
	struct m0_conf_profile *s = M0_CONF_CAST(src, m0_conf_profile);

	dest->o_conf.u_type = src->co_type;
	rc = m0_buf_copy(&dest->o_id, &src->co_id);
	if (rc != 0)
		return rc;

	rc = m0_buf_copy(&dest->o_conf.u.u_profile.xp_filesystem,
			 &s->cp_filesystem->cf_obj.co_id);
	if (rc != 0)
		m0_buf_free(&dest->o_id);
	return rc;
}

static bool
profile_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_profile   *xobj = &flat->o_conf.u.u_profile;
	const struct m0_conf_filesystem *child =
		M0_CONF_CAST(cached, m0_conf_profile)->cp_filesystem;

	return m0_buf_eq(&child->cf_obj.co_id, &xobj->xp_filesystem);
}

static int profile_lookup(struct m0_conf_obj *parent, const struct m0_buf *name,
			  struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_buf_streq(name, "filesystem"))
		return -ENOENT;

	*out = &M0_CONF_CAST(parent, m0_conf_profile)->cp_filesystem->cf_obj;
	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void profile_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_profile *x = M0_CONF_CAST(obj, m0_conf_profile);

	m0_conf_profile_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops profile_ops = {
	.coo_invariant = profile_invariant,
	.coo_decode    = profile_decode,
	.coo_encode    = profile_encode,
	.coo_match     = profile_match,
	.coo_lookup    = profile_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = profile_delete
};

M0_INTERNAL struct m0_conf_obj *m0_conf__profile_create(void)
{
	struct m0_conf_profile *x;
	struct m0_conf_obj     *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_profile_bob_init(x);

	ret = &x->cp_obj;
	ret->co_ops = &profile_ops;
	return ret;
}
