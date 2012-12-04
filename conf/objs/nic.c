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
#include "colibri/magic.h" /* C2_CONF_NIC_MAGIC */

static bool nic_check(const void *bob)
{
	const struct c2_conf_nic *self = bob;
	const struct c2_conf_obj *self_obj = &self->ni_obj;

	C2_PRE(self_obj->co_type == C2_CO_NIC);

	return c2_conf_obj_is_stub(self_obj) == (self->ni_filename == NULL) &&
		ergo(self_obj->co_mounted, parent_check(self_obj));
}

C2_CONF__BOB_DEFINE(c2_conf_nic, C2_CONF_NIC_MAGIC, nic_check);

C2_CONF__INVARIANT_DEFINE(nic_invariant, c2_conf_nic);

static int nic_fill(struct c2_conf_obj *dest, const struct confx_object *src,
		    struct c2_conf_reg *reg)
{
	struct c2_conf_nic     *d = C2_CONF_CAST(dest, c2_conf_nic);
	const struct confx_nic *s = FLAT_OBJ(src, nic);

	d->ni_iface      = s->xi_iface;
	d->ni_mtu        = s->xi_mtu;
	d->ni_speed      = s->xi_speed;
	d->ni_last_state = s->xi_last_state;

	d->ni_filename = c2_buf_strdup(&s->xi_filename);
	return d->ni_filename == NULL ? -ENOMEM : 0;
}

static bool
nic_match(const struct c2_conf_obj *cached, const struct confx_object *flat)
{
	const struct confx_nic   *objx = &flat->o_conf.u.u_nic;
	const struct c2_conf_nic *obj = C2_CONF_CAST(cached, c2_conf_nic);

	return  obj->ni_iface      == objx->xi_iface      &&
		obj->ni_mtu        == objx->xi_mtu        &&
		obj->ni_speed      == objx->xi_speed      &&
		obj->ni_last_state == objx->xi_last_state &&
		c2_buf_streq(&objx->xi_filename, obj->ni_filename);
}

static int nic_lookup(struct c2_conf_obj *parent, const struct c2_buf *name,
		      struct c2_conf_obj **out)
{
	C2_IMPOSSIBLE("XXX not implemented");
	return -1;
}

static void nic_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_nic *x = C2_CONF_CAST(obj, c2_conf_nic);

	c2_free((void *)x->ni_filename);
	c2_conf_nic_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops nic_ops = {
	.coo_invariant = nic_invariant,
	.coo_fill      = nic_fill,
	.coo_match     = nic_match,
	.coo_lookup    = nic_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = nic_delete
};

C2_INTERNAL struct c2_conf_obj *c2_conf__nic_create(void)
{
	struct c2_conf_nic *x;
	struct c2_conf_obj *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_nic_bob_init(x);

	ret = &x->ni_obj;
	ret->co_ops = &nic_ops;
	return ret;
}

