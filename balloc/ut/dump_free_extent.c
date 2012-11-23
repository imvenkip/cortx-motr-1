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

int main(int argc, char **argv)
{
	struct c2_balloc     *colibri_balloc;
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

	c2_balloc_allocate(0, &colibri_balloc);

	result = colibri_balloc->cb_ballroom.ab_ops->bo_init
		(&colibri_balloc->cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if (result == 0) {
		struct c2_balloc_group_info *grp =
			c2_balloc_gn2info(colibri_balloc, gn);
		if (grp) {
			c2_balloc_lock_group(grp);
			result = c2_balloc_load_extents(
				    colibri_balloc, grp, &dtx.tx_dbtx);
			if (result == 0)
				c2_balloc_debug_dump_group_extent(argv[0], grp);
			c2_balloc_release_extents(grp);
			c2_balloc_unlock_group(grp);
		}
	}
	result = c2_db_tx_commit(&dtx.tx_dbtx);
	C2_ASSERT(result == 0);
	colibri_balloc->cb_ballroom.ab_ops->bo_fini(&colibri_balloc->cb_ballroom);

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
