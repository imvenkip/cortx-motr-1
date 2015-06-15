/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 24-Nov-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_controller_xc */
#include "mero/magic.h"      /* M0_CONF_CONTROLLER_MAGIC */
#include "conf/helpers.h"    /* m0_conf_failure_set_update */

#define XCAST(xobj) ((struct m0_confx_controller *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_controller, xc_header) == 0);

static bool controller_check(const void *bob)
{
	const struct m0_conf_controller *self = bob;
	const struct m0_conf_obj        *self_obj = &self->cc_obj;

	return m0_conf_obj_type(self_obj) == &M0_CONF_CONTROLLER_TYPE;
}

M0_CONF__BOB_DEFINE(m0_conf_controller, M0_CONF_CONTROLLER_MAGIC,
		    controller_check);
M0_CONF__INVARIANT_DEFINE(controller_invariant, m0_conf_controller);

static int controller_decode(struct m0_conf_obj        *dest,
			     const struct m0_confx_obj *src,
			     struct m0_conf_cache      *cache)
{
	int                               rc;
	struct m0_conf_obj               *obj;
	const struct m0_confx_controller *s = XCAST(src);
	struct m0_conf_controller        *d =
		M0_CONF_CAST(dest, m0_conf_controller);

	rc = m0_conf_obj_find(cache, &s->xc_node, &obj);
	if (rc != 0)
		return M0_RC(rc);

	d->cc_node = M0_CONF_CAST(obj, m0_conf_node);
	return M0_RC(dir_create_and_populate(
			     &d->cc_disks,
			     &CONF_DIR_ENTRIES(&M0_CONF_CONTROLLER_DISKS_FID,
					       &M0_CONF_DISK_TYPE,
					       &s->xc_disks), dest, cache) ?:
		    conf_pvers_decode(cache, &s->xc_pvers,
				      &d->cc_pvers, &d->cc_pvers_nr));
}

static int
controller_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_controller  *s = M0_CONF_CAST(src, m0_conf_controller);
	struct m0_confx_controller *d = XCAST(dest);
	const struct conf_dir_encoding_pair dirs[] = {
		{ s->cc_disks, &d->xc_disks }
	};

	confx_encode(dest, src);
	d->xc_node = s->cc_node->cn_obj.co_id;
	return M0_RC(conf_dirs_encode(dirs, ARRAY_SIZE(dirs)) ?:
		     conf_pvers_encode(s->cc_pvers, s->cc_pvers_nr,
				       &d->xc_pvers));
}

static bool controller_match(const struct m0_conf_obj   *cached,
			     const struct m0_confx_obj *flat)
{
	const struct m0_confx_controller *xobj = XCAST(flat);
	const struct m0_conf_controller  *obj  =
		M0_CONF_CAST(cached, m0_conf_controller);

	return m0_fid_eq(&obj->cc_node->cn_obj.co_id, &xobj->xc_node) &&
	       m0_conf_dir_elems_match(obj->cc_disks, &xobj->xc_disks);
}

static int controller_lookup(struct m0_conf_obj  *parent,
			     const struct m0_fid *name,
			     struct m0_conf_obj **out)
{
	struct m0_conf_controller *c = M0_CONF_CAST(parent, m0_conf_controller);
	const struct conf_dir_relation dirs[] = {
		{ c->cc_disks, &M0_CONF_CONTROLLER_DISKS_FID }
	};

	M0_PRE(parent->co_status == M0_CS_READY);
	return M0_RC(conf_dirs_lookup(out, name, dirs, ARRAY_SIZE(dirs)));
}

static void controller_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_controller *x = M0_CONF_CAST(obj, m0_conf_controller);
	m0_conf_controller_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops controller_ops = {
	.coo_invariant = controller_invariant,
	.coo_decode    = controller_decode,
	.coo_encode    = controller_encode,
	.coo_match     = controller_match,
	.coo_lookup    = controller_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = controller_delete
};

M0_CONF__CTOR_DEFINE(controller_create, m0_conf_controller,
		     &controller_ops, m0_conf_failure_sets_update);

const struct m0_conf_obj_type M0_CONF_CONTROLLER_TYPE = {
	.cot_ftype = {
		.ft_id   = 'c',
		.ft_name = "controller"
	},
	.cot_create  = &controller_create,
	.cot_xt      = &m0_confx_controller_xc,
	.cot_branch  = "u_controller",
	.cot_xc_init = &m0_xc_m0_confx_controller_struct_init,
	.cot_magic   = M0_CONF_CONTROLLER_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
