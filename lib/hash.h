/* -*- C -*- */
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
 * Original author: Anand Vidwana <anand_vidwansa@xyratex.com>
 * Original creation date: 05/21/2013
 */

#pragma once

#ifndef __MERO_LIB_HASH_H__
#define __MERO_LIB_HASH_H__

#include "lib/types.h"
#include "lib/tlist.h"

/**
 * @defgroup hash Hashed lists.
 *
 * Hashed list module provides a simple hash implementation built over
 * top of typed lists. @see tlist.
 *
 * Often, lookup for objects stored in simple tlists prove to be expensive
 * owing to lack of any efficient arrangement of objects in tlist.
 * Hash list provides a simple way to distribute objects in hash using a
 * key-value mechanism, which enhances lookup time of objects.
 *
 * Hash list contains array of hash buckets which contain bucket id
 * and a tlist of objects.
 * Every object is supposed to provide a key, based on which its location
 * in hash list is decided. The caller is supposed to provide a hash function
 * which is used to calculate bucket id in which object will lie.
 *
 * Similar to tlists, hash list is a simple algorithmic module. It does not
 * deal with liveness or concurrency and other such issues. Caller is supposed
 * to control liveness and use proper synchronization primitives to handle
 * concurrency.
 *
 * A good hash function can ensure good distribution of objects
 * throughout the hash list, thus owing to efficient operation of hash.
 *
 * Consider a scenario with struct bar containing multiple objects of
 * struct foo.
 *
 * @code
 *
 * struct bar {
 *         ...
 *         // Hash list used to store multiple foo-s.
 *         struct m0_hashlist b_foohash;
 *         ...
 * };
 *
 * struct foo {
 *         // Magic to validate sanity of object.
 *         uint64_t        f_magic;
 *
 *         // Key used to find out appropriate bucket.
 *         uint64_t        f_hash_key;
 *
 *         ...
 *         // linkage into list of foo-s hanging off bar::b_foohash.
 *         struct m0_tlink f_link;
 * };
 *
 * - Now, define the tlist descriptor.
 *
 *   M0_TL_DESCR_DEFINE(foohash, "Hash of foo-s", static, struct foo,
 *                      f_link, f_magic, magix, M0_LIB_HASHBUCKET_MAGIC);
 *
 * - Define a hash function which will take care of distributing objects
 *   throughtout the hash buckets.
 *
 *   uint64_t hash_func(uint64_t key)
 *   {
 *           return key % bucket_nr;
 *   }
 *
 * - Now initialize the m0_hashlist like
 *
 *   m0_hashlist_init(&bar->b_foohash, hash_func, bucket_nr,
 *                    offsetof(struct foo, f_key, &foohash_tl);
 *
 * Now, foo objects can be added/removed to/from bar::b_foohash using
 * APIs like m0_hashlist_add() and m0_hashlist_del().
 *
 * Also, lookup through hash can be done using API like m0_hashlist_lookup().
 *
 * @endcode
 *
 * Macros like m0_hashbucket_forall() and m0_hashlist_forall() can be used
 * to evaluate a certain expression for all objects in hashbucket/hashlist.
 *
 * m0_hashlist_for() and m0_hashlist_endfor() can be used to have a loop
 * over all objects in hashlist.
 *
 * @{
 */

/**
 * Represents a simple hash bucket.
 */
struct m0_hashbucket {
	/**
	 * Bucket id. It is calculated by supplying key (provided by user)
	 * to m0_hashlist::hl_hash_func().
	 * Typically for struct m0_hashlist, m0_fid::f_key is used as hash key.
	 * During initialization, this key is set to an invalid value.
	 * As and when elements are added to hash, this key is updated.
	 */
	uint64_t            hb_bucket_id;

	/**
	 * List of target_ioreq objects which share
	 * target_ioreq::ti_fid::f_key.
	 * A single m0_tl_descr object would be used by all
	 * m0_hashbucket::hb_objects lists in a single m0_hash object.
	 */
	struct m0_tl        hb_objects;

	/** Backlink to parent m0_hashlist structure. */
	struct m0_hashlist *hb_hlist;
};

/**
 * Allocates and initializes m0_hashbucket structure.
 * @pre   hlist != NULL && hlist->hl_buckets != NULL &&
 *        hlist->hl_buckets[bucket_id] == NULL.
 * @post  hlist->hl_buckets[bucket_id] != NULL.
 */
M0_INTERNAL int hashbucket_alloc_init(struct m0_hashlist *hlist,
				      uint64_t            bucket_id);

/**
 * Finalizes and deallocates a m0_hashbucket structure.
 * @pre bucket != NULL.
 */
M0_INTERNAL void hashbucket_dealloc_fini(struct m0_hashbucket *bucket);

struct m0_hashlist;

/**
 * A simple hash data structure which helps to avoid the linear search
 * of whole list of objects.
 * However considering that linux kernel can not guarantee more than one
 * contiguous pages during memory allocation, the upper threshold of
 * number of buckets is limited by page_size / sizeof(m0_hashbucket *).
 */
struct m0_hashlist {
	/** Magic value. Holds M0_LIB_HASHLIST_MAGIC.  */
	uint64_t                   hl_magic;

	/** Number of hash buckets used. */
	uint64_t                   hl_bucket_nr;

	/** Offset of key field in ambient structure. */
	size_t                     hl_key_offset;

	/**
	 * Array of hash buckets. Hash buckets are supposed to be
	 * indexed in increasing order of hash key.
	 * Ergo, the very first bucket will have bucket id 0, the next one
	 * will have bucket id 1 and so on.
	 */
	struct m0_hashbucket     **hl_buckets;

	/** tlist descriptor used for m0_hashbucket::hb_objects tlist. */
	const struct m0_tl_descr  *hl_tldescr;

	/** Hash function. Has to be provided by user. */
	uint64_t (*hl_hash_func)  (const struct m0_hashlist *hlist,
			           uint64_t               key);
};

/**
 * Initializes a hashlist.
 * @param bucket_nr Number of buckets that will be housed in this m0_hashlist.
 *        Max number of buckets has upper threshold of 512 buckets.
 * @param key_offset Offset of key field in ambient object.
 *        This key is used in operations like add, del, lookup &c.
 * @param hfunc Hash function used to calculate bucket id.
 * @param descr tlist descriptor used for tlist in hash buckets.
 * @pre   hlist != NULL &&
 *        hfunc != NULL &&
 *        bucket_nr > 0    &&
 *        descr != NULL.
 * @post hlist->hl_magic == M0_LIB_HASHLIST_MAGIC &&
 *       hlist->hl_bucket_nr > 0 &&
 *       hlist->hl_hash_func == hfunc &&
 *       hlist->hl_tldescr == descr.
 */
M0_INTERNAL int m0_hashlist_init(struct m0_hashlist *hlist,
				 uint64_t (*hfunc)
				 (const struct m0_hashlist *hlist,
				  uint64_t                  key),
				 uint64_t                   bucket_nr,
				 size_t                     key_offset,
				 const struct m0_tl_descr  *descr);

/**
 * Finalizes a struct m0_hashlist.
 * @pre  hlist != NULL &&
 *       hlist->hl_magic == M0_LIB_HASHLIST_MAGIC &&
 *       hlist->hl_buckets != NULL.
 * @post hlist->buckets == NULL &&
 *       hlist->bucket_nr == 0.
 */
M0_INTERNAL void m0_hashlist_fini(struct m0_hashlist *hlist);

/**
 * Adds an object to hash list.
 * The key must be set in object at specified location in order to
 * identify the bucket.
 * @pre  hlist != NULL &&
 *       obj   != NULL &&
 *       hlist->hl_buckets != NULL.
 */
M0_INTERNAL int m0_hashlist_add(struct m0_hashlist *hlist, void *obj);

/**
 * Removes an object from hash list.
 * The key must be set in object at specified location in order to
 * identify the bucket.
 * @pre hlist != NULL &&
 *      obj   != NULL &&
 *      hlist->hl_buckets != NULL.
 */
M0_INTERNAL void m0_hashlist_del(struct m0_hashlist *hlist, void *obj);

/**
 * Looks up if given object is present in hash list based on input key.
 * Returns ambient object on successful lookup, returns NULL otherwise.
 * @pre hlist != NULL &&
 *      hlist->hl_buckets != NULL.
 */
M0_INTERNAL void *m0_hashlist_lookup(const struct m0_hashlist *hlist,
				     uint64_t		       key);

/** Returns if m0_hashlist contains any objects. */
M0_INTERNAL bool m0_hashlist_is_empty(const struct m0_hashlist *hlist);

/** Returns number of objects stored within m0_hashlist. */
M0_INTERNAL uint64_t m0_hashlist_length(const struct m0_hashlist *hlist);

/**
 * Iterates over the members of a m0_hashbucket and performs given operation
 * for all of them.
 */
#define m0_hashbucket_forall(name, var, bucket, ...)			    \
({									    \
	m0_tl_forall(name, var, &bucket->hb_objects, ({ __VA_ARGS__ ; }));  \
})

/**
 * Iterates over all hashbuckets and invokes m0_hashbucket_forall() for all
 * buckets.
 */
#define m0_hashlist_forall(name, var, hlist, ...)			    \
({									    \
	uint64_t cnt;							    \
	typeof (hlist) hl = (hlist);					    \
 									    \
	for (cnt = 0; cnt < hl->hl_bucket_nr; ++cnt)	{		    \
		if (hl->hl_buckets[cnt] != NULL &&			    \
		    (!(m0_hashbucket_forall(name, var, hl->hl_buckets[cnt], \
					 ({ __VA_ARGS__ ; })))))	    \
			break;					 	    \
	}								    \
	cnt == hl->hl_bucket_nr;					    \
})

/**
 * An open ended version of loop over all objects in all hash buckets
 * in a m0_hashlist.
 * This loop has to be closed using hashlist_endfor() macro.
 */
#define m0_hashlist_for(name, var, hlist)				    \
({									    \
	uint64_t __cnt;							    \
	typeof (hlist) hl = (hlist);					    \
									    \
	for (__cnt = 0; __cnt < hl->hl_bucket_nr; ++__cnt) {		    \
		if (hl->hl_buckets[__cnt] != NULL) {			    \
			m0_tl_for(name, &hl->hl_buckets[__cnt]->hb_objects, \
				  var)

#define m0_hashlist_endfor m0_tl_endfor; } }; })

/** @} end of hash */

#endif /* __MERO_LIB_HASH_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
