/* -*- C -*- */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "lib/thread.h"
#include "lib/getopts.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;
const int MAX = 1000 * 1000;
const int DEF = 1000 * 1;


unsigned long timesub(struct timeval *begin, struct timeval *end) {
	unsigned long interval = 
		(unsigned long)((end->tv_sec - begin->tv_sec) * 1000000 +
		                (end->tv_usec - begin->tv_usec)
			        );
	if (interval == 0)
		interval = 1;
	return interval;
}


int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	int                   result;
	struct c2_ext         *ext;
	struct c2_ext         tmp;
	c2_bcount_t	      count = 0;
	c2_bcount_t	      target;
	int		      loops = DEF;
	int		      g = 0;
	int		      r = 0;
	int		      i = 0;
	int		      verbose = 0;
	time_t		      now;
	struct timeval	      alloc_begin, alloc_end;
	unsigned long	      alloc_usec;
	struct timeval	      free_begin, free_end;
	unsigned long	      free_usec;


        result = C2_GETOPTS("perf", argc, argv,
                            C2_STRINGARG('d', "db-dir",
                                       LAMBDA(void, (const char *string) {
                                               db_name = string; })),
                            C2_NUMBERARG('l', "loops to run",
                                         LAMBDA(void, (int64_t num) { 
                                               loops = num; })),
                            C2_NUMBERARG('r', "randomize the result",
                                         LAMBDA(void, (int64_t num) { 
                                               r = num;})),
                            C2_NUMBERARG('c', "count to alloc",
                                         LAMBDA(void, (int64_t num) { 
                                               count = num;})),
                            C2_NUMBERARG('v', "verbose",
                                         LAMBDA(void, (int64_t num) { 
                                               verbose = num;})),
                            C2_NUMBERARG('g', "use goal or not",
                                       LAMBDA(void, (int64_t num) {
                                               g = num; })));
        if (result != 0)
                return result;

	if (loops <= 0 || loops > MAX)
		loops = DEF;
	printf("dbdir=%s, loops=%d, r=%d, count=%d, g=%d, verbose=%d\n",
		db_name, loops, r, (int)count, g, verbose);

	ext = malloc(loops * sizeof (struct c2_ext));
	if (ext == NULL)
		return -ENOMEM;

	memset(ext, 0, loops * sizeof (struct c2_ext));

	time(&now); srand(now);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init
		(&colibri_balloc.cb_ballroom, &db, 12);

	gettimeofday(&alloc_begin, NULL);
	for (i = 0; i < loops && result == 0; i++ ) {
		if (count > 0)
			target = count;
		else do  {
			target= rand() % 1500;
		} while (target == 0);

		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);

		if (g)
		tmp.e_start = tmp.e_end;

		result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(&colibri_balloc.cb_ballroom, &dtx, target, &tmp);
		ext[i] = tmp;
		if (verbose)
		printf("%d: rc = %d: requested count=%5d, result count=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			i, result, (int)count,
			(int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
		if (result == 0 )
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
		if (result == -ENOSPC) {
			result = 0;
			break;
		}
	}
	gettimeofday(&alloc_end, NULL);
	alloc_usec = timesub(&alloc_begin, &alloc_end);

	/* randonmize the array */
	if (r) {
		for (i = 0; i < loops * 2; i++ ) {
			int a, b;
			a = rand() % loops;
			b = rand() % loops;
			C2_SWAP(ext[a], ext[b]);
		}
	}

	gettimeofday(&free_begin, NULL);
	for (i = loops - 1; i >= 0 && result == 0; i-- ) {
		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);

		if (ext[i].e_start != 0)
			result = colibri_balloc.cb_ballroom.ab_ops->bo_free(&colibri_balloc.cb_ballroom, &dtx, &ext[i]);
		if (verbose)
		printf("%d: rc = %d: freed: len=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			i, result, (int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);

		if (result == 0 )
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
	}

	gettimeofday(&free_end, NULL);
	free_usec = timesub(&free_begin, &free_end);

	colibri_balloc.cb_ballroom.ab_ops->bo_fini(&colibri_balloc.cb_ballroom);
	c2_dbenv_fini(&db);
	printf("==================\nPerf: alloc/sec = %lu\n", (unsigned long)loops * 1000000 / alloc_usec);
	printf("==================\nPerf: free/sec = %lu\n", (unsigned long)loops * 1000000 / free_usec);
	printf("done\n");
	return 0;
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
