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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 12-Mar-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"   /* m0_confx_root_xc */
#include "mero/magic.h"       /* M0_CONF_ROOT_MAGIC */

#define XCAST(xobj) ((struct m0_confx_root *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_root, xt_header) == 0);

static bool root_check(const void *bob)
{
	const struct m0_conf_root *self = bob;
	const struct m0_conf_obj  *self_obj = &self->rt_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_ROOT_TYPE);

	return _0C(m0_fid_eq(&self->rt_obj.co_id, &M0_CONF_ROOT_FID)) &&
	       _0C(self_obj->co_parent == NULL) &&
	       _0C(m0_conf_obj_is_stub(self_obj) || self->rt_verno > 0);
}

M0_CONF__BOB_DEFINE(m0_conf_root, M0_CONF_ROOT_MAGIC, root_check);
M0_CONF__INVARIANT_DEFINE(root_invariant, m0_conf_root);

static int
root_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src)
{
	struct m0_conf_root        *d = M0_CONF_CAST(dest, m0_conf_root);
	const struct m0_confx_root *s = XCAST(src);

	if (s->xt_verno == 0)
		return M0_ERR(-EINVAL);
	d->rt_verno = s->xt_verno;
	return M0_RC(dir_create_and_populate(
			     &d->rt_profiles,
			     &CONF_DIR_ENTRIES(&M0_CONF_ROOT_PROFILES_FID,
					       &M0_CONF_PROFILE_TYPE,
					       &s->xt_profiles), dest));
}

static int root_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_root  *s = M0_CONF_CAST(src, m0_conf_root);
	struct m0_confx_root *d = XCAST(dest);
	const struct conf_dir_encoding_pair dirs[] = {
		{ s->rt_profiles, &d->xt_profiles }
	};

	confx_encode(dest, src);
	d->xt_verno = s->rt_verno;
	return conf_dirs_encode(dirs, ARRAY_SIZE(dirs));
}

static bool root_match(const struct m0_conf_obj  *cached,
		       const struct m0_confx_obj *flat)
{
	const struct m0_confx_root *xobj = XCAST(flat);
	const struct m0_conf_root  *obj = M0_CONF_CAST(cached, m0_conf_root);

	return obj->rt_verno == xobj->xt_verno &&
	       m0_conf_dir_elems_match(obj->rt_profiles, &xobj->xt_profiles);
}

static int root_lookup(const struct m0_conf_obj *parent,
		       const struct m0_fid *name, struct m0_conf_obj **out)
{
	struct m0_conf_root *root = M0_CONF_CAST(parent, m0_conf_root);
	const struct conf_dir_relation dirs[] = {
		{ root->rt_profiles, &M0_CONF_ROOT_PROFILES_FID }
	};

	M0_PRE(parent->co_status == M0_CS_READY);
	return M0_RC(conf_dirs_lookup(out, name, dirs, ARRAY_SIZE(dirs)));
}

static const struct m0_fid **root_downlinks(const struct m0_conf_obj *obj)
{
	static const struct m0_fid *rels[] = { &M0_CONF_ROOT_PROFILES_FID,
					       NULL };
	M0_PRE(m0_conf_obj_type(obj) == &M0_CONF_ROOT_TYPE);
	return rels;
}

static void root_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_root *root = M0_CONF_CAST(obj, m0_conf_root);

	m0_conf_root_bob_fini(root);
	m0_free(root);
}

static const struct m0_conf_obj_ops root_ops = {
	.coo_invariant = root_invariant,
	.coo_decode    = root_decode,
	.coo_encode    = root_encode,
	.coo_match     = root_match,
	.coo_lookup    = root_lookup,
	.coo_readdir   = NULL,
	.coo_downlinks = root_downlinks,
	.coo_delete    = root_delete
};

M0_CONF__CTOR_DEFINE(root_create, m0_conf_root, &root_ops);

const struct m0_conf_obj_type M0_CONF_ROOT_TYPE = {
	.cot_ftype = {
		.ft_id   = 't',
		.ft_name = "conf_root"
	},
	.cot_create  = &root_create,
	.cot_xt      = &m0_confx_root_xc,
	.cot_branch  = "u_root",
	.cot_xc_init = &m0_xc_m0_confx_root_struct_init,
	.cot_magic   = M0_CONF_ROOT_MAGIC
};

const struct m0_fid M0_CONF_ROOT_FID = M0_FID_TINIT('t', 1, 0);

#undef XCAST
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
