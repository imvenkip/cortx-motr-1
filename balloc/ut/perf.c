/* -*- C -*- */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;
const int MAX = 1000 * 1000;
const int DEF = 1000 * 1;


unsigned long timesub(struct timeval *begin, struct timeval *end) {
	return
		(unsigned long)((end->tv_sec - begin->tv_sec) * 1000000 +
		                (end->tv_usec - begin->tv_usec)
			        );
}


int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	int                   result;
	struct c2_ext         *ext;
	c2_bcount_t	      count = 539;
	int		      loops;
	int		      i = 0;
	time_t		      now;
	struct timeval	      alloc_begin, alloc_end;
	unsigned long	      alloc_usec;
	struct timeval	      free_begin, free_end;
	unsigned long	      free_usec;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <db-dir> number_of_loops\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	loops = atoi(argv[2]);
	if (loops <= 0 || loops > MAX)
		loops = DEF;

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
		do  {
			count = rand() % 1500;
		} while (count == 0);

		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);

		result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(&colibri_balloc.cb_ballroom, &dtx, count, &ext[i]);
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
	}
	gettimeofday(&alloc_end, NULL);
	alloc_usec = timesub(&alloc_begin, &alloc_end);

	/* randonmize the array */
	for (i = 0; i < loops * 2; i++ ) {
		int a, b;
		a = rand() % loops;
		b = rand() % loops;
		C2_SWAP(ext[a], ext[b]);
	}

	gettimeofday(&free_begin, NULL);
	for (i = 0; i < loops && result == 0; i++ ) {
		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);

		if (ext[i].e_start != 0)
			result = colibri_balloc.cb_ballroom.ab_ops->bo_free(&colibri_balloc.cb_ballroom, &dtx, &ext[i]);
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
