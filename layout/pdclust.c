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
 * Layout mapping function c2_pdclust_instance_map() performs these
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
 * Inverse layout mapping function c2_pdclust_instance_inv() performs reverse
 * conversions.
 *
 * @{
 */

#include "lib/errno.h"
#include "lib/memory.h" /* C2_ALLOC_PTR(), C2_ALLOC_ARR(), c2_free() */
#include "lib/misc.h"   /* C2_IN() */
#include "lib/vec.h"    /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */
#include "lib/arith.h"  /* c2_rnd() */
#include "lib/misc.h"   /* c2_forall */
#include "lib/bob.h"
#include "lib/finject.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "layout/layout_internal.h"
#include "layout/pdclust.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	PDCLUST_MAGIC     = 0x5044434C5553544CULL, /* PDCLUSTL */
	PD_INSTANCE_MAGIC = 0x5044494E5354414EULL  /* PDINSTAN */
};

static const struct c2_bob_type pdclust_bob = {
	.bt_name         = "pdclust",
	.bt_magix_offset = offsetof(struct c2_pdclust_layout, pl_magic),
	.bt_magix        = PDCLUST_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &pdclust_bob, c2_pdclust_layout);

static const struct c2_bob_type pdclust_instance_bob = {
	.bt_name         = "pd_instance",
	.bt_magix_offset = offsetof(struct c2_pdclust_instance, pi_magic),
	.bt_magix        = PD_INSTANCE_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &pdclust_instance_bob, c2_pdclust_instance);

C2_ADDB_EV_DEFINE(pdclust_tile_cache_hit, "pdclust_tile_cache_hit",
		  C2_ADDB_EVENT_LAYOUT_TILE_CACHE_HIT, C2_ADDB_FLAG);

static bool pdclust_allocated_invariant(const struct c2_pdclust_layout *pl)
{
	return
		pl != NULL &&
		c2_layout__striped_allocated_invariant(&pl->pl_base);
}

static bool pdclust_invariant(const struct c2_pdclust_layout *pl)
{
	struct c2_pdclust_attr attr = pl->pl_attr;

	return
		c2_pdclust_layout_bob_check(pl) &&
		c2_layout__striped_invariant(&pl->pl_base) &&
		pl->pl_C * (attr.pa_N + 2 * attr.pa_K) ==
		pl->pl_L * attr.pa_P &&
		pl->pl_base.sl_enum->le_ops->leo_nr(pl->pl_base.sl_enum) ==
		attr.pa_P;
}

static bool pdclust_instance_invariant(const struct c2_pdclust_instance *pi)
{
	uint32_t                 P;
	const struct tile_cache *tc;

	P  = pi->pi_layout->pl_attr.pa_P;
	tc = &pi->pi_tile_cache;

	return
		c2_pdclust_instance_bob_check(pi) &&
		c2_layout__instance_invariant(&pi->pi_base) &&
		pdclust_invariant(pi->pi_layout) &&
		/*
		 * tc->tc_permute[] and tc->tc_inverse[] are mutually inverse
		 * bijections of {0, ..., P - 1}.
		 */
		c2_forall(i, P,
			  tc->tc_lcode[i] + i < P &&
			  (tc->tc_permute[i] < P && tc->tc_inverse[i] < P) &&
			  tc->tc_permute[tc->tc_inverse[i]] == i &&
			  tc->tc_inverse[tc->tc_permute[i]] == i);
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

/** Implementation of lto_unregister for PDCLUST layout type. */
static void pdclust_unregister(struct c2_layout_domain *dom,
			       const struct c2_layout_type *lt)
{
}

/** Implementation of lo_fini for pdclust layout type. */
static void pdclust_fini(struct c2_layout *l)
{
	struct c2_pdclust_layout *pl;

	C2_PRE(l != NULL);
	C2_PRE(c2_mutex_is_not_locked(&l->l_lock));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	pl = c2_layout_to_pdl(l);
	c2_pdclust_layout_bob_fini(pl);
	c2_layout__striped_fini(&pl->pl_base);
	c2_free(pl);
	C2_LEAVE();
}

static const struct c2_layout_ops pdclust_ops;
/** Implementation of lto_allocate() for PDCLUST layout type. */
static int pdclust_allocate(struct c2_layout_domain *dom,
			    uint64_t lid,
			    struct c2_layout **out)
{
	struct c2_pdclust_layout *pl;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lid > 0);
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	if (C2_FI_ENABLED("mem_err")) { pl = NULL; goto err1_injected; }
	C2_ALLOC_PTR(pl);
err1_injected:
	if (pl == NULL) {
		c2_layout__log("pdclust_allocate", "C2_ALLOC_PTR() failed",
			       &c2_addb_oom, &layout_global_ctx, lid, -ENOMEM);
		return -ENOMEM;
	}

	c2_layout__striped_init(&pl->pl_base, dom, lid,
				&c2_pdclust_layout_type, &pdclust_ops);
	c2_pdclust_layout_bob_init(pl);
	c2_mutex_lock(&pl->pl_base.sl_base.l_lock);

	*out = &pl->pl_base.sl_base;
	C2_POST(pdclust_allocated_invariant(pl));
	C2_POST(c2_mutex_is_locked(&(*out)->l_lock));
	C2_LEAVE("lid %llu, pl pointer %p", (unsigned long long)lid, pl);
	return 0;
}

/** Implementation of lo_delete() for PDCLUST layout type. */
static void pdclust_delete(struct c2_layout *l)
{
	struct c2_pdclust_layout *pl;

	pl = bob_of(l, struct c2_pdclust_layout,
		    pl_base.sl_base, &pdclust_bob);
	C2_PRE(pdclust_allocated_invariant(pl));
	C2_PRE(c2_mutex_is_locked(&l->l_lock));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	c2_mutex_unlock(&l->l_lock);
	c2_pdclust_layout_bob_fini(pl);
	c2_layout__striped_delete(&pl->pl_base);
	c2_free(pl);
	C2_LEAVE();
}

/** Populates pl using the arguments supplied. */
static int pdclust_populate(struct c2_pdclust_layout *pl,
			    const struct c2_pdclust_attr *attr,
			    struct c2_layout_enum *le,
			    uint32_t ref_count)
{
	uint64_t lid;
	uint32_t B;
	uint32_t N;
	uint32_t K;
	uint32_t P;

	N = attr->pa_N;
	K = attr->pa_K;
	P = attr->pa_P;
	C2_PRE(pdclust_allocated_invariant(pl));
	C2_PRE(c2_mutex_is_locked(&pl->pl_base.sl_base.l_lock));
	C2_PRE(le != NULL);

	if (N + 2 * K > P) {
		C2_LOG("pl %p, attr %p, Invalid attributes, rc %d",
		       pl, attr, -EPROTO);
		return -EPROTO;
	}

	lid = pl->pl_base.sl_base.l_id;
	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_layout__striped_populate(&pl->pl_base, le, ref_count);
	pl->pl_attr = *attr;

	/* Select minimal possible B (least common multiple of P and N+2*K). */
	B = P*(N+2*K)/c2_gcd64(N+2*K, P);
	pl->pl_C = B/(N+2*K);
	pl->pl_L = B/P;

	C2_POST(pdclust_invariant(pl));
	C2_PRE(c2_mutex_is_locked(&pl->pl_base.sl_base.l_lock));
	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return 0;
}

int c2_pdclust_build(struct c2_layout_domain *dom,
		     uint64_t lid,
		     const struct c2_pdclust_attr *attr,
		     struct c2_layout_enum *le,
		     struct c2_pdclust_layout **out)
{
	struct c2_layout         *l;
	struct c2_pdclust_layout *pl;
	int                       rc;

	C2_PRE(out != NULL);

	C2_ENTRY("domain %p, lid %llu", dom,(unsigned long long)lid);
	rc = pdclust_allocate(dom, lid, &l);
	if (rc == 0) {
		/* Here pdclust_allocate() has locked l->l_lock. */
		pl = bob_of(l, struct c2_pdclust_layout,
			    pl_base.sl_base, &pdclust_bob);
		C2_ASSERT(pdclust_allocated_invariant(pl));

		rc = pdclust_populate(pl, attr, le, 1);
		if (rc == 0) {
			*out = pl;
			c2_mutex_unlock(&l->l_lock);
		} else
			pdclust_delete(l);
	}

	C2_POST(ergo(rc == 0, pdclust_invariant(*out) &&
			      c2_mutex_is_not_locked(&l->l_lock)));
	C2_LEAVE("domain %p, lid %llu, pl %p, rc %d",
		 dom, (unsigned long long)lid, *out, rc);
	return rc;
}

uint32_t c2_pdclust_N(const struct c2_pdclust_layout *pl)
{
	return pl->pl_attr.pa_N;
}

uint32_t c2_pdclust_K(const struct c2_pdclust_layout *pl)
{
	return pl->pl_attr.pa_K;
}

uint32_t c2_pdclust_P(const struct c2_pdclust_layout *pl)
{
	return pl->pl_attr.pa_P;
}

uint64_t c2_pdclust_unit_size(const struct c2_pdclust_layout *pl)
{
	return pl->pl_attr.pa_unit_size;
}

struct c2_pdclust_layout *c2_layout_to_pdl(const struct c2_layout *l)
{
	struct c2_pdclust_layout *pl;

	pl = bob_of(l, struct c2_pdclust_layout,
		    pl_base.sl_base, &pdclust_bob);
	C2_ASSERT(pdclust_invariant(pl));
	return pl;
}

struct c2_layout *c2_pdl_to_layout(struct c2_pdclust_layout *pl)
{
	C2_PRE(pdclust_invariant(pl));
	return &pl->pl_base.sl_base;
}

static struct c2_layout_enum *
pdclust_instance_to_enum(const struct c2_layout_instance *li)
{
	struct c2_pdclust_instance *pdi;

	pdi = c2_layout_instance_to_pdi(li);
	return c2_striped_layout_to_enum(&pdi->pi_layout->pl_base);
}

enum c2_pdclust_unit_type
c2_pdclust_unit_classify(const struct c2_pdclust_layout *pl,
			 int unit)
{
	if (unit < pl->pl_attr.pa_N)
		return C2_PUT_DATA;
	else if (unit < pl->pl_attr.pa_N + pl->pl_attr.pa_K)
		return C2_PUT_PARITY;
	else
		return C2_PUT_SPARE;
}

/** Implementation of lto_max_recsize() for pdclust layout type. */
static c2_bcount_t pdclust_max_recsize(struct c2_layout_domain *dom)
{
	C2_PRE(dom != NULL);

	return sizeof(struct c2_layout_pdclust_rec) +
		c2_layout__enum_max_recsize(dom);
}

/** Implementation of lo_decode() for pdclust layout type. */
static int pdclust_decode(struct c2_layout *l,
			  struct c2_bufvec_cursor *cur,
			  enum c2_layout_xcode_op op,
			  struct c2_db_tx *tx,
			  uint32_t ref_count)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_pdclust_rec *pl_rec;
	struct c2_layout_enum_type   *et;
	struct c2_layout_enum        *e;
	int                           rc;

	C2_PRE(c2_layout__allocated_invariant(l));
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *pl_rec);
	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	pl = bob_of(l, struct c2_pdclust_layout,
		    pl_base.sl_base, &pdclust_bob);
	C2_ASSERT(pdclust_allocated_invariant(pl));

	/* pl_rec can not be NULL since the buffer size is already verified. */
	pl_rec = c2_bufvec_cursor_addr(cur);
	c2_bufvec_cursor_move(cur, sizeof *pl_rec);

	if (C2_FI_ENABLED("attr_err1"))
		{ pl_rec->pr_let_id = C2_LAYOUT_ENUM_TYPE_MAX - 1; }
	if (C2_FI_ENABLED("attr_err2"))
		{ pl_rec->pr_let_id = C2_LAYOUT_ENUM_TYPE_MAX + 1; }
	et = l->l_dom->ld_enum[pl_rec->pr_let_id];
	if (!IS_IN_ARRAY(pl_rec->pr_let_id, l->l_dom->ld_enum) || et == NULL) {
		rc = -EPROTO;
		C2_LOG("lid %llu, unregistered enum type, rc %d",
		       (unsigned long long)l->l_id, rc);
		goto out;
	}
	rc = et->let_ops->leto_allocate(l->l_dom, &e);
	if (rc != 0) {
		C2_LOG("lid %llu, leto_allocate() failed, rc %d",
		       (unsigned long long)l->l_id, rc);
		goto out;
	}
	rc = e->le_ops->leo_decode(e, cur, op, tx, &pl->pl_base);
	if (rc != 0) {
		/* Finalise the allocated enum object. */
		e->le_ops->leo_delete(e);
		C2_LOG("lid %llu, leo_decode() failed, rc %d",
		       (unsigned long long)l->l_id, rc);
		goto out;
	}

	if (C2_FI_ENABLED("attr_err3")) { pl_rec->pr_attr.pa_P = 1; }
	rc = pdclust_populate(pl, &pl_rec->pr_attr, e, ref_count);
	if (rc != 0) {
		/* Finalise the populated enum object. */
		e->le_ops->leo_fini(e);
		C2_LOG("lid %llu, pdclust_populate() failed, rc %d",
		       (unsigned long long)l->l_id, rc);
	}
out:
	C2_POST(ergo(rc == 0, pdclust_invariant(pl)));
	C2_POST(ergo(rc != 0, pdclust_allocated_invariant(pl)));
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/** Implementation of lo_encode() for pdclust layout type. */
static int pdclust_encode(struct c2_layout *l,
			  enum c2_layout_xcode_op op,
			  struct c2_db_tx *tx,
		          struct c2_bufvec_cursor *out)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_pdclust_rec  pl_rec;
	struct c2_layout_enum        *e;
	c2_bcount_t                   nbytes;
	int                           rc;

	/*
	 * c2_layout__invariant() is part of pdclust_invariant(),
	 * to be invoked little later through c2_layout_to_pdl() below.
	 */
	C2_PRE(l != NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
		          C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof pl_rec);

	C2_ENTRY("%llu", (unsigned long long)l->l_id);
	pl = c2_layout_to_pdl(l);
	pl_rec.pr_let_id = pl->pl_base.sl_enum->le_type->let_id;
	pl_rec.pr_attr   = pl->pl_attr;

	nbytes = c2_bufvec_cursor_copyto(out, &pl_rec, sizeof pl_rec);
	C2_ASSERT(nbytes == sizeof pl_rec);

	e = pl->pl_base.sl_enum;
	rc = e->le_ops->leo_encode(e, op, tx, out);
	if (rc != 0)
		C2_LOG("lid %llu, leo_encode() failed, rc %d",
		       (unsigned long long)l->l_id, rc);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/** Implementation of lo_recsize() for pdclust layout type. */
static c2_bcount_t pdclust_recsize(const struct c2_layout *l)
{
	struct c2_striped_layout *stl;
	struct c2_layout_enum    *e;
	c2_bcount_t               recsize;

	C2_PRE(l!= NULL);
	stl = c2_layout_to_striped(l);
	e = c2_striped_layout_to_enum(stl);
	recsize = sizeof(struct c2_layout_rec) +
		  sizeof(struct c2_layout_pdclust_rec) +
		  e->le_ops->leo_recsize(e);
	C2_POST(recsize <= c2_layout_max_recsize(l->l_dom));
	return recsize;
}

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
 * @pre  c2_forall(i, n, k[i] + i < n)
 * @pre  c2_forall(i, n, s[i] < n && ergo(s[i] == s[j], i == j))
 * @post c2_forall(i, n, s[i] < n && ergo(s[i] == s[j], i == j))
 * @post c2_forall(i, n, s[r[i]] == i && r[s[i]] == i)
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

/** Simple multiplicative hash. */
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
static uint64_t permute_column(struct c2_pdclust_instance *pi,
			       uint64_t omega, uint64_t t)
{
	struct tile_cache      *tc;
	struct c2_pdclust_attr  attr = pi->pi_layout->pl_attr;

	C2_ENTRY("t %lu, P %lu", (unsigned long)t, (unsigned long)attr.pa_P);
	C2_ASSERT(t < attr.pa_P);
	tc = &pi->pi_tile_cache;

	/* If cached values are for different tile, update the cache. */
	if (tc->tc_tile_no != omega) {
		uint32_t i;
		uint64_t rstate;

		/* Initialise columns array that will be permuted. */
		for (i = 0; i < attr.pa_P; ++i)
			tc->tc_permute[i] = i;

		/* Initialise PRNG. */
		rstate = hash(attr.pa_seed.u_hi) ^
			 hash(attr.pa_seed.u_lo + omega);

		/* Generate permutation number in lexicographic ordering. */
		for (i = 0; i < attr.pa_P - 1; ++i)
			tc->tc_lcode[i] = c2_rnd(attr.pa_P - i, &rstate);

		/* Apply the permutation. */
		permute(attr.pa_P, tc->tc_lcode,
			tc->tc_permute, tc->tc_inverse);
		tc->tc_tile_no = omega;
	}

	C2_ADDB_ADD(&pi->pi_layout->pl_base.sl_base.l_addb, &layout_addb_loc,
		    pdclust_tile_cache_hit, tc->tc_tile_no == omega);

	C2_POST(tc->tc_permute[t] < attr.pa_P);
	C2_POST(tc->tc_inverse[tc->tc_permute[t]] == t);
	C2_POST(tc->tc_permute[tc->tc_inverse[t]] == t);
	return tc->tc_permute[t];
}

void c2_pdclust_instance_map(struct c2_pdclust_instance *pi,
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

	C2_PRE(pdclust_instance_invariant(pi));

	C2_ENTRY("pi %p", pi);
	N = pi->pi_layout->pl_attr.pa_N;
	K = pi->pi_layout->pl_attr.pa_K;
	P = pi->pi_layout->pl_attr.pa_P;
	C = pi->pi_layout->pl_C;
	L = pi->pi_layout->pl_L;

	/*
	 * First translate source address into a tile number and parity group
	 * number in the tile.
	 */
	m_dec(C, src->sa_group, &omega, &j);
	/*
	 * Then, convert from C*(N+2*K) coordinates to L*P coordinates within a
	 * tile.
	 */
	m_dec(P, m_enc(N + 2*K, j, src->sa_unit), &r, &t);
	/* Permute columns */
	tgt->ta_obj = permute_column(pi, omega, t);
	/* And translate back from tile to target address. */
	tgt->ta_frame = m_enc(L, omega, r);
	C2_LEAVE("pi %p", pi);
}

void c2_pdclust_instance_inv(struct c2_pdclust_instance *pi,
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

	N = pi->pi_layout->pl_attr.pa_N;
	K = pi->pi_layout->pl_attr.pa_K;
	P = pi->pi_layout->pl_attr.pa_P;
	C = pi->pi_layout->pl_C;
	L = pi->pi_layout->pl_L;

	r = tgt->ta_frame;
	t = tgt->ta_obj;

	C2_ASSERT(pdclust_instance_invariant(pi));

	/*
	 * Execute inverses of the steps of c2_pdclust_instance_map() in
	 * reverse order.
	 */
	m_dec(L, tgt->ta_frame, &omega, &r);
	permute_column(pi, omega, t); /* Force tile cache update */
	t = pi->pi_tile_cache.tc_inverse[t];
	m_dec(N + 2*K, m_enc(P, r, t), &j, &src->sa_unit);
	src->sa_group = m_enc(C, omega, j);
}

static const struct c2_layout_instance_ops pdclust_instance_ops;
void pdclust_instance_fini(struct c2_layout_instance *li);

/**
 * Allocates and builds a parity de-clustered layout instance using the
 * supplied pdclust layout 'pl' and acquires an additional reference on
 * 'pl->pl_base.sl_base'.
 * @pre pdclust_invariant(pl)
 * @post ergo(rc == 0, pdclust_instance_invariant(*out) &&
 *                     pl->pl_base.sl_base.l_ref > 0))
 *
 * @note This layout instance object is to be finalised explicitly by the user,
 * using c2_layout_instance_fini().
 */
static int pdclust_instance_build(struct c2_layout           *l,
				  const struct c2_fid        *fid,
				  struct c2_layout_instance **out)
{
	struct c2_pdclust_layout   *pl = c2_layout_to_pdl(l);
	struct c2_pdclust_instance *pi;
	struct tile_cache          *tc = NULL; /* to keep gcc happy */
	uint32_t                    N;
	uint32_t                    K;
	uint32_t                    P;
	int                         rc;

	C2_PRE(pdclust_invariant(pl));
	C2_PRE(c2_fid_is_valid(fid));
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu, gfid container %llu, gfid key %llu",
		 (unsigned long long)l->l_id,
		 (unsigned long long)fid->f_container,
		 (unsigned long long)fid->f_key);
	N  = pl->pl_attr.pa_N;
	K  = pl->pl_attr.pa_K;
	P  = pl->pl_attr.pa_P;

	if (C2_FI_ENABLED("mem_err1")) { pi = NULL; goto err1_injected; }
	C2_ALLOC_PTR(pi);
err1_injected:
	if (pi != NULL) {
		tc = &pi->pi_tile_cache;

		if (C2_FI_ENABLED("mem_err2"))
			{ tc->tc_lcode = NULL; goto err2_injected; }
		C2_ALLOC_ARR(tc->tc_lcode, P);
		C2_ALLOC_ARR(tc->tc_permute, P);
		C2_ALLOC_ARR(tc->tc_inverse, P);
err2_injected:
		if (tc->tc_lcode != NULL &&
		    tc->tc_permute != NULL &&
		    tc->tc_inverse != NULL) {
			pi->pi_layout = pl;
			tc->tc_tile_no = 1;
			permute_column(pi, 0, 0);

			if (C2_FI_ENABLED("parity_math_err"))
				{ rc = -EPROTO; goto err3_injected; }
			rc = c2_parity_math_init(&pi->pi_math, N, K);
err3_injected:
			if (rc == 0) {
				c2_layout__instance_init(&pi->pi_base, fid,
							&pdclust_instance_ops);
				c2_pdclust_instance_bob_init(pi);
				c2_layout_get(l);
			}
			else
				C2_LOG("pi %p, c2_parity_math_init() failed, "
				       "rc %d", pi, rc);
		} else
			rc = -ENOMEM;
	} else
		rc = -ENOMEM;

	if (rc == 0) {
		*out = &pi->pi_base;
		C2_POST(pdclust_instance_invariant(pi));
		C2_POST(l->l_ref > 0);
	} else {
		if (rc == -ENOMEM)
			c2_layout__log("c2_pdclust_instance_build",
				       "C2_ALLOC() failed",
				       &c2_addb_oom, &l->l_addb, l->l_id, rc);
		if (pi != NULL) {
			c2_free(tc->tc_inverse);
			c2_free(tc->tc_permute);
			c2_free(tc->tc_lcode);
		}
		c2_free(pi);
	}

	C2_LEAVE("rc %d", rc);
	return rc;
}

/** Implementation of lio_fini(). */
void pdclust_instance_fini(struct c2_layout_instance *li)
{
	struct c2_pdclust_instance *pi;

	pi = bob_of(li, struct c2_pdclust_instance,
		    pi_base, &pdclust_instance_bob);
	C2_PRE(pdclust_instance_invariant(pi));

	C2_ENTRY("pi %p", pi);
	c2_layout_put(&pi->pi_layout->pl_base.sl_base);
	c2_layout__instance_fini(&pi->pi_base);
	c2_pdclust_instance_bob_fini(pi);
	c2_parity_math_fini(&pi->pi_math);
	c2_free(pi->pi_tile_cache.tc_inverse);
	c2_free(pi->pi_tile_cache.tc_permute);
	c2_free(pi->pi_tile_cache.tc_lcode);
	c2_free(pi);
	C2_LEAVE();
}

struct c2_pdclust_instance *c2_layout_instance_to_pdi(
					const struct c2_layout_instance *li)
{
	struct c2_pdclust_instance *pi;
	pi = bob_of(li, struct c2_pdclust_instance, pi_base,
		    &pdclust_instance_bob);
	C2_POST(pdclust_instance_invariant(pi));
	return pi;
}

static const struct c2_layout_ops pdclust_ops = {
	.lo_fini           = pdclust_fini,
	.lo_delete         = pdclust_delete,
	.lo_recsize        = pdclust_recsize,
	.lo_instance_build = pdclust_instance_build,
	.lo_decode         = pdclust_decode,
	.lo_encode         = pdclust_encode
};

static const struct c2_layout_type_ops pdclust_type_ops = {
	.lto_register    = pdclust_register,
	.lto_unregister  = pdclust_unregister,
	.lto_max_recsize = pdclust_max_recsize,
	.lto_allocate    = pdclust_allocate,
};

struct c2_layout_type c2_pdclust_layout_type = {
	.lt_name      = "pdclust",
	.lt_id        = 0,
	.lt_ref_count = 0,
	.lt_domain    = NULL,
	.lt_ops       = &pdclust_type_ops
};

static const struct c2_layout_instance_ops pdclust_instance_ops = {
	.lio_fini    = pdclust_instance_fini,
	.lio_to_enum = pdclust_instance_to_enum
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
