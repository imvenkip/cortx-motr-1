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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/20/2010
 */

#ifndef _ISOC99_SOURCE
#define _ISOC99_SOURCE /* INFINITY */
#endif

#include <stdio.h>     /* printf */
#include <sys/time.h>  /* gettimeofday */
#include <math.h>      /* sqrt */

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/arith.h"
#include "lib/ub.h"

/**
   @addtogroup ub
   @{
 */

static struct m0_ub_set *last = NULL;

M0_INTERNAL void m0_ub_set_print(void)
{
	const struct m0_ub_set *set;

	printf("Available benchmarks:\n");
	for (set = last; set != NULL; set = set->us_prev)
		printf("%s\n", set->us_name);
}

M0_INTERNAL int m0_ub_set_select(const char *name)
{
	struct m0_ub_set *set;

	for (set = last; set != NULL; set = set->us_prev) {
		if (strcmp(set->us_name, name) == 0) {
			last = set;
			last->us_prev = NULL;
			return 0;
		}
	}
	printf("No such benchmark: %s\n", name);
	return -ENOENT;
}

M0_INTERNAL void m0_ub_set_add(struct m0_ub_set *set)
{
	M0_PRE(set->us_prev == NULL);

	set->us_prev = last;
	last = set;
}

/* normalize struct timeval, so that microseconds field (tv_usec) is less than
   one million */
static void timeval_norm(struct timeval *t)
{
	while (t->tv_usec < 0) {
		t->tv_sec--;
		t->tv_usec += 1000000;
	}
	while (t->tv_usec >= 1000000) {
		t->tv_sec++;
		t->tv_usec -= 1000000;
	}
}

static void timeval_diff(const struct timeval *start, const struct timeval *end,
			 struct timeval *diff)
{
	diff->tv_sec += end->tv_sec - start->tv_sec;
	/* this relies on tv_usec being signed */
	diff->tv_usec += end->tv_usec - start->tv_usec;
	timeval_norm(diff);
}

double delay(const struct timeval *start, const struct timeval *end)
{
	struct timeval diff;

	M0_SET0(&diff);
	timeval_diff(start, end, &diff);
	return diff.tv_sec + ((double)diff.tv_usec)/1000000;
}

static void ub_run_one(const struct m0_ub_set *set, struct m0_ub_bench *bench)
{
	uint32_t       i;
	struct timeval start;
	struct timeval end;
	double         sec;

	printf(".");
	if (bench->ub_init != NULL)
		bench->ub_init();
	gettimeofday(&start, NULL);
	for (i = 0; i < bench->ub_iter; ++i)
		bench->ub_round(i);
	gettimeofday(&end, NULL);
	if (bench->ub_fini != NULL)
		bench->ub_fini();
	sec = delay(&start, &end);
	bench->ub_total += sec;
	bench->ub_square += sec * sec;
	bench->ub_max = max_type(double, bench->ub_max, sec);
	bench->ub_min = min_type(double, bench->ub_min, sec);
}

static void results_print(uint32_t round)
{
	const struct m0_ub_set   *set;
	const struct m0_ub_bench *bench;
	double                    avg;
	double                    std;

	printf("\t%12.12s: [%7s] %6s %6s %6s %5s %8s %8s\n",
	       "bench", "iter", "min", "max", "avg", "std", "sec/op", "op/sec");
	for (set = last; set != NULL; set = set->us_prev) {
		printf("set: %s\n", set->us_name);
		for (bench = &set->us_run[0]; bench->ub_name; ++bench) {
			avg = bench->ub_total / round;
			std = sqrt(bench->ub_square / round - avg * avg);
			printf("\t%12.12s: [%7i] %6.2f %6.2f %6.2f %5.2f%%"
			       " %8.3e/%8.3e\n", bench->ub_name, bench->ub_iter,
			       bench->ub_min, bench->ub_max, avg,
			       std * 100.0 / avg, avg / bench->ub_iter,
			       bench->ub_iter / avg);
		}
	}
}

M0_INTERNAL int m0_ub_run(uint32_t rounds, const char *opts)
{
	struct m0_ub_set   *set;
	struct m0_ub_bench *bench;
	uint32_t            round;
	int                 rc = 0;

	for (set = last; set != NULL; set = set->us_prev) {
		for (bench = &set->us_run[0]; bench->ub_name; ++bench) {
			bench->ub_total  = 0.0;
			bench->ub_square = 0.0;
			bench->ub_min    = INFINITY;
			bench->ub_max    = 0.0;
		}
	}

	for (round = 1; round <= rounds; ++round) {
		printf("-- round %2i ", round);
		for (set = last; set != NULL; set = set->us_prev) {
			printf("%s[", set->us_name);
			if (set->us_init != NULL) {
				rc = set->us_init(opts);
				if (rc != 0)
					goto end;
			}
			for (bench = &set->us_run[0]; bench->ub_name; ++bench)
				ub_run_one(set, bench);
			if (set->us_fini != NULL)
				set->us_fini();
			printf("]");
		}
		printf("\n");
		results_print(round);
	}
end:
	if (rc != 0)
		fprintf(stderr, "Benchmark initialisation failed: `%s',"
			" rc=%d\n", set->us_name, rc);
	return rc;
}

/** @} end of ub group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
