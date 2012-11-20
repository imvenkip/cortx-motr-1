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

#ifndef __COLIBRI_STOB_CACHE_H__
#define __COLIBRI_STOB_CACHE_H__

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

struct c2_stob_cacheable {
	uint64_t        ca_magix;
	struct c2_stob  ca_stob;
	struct c2_tlink ca_linkage;
};

struct c2_stob_cache {
	struct c2_tl ch_head;
};

C2_INTERNAL void c2_stob_cacheable_init(struct c2_stob_cacheable *obj,
					const struct c2_stob_id *id,
					struct c2_stob_domain *dom);
C2_INTERNAL void c2_stob_cacheable_fini(struct c2_stob_cacheable *obj);

C2_INTERNAL void c2_stob_cache_init(struct c2_stob_cache *cache);
C2_INTERNAL void c2_stob_cache_fini(struct c2_stob_cache *cache);

/** Searches for the object with a given identifier in the cache. */
C2_INTERNAL struct c2_stob_cacheable *
c2_stob_cacheable_lookup(struct c2_stob_cache *cache,
			 const struct c2_stob_id *id);

/**
 * Searches for the object with a given identifier in the cache, creates one if
 * none is found. This can be used as an implementation of
 * c2_stob_domain_op::sdo_stob_find().
 *
 * Domain read-write lock is used for synchronisation.
 */
C2_INTERNAL int c2_stob_cache_find(struct c2_stob_cache *cache,
				   struct c2_stob_domain *dom,
				   const struct c2_stob_id *id,
				   int (*init)(struct c2_stob_domain *,
					       const struct c2_stob_id *,
					       struct c2_stob_cacheable **),
				   struct c2_stob_cacheable **out);

/** @} end group stobcache */

/* __COLIBRI_STOB_CACHE_H__ */
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
