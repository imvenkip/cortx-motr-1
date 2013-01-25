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
 * Original creation date: 09-Jan-2013
 */
#pragma once
#ifndef __MERO_CONF_CACHE_H__
#define __MERO_CONF_CACHE_H__

#include "conf/reg.h"  /* m0_conf_reg */
#include "lib/mutex.h" /* m0_mutex */

/**
 * @page conf-fspec-cache Configuration Cache
 *
 * Confc and confd maintain independent in-memory caches of
 * configuration data.  Configuration cache consists of a set of
 * dynamically allocated configuration objects, joined together by
 * relations into a directed acyclic graph (DAG), and a @ref
 * conf-fspec-reg "registry of cached objects", that maps object
 * identities to memory addresses of these objects.
 *
 * Configuration cache is represented by m0_conf_cache structure.
 *
 * @note Configuration consumers should not #include "conf/cache.h".
 *       This is an "internal" API, used by confc and confd
 *       implementations.
 *
 * @section conf-fspec-cache-thread Concurrency control
 *
 * Prior to modifying cached configuration objects,
 * m0_conf_cache::ca_lock should be obtained.  This mutex is also used
 * by confc code to protect the ambient instance of m0_confc and to
 * initialise m0_conf_obj::co_chan.
 *
 * @see @ref conf_dfspec_cache "Detailed Functional Specification",
 *      @ref conf-fspec-preload
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
	/** Registry of cached configuration objects. */
	struct m0_conf_reg ca_registry;

	/**
	 * Cache lock.
	 *
	 * Protects the DAG of cached configuration objects from
	 * concurrent modifications.
	 *
	 * @see confc-lspec-thread
	 */
	struct m0_mutex    ca_lock;
};

M0_INTERNAL void m0_conf_cache_init(struct m0_conf_cache *cache);
M0_INTERNAL void m0_conf_cache_fini(struct m0_conf_cache *cache);

/** @} conf_dfspec_cache */
#endif /* __MERO_CONF_CACHE_H__ */
