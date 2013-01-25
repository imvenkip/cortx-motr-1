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
#include "lib/arith.h"      /* M0_CNT_INC */
#include "mero/magic.h"  /* M0_CONF_NODE_MAGIC */

static bool node_check(const void *bob)
{
	const struct m0_conf_node *self = bob;
	const struct m0_conf_obj  *self_obj = &self->cn_obj;

	M0_PRE(self_obj->co_type == M0_CO_NODE);

	return /* The notion of parent is not applicable to a node,
		* since a node may host (be a child of) several services. */
		self_obj->co_parent == NULL &&
		ergo(self_obj->co_mounted, /* check relations */
		     child_check(self_obj, MEMBER_PTR(self->cn_nics, cd_obj),
				 M0_CO_DIR) &&
		     child_check(self_obj, MEMBER_PTR(self->cn_sdevs, cd_obj),
				 M0_CO_DIR));
}

M0_CONF__BOB_DEFINE(m0_conf_node, M0_CONF_NODE_MAGIC, node_check);

M0_CONF__INVARIANT_DEFINE(node_invariant, m0_conf_node);

/**
 * Prefixes `cdr' with `car' byte and copies the result to `dest'.
 *
 * Note, that the caller is responsible for m0_buf_free()ing `dest'.
 */
static int buf_cons(char car, const struct m0_buf *cdr, struct m0_buf *dest)
{
	M0_PRE(dest->b_nob == 0 && dest->b_addr == NULL);

	M0_ALLOC_ARR(dest->b_addr, 1 + cdr->b_nob);
	if (dest->b_addr == NULL)
		return -ENOMEM;
	dest->b_nob = 1 + cdr->b_nob;

	*(char *)dest->b_addr = car;
	memcpy(dest->b_addr + 1, cdr->b_addr, cdr->b_nob);

	return 0;
}

static int node_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src,
		       struct m0_conf_cache *cache)
{
	int                         rc;
	size_t                      i;
	struct m0_conf_node        *d = M0_CONF_CAST(dest, m0_conf_node);
	const struct m0_confx_node *s = FLAT_OBJ(src, node);
	struct m0_buf               mangled_id = M0_BUF_INIT0;
	struct {
		struct m0_conf_dir  **pptr;
		char                  head;
		enum m0_conf_objtype  children_type;
		const struct arr_buf *children_ids;
	} subdirs[] = {
		{ &d->cn_nics,  'N', M0_CO_NIC,  &s->xn_nics },
		{ &d->cn_sdevs, 'S', M0_CO_SDEV, &s->xn_sdevs }
	};

	d->cn_memsize    = s->xn_memsize;
	d->cn_nr_cpu     = s->xn_nr_cpu;
	d->cn_last_state = s->xn_last_state;
	d->cn_flags      = s->xn_flags;
	d->cn_pool_id    = s->xn_pool_id;

	for (i = 0; i < ARRAY_SIZE(subdirs); ++i) {
		/* Mangle directory identifier to make it unique. */
		rc = buf_cons(subdirs[i].head, &src->o_id, &mangled_id);
		if (rc != 0)
			return rc;

		rc = dir_new(cache, &mangled_id, subdirs[i].children_type,
			     subdirs[i].children_ids, subdirs[i].pptr);
		m0_buf_free(&mangled_id);
		if (rc != 0)
			return rc;

		child_adopt(dest, &(*subdirs[i].pptr)->cd_obj);
	}
	dest->co_mounted = true;

	return 0;
}

static int node_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	int                   rc;
	struct m0_conf_node  *s = M0_CONF_CAST(src, m0_conf_node);
	struct m0_confx_node *d = &dest->o_conf.u.u_node;

	rc = m0_buf_copy(&dest->o_id, &src->co_id);
	if (rc != 0)
		return -ENOMEM;

	dest->o_conf.u_type = src->co_type;
	d->xn_memsize    = s->cn_memsize;
	d->xn_nr_cpu     = s->cn_nr_cpu;
	d->xn_last_state = s->cn_last_state;
	d->xn_flags      = s->cn_flags;
	d->xn_pool_id    = s->cn_pool_id;

	rc = arrbuf_from_dir(&d->xn_nics, s->cn_nics);
	if (rc != 0)
		return rc;

	rc = arrbuf_from_dir(&d->xn_sdevs, s->cn_sdevs);
	if (rc != 0)
		arrbuf_free(&d->xn_nics);

	return rc;
}

static bool
node_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_node *xobj = &flat->o_conf.u.u_node;
	const struct m0_conf_node  *obj = M0_CONF_CAST(cached, m0_conf_node);

	M0_IMPOSSIBLE("XXX TODO: compare dir elements");
	return  obj->cn_memsize    == xobj->xn_memsize    &&
		obj->cn_nr_cpu     == xobj->xn_nr_cpu     &&
		obj->cn_last_state == xobj->xn_last_state &&
		obj->cn_flags      == xobj->xn_flags      &&
		obj->cn_pool_id    == xobj->xn_pool_id;
}

static int node_lookup(struct m0_conf_obj *parent, const struct m0_buf *name,
		       struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (m0_buf_streq(name, "nics"))
		*out = &M0_CONF_CAST(parent, m0_conf_node)->cn_nics->cd_obj;
	else if (m0_buf_streq(name, "sdevs"))
		*out = &M0_CONF_CAST(parent, m0_conf_node)->cn_sdevs->cd_obj;
	else
		return -ENOENT;

	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void node_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_node *x = M0_CONF_CAST(obj, m0_conf_node);

	m0_conf_node_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops node_ops = {
	.coo_invariant = node_invariant,
	.coo_decode    = node_decode,
	.coo_encode    = node_encode,
	.coo_match     = node_match,
	.coo_lookup    = node_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = node_delete
};

M0_INTERNAL struct m0_conf_obj *m0_conf__node_create(void)
{
	struct m0_conf_node *x;
	struct m0_conf_obj  *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_node_bob_init(x);

	ret = &x->cn_obj;
	ret->co_ops = &node_ops;
	return ret;
}
