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
 * Original creation date: 12-Dec-2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "conf/onwire_xc.h"  /* m0_confx_disk_xc */
#include "mero/magic.h"      /* M0_CONF_DISK_MAGIC */

#define XCAST(xobj) ((struct m0_confx_disk *)(&(xobj)->xo_u))
M0_BASSERT(offsetof(struct m0_confx_disk, xk_header) == 0);

static bool disk_check(const void *bob)
{
	const struct m0_conf_disk *self = bob;
	const struct m0_conf_obj  *self_obj = &self->ck_obj;

	M0_PRE(m0_conf_obj_type(self_obj) == &M0_CONF_DISK_TYPE);

	return true;
}

M0_CONF__BOB_DEFINE(m0_conf_disk, M0_CONF_DISK_MAGIC, disk_check);
M0_CONF__INVARIANT_DEFINE(disk_invariant, m0_conf_disk);

static int disk_decode(struct m0_conf_obj *dest, const struct m0_confx_obj *src,
		       struct m0_conf_cache *cache)
{
	int                   rc;
	struct m0_conf_obj   *child;
	struct m0_conf_disk  *d = M0_CONF_CAST(dest, m0_conf_disk);
	struct m0_confx_disk *s = XCAST(src);

	rc = m0_conf_obj_find(cache, &XCAST(src)->xk_dev, &child);
	if (rc == 0) {
		d->ck_dev = M0_CONF_CAST(child, m0_conf_sdev);
		/* back pointer to disk objects */
		d->ck_dev->sd_disk = &dest->co_id;
	}
	return M0_RC(conf_pvers_decode(&d->ck_pvers, &s->xk_pvers, cache));

}

static int disk_encode(struct m0_confx_obj *dest, const struct m0_conf_obj *src)
{
	struct m0_conf_disk  *s = M0_CONF_CAST(src, m0_conf_disk);
	struct m0_confx_disk *d = XCAST(dest);

	confx_encode(dest, src);
	if (s->ck_dev != NULL)
		XCAST(dest)->xk_dev = s->ck_dev->sd_obj.co_id;
	return M0_RC(conf_pvers_encode(&d->xk_pvers,
			  (const struct m0_conf_pver**)s->ck_pvers));
}

static bool
disk_match(const struct m0_conf_obj *cached, const struct m0_confx_obj *flat)
{
	const struct m0_confx_disk *xobj = XCAST(flat);
	const struct m0_conf_sdev  *child =
		M0_CONF_CAST(cached, m0_conf_disk)->ck_dev;

	return m0_fid_eq(&child->sd_obj.co_id, &xobj->xk_dev);
}

static int disk_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		       struct m0_conf_obj **out)
{
	M0_PRE(parent->co_status == M0_CS_READY);

	if (!m0_fid_eq(name, &M0_CONF_DISK_SDEV_FID))
		return M0_ERR(-ENOENT);

	*out = &M0_CONF_CAST(parent, m0_conf_disk)->ck_dev->sd_obj;
	M0_POST(m0_conf_obj_invariant(*out));
	return 0;
}

static void disk_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_disk *x = M0_CONF_CAST(obj, m0_conf_disk);

	m0_conf_disk_bob_fini(x);
	m0_free(x->ck_pvers);
	m0_free(x);
}

static const struct m0_conf_obj_ops disk_ops = {
	.coo_invariant = disk_invariant,
	.coo_decode    = disk_decode,
	.coo_encode    = disk_encode,
	.coo_match     = disk_match,
	.coo_lookup    = disk_lookup,
	.coo_readdir   = NULL,
	.coo_delete    = disk_delete
};

M0_CONF__CTOR_DEFINE(disk_create, m0_conf_disk, &disk_ops);

const struct m0_conf_obj_type M0_CONF_DISK_TYPE = {
	.cot_ftype = {
		.ft_id   = 'k',
		.ft_name = "storage disk"
	},
	.cot_create  = &disk_create,
	.cot_xt      = &m0_confx_disk_xc,
	.cot_branch  = "u_disk",
	.cot_xc_init = &m0_xc_m0_confx_disk_struct_init,
	.cot_magic   = M0_CONF_DISK_MAGIC
};

#undef XCAST
#undef M0_TRACE_SUBSYSTEM
