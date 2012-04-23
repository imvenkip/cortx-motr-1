/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 07/15/2010
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/arith.h" /* c2_rnd() */

#include "stob/stob.h"
#include "pool/pool.h"

#include "layout/pdclust.h"

/**
   @addtogroup pdclust

   <b>Implementation overview.</b>

   Parity de-clustering layout mapping function requires some amount of code
   dealing with permutations, random sequences generations and conversions
   between matrices of different shapes.

   First, as explained in the HLD, an efficient way to generate permutations
   uniformly scattered across the set of all permutations of a given set is
   necessary. To this end permute_column() uses a sequence of pseudo-random
   numbers obtained from a PRNG (c2_rnd()). Few comments are in order:

   @li to seed a PRNG, layout seed and tile number are hashed by a
   multiplicative cache (hash());

   @li system PRNG cannot be used, because reproducible sequences are
   needed. c2_rnd() is a very simple linear congruential generator straight from
   TAOCP. It takes care to return higher, more random, bits of result;

   @li layout behavior is quite sensitive to the PRNG properties. For example,
   if c2_rnd() is changed to return lower bits (result % max), resulting
   distribution of spare and parity units is not uniform even for large number
   of units. Experiments with different PRNG's are indicated.

   Once permutation's Lehmer code is generated, it has to be applied to the set
   of columns. permute() function applies a permutation, simultaneously building
   an inverse permutation.

   Finally, layout mapping function is defined in terms of conversions between
   matrices of different shapes. Let's call a matrix having M columns and an
   arbitrary (probably infinite) number of rows an M-matrix. An element of an
   M-matrix has (row, column) coordinates. Coordinate pairs can be ordered and
   enumerated in the "row first" lexicographical order:

           (0, 0) < (0, 1) < ... < (0, M - 1) < (1, 0) < ...

   Function m_enc() returns the number a (row, column) element of an M-matrix
   has in this ordering. Conversely, function m_dec() returns coordinates of the
   element having a given number in the ordering. With the help of these two
   function an M-matrix can be re-arranged into an N-matrix in such a way the
   element position in the ordering remains invariant.

   Layout mapping function c2_pdclust_layout_map() performs these
   re-arrangements in the following places:

   @li to convert a parity group number to a (tile number, group in tile)
   pair. This is a conversion of 1-matrix to C-matrix;

   @li to convert a tile from C*(N + 2*K) to L*P form. This is a conversion of
   (N + 2*K)-matrix to P-matrix;

   @li to convert a (tile number, frame in tile) pair to a target frame
   number. This is a conversion of L-matrix to 1-matrix.

   Inverse layout mapping function c2_pdclust_layout_inv() performs reverse
   conversions.

   @{
*/

/**
   "Encoding" function: returns the number that a (row, column) element of a
   matrix with "width" columns has when elements are counted row by row. This
   function is denoted e_{width} in the HLD.

   @see m_dec()
 */
static uint64_t m_enc(uint64_t width, uint64_t row, uint64_t column)
{
	C2_ASSERT(column < width);
	return row * width + column;
}

/**
   "Decoding" function: returns (row, column) coordinates of a pos-th element in
   a matrix with "width" column when elements are counted row by row. This
   function is denoted d_{width} in the HLD.

   @see m_enc()
 */
static void m_dec(uint64_t width, uint64_t pos, uint64_t *row, uint64_t *column)
{
	*row    = pos / width;
	*column = pos % width;
}

/**
   Apply a permutation given by its Lehmer code in k[] to a set s[] of n
   elements and build inverse permutation in r[].

   @param n - number of elements in k[], s[] and r[]
   @param k - Lehmer code of the permutation
   @param s - an array to permute
   @param r - an array to build inverse permutation in

   @pre  k[i] + i < n
   @pre  s[i] < n && ergo(s[i] == s[j], i == j)
   @post s[i] < n && ergo(s[i] == s[j], i == j)
   @post s[r[i]] == i && r[s[i]] == i
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

static bool c2_pdclust_layout_invariant(const struct c2_pdclust_layout *play)
{
	uint32_t i;
	uint32_t P;

	const struct tile_cache *tc;

	P = play->pl_P;

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
		/* existence of inverse guarantees that tc->tc_permute[] is a
		   bijection. */
	}
	return
		play->pl_C * (play->pl_N + 2*play->pl_K) == play->pl_L * P;
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
   Returns column number that a column t has after a permutation for tile omega
   is applied.
 */
static uint64_t permute_column(struct c2_pdclust_layout *play,
			       uint64_t omega, uint64_t t)
{
	struct tile_cache *tc;

	C2_ASSERT(t < play->pl_P);
	tc = &play->pl_tile_cache;
	/*
	 * If cached values are for different tile, update the cache.
	 */
	if (tc->tc_tile_no != omega) {
		uint32_t i;
		uint64_t rstate;

		/* initialise columns array that will be permuted. */
		for (i = 0; i < play->pl_P; ++i)
			tc->tc_permute[i] = i;

		/* initialize PRNG */
		rstate  =
			hash(play->pl_seed.u_hi) ^
			hash(play->pl_seed.u_lo + omega);

		/* generate permutation number in lexicographic ordering */
		for (i = 0; i < play->pl_P - 1; ++i)
			tc->tc_lcode[i] = c2_rnd(play->pl_P - i, &rstate);

		/* apply the permutation */
		permute(play->pl_P, tc->tc_lcode,
			tc->tc_permute, tc->tc_inverse);
		tc->tc_tile_no = omega;
	}
	C2_ASSERT(tc->tc_permute[t] < play->pl_P);
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

	N = play->pl_N;
	K = play->pl_K;
	P = play->pl_P;
	C = play->pl_C;
	L = play->pl_L;

	C2_ASSERT(c2_pdclust_layout_invariant(play));

	/* first translate source address into a tile number and parity group
	   number in the tile. */
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

	N = play->pl_N;
	K = play->pl_K;
	P = play->pl_P;
	C = play->pl_C;
	L = play->pl_L;

	r = tgt->ta_frame;
	t = tgt->ta_obj;

	C2_ASSERT(c2_pdclust_layout_invariant(play));

	/* execute inverses of the steps of c2_pdclust_layout_map() in reverse
	   order.  */
	m_dec(L, tgt->ta_frame, &omega, &r);
	permute_column(play, omega, t); /* force tile cache update */
	t = play->pl_tile_cache.tc_inverse[t];
	m_dec(N + 2*K, m_enc(P, r, t), &j, &src->sa_unit);
	src->sa_group = m_enc(C, omega, j);
}

static bool pdclust_equal(const struct c2_layout *l0,
			  const struct c2_layout *l1)
{
	struct c2_pdclust_layout *p0;
	struct c2_pdclust_layout *p1;

	p0 = container_of(l0, struct c2_pdclust_layout, pl_layout);
	p1 = container_of(l1, struct c2_pdclust_layout, pl_layout);

	return
		c2_uint128_eq(&p0->pl_seed, &p1->pl_seed) &&
		p0->pl_N == p1->pl_N &&
		p0->pl_K == p1->pl_K &&
		p0->pl_P == p1->pl_P &&
		p0->pl_C == p1->pl_C &&
		p0->pl_L == p1->pl_L &&
		p0->pl_pool == p1->pl_pool;
	/* XXX and check that target objects are the same */
}

static const struct c2_layout_ops pdlclust_ops = {
};

void c2_pdclust_fini(struct c2_pdclust_layout *pdl)
{
	uint32_t i;

	if (pdl != NULL) {
		c2_layout_fini(&pdl->pl_layout);
		c2_free(pdl->pl_tile_cache.tc_inverse);
		c2_free(pdl->pl_tile_cache.tc_permute);
		c2_free(pdl->pl_tile_cache.tc_lcode);
		if (pdl->pl_tgt != NULL) {
			for (i = 0; i < pdl->pl_P; ++i) {
				if (c2_stob_id_is_set(&pdl->pl_tgt[i]))
					c2_pool_put(pdl->pl_pool,
						    &pdl->pl_tgt[i]);
			}
			c2_free(pdl->pl_tgt);
		}
		c2_free(pdl);
	}
}

int c2_pdclust_build(struct c2_pool *pool, struct c2_uint128 *id,
		     uint32_t N, uint32_t K, uint64_t unitsize,
		     const struct c2_uint128 *seed,
		     struct c2_pdclust_layout **out)
{
	struct c2_pdclust_layout *pdl;
	uint32_t B;
	uint32_t i;
	uint32_t P;
	int      result;

	P = pool->po_width;
	C2_PRE(N + 2 * K <= P);

	C2_ALLOC_PTR(pdl);
	C2_ALLOC_ARR(pdl->pl_tgt, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_lcode, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_permute, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_inverse, P);

	if (pdl != NULL && pdl->pl_tgt != NULL &&
	    pdl->pl_tile_cache.tc_lcode != NULL &&
	    pdl->pl_tile_cache.tc_permute != NULL &&
	    pdl->pl_tile_cache.tc_inverse != NULL) {
		c2_layout_init(&pdl->pl_layout);
		pdl->pl_layout.l_type    = &c2_pdclust_layout_type;
		pdl->pl_layout.l_form    = NULL;
		pdl->pl_layout.l_actuals = NULL;
		pdl->pl_layout.l_ops     = &pdlclust_ops;
		pdl->pl_layout.l_id      = *id;

		pdl->pl_seed      = *seed;
		pdl->pl_N         = N;
		pdl->pl_K         = K;
		pdl->pl_unit_size = unitsize;

		pdl->pl_pool = pool;
		/* select minimal possible B (least common multiple of P and
		   N+2*K */
		B = P*(N+2*K)/c2_gcd64(N+2*K, P);
		pdl->pl_P = P;
		pdl->pl_C = B/(N+2*K);
		pdl->pl_L = B/P;

		pdl->pl_tile_cache.tc_tile_no = 1;
		permute_column(pdl, 0, 0);
		for (result = 0, i = 0; i < P; ++i) {
			result = c2_pool_alloc(pool, &pdl->pl_tgt[i]);
			if (result != 0)
				break;
		}

		result = c2_parity_math_init(&pdl->pl_math, N, K);
	} else {
		result = -ENOMEM;
	}
	if (result == 0)
		*out = pdl;
	else
		c2_pdclust_fini(pdl);
	return result;
}

enum c2_pdclust_unit_type
c2_pdclust_unit_classify(const struct c2_pdclust_layout *play,
			 int unit)
{
	if (unit < play->pl_N)
		return PUT_DATA;
	else if (unit < play->pl_N + play->pl_K)
		return PUT_PARITY;
	else
		return PUT_SPARE;
}

const struct c2_layout_type c2_pdclust_layout_type = {
	.lt_name  = "pdclust",
	.lt_equal = pdclust_equal
};

static const struct c2_layout_formula_ops nkp_ops = {
	.lfo_subst = NULL
};

const struct c2_layout_formula c2_pdclust_NKP_formula = {
	.lf_type = &c2_pdclust_layout_type,
	.lf_id   = { .u_hi = 0x5041524954594445, /* PARITYDE */
		     .u_lo = 0x434c55535445522e  /* CLUSTER. */
	},
	.lf_ops  = &nkp_ops
};

/** @} end of group pdclust */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
