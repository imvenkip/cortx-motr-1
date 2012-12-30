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
#include "mero/magic.h" /* M0_CONF_SERVICE_MAGIC */

static bool service_check(const void *bob)
{
	const struct m0_conf_service *self = bob;
	const struct m0_conf_obj     *self_obj = &self->cs_obj;

	M0_PRE(self_obj->co_type == M0_CO_SERVICE);

	return ergo(self_obj->co_status == M0_CS_READY,
		    M0_IN(self->cs_type,
			  (M0_CST_MDS, M0_CST_IOS, M0_CST_MGS, M0_CST_DLM))) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent_check(self_obj) &&
		     child_check(self_obj, MEMBER_PTR(self->cs_node, cn_obj),
				 M0_CO_NODE));
}

M0_CONF__BOB_DEFINE(m0_conf_service, M0_CONF_SERVICE_MAGIC, service_check);

M0_CONF__INVARIANT_DEFINE(service_invariant, m0_conf_service);

static int service_fill(struct m0_conf_obj *dest,
			const struct confx_object *src, struct m0_conf_reg *reg)
{
	int                         rc;
	struct m0_conf_obj         *child;
	struct m0_conf_service     *d = M0_CONF_CAST(dest, m0_conf_service);
	const struct confx_service *s = FLAT_OBJ(src, service);

	d->cs_type = s->xs_type;

	rc = m0_conf_obj_find(reg, M0_CO_NODE, &s->xs_node, &child);
	if (rc != 0)
		return rc;

	d->cs_node = M0_CONF_CAST(child, m0_conf_node);
	child_adopt(dest, child);
	dest->co_mounted = true;

	return strings_from_arrbuf(&d->cs_endpoints, &s->xs_endpoints);
}

static int
service_xfill(struct confx_object *dest, const struct m0_conf_obj *src)
{
	int                     rc;
	struct m0_conf_service *s = M0_CONF_CAST(src, m0_conf_service);
	struct confx_service   *d = &dest->o_conf.u.u_service;

	rc = m0_buf_copy(&dest->o_id, &src->co_id);
	if (rc != 0)
		return -ENOMEM;

	dest->o_conf.u_type = src->co_type;
	d->xs_type = s->cs_type;

	rc = arrbuf_from_strings(&d->xs_endpoints, s->cs_endpoints);
	if (rc != 0)
		goto err;

	rc = m0_buf_copy(&d->xs_node, &s->cs_node->cn_obj.co_id);
	if (rc == 0)
		return 0;

	arrbuf_free(&d->xs_endpoints);
err:
	m0_buf_free(&dest->o_id);
	return -ENOMEM;
}

static bool
service_match(const struct m0_conf_obj *cached, const struct confx_object *flat)
{
	const struct confx_service   *objx = &flat->o_conf.u.u_service;
	const struct m0_conf_service *obj = M0_CONF_CAST(cached,
							 m0_conf_service);
	const struct m0_conf_node    *child = obj->cs_node;

	return obj->cs_type == objx->xs_type &&
		arrays_eq(obj->cs_endpoints, &objx->xs_endpoints) &&
		m0_buf_eq(&child->cn_obj.co_id, &objx->xs_node);
}

static int service_lookup(struct m0_conf_obj *parent, const struct m0_buf *name,
			  struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_buf_streq(name, "node"))
		return -ENOENT;

	*out = &M0_CONF_CAST(parent, m0_conf_service)->cs_node->cn_obj;
	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void service_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_service *x = M0_CONF_CAST(obj, m0_conf_service);

	strings_free(x->cs_endpoints);
	m0_conf_service_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops conf_service_ops = {
	.coo_invariant = service_invariant,
	.coo_fill      = service_fill,
	.coo_xfill     = service_xfill,
	.coo_match     = service_match,
	.coo_lookup    = service_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = service_delete
};

M0_INTERNAL struct m0_conf_obj *m0_conf__service_create(void)
{
	struct m0_conf_service *x;
	struct m0_conf_obj     *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_service_bob_init(x);

	ret = &x->cs_obj;
	ret->co_ops = &conf_service_ops;
	return ret;
}
