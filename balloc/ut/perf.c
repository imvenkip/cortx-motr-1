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

unsigned long timesub(struct timeval *begin, struct timeval *end) {
	unsigned long interval =
		(unsigned long)((end->tv_sec - begin->tv_sec) * 1000000 +
		                end->tv_usec - begin->tv_usec
			        );
	if (interval == 0)
		interval = 1;
	return interval;
}


int main(int argc, char **argv)
{
	struct m0_balloc     *mero_balloc;
	const char           *db_name = NULL;
	struct m0_dbenv       db;
	struct m0_dtx         dtx;
	int                   result;
	struct m0_ext         *ext;
	struct m0_ext         tmp;
	m0_bcount_t	      count = 0;
	m0_bcount_t	      target;
	int		      loops = DEF;
	bool		      g;
	int		      r = 0;
	int		      i = 0;
	bool		      verbose;
	time_t		      now;
	struct timeval	      alloc_begin, alloc_end;
	unsigned long	      alloc_usec = 0;
	struct timeval	      free_begin, free_end;
	unsigned long	      free_usec = 0;


        result = M0_GETOPTS("perf", argc, argv,
                            M0_STRINGARG('d', "db-dir",
                                       LAMBDA(void, (const char *string) {
                                               db_name = string; })),
                            M0_FORMATARG('l', "loops to run", "%i", &loops),
                            M0_FORMATARG('r', "randomize the result", "%i", &r),
                            M0_FORMATARG('c', "count to alloc", "%lu",
					 &count),
                            M0_FLAGARG('v', "verbose", &verbose),
                            M0_FLAGARG('g', "use goal or not", &g));
        if (result != 0)
                return result;

	if (db_name == NULL) {
		fprintf(stderr, "Specify <db-dir>. -? for usage.\n");
		return -EINVAL;
	}

	if (loops <= 0 || loops > MAX)
		loops = DEF;
	printf("dbdir=%s, loops=%d, r=%d, count=%d, g=%d, verbose=%d\n",
		db_name, loops, r, (int)count, g, verbose);

	ext = malloc(loops * sizeof (struct m0_ext));
	if (ext == NULL)
		return -ENOMEM;

	memset(ext, 0, loops * sizeof (struct m0_ext));

	time(&now); srand(now);

	result = m0_dbenv_init(&db, db_name, 0);
	M0_ASSERT(result == 0);

	m0_balloc_allocate(0, &mero_balloc);

	result = mero_balloc->cb_ballroom.ab_ops->bo_init
		(&mero_balloc->cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	for (i = 0; i < loops && result == 0; i++ ) {
		if (count > 0)
			target = count;
		else do  {
			target= rand() % 1500;
		} while (target == 0);

		result = m0_db_tx_init(&dtx.tx_dbtx, &db, 0);
		M0_ASSERT(result == 0);

		if (g)
			tmp.e_start = tmp.e_end;
		else
			tmp.e_start = 0;

		gettimeofday(&alloc_begin, NULL);
		result = mero_balloc->cb_ballroom.ab_ops->bo_alloc(
			    &mero_balloc->cb_ballroom, &dtx, target, &tmp);
		gettimeofday(&alloc_end, NULL);
		alloc_usec += timesub(&alloc_begin, &alloc_end);
		ext[i] = tmp;
		if (verbose)
		printf("%d: rc = %d: requested count=%5d, result count=%5d:"
		       " [%08llx,%08llx)=[%8llu,%8llu)\n",
			i, result, (int)count,
			(int)m0_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
		if (result == 0 )
			m0_db_tx_commit(&dtx.tx_dbtx);
		else
			m0_db_tx_abort(&dtx.tx_dbtx);
		if (result == -ENOSPC) {
			result = 0;
			break;
		}
	}
	printf("==================\nPerf: alloc/sec = %lu\n",
	       (unsigned long)loops * 1000000 / alloc_usec);

	/* randomize the array */
	if (r) {
		for (i = 0; i < loops * 2; i++ ) {
			int a, b;
			a = rand() % loops;
			b = rand() % loops;
			M0_SWAP(ext[a], ext[b]);
		}
	}

	for (i = loops - 1; i >= 0 && result == 0; i-- ) {
		result = m0_db_tx_init(&dtx.tx_dbtx, &db, 0);
		M0_ASSERT(result == 0);

		gettimeofday(&free_begin, NULL);
		if (ext[i].e_start !=
		    0) result = mero_balloc->cb_ballroom.ab_ops->bo_free(
				&mero_balloc->cb_ballroom, &dtx, &ext[i]);
		gettimeofday(&free_end, NULL);
		free_usec += timesub(&free_begin, &free_end);
		if (verbose)
		printf("%d: rc = %d: freed: len=%5d: [%08llx,%08llx)=[%8llu,"
		       "%8llu)\n",
			i, result, (int)m0_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);

		if (result == 0 )
			m0_db_tx_commit(&dtx.tx_dbtx);
		else
			m0_db_tx_abort(&dtx.tx_dbtx);
	}


	mero_balloc->cb_ballroom.ab_ops->bo_fini(&mero_balloc->cb_ballroom);
	m0_dbenv_fini(&db);
	printf("==================\nPerf: free/sec = %lu\n",
	       (unsigned long)loops * 1000000 / free_usec);
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
