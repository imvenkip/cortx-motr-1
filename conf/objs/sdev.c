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
#include "colibri/magic.h" /* C2_CONF_SDEV_MAGIC */

static bool sdev_check(const void *bob)
{
	const struct c2_conf_sdev *self = bob;
	const struct c2_conf_obj  *self_obj = &self->sd_obj;

	C2_PRE(self_obj->co_type == C2_CO_SDEV);

	return obj_is_stub(self_obj) == (self->sd_filename == NULL) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent_check(self_obj) &&
		     child_check(self_obj, MEMBER_PTR(self->sd_partitions,
						      cd_obj), C2_CO_DIR));
}

C2_CONF__BOB_DEFINE(c2_conf_sdev, C2_CONF_SDEV_MAGIC, sdev_check);

C2_CONF__INVARIANT_DEFINE(sdev_invariant, c2_conf_sdev);

static int sdev_fill(struct c2_conf_obj *dest, const struct confx_object *src,
		     struct c2_conf_reg *reg)
{
	int                      rc;
	struct c2_conf_sdev     *d = C2_CONF_CAST(dest, c2_conf_sdev);
	const struct confx_sdev *s = FLAT_OBJ(src, sdev);

	d->sd_iface      = s->xd_iface;
	d->sd_media      = s->xd_media;
	d->sd_size       = s->xd_size;
	d->sd_last_state = s->xd_last_state;
	d->sd_flags      = s->xd_flags;

	d->sd_filename = c2_buf_strdup(&s->xd_filename);
	if (d->sd_filename == NULL)
		return -ENOMEM;

	rc = dir_new(&src->o_id, C2_CO_PARTITION, &s->xd_partitions, reg,
		     &d->sd_partitions);
	if (rc == 0) {
		child_adopt(dest, &d->sd_partitions->cd_obj);
		dest->co_mounted = true;
	}
	return rc;
}

static bool
sdev_match(const struct c2_conf_obj *cached, const struct confx_object *flat)
{
	const struct confx_sdev   *objx = &flat->o_conf.u.u_sdev;
	const struct c2_conf_sdev *obj = C2_CONF_CAST(cached, c2_conf_sdev);

	C2_IMPOSSIBLE("XXX TODO: compare dir elements");
	return  obj->sd_iface      == objx->xd_iface      &&
		obj->sd_media      == objx->xd_media      &&
		obj->sd_size       == objx->xd_size       &&
		obj->sd_last_state == objx->xd_last_state &&
		obj->sd_flags      == objx->xd_flags      &&
		c2_buf_streq(&objx->xd_filename, obj->sd_filename);
}

static int sdev_lookup(struct c2_conf_obj *parent, const struct c2_buf *name,
		       struct c2_conf_obj **out)
{
	C2_IMPOSSIBLE("XXX not implemented");
	return -1;
}

static void sdev_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_sdev *x = C2_CONF_CAST(obj, c2_conf_sdev);

	c2_free((void *)x->sd_filename);
	c2_conf_sdev_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops sdev_ops = {
	.coo_invariant = sdev_invariant,
	.coo_fill      = sdev_fill,
	.coo_match     = sdev_match,
	.coo_lookup    = sdev_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = sdev_delete
};

struct c2_conf_obj *c2_conf__sdev_create(void)
{
	struct c2_conf_sdev *x;
	struct c2_conf_obj  *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_sdev_bob_init(x);

	ret = &x->sd_obj;
	ret->co_ops = &sdev_ops;
	return ret;
}
