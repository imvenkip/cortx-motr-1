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
#include "lib/ut.h"
#include "balloc/balloc.h"

int main(int argc, char **argv)
{
	struct m0_balloc     *mero_balloc;
	const char           *db_name;
	struct m0_dbenv       db;
	struct m0_dtx         dtx;
	m0_bcount_t	      gn;
	int                   result;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <db-dir> groupno\n", argv[0]);
		return 1;
	}
	db_name = argv[1];
	gn = atoll(argv[2]);

	result = m0_dbenv_init(&db, db_name, 0);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&dtx.tx_dbtx, &db, 0);
	M0_ASSERT(result == 0);

	m0_balloc_allocate(0, &mero_balloc);

	result = mero_balloc->cb_ballroom.ab_ops->bo_init
		(&mero_balloc->cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if (result == 0) {
		struct m0_balloc_group_info *grp =
			m0_balloc_gn2info(mero_balloc, gn);
		if (grp) {
			m0_balloc_lock_group(grp);
			result = m0_balloc_load_extents(
				    mero_balloc, grp, &dtx.tx_dbtx);
			if (result == 0)
				m0_balloc_debug_dump_group_extent(argv[0], grp);
			m0_balloc_release_extents(grp);
			m0_balloc_unlock_group(grp);
		}
	}
	result = m0_db_tx_commit(&dtx.tx_dbtx);
	M0_ASSERT(result == 0);
	mero_balloc->cb_ballroom.ab_ops->bo_fini(&mero_balloc->cb_ballroom);

	m0_dbenv_fini(&db);
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
