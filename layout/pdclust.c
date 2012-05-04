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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 07/15/2010
 */

/**
 * @addtogroup pdclust
 *
 * <b>Implementation overview.</b>
 *
 * Parity de-clustering layout mapping function requires some amount of code
 * dealing with permutations, random sequences generations and conversions
 * between matrices of different shapes.
 *
 * First, as explained in the HLD, an efficient way to generate permutations
 * uniformly scattered across the set of all permutations of a given set is
 * necessary. To this end permute_column() uses a sequence of pseudo-random
 * numbers obtained from a PRNG (c2_rnd()). Few comments are in order:
 *
 * - to seed a PRNG, layout seed and tile number are hashed by a
 *   multiplicative cache (hash());
 *
 * - system PRNG cannot be used, because reproducible sequences are needed.
 *   c2_rnd() is a very simple linear congruential generator straight from
 *   TAOCP. It takes care to return higher, more random, bits of result;
 *
 * - layout behavior is quite sensitive to the PRNG properties. For example,
 *   if c2_rnd() is changed to return lower bits (result % max), resulting
 *   distribution of spare and parity units is not uniform even for large number
 *   of units. Experiments with different PRNG's are indicated.
 *
 * Once permutation's Lehmer code is generated, it has to be applied to the set
 * of columns. permute() function applies a permutation, simultaneously building
 * an inverse permutation.
 *
 * Finally, layout mapping function is defined in terms of conversions between
 * matrices of different shapes. Let's call a matrix having M columns and an
 * arbitrary (probably infinite) number of rows an M-matrix. An element of an
 * M-matrix has (row, column) coordinates. Coordinate pairs can be ordered and
 * enumerated in the "row first" lexicographical order:
 *
 *         (0, 0) < (0, 1) < ... < (0, M - 1) < (1, 0) < ...
 *
 * Function m_enc() returns the number a (row, column) element of an M-matrix
 * has in this ordering. Conversely, function m_dec() returns coordinates of the
 * element having a given number in the ordering. With the help of these two
 * function an M-matrix can be re-arranged into an N-matrix in such a way the
 * element position in the ordering remains invariant.
 *
 * Layout mapping function c2_pdclust_layout_map() performs these
 * re-arrangements in the following places:
 *
 * - to convert a parity group number to a (tile number, group in tile)
 *   pair. This is a conversion of 1-matrix to C-matrix;
 *
 * - to convert a tile from C*(N + 2*K) to L*P form. This is a conversion of
 *   (N + 2*K)-matrix to P-matrix;
 *
 * - to convert a (tile number, frame in tile) pair to a target frame
 *   number. This is a conversion of L-matrix to 1-matrix.
 *
 * Inverse layout mapping function c2_pdclust_layout_inv() performs reverse
 *  conversions.
 *
 * @{
 */

#include "lib/errno.h"
#include "lib/memory.h" /* C2_ALLOC_PTR(), C2_ALLOC_ARR(), c2_free() */
#include "lib/arith.h"  /* c2_rnd() */
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "stob/stob.h"  /* c2_stob_id_is_set() */
#include "pool/pool.h"  /* c2_pool_lookup() */
#include "layout/layout_internal.h"
#include "layout/pdclust.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	PDCLUST_MAGIC = 0x5044434C5553544CULL /* PDCLUSTL */
};

static const struct c2_bob_type pdclust_bob = {
	.bt_name         = "pdclust",
	.bt_magix_offset = offsetof(struct c2_pdclust_layout, pl_magic),
	.bt_magix        = PDCLUST_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &pdclust_bob, c2_pdclust_layout);

/**
 * "Encoding" function: returns the number that a (row, column) element of a
 * matrix with "width" columns has when elements are counted row by row. This
 * function is denoted e_{width} in the HLD.
 *
 * @see m_dec()
 */
static uint64_t m_enc(uint64_t width, uint64_t row, uint64_t column)
{
	C2_ASSERT(column < width);
	return row * width + column;
}

/**
 * "Decoding" function: returns (row, column) coordinates of a pos-th element in
 * a matrix with "width" column when elements are counted row by row. This
 * function is denoted d_{width} in the HLD.
 *
 * @see m_enc()
 */
static void m_dec(uint64_t width, uint64_t pos, uint64_t *row, uint64_t *column)
{
	*row    = pos / width;
	*column = pos % width;
}

/**
 * Apply a permutation given by its Lehmer code in k[] to a set s[] of n
 * elements and build inverse permutation in r[].
 *
 * @param n - number of elements in k[], s[] and r[]
 * @param k - Lehmer code of the permutation
 * @param s - an array to permute
 * @param r - an array to build inverse permutation in
 *
 * @pre  k[i] + i < n
 * @pre  s[i] < n && ergo(s[i] == s[j], i == j)
 * @post s[i] < n && ergo(s[i] == s[j], i == j)
 * @post s[r[i]] == i && r[s[i]] == i
 */
static void permute(uint32_t n, uint32_t *k, uint32_t *s, uint32_t *r)
{
	uint32_t i;
	uint32_t j;
	uint32_t t;
	uint32_t x;

	/*
	 * k[0] is an index of one of the n elements that permutation moves to
	 * the 0-th position in s[];
	 *
	 * k[1] is an index of one of the (n - 1) remaining elements that
	 * permutation moves to the 1-st position in s[], etc.
	 *
	 * To produce i-th element of s[], pick one of remaining elements, say
	 * s[t], as specified by k[i], shift elements s[i] ... s[t] to the right
	 * by one and place s[t] in s[i]. This guarantees that at beginning of
	 * the loop elements s[0] ... s[i - 1] are already selected and elements
	 * s[i] ... s[n - 1] are "remaining".
	 */

	for (i = 0; i < n - 1; ++i) {
		t = k[i] + i;
		C2_ASSERT(t < n);
		x = s[t];
		for (j = t; j > i; --j)
			s[j] = s[j - 1];
		s[i] = x;
		r[x] = i;
	}
	/*
	 * The loop above iterates n-1 times, because the last element finds its
	 * place automatically. Complete inverse permutation.
	 */
	r[s[n - 1]] = n - 1;
}

bool c2_pdclust_layout_invariant(const struct c2_pdclust_layout *play)
{
	uint32_t                 i;
	uint32_t                 P;
	const struct tile_cache *tc;

	if (play == NULL)
		return false;

	if (!c2_pdclust_layout_bob_check(play))
		return false;

	if (!striped_layout_invariant(&play->pl_base,
				      play->pl_base.ls_base.l_id))
		return false;

	P = play->pl_attr.pa_P;

	tc = &play->pl_tile_cache;
	/*
	 * tc->tc_permute[] and tc->tc_inverse[] are mutually inverse bijections
	 * of {0, ..., P - 1}.
	 */
	for (i = 0; i < P; ++i) {
		if (tc->tc_lcode[i] + i >= P)
			return false;
		if (tc->tc_permute[i] >= P || tc->tc_inverse[i] >= P)
			return false;
		if (tc->tc_permute[tc->tc_inverse[i]] != i)
			return false;
		if (tc->tc_inverse[tc->tc_permute[i]] != i)
			return false;
		/*
		 * existence of inverse guarantees that tc->tc_permute[] is a
		 * bijection.
		 */
	}
	return
		play->pl_C * (play->pl_attr.pa_N + 2*play->pl_attr.pa_K) == play->pl_L * P;
}

/**
 * Simple multiplicative hash.
 */
static uint64_t hash(uint64_t x)
{
	uint64_t y;

	y = x;
	y <<= 18;
	x -= y;
	y <<= 33;
	x -= y;
	y <<= 3;
	x += y;
	y <<= 3;
	x -= y;
	y <<= 4;
	x += y;
	y <<= 2;

	return x + y;
}

/**
 * Returns column number that a column t has after a permutation for tile omega
 * is applied.
 */
static uint64_t permute_column(struct c2_pdclust_layout *play,
			       uint64_t omega, uint64_t t)
{
	struct tile_cache *tc;

	C2_ASSERT(t < play->pl_attr.pa_P);
	tc = &play->pl_tile_cache;

	/* If cached values are for different tile, update the cache. */
	if (tc->tc_tile_no != omega) {
		uint32_t i;
		uint64_t rstate;

		/* initialise columns array that will be permuted. */
		for (i = 0; i < play->pl_attr.pa_P; ++i)
			tc->tc_permute[i] = i;

		/* initialise PRNG */
		rstate  =
			hash(play->pl_attr.pa_seed.u_hi) ^
			hash(play->pl_attr.pa_seed.u_lo + omega);

		/* generate permutation number in lexicographic ordering */
		for (i = 0; i < play->pl_attr.pa_P - 1; ++i)
			tc->tc_lcode[i] = c2_rnd(play->pl_attr.pa_P - i,
						 &rstate);

		/* apply the permutation */
		permute(play->pl_attr.pa_P, tc->tc_lcode,
			tc->tc_permute, tc->tc_inverse);
		tc->tc_tile_no = omega;
	}
	C2_ASSERT(tc->tc_permute[t] < play->pl_attr.pa_P);
	C2_ASSERT(tc->tc_inverse[tc->tc_permute[t]] == t);
	C2_ASSERT(tc->tc_permute[tc->tc_inverse[t]] == t);
	return tc->tc_permute[t];
}

void c2_pdclust_layout_map(struct c2_pdclust_layout *play,
			   const struct c2_pdclust_src_addr *src,
			   struct c2_pdclust_tgt_addr *tgt)
{
	uint32_t N;
	uint32_t K;
	uint32_t P;

	uint32_t C;
	uint32_t L;

	uint64_t omega;
	uint64_t j;

	uint64_t r;
	uint64_t t;

	N = play->pl_attr.pa_N;
	K = play->pl_attr.pa_K;
	P = play->pl_attr.pa_P;
	C = play->pl_C;
	L = play->pl_L;

	C2_ASSERT(c2_pdclust_layout_invariant(play));

	/*
	 * first translate source address into a tile number and parity group
	 * number in the tile.
	 */
	m_dec(C, src->sa_group, &omega, &j);
	/*
	 * then, convert from C*(N+2*K) coordinates to L*P coordinates within a
	 * tile.
	 */
	m_dec(P, m_enc(N + 2*K, j, src->sa_unit), &r, &t);
	/* permute columns */
	tgt->ta_obj   = permute_column(play, omega, t);
	/* and translate back from tile to target address. */
	tgt->ta_frame = m_enc(L, omega, r);
}

void c2_pdclust_layout_inv(struct c2_pdclust_layout *play,
			   const struct c2_pdclust_tgt_addr *tgt,
			   struct c2_pdclust_src_addr *src)
{
	uint32_t N;
	uint32_t K;
	uint32_t P;

	uint32_t C;
	uint32_t L;

	uint64_t omega;
	uint64_t j;

	uint64_t r;
	uint64_t t;

	N = play->pl_attr.pa_N;
	K = play->pl_attr.pa_K;
	P = play->pl_attr.pa_P;
	C = play->pl_C;
	L = play->pl_L;

	r = tgt->ta_frame;
	t = tgt->ta_obj;

	C2_ASSERT(c2_pdclust_layout_invariant(play));

	/*
	 * execute inverses of the steps of c2_pdclust_layout_map() in reverse
	   order.
	 */
	m_dec(L, tgt->ta_frame, &omega, &r);
	permute_column(play, omega, t); /* force tile cache update */
	t = play->pl_tile_cache.tc_inverse[t];
	m_dec(N + 2*K, m_enc(P, r, t), &j, &src->sa_unit);
	src->sa_group = m_enc(C, omega, j);
}

/** Implementation of lo_fini for pdclust layout type. */
static void pdclust_fini(struct c2_layout *l, struct c2_layout_domain *dom)
{
	uint32_t                  i;
	struct c2_pdclust_layout *pl;

	C2_PRE(l != NULL);
	C2_PRE(dom != NULL);

	C2_ENTRY("DESTROY, lid %llu", (unsigned long long)l->l_id);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);
	C2_ASSERT(c2_pdclust_layout_invariant(pl));

	c2_pdclust_layout_bob_fini(pl);

	c2_parity_math_fini(&pl->pl_math);

	c2_free(pl->pl_tile_cache.tc_inverse);
	c2_free(pl->pl_tile_cache.tc_permute);
	c2_free(pl->pl_tile_cache.tc_lcode);
	if (pl->pl_tgt != NULL) {
		for (i = 0; i < pl->pl_attr.pa_P; ++i) {
			if (c2_stob_id_is_set(&pl->pl_tgt[i]))
				c2_pool_put(pl->pl_pool,
					    &pl->pl_tgt[i]);
		}
		c2_free(pl->pl_tgt);
	}

	striped_fini(dom, &pl->pl_base);

	c2_free(pl);

	C2_LEAVE();
}

/**
 * Implementation of lto_register for PDCLUST layout type.
 * No table is required specifically for PDCLUST layout type.
 */
static int pdclust_register(struct c2_layout_domain *dom,
			    const struct c2_layout_type *lt)
{
	return 0;
}

/**
 * Implementation of lto_unregister for PDCLUST layout type.
 */
static void pdclust_unregister(struct c2_layout_domain *dom,
			       const struct c2_layout_type *lt)
{
}

static const struct c2_layout_ops pdclust_ops;

/**
 * @post A pdclust type of layout object is created. It needs to be
 * finalised by the user, once done with the usage. It can be finalised
 * using l->l_ops->lo_fini().
 */
int c2_pdclust_build(struct c2_layout_domain *dom,
		     struct c2_pool *pool, uint64_t lid,
		     uint32_t N, uint32_t K, uint64_t unitsize,
		     const struct c2_uint128 *seed,
		     struct c2_layout_enum *le,
		     struct c2_pdclust_layout **out)
{
	struct c2_pdclust_layout *pdl;
	uint32_t                  B;
	uint32_t                  i;
	uint32_t                  P;
	int                       rc;

	C2_PRE(dom != NULL);
	C2_PRE(pool != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(seed != NULL);
	C2_PRE(le != NULL);
	C2_PRE(out != NULL && *out == NULL);

	P = pool->po_width;
	C2_PRE(N + 2 * K <= P);

	C2_ENTRY("BUILD, lid %llu", (unsigned long long)lid);

	C2_ALLOC_PTR(pdl);
	C2_ALLOC_ARR(pdl->pl_tgt, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_lcode, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_permute, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_inverse, P);

	if (pdl == NULL || pdl->pl_tgt == NULL ||
	    pdl->pl_tile_cache.tc_lcode == NULL ||
	    pdl->pl_tile_cache.tc_permute == NULL ||
	    pdl->pl_tile_cache.tc_inverse == NULL) {
		rc = -ENOMEM;
		layout_log("c2_pdclust_build", "C2_ALLOC() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_oom.ae_id, &layout_global_ctx,
			   LID_APPLICABLE, lid, rc);
		goto out;
	}

	rc = striped_init(dom, &pdl->pl_base, le, lid, pool->po_id,
			  &c2_pdclust_layout_type, &pdclust_ops);
	if (rc != 0) {
		C2_LOG("c2_pdclust_build(): lid %llu, striped_init() "
		       "failed, rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	pdl->pl_attr.pa_seed      = *seed;
	pdl->pl_attr.pa_N         = N;
	pdl->pl_attr.pa_K         = K;
	pdl->pl_attr.pa_unit_size = unitsize;

	pdl->pl_pool              = pool;

	/* select minimal possible B (least common multiple of P and N+2*K) */
	B = P*(N+2*K)/c2_gcd64(N+2*K, P);
	pdl->pl_attr.pa_P = P;
	pdl->pl_C = B/(N+2*K);
	pdl->pl_L = B/P;

	pdl->pl_tile_cache.tc_tile_no = 1;
	permute_column(pdl, 0, 0);
	for (rc = 0, i = 0; i < P; ++i) {
		rc = c2_pool_alloc(pool, &pdl->pl_tgt[i]);
		if (rc != 0) {
			rc = -ENOMEM;
			C2_LOG("c2_pdclust_build: lid %llu, c2_pool_alloc() "
			       "failed, rc %d", (unsigned long long)lid, rc);
			goto out;
		}
	}

	rc = c2_parity_math_init(&pdl->pl_math, N, K);
	if (rc != 0) {
		C2_LOG("c2_pdclust_build: lid %llu, c2_parity_math_init() "
		       "failed, rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	c2_pdclust_layout_bob_init(pdl);
out:
	if (rc == 0) {
		*out = pdl;
		C2_POST(c2_pdclust_layout_invariant(pdl));
	}
	else {
		pdclust_fini(&pdl->pl_base.ls_base, dom);
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

enum c2_pdclust_unit_type
c2_pdclust_unit_classify(const struct c2_pdclust_layout *play,
			 int unit)
{
	if (unit < play->pl_attr.pa_N)
		return PUT_DATA;
	else if (unit < play->pl_attr.pa_N + play->pl_attr.pa_K)
		return PUT_PARITY;
	else
		return PUT_SPARE;
}

/** Implementation of lto_max_recsize() for pdclust layout type. */
static c2_bcount_t pdclust_max_recsize(struct c2_layout_domain *dom)
{
	uint32_t    i;
	c2_bcount_t e_recsize;
	c2_bcount_t max_recsize = 0;

	C2_PRE(dom != NULL);

	/* Iterate over all the enum types to find maximum possible recsize. */
        for (i = 0; i < ARRAY_SIZE(dom->ld_enum); ++i) {
		if (dom->ld_enum[i] == NULL)
			continue;
                e_recsize = dom->ld_enum[i]->let_ops->leto_max_recsize();
		max_recsize = max64u(max_recsize, e_recsize);
        }

	return sizeof(struct c2_layout_pdclust_rec) + max_recsize;
}

/** Implementation of lto_recsize() for pdclust layout type. */
static c2_bcount_t pdclust_recsize(struct c2_layout_domain *dom,
				   struct c2_layout *l)
{
	c2_bcount_t                 e_recsize;
	struct c2_pdclust_layout   *pl;
	struct c2_layout_enum_type *et;

	C2_PRE(dom != NULL);
	C2_PRE(l!= NULL);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);
	C2_ASSERT(c2_pdclust_layout_invariant(pl));

	et = dom->ld_enum[pl->pl_base.ls_enum->le_type->let_id];
	C2_ASSERT(is_enum_type_valid(et->let_id, dom));

	e_recsize = et->let_ops->leto_recsize(pl->pl_base.ls_enum, l->l_id);

	return sizeof(struct c2_layout_pdclust_rec) + e_recsize;
}

/**
 * Implementation of lto_decode() for pdclust layout type.
 *
 * Continues to build the in-memory layout object from its representation
 * either 'stored in the Layout DB' or 'received through the buffer'.
 *
 * @param op This enum parameter indicates what, if a DB operation is to be
 * performed on the layout record and it could be LOOKUP if at all.
 * If it is BUFFER_OP, then the layout is decoded from its representation
 * received through the buffer.
 */
static int pdclust_decode(struct c2_layout_domain *dom,
			  enum c2_layout_xcode_op op,
			  struct c2_db_tx *tx,
			  uint64_t lid,
			  uint64_t pool_id,
			  struct c2_bufvec_cursor *cur,
		          struct c2_layout **out)
{
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_layout_pdclust_rec *pl_rec;
	struct c2_layout_enum_type   *et;
	struct c2_layout_enum        *e = NULL;
	struct c2_pool               *pool = NULL;
	int                           rc;

	C2_PRE(dom != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_BUFFER_OP);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *pl_rec);
	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	/* pl_rec can not be NULL since the buffer size is already verified. */
	pl_rec = c2_bufvec_cursor_addr(cur);

	et = dom->ld_enum[pl_rec->pr_let_id];
	C2_ASSERT(is_enum_type_valid(et->let_id, dom));

	c2_bufvec_cursor_move(cur, sizeof *pl_rec);

	rc = et->let_ops->leto_decode(dom, op, tx, lid, cur, &e);
	if (rc != 0) {
		C2_LOG("pdclust_decode(): lid %llu, leto_decode() failed, "
		       "rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	rc = c2_pool_lookup(pool_id, &pool);
	if (rc != 0) {
		C2_LOG("pdclust_decode(): lid %llu, pool_id %llu, "
		       "c2_pool_lookup() failed, rc %d",
		       (unsigned long long)lid, (unsigned long long)pool_id,
		       rc);
		goto out;
	}

	rc = c2_pdclust_build(dom, pool, lid,
			      pl_rec->pr_attr.pa_N,
			      pl_rec->pr_attr.pa_K,
			      pl_rec->pr_attr.pa_unit_size,
			      &pl_rec->pr_attr.pa_seed,
			      e, &pl);
	if (rc != 0) {
		C2_LOG("pdclust_decode(): lid %llu, c2_pdclust_build() failed, "
		       "rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	*out = &pl->pl_base.ls_base;
	C2_POST(c2_pdclust_layout_invariant(pl));

out:
	if (rc != 0 && e != NULL) {
		C2_ASSERT(pl == NULL);
		e->le_ops->leo_fini(dom, e, lid);
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/**
 * Implementation of lto_encode() for pdclust layout type.
 *
 * Continues to use the in-memory layout object and
 * - Either adds/updates/deletes it to/from the Layout DB
 * - Or converts it to a buffer.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is BUFFER_OP, then the layout is stored in the
 * buffer.
 */
static int pdclust_encode(struct c2_layout_domain *dom,
			  enum c2_layout_xcode_op op,
			  struct c2_db_tx *tx,
			  struct c2_layout *l,
		          struct c2_bufvec_cursor *oldrec_cur,
		          struct c2_bufvec_cursor *out)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_pdclust_rec  pl_rec;
	struct c2_layout_pdclust_rec *pl_oldrec;
	struct c2_layout_enum_type   *et;
	c2_bcount_t                   nbytes;
	int                           rc;

	C2_PRE(dom != NULL);
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_BUFFER_OP);
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));

	/*
	 * layout_invariant() is part of c2_pdclust_layout_invariant(),
	 * to be invoked little later below.
	 */
	C2_PRE(l != NULL);

	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE,
		    c2_bufvec_cursor_step(oldrec_cur) >= sizeof *pl_oldrec));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof pl_rec);

	C2_ENTRY("%llu", (unsigned long long)l->l_id);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);
	C2_ASSERT(c2_pdclust_layout_invariant(pl));

	if (op == C2_LXO_DB_UPDATE) {
		/*
		 * Processing the oldrec_cur, to verify that nothing other than
		 * l_ref is being changed for this layout and then to make it
		 * point to the enumeration type specific payload.
		 */
		pl_oldrec = c2_bufvec_cursor_addr(oldrec_cur);

		C2_ASSERT(pl_oldrec->pr_let_id ==
			  pl->pl_base.ls_enum->le_type->let_id &&
			  pl_oldrec->pr_attr.pa_N == pl->pl_attr.pa_N &&
			  pl_oldrec->pr_attr.pa_K == pl->pl_attr.pa_K &&
			  pl_oldrec->pr_attr.pa_P == pl->pl_attr.pa_P &&
			  c2_uint128_eq(&pl_oldrec->pr_attr.pa_seed,
					&pl->pl_attr.pa_seed));
		c2_bufvec_cursor_move(oldrec_cur, sizeof *pl_oldrec);
	}

	et = dom->ld_enum[pl->pl_base.ls_enum->le_type->let_id];
	C2_ASSERT(is_enum_type_valid(et->let_id, dom));

	pl_rec.pr_let_id  = pl->pl_base.ls_enum->le_type->let_id;
	pl_rec.pr_attr    = pl->pl_attr;

	nbytes = c2_bufvec_cursor_copyto(out, &pl_rec, sizeof pl_rec);
	C2_ASSERT(nbytes == sizeof pl_rec);

	rc = et->let_ops->leto_encode(dom, op, tx, l->l_id,
				      pl->pl_base.ls_enum, oldrec_cur, out);
	if (rc != 0) {
		C2_LOG("pdclust_encode(): lid %llu, leto_encode() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}


static const struct c2_layout_ops pdclust_ops = {
	.lo_fini        = &pdclust_fini
};

static const struct c2_layout_type_ops pdclust_type_ops = {
	.lto_register    = pdclust_register,
	.lto_unregister  = pdclust_unregister,
	.lto_max_recsize = pdclust_max_recsize,
	.lto_recsize     = pdclust_recsize,
	.lto_decode      = pdclust_decode,
	.lto_encode      = pdclust_encode
};

const struct c2_layout_type c2_pdclust_layout_type = {
	.lt_name        = "pdclust",
	.lt_id          = 0,
	.lt_ops         = &pdclust_type_ops
};


/** @} end group pdclust */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
