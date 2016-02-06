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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_service_xc */
#include "conf/schema.h"     /* M0_CONF_SVC_TYPE_IS_VALID */
#include "mero/magic.h"      /* M0_CONF_SERVICE_MAGIC */
#include "lib/string.h"      /* m0_strings_free */

static bool service_check(const void *bob)
{
	const struct m0_conf_service *self = bob;
	const struct m0_conf_obj     *self_obj = &self->cs_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_SERVICE_TYPE);

	return _0C(ergo(self_obj->co_status == M0_CS_READY,
			M0_CONF_SVC_TYPE_IS_VALID(self->cs_type)));
}

M0_CONF__BOB_DEFINE(m0_conf_service, M0_CONF_SERVICE_MAGIC, service_check);
M0_CONF__INVARIANT_DEFINE(service_invariant, m0_conf_service);

#define XCAST(xobj) ((struct m0_confx_service *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_service, xs_header) == 0);

static int service_decode(struct m0_conf_obj *dest,
			  const struct m0_confx_obj *src,
			  struct m0_conf_cache *cache)
{
	int                            rc;
	struct m0_conf_service        *d = M0_CONF_CAST(dest, m0_conf_service);
	const struct m0_confx_service *s = XCAST(src);

	d->cs_type = s->xs_type;
	rc = m0_bufs_to_strings(&d->cs_endpoints, &s->xs_endpoints);
	if (rc != 0)
		return M0_ERR(rc);

	rc = dir_new(cache, &dest->co_id, &M0_CONF_SERVICE_SDEVS_FID,
		     &M0_CONF_SDEV_TYPE, &s->xs_sdevs, &d->cs_sdevs);
	if (rc == 0)
		child_adopt(dest, &d->cs_sdevs->cd_obj);
	return M0_RC(rc);
}

static int
service_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	int                      rc;
	struct m0_conf_service  *s = M0_CONF_CAST(src, m0_conf_service);
	struct m0_confx_service *d = XCAST(dest);

	confx_encode(dest, src);
	d->xs_type = s->cs_type;

	rc = m0_bufs_from_strings(&d->xs_endpoints, s->cs_endpoints);
	return M0_RC(rc == 0 ? arrfid_from_dir(&d->xs_sdevs, s->cs_sdevs) :
		     -ENOMEM);
}

static bool
service_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_service *xobj = XCAST(flat);
	const struct m0_conf_service  *obj = M0_CONF_CAST(cached,
							  m0_conf_service);
	M0_PRE(xobj->xs_endpoints.ab_count != 0);

	return obj->cs_type == xobj->xs_type &&
	       m0_bufs_streq(&xobj->xs_endpoints, obj->cs_endpoints) &&
	       m0_conf_dir_elems_match(obj->cs_sdevs, &xobj->xs_sdevs);
}

static int service_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
			  struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_SERVICE_SDEVS_FID))
		return M0_ERR(-ENOENT);

	*out = &M0_CONF_CAST(parent, m0_conf_service)->cs_sdevs->cd_obj;
	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void service_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_service *x = M0_CONF_CAST(obj, m0_conf_service);

	m0_strings_free(x->cs_endpoints);
	m0_conf_service_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops service_ops = {
	.coo_invariant = service_invariant,
	.coo_decode    = service_decode,
	.coo_encode    = service_encode,
	.coo_match     = service_match,
	.coo_lookup    = service_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = service_delete
};

M0_CONF__CTOR_DEFINE(service_create, m0_conf_service, &service_ops);

static bool service_fid_is_valid(const struct m0_fid *fid)
{
	return M0_RC(m0_fid_type_getfid(fid)->ft_id ==
		     M0_CONF_SERVICE_TYPE.cot_ftype.ft_id);
}

const struct m0_conf_obj_type M0_CONF_SERVICE_TYPE = {
	.cot_ftype = {
		.ft_id       = 's',
		.ft_name     = "service",
		.ft_is_valid = service_fid_is_valid
	},
	.cot_create  = &service_create,
	.cot_xt      = &m0_confx_service_xc,
	.cot_branch  = "u_service",
	.cot_xc_init = &m0_xc_m0_confx_service_struct_init,
	.cot_magic   = M0_CONF_SERVICE_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
