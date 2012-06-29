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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 09/02/2010
 */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "dtm/dtm.h"      /* c2_dtx */
#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/getopts.h"
#include "db/db.h"
#include "lib/ut.h"
#include "balloc/balloc.h"

const int MAX = 1000 * 1000;
const int DEF = 1000 * 1;
const int THREAD_COUNT = 10;
int		      loops;
c2_bcount_t	      count = 32;

unsigned long timesub(struct timeval *begin, struct timeval *end) {
	unsigned long interval =
		(unsigned long)((end->tv_sec - begin->tv_sec) * 1000000 +
		                end->tv_usec - begin->tv_usec
			        );
	if (interval == 0)
		interval = 1;
	return interval;
}

void alloc_free(struct c2_balloc *colibri_balloc)
{
	struct c2_dtx         dtx;
	struct c2_ext         *ext;
	struct c2_ext         tmp = {0};
	int		      i, alloc;
	int                   result = 0;
	struct timeval	      alloc_begin, alloc_end;
	unsigned long	      alloc_usec = 0;
	struct timeval	      free_begin, free_end;
	unsigned long	      free_usec = 0;


	ext = malloc(loops * sizeof (struct c2_ext));
	if (ext == NULL)
		return;

	memset(ext, 0, loops * sizeof (struct c2_ext));

	printf("Hello from %lx: count=%d\n", (long)pthread_self(),(int) count);
	for (i = 0; i < loops && result == 0; i++ ) {

repeat:
		result = c2_db_tx_init(&dtx.tx_dbtx, colibri_balloc->cb_dbenv, 0);
		C2_ASSERT(result == 0);
		tmp.e_start = tmp.e_end;
		gettimeofday(&alloc_begin, NULL);
		result = colibri_balloc->cb_ballroom.ab_ops->bo_alloc(
			    &colibri_balloc->cb_ballroom, &dtx, count, &ext[i]);
		gettimeofday(&alloc_end, NULL);
		alloc_usec += timesub(&alloc_begin, &alloc_end);
		tmp = ext[i];
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
#ifdef BALLOC_DEBUG

		printf("%lx %5d: rc = %d: requested count=%5d, result count=%5d:"
		       "[%08llx,%08llx)=[%10llu,%10llu)\n",
			(long)pthread_self(), i, result, (int)count,
			(int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
#endif
	}

	printf("=======%lx====%d========\tPerf: alloc/sec = %lu\n",
	       (long)pthread_self(), i, (unsigned long)i * 1000000 / alloc_usec);
	alloc = i;

	/* Uncomment the following region to perform a worst case test */
	/* randomize the array */
	/*
	for (i = 0; i < alloc * 2; i++ ) {
		int a, b;
		a = rand() % alloc;
		b = rand() % alloc;
		C2_SWAP(ext[a], ext[b]);
	}
	*/
	result = 0;

	for (i = alloc - 1; i >= 0 && result == 0; i-- ) {

repeat_free:
		result = c2_db_tx_init(&dtx.tx_dbtx, colibri_balloc->cb_dbenv, 0);
		C2_ASSERT(result == 0);

		gettimeofday(&free_begin, NULL);
		if (ext[i].e_start != 0)
			result = colibri_balloc->cb_ballroom.ab_ops->bo_free(
				    &colibri_balloc->cb_ballroom, &dtx, &ext[i]);
		gettimeofday(&free_end, NULL);
		free_usec += timesub(&free_begin, &free_end);
		if (result == 0 )
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
		if (result == -EDEADLK) {
			usleep( rand() & 0xfff);
			goto repeat_free;
		}
#ifdef BALLOC_DEBUG
		printf("%lx %5d: rc = %d: freed: len=%5d: [%08llx,%08llx)="
		       "[%10llu,%10llu)\n",
		       (long)pthread_self(), i, result,
		       (int)c2_ext_length(&ext[i]),
		       (unsigned long long)ext[i].e_start,
		       (unsigned long long)ext[i].e_end,
		       (unsigned long long)ext[i].e_start,
		       (unsigned long long)ext[i].e_end);
#endif
	}

	printf("=======%lx====%d========\tPerf: free/sec  = %lu\n",
	       (long)pthread_self(), i, (unsigned long)alloc * 1000000
	       / free_usec);
}

int main(int argc, char **argv)
{
	struct c2_balloc     *colibri_balloc;
	const char           *db_name;
	struct c2_dbenv       db;
	int		      i = 0;
	int                   result;
	time_t		      now;

	int 		       num_threads;
	struct c2_thread      *threads;

	if (argc != 5) {
		fprintf(stderr, "Usage: %s <db-dir> number_of_loops"
			"num_of_threads num_of_blocks\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	loops = atoi(argv[2]);
	if (loops <= 0 || loops > MAX)
		loops = DEF;
	num_threads = atoi(argv[3]);
	if (num_threads <=0 || num_threads > 50)
		num_threads = THREAD_COUNT;

	count = atoi(argv[4]);
	if (count <= 0 || count > 2048)
		count = 32;

	time(&now); srand(now);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	threads = c2_alloc(num_threads * sizeof (struct c2_thread));
	C2_ASSERT(threads != NULL);

	result = c2_balloc_allocate(&colibri_balloc);
	C2_ASSERT(result == 0);

	result = colibri_balloc->cb_ballroom.ab_ops->bo_init
		(&colibri_balloc->cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	C2_ASSERT(result == 0);
	for (i = 0; i < num_threads; i++) {
		result = C2_THREAD_INIT(&threads[i], struct c2_balloc*, NULL,
					&alloc_free, colibri_balloc,
					"alloc_free%d", i);
		C2_ASSERT(result == 0);
	}
	for (i = 0; i < num_threads; i++) {
		result = c2_thread_join(&threads[i]);
		C2_ASSERT(result == 0);
	}
#ifdef BALLOC_DEBUG
	printf("all threads exited\n");
#endif
	colibri_balloc->cb_ballroom.ab_ops->bo_fini(&colibri_balloc->cb_ballroom);
	c2_dbenv_fini(&db);

#ifdef BALLOC_DEBUG
	printf("done\n");
#endif
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
