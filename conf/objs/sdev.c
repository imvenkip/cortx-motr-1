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
#include "conf/onwire_xc.h" /* m0_confx_sdev_xc */
#include "mero/magic.h"     /* M0_CONF_SDEV_MAGIC */

#define XCAST(xobj) ((struct m0_confx_sdev *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_sdev, xd_header) == 0);

static bool sdev_check(const void *bob)
{
	const struct m0_conf_sdev *self = bob;
	const struct m0_conf_obj  *self_obj = &self->sd_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_SDEV_TYPE);

	return  m0_conf_obj_is_stub(self_obj) == (self->sd_filename == NULL);
}

M0_CONF__BOB_DEFINE(m0_conf_sdev, M0_CONF_SDEV_MAGIC, sdev_check);

M0_CONF__INVARIANT_DEFINE(sdev_invariant, m0_conf_sdev);

static int sdev_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src,
		       struct m0_conf_cache *cache)
{
	struct m0_conf_sdev        *d = M0_CONF_CAST(dest, m0_conf_sdev);
	const struct m0_confx_sdev *s = XCAST(src);

	d->sd_iface      = s->xd_iface;
	d->sd_media      = s->xd_media;
	d->sd_size       = s->xd_size;
	d->sd_last_state = s->xd_last_state;
	d->sd_flags      = s->xd_flags;

	d->sd_filename = m0_buf_strdup(&s->xd_filename);
	if (d->sd_filename == NULL)
		return -ENOMEM;
	return 0;
}

static int sdev_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	confx_encode(dest, src);
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static bool
sdev_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_sdev *xobj = XCAST(flat);
	const struct m0_conf_sdev  *obj = M0_CONF_CAST(cached, m0_conf_sdev);

	M0_IMPOSSIBLE("XXX TODO: compare dir elements");
	return  obj->sd_iface      == xobj->xd_iface      &&
		obj->sd_media      == xobj->xd_media      &&
		obj->sd_size       == xobj->xd_size       &&
		obj->sd_last_state == xobj->xd_last_state &&
		obj->sd_flags      == xobj->xd_flags      &&
		m0_buf_streq(&xobj->xd_filename, obj->sd_filename);
}

static int sdev_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		       struct m0_conf_obj **out)
{
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static void sdev_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_sdev *x = M0_CONF_CAST(obj, m0_conf_sdev);

	m0_free((void *)x->sd_filename);
	m0_conf_sdev_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops sdev_ops = {
	.coo_invariant = sdev_invariant,
	.coo_decode    = sdev_decode,
	.coo_encode    = sdev_encode,
	.coo_match     = sdev_match,
	.coo_lookup    = sdev_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = sdev_delete
};

static struct m0_conf_obj *sdev_create(void)
{
	struct m0_conf_sdev *x;
	struct m0_conf_obj  *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_sdev_bob_init(x);

	ret = &x->sd_obj;
	ret->co_ops = &sdev_ops;
	return ret;
}

const struct m0_conf_obj_type M0_CONF_SDEV_TYPE = {
	.cot_ftype = {
		.ft_id   = 'd',
		.ft_name = "storage device"
	},
	.cot_create     = &sdev_create,
	.cot_xt         = &m0_confx_sdev_xc,
	.cot_branch     = "u_sdev",
	.cot_xc_init    = &m0_xc_m0_confx_sdev_struct_init,
	.cot_magic      = M0_CONF_SDEV_MAGIC
};

#undef XCAST
