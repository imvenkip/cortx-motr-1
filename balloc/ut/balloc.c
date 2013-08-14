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
#include "mero/magic.h"
#include "db/db.h"
#include "ut/ut.h"
#include "balloc/balloc.h"
#include "be/ut/helper.h"

#define BALLOC_DBNAME "./__balloc_db"

#define GROUP_SIZE (BALLOC_DEF_CONTAINER_SIZE / (BALLOC_DEF_BLOCKS_PER_GROUP * \
						 (1 << BALLOC_DEF_BLOCK_SHIFT)))

#define BALLOC_DEBUG

static const int    MAX     = 10;
static m0_bcount_t  prev_free_blocks;
m0_bcount_t	   *prev_group_info_free_blocks;

enum balloc_invariant_enum {
	INVAR_ALLOC,
	INVAR_FREE,
};

bool balloc_ut_invariant(struct m0_balloc *mero_balloc,
			 struct m0_ext alloc_ext,
			 int balloc_invariant_flag)
{
	m0_bcount_t len = m0_ext_length(&alloc_ext);
	m0_bcount_t group;

	group = alloc_ext.e_start >> mero_balloc->cb_sb.bsb_gsbits;

	if (mero_balloc->cb_sb.bsb_magic != M0_BALLOC_SB_MAGIC)
		return false;

	switch (balloc_invariant_flag) {
	    case INVAR_ALLOC:
		 prev_free_blocks		    -= len;
		 prev_group_info_free_blocks[group] -= len;
		 break;
	    case INVAR_FREE:
		 prev_free_blocks		    += len;
		 prev_group_info_free_blocks[group] += len;
		 break;
	    default:
		 return false;
	}

	return mero_balloc->cb_group_info[group].bgi_freeblocks ==
		prev_group_info_free_blocks[group] &&
		mero_balloc->cb_sb.bsb_freeblocks ==
		prev_free_blocks;
}

static int be_tx_init_open(struct m0_be_tx *tx,
			   struct m0_be_ut_backend *ut_be,
			   struct m0_be_tx_credit *cred)
{
	int rc;

	m0_be_ut_tx_init(tx, ut_be);
	m0_be_tx_prep(tx, cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE,
						M0_BTS_FAILED),
				    M0_TIME_NEVER);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_ACTIVE);

	return rc;
}

static int be_tx_close_fini(struct m0_be_tx *tx)
{
	int rc;

	m0_be_tx_close(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE),
				    M0_TIME_NEVER);
	m0_be_tx_fini(tx);

	return rc;
}

int test_balloc_ut_ops(struct m0_be_ut_backend *ut_be, struct m0_be_seg *seg)
{
	struct m0_balloc *mero_balloc;
	struct m0_dtx	  dtx;
	struct m0_be_tx_credit cred;
	struct m0_be_tx  *tx = &dtx.tx_betx;
	int		  result;
	struct m0_ext	  ext[MAX];
	struct m0_ext	  tmp   = { 0 };
	m0_bcount_t	  count = 539;
	int		  i     = 0;
	time_t		  now;

	time(&now);
	srand(now);

	result = m0_balloc_allocate(0, seg, &mero_balloc);
	M0_UT_ASSERT(result == 0);

	result = mero_balloc->cb_ballroom.ab_ops->bo_init
		(&mero_balloc->cb_ballroom, seg, BALLOC_DEF_BLOCK_SHIFT,
		 BALLOC_DEF_CONTAINER_SIZE, BALLOC_DEF_BLOCKS_PER_GROUP,
		 BALLOC_DEF_RESERVED_GROUPS);

	if (result == 0) {
		prev_free_blocks = mero_balloc->cb_sb.bsb_freeblocks;
		M0_ALLOC_ARR(prev_group_info_free_blocks, GROUP_SIZE);
		for (i = 0; i < GROUP_SIZE; ++i) {
			prev_group_info_free_blocks[i] =
				mero_balloc->cb_group_info[i].bgi_freeblocks;
		}

		for (i = 0; i < MAX; ++i) {
			count = rand() % 1500 + 1;

			cred = M0_BE_TX_CREDIT_OBJ(0, 0);
			mero_balloc->cb_ballroom.ab_ops->bo_alloc_credit(
				    &mero_balloc->cb_ballroom, 1, &cred);
			result = be_tx_init_open(tx, ut_be, &cred);
			M0_UT_ASSERT(result == 0);

			/* pass last result as goal. comment out this to turn
			   off goal */
			//tmp.e_start = tmp.e_end;
			result = mero_balloc->cb_ballroom.ab_ops->bo_alloc(
				    &mero_balloc->cb_ballroom, &dtx, count,
				    &tmp);
			if (result < 0) {
				fprintf(stderr, "Error in allocation\n");
				return result;
			}

			ext[i] = tmp;

			/* The result extent length should be less than
			 * or equal to the requested length. */
			if (m0_ext_length(&ext[i]) > count) {
				fprintf(stderr, "Allocation size mismatch: "
					"requested count = %5d, result = %5d\n",
					(int)count,
					(int)m0_ext_length(&ext[i]));
				result = -EINVAL;
			}

			M0_UT_ASSERT(balloc_ut_invariant(mero_balloc, ext[i],
							 INVAR_ALLOC));
#ifdef BALLOC_DEBUG
			printf("%3d:rc = %d: requested count=%5d, result"
			       " count=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			       i, result, (int)count,
			       (int)m0_ext_length(&ext[i]),
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end,
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end);
#endif
			result = be_tx_close_fini(tx);
			M0_UT_ASSERT(result == 0);

		}

		for (i = mero_balloc->cb_sb.bsb_reserved_groups;
		     i < mero_balloc->cb_sb.bsb_groupcount && result == 0;
		     ++i) {
			struct m0_balloc_group_info *grp = m0_balloc_gn2info
				(mero_balloc, i);

			cred = M0_BE_TX_CREDIT_OBJ(0, 0);
			m0_balloc_load_extents_credit(mero_balloc, &cred);
			result = be_tx_init_open(tx, ut_be, &cred);
			M0_UT_ASSERT(result == 0);
			if (grp) {
				m0_balloc_lock_group(grp);
				result = m0_balloc_load_extents(mero_balloc,
							     grp,
							     &dtx.tx_betx);
				if (result == 0)
					m0_balloc_debug_dump_group_extent(
						    "balloc ut", grp);
				m0_balloc_release_extents(grp);
				m0_balloc_unlock_group(grp);
			}
			result = be_tx_close_fini(tx);
			M0_UT_ASSERT(result == 0);
		}

		/* randomize the array */
		for (i = 0; i < MAX; ++i) {
			int a;
			int b;
			a = rand() % MAX;
			b = rand() % MAX;
			M0_SWAP(ext[a], ext[b]);
		}

		for (i = 0; i < MAX && result == 0; ++i) {
			cred = M0_BE_TX_CREDIT_OBJ(0, 0);
			mero_balloc->cb_ballroom.ab_ops->bo_free_credit(
				    &mero_balloc->cb_ballroom, 1, &cred);
			result = be_tx_init_open(tx, ut_be, &cred);
			M0_UT_ASSERT(result == 0);

			if (ext[i].e_start != 0)
				result =
				mero_balloc->cb_ballroom.ab_ops->bo_free(
					    &mero_balloc->cb_ballroom, &dtx,
					    &ext[i]);
			if (result < 0) {
				fprintf(stderr,"Error during free for size %5d",
					(int)m0_ext_length(&ext[i]));
				return result;
			}

			M0_UT_ASSERT(balloc_ut_invariant(mero_balloc, ext[i],
							 INVAR_FREE));
#ifdef BALLOC_DEBUG
			printf("%3d:rc = %d: freed:                          "
			       "len=%5d: [%08llx,%08llx)=[%8llu,%8llu)\n",
			       i, result, (int)m0_ext_length(&ext[i]),
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end,
			       (unsigned long long)ext[i].e_start,
			       (unsigned long long)ext[i].e_end);
#endif
			result = be_tx_close_fini(tx);
			M0_UT_ASSERT(result == 0);
		}

		if (mero_balloc->cb_sb.bsb_freeblocks != prev_free_blocks) {
			fprintf(stderr, "Size mismatch during block reclaim\n");
			result = -EINVAL;
		}

		for (i = mero_balloc->cb_sb.bsb_reserved_groups;
		     i < mero_balloc->cb_sb.bsb_groupcount && result == 0;
		     ++i) {
			struct m0_balloc_group_info *grp = m0_balloc_gn2info
				(mero_balloc, i);

			cred = M0_BE_TX_CREDIT_OBJ(0, 0);
			m0_balloc_load_extents_credit(mero_balloc, &cred);
			result = be_tx_init_open(tx, ut_be, &cred);
			M0_UT_ASSERT(result == 0);
			if (grp) {
				m0_balloc_lock_group(grp);
				result = m0_balloc_load_extents(mero_balloc,
								grp,
								&dtx.tx_betx);
				if (result == 0)
					m0_balloc_debug_dump_group_extent(
						    "balloc ut", grp);
				if (grp->bgi_freeblocks !=
				    mero_balloc->cb_sb.bsb_groupsize) {
					printf("corrupted grp %d: %llx != %llx\n",
					       i, (unsigned long long)
					       grp->bgi_freeblocks,
					       (unsigned long long)
					       mero_balloc->cb_sb.bsb_groupsize);
					result = -EINVAL;
				}
				m0_balloc_release_extents(grp);
				m0_balloc_unlock_group(grp);
			}
			result = be_tx_close_fini(tx);
			M0_UT_ASSERT(result == 0);
		}

		mero_balloc->cb_ballroom.ab_ops->bo_fini(
			    &mero_balloc->cb_ballroom);
	}

	m0_free(prev_group_info_free_blocks);

#ifdef BALLOC_DEBUG
	printf("done. status = %d\n", result);
#endif
	return result;
}

void test_balloc()
{
	struct m0_be_ut_backend	 ut_be;
	struct m0_be_ut_seg	 ut_seg;
	struct m0_be_seg	*seg;
	int			 result;

	/* Init BE */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, 1ULL << 24);
	m0_be_ut_seg_allocator_init(&ut_seg, &ut_be);
	seg = &ut_seg.bus_seg;

	result = test_balloc_ut_ops(&ut_be, seg);
	M0_UT_ASSERT(result == 0);

	m0_be_ut_seg_allocator_fini(&ut_seg, &ut_be);
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

const struct m0_test_suite balloc_ut = {
        .ts_name  = "balloc-ut",
        .ts_init  = NULL,
        .ts_fini  = NULL,
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
