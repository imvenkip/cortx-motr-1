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
#include "mero/magic.h" /* M0_CONF_SDEV_MAGIC */

static bool sdev_check(const void *bob)
{
	const struct m0_conf_sdev *self = bob;
	const struct m0_conf_obj  *self_obj = &self->sd_obj;

	M0_PRE(self_obj->co_type == M0_CO_SDEV);

	return  m0_conf_obj_is_stub(self_obj) == (self->sd_filename == NULL) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent_check(self_obj) &&
		     child_check(self_obj,
				 M0_MEMBER_PTR(self->sd_partitions, cd_obj),
				 M0_CO_DIR));
}

M0_CONF__BOB_DEFINE(m0_conf_sdev, M0_CONF_SDEV_MAGIC, sdev_check);

M0_CONF__INVARIANT_DEFINE(sdev_invariant, m0_conf_sdev);

static int sdev_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src,
		       struct m0_conf_cache *cache)
{
	int                         rc;
	struct m0_conf_sdev        *d = M0_CONF_CAST(dest, m0_conf_sdev);
	const struct m0_confx_sdev *s = FLAT_OBJ(src, sdev);

	d->sd_iface      = s->xd_iface;
	d->sd_media      = s->xd_media;
	d->sd_size       = s->xd_size;
	d->sd_last_state = s->xd_last_state;
	d->sd_flags      = s->xd_flags;

	d->sd_filename = m0_buf_strdup(&s->xd_filename);
	if (d->sd_filename == NULL)
		return -ENOMEM;

	rc = dir_new(cache, &src->o_id, M0_CO_PARTITION, &s->xd_partitions,
		     &d->sd_partitions);
	if (rc == 0) {
		child_adopt(dest, &d->sd_partitions->cd_obj);
		dest->co_mounted = true;
	}
	return rc;
}

static int sdev_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static bool
sdev_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_sdev *xobj = &flat->o_conf.u.u_sdev;
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

M0_INTERNAL struct m0_conf_obj *m0_conf__sdev_create(void)
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
