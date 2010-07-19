#ifndef __COLIBRI_LIB_UB_H_
#define __COLIBRI_LIB_UB_H_

#include "lib/cdefs.h"

/**
   @defgroup ub Unit Benchmarking.

   @{
 */

struct c2_ub_bench {
	const char *ut_name;
	uint32_t    ut_iter;
	void      (*ut_round)(int iter);
	double      ut_total;
	double      ut_square;
	double      ut_min;
	double      ut_max;
};

struct c2_ub_set {
	const char        *us_name;
	void             (*us_init)(void);
	void             (*us_fini)(void);
	struct c2_ub_set  *us_prev;
	struct c2_ub_bench us_run[];
};

void c2_ub_set_add(struct c2_ub_set *set);
void c2_ub_run(uint32_t rounds);

/** @} end of ub group. */

/* __COLIBRI_LIB_UB_H_ */
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
