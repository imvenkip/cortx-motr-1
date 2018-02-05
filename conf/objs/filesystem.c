/*
 * COPYRIGHT 2017 XYRATEX TECHNOLOGY LIMITED
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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_filesystem_xc */
#include "mero/magic.h"      /* M0_CONF_FILESYSTEM_MAGIC */
#include "lib/string.h"      /* m0_strings_free */

#define XCAST(xobj) ((struct m0_confx_filesystem *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_filesystem, xf_header) == 0);

static bool filesystem_check(const void *bob)
{
	const struct m0_conf_filesystem *self = bob;
	const struct m0_conf_obj        *self_obj = &self->cf_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_FILESYSTEM_TYPE);
	return
		m0_conf_obj_is_stub(self_obj) ?
		_0C(self->cf_params == NULL) &&
		_0C(!m0_fid_is_set(&self->cf_mdpool)) :
		_0C(m0_conf_fid_type(&self->cf_mdpool) == &M0_CONF_POOL_TYPE);
}

M0_CONF__BOB_DEFINE(m0_conf_filesystem, M0_CONF_FILESYSTEM_MAGIC,
		    filesystem_check);
M0_CONF__INVARIANT_DEFINE(filesystem_invariant, m0_conf_filesystem);

static int
filesystem_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src)
{
	struct m0_conf_filesystem        *d =
		M0_CONF_CAST(dest, m0_conf_filesystem);
	const struct m0_confx_filesystem *s = XCAST(src);
	int                               rc;

	if (!m0_conf_fid_is_valid(&s->xf_mdpool) ||
	    m0_conf_fid_type(&s->xf_mdpool) != &M0_CONF_POOL_TYPE)
		return M0_ERR(-EINVAL);
	if (m0_fid_is_set(&s->xf_imeta_pver) &&
	    (!m0_conf_fid_is_valid(&s->xf_imeta_pver) ||
	     m0_conf_fid_type(&s->xf_imeta_pver) != &M0_CONF_PVER_TYPE))
		return M0_ERR(-EINVAL);
	d->cf_mdpool     = s->xf_mdpool;
	d->cf_imeta_pver = s->xf_imeta_pver;
	d->cf_rootfid    = s->xf_rootfid;
	d->cf_redundancy = s->xf_redundancy;

	rc = m0_bufs_to_strings(&d->cf_params, &s->xf_params);
	if (rc != 0)
		return M0_ERR(rc);
	rc =
		dir_create_and_populate(
			&d->cf_nodes,
			&CONF_DIR_ENTRIES(&M0_CONF_FILESYSTEM_NODES_FID,
					  &M0_CONF_NODE_TYPE,
					  &s->xf_nodes), dest) ?:
		dir_create_and_populate(
			&d->cf_pools,
			&CONF_DIR_ENTRIES(&M0_CONF_FILESYSTEM_POOLS_FID,
					  &M0_CONF_POOL_TYPE,
					  &s->xf_pools), dest) ?:
		dir_create_and_populate(
			&d->cf_racks,
			&CONF_DIR_ENTRIES(&M0_CONF_FILESYSTEM_RACKS_FID,
					  &M0_CONF_RACK_TYPE,
					  &s->xf_racks), dest) ?:
		dir_create_and_populate(
			&d->cf_fdmi_flt_grps,
			&CONF_DIR_ENTRIES(&M0_CONF_FILESYSTEM_FDMI_FLT_GRPS_FID,
					  &M0_CONF_FDMI_FLT_GRP_TYPE,
					  &s->xf_fdmi_flt_grps), dest);

	if (rc != 0) {
		m0_strings_free(d->cf_params);
		d->cf_params = NULL; /* make invariant happy */
	}
	return M0_RC(rc);
}

static int
filesystem_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_filesystem  *s = M0_CONF_CAST(src, m0_conf_filesystem);
	struct m0_confx_filesystem *d = XCAST(dest);
	const struct conf_dir_encoding_pair dirs[] = {
		{ s->cf_nodes, &d->xf_nodes },
		{ s->cf_pools, &d->xf_pools },
		{ s->cf_racks, &d->xf_racks }
	};
	int rc;

	confx_encode(dest, src);
	d->xf_rootfid    = s->cf_rootfid;
	d->xf_mdpool     = s->cf_mdpool;
	d->xf_imeta_pver = s->cf_imeta_pver;
	d->xf_redundancy = s->cf_redundancy;
	rc = m0_bufs_from_strings(&d->xf_params, s->cf_params);
	if (rc != 0)
		return M0_ERR(rc);
	rc = conf_dirs_encode(dirs, ARRAY_SIZE(dirs));
	if (rc != 0)
		m0_bufs_free(&d->xf_params);
	if (s->cf_fdmi_flt_grps != NULL) {
		/**
		 * @todo Make spiel happy for now as it does not know about
		 * fdmi yet.
		 */
		rc = arrfid_from_dir(&d->xf_fdmi_flt_grps, s->cf_fdmi_flt_grps);
		if (rc != 0)
			m0_bufs_free(&d->xf_params);
	}
	return M0_RC(rc);
}

static bool filesystem_match(const struct m0_conf_obj  *cached,
			     const struct m0_confx_obj *flat)
{
	const struct m0_confx_filesystem *xobj = XCAST(flat);
	const struct m0_conf_filesystem  *obj =
		M0_CONF_CAST(cached, m0_conf_filesystem);

	M0_PRE(xobj->xf_params.ab_count != 0);
	return
		m0_fid_eq(&obj->cf_rootfid, &xobj->xf_rootfid) &&
		m0_fid_eq(&obj->cf_mdpool, &xobj->xf_mdpool) &&
		m0_fid_eq(&obj->cf_imeta_pver, &xobj->xf_imeta_pver) &&
		obj->cf_redundancy == xobj->xf_redundancy &&
		m0_bufs_streq(&xobj->xf_params, obj->cf_params) &&
		m0_conf_dir_elems_match(obj->cf_nodes, &xobj->xf_nodes) &&
		m0_conf_dir_elems_match(obj->cf_pools, &xobj->xf_pools) &&
		m0_conf_dir_elems_match(obj->cf_racks, &xobj->xf_racks);
}

static int filesystem_lookup(const struct m0_conf_obj *parent,
			     const struct m0_fid *name,
			     struct m0_conf_obj **out)
{
	struct m0_conf_filesystem *f = M0_CONF_CAST(parent, m0_conf_filesystem);
	const struct conf_dir_relation dirs[] = {
		{ f->cf_nodes,         &M0_CONF_FILESYSTEM_NODES_FID },
		{ f->cf_pools,         &M0_CONF_FILESYSTEM_POOLS_FID },
		{ f->cf_racks,         &M0_CONF_FILESYSTEM_RACKS_FID },
		{ f->cf_fdmi_flt_grps, &M0_CONF_FILESYSTEM_FDMI_FLT_GRPS_FID }
	};

	M0_PRE(parent->co_status == M0_CS_READY);
	return M0_RC(conf_dirs_lookup(out, name, dirs, ARRAY_SIZE(dirs)));
}

static const struct m0_fid **filesystem_downlinks(const struct m0_conf_obj *obj)
{
	static const struct m0_fid *rels[] = {
		&M0_CONF_FILESYSTEM_NODES_FID,
		&M0_CONF_FILESYSTEM_RACKS_FID,
		&M0_CONF_FILESYSTEM_POOLS_FID,
		&M0_CONF_FILESYSTEM_FDMI_FLT_GRPS_FID,
		NULL
	};
	M0_PRE(m0_conf_obj_type(obj) == &M0_CONF_FILESYSTEM_TYPE);
	return rels;
}

static void filesystem_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_filesystem *x = M0_CONF_CAST(obj, m0_conf_filesystem);

	m0_strings_free(x->cf_params);
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
	.coo_downlinks = filesystem_downlinks,
	.coo_delete    = filesystem_delete
};

static struct m0_conf_obj *filesystem_create(void)
{
	struct m0_conf_filesystem *x;
	struct m0_conf_obj        *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_filesystem_bob_init(x);

	ret = &x->cf_obj;
	ret->co_parent = NULL;
	ret->co_ops = &filesystem_ops;
	return ret;
}

const struct m0_conf_obj_type M0_CONF_FILESYSTEM_TYPE = {
	.cot_ftype = {
		.ft_id   = 'f',
		.ft_name = "conf_filesystem"
	},
	.cot_create  = &filesystem_create,
	.cot_xt      = &m0_confx_filesystem_xc,
	.cot_branch  = "u_filesystem",
	.cot_xc_init = &m0_xc_m0_confx_filesystem_struct_init,
	.cot_magic   = M0_CONF_FILESYSTEM_MAGIC
};

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
