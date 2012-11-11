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

#include "colibri/magic.h"

#include "stob/cache.h"

/**
 * @addtogroup stobcache
 *
 * @{
 */

C2_TL_DESCR_DEFINE(cache, "cacheable stobs", static, struct c2_stob_cacheable,
		   ca_linkage, ca_magix,
		   C2_STOB_CACHEABLE_MAGIX, C2_STOB_CACHE_MAGIX);

C2_TL_DEFINE(cache, static, struct c2_stob_cacheable);

void c2_stob_cacheable_init(struct c2_stob_cacheable *obj,
			    const struct c2_stob_id *id,
			    struct c2_stob_domain *dom)
{
	c2_stob_init(&obj->ca_stob, id, dom);
	cache_tlink_init(obj);
}

void c2_stob_cacheable_fini(struct c2_stob_cacheable *obj)
{
	cache_tlink_del_fini(obj);
	c2_stob_fini(&obj->ca_stob);
}

void c2_stob_cache_init(struct c2_stob_cache *cache)
{
	cache_tlist_init(&cache->ch_head);
}

void c2_stob_cache_fini(struct c2_stob_cache *cache)
{
	struct c2_stob_cacheable *obj;

	c2_tl_for(cache, &cache->ch_head, obj) {
		c2_stob_put(&obj->ca_stob);
	} c2_tl_endfor;
	cache_tlist_fini(&cache->ch_head);
}

struct c2_stob_cacheable *c2_stob_cacheable_lookup(struct c2_stob_cache *cache,
						   const struct c2_stob_id *id)
{
	struct c2_stob_cacheable *obj;

	c2_tl_for(cache, &cache->ch_head, obj) {
		if (c2_stob_id_eq(id, &obj->ca_stob.so_id)) {
			c2_stob_get(&obj->ca_stob);
			break;
		}
	} c2_tl_endfor;
	return obj;
}

int c2_stob_cache_find(struct c2_stob_cache *cache,
		       struct c2_stob_domain *dom,
		       const struct c2_stob_id *id,
		       int (*init)(struct c2_stob_domain *,
				   const struct c2_stob_id *,
				   struct c2_stob_cacheable **),
		       struct c2_stob_cacheable **out)
{
	struct c2_stob_cacheable *obj;
	struct c2_stob_cacheable *ghost;
	int                       result;

	result = 0;
	c2_rwlock_read_lock(&dom->sd_guard);
	obj = c2_stob_cacheable_lookup(cache, id);
	c2_rwlock_read_unlock(&dom->sd_guard);

	if (obj == NULL) {
		result = (*init)(dom, id, &obj);
		if (result == 0) {
			C2_ASSERT(obj != NULL);
			c2_rwlock_write_lock(&dom->sd_guard);
			ghost = c2_stob_cacheable_lookup(cache, id);
			if (ghost == NULL)
				cache_tlist_add(&cache->ch_head, obj);
			else {
				obj->ca_stob.so_op->sop_fini(&obj->ca_stob);
				obj = ghost;
			}
			c2_stob_get(&obj->ca_stob);
			c2_rwlock_write_unlock(&dom->sd_guard);
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
