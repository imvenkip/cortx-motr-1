/* -*- C -*- */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <err.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;
const int MAX = 10;

int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	int                   result;
	struct c2_ext         ext[MAX];
	c2_bcount_t	      count = 539;
	int		      i = 0;
	time_t		      now;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <db-dir>\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	memset(ext, 0, ARRAY_SIZE(ext));

	time(&now);
	srand(now);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
	C2_ASSERT(result == 0);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init
		(&colibri_balloc.cb_ballroom, &db, 12);
	
	for (i = 0; i < MAX && result == 0; i++ ) {
		count = rand() % 2500;
		result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(&colibri_balloc.cb_ballroom, &dtx, count, &ext[i]);
		printf("rc = %d: requested count=%5d, result count=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			result, (int)count,
			(int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
	}

	/* randonmize the array */
	for (i = 0; i < MAX; i++ ) {
		int a, b;
		a = rand() % MAX;
		b = rand() % MAX;
		C2_SWAP(ext[a], ext[b]);
	}

	for (i = 0; i < MAX && result == 0; i++ ) {
		if (ext[i].e_start != 0)
			result = colibri_balloc.cb_ballroom.ab_ops->bo_free(&colibri_balloc.cb_ballroom, &dtx, &ext[i]);
		printf("rc = %d: freed: len=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			result, (int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
	}

	result = c2_db_tx_commit(&dtx.tx_dbtx);
	C2_ASSERT(result == 0);
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
