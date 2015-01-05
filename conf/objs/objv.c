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
 * Original author: Mandar Sawant <mandar.sawant@seagate.com>
 * Original creation date: 25-Nov-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_objv_xc */
#include "lib/arith.h"       /* M0_CNT_INC */
#include "mero/magic.h"      /* M0_CONF_OBJV_MAGIC */

#define XCAST(xobj) ((struct m0_confx_objv *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_objv, xj_header) == 0);

static bool objv_check(const void *bob)
{
	const struct m0_conf_objv *self = bob;
	const struct m0_conf_obj  *self_obj = &self->cv_obj;

	return m0_conf_obj_type(self_obj) == &M0_CONF_OBJV_TYPE;
}

M0_CONF__BOB_DEFINE(m0_conf_objv, M0_CONF_OBJV_MAGIC, objv_check);
M0_CONF__INVARIANT_DEFINE(objv_invariant, m0_conf_objv);

static int objv_decode(struct m0_conf_obj        *dest,
		       const struct m0_confx_obj *src,
		       struct m0_conf_cache      *cache)
{
	int                            rc;
	struct m0_conf_obj            *child;
	struct m0_conf_objv           *d = M0_CONF_CAST(dest, m0_conf_objv);
	const struct m0_confx_objv    *s = XCAST(src);
	const struct m0_fid           *relfid;
	const struct m0_conf_obj_type *obj_type;

	rc = m0_conf_obj_find(cache, &XCAST(src)->xj_real, &child);
	if (rc != 0)
		return M0_RC(rc);

	d->cv_real = child;

	obj_type = m0_conf_fid_type(&d->cv_real->co_id);
	if (obj_type == &M0_CONF_RACK_TYPE)
		relfid = &M0_CONF_RACKV_ENCLVS_FID;
	else if (obj_type == &M0_CONF_ENCLOSURE_TYPE)
		relfid = &M0_CONF_ENCLV_CTRLVS_FID;
	else if (obj_type == &M0_CONF_CONTROLLER_TYPE)
		relfid = &M0_CONF_CTRLV_DISKVS_FID;
	else if (obj_type == &M0_CONF_DISK_TYPE)
		return 0;
	else
		return M0_RC(-EINVAL);

	rc = dir_new(cache, &dest->co_id, relfid, &M0_CONF_OBJV_TYPE,
		     &s->xj_children, &d->cv_children);
	if (rc == 0)
		child_adopt(dest, &d->cv_children->cd_obj);

	return M0_RC(rc);
}

static int
objv_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_objv  *s = M0_CONF_CAST(src, m0_conf_objv);
	struct m0_confx_objv *d = XCAST(dest);

	confx_encode(dest, src);
	XCAST(dest)->xj_real = s->cv_real->co_id;
	return s->cv_children == NULL ? 0 :
		arrfid_from_dir(&d->xj_children, s->cv_children);
}

static bool
objv_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_objv *xobj = XCAST(flat);
	const struct m0_conf_objv  *obj = M0_CONF_CAST(cached, m0_conf_objv);

	return m0_fid_eq(&cached->co_id, &xobj->xj_real) &&
	       m0_conf_dir_elems_match(obj->cv_children, &xobj->xj_children);
}

static int objv_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		       struct m0_conf_obj **out)
{
	struct m0_conf_objv *objv = M0_CONF_CAST(parent, m0_conf_objv);
	M0_PRE(parent->co_status == M0_CS_READY);

	if ((m0_conf_fid_type(&objv->cv_real->co_id) == &M0_CONF_RACK_TYPE &&
	     m0_fid_eq(name, &M0_CONF_RACKV_ENCLVS_FID)) ||
	    (m0_conf_fid_type(&objv->cv_real->co_id) ==
			      &M0_CONF_ENCLOSURE_TYPE &&
	     m0_fid_eq(name, &M0_CONF_ENCLV_CTRLVS_FID)) ||
	     (m0_conf_fid_type(&objv->cv_real->co_id) ==
			       &M0_CONF_CONTROLLER_TYPE &&
	      m0_fid_eq(name, &M0_CONF_CTRLV_DISKVS_FID)))
		*out = &M0_CONF_CAST(parent, m0_conf_objv)->cv_children->cd_obj;
	else
		return M0_ERR(-ENOENT);

	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void objv_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_objv *x = M0_CONF_CAST(obj, m0_conf_objv);

	m0_conf_objv_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops objv_ops = {
	.coo_invariant = objv_invariant,
	.coo_decode    = objv_decode,
	.coo_encode    = objv_encode,
	.coo_match     = objv_match,
	.coo_lookup    = objv_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = objv_delete
};

M0_CONF__CTOR_DEFINE(objv_create, m0_conf_objv, &objv_ops);

const struct m0_conf_obj_type M0_CONF_OBJV_TYPE = {
	.cot_ftype = {
		.ft_id   = 'j',
		.ft_name = "objv"
	},
	.cot_create  = &objv_create,
	.cot_xt      = &m0_confx_objv_xc,
	.cot_branch  = "u_objv",
	.cot_xc_init = &m0_xc_m0_confx_objv_struct_init,
	.cot_magic   = M0_CONF_OBJV_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
