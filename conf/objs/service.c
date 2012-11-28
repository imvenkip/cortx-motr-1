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
#include "colibri/magic.h" /* C2_CONF_SERVICE_MAGIC */

static bool service_check(const void *bob)
{
	const struct c2_conf_service *self = bob;
	const struct c2_conf_obj     *self_obj = &self->cs_obj;

	C2_PRE(self_obj->co_type == C2_CO_SERVICE);

	return ergo(self_obj->co_status == C2_CS_READY,
		    C2_IN(self->cs_type, (C2_CFG_SERVICE_METADATA,
					  C2_CFG_SERVICE_IO,
					  C2_CFG_SERVICE_MGMT,
					  C2_CFG_SERVICE_DLM))) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent_check(self_obj) &&
		     child_check(self_obj, MEMBER_PTR(self->cs_node, cn_obj),
				 C2_CO_NODE));
}

C2_CONF__BOB_DEFINE(c2_conf_service, C2_CONF_SERVICE_MAGIC, service_check);

C2_CONF__INVARIANT_DEFINE(service_invariant, c2_conf_service);

static int service_fill(struct c2_conf_obj *dest,
			const struct confx_object *src, struct c2_conf_reg *reg)
{
	int                         rc;
	struct c2_conf_obj         *child;
	struct c2_conf_service     *d = C2_CONF_CAST(dest, c2_conf_service);
	const struct confx_service *s = FLAT_OBJ(src, service);

	d->cs_type = s->xs_type;

	rc = c2_conf_obj_find(reg, C2_CO_NODE, &s->xs_node, &child);
	if (rc != 0)
		return rc;

	d->cs_node = C2_CONF_CAST(child, c2_conf_node);
	child_adopt(dest, child);
	dest->co_mounted = true;

	return strings_copy(&d->cs_endpoints, &s->xs_endpoints);
}

static bool
service_match(const struct c2_conf_obj *cached, const struct confx_object *flat)
{
	const struct confx_service   *objx = &flat->o_conf.u.u_service;
	const struct c2_conf_service *obj = C2_CONF_CAST(cached,
							 c2_conf_service);
	const struct c2_conf_node    *child = obj->cs_node;

	return obj->cs_type == objx->xs_type &&
		arrays_eq(obj->cs_endpoints, &objx->xs_endpoints) &&
		c2_buf_eq(&child->cn_obj.co_id, &objx->xs_node);
}

static int service_lookup(struct c2_conf_obj *parent, const struct c2_buf *name,
			  struct c2_conf_obj **out)
{
	C2_PRE(parent->co_status == C2_CS_READY);

	if (!c2_buf_streq(name, "node"))
		return -ENOENT;

	*out = &C2_CONF_CAST(parent, c2_conf_service)->cs_node->cn_obj;
	return 0;
}

static void service_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_service *x = C2_CONF_CAST(obj, c2_conf_service);

	strings_free(x->cs_endpoints);
	c2_conf_service_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops conf_service_ops = {
	.coo_invariant = service_invariant,
	.coo_fill      = service_fill,
	.coo_match     = service_match,
	.coo_lookup    = service_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = service_delete
};

C2_INTERNAL struct c2_conf_obj *c2_conf__service_create(void)
{
	struct c2_conf_service *x;
	struct c2_conf_obj     *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_service_bob_init(x);

	ret = &x->cs_obj;
	ret->co_ops = &conf_service_ops;
	return ret;
}
