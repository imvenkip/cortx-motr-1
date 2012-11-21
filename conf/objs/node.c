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
#include "lib/arith.h"      /* C2_CNT_INC */
#include "colibri/magic.h"  /* C2_CONF_NODE_MAGIC */

static bool node_check(const void *bob)
{
	const struct c2_conf_node *self = bob;
	const struct c2_conf_obj  *self_obj = &self->cn_obj;

	C2_PRE(self_obj->co_type == C2_CO_NODE);

	return /* The notion of parent is not applicable to a node,
		* since a node may host (be a child of) several services. */
		self_obj->co_parent == NULL &&
		ergo(self_obj->co_mounted, /* check relations */
		     child_check(self_obj, MEMBER_PTR(self->cn_nics, cd_obj),
				 C2_CO_DIR) &&
		     child_check(self_obj, MEMBER_PTR(self->cn_sdevs, cd_obj),
				 C2_CO_DIR));
}

C2_CONF__BOB_DEFINE(c2_conf_node, C2_CONF_NODE_MAGIC, node_check);

C2_CONF__INVARIANT_DEFINE(node_invariant, c2_conf_node);

/**
 * Prefixes `cdr' with `car' byte and copies the result to `dest'.
 *
 * Note, that the caller is responsible for c2_buf_free()ing `dest'.
 */
static int buf_cons(char car, const struct c2_buf *cdr, struct c2_buf *dest)
{
	C2_PRE(dest->b_nob == 0 && dest->b_addr == NULL);

	C2_ALLOC_ARR(dest->b_addr, 1 + cdr->b_nob);
	if (dest->b_addr == NULL)
		return -ENOMEM;
	dest->b_nob = 1 + cdr->b_nob;

	*(char *)dest->b_addr = car;
	memcpy(dest->b_addr + 1, cdr->b_addr, cdr->b_nob);

	return 0;
}

static int node_fill(struct c2_conf_obj *dest, const struct confx_object *src,
		     struct c2_conf_reg *reg)
{
	int                      rc;
	size_t                   i;
	struct c2_conf_node     *d = C2_CONF_CAST(dest, c2_conf_node);
	const struct confx_node *s = FLAT_OBJ(src, node);
	struct c2_buf            mangled_id = C2_BUF_INIT0;
	struct {
		struct c2_conf_dir  **pptr;
		char                  head;
		enum c2_conf_objtype  children_type;
		const struct arr_buf *children_ids;
	} subdirs[] = {
		{ &d->cn_nics,  'N', C2_CO_NIC,  &s->xn_nics },
		{ &d->cn_sdevs, 'S', C2_CO_SDEV, &s->xn_sdevs }
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

		rc = dir_new(&mangled_id, subdirs[i].children_type,
			     subdirs[i].children_ids, reg, subdirs[i].pptr);
		c2_buf_free(&mangled_id);
		if (rc != 0)
			return rc;

		child_adopt(dest, &(*subdirs[i].pptr)->cd_obj);
	}
	dest->co_mounted = true;

	return 0;
}

static bool
node_match(const struct c2_conf_obj *cached, const struct confx_object *flat)
{
	const struct confx_node   *objx = &flat->o_conf.u.u_node;
	const struct c2_conf_node *obj = C2_CONF_CAST(cached, c2_conf_node);

	C2_IMPOSSIBLE("XXX TODO: compare dir elements");
	return  obj->cn_memsize    == objx->xn_memsize    &&
		obj->cn_nr_cpu     == objx->xn_nr_cpu     &&
		obj->cn_last_state == objx->xn_last_state &&
		obj->cn_flags      == objx->xn_flags      &&
		obj->cn_pool_id    == objx->xn_pool_id;

}

static int node_lookup(struct c2_conf_obj *parent, const struct c2_buf *name,
		       struct c2_conf_obj **out)
{
	C2_IMPOSSIBLE("XXX not implemented");
	return -1;
}

static void node_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_node *x = C2_CONF_CAST(obj, c2_conf_node);

	c2_conf_node_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops node_ops = {
	.coo_invariant = node_invariant,
	.coo_fill      = node_fill,
	.coo_match     = node_match,
	.coo_lookup    = node_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = node_delete
};

C2_INTERNAL struct c2_conf_obj *c2_conf__node_create(void)
{
	struct c2_conf_node *x;
	struct c2_conf_obj  *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_node_bob_init(x);

	ret = &x->cn_obj;
	ret->co_ops = &node_ops;
	return ret;
}
