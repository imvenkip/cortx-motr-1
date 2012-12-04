/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

#ifndef __MERO_STOB_CACHE_H__
#define __MERO_STOB_CACHE_H__

/**
 * @defgroup stobcache Stob caching.
 *
 * This module provides a simple interface for stob types that want to cache
 * stobs in memory.
 *
 * @{
 */

#include "lib/tlist.h"
#include "lib/types.h"             /* uint64_t */

#include "stob/stob.h"

struct m0_stob_cacheable {
	uint64_t        ca_magix;
	struct m0_stob  ca_stob;
	struct m0_tlink ca_linkage;
};

struct m0_stob_cache {
	struct m0_tl ch_head;
};

M0_INTERNAL void m0_stob_cacheable_init(struct m0_stob_cacheable *obj,
					const struct m0_stob_id *id,
					struct m0_stob_domain *dom);
M0_INTERNAL void m0_stob_cacheable_fini(struct m0_stob_cacheable *obj);

M0_INTERNAL void m0_stob_cache_init(struct m0_stob_cache *cache);
M0_INTERNAL void m0_stob_cache_fini(struct m0_stob_cache *cache);

/** Searches for the object with a given identifier in the cache. */
M0_INTERNAL struct m0_stob_cacheable *
m0_stob_cacheable_lookup(struct m0_stob_cache *cache,
			 const struct m0_stob_id *id);

/**
 * Searches for the object with a given identifier in the cache, creates one if
 * none is found. This can be used as an implementation of
 * m0_stob_domain_op::sdo_stob_find().
 *
 * Domain read-write lock is used for synchronisation.
 */
M0_INTERNAL int m0_stob_cache_find(struct m0_stob_cache *cache,
				   struct m0_stob_domain *dom,
				   const struct m0_stob_id *id,
				   int (*init)(struct m0_stob_domain *,
					       const struct m0_stob_id *,
					       struct m0_stob_cacheable **),
				   struct m0_stob_cacheable **out);

/** @} end group stobcache */

/* __MERO_STOB_CACHE_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
