/* -*- C -*- */

#include <errno.h>

#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/arith.h" /* c2_rnd() */

#include "stob/stob.h"
#include "pool/pool.h"

#include "layout/pdclust.h"

/**
   @addtogroup pdclust
   @{
*/

/**
   "Encoding" function: returns the number that a (row, column) element of a
   matrix with "width" columns has when elements are counted row by row.

   @see m_dec()
 */
static uint64_t m_enc(uint64_t row, uint64_t column, uint64_t width)
{
	C2_ASSERT(column < width);
	return row * width + column;
}

/**
   "Decoding" function: returns (row, column) coordinates of a pos-th element in
   a matrix with "width" column when elements are counted row by row.

   @see m_enc()
 */
static void m_dec(uint64_t pos, uint64_t width, uint64_t *row, uint64_t *column)
{
	*row    = pos / width;
	*column = pos % width;
}

static void permute(uint32_t n, uint32_t *k, uint32_t *s, uint32_t *r)
{
	uint32_t i;
	uint32_t j;
	uint32_t t;
	uint32_t x;

	for (i = 0; i < n - 1; ++i) {
		t = k[i] + i;
		C2_ASSERT(t < n);
		x = s[t];
		for (j = t; j > i; --j)
			s[j] = s[j - 1];
		s[i] = t;
		r[t] = i;
	}
}

static bool c2_pdclust_layout_invariant(const struct c2_pdclust_layout *play)
{
	return 
		play->pl_C * (play->pl_N + 2*play->pl_K) == 
		play->pl_L * play->pl_P;
}

static uint64_t permute_column(struct c2_pdclust_layout *play, 
			       uint64_t omega, uint64_t t)
{
	struct tile_cache *tc;

	C2_ASSERT(t < play->pl_P);
	tc = &play->pl_tile_cache;
	if (tc->tc_tile_no != omega) {
		uint32_t i;
		uint64_t rstate;

		for (i = 0; i < play->pl_P; ++i)
			tc->tc_permute[i] = i;

		/* Initialize PRNG */
		rstate  = play->pl_seed.u_hi;
		c2_rnd(1, &rstate);
		rstate += omega;
		c2_rnd(1, &rstate);
		rstate += play->pl_seed.u_lo;

		for (i = 0; i < play->pl_P - 1; ++i)
			tc->tc_lcode[i] = c2_rnd(play->pl_P - i, &rstate);

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

	m_dec(src->sa_group, C, &omega, &j);
	m_dec(m_enc(j, src->sa_unit, N + 2*K), P, &r, &t);
	tgt->ta_obj   = permute_column(play, omega, t);
	tgt->ta_frame = m_enc(omega, r, L);
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

	m_dec(tgt->ta_frame, L, &omega, &r);
	permute_column(play, omega, t); /* force tile cache update */
	t = play->pl_tile_cache.tc_inverse[t];
	m_dec(m_enc(r, t, P), N + 2*K, &j, &src->sa_unit);
	src->sa_group = m_enc(omega, j, C);
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
		     uint32_t N, uint32_t K, const struct c2_uint128 *seed)
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
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_permute, P);
	C2_ALLOC_ARR(pdl->pl_tile_cache.tc_inverse, P);

	if (pdl != NULL && pdl->pl_tgt != NULL &&
	    pdl->pl_tile_cache.tc_permute != NULL &&
	    pdl->pl_tile_cache.tc_inverse != NULL) {
		c2_layout_init(&pdl->pl_layout);
		pdl->pl_layout.l_type    = &c2_pdclust_layout_type;
		pdl->pl_layout.l_form    = NULL;
		pdl->pl_layout.l_actuals = NULL;
		pdl->pl_layout.l_ops     = &pdlclust_ops;
		pdl->pl_layout.l_id      = *id;

		pdl->pl_seed = *seed;
		pdl->pl_N = N;
		pdl->pl_K = K;

		pdl->pl_pool = pool;
		B = c2_gcd64(N+2*K, P);
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
	} else {
		result = -ENOMEM;
	}
	if (result != 0)
		c2_pdclust_fini(pdl);
	return result;
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
