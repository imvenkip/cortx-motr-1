/* -*- C -*- */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* atoll */
#include <errno.h>
#include <err.h>

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;
extern void c2_balloc_debug_dump_group_extent(const char *tag, struct c2_balloc_group_info *grp);

extern struct c2_balloc_group_info * c2_balloc_gn2info(struct c2_balloc *cb,
						       c2_bindex_t groupno);
extern int c2_balloc_load_extents(struct c2_balloc_group_info *grp, struct c2_db_tx *tx);


int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	c2_bcount_t	      gn;
	int                   result;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <db-dir> groupno\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	gn = atoll(argv[2]);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
	C2_ASSERT(result == 0);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init(&colibri_balloc.cb_ballroom, &db);

	if (result == 0) {
		struct c2_balloc_group_info *grp = c2_balloc_gn2info(&colibri_balloc, gn);
		if (grp) {
			result = c2_balloc_load_extents(grp, &dtx.tx_dbtx);
			if (result == 0)
				c2_balloc_debug_dump_group_extent(argv[0], grp);
		}
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
