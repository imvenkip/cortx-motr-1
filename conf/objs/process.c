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
#include "conf/onwire_xc.h"  /* m0_confx_process_xc */
#include "mero/magic.h"      /* M0_CONF_PROCESS_MAGIC */

#define XCAST(xobj) ((struct m0_confx_process *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_process, xr_header) == 0);

static bool process_check(const void *bob)
{
	const struct m0_conf_process *self = bob;
	const struct m0_conf_obj     *self_obj = &self->pc_obj;

	return m0_conf_obj_type(self_obj) == &M0_CONF_PROCESS_TYPE;
}

M0_CONF__BOB_DEFINE(m0_conf_process, M0_CONF_PROCESS_MAGIC, process_check);
M0_CONF__INVARIANT_DEFINE(process_invariant, m0_conf_process);

static int process_decode(struct m0_conf_obj        *dest,
			  const struct m0_confx_obj *src,
			  struct m0_conf_cache      *cache M0_UNUSED)
{
	int                            rc;
	struct m0_conf_process        *d = M0_CONF_CAST(dest, m0_conf_process);
	const struct m0_confx_process *s = XCAST(src);

	d->pc_cores    = s->xr_cores;
	d->pc_memlimit = s->xr_mem_limit;

	rc = dir_new(cache, &dest->co_id, &M0_CONF_PROCESS_SERVICES_FID,
		     &M0_CONF_SERVICE_TYPE, &s->xr_services, &d->pc_services);
	if (rc == 0)
		child_adopt(dest, &d->pc_services->cd_obj);

	return M0_RC(rc);
}

static int
process_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_process  *s = M0_CONF_CAST(src, m0_conf_process);
	struct m0_confx_process *d = XCAST(dest);

	confx_encode(dest, src);
	d->xr_cores     = s->pc_cores;
	d->xr_mem_limit = s->pc_memlimit;
	return M0_RC(arrfid_from_dir(&d->xr_services, s->pc_services));
}

static bool
process_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_process *xobj = XCAST(flat);
	const struct m0_conf_process  *obj =
		M0_CONF_CAST(cached, m0_conf_process);

	return  obj->pc_memlimit == xobj->xr_mem_limit &&
		obj->pc_cores	 == xobj->xr_cores     &&
		m0_conf_dir_elems_match(obj->pc_services, &xobj->xr_services);
}

static int process_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
			  struct m0_conf_obj **out)
{
	const struct m0_conf_process *p = M0_CONF_CAST(parent, m0_conf_process);

	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_PROCESS_SERVICES_FID))
		return M0_ERR(-ENOENT);

	*out = &p->pc_services->cd_obj;
	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void process_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_process *x = M0_CONF_CAST(obj, m0_conf_process);
	m0_conf_process_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops process_ops = {
	.coo_invariant = process_invariant,
	.coo_decode    = process_decode,
	.coo_encode    = process_encode,
	.coo_match     = process_match,
	.coo_lookup    = process_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = process_delete
};

M0_CONF__CTOR_DEFINE(process_create, m0_conf_process, &process_ops, NULL);

const struct m0_conf_obj_type M0_CONF_PROCESS_TYPE = {
	.cot_ftype = {
		.ft_id   = 'r',
		.ft_name = "process"
	},
	.cot_create  = &process_create,
	.cot_xt      = &m0_confx_process_xc,
	.cot_branch  = "u_process",
	.cot_xc_init = &m0_xc_m0_confx_process_struct_init,
	.cot_magic   = M0_CONF_PROCESS_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
