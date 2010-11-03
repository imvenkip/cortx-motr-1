/* -*- C -*- */

#ifndef __COLIBRI_SNS_PARITY_OPS_H__
#define __COLIBRI_SNS_PARITY_OPS_H__

#include "galois.h"
#include "lib/assert.h"

#define C2_PARITY_ZERO (0)
#define C2_PARITY_GALOIS_W (8)
typedef int c2_parity_elem_t;

//void c2_parity_init(void)

static void c2_parity_init(void) __attribute__((unused));
static void c2_parity_init(void)
{
	int ret = galois_create_mult_tables(C2_PARITY_GALOIS_W);
	C2_ASSERT(ret == 0);
}

static void c2_parity_fini(void) __attribute__((unused));
static void c2_parity_fini(void)
{
	/* galois_calc_tables_release(); */
}

static inline c2_parity_elem_t c2_parity_add(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x ^ y;
}

static inline c2_parity_elem_t c2_parity_sub(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x ^ y;
}

static inline c2_parity_elem_t c2_parity_mul(c2_parity_elem_t x, c2_parity_elem_t y)
{
	/* return galois_single_multiply(x, y, C2_PARITY_GALOIS_W); */
	return galois_multtable_multiply(x, y, C2_PARITY_GALOIS_W);
}

static inline c2_parity_elem_t c2_parity_div(c2_parity_elem_t x, c2_parity_elem_t y)
{
	/* return galois_single_divide(x, y, C2_PARITY_GALOIS_W); */
	return galois_multtable_divide(x, y, C2_PARITY_GALOIS_W);
}

static inline c2_parity_elem_t c2_parity_lt(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x < y;
}

static inline c2_parity_elem_t c2_parity_gt(c2_parity_elem_t x, c2_parity_elem_t y)
{
	return x > y;
}

/* __COLIBRI_SNS_PARITY_OPS_H__ */
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
