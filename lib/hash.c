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

/**
 * @addtogroup hash
 * @{
 */

#include "lib/bob.h"	/* m0_bob_type */
#include "lib/hash.h"   /* m0_htable */
#include "lib/errno.h"  /* Include appropriate errno.h header. */
#include "lib/arith.h"	/* min64u() */
#include "lib/memory.h" /* M0_ALLOC_ARR() */
#include "lib/misc.h"	/* m0_forall() */

static const struct m0_bob_type htable_bobtype;
M0_BOB_DEFINE(static, &htable_bobtype, m0_htable);

static const struct m0_bob_type htable_bobtype = {
	.bt_name         = "hashtable",
	.bt_magix_offset = offsetof(struct m0_htable, h_magic),
	.bt_magix        = M0_LIB_HASHLIST_MAGIC,
	.bt_check        = NULL,
};

static bool htable_invariant(const struct m0_ht_descr *d,
			     const struct m0_htable   *htable);

M0_INTERNAL void m0_hbucket_init(const struct m0_ht_descr *d,
				 struct m0_hbucket        *bucket)
{
	M0_PRE(bucket != NULL);
	M0_PRE(d != NULL);
	M0_PRE(d->hd_tldescr != NULL);

	m0_tlist_init(d->hd_tldescr, &bucket->hb_objects);
}

M0_INTERNAL void m0_hbucket_fini(const struct m0_ht_descr *d,
				 struct m0_hbucket        *bucket)
{
	M0_PRE(bucket != NULL);
	M0_PRE(d != NULL);
	M0_PRE(d->hd_tldescr != NULL);

	m0_tlist_fini(d->hd_tldescr, &bucket->hb_objects);
}

M0_INTERNAL void *m0_htable_key(const struct m0_ht_descr *d,
				void                     *amb)
{
	return d->hd_key(d, amb);
}

M0_INTERNAL bool m0_htable_key_eq(const struct m0_ht_descr *d,
				  void                     *key1,
				  void                     *key2)
{
	return d->hd_key_eq(key1, key2);
}

static inline uint64_t key_get(const struct m0_ht_descr *d,
			       void                     *amb)
{
	uint64_t key = 0;

	switch (d->hd_key_type) {
	case M0_HT_KEY_U8 :
		key = *(uint8_t *)m0_htable_key(d, amb);
		break;
	case M0_HT_KEY_U16:
		key = *(uint16_t *)m0_htable_key(d, amb);
		break;
	case M0_HT_KEY_U32:
		key = *(uint32_t *)m0_htable_key(d, amb);
		break;
	case M0_HT_KEY_U64:
		key = *(uint64_t *)m0_htable_key(d, amb);
		break;
	default:
		M0_IMPOSSIBLE("Invalid key type");
	}
	return key;
}

static bool hbucket_invariant(const struct m0_ht_descr *d,
			      const struct m0_hbucket  *bucket,
			      const struct m0_htable   *htable)
{
	uint64_t  index;
	void     *amb;
	uint64_t  key;

	index = bucket - htable->h_buckets;

	return
		bucket != NULL &&
		d != NULL &&
		d->hd_tldescr != NULL &&
		m0_hbucket_forall_ol (d->hd_tldescr, amb, bucket,
			 ((void)(key = key_get(d, amb)), true) &&
			 (index == d->hd_hash_func(htable, &key)));
}

static bool htable_invariant(const struct m0_ht_descr *d,
			     const struct m0_htable   *htable)
{
	return
		m0_htable_bob_check(htable) &&
		htable->h_bucket_nr >  0 &&
		htable->h_buckets   != NULL &&
		m0_forall(i, htable->h_bucket_nr, hbucket_invariant(d,
			  &htable->h_buckets[i], htable));
}

M0_INTERNAL int m0_htable_init(const struct m0_ht_descr *d,
			       struct m0_htable         *htable,
			       uint64_t                  bucket_nr)
{
	uint64_t nr;

	M0_PRE(htable != NULL);
	M0_PRE(d != NULL);
	M0_PRE(bucket_nr > 0);

	m0_htable_bob_init(htable);

	htable->h_bucket_nr  = bucket_nr;
	M0_ALLOC_ARR(htable->h_buckets, htable->h_bucket_nr);
	if (htable->h_buckets == NULL)
		return -ENOMEM;

	for (nr = 0; nr < htable->h_bucket_nr; ++nr)
		m0_hbucket_init(d, &htable->h_buckets[nr]);
	M0_POST_EX(htable_invariant(d, htable));
	return 0;
}

M0_INTERNAL void m0_htable_add(const struct m0_ht_descr *d,
			       struct m0_htable         *htable,
			       void                     *amb)
{
	uint64_t key;
	uint64_t bucket_id;

	M0_PRE_EX(htable_invariant(d, htable));
	M0_PRE(amb != NULL);

	key = key_get(d, amb);
	bucket_id = d->hd_hash_func(htable, &key);

	m0_tlist_add(d->hd_tldescr,
		     &htable->h_buckets[bucket_id].hb_objects, amb);
	M0_POST_EX(htable_invariant(d, htable));
	M0_POST(m0_tlink_is_in(d->hd_tldescr, amb));
}

M0_INTERNAL void m0_htable_del(const struct m0_ht_descr *d,
			       struct m0_htable         *htable,
			       void                     *amb)
{
	uint64_t key;
	uint64_t bucket_id;

	M0_PRE_EX(htable_invariant(d, htable));
	M0_PRE(amb != NULL);

	key = key_get(d, amb);
	bucket_id = d->hd_hash_func(htable, &key);

	m0_tlist_del(d->hd_tldescr, amb);

	M0_POST_EX(htable_invariant(d, htable));
	M0_POST(!m0_tlink_is_in(d->hd_tldescr, amb));
}

M0_INTERNAL void *m0_htable_lookup(const struct m0_ht_descr *d,
				   const struct m0_htable   *htable,
				   void                     *key)
{
	void     *scan;
	uint64_t  k;
	uint64_t  bucket_id;

	M0_PRE_EX(htable_invariant(d, htable));

	bucket_id = d->hd_hash_func(htable, key);

	m0_tlist_for (d->hd_tldescr, &htable->h_buckets[bucket_id].hb_objects,
		      scan) {
		k = key_get(d, scan);
		if (m0_htable_key_eq(d, &k, key))
			break;
	} m0_tlist_endfor;

	return scan;
}

M0_INTERNAL void m0_htable_fini(const struct m0_ht_descr *d,
				struct m0_htable         *htable)
{
	uint64_t nr;

	M0_PRE_EX(htable_invariant(d, htable));

	for (nr = 0; nr < htable->h_bucket_nr; ++nr)
		m0_hbucket_fini(d, &htable->h_buckets[nr]);
	m0_free(htable->h_buckets);
	m0_htable_bob_fini(htable);
	htable->h_buckets   = NULL;
	htable->h_bucket_nr = 0;
}

M0_INTERNAL bool m0_htable_is_empty(const struct m0_ht_descr *d,
				    const struct m0_htable   *htable)
{
	uint64_t nr;

	M0_PRE_EX(htable_invariant(d, htable));

	for (nr = 0; nr < htable->h_bucket_nr; ++nr) {
		if (!m0_tlist_is_empty(d->hd_tldescr,
				&htable->h_buckets[nr].hb_objects))
			break;
	}
	return nr == htable->h_bucket_nr;
}

M0_INTERNAL uint64_t m0_htable_length(const struct m0_ht_descr *d,
				      const struct m0_htable   *htable)
{
	uint64_t nr;
	uint64_t len = 0;

	M0_PRE_EX(htable_invariant(d, htable));

	for (nr = 0; nr < htable->h_bucket_nr; ++nr) {
		len += m0_tlist_length(d->hd_tldescr,
				&htable->h_buckets[nr].hb_objects);
	}
	return len;
}

/** @} end of hash */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
