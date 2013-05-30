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
 * Original creation date: 05/28/2013
 */

#include "lib/bob.h"	/* m0_bob_type */
#include "lib/hash.h"   /* m0_hashlist */
#include "lib/errno.h"  /* Include appropriate errno.h header. */
#include "ut/ut.h"  	/* M0_UT_ASSERT() */

struct bar {
	/* Holds BAR_MAGIC. */
	uint64_t           b_magic;
	int                b_rc;
	struct m0_hashlist b_hash;
};

struct foo {
	/* Holds FOO_MAGIC. */
	uint64_t        f_magic;
	uint64_t        f_hkey;
	int             f_subject;
	struct m0_tlink f_link;
};

enum {
	BUCKET_NR = 8,
	FOO_NR    = 19,
	BAR_MAGIC = 0xa817115ad15ababaULL,
	FOO_MAGIC = 0x911ea3a7096a96e5ULL,
};

static uint64_t hash_func(const struct m0_hashlist *hlist, uint64_t key)
{
	return key % hlist->hl_bucket_nr;
}

M0_TL_DESCR_DEFINE(foohash, "Hash of foos", static, struct foo,
		   f_link, f_magic, FOO_MAGIC, BAR_MAGIC);
M0_TL_DEFINE(foohash, static, struct foo);

static struct foo foos[FOO_NR];
static struct bar thebar;

void test_hash(void)
{
	int                   i;
	int                   rc;
	struct foo           *f;
	struct m0_hashbucket *hb;

	for (i = 0; i < FOO_NR; ++i) {
		foos[i].f_magic = FOO_MAGIC;
		foos[i].f_hkey  = i;
		foos[i].f_subject = 0;
		m0_tlink_init(&foohash_tl, &foos[i]);
	}

	thebar.b_magic = BAR_MAGIC;
	thebar.b_rc    = 0;
	rc = m0_hashlist_init(&thebar.b_hash, hash_func, BUCKET_NR,
			      offsetof(struct foo, f_hkey), &foohash_tl);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(thebar.b_hash.hl_magic == M0_LIB_HASHLIST_MAGIC);
	M0_UT_ASSERT(thebar.b_hash.hl_bucket_nr == BUCKET_NR);
	M0_UT_ASSERT(thebar.b_hash.hl_buckets != NULL);
	M0_UT_ASSERT(thebar.b_hash.hl_hash_func == hash_func);

	rc = m0_hashlist_add(&thebar.b_hash, &foos[0]);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_hashlist_length(&thebar.b_hash) == 1);
	M0_UT_ASSERT(!m0_hashlist_is_empty(&thebar.b_hash));
	M0_UT_ASSERT(m0_hashlist_lookup(&thebar.b_hash, 0) == &foos[0]);
	M0_UT_ASSERT(m0_hashlist_lookup(&thebar.b_hash, 1) == NULL);

	M0_UT_ASSERT(thebar.b_hash.hl_buckets[0] != NULL);
	M0_UT_ASSERT(thebar.b_hash.hl_buckets[0]->hb_bucket_id == 0);
	M0_UT_ASSERT(!m0_tlist_is_empty(&foohash_tl, &thebar.b_hash.
				        hl_buckets[0]->hb_objects));
	M0_UT_ASSERT(thebar.b_hash.hl_buckets[0]->hb_hlist == &thebar.b_hash);

	m0_hashlist_del(&thebar.b_hash, &foos[0]);
	M0_UT_ASSERT(m0_hashlist_is_empty(&thebar.b_hash));
	M0_UT_ASSERT(m0_hashlist_length(&thebar.b_hash) == 0);
	M0_UT_ASSERT(m0_hashlist_lookup(&thebar.b_hash, foos[0].f_hkey) == NULL);
	M0_UT_ASSERT(thebar.b_hash.hl_buckets[0] == NULL);

	for (i = 0; i < FOO_NR; ++i) {
		rc = m0_hashlist_add(&thebar.b_hash, &foos[i]);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(m0_tlink_is_in(&foohash_tl, &foos[i]));
	}
	M0_UT_ASSERT(m0_hashlist_length(&thebar.b_hash) == FOO_NR);

	for (i = 0; i < BUCKET_NR; ++i) {
		hb = thebar.b_hash.hl_buckets[i];
		M0_UT_ASSERT(hb != NULL);
		M0_UT_ASSERT(hb->hb_bucket_id == i);
		M0_UT_ASSERT(!m0_tlist_is_empty(&foohash_tl, &hb->hb_objects));
		M0_UT_ASSERT(m0_hashbucket_forall(foohash, f, hb,
			     f->f_hkey % BUCKET_NR == hb->hb_bucket_id));
	}
	M0_UT_ASSERT(m0_hashlist_forall(foohash, f, &thebar.b_hash,
		     f->f_subject == 0));

	m0_hashlist_for(foohash, f, &thebar.b_hash) {
		f->f_subject = 1;
	} m0_hashlist_endfor;

	M0_UT_ASSERT(m0_hashlist_forall(foohash, f, &thebar.b_hash,
		     f->f_subject == 1));

	for (i = 0; i < FOO_NR; ++i) {
		m0_hashlist_del(&thebar.b_hash, &foos[i]);
		M0_UT_ASSERT(m0_hashlist_length(&thebar.b_hash) ==
			     FOO_NR - (i + 1));
		M0_UT_ASSERT(m0_hashlist_lookup(&thebar.b_hash,
					foos[i].f_hkey) == NULL);
		M0_UT_ASSERT(!m0_tlink_is_in(&foohash_tl, &foos[i]));
	}
	M0_UT_ASSERT(m0_hashlist_length(&thebar.b_hash) == 0);
	M0_UT_ASSERT(m0_hashlist_is_empty(&thebar.b_hash));

	m0_hashlist_fini(&thebar.b_hash);
	M0_UT_ASSERT(thebar.b_hash.hl_buckets   == NULL);
	M0_UT_ASSERT(thebar.b_hash.hl_bucket_nr == 0);
	M0_UT_ASSERT(thebar.b_hash.hl_magic     == 0);
	M0_UT_ASSERT(thebar.b_hash.hl_tldescr   == NULL);
	M0_UT_ASSERT(thebar.b_hash.hl_hash_func == NULL);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
