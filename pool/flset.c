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
#include "mero/magic.h"   /* M0_CONF_OBJ_MAGIC, M0_FLSET_MAGIC */
#include "conf/obj.h"
#include "conf/obj_ops.h" /* m0_conf_obj_get_lock */
#include "conf/diter.h"
#include "conf/helpers.h" /* m0_conf_ha_state_update, m0_conf_pvers */
#include "conf/pvers.h"   /* m0_conf_pver_level */
#include "pool/flset.h"
#include "reqh/reqh.h"    /* m0_reqh, m0_reqh_invariant */

M0_TL_DESCR_DEFINE(m0_flset, "failed resources", M0_INTERNAL,
		   struct m0_conf_obj, co_flset_link, co_gen_magic,
		   M0_CONF_OBJ_MAGIC, M0_FLSET_MAGIC);
M0_TL_DEFINE(m0_flset, M0_INTERNAL, struct m0_conf_obj);

struct flset_clink {
	struct m0_clink  fcl_link;
	struct m0_flset *fcl_parent;
	struct m0_fid    fcl_fid;
};

static bool flset_conf_expired_cb(struct m0_clink *clink);
static bool flset_conf_ready_cb(struct m0_clink *clink);

static bool obj_is_flset_target(const struct m0_conf_obj *obj)
{
	return M0_IN(m0_conf_obj_type(obj), (&M0_CONF_RACK_TYPE,
					     &M0_CONF_ENCLOSURE_TYPE,
					     &M0_CONF_CONTROLLER_TYPE,
					     &M0_CONF_DISK_TYPE));
}

static int flset_diter_init(struct m0_conf_diter      *it,
			    struct m0_conf_filesystem *fs)
{
	struct m0_confc *confc = m0_confc_from_obj(&fs->cf_obj);

	return m0_conf_diter_init(it, confc, &fs->cf_obj,
				  M0_CONF_FILESYSTEM_RACKS_FID,
				  M0_CONF_RACK_ENCLS_FID,
				  M0_CONF_ENCLOSURE_CTRLS_FID,
				  M0_CONF_CONTROLLER_DISKS_FID);
}

static int flset_diter_next(struct m0_conf_diter *it)
{
	return m0_conf_diter_next_sync(it, obj_is_flset_target);
}

static void flset_diter_fini(struct m0_conf_diter *it)
{
	m0_conf_diter_fini(it);
}

static void pver_recd_update(struct m0_conf_obj *obj)
{
	unsigned              level = m0_conf_pver_level(obj);
	struct m0_conf_pver **pvers = m0_conf_pvers(obj);

	if (obj->co_ha_state == M0_NC_ONLINE) {
		for (; *pvers != NULL; ++pvers)
			M0_CNT_DEC((*pvers)->pv_u.subtree.pvs_recd[level]);
	} else if (M0_IN(obj->co_ha_state, (M0_NC_TRANSIENT, M0_NC_FAILED))) {
		for (; *pvers != NULL; ++pvers)
			M0_CNT_INC((*pvers)->pv_u.subtree.pvs_recd[level]);
	}
}

static void
flset_update(struct m0_flset *flset, struct m0_conf_obj *obj)
{
	if (obj->co_ha_state == M0_NC_ONLINE &&
	    m0_flset_tlist_contains(&flset->fls_objs, obj)) {
		m0_flset_tlist_del(obj);
		pver_recd_update(obj);
	} else if (M0_IN(obj->co_ha_state, (M0_NC_TRANSIENT, M0_NC_FAILED)) &&
		   !m0_flset_tlist_contains(&flset->fls_objs, obj)) {
		m0_flset_tlist_add_tail(&flset->fls_objs, obj);
		pver_recd_update(obj);
	}
}

static bool flset_hw_obj_failure_cb(struct m0_clink *cl)
{
	struct m0_conf_obj *obj = container_of(cl->cl_chan, struct m0_conf_obj,
					       co_ha_chan);
	struct m0_flset    *flset = container_of(cl, struct flset_clink,
						 fcl_link)->fcl_parent;
	M0_PRE(obj_is_flset_target(obj));
	M0_PRE(obj->co_status == M0_CS_READY);

	flset_update(flset, obj);
	return false;
}

static int flset_target_objects_nr(struct m0_conf_filesystem *fs)
{
	struct m0_conf_diter it;
	int                  objs_nr = 0;
	int                  rc;

	rc = flset_diter_init(&it, fs);
	if (rc == 0)
		while ((rc = flset_diter_next(&it)) == M0_CONF_DIRNEXT)
			objs_nr++;
	flset_diter_fini(&it);
	return rc ? M0_ERR(rc) : objs_nr;
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
				 struct flset_clink *link,
				 struct m0_conf_obj *obj)
{
	m0_clink_init(&link->fcl_link, flset_hw_obj_failure_cb);
	m0_clink_add_lock(&obj->co_ha_chan, &link->fcl_link);
	link->fcl_parent = flset;
	link->fcl_fid = obj->co_id;
}

static struct flset_clink *flset_clink_get(struct m0_flset *flset, int idx)
{
	return &flset->fls_links[idx];
}

static int flset_clinks_register(struct m0_flset           *flset,
				 struct m0_conf_filesystem *fs)
{
	struct m0_conf_diter  it;
	struct m0_conf_obj   *obj;
	struct flset_clink   *fls_cl;
	int                   objs_nr = 0;
	int                   rc;

	M0_ENTRY();

	objs_nr = flset_target_objects_nr(fs);
	if (objs_nr < 0)
		return M0_ERR(objs_nr);
	if (objs_nr == 0) {
		M0_LOG(M0_WARN, "No objs for failure set, fs fid"FID_F,
				FID_P(&fs->cf_obj.co_id));
		return M0_RC(0);
	}

	M0_ALLOC_ARR(flset->fls_links, objs_nr);
	if (flset->fls_links == NULL)
		return M0_ERR(-ENOMEM);
	flset->fls_links_nr = 0;

	rc = flset_diter_init(&it, fs);
	if (rc != 0) {
		m0_free(flset->fls_links);
		return M0_ERR(rc);
	}

	/* For every conf object add tracking clink on its HA channel. */
	while ((rc = flset_diter_next(&it)) == M0_CONF_DIRNEXT) {
		obj = m0_conf_diter_result(&it);
		fls_cl = flset_clink_get(flset, flset->fls_links_nr);
		m0_conf_obj_get_lock(obj);
		flset_clink_init_add(flset, fls_cl, obj);
		flset->fls_links_nr++;
	}
	M0_ASSERT(ergo(rc == 0, flset->fls_links_nr == objs_nr));
	flset_diter_fini(&it);

	if (rc != 0)
		flset_clinks_deregister(flset);
	return M0_RC(rc);
}

static int flset_fill(struct m0_flset           *flset,
		      struct m0_rpc_session     *ha_session,
		      struct m0_conf_filesystem *fs)

{
	struct m0_confc *confc = m0_confc_from_obj(&fs->cf_obj);
	int              rc;

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
	flset_clinks_delete(flset);
	M0_LEAVE();
	return true;
}

static bool flset_conf_ready_cb(struct m0_clink *clink)
{
	struct m0_flset      *flset = container_of(clink, struct m0_flset,
						   fls_conf_ready);
	struct m0_reqh       *reqh = container_of(clink->cl_chan,
						  struct m0_reqh,
						  rh_conf_cache_ready);
	struct m0_conf_cache *cache = &reqh->rh_rconfc.rc_confc.cc_cache;
	struct flset_clink   *fls_cl;
	struct m0_conf_obj   *obj;
	int                   i;

	M0_ENTRY("flset %p", flset);
	/*
	 * @todo the code should process any updates in the configuration tree.
	 * Currently it expects that an interested object wasn't removed from
	 * the tree or new object (m0_conf_sdev) was added.
	 */
	for (i = 0; i < flset->fls_links_nr; i++) {
		fls_cl = flset_clink_get(flset, i);
		obj = m0_conf_cache_lookup(cache, &fls_cl->fcl_fid);
		M0_ASSERT_INFO(_0C(obj != NULL) &&
			       _0C(m0_conf_obj_invariant(obj)),
			       "flset_clink->flc_fid "FID_F,
			       FID_P(&fls_cl->fcl_fid));
		m0_conf_obj_get_lock(obj);
		flset_clink_init_add(flset, fls_cl, obj);
	}
	M0_LEAVE();
	return true;
}
M0_INTERNAL int m0_flset_build(struct m0_flset           *flset,
			       struct m0_rpc_session     *ha_session,
			       struct m0_conf_filesystem *fs)
{
	struct m0_reqh *reqh = container_of(flset, struct m0_reqh,
					    rh_failure_set);
	int             rc;

	M0_ENTRY();
	M0_PRE(m0_reqh_invariant(reqh));
	m0_flset_tlist_init(&flset->fls_objs);
	rc = flset_fill(flset, ha_session, fs);
	if (rc != 0) {
		m0_flset_tlist_fini(&flset->fls_objs);
		return M0_ERR(rc);
	}
	m0_clink_init(&flset->fls_conf_expired, flset_conf_expired_cb);
	m0_clink_init(&flset->fls_conf_ready, flset_conf_ready_cb);
	m0_clink_add_lock(&reqh->rh_conf_cache_exp, &flset->fls_conf_expired);
	m0_clink_add_lock(&reqh->rh_conf_cache_ready, &flset->fls_conf_ready);
	return M0_RC(0);
}

M0_INTERNAL void m0_flset_destroy(struct m0_flset *flset)
{
	struct m0_conf_obj *obj;

	M0_ENTRY();
	flset_clinks_deregister(flset);
	m0_tl_for(m0_flset, &flset->fls_objs, obj) {
		m0_flset_tlist_del(obj);
	} m0_tlist_endfor;
	m0_flset_tlist_fini(&flset->fls_objs);
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
