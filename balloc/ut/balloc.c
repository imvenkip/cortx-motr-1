/* -*- C -*- */

#include <stdio.h>        /* fprintf */
#include <errno.h>
#include <err.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;

int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	int                   result;
	struct c2_ext         ext = { 0 };
	c2_bcount_t	      count = 4096 * 539;

	if (argc != 2) {
		fprintf(stderr, "Usage: elist <db-dir>\n");
		return 1;
	}
	db_name = argv[1];

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
	C2_ASSERT(result == 0);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init
		(&colibri_balloc.cb_ballroom, &db, 12);
	
	if (result == 0) {
//		result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(&colibri_balloc.cb_ballroom, &dtx, count, &ext);
		printf("rc = %d: count=%d [%08llx,%08llx)\n", result, (int)count,
			(unsigned long long)ext.e_start,
			(unsigned long long)ext.e_end);
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
