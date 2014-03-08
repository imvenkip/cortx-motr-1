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
#include "mero/magic.h" /* M0_CONF_PARTITION_MAGIC */

static bool partition_check(const void *bob)
{
	const struct m0_conf_partition *self = bob;
	const struct m0_conf_obj       *self_obj = &self->pa_obj;

	M0_PRE(self_obj->co_type == M0_CO_PARTITION);

	return m0_conf_obj_is_stub(self_obj) == (self->pa_filename == NULL) &&
		ergo(self_obj->co_mounted, parent_check(self_obj));
}

M0_CONF__BOB_DEFINE(m0_conf_partition, M0_CONF_PARTITION_MAGIC,
		    partition_check);

M0_CONF__INVARIANT_DEFINE(partition_invariant, m0_conf_partition);

static int partition_decode(struct m0_conf_obj *dest,
			    const struct m0_confx_obj *src,
			    struct m0_conf_cache *cache M0_UNUSED)
{
	struct m0_conf_partition *d = M0_CONF_CAST(dest, m0_conf_partition);
	const struct m0_confx_partition *s = FLAT_OBJ(src, partition);

	d->pa_start = s->xa_start;
	d->pa_size  = s->xa_size;
	d->pa_index = s->xa_index;
	d->pa_type  = s->xa_type;

	d->pa_filename = m0_buf_strdup(&s->xa_file);
	return d->pa_filename == NULL ? -ENOMEM : 0;
}

static int
partition_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static bool partition_match(const struct m0_conf_obj *cached,
			    const struct m0_confx_obj *flat)
{
	const struct m0_confx_partition *xobj = &flat->o_conf.u.u_partition;
	const struct m0_conf_partition  *obj = M0_CONF_CAST(cached,
							    m0_conf_partition);
	return  obj->pa_start == xobj->xa_start &&
		obj->pa_size  == xobj->xa_size  &&
		obj->pa_index == xobj->xa_index &&
		obj->pa_type  == xobj->xa_type  &&
		m0_buf_streq(&xobj->xa_file, obj->pa_filename);
}

static int partition_lookup(struct m0_conf_obj *parent,
			    const struct m0_fid *name, struct m0_conf_obj **out)
{
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static void partition_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_partition *x = M0_CONF_CAST(obj, m0_conf_partition);

	m0_free((void *)x->pa_filename);
	m0_conf_partition_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops partition_ops = {
	.coo_invariant = partition_invariant,
	.coo_decode    = partition_decode,
	.coo_encode    = partition_encode,
	.coo_match     = partition_match,
	.coo_lookup    = partition_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = partition_delete
};

M0_INTERNAL struct m0_conf_obj *m0_conf__partition_create(void)
{
	struct m0_conf_partition *x;
	struct m0_conf_obj       *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_partition_bob_init(x);

	ret = &x->pa_obj;
	ret->co_ops = &partition_ops;
	return ret;
}
