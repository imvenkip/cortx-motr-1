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
 * Original creation date: 11-Mar-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/cache.h"
#include "conf/obj_ops.h"   /* m0_conf_obj_delete */
#include "mero/magic.h"     /* M0_CONF_OBJ_MAGIC, M0_CONF_CACHE_MAGIC */
#include "lib/errno.h"      /* EEXIST */

/**
 * @defgroup conf_dlspec_cache Configuration Cache (lspec)
 *
 * The implementation of m0_conf_cache::ca_registry is based on linked
 * list data structure.
 *
 * @see @ref conf, @ref conf-lspec
 *
 * @{
 */

M0_TL_DESCR_DEFINE(m0_conf_cache, "registered m0_conf_obj-s", M0_INTERNAL,
		   struct m0_conf_obj, co_cache_link, co_gen_magic,
		   M0_CONF_OBJ_MAGIC, M0_CONF_CACHE_MAGIC);
M0_TL_DEFINE(m0_conf_cache, M0_INTERNAL, struct m0_conf_obj);

M0_INTERNAL void
m0_conf_cache_init(struct m0_conf_cache *cache, struct m0_mutex *lock)
{
	M0_ENTRY();

	m0_conf_cache_tlist_init(&cache->ca_registry);
	cache->ca_lock = lock;

	M0_LEAVE();
}

M0_INTERNAL int
m0_conf_cache_add(struct m0_conf_cache *cache, struct m0_conf_obj *obj)
{
	const struct m0_conf_obj *x;

	M0_ENTRY();
	M0_PRE(m0_mutex_is_locked(cache->ca_lock));
	M0_PRE(!m0_conf_cache_tlink_is_in(obj));

	x = m0_conf_cache_lookup(cache, obj->co_type, &obj->co_id);
	if (x == NULL) {
		m0_conf_cache_tlist_add(&cache->ca_registry, obj);
		return M0_RC(0);
	}
	return M0_RC(-EEXIST);
}

M0_INTERNAL struct m0_conf_obj *
m0_conf_cache_lookup(const struct m0_conf_cache *cache,
		     enum m0_conf_objtype type, const struct m0_buf *id)
{
	struct m0_conf_obj *obj;

	M0_ENTRY();

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		if (obj->co_type == type && m0_buf_eq(&obj->co_id, id))
			break;
	} m0_tl_endfor;

	M0_LEAVE();
	return obj;
}

static void _obj_del(struct m0_conf_obj *obj)
{
	m0_conf_cache_tlist_del(obj);

	/* Don't let concrete invariants check relations. */
	obj->co_mounted = false;

	m0_conf_obj_delete(obj);
}

M0_INTERNAL void
m0_conf_cache_del(const struct m0_conf_cache *cache, struct m0_conf_obj *obj)
{
	M0_ENTRY();
	M0_PRE(m0_mutex_is_locked(cache->ca_lock));
	M0_PRE(m0_conf_cache_tlist_contains(&cache->ca_registry, obj));

	_obj_del(obj);

	M0_LEAVE();
}

M0_INTERNAL void m0_conf_cache_fini(struct m0_conf_cache *cache)
{
	struct m0_conf_obj *obj;

	M0_ENTRY();

	m0_mutex_lock(cache->ca_lock);

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		_obj_del(obj);
	} m0_tl_endfor;
	m0_conf_cache_tlist_fini(&cache->ca_registry);

	m0_mutex_unlock(cache->ca_lock);

	M0_LEAVE();
}

/** @} conf_dlspec_cache */
#undef M0_TRACE_SUBSYSTEM
