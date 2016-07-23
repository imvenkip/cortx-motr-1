/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nachiket Sahasrabudhe <nachiket_sahasrabuddhe@xyratex.com>
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 22-Aug-15
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_FD
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"   /* M0_ALLOC_ARR, m0_free */
#include "lib/assert.h"   /* M0_IMPOSSIBLE */
#include "lib/tlist.h"
#include "lib/arith.h"    /* M0_CNT_INC, M0_CNT_DEC */
#include "lib/chan.h"
#include "lib/finject.h"  /* M0_FI_ENABLED */
#include "lib/misc.h"     /* M0_AMB */
#include "mero/magic.h"   /* M0_CONF_OBJ_MAGIC, M0_FLSET_MAGIC */
#include "conf/obj.h"
#include "conf/obj_ops.h" /* m0_conf_obj_get_lock */
#include "conf/diter.h"
#include "conf/helpers.h" /* m0_conf_ha_state_update, m0_conf_pvers */
#include "conf/pvers.h"   /* m0_conf_pver_level */
#include "pool/flset.h"
#include "reqh/reqh.h"    /* m0_reqh, m0_reqh_invariant */
#include "conf/walk.h"    /* m0_conf_walk */

static bool flset_conf_expired_cb(struct m0_clink *clink);
static bool flset_conf_ready_cb(struct m0_clink *clink);

static bool obj_is_flset_target(const struct m0_conf_obj *obj)
{
	return M0_IN(m0_conf_obj_type(obj), (&M0_CONF_RACK_TYPE,
					     &M0_CONF_ENCLOSURE_TYPE,
					     &M0_CONF_CONTROLLER_TYPE,
					     &M0_CONF_DISK_TYPE));
}

/** Updates recd vectors of the pvers which given object belongs to. */
static void recd_update(const struct m0_conf_obj *obj,
			enum m0_ha_obj_state old_state)
{
	struct m0_conf_pver **pvers = m0_conf_pvers(obj);
	unsigned              level = m0_conf_pver_level(obj);

	if (obj->co_ha_state != M0_NC_ONLINE) {
		/* object went offline */
		for (; *pvers != NULL; ++pvers)
			M0_CNT_INC((*pvers)->pv_u.subtree.pvs_recd[level]);
	} else if (old_state != M0_NC_UNKNOWN) {
		/* object is back online */
		for (; *pvers != NULL; ++pvers)
			M0_CNT_DEC((*pvers)->pv_u.subtree.pvs_recd[level]);
	}
}

static bool flset_hw_obj_failure_cb(struct m0_clink *cl)
{
	struct m0_conf_obj    *obj = M0_AMB(obj, cl->cl_chan, co_ha_chan);
	struct m0_flset_clink *link = M0_AMB(link, cl, fcl_link);

	M0_PRE(obj_is_flset_target(obj));
	M0_PRE(obj->co_status == M0_CS_READY);
	M0_ENTRY("obj = "FID_F, FID_P(&obj->co_id));

	if (link->fcl_state != obj->co_ha_state) {
		recd_update(obj, link->fcl_state);
		link->fcl_state = obj->co_ha_state;
	}
	M0_LEAVE();
	return false;
}

static void flset_clinks_delete(struct m0_flset *flset)
{
	struct m0_conf_obj *obj;
	struct m0_clink    *cl;
	int                 i;

	for (i = 0; i < flset->fls_links_nr; i++) {
		cl = &flset->fls_links[i].fcl_link;
		if (cl->cl_chan == NULL)
			continue;
		obj = container_of(cl->cl_chan, struct m0_conf_obj, co_ha_chan);
		m0_clink_cleanup(cl);
		m0_confc_close(obj);
		m0_clink_fini(cl);
		M0_SET0(cl);
	}
}

static void flset_clinks_deregister(struct m0_flset *flset)
{
	flset_clinks_delete(flset);
	m0_free(flset->fls_links);
}

static void flset_clink_init_add(struct m0_flset    *flset,
				 struct m0_flset_clink *link,
				 struct m0_conf_obj *obj)
{
	m0_clink_init(&link->fcl_link, flset_hw_obj_failure_cb);
	m0_clink_add(&obj->co_ha_chan, &link->fcl_link);
	link->fcl_parent = flset;
	link->fcl_fid = obj->co_id;
	link->fcl_state = M0_NC_UNKNOWN;
}

static struct m0_flset_clink *flset_clink_get(struct m0_flset *flset, int idx)
{
	return &flset->fls_links[idx];
}

static int flset_clinks_add(struct m0_conf_obj *obj, void *args)
{
	struct m0_flset *flset = (struct m0_flset *)args;

	if (M0_IN(m0_conf_obj_type(obj), (&M0_CONF_RACK_TYPE,
					  &M0_CONF_ENCLOSURE_TYPE,
					  &M0_CONF_CONTROLLER_TYPE,
					  &M0_CONF_DISK_TYPE))) {
		struct m0_flset_clink * fls_cl =
			flset_clink_get(flset, flset->fls_links_nr);
		m0_conf_obj_get(obj);
		flset_clink_init_add(flset, fls_cl, obj);
		flset->fls_links_nr++;
	}
	return M0_CW_CONTINUE;
}

static int flset_target_objects_count(struct m0_conf_obj *obj, void *args)
{
	int *count = args;

	if (M0_IN(m0_conf_obj_type(obj), (&M0_CONF_RACK_TYPE,
					  &M0_CONF_ENCLOSURE_TYPE,
					  &M0_CONF_CONTROLLER_TYPE,
					  &M0_CONF_DISK_TYPE)))
		M0_CNT_INC(*count);
	return M0_CW_CONTINUE;
}

static int flset_clinks_register(struct m0_flset           *flset,
				 struct m0_conf_filesystem *fs)
{
	int objs_nr = 0;
	int rc;

	M0_ENTRY();

	m0_conf_cache_lock(fs->cf_obj.co_cache);
	rc = m0_conf_walk(flset_target_objects_count, &fs->cf_obj, &objs_nr);
	m0_conf_cache_unlock(fs->cf_obj.co_cache);
	if (rc != 0 || objs_nr == 0)
		return M0_ERR_INFO(rc ?: -ENOENT,
				   "Failed to get pool devices.");

	M0_ALLOC_ARR(flset->fls_links, objs_nr);
	if (flset->fls_links == NULL)
		return M0_ERR(-ENOMEM);
	flset->fls_links_nr = 0;

	m0_conf_cache_lock(fs->cf_obj.co_cache);
	rc = m0_conf_walk(flset_clinks_add, &fs->cf_obj, flset);
	m0_conf_cache_unlock(fs->cf_obj.co_cache);
	M0_ASSERT(ergo(rc == 0, flset->fls_links_nr == objs_nr));

	if (rc != 0)
		flset_clinks_deregister(flset);
	return M0_RC(rc);
}

static int flset_fill(struct m0_flset           *flset,
		      struct m0_conf_filesystem *fs)

{
	struct m0_confc       *confc = m0_confc_from_obj(&fs->cf_obj);
	struct m0_rpc_session *ha_session = m0_ha_session_get();
	int              rc;

	M0_PRE(ha_session != NULL);
	M0_ENTRY();

	rc = flset_clinks_register(flset, fs);
	if (rc != 0)
		return M0_ERR(rc);
	/*
	 * Fill failure sets list by updating HA state of objects for which
	 * clinks are registered. List population will be done in
	 * flset_hw_obj_failure_cb.
	 */
	/**
	 * @todo m0_conf_ha_state_update retrieves HA state of all objects in
	 * the cache, but actually states for racks, enclosures and controllers
	 * are sufficient.
	 */
	rc = m0_conf_confc_ha_update(ha_session, confc);
	if (rc != 0) {
		flset_clinks_deregister(flset);
		return M0_ERR(rc);
	}
	return M0_RC(0);
}

static bool flset_conf_expired_cb(struct m0_clink *clink)
{
	struct m0_flset *flset = container_of(clink, struct m0_flset,
					      fls_conf_expired);
	M0_ENTRY("flset %p", flset);
	flset_clinks_deregister(flset);
	M0_LEAVE();
	return true;
}

static bool flset_conf_ready_cb(struct m0_clink *clink)
{
	struct m0_flset           *flset = M0_AMB(flset, clink, fls_conf_ready);
	struct m0_reqh            *reqh = M0_AMB(reqh, clink->cl_chan,
						 rh_conf_cache_ready);
	struct m0_confc           *confc = m0_reqh2confc(reqh);
	struct m0_conf_profile    *profile;
	struct m0_conf_filesystem *fs;
	struct m0_conf_obj        *obj;
	int                        rc;

	M0_ENTRY("flset %p", flset);

	rc = m0_conf_obj_find_lock(&confc->cc_cache, &reqh->rh_profile, &obj);
	M0_ASSERT(rc == 0 && !m0_conf_obj_is_stub(obj));

	profile = M0_CONF_CAST(obj, m0_conf_profile);
	rc = m0_conf_obj_find_lock(&confc->cc_cache,
				   &profile->cp_filesystem->cf_obj.co_id,
				   &obj);
	M0_ASSERT(rc == 0 && !m0_conf_obj_is_stub(obj));
	fs = M0_CONF_CAST(obj, m0_conf_filesystem);

	rc = flset_fill(flset, fs);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to fill flset: rc=%d", rc);

	M0_LEAVE();
	return true;
}

M0_INTERNAL int m0_flset_build(struct m0_flset           *flset,
			       struct m0_conf_filesystem *fs)
{
	struct m0_reqh *reqh = M0_AMB(reqh, flset, rh_failure_set);
	int             rc;

	M0_ENTRY();
	M0_PRE(m0_reqh_invariant(reqh));
	rc = flset_fill(flset, fs);
	if (rc != 0)
		return M0_ERR(rc);

	m0_clink_init(&flset->fls_conf_expired, flset_conf_expired_cb);
	m0_clink_init(&flset->fls_conf_ready, flset_conf_ready_cb);
	m0_clink_add_lock(&reqh->rh_conf_cache_exp, &flset->fls_conf_expired);
	m0_clink_add_lock(&reqh->rh_conf_cache_ready, &flset->fls_conf_ready);
	return M0_RC(0);
}

M0_INTERNAL void m0_flset_destroy(struct m0_flset *flset)
{
	M0_ENTRY();
	flset_clinks_deregister(flset);
	if (m0_clink_is_armed(&flset->fls_conf_expired))
		m0_clink_del_lock(&flset->fls_conf_expired);
	if (m0_clink_is_armed(&flset->fls_conf_ready))
		m0_clink_del_lock(&flset->fls_conf_ready);
	m0_clink_fini(&flset->fls_conf_expired);
	m0_clink_fini(&flset->fls_conf_ready);
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
