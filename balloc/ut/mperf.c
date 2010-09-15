/* -*- C -*- */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;
const int MAX = 1000 * 1000;
const int DEF = 1000 * 1;
const int THREAD_COUNT = 10;
int		      loops;

unsigned long timesub(struct timeval *begin, struct timeval *end) {
	unsigned long interval = 
		(unsigned long)((end->tv_sec - begin->tv_sec) * 1000000 +
		                (end->tv_usec - begin->tv_usec)
			        );
	if (interval == 0)
		interval = 1;
	return interval;
}

void alloc_free(struct c2_balloc *cb)
{
	struct c2_dtx         dtx;
	struct c2_ext         *ext;
	c2_bcount_t	      count = 539;
	int		      i, alloc;
	int                   result = 0;
	struct timeval	      alloc_begin, alloc_end;
	unsigned long	      alloc_usec;
	struct timeval	      free_begin, free_end;
	unsigned long	      free_usec;

	printf("Hello from %lx\n", (long)pthread_self());

	ext = malloc(loops * sizeof (struct c2_ext));
	if (ext == NULL)
		return;

	memset(ext, 0, loops * sizeof (struct c2_ext));

	gettimeofday(&alloc_begin, NULL);
	for (i = 0; i < loops && result == 0; i++ ) {
		do  {
			count = rand() % 1500;
		} while (count == 0);

repeat:
		result = c2_db_tx_init(&dtx.tx_dbtx, cb->cb_dbenv, 0);
		C2_ASSERT(result == 0);

		result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(&colibri_balloc.cb_ballroom, &dtx, count, &ext[i]);
		if (result == 0 )
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
		if (result == -ENOSPC) {
			result = 0;
			break;
		}
		if (result == -EDEADLK) {
			usleep( rand() & 0xfff);
			goto repeat;
		}
/*
		printf("%lx %5d: rc = %d: requested count=%5d, result count=%5d: [%08llx,%08llx)=[%10llu,%10llu)\n",
			(long)pthread_self(), i, result, (int)count,
			(int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
*/
	}
	gettimeofday(&alloc_end, NULL);
	alloc_usec = timesub(&alloc_begin, &alloc_end);

	printf("=======%lx====%d========\tPerf: alloc/sec = %lu\n", (long)pthread_self(), i, (unsigned long)i * 1000000 / alloc_usec);
	alloc = i;
	/* randonmize the array */
	for (i = 0; i < alloc * 2; i++ ) {
		int a, b;
		a = rand() % alloc;
		b = rand() % alloc;
		C2_SWAP(ext[a], ext[b]);
	}
	result = 0;

	gettimeofday(&free_begin, NULL);
	for (i = 0; i < alloc && result == 0; i++ ) {

repeat_free:
		result = c2_db_tx_init(&dtx.tx_dbtx, cb->cb_dbenv, 0);
		C2_ASSERT(result == 0);

		if (ext[i].e_start != 0)
			result = colibri_balloc.cb_ballroom.ab_ops->bo_free(&colibri_balloc.cb_ballroom, &dtx, &ext[i]);
		if (result == 0 )
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
		if (result == -EDEADLK) {
			usleep( rand() & 0xfff);
			goto repeat_free;
		}
/*
		printf("%lx %5d: rc = %d: freed: len=%5d: [%08llx,%08llx)=[%10llu,%10llu)\n",
			(long)pthread_self(), i, result, (int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
*/
	}

	gettimeofday(&free_end, NULL);
	free_usec = timesub(&free_begin, &free_end);

	printf("=======%lx====%d========\tPerf: free/sec  = %lu\n", (long)pthread_self(), i, (unsigned long)i * 1000000 / free_usec);
}

int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	int		      i = 0;
	int                   result;
	time_t		      now;

	struct c2_thread      *threads;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <db-dir> number_of_loops\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	loops = atoi(argv[2]);
	if (loops <= 0 || loops > MAX)
		loops = DEF;

	time(&now); srand(now);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	threads = c2_alloc(THREAD_COUNT * sizeof (struct c2_thread));
	C2_ASSERT(threads != NULL);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init(&colibri_balloc.cb_ballroom, &db, 12);
	C2_ASSERT(result == 0);
	for (i = 0; i < THREAD_COUNT; i++) {
		result = C2_THREAD_INIT(&threads[i], struct c2_balloc*, NULL, &alloc_free, &colibri_balloc);
		C2_ASSERT(result == 0);
	}
	for (i = 0; i < THREAD_COUNT; i++) {
		result = c2_thread_join(&threads[i]);
		C2_ASSERT(result == 0);
	}

	printf("all threads exited\n");
	colibri_balloc.cb_ballroom.ab_ops->bo_fini(&colibri_balloc.cb_ballroom);
	c2_dbenv_fini(&db);
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
