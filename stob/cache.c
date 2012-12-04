/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 11-Nov-2012
 */

#include "mero/magic.h"

#include "stob/cache.h"

/**
 * @addtogroup stobcache
 *
 * @{
 */

M0_TL_DESCR_DEFINE(cache, "cacheable stobs", static, struct m0_stob_cacheable,
		   ca_linkage, ca_magix,
		   M0_STOB_CACHEABLE_MAGIX, M0_STOB_CACHE_MAGIX);

M0_TL_DEFINE(cache, static, struct m0_stob_cacheable);

M0_INTERNAL void m0_stob_cacheable_init(struct m0_stob_cacheable *obj,
					const struct m0_stob_id *id,
					struct m0_stob_domain *dom)
{
	m0_stob_init(&obj->ca_stob, id, dom);
	cache_tlink_init(obj);
}

M0_INTERNAL void m0_stob_cacheable_fini(struct m0_stob_cacheable *obj)
{
	cache_tlink_del_fini(obj);
	m0_stob_fini(&obj->ca_stob);
}

M0_INTERNAL void m0_stob_cache_init(struct m0_stob_cache *cache)
{
	cache_tlist_init(&cache->ch_head);
}

M0_INTERNAL void m0_stob_cache_fini(struct m0_stob_cache *cache)
{
	struct m0_stob_cacheable *obj;

	m0_tl_for(cache, &cache->ch_head, obj) {
		m0_stob_put(&obj->ca_stob);
	} m0_tl_endfor;
	cache_tlist_fini(&cache->ch_head);
}

M0_INTERNAL struct m0_stob_cacheable *
m0_stob_cacheable_lookup(struct m0_stob_cache *cache,
			 const struct m0_stob_id *id)
{
	struct m0_stob_cacheable *obj;

	m0_tl_for(cache, &cache->ch_head, obj) {
		if (m0_stob_id_eq(id, &obj->ca_stob.so_id)) {
			m0_stob_get(&obj->ca_stob);
			break;
		}
	} m0_tl_endfor;
	return obj;
}

M0_INTERNAL int m0_stob_cache_find(struct m0_stob_cache *cache,
				   struct m0_stob_domain *dom,
				   const struct m0_stob_id *id,
				   int (*init)(struct m0_stob_domain *,
					       const struct m0_stob_id *,
					       struct m0_stob_cacheable **),
				   struct m0_stob_cacheable **out)
{
	struct m0_stob_cacheable *obj;
	struct m0_stob_cacheable *ghost;
	int                       result;

	result = 0;
	m0_rwlock_read_lock(&dom->sd_guard);
	obj = m0_stob_cacheable_lookup(cache, id);
	m0_rwlock_read_unlock(&dom->sd_guard);

	if (obj == NULL) {
		result = (*init)(dom, id, &obj);
		if (result == 0) {
			M0_ASSERT(obj != NULL);
			m0_rwlock_write_lock(&dom->sd_guard);
			ghost = m0_stob_cacheable_lookup(cache, id);
			if (ghost == NULL)
				cache_tlist_add(&cache->ch_head, obj);
			else {
				obj->ca_stob.so_op->sop_fini(&obj->ca_stob);
				obj = ghost;
			}
			m0_stob_get(&obj->ca_stob);
			m0_rwlock_write_unlock(&dom->sd_guard);
		}
	}
	*out = obj;
	return result;
}

/** @} end group stobcache */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
