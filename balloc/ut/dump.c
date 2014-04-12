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
 *
 * Modified by: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Modification date: 12/07/2012
 */

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <string.h>       /* strcmp */
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

static int usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s -s <db-dir>\n"
			"       %s -g <db-dir> groupno\n"
			"       %s -f <db-dir> groupno\n"
			"\n"
			"  where:\n"
			"    -s  dump superblock\n"
			"    -g  dump group descriptor\n"
			"    -f  dump free extents\n",
			prog_name, prog_name, prog_name);

	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	struct m0_balloc     *mero_balloc;
	struct m0_dbenv       db;
	struct m0_dtx         dtx;
	const char           *db_name;
	m0_bcount_t           gn = 0;
	int                   result;

	enum action_type {
		DUMP_SUPER,
		DUMP_GROUP,
		DUMP_FREE,
	} action;

	if (argc < 3)
		return usage(argv[0]);

	if (strcmp(argv[1], "-s") == 0)
		action = DUMP_SUPER;
	else if (strcmp(argv[1], "-g") == 0)
		action = DUMP_GROUP;
	else if (strcmp(argv[1], "-f") == 0)
		action = DUMP_FREE;
	else
		return usage(argv[0]);

	switch (action) {
	case DUMP_GROUP:
	case DUMP_FREE:
		if (argc < 4)
			return usage(argv[0]);
		gn = atoll(argv[3]);
	case DUMP_SUPER:
		db_name = argv[2];
		break;
	default:
		M0_IMPOSSIBLE("invalid action");
	}

	result = m0_dbenv_init(&db, db_name, 0, true);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&dtx.tx_dbtx, &db, 0);
	M0_ASSERT(result == 0);

	m0_balloc_allocate(0, &mero_balloc);

	result = mero_balloc->cb_ballroom.ab_ops->bo_init
		(&mero_balloc->cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if (result == 0) {
		switch (action) {
		case DUMP_SUPER:
			m0_balloc_debug_dump_sb(argv[0], &mero_balloc->cb_sb);
			break;
		case DUMP_GROUP:
		case DUMP_FREE:
		{
			struct m0_balloc_group_info *grp =
				m0_balloc_gn2info(mero_balloc, gn);

			if (grp && action == DUMP_GROUP) {
				m0_balloc_debug_dump_group(argv[0], grp);
			} else if (grp && action == DUMP_FREE) {
				m0_balloc_lock_group(grp);
				result = m0_balloc_load_extents(
					    mero_balloc, grp, &dtx.tx_dbtx);
				if (result == 0)
					m0_balloc_debug_dump_group_extent(argv[0], grp);
				m0_balloc_release_extents(grp);
				m0_balloc_unlock_group(grp);
			}
			break;
		}
		default:
			M0_IMPOSSIBLE("invalid action");
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
