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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_node_xc */
#include "lib/arith.h"       /* M0_CNT_INC */
#include "mero/magic.h"      /* M0_CONF_NODE_MAGIC */

#define XCAST(xobj) ((struct m0_confx_node *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_node, xn_header) == 0);

static bool node_check(const void *bob)
{
	const struct m0_conf_node *self = bob;
	const struct m0_conf_obj  *self_obj = &self->cn_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_NODE_TYPE);

	return true;
}

M0_CONF__BOB_DEFINE(m0_conf_node, M0_CONF_NODE_MAGIC, node_check);
M0_CONF__INVARIANT_DEFINE(node_invariant, m0_conf_node);

static int node_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src,
		       struct m0_conf_cache *cache)
{
	int                         rc;
	struct m0_conf_node        *d = M0_CONF_CAST(dest, m0_conf_node);
	const struct m0_confx_node *s = XCAST(src);
	struct m0_conf_obj         *obj;

	d->cn_memsize    = s->xn_memsize;
	d->cn_nr_cpu     = s->xn_nr_cpu;
	d->cn_last_state = s->xn_last_state;
	d->cn_flags      = s->xn_flags;

	rc = m0_conf_obj_find(cache, &s->xn_pool_id, &obj);
	if (rc != 0)
		return M0_RC(rc);

	d->cn_pool = M0_CONF_CAST(obj, m0_conf_pool);

	rc = dir_new(cache, &dest->co_id, &M0_CONF_NODE_PROCESSES_FID,
		     &M0_CONF_PROCESS_TYPE, &s->xn_processes, &d->cn_processes);
	if (rc == 0)
		child_adopt(dest, &d->cn_processes->cd_obj);

	return M0_RC(rc);
}

static int node_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_node  *s = M0_CONF_CAST(src, m0_conf_node);
	struct m0_confx_node *d = XCAST(dest);

	confx_encode(dest, src);
	d->xn_memsize    = s->cn_memsize;
	d->xn_nr_cpu     = s->cn_nr_cpu;
	d->xn_last_state = s->cn_last_state;
	d->xn_flags      = s->cn_flags;
	d->xn_pool_id    = s->cn_pool->pl_obj.co_id;

	return arrfid_from_dir(&d->xn_processes, s->cn_processes);
}

static bool
node_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_node *xobj = XCAST(flat);
	const struct m0_conf_node  *obj = M0_CONF_CAST(cached, m0_conf_node);

	return  obj->cn_memsize    == xobj->xn_memsize    &&
		obj->cn_nr_cpu     == xobj->xn_nr_cpu     &&
		obj->cn_last_state == xobj->xn_last_state &&
		obj->cn_flags      == xobj->xn_flags      &&
		m0_fid_eq(&obj->cn_pool->pl_obj.co_id, &xobj->xn_pool_id) &&
		m0_conf_dir_elems_match(obj->cn_processes, &xobj->xn_processes);
}

static int node_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		       struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_NODE_PROCESSES_FID))
		return M0_ERR(-ENOENT);

	*out = &M0_CONF_CAST(parent, m0_conf_node)->cn_processes->cd_obj;
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

M0_CONF__CTOR_DEFINE(node_create, m0_conf_node, &node_ops, NULL);

const struct m0_conf_obj_type M0_CONF_NODE_TYPE = {
	.cot_ftype = {
		.ft_id   = 'n',
		.ft_name = "node"
	},
	.cot_create  = &node_create,
	.cot_xt      = &m0_confx_node_xc,
	.cot_branch  = "u_node",
	.cot_xc_init = &m0_xc_m0_confx_node_struct_init,
	.cot_magic   = M0_CONF_NODE_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
