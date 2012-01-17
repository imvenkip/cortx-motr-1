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
#include <sys/stat.h>     /* mkdir */
#include <sys/types.h>    /* mkdir */

#include "dtm/dtm.h"      /* c2_dtx */
#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "lib/ut.h"
#include "db/db.h"
#include "balloc/balloc.h"

extern	struct c2_balloc	colibri_balloc;
const char			*db_name = "./__s";;
const int			MAX = 100;

enum balloc_io_flags {
	BALLOC_IO_DIRECT_BSHIFT   = 0,
	BALLOC_IO_BUFFERED_BSHIFT = 12
};

int test_balloc_ut_ops(int io_flag)
{
	struct c2_dbenv		db;
	struct c2_dtx		dtx;
	int			result;
	struct c2_ext		ext[MAX];
	struct c2_ext		tmp = { 0 };
	c2_bcount_t		count = 539;
	int			i = 0;
	time_t			now;
	c2_bcount_t		prev_totalsize;
	c2_bcount_t		prev_free_blocks;

	time(&now);
	srand(now);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = colibri_balloc.cb_ballroom.ab_ops->bo_init
		(&colibri_balloc.cb_ballroom, &db, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if(result == 0) {

		prev_totalsize = colibri_balloc.cb_sb.bsb_totalsize;
		prev_free_blocks = colibri_balloc.cb_sb.bsb_freeblocks;

		for (i = 0; i < MAX; i++ ) {
			do  {
				count = rand() % 1500;
			} while (count == 0);

			result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
			C2_UT_ASSERT(result == 0);

			/* pass last result as goal. comment out this to turn
			   off goal */
			//tmp.e_start = tmp.e_end;
			result = colibri_balloc.cb_ballroom.ab_ops->bo_alloc(
				    &colibri_balloc.cb_ballroom, &dtx, count,
				    &tmp);
			if(result < 0) {
				fprintf(stderr, "Error in allocation\n");
				return result;
			}

			ext[i] = tmp;

			if(c2_ext_length(&ext[i]) < count) {
				fprintf(stderr, "Allocation size mismatch for"
					" count %5d\n", (int)count);
				result = -EINVAL;
			}
#ifdef BALLOC_DEBUG
			printf("%3d:rc = %d: requested count=%5d, result"
			       " count=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			       i, result, (int)count,
			       (int)c2_ext_length(&ext[i]),
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end,
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end);
#endif
			if (result == 0)
				c2_db_tx_commit(&dtx.tx_dbtx);
			else
				c2_db_tx_abort(&dtx.tx_dbtx);
		}

		for (i = colibri_balloc.cb_sb.bsb_reserved_groups;
		     i < colibri_balloc.cb_sb.bsb_groupcount && result == 0;
		     i++) {
			struct c2_balloc_group_info *grp = c2_balloc_gn2info
				(&colibri_balloc, i);

			result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
			C2_UT_ASSERT(result == 0);
			if (grp) {
				c2_balloc_lock_group(grp);
				result = c2_balloc_load_extents(&colibri_balloc,
								grp,
								&dtx.tx_dbtx);
				if (result ==
				    0) c2_balloc_debug_dump_group_extent(
						"balloc", grp);
				c2_balloc_release_extents(grp);
				c2_balloc_unlock_group(grp);
			}
			c2_db_tx_commit(&dtx.tx_dbtx);
		}

		/* randomize the array */
		for (i = 0; i < MAX; i++ ) {
			int a, b;
			a = rand() % MAX;
			b = rand() % MAX;
			C2_SWAP(ext[a], ext[b]);
		}

		for (i = 0; i < MAX && result == 0; i++ ) {

			result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
			C2_UT_ASSERT(result == 0);
			if (ext[i].e_start != 0)
				result =
				colibri_balloc.cb_ballroom.ab_ops->bo_free(
					    &colibri_balloc.cb_ballroom, &dtx,
					    &ext[i]);
			if(result < 0) {
				fprintf(stderr,"Error during free for size %5d",
					(int)c2_ext_length(&ext[i]));
				return result;
			}
#ifdef BALLOC_DEBUG
			printf("%3d:rc = %d: freed:                          "
			       "len=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			       i, result, (int)c2_ext_length(&ext[i]),
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end,
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end);
#endif
			if (result == 0)
				c2_db_tx_commit(&dtx.tx_dbtx);
			else
				c2_db_tx_abort(&dtx.tx_dbtx);
		}

		if(colibri_balloc.cb_sb.bsb_totalsize != prev_totalsize ||
		   colibri_balloc.cb_sb.bsb_freeblocks != prev_free_blocks) {
			fprintf(stderr, "Size mismatch during block reclaim\n");
			result = -EINVAL;
		}

		for (i = colibri_balloc.cb_sb.bsb_reserved_groups;
		     i < colibri_balloc.cb_sb.bsb_groupcount && result == 0;
		     i++) {
			struct c2_balloc_group_info *grp = c2_balloc_gn2info
				(&colibri_balloc, i);

			result = c2_db_tx_init(&dtx.tx_dbtx, &db, 0);
			C2_UT_ASSERT(result == 0);
			if (grp) {
				c2_balloc_lock_group(grp);
				result = c2_balloc_load_extents(&colibri_balloc,
								grp,
								&dtx.tx_dbtx);
				if (result ==
				    0) c2_balloc_debug_dump_group_extent(
						"balloc", grp);
				if (grp->bgi_freeblocks !=
				    colibri_balloc.cb_sb.bsb_groupsize) {
					printf("corrupted grp %d: %llx != %llx\n",
					       i, (unsigned long long)
					       grp->bgi_freeblocks,
					       (unsigned long long)
					       colibri_balloc.cb_sb.bsb_groupsize);
					result = -EINVAL;
				}
				c2_balloc_release_extents(grp);
				c2_balloc_unlock_group(grp);
			}
			c2_db_tx_commit(&dtx.tx_dbtx);
		}

		colibri_balloc.cb_ballroom.ab_ops->bo_fini(
			    &colibri_balloc.cb_ballroom);
	}

	c2_dbenv_fini(&db);

#ifdef BALLOC_DEBUG
	printf("done. status = %d\n", result);
#endif

	return result;
}

void test_balloc()
{
	int result;

	result = test_balloc_ut_ops(BALLOC_IO_BUFFERED_BSHIFT);
	C2_UT_ASSERT(result == 0);

	/* The blocksize in superblock needs to be changed.
	 * So we delete the database directory due to which a new
	 * directory will be created and eventually the superblock.
	 */

	result = system("rm -fr ./__s");
	C2_UT_ASSERT(result == 0);

	result = test_balloc_ut_ops(BALLOC_IO_DIRECT_BSHIFT);
	C2_UT_ASSERT(result == 0);
}

const struct c2_test_suite balloc_ut = {
        .ts_name = "balloc-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "balloc", test_balloc},
		{ NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
