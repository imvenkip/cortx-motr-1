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
#include "conf/onwire_xc.h" /* m0_confx_nic_xc */
#include "mero/magic.h"     /* M0_CONF_NIC_MAGIC */

static bool nic_check(const void *bob)
{
	const struct m0_conf_nic *self = bob;
	const struct m0_conf_obj *self_obj = &self->ni_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_NIC_TYPE);

	return m0_conf_obj_is_stub(self_obj) == (self->ni_filename == NULL) &&
		ergo(self_obj->co_mounted, parent_check(self_obj));
}

M0_CONF__BOB_DEFINE(m0_conf_nic, M0_CONF_NIC_MAGIC, nic_check);

M0_CONF__INVARIANT_DEFINE(nic_invariant, m0_conf_nic);

static int nic_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src,
		      struct m0_conf_cache *cache M0_UNUSED)
{
	struct m0_conf_nic        *d = M0_CONF_CAST(dest, m0_conf_nic);
	const struct m0_confx_nic *s = &src->xo_u.u_nic;

	d->ni_iface      = s->xi_iface;
	d->ni_mtu        = s->xi_mtu;
	d->ni_speed      = s->xi_speed;
	d->ni_last_state = s->xi_last_state;

	d->ni_filename = m0_buf_strdup(&s->xi_filename);
	return d->ni_filename == NULL ? -ENOMEM : 0;
}

static int nic_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	confx_encode(dest, src);
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static bool
nic_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_nic *xobj = &flat->xo_u.u_nic;
	const struct m0_conf_nic  *obj = M0_CONF_CAST(cached, m0_conf_nic);

	return  obj->ni_iface      == xobj->xi_iface      &&
		obj->ni_mtu        == xobj->xi_mtu        &&
		obj->ni_speed      == xobj->xi_speed      &&
		obj->ni_last_state == xobj->xi_last_state &&
		m0_buf_streq(&xobj->xi_filename, obj->ni_filename);
}

static int nic_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		      struct m0_conf_obj **out)
{
	M0_IMPOSSIBLE("XXX not implemented");
	return -ENOSYS;
}

static void nic_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_nic *x = M0_CONF_CAST(obj, m0_conf_nic);

	m0_free((void *)x->ni_filename);
	m0_conf_nic_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops nic_ops = {
	.coo_invariant = nic_invariant,
	.coo_decode    = nic_decode,
	.coo_encode    = nic_encode,
	.coo_match     = nic_match,
	.coo_lookup    = nic_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = nic_delete
};

static struct m0_conf_obj *nic_create(void)
{
	struct m0_conf_nic *x;
	struct m0_conf_obj *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_nic_bob_init(x);

	ret = &x->ni_obj;
	ret->co_ops = &nic_ops;
	return ret;
}

const struct m0_conf_obj_type M0_CONF_NIC_TYPE = {
	.cot_ftype = {
		.ft_id   = 'i',
		.ft_name = "nic"
	},
	.cot_ctor       = &nic_create,
	.cot_table_name = "nic",
	.cot_xt         = &m0_confx_nic_xc,
	.cot_magic      = M0_CONF_NIC_MAGIC
};
