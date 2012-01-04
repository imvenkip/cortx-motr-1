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
#include <err.h>

#include "dtm/dtm.h"      /* c2_dtx */
#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc colibri_balloc;
const int MAX = 100;

int main(int argc, char **argv)
{
	const char           *db_name;
	struct c2_dbenv       db;
	struct c2_dtx         dtx;
	int                   result;
	struct c2_ext         ext[MAX];
	struct c2_ext         tmp = { 0 };
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

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init
		(&colibri_balloc.cb_ballroom, &db, 12, 4096ULL * 1024 * 1024 * 1000,
		 128 * 1024 * 1024, 2);

	for (i = 0; i < MAX && result == 0; i++ ) {
		do  {
			count = rand() % 1500;
		} while (count == 0);

		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);

		/* pass last result as goal. comment out this to turn off goal */
		//tmp.e_start = tmp.e_end;
		result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(&colibri_balloc.cb_ballroom, &dtx, count, &tmp);
		ext[i] = tmp;

		printf("%3d:rc = %d: requested count=%5d, result count=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			i, result, (int)count,
			(int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
		if (result == 0)
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
	}

	for (i = colibri_balloc.cb_sb.bsb_reserved_groups;
	     i < colibri_balloc.cb_sb.bsb_groupcount && result == 0; i++ ) {
		struct c2_balloc_group_info *grp = c2_balloc_gn2info(&colibri_balloc, i);

		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);
		if (grp) {
			c2_balloc_lock_group(grp);
			result = c2_balloc_load_extents(&colibri_balloc, grp, &dtx.tx_dbtx);
			if (result == 0)
				c2_balloc_debug_dump_group_extent(argv[0], grp);
			c2_balloc_release_extents(grp);
			c2_balloc_unlock_group(grp);
		}
		c2_db_tx_commit(&dtx.tx_dbtx);
	}

	/* randonmize the array */
	for (i = 0; i < MAX; i++ ) {
		int a, b;
		a = rand() % MAX;
		b = rand() % MAX;
		C2_SWAP(ext[a], ext[b]);
	}

	for (i = 0; i < MAX && result == 0; i++ ) {

		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);
		if (ext[i].e_start != 0)
			result = colibri_balloc.cb_ballroom.ab_ops->bo_free(&colibri_balloc.cb_ballroom, &dtx, &ext[i]);

		printf("%3d:rc = %d: freed:                          len=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			i, result, (int)c2_ext_length(&ext[i]),
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end,
			(unsigned long long)ext[i].e_start,
			(unsigned long long)ext[i].e_end);
		if (result == 0)
			c2_db_tx_commit(&dtx.tx_dbtx);
		else
			c2_db_tx_abort(&dtx.tx_dbtx);
	}

	for (i = colibri_balloc.cb_sb.bsb_reserved_groups;
	     i < colibri_balloc.cb_sb.bsb_groupcount && result == 0; i++ ) {
		struct c2_balloc_group_info *grp = c2_balloc_gn2info(&colibri_balloc, i);

		result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
		C2_ASSERT(result == 0);
		if (grp) {
			c2_balloc_lock_group(grp);
			result = c2_balloc_load_extents(&colibri_balloc, grp, &dtx.tx_dbtx);
			if (result == 0)
				c2_balloc_debug_dump_group_extent(argv[0], grp);
			if (grp->bgi_freeblocks != colibri_balloc.cb_sb.bsb_groupsize) {
				printf("corrupted grp %d: %llx != %llx\n",
					i,
					(unsigned long long)grp->bgi_freeblocks,
					(unsigned long long)colibri_balloc.cb_sb.bsb_groupsize);
				result = -EINVAL;
			}
			c2_balloc_release_extents(grp);
			c2_balloc_unlock_group(grp);
		}
		c2_db_tx_commit(&dtx.tx_dbtx);
	}

	colibri_balloc.cb_ballroom.ab_ops->bo_fini(&colibri_balloc.cb_ballroom);
	c2_dbenv_fini(&db);
	printf("done. status = %d\n", result);
	return result;
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
