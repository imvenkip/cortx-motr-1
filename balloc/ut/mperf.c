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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 09/02/2010
 */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "dtm/dtm.h"      /* m0_dtx */
#include "lib/arith.h"    /* M0_3WAY, m0_uint128 */
#include "lib/misc.h"     /* M0_SET0 */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/getopts.h"
#include "db/db.h"
#include "ut/ut.h"
#include "balloc/balloc.h"

const int MAX = 1000 * 1000;
const int DEF = 1000 * 1;
const int THREAD_COUNT = 10;
int		      loops;
m0_bcount_t	      count = 32;

unsigned long timesub(struct timeval *begin, struct timeval *end) {
	unsigned long interval =
		(unsigned long)((end->tv_sec - begin->tv_sec) * 1000000 +
		                end->tv_usec - begin->tv_usec
			        );
	if (interval == 0)
		interval = 1;
	return interval;
}

void alloc_free(struct m0_balloc *mero_balloc)
{
	struct m0_dtx         dtx;
	struct m0_ext         *ext;
	struct m0_ext         tmp = {0};
	int		      i, alloc;
	int                   result = 0;
	struct timeval	      alloc_begin, alloc_end;
	unsigned long	      alloc_usec = 0;
	struct timeval	      free_begin, free_end;
	unsigned long	      free_usec = 0;


	ext = malloc(loops * sizeof (struct m0_ext));
	if (ext == NULL)
		return;

	memset(ext, 0, loops * sizeof (struct m0_ext));

	printf("Hello from %lx: count=%d\n", (long)pthread_self(),(int) count);
	for (i = 0; i < loops && result == 0; i++ ) {

repeat:
		result = m0_db_tx_init(&dtx.tx_dbtx, mero_balloc->cb_dbenv, 0);
		M0_ASSERT(result == 0);
		tmp.e_start = tmp.e_end;
		gettimeofday(&alloc_begin, NULL);
		result = mero_balloc->cb_ballroom.ab_ops->bo_alloc(
			    &mero_balloc->cb_ballroom, &dtx, count, &ext[i]);
		gettimeofday(&alloc_end, NULL);
		alloc_usec += timesub(&alloc_begin, &alloc_end);
		tmp = ext[i];
		if (result == 0 )
			m0_db_tx_commit(&dtx.tx_dbtx);
		else
			m0_db_tx_abort(&dtx.tx_dbtx);
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
			(int)m0_ext_length(&ext[i]),
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
		M0_SWAP(ext[a], ext[b]);
	}
	*/
	result = 0;

	for (i = alloc - 1; i >= 0 && result == 0; i-- ) {

repeat_free:
		result = m0_db_tx_init(&dtx.tx_dbtx, mero_balloc->cb_dbenv, 0);
		M0_ASSERT(result == 0);

		gettimeofday(&free_begin, NULL);
		if (ext[i].e_start != 0)
			result = mero_balloc->cb_ballroom.ab_ops->bo_free(
				    &mero_balloc->cb_ballroom, &dtx, &ext[i]);
		gettimeofday(&free_end, NULL);
		free_usec += timesub(&free_begin, &free_end);
		if (result == 0 )
			m0_db_tx_commit(&dtx.tx_dbtx);
		else
			m0_db_tx_abort(&dtx.tx_dbtx);
		if (result == -EDEADLK) {
			usleep( rand() & 0xfff);
			goto repeat_free;
		}
#ifdef BALLOC_DEBUG
		printf("%lx %5d: rc = %d: freed: len=%5d: [%08llx,%08llx)="
		       "[%10llu,%10llu)\n",
		       (long)pthread_self(), i, result,
		       (int)m0_ext_length(&ext[i]),
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
	struct m0_balloc     *mero_balloc;
	const char           *db_name;
	struct m0_dbenv       db;
	int		      i = 0;
	int                   result;
	time_t		      now;

	int 		       num_threads;
	struct m0_thread      *threads;

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

	result = m0_dbenv_init(&db, db_name, 0);
	M0_ASSERT(result == 0);

	threads = m0_alloc(num_threads * sizeof (struct m0_thread));
	M0_ASSERT(threads != NULL);

	result = m0_balloc_allocate(0, &mero_balloc);
	M0_ASSERT(result == 0);

	result = mero_balloc->cb_ballroom.ab_ops->bo_init
		(&mero_balloc->cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	M0_ASSERT(result == 0);
	for (i = 0; i < num_threads; i++) {
		result = M0_THREAD_INIT(&threads[i], struct m0_balloc*, NULL,
					&alloc_free, mero_balloc,
					"alloc_free%d", i);
		M0_ASSERT(result == 0);
	}
	for (i = 0; i < num_threads; i++) {
		result = m0_thread_join(&threads[i]);
		M0_ASSERT(result == 0);
	}
#ifdef BALLOC_DEBUG
	printf("all threads exited\n");
#endif
	mero_balloc->cb_ballroom.ab_ops->bo_fini(&mero_balloc->cb_ballroom);
	m0_dbenv_fini(&db);

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
