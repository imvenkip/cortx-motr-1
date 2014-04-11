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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"

#include "stob/cache.h"

#include "mero/magic.h"

#include "stob/stob.h"	/* m0_stob */

/**
 * @addtogroup stobcache
 *
 * @{
 */

M0_TL_DESCR_DEFINE(stob_busy, "used stobs in cache", static, struct m0_stob,
		   so_cache_linkage, so_cache_magic,
		   M0_STOB_CACHE_MAGIC, M0_STOB_CACHE_HEAD_MAGIC);
M0_TL_DEFINE(stob_busy, static, struct m0_stob);

M0_TL_DESCR_DEFINE(stob_idle, "unused stobs in cache", static, struct m0_stob,
		   so_cache_linkage, so_cache_magic,
		   M0_STOB_CACHE_MAGIC, M0_STOB_CACHE_HEAD_MAGIC);
M0_TL_DEFINE(stob_idle, static, struct m0_stob);

M0_INTERNAL int m0_stob_cache_init(struct m0_stob_cache *cache,
				   uint64_t idle_size,
				   m0_stob_cache_eviction_cb_t eviction_cb)
{
	*cache = (struct m0_stob_cache){
		.sc_idle_size	= idle_size,
		.sc_idle_used	= 0,
		.sc_eviction_cb = eviction_cb,
		.sc_busy_hits	= 0,
		.sc_idle_hits	= 0,
		.sc_misses	= 0,
		.sc_evictions	= 0,
	};
	m0_mutex_init(&cache->sc_lock);
	stob_busy_tlist_init(&cache->sc_busy);
	stob_idle_tlist_init(&cache->sc_idle);
	return 0;
}

M0_INTERNAL void m0_stob_cache_fini(struct m0_stob_cache *cache)
{
	m0_stob_cache_purge(cache, cache->sc_idle_size);
	m0_stob_cache__print(cache);
	stob_idle_tlist_fini(&cache->sc_idle);
	stob_busy_tlist_fini(&cache->sc_busy);
	m0_mutex_init(&cache->sc_lock);
}


M0_INTERNAL bool m0_stob_cache__invariant(const struct m0_stob_cache *cache)
{
	return _0C(m0_stob_cache_is_locked(cache)) &&
	       _0C(cache->sc_idle_size >= cache->sc_idle_used) &&
	       M0_CHECK_EX(_0C(stob_idle_tlist_length(&cache->sc_idle) ==
			       cache->sc_idle_used));

}

static void stob_cache_evict(struct m0_stob_cache *cache,
			     struct m0_stob *stob)
{
	cache->sc_eviction_cb(cache, stob);
	++cache->sc_evictions;
}

static void stob_cache_busy_add(struct m0_stob_cache *cache,
				struct m0_stob *stob)
{
	stob_busy_tlink_init_at(stob, &cache->sc_busy);
}

static void stob_cache_idle_del(struct m0_stob_cache *cache,
				struct m0_stob *stob)
{
	stob_idle_tlink_del_fini(stob);
	--cache->sc_idle_used;
}

static void stob_cache_idle_moveto(struct m0_stob_cache *cache,
				   struct m0_stob *stob)
{
	struct m0_stob *evicted;

	stob_busy_tlink_del_fini(stob);
	if (cache->sc_idle_size == 0) {
		stob_cache_evict(cache, stob);
	} else {
		if (cache->sc_idle_size == cache->sc_idle_used) {
			evicted = stob_idle_tlist_tail(&cache->sc_idle);
			stob_cache_idle_del(cache, evicted);
			stob_cache_evict(cache, evicted);
		}
		stob_idle_tlink_init_at(stob, &cache->sc_idle);
		++cache->sc_idle_used;
	}
}

M0_INTERNAL void m0_stob_cache_add(struct m0_stob_cache *cache,
				   struct m0_stob *stob)
{
	M0_PRE(m0_stob_cache__invariant(cache));
	M0_PRE_EX(m0_stob_cache_lookup(cache, m0_stob_key_get(stob)) == NULL);

	stob_cache_busy_add(cache, stob);
}

M0_INTERNAL void m0_stob_cache_idle(struct m0_stob_cache *cache,
				   struct m0_stob *stob)
{
	M0_PRE(m0_stob_cache__invariant(cache));

	stob_cache_idle_moveto(cache, stob);
}

M0_INTERNAL struct m0_stob *m0_stob_cache_lookup(struct m0_stob_cache *cache,
						 uint64_t stob_key)
{
	struct m0_stob *stob;

	M0_PRE(m0_stob_cache__invariant(cache));

	m0_tl_for(stob_busy, &cache->sc_busy, stob) {
		if (stob_key == m0_stob_key_get(stob)) {
			++cache->sc_busy_hits;
			return stob;
		}
	} m0_tl_endfor;

	m0_tl_for(stob_idle, &cache->sc_idle, stob) {
		if (stob_key == m0_stob_key_get(stob)) {
			++cache->sc_idle_hits;
			stob_cache_idle_del(cache, stob);
			stob_busy_tlink_init_at(stob, &cache->sc_busy);
			return stob;
		}
	} m0_tl_endfor;

	++cache->sc_misses;
	return NULL;
}


M0_INTERNAL void m0_stob_cache_purge(struct m0_stob_cache *cache, int nr)
{
	struct m0_stob *stob;
	struct m0_stob *prev;

	m0_stob_cache_lock(cache);
	M0_PRE(m0_stob_cache__invariant(cache));

	stob = stob_idle_tlist_tail(&cache->sc_idle);
	for (; stob != NULL && nr > 0; --nr) {
		prev = stob_idle_tlist_prev(&cache->sc_idle, stob);
		stob_cache_idle_del(cache, stob);
		stob_cache_evict(cache, stob);
		stob = prev;
	}

	M0_POST(m0_stob_cache__invariant(cache));
	m0_stob_cache_unlock(cache);
}

M0_INTERNAL void m0_stob_cache_lock(struct m0_stob_cache *cache)
{
	m0_mutex_lock(&cache->sc_lock);
}

M0_INTERNAL void m0_stob_cache_unlock(struct m0_stob_cache *cache)
{
	m0_mutex_unlock(&cache->sc_lock);
}

M0_INTERNAL bool m0_stob_cache_is_locked(const struct m0_stob_cache *cache)
{
	return m0_mutex_is_locked(&cache->sc_lock);
}

M0_INTERNAL bool m0_stob_cache_is_not_locked(const struct m0_stob_cache *cache)
{
	return m0_mutex_is_not_locked(&cache->sc_lock);
}

M0_INTERNAL void m0_stob_cache__print(struct m0_stob_cache *cache)
{
#define LEVEL M0_DEBUG
	struct m0_stob *stob;
	int		i;

	M0_LOG(LEVEL, "m0_stob_cache %p: "
	       "sc_busy_hits = %"PRIu64", sc_idle_hits = %"PRIu64", "
	       "sc_misses = %"PRIu64", sc_evictions = %"PRIu64, cache,
	       cache->sc_busy_hits, cache->sc_idle_hits,
	       cache->sc_misses, cache->sc_evictions);
	M0_LOG(LEVEL, "m0_stob_cache %p: "
	       "sc_idle_size = %"PRIu64", sc_idle_used = %"PRIu64", ",
	       cache, cache->sc_idle_size, cache->sc_idle_used);
	M0_LOG(LEVEL, "m0_stob_cache %p: "
	       "sc_busy length = %zu, sc_idle length = %zu", cache,
	       stob_busy_tlist_length(&cache->sc_busy),
	       stob_idle_tlist_length(&cache->sc_idle));


	M0_LOG(LEVEL, "m0_stob_cache %p: sc_busy list", cache);
	i = 0;
	m0_tl_for(stob_busy, &cache->sc_busy, stob) {
		M0_LOG(LEVEL, "%d: %p, stob_key = %"PRIu64,
		       i, stob, m0_stob_key_get(stob));
		++i;
	} m0_tl_endfor;

	M0_LOG(LEVEL, "m0_stob_cache %p: sc_idle list", cache);
	i = 0;
	m0_tl_for(stob_idle, &cache->sc_idle, stob) {
		M0_LOG(LEVEL, "%d: %p, stob_key = %"PRIu64,
		       i, stob, m0_stob_key_get(stob));
		++i;
	} m0_tl_endfor;
	M0_LOG(LEVEL, "m0_stob_cache %p: end.", cache);
#undef LEVEL
}

/** @} end group stobcache */
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
