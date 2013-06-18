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
 * Original author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 05/21/2013
 */

#pragma once

#ifndef __MERO_LIB_HASH_H__
#define __MERO_LIB_HASH_H__

#include "lib/types.h"
#include "lib/tlist.h"

/**
 * @defgroup hash Hash table.
 *
 * Hash table module provides a simple hash implementation built over
 * top of typed lists. @see tlist.
 *
 * Often, lookup for objects stored in simple tlists prove to be expensive
 * owing to lack of any efficient arrangement of objects in tlist.
 * Hash table provides a simple way to distribute objects in hash using a
 * key-value mechanism, which enhances lookup time of objects.
 *
 * Hash table contains array of hash buckets which contain a simple tlist
 * of ambient objects.
 * Every object is supposed to provide a key, based on which its location
 * in hash table is decided. The caller is supposed to provide a hash function
 * which is used to calculate bucket id in which object will lie.
 *
 * Hash keys and ambient objects are generic (kept as void *) in hash code
 * so as to support any type of keys and objects.
 * However, since hash code tries to retrieve hash key in generic functions
 * like m0_htable_add(), m0_htable_lookup() &c, there is a restriction on
 * types of keys that can be used with hash table.
 *
 * A known set of key types are supported by hash table. And the code expects
 * key type to be from this set. @see m0_ht_key_type.
 * Since type of key can be variable, key retrieval and key equal functions
 * are supposed to be provided by user. @see m0_ht_descr.
 *
 * Users are encouraged to use the type-safe interfaces defined over hash
 * table using macros like M0_HT_DEFINE().
 *
 * Similar to tlists, hash table is a simple algorithmic module. It does not
 * deal with liveness or concurrency and other such issues. Caller is supposed
 * to control liveness and use proper synchronization primitives to handle
 * concurrency.
 *
 * A good hash function can ensure good distribution of objects
 * throughout the hash table, thus owing to efficient operation of hash.
 *
 * Consider a scenario with struct bar containing multiple objects of
 * struct foo.
 *
 * @code
 *
 * struct bar {
 *         ...
 *         // Hash table used to store multiple foo-s.
 *         struct m0_htable b_foohash;
 *         ...
 * };
 *
 * struct foo {
 *         // Magic to validate sanity of object.
 *         uint64_t        f_magic;
 *
 *         // A uint64_t Key used to find out appropriate bucket.
 *         uint64_t        f_hash_key;
 *
 *         ...
 *         // linkage into list of foo-s hanging off bar::b_foohash.
 *         struct m0_tlink f_link;
 * };
 *
 * - Define a hash function which will take care of distributing objects
 *   throughtout the hash buckets and the key retrieval and key equal
 *   functions.
 *
 *   uint64_t hash_func(const struct m0_htable *htable, void *key)
 *   {
 *           uint64_t *k = (uint64_t *)key;
 *           return (*k) % bucket_nr;
 *   }
 *
 *   void *hash_key_get(const struct m0_ht_descr *d, void *amb))
 *   {
 *           struct foo *f = (struct foo *)amb;
 *           return &(f->f_hash_key);
 *   }
 *
 *   void *hash_key_eq(void *key1, void *key2)
 *   {
 *           uint64_t *k1 = (uint64_t *)key1;
 *           uint64_t *k2 = (uint64_t *)key2;
 *           return (*k1) == (*k2);
 *   }
 *
 * - Now define hash descriptor like this.
 *
 *   M0_HT_DESCR_DEFINE(foohash, "Hash of foo structures", static, struct foo,
 *                      f_link, FOO_MAGIC, BAR_MAGIC, uint64_t, f_hkey,
 *                      hash_func, hash_key_get, hash_key_eq);
 *
 *   This will take care of defining tlist descriptor internally.
 *
 *   Similarly,
 *
 *   M0_HT_DEFINE(foohash, static, struct foo, uint64_t);
 *
 *   this will take care of defining tlist APIs internally.
 *
 * - Now initialize the m0_htable like
 *
 *   m0_htable_init(&bar->b_foohash, hash_func, bucket_nr,
 *                     offsetof(struct foo, f_key, &foohash_tl);
 *
 * Now, foo objects can be added/removed to/from bar::b_foohash using
 * APIs like m0_htable_add() and m0_htable_del().
 *
 * Also, lookup through hash can be done using API like m0_htable_lookup().
 *
 * @endcode
 *
 * Macros like m0_hbucket_forall() and m0_htable_forall() can be used
 * to evaluate a certain expression for all objects in hashbucket/hashtable.
 *
 * m0_htable_for() and m0_htable_endfor() can be used to have a loop
 * over all objects in hashtable.
 *
 * @{
 */

struct m0_htable;
struct m0_hbucket;
struct m0_ht_descr;

/**
 * Represents a simple hash bucket.
 */
struct m0_hbucket {
	/**
	 * List of objects which lie in same hash bucket.
	 * A single m0_tl_descr object would be used by all
	 * m0_hbucket::hb_objects lists in a single m0_hash object.
	 */
	struct m0_tl        hb_objects;
};

/**
 * Allocates and initializes m0_hbucket structure.
 * @pre hbucket != NULL && d != NULL.
 */
M0_INTERNAL void m0_hbucket_init(const struct m0_ht_descr *d,
				 struct m0_hbucket        *hbucket);

/**
 * Finalizes and deallocates a m0_hbucket structure.
 * @pre bucket != NULL.
 */
M0_INTERNAL void m0_hbucket_fini(const struct m0_ht_descr *d,
				 struct m0_hbucket        *bucket);

/**
 * A simple hash data structure which helps to avoid the linear search
 * of whole list of objects.
 * However considering that linux kernel can not guarantee more than one
 * contiguous pages during memory allocation, the upper threshold of
 * number of buckets is limited by page_size / sizeof(m0_hbucket *).
 */
struct m0_htable {
	/** Magic value. Holds M0_LIB_HASHLIST_MAGIC.  */
	uint64_t                  h_magic;

	/** Number of hash buckets used. */
	uint64_t                  h_bucket_nr;

	/**
	 * Array of hash buckets. Hash buckets are supposed to be
	 * indexed in increasing order of hash key.
	 * Ergo, the very first bucket will have bucket id 0, the next one
	 * will have bucket id 1 and so on.
	 */
	struct m0_hbucket        *h_buckets;
};

/**
 * Possible permissible types for keys used in hash tables.
 * Such a table has to be used since type of key has to be found
 * out in generic code (m0_htable_add, m0_htable_del, m0_htable_lookup).
 * Since key is supposed to be void *, the actual type of key can be
 * found out only by using typeof() macro, where name of key_field
 * is needed, along with ambient type.
 * But all ambient objects and keys are used as void * in generic code,
 * hence typeof() macro can not yield results.
 */
enum m0_ht_key_type {
	M0_HT_KEY_U8  = 1,
	M0_HT_KEY_U16 = 2,
	M0_HT_KEY_U32 = 4,
	M0_HT_KEY_U64 = 8,
	M0_HT_KEY_NR,
};

/**
 * Hash table descriptor. An instance of this type must be defined per
 * struct m0_htable.
 * It keeps track of tlist descriptor, offset to key field and hash function.
 */
struct m0_ht_descr {
	/** Human readable name. */
	const char               *hd_name;

	/** tlist descriptor used for m0_hbucket::hb_objects tlist. */
	const struct m0_tl_descr *hd_tldescr;

	/** Hash function. Has to be provided by user. */
	uint64_t (*hd_hash_func) (const struct m0_htable *htable,
			          void                   *key);

	void *(*hd_key)          (const struct m0_ht_descr *d,
				  void                     *amb);
	/**
	 * Key comparison routine. Since hash component supports
	 * custom made keys, the comparison routine has to be
	 * provided by user.
	 */
	bool (*hd_key_eq)        (void *key1, void *key2);

	/** Type of key in ambient structure. */
	enum m0_ht_key_type      hd_key_type;
};

M0_INTERNAL void *m0_htable_key(const struct m0_ht_descr *d,
				void                     *amb);

M0_INTERNAL bool m0_htable_key_eq(const struct m0_ht_descr *d,
				  void                     *key1,
				  void                     *key2);

/**
 * Initializes a hashtable.
 * @param bucket_nr Number of buckets that will be housed in this m0_htable.
 *        Max number of buckets has upper threshold of 512 buckets.
 * @param key_offset Offset of key field in ambient object.
 *        This key is used in operations like add, del, lookup &c.
 * @param hfunc Hash function used to calculate bucket id.
 * @param d tlist descriptor used for tlist in hash buckets.
 * @pre   htable != NULL &&
 *        hfunc  != NULL &&
 *        bucket_nr > 0    &&
 *        d != NULL.
 * @post htable->h_magic == M0_LIB_HASHLIST_MAGIC &&
 *       htable->h_bucket_nr > 0 &&
 *       htable->h_hash_func == hfunc &&
 *       htable->h_tldescr == d.
 */
M0_INTERNAL int m0_htable_init(const struct m0_ht_descr *d,
			       struct m0_htable         *htable,
			       uint64_t                  bucket_nr);

/**
 * Finalizes a struct m0_htable.
 * @pre  htable != NULL &&
 *       htable->h_magic == M0_LIB_HASHLIST_MAGIC &&
 *       htable->h_buckets != NULL.
 * @post htable->buckets == NULL &&
 *       htable->bucket_nr == 0.
 */
M0_INTERNAL void m0_htable_fini(const struct m0_ht_descr *d,
				struct m0_htable         *htable);

/**
 * Adds an object to hash table.
 * The key must be set in object at specified location in order to
 * identify the bucket.
 * @pre  htable != NULL &&
 *       amb    != NULL &&
 *       htable->h_buckets != NULL.
 */
M0_INTERNAL void m0_htable_add(const struct m0_ht_descr *d,
			       struct m0_htable         *htable,
			       void                     *amb);

/**
 * Removes an object from hash table.
 * The key must be set in object at specified location in order to
 * identify the bucket.
 * @pre htable != NULL &&
 *      amb    != NULL &&
 *      htable->h_buckets != NULL.
 */
M0_INTERNAL void m0_htable_del(const struct m0_ht_descr *d,
			       struct m0_htable         *htable,
			       void                     *amb);

/**
 * Looks up if given object is present in hash table based on input key.
 * Returns ambient object on successful lookup, returns NULL otherwise.
 * @pre htable != NULL &&
 *      htable->h_buckets != NULL.
 */
M0_INTERNAL void *m0_htable_lookup(const struct m0_ht_descr *d,
				   const struct m0_htable   *htable,
				   void                     *key);

/** Returns if m0_htable contains any objects. */
M0_INTERNAL bool m0_htable_is_empty(const struct m0_ht_descr *d,
				    const struct m0_htable   *htable);

/** Returns number of objects stored within m0_htable. */
M0_INTERNAL uint64_t m0_htable_length(const struct m0_ht_descr *d,
				      const struct m0_htable   *htable);

#define KEY_TYPE_IS_VALID(key)	\
	M0_HAS_TYPE((key), uint8_t)  || \
	M0_HAS_TYPE((key), uint16_t) ||	\
	M0_HAS_TYPE((key), uint32_t) ||	\
	M0_HAS_TYPE((key), uint64_t)

/** Defines a hashtable descriptor. */
#define M0_HT_DESCR(name, ambient_type, key_type, key_field,		\
		    hash_func, key_get, key_eq, tldescr)		\
{									\
	.hd_name       = name,						\
	.hd_key        = key_get,					\
	.hd_key_eq     = key_eq,					\
	.hd_tldescr    = tldescr,					\
	.hd_hash_func  = hash_func,					\
	.hd_key_type   = (enum m0_ht_key_type)sizeof(key_type)		\
};									\
									\
M0_BASSERT(M0_HAS_TYPE(M0_FIELD_VALUE(ambient_type, key_field),		\
		       key_type));					\
M0_BASSERT(KEY_TYPE_IS_VALID(M0_FIELD_VALUE(ambient_type, key_field)));	\
M0_BASSERT((sizeof(uint64_t) == M0_HT_KEY_U64) &&			\
	   (sizeof(uint32_t) == M0_HT_KEY_U32) &&			\
	   (sizeof(uint16_t) == M0_HT_KEY_U16) &&			\
	   (sizeof(uint8_t)  == M0_HT_KEY_U8));

/** Defines a hashtable descriptor with given scope. */
#define M0_HT_DESCR_DEFINE(name, htname, scope, amb_type,		\
			   amb_link_field, amb_magic_field,		\
			   amb_magic, head_magic, key_type,		\
			   key_field, hash_func, key_get, key_eq)	\
M0_TL_DESCR_DEFINE(name, htname, scope, amb_type, amb_link_field,	\
		   amb_magic_field, amb_magic, head_magic);		\
									\
scope const struct m0_ht_descr name ## _ht = M0_HT_DESCR(htname,	\
							 amb_type,	\
							 key_type,	\
							 key_field,	\
							 hash_func,	\
							 key_get,	\
							 key_eq,	\
							 &name ## _tl)

/** Declares a hashtable descriptr with given scope. */
#define M0_HT_DESCR_DECLARE(name, scope)				\
scope const struct m0_ht_descr name ## _ht

/**
 * Declares all functions of hash table which accepts ambient type
 * and key type as input.
 */
#define M0_HT_DECLARE(name, scope, amb_type, key_type)			\
									\
scope int name ## _htable_init(struct m0_htable *htable,		\
			       uint64_t bucket_nr);			\
scope void *name ## _htable_key(const struct m0_htable *htable,		\
				const void             *amb);		\
scope bool name ## _htable_key_eq(key_type key1, key_type key2);	\
scope void name ## _htable_add (struct m0_htable *htable, amb_type *amb);\
scope void name ## _htable_del(struct m0_htable *htable, amb_type *amb);\
scope void * name ## _htable_lookup(const struct m0_htable *htable,	\
				    key_type               *key);	\
scope void name ## _htable_fini(struct m0_htable *htable);		\
scope bool name ## _htable_is_empty(const struct m0_htable *htable);	\
scope uint64_t m0_htable_length(const struct m0_htable *htable);

/**
 * Defines all functions of hash table which accepts ambient type
 * and key type as input.
 */
#define M0_HT_DEFINE(name, scope, amb_type, key_type)			\
									\
M0_TL_DEFINE(name, scope, amb_type);					\
									\
scope __AUN int name ## _htable_init(struct m0_htable *htable,		\
				     uint64_t          bucket_nr)	\
{									\
	return m0_htable_init(&name ## _ht, htable, bucket_nr);		\
}									\
									\
scope __AUN void *name ## _htable_key(key_type               *key,	\
			              void                   *amb)	\
{									\
	return m0_htable_key(&name ## _ht, amb);			\
}									\
									\
scope __AUN bool name ## _htable_key_eq(key_type key1, key_type key2)	\
{									\
	return m0_htable_key_eq(&name ## _ht, &key1, &key2);		\
}									\
									\
scope __AUN void name ## _htable_add(struct m0_htable *htable,		\
				    amb_type         *amb)		\
{									\
	m0_htable_add(&name ## _ht, htable, amb);			\
}									\
									\
scope __AUN void name ## _htable_del(struct m0_htable *htable,		\
			             amb_type         *amb)		\
{									\
	m0_htable_del(&name ## _ht, htable, amb);			\
}									\
									\
scope __AUN void *name ## _htable_lookup(const struct m0_htable *htable,\
					 key_type               *key)	\
{									\
	return m0_htable_lookup(&name ## _ht, htable, key);		\
}									\
									\
scope __AUN void name ## _htable_fini(struct m0_htable *htable)		\
{									\
	m0_htable_fini(&name ## _ht, htable);				\
}									\
									\
scope __AUN bool name ## _htable_is_empty(const struct m0_htable *htable)\
{									\
	return m0_htable_is_empty(&name ## _ht, htable);		\
}									\
									\
scope __AUN uint64_t name ## _htable_length(const struct m0_htable *htable)\
{									\
	return m0_htable_length(&name ## _ht, htable);			\
}									\

/**
 * Iterates over the members of a m0_hbucket and performs given operation
 * for all of them.
 */
#define m0_hbucket_forall(name, var, bucket, ...)			    \
({									    \
	typeof (bucket) __bucket = (bucket);				    \
									    \
	m0_tl_forall(name, var, &__bucket->hb_objects, ({ __VA_ARGS__ ; }));\
})

/**
 * Iterates over all hashbuckets and invokes m0_hbucket_forall() for all
 * buckets.
 */
#define m0_htable_forall(name, var, htable, ...)			    \
({									    \
	uint64_t cnt;							    \
	typeof (htable) ht = (htable);					    \
									    \
	for (cnt = 0; cnt < ht->h_bucket_nr; ++cnt)	{		    \
		if (!(m0_hbucket_forall(name, var, &ht->h_buckets[cnt],     \
					 ({ __VA_ARGS__ ; }))))	            \
			break;						    \
	}								    \
	cnt == ht->h_bucket_nr;					            \
})

/**
 * An open ended version of loop over all objects in all hash buckets
 * in a m0_htable.
 * This loop has to be closed using m0_htable_endfor() macro.
 */
#define m0_htable_for(name, var, htable)				    \
({									    \
	uint64_t __cnt;							    \
	typeof (htable) ht = (htable);					    \
									    \
	for (__cnt = 0; __cnt < ht->h_bucket_nr; ++__cnt) {		    \
		m0_tl_for(name, &ht->h_buckets[__cnt].hb_objects, var)

#define m0_htable_endfor m0_tl_endfor; }; })

/**
 * Open ended version of loop over all ambient objects in a given
 * hash bucket.
 * The loop has to be closed using m0_hbucket_endfor;
 */
#define m0_hbucket_for(descr, var, bucket)				    \
({									    \
	m0_tlist_for (descr, &bucket->hb_objects, var)

#define m0_hbucket_endfor m0_tlist_endfor; })

/**
 * A loop which uses open ended version of hash bucket.
 * This can be used for invariant checking.
 */
#define m0_hbucket_forall_ol(descr, var, bucket, ...)			    \
({									    \
	m0_hbucket_for (descr, var, bucket) {				    \
		if (!({ __VA_ARGS__; }))				    \
			break;						    \
	} m0_hbucket_endfor;						    \
	var == NULL;							    \
})

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
