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
 * Original creation date: 04-Mar-2012
 */
#pragma once
#ifndef __MERO_CONF_CACHE_H__
#define __MERO_CONF_CACHE_H__

#include "lib/tlist.h"  /* m0_tl, M0_TL_DESCR_DECLARE */
#include "conf/obj.h"   /* m0_conf_objtype */

struct m0_mutex;

/**
 * @page conf-fspec-cache Configuration Cache
 *
 * Configuration cache comprises a set of dynamically allocated
 * configuration objects, interconnected into directed acyclic graph
 * (DAG).  The cache is represented by m0_conf_cache structure.
 *
 * A registry of cached configuration objects --
 * m0_con_cache::ca_registry -- performs the following functions:
 *
 *   - maps object identities to memory addresses of these objects;
 *
 *   - ensures uniqueness of configuration objects in the cache.
 *     After an object has been added to the registry, any attempt to
 *     add another one with similar identity will fail;
 *
 *   - simplifies erasing of configuration cache.
 *     m0_conf_cache_fini() frees all configuration objects that are
 *     registered. No sophisticated DAG traversal is needed.
 *
 * @note Configuration consumers should not #include "conf/cache.h".
 *       This is "internal" API, used by confc and confd
 *       implementations.
 *
 * @section conf-fspec-cache-thread Concurrency control
 *
 * m0_conf_cache::ca_lock should be acquired prior to modifying cached
 * configuration objects.
 *
 * @see @ref conf_dfspec_cache "Detailed Functional Specification"
 */

/**
 * @defgroup conf_dfspec_cache Configuration Cache
 * @brief Detailed Functional Specification.
 *
 * @see @ref conf, @ref conf-fspec-cache "Functional Specification"
 *
 * @{
 */

/** Configuration cache. */
struct m0_conf_cache {
	/**
	 * Registry of cached configuration objects.
	 * List of m0_conf_obj-s, linked through m0_conf_obj::co_cache_link.
	 */
	struct m0_tl     ca_registry;

	/** Cache lock. */
	struct m0_mutex *ca_lock;

#if 0 /* XXX USEME */
	/** Magic value. */
	uint64_t         ca_magic;
#endif
};

M0_TL_DESCR_DECLARE(m0_conf_cache, M0_EXTERN);
M0_TL_DECLARE(m0_conf_cache, M0_INTERNAL, struct m0_conf_obj);

/** Initialises configuration cache. */
M0_INTERNAL void m0_conf_cache_init(struct m0_conf_cache *cache,
				    struct m0_mutex *lock);

/**
 * Finalises configuration cache.
 *
 * m0_conf_obj_delete()s every registered configuration object.
 */
M0_INTERNAL void m0_conf_cache_fini(struct m0_conf_cache *cache);

/**
 * Adds configuration object to the cache.
 *
 * @pre  m0_mutex_is_locked(cache->ca_lock)
 * @pre  !m0_conf_cache_tlink_is_in(obj)
 */
M0_INTERNAL int m0_conf_cache_add(struct m0_conf_cache *cache,
				  struct m0_conf_obj *obj);

/**
 * Unregisters and m0_conf_obj_delete()s configuration object.
 *
 * @pre  m0_mutex_is_locked(cache->ca_lock)
 * @pre  m0_conf_cache_tlist_contains(&cache->ca_registry, obj)
 */
M0_INTERNAL void m0_conf_cache_del(const struct m0_conf_cache *cache,
				   struct m0_conf_obj *obj);

/**
 * Searches for a configuration object given its identity (type & id).
 *
 * Returns NULL if there is no such object in the cache.
 */
M0_INTERNAL struct m0_conf_obj *
m0_conf_cache_lookup(const struct m0_conf_cache *cache,
		     enum m0_conf_objtype type, const struct m0_fid *id);

/** @} conf_dfspec_cache */
#endif /* __MERO_CONF_CACHE_H__ */
