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

/**
 * @addtogroup hash
 * @{
 */

#include "lib/bob.h"	/* m0_bob_type */
#include "lib/hash.h"   /* m0_hashlist */
#include "lib/errno.h"  /* Include appropriate errno.h header. */
#include "lib/arith.h"	/* min64u() */
#include "lib/memory.h" /* M0_ALLOC_ARR() */

static const struct m0_bob_type hashlist_bobtype;
M0_BOB_DEFINE(static, &hashlist_bobtype, m0_hashlist);

static const struct m0_bob_type hashlist_bobtype = {
	.bt_name         = "hashlist",
	.bt_magix_offset = offsetof(struct m0_hashlist, hl_magic),
	.bt_magix        = M0_LIB_HASHLIST_MAGIC,
	.bt_check        = NULL,
};

static bool hashlist_invariant(const struct m0_hashlist *hlist);

M0_INTERNAL int m0_hashbucket_alloc_init(struct m0_hashlist *hlist,
				         uint64_t            bucket_id)
{
	M0_PRE(hlist != NULL);
	M0_PRE(hlist->hl_buckets != NULL);
	M0_PRE(hlist->hl_buckets[bucket_id] == NULL);

	M0_ALLOC_PTR(hlist->hl_buckets[bucket_id]);
	if (hlist->hl_buckets[bucket_id] == NULL)
		return -ENOMEM;

	hlist->hl_buckets[bucket_id]->hb_bucket_id = bucket_id;
	m0_tlist_init(hlist->hl_tldescr,
		      &hlist->hl_buckets[bucket_id]->hb_objects);
	hlist->hl_buckets[bucket_id]->hb_hlist = hlist;
	return 0;
}

M0_INTERNAL void m0_hashbucket_dealloc_fini(struct m0_hashbucket *bucket)
{
	M0_PRE(bucket != NULL);
	M0_PRE(bucket->hb_hlist != NULL);

	bucket->hb_hlist->hl_buckets[bucket->hb_bucket_id] = NULL;
	m0_tlist_fini(bucket->hb_hlist->hl_tldescr, &bucket->hb_objects);
	bucket->hb_hlist = NULL;
	m0_free(bucket);
}

static uint64_t hashlist_key_get(const struct m0_hashlist *hlist,
				 const void               *obj)
{
	return *(uint64_t *)(obj + hlist->hl_key_offset);
}

static bool hashlist_invariant(const struct m0_hashlist *hlist)
{
	return
		m0_hashlist_bob_check(hlist) &&
		hlist->hl_bucket_nr >  0 &&
		hlist->hl_hash_func != NULL &&
		hlist->hl_buckets   != NULL &&
		hlist->hl_tldescr   != NULL;
}

M0_INTERNAL int m0_hashlist_init(struct m0_hashlist       *hlist,
		   		  uint64_t (*hfunc)
				 (const struct m0_hashlist *hlist,
				  uint64_t                  key),
				 uint64_t                  bucket_nr,
				 size_t                    key_offset,
				 const struct m0_tl_descr *descr)
{
	M0_PRE(hlist != NULL);
	M0_PRE(hfunc != NULL);
	M0_PRE(bucket_nr > 0);
	M0_PRE(descr != NULL);

	m0_hashlist_bob_init(hlist);

	/*
	 * Number of buckets is determined based on minimum of
	 * - number of objects to be stored.
	 * - max number of buckets that can fit into M0_0VEC_ALIGN (4K)
	 *   segment.
	 * This helps in keeping buckets localized in one page while operating
	 * in linux kernel.
	 */
	hlist->hl_key_offset = key_offset;
	hlist->hl_hash_func  = hfunc;
	hlist->hl_tldescr    = descr;
	hlist->hl_bucket_nr  = min64u(bucket_nr, M0_0VEC_ALIGN /
				      sizeof(struct m0_hashbucket *));
	M0_ALLOC_ARR(hlist->hl_buckets, hlist->hl_bucket_nr);
	if (hlist->hl_buckets == NULL)
		return -ENOMEM;

	M0_POST(hashlist_invariant(hlist));
	return 0;
}

M0_INTERNAL int m0_hashlist_add(struct m0_hashlist *hlist, void *obj)
{
	int      rc;
	uint64_t bucket_id;

	M0_PRE(hashlist_invariant(hlist));
	M0_PRE(obj != NULL);

	bucket_id = hlist->hl_hash_func(hlist, hashlist_key_get(hlist, obj));

	/*
	 * Allocates and initializes the bucket if it is not
	 * initialized already.
	 */
	if (hlist->hl_buckets[bucket_id] == NULL) {
		rc = m0_hashbucket_alloc_init(hlist, bucket_id);
		if (rc != 0)
			return rc;
	}
	m0_tlist_add(hlist->hl_tldescr,
		     &hlist->hl_buckets[bucket_id]->hb_objects, obj);
	M0_POST(hashlist_invariant(hlist));
	M0_POST(m0_tlink_is_in(hlist->hl_tldescr, obj));

	return 0;
}

M0_INTERNAL void m0_hashlist_del(struct m0_hashlist *hlist, void *obj)
{
	uint64_t bucket_id;

	M0_PRE(hashlist_invariant(hlist));
	M0_PRE(obj != NULL);

	bucket_id = hlist->hl_hash_func(hlist, hashlist_key_get(hlist, obj));

	if (hlist->hl_buckets[bucket_id] == NULL)
		return;
	m0_tlist_del(hlist->hl_tldescr, obj);

	/* Finalizes and deallocates the bucket if it is empty. */
	if (m0_tlist_is_empty(hlist->hl_tldescr,
			      &hlist->hl_buckets[bucket_id]->hb_objects))
		m0_hashbucket_dealloc_fini(hlist->hl_buckets[bucket_id]);

	M0_POST(hashlist_invariant(hlist));
	M0_POST(!m0_tlink_is_in(hlist->hl_tldescr, obj));
}

M0_INTERNAL void *m0_hashlist_lookup(const struct m0_hashlist *hlist,
				     uint64_t                  key)
{
	void                 *scan;
	uint64_t              bucket_id;
	struct m0_hashbucket *bucket;

	M0_PRE(hashlist_invariant(hlist));

	bucket_id = hlist->hl_hash_func(hlist, key);
	bucket    = hlist->hl_buckets[bucket_id];
	if (bucket == NULL)
		return NULL;

	m0_tlist_for (hlist->hl_tldescr, &bucket->hb_objects, scan) {
		if (hashlist_key_get(hlist, scan) == key)
			break;
	} m0_tlist_endfor;

	return scan;
}

M0_INTERNAL void m0_hashlist_fini(struct m0_hashlist *hlist)
{
	uint64_t              nr;
	struct m0_hashbucket *bucket;

	M0_PRE(hashlist_invariant(hlist));

	for (nr = 0; nr < hlist->hl_bucket_nr; ++nr) {
		bucket = hlist->hl_buckets[nr];
		if (bucket != NULL)
			m0_hashbucket_dealloc_fini(bucket);
	}
	m0_free(hlist->hl_buckets);
	m0_hashlist_bob_fini(hlist);
	hlist->hl_buckets   = NULL;
	hlist->hl_bucket_nr = 0;
	hlist->hl_tldescr   = NULL;
	hlist->hl_hash_func = NULL;
}

M0_INTERNAL bool m0_hashlist_is_empty(const struct m0_hashlist *hlist)
{
	uint64_t nr;

	M0_PRE(hashlist_invariant(hlist));

	for (nr = 0; nr < hlist->hl_bucket_nr; ++nr) {
		if (hlist->hl_buckets[nr] != NULL &&
		    !m0_tlist_is_empty(hlist->hl_tldescr,
			    &hlist->hl_buckets[nr]->hb_objects))
			break;
	}
	return nr == hlist->hl_bucket_nr;
}

M0_INTERNAL uint64_t m0_hashlist_length(const struct m0_hashlist *hlist)
{
	uint64_t nr;
	uint64_t len = 0;

	M0_PRE(hashlist_invariant(hlist));

	for (nr = 0; nr < hlist->hl_bucket_nr; ++nr) {
		if (hlist->hl_buckets[nr] != NULL)
			len += m0_tlist_length(hlist->hl_tldescr,
					&hlist->hl_buckets[nr]->hb_objects);
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
