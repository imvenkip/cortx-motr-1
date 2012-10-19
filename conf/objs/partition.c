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
#include "colibri/magic.h" /* C2_CONF_PARTITION_MAGIC */

static bool partition_check(const void *bob)
{
	const struct c2_conf_partition *self = bob;
	const struct c2_conf_obj       *self_obj = &self->pa_obj;

	C2_PRE(self_obj->co_type == C2_CO_PARTITION);

	return obj_is_stub(self_obj) == (self->pa_filename == NULL) &&
		ergo(self_obj->co_mounted, parent_check(self_obj));
}

C2_CONF__BOB_DEFINE(c2_conf_partition, C2_CONF_PARTITION_MAGIC,
		    partition_check);

C2_CONF__INVARIANT_DEFINE(partition_invariant, c2_conf_partition);

static int partition_fill(struct c2_conf_obj *dest,
			  const struct confx_object *src,
			  struct c2_conf_reg *reg)
{
	struct c2_conf_partition     *d = C2_CONF_CAST(dest, c2_conf_partition);
	const struct confx_partition *s = FLAT_OBJ(src, partition);

	d->pa_start = s->xa_start;
	d->pa_size  = s->xa_size;
	d->pa_index = s->xa_index;
	d->pa_type  = s->xa_type;

	d->pa_filename = c2_buf_strdup(&s->xa_file);
	return d->pa_filename == NULL ? -ENOMEM : 0;
}

static bool partition_match(const struct c2_conf_obj *cached,
			    const struct confx_object *flat)
{
	const struct confx_partition   *objx = &flat->o_conf.u.u_partition;
	const struct c2_conf_partition *obj = C2_CONF_CAST(cached,
							   c2_conf_partition);
	return  obj->pa_start == objx->xa_start &&
		obj->pa_size  == objx->xa_size  &&
		obj->pa_index == objx->xa_index &&
		obj->pa_type  == objx->xa_type  &&
		c2_buf_streq(&objx->xa_file, obj->pa_filename);
}

static int partition_lookup(struct c2_conf_obj *parent,
			    const struct c2_buf *name, struct c2_conf_obj **out)
{
	C2_IMPOSSIBLE("XXX not implemented");
	return -1;
}

static void partition_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_partition *x = C2_CONF_CAST(obj, c2_conf_partition);

	c2_free((void *)x->pa_filename);
	c2_conf_partition_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops partition_ops = {
	.coo_invariant = partition_invariant,
	.coo_fill      = partition_fill,
	.coo_match     = partition_match,
	.coo_lookup    = partition_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = partition_delete
};

struct c2_conf_obj *c2_conf__partition_create(void)
{
	struct c2_conf_partition *x;
	struct c2_conf_obj       *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_partition_bob_init(x);

	ret = &x->pa_obj;
	ret->co_ops = &partition_ops;
	return ret;
}
