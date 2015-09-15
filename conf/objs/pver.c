/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 24-Nov-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_pver_xc */
#include "mero/magic.h"      /* M0_CONF_PVER_MAGIC */

#define XCAST(xobj) ((struct m0_confx_pver *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_pver, xv_header) == 0);

static bool pver_check(const void *bob)
{
	const struct m0_conf_pver *self = bob;
	const struct m0_conf_obj  *self_obj = &self->pv_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_PVER_TYPE);

	return ergo(!m0_conf_obj_is_stub(self_obj),
		    _0C((self->pv_attr.pa_P >=
			 self->pv_attr.pa_N + 2 * self->pv_attr.pa_K)) &&
		    _0C((self->pv_nr_failures_nr == 0) ==
			(self->pv_nr_failures == NULL)));
}

M0_CONF__BOB_DEFINE(m0_conf_pver, M0_CONF_PVER_MAGIC, pver_check);
M0_CONF__INVARIANT_DEFINE(pver_invariant, m0_conf_pver);

static int pver_decode(struct m0_conf_obj        *dest,
		       const struct m0_confx_obj *src,
		       struct m0_conf_cache      *cache)
{
	int                         rc;
	struct m0_conf_pver        *d = M0_CONF_CAST(dest, m0_conf_pver);
	const struct m0_confx_pver *s = XCAST(src);

	d->pv_ver       = s->xv_ver;
	d->pv_attr.pa_N = s->xv_N;
	d->pv_attr.pa_K = s->xv_K;
	d->pv_attr.pa_P = s->xv_P;

	d->pv_nr_failures_nr = s->xv_nr_failures.au_count;
	rc = d->pv_nr_failures_nr == 0 ? 0 :
		u32arr_decode(&s->xv_nr_failures, &d->pv_nr_failures);
	if (rc != 0)
		return M0_ERR(rc);

	rc = dir_new(cache, &dest->co_id, &M0_CONF_PVER_RACKVS_FID,
		     &M0_CONF_OBJV_TYPE, &s->xv_rackvs, &d->pv_rackvs);
	if (rc == 0)
		child_adopt(dest, &d->pv_rackvs->cd_obj);
	else
		m0_free0(&d->pv_nr_failures);
	return M0_RC(rc);
}

static int
pver_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	int                        rc;
	const struct m0_conf_pver *s = M0_CONF_CAST(src, m0_conf_pver);
	struct m0_confx_pver      *d = XCAST(dest);

	confx_encode(dest, src);

	d->xv_ver = s->pv_ver;
	d->xv_N   = s->pv_attr.pa_N;
	d->xv_K   = s->pv_attr.pa_K;
	d->xv_P   = s->pv_attr.pa_P;

	rc = u32arr_encode(&d->xv_nr_failures,
			   s->pv_nr_failures, s->pv_nr_failures_nr);
	if (rc != 0)
		return M0_ERR(rc);

	rc = arrfid_from_dir(&d->xv_rackvs, s->pv_rackvs);
	if (rc != 0)
		u32arr_free(&d->xv_nr_failures);
	return M0_RC(rc);
}

static bool
pver_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_pver *xobj = XCAST(flat);
	const struct m0_conf_pver  *obj  = M0_CONF_CAST(cached, m0_conf_pver);

	return obj->pv_ver       == xobj->xv_ver &&
	       obj->pv_attr.pa_N == xobj->xv_N &&
	       obj->pv_attr.pa_K == xobj->xv_K &&
	       obj->pv_attr.pa_P == xobj->xv_P &&
	       u32arr_cmp(&xobj->xv_nr_failures,
			  obj->pv_nr_failures, obj->pv_nr_failures_nr) &&
	       m0_conf_dir_elems_match(obj->pv_rackvs, &xobj->xv_rackvs);
}

static int pver_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		       struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_PVER_RACKVS_FID))
		return M0_ERR(-ENOENT);

	*out = &M0_CONF_CAST(parent, m0_conf_pver)->pv_rackvs->cd_obj;
	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void pver_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_pver *x = M0_CONF_CAST(obj, m0_conf_pver);

	m0_free(x->pv_nr_failures);
	m0_conf_pver_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops pver_ops = {
	.coo_invariant = pver_invariant,
	.coo_decode    = pver_decode,
	.coo_encode    = pver_encode,
	.coo_match     = pver_match,
	.coo_lookup    = pver_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = pver_delete
};

M0_CONF__CTOR_DEFINE(pver_create, m0_conf_pver, &pver_ops);

static bool pver_fid_is_valid(const struct m0_fid *fid)
{
	return M0_RC(m0_fid_type_getfid(fid)->ft_id ==
		     M0_CONF_PVER_TYPE.cot_ftype.ft_id);
}

const struct m0_conf_obj_type M0_CONF_PVER_TYPE = {
	.cot_ftype = {
		.ft_id   = 'v',
		.ft_name = "pver",
		.ft_is_valid = pver_fid_is_valid
	},
	.cot_create  = &pver_create,
	.cot_xt      = &m0_confx_pver_xc,
	.cot_branch  = "u_pver",
	.cot_xc_init = &m0_xc_m0_confx_pver_struct_init,
	.cot_magic   = M0_CONF_PVER_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
