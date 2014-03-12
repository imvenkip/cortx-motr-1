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
#include "mero/magic.h" /* M0_CONF_SERVICE_MAGIC */

static bool service_check(const void *bob)
{
	const struct m0_conf_service *self = bob;
	const struct m0_conf_obj     *self_obj = &self->cs_obj;

	M0_PRE(m0_conf_obj_tid(self_obj) == M0_CO_SERVICE);

	return ergo(self_obj->co_status == M0_CS_READY,
		    M0_IN(self->cs_type,
			  (M0_CST_MDS, M0_CST_IOS, M0_CST_MGS, M0_CST_DLM,
			   M0_CST_SS))) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent_check(self_obj) &&
		     child_check(self_obj, M0_MEMBER_PTR(self->cs_node, cn_obj),
				 &M0_CONF_NODE_TYPE));
}

M0_CONF__BOB_DEFINE(m0_conf_service, M0_CONF_SERVICE_MAGIC, service_check);

M0_CONF__INVARIANT_DEFINE(service_invariant, m0_conf_service);

const struct m0_fid M0_CONF_SERVICE_NODE_FID = M0_FID_TINIT('/', 0, 2);

static int service_decode(struct m0_conf_obj *dest,
			  const struct m0_confx_obj *src,
			  struct m0_conf_cache *cache)
{
	int                            rc;
	struct m0_conf_obj            *child;
	struct m0_conf_service        *d = M0_CONF_CAST(dest, m0_conf_service);
	const struct m0_confx_service *s = FLAT_OBJ(src, service);

	d->cs_type = s->xs_type;

	rc = m0_conf_obj_find(cache, &s->xs_node, &child);
	if (rc != 0)
		return rc;

	d->cs_node = M0_CONF_CAST(child, m0_conf_node);
	child_adopt(dest, child);
	dest->co_mounted = true;

	return strings_from_arrbuf(&d->cs_endpoints, &s->xs_endpoints);
}

static int
service_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	int                      rc;
	struct m0_conf_service  *s = M0_CONF_CAST(src, m0_conf_service);
	struct m0_confx_service *d = &dest->o_conf.u.u_service;

	confx_encode(dest, src);
	d->xs_type = s->cs_type;

	rc = arrbuf_from_strings(&d->xs_endpoints, s->cs_endpoints);
	if (rc != 0)
		return -ENOMEM;

	d->xs_node = s->cs_node->cn_obj.co_id;
	return 0;
}

static bool
service_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_service *xobj = &flat->o_conf.u.u_service;
	const struct m0_conf_service  *obj = M0_CONF_CAST(cached,
							  m0_conf_service);
	return obj->cs_type == xobj->xs_type &&
		arrays_eq(obj->cs_endpoints, &xobj->xs_endpoints) &&
		m0_fid_eq(&obj->cs_node->cn_obj.co_id, &xobj->xs_node);
}

static int service_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
			  struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_SERVICE_NODE_FID))
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
	.coo_decode    = service_decode,
	.coo_encode    = service_encode,
	.coo_match     = service_match,
	.coo_lookup    = service_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = service_delete
};

static struct m0_conf_obj *service_create(void)
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

const struct m0_conf_obj_type M0_CONF_SERVICE_TYPE = {
	.cot_ftype = {
		.ft_id   = 's',
		.ft_name = "service"
	},
	.cot_id         = M0_CO_SERVICE,
	.cot_ctor       = &service_create,
	.cot_table_name = "service",
	.cot_magic      = M0_CONF_SERVICE_MAGIC
};
