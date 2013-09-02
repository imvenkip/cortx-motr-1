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
 * Original creation date: 07/27/2010
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BALLOC
#include "lib/trace.h"

#include <stdio.h>        /* sprintf */
#include <stdlib.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "dtm/dtm.h"	  /* m0_dtx */
#include "lib/misc.h"	  /* M0_SET0 */
#include "lib/errno.h"
#include "lib/arith.h"	  /* min_check, m0_is_po2 */
#include "lib/memory.h"
#include "balloc.h"
#include "mero/magic.h"

/**
   M0 Data Block Allocator.
   BALLOC is a multi-block allocator, with pre-allocation. All metadata about
   block allocation is stored in database -- Oracle Berkeley DB.

 */

/* This macro is to control the debug verbose message */
#define BALLOC_ENABLE_DUMP

static void balloc_debug_dump_extent(const char *tag, struct m0_ext *ex)
{
#ifdef BALLOC_ENABLE_DUMP

	if (ex == NULL)
		return;

	M0_LOG(M0_DEBUG, "dumping ex@%p:%s\n"
	       "|----[%10llu, %10llu), [0x%08llx, 0x%08llx)\n",
		ex, (char*) tag,
		(unsigned long long) ex->e_start,
		(unsigned long long) ex->e_end,
		(unsigned long long) ex->e_start,
		(unsigned long long) ex->e_end);
#endif
}

M0_INTERNAL void m0_balloc_debug_dump_group(const char *tag,
					    struct m0_balloc_group_info *grp)
{
#ifdef BALLOC_ENABLE_DUMP
	if (grp == NULL)
		return;

	M0_LOG(M0_DEBUG, "dumping group_desc@%p:%s\n"
	       "|-----groupno=%08llx, freeblocks=%08llx, maxchunk=0x%08llx, "
		"fragments=0x%08llx\n",
		grp, (char*) tag,
		(unsigned long long) grp->bgi_groupno,
		(unsigned long long) grp->bgi_freeblocks,
		(unsigned long long) grp->bgi_maxchunk,
		(unsigned long long) grp->bgi_fragments);
#endif
}

M0_INTERNAL void m0_balloc_debug_dump_group_extent(const char *tag,
						   struct m0_balloc_group_info
						   *grp)
{
#ifdef BALLOC_ENABLE_DUMP
	m0_bcount_t	 i;
	struct m0_ext	*ex;

	if (grp == NULL || grp->bgi_extents == NULL)
		return;

	M0_LOG(M0_DEBUG, "dumping free extents@%p:%s for %08llx",
		grp, (char*) tag, (unsigned long long) grp->bgi_groupno);
	for (i = 0; i < grp->bgi_fragments; i++) {
		ex = &grp->bgi_extents[i];
		M0_LOG(M0_DEBUG, "[0x%08llx, 0x%08llx)",
			(unsigned long long) ex->e_start,
			(unsigned long long) ex->e_end);
	}
#endif
}

M0_INTERNAL void m0_balloc_debug_dump_sb(const char *tag,
					 struct m0_balloc_super_block *sb)
{
#ifdef BALLOC_ENABLE_DUMP
	if (sb == NULL)
		return;

	M0_LOG(M0_DEBUG, "dumping sb@%p:%s\n"
		"|-----magic=%llx, state=%llu, version=%llu\n"
		"|-----total=%llu, free=%llu, bs=%llu(bits=%lu)\n",
		sb, (char*) tag,
		(unsigned long long) sb->bsb_magic,
		(unsigned long long) sb->bsb_state,
		(unsigned long long) sb->bsb_version,
		(unsigned long long) sb->bsb_totalsize,
		(unsigned long long) sb->bsb_freeblocks,
		(unsigned long long) sb->bsb_blocksize,
		(unsigned long	   ) sb->bsb_bsbits);

	M0_LOG(M0_DEBUG, "|-----gs=%llu(bits=%lu), gc=%llu, rsvd=%llu,"
		" prealloc=%llu\n"
		"|-----time format=%llu,\n"
		"|-----write=%llu,\n"
		"|-----mnt  =%llu,\n"
		"|-----last =%llu\n",
		(unsigned long long) sb->bsb_groupsize,
		(unsigned long	   ) sb->bsb_gsbits,
		(unsigned long long) sb->bsb_groupcount,
		(unsigned long long) sb->bsb_reserved_groups,
		(unsigned long long) sb->bsb_prealloc_count,
		(unsigned long long) sb->bsb_format_time,
		(unsigned long long) sb->bsb_write_time,
		(unsigned long long) sb->bsb_mnt_time,
		(unsigned long long) sb->bsb_last_check_time);

	M0_LOG(M0_DEBUG, "|-----mount=%llu, max_mnt=%llu, stripe_size=%llu\n",
		(unsigned long long) sb->bsb_mnt_count,
		(unsigned long long) sb->bsb_max_mnt_count,
		(unsigned long long) sb->bsb_stripe_size
		);
#endif
}

static inline m0_bindex_t
balloc_bn2gn(m0_bindex_t blockno, struct m0_balloc *cb)
{
	return blockno >> cb->cb_sb.bsb_gsbits;
}

M0_INTERNAL struct m0_balloc_group_info *m0_balloc_gn2info(struct m0_balloc *cb,
							   m0_bindex_t groupno)
{
	if (cb->cb_group_info)
		return &cb->cb_group_info[groupno];
	else
		return NULL;
}

M0_INTERNAL int m0_balloc_release_extents(struct m0_balloc_group_info *grp)
{
	M0_ASSERT(m0_mutex_is_locked(&grp->bgi_mutex));
	if (grp->bgi_extents) {
		m0_free(grp->bgi_extents);
		grp->bgi_extents = NULL;
	}
	return 0;
}

M0_INTERNAL void m0_balloc_lock_group(struct m0_balloc_group_info *grp)
{
	m0_mutex_lock(&grp->bgi_mutex);
}

M0_INTERNAL int m0_balloc_trylock_group(struct m0_balloc_group_info *grp)
{
	return m0_mutex_trylock(&grp->bgi_mutex);
}

M0_INTERNAL void m0_balloc_unlock_group(struct m0_balloc_group_info *grp)
{
	m0_mutex_unlock(&grp->bgi_mutex);
}


#define MAX_ALLOCATION_CHUNK 2048ULL

/**
   finalization of the balloc environment.
 */
static int balloc_fini_internal(struct m0_balloc *mero,
				struct m0_db_tx  *tx)
{
	struct m0_balloc_group_info	*gi;
	int				 i;
	M0_ENTRY();

	if (mero->cb_group_info) {
		for (i = 0 ; i < mero->cb_sb.bsb_groupcount; i++) {
			gi = &mero->cb_group_info[i];
			m0_balloc_lock_group(gi);
			m0_balloc_release_extents(gi);
			m0_balloc_unlock_group(gi);
		}

		m0_free(mero->cb_group_info);
		mero->cb_group_info = NULL;
	}

	m0_table_fini(&mero->cb_db_group_extents);
	m0_table_fini(&mero->cb_db_group_desc);
	m0_table_fini(&mero->cb_db_sb);
	M0_LEAVE();
	return 0;
}

/**
   Comparison function for block number, supplied to database.
 */
static int balloc_blockno_compare(struct m0_table *t,
				  const void *k0, const void *k1)
{
	const m0_bindex_t *bn0;
	const m0_bindex_t *bn1;

	bn0 = (m0_bindex_t*)k0;
	bn1 = (m0_bindex_t*)k1;
	return M0_3WAY(*bn0, *bn1);
}

static const struct m0_table_ops m0_super_block_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (uint64_t) },
		[TO_REC] = { .max_size = sizeof (struct m0_balloc_super_block) }
	},
	.key_cmp = NULL
};

static const struct m0_table_ops m0_group_extent_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (m0_bindex_t) },
		[TO_REC] = { .max_size = sizeof (m0_bindex_t) }
	},
	.key_cmp = balloc_blockno_compare,
};

static const struct m0_table_ops m0_group_desc_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (m0_bindex_t) },
		[TO_REC] = { .max_size = sizeof (struct m0_balloc_group_desc) }
	},
	.key_cmp = NULL
};

/**
   Format the container: create database, fill them with initial information.

   This routine will create a "super_block" database to store global parameters
   for this container. It will also create "group free extent" and "group_desc"
   for every group. If some groups are reserved for special purpose, then they
   will be marked as "allocated" at the format time, and those groups will not
   be used by normal allocation routines.

   @param req pointer to this format request. All configuration will be passed
	  by this parameter.
   @return 0 means success. Otherwize, error number will be returned.
 */
static int balloc_format(struct m0_balloc *mero,
			 struct m0_balloc_format_req *req)
{
	struct timeval			 now;
	struct m0_db_pair		 pair;
	m0_bcount_t			 number_of_groups;
	m0_bcount_t			 i;
	int				 sz;
	int				 rc;
	struct m0_db_tx			 format_tx;
	int				 tx_started = 0;
	struct m0_balloc_group_info	*grp;
	struct m0_ext			 ext;
	struct m0_balloc_group_desc	 gd;
	struct m0_balloc_super_block	*sb	    = &mero->cb_sb;
	M0_ENTRY();

	M0_PRE(m0_is_po2(req->bfr_blocksize));
	M0_PRE(m0_is_po2(req->bfr_groupsize));

	number_of_groups = req->bfr_totalsize / req->bfr_blocksize /
	                            req->bfr_groupsize;

	if (number_of_groups == 0)
		number_of_groups = 1;

	M0_LOG(M0_DEBUG, "total=%llu, bs=%llu, groupsize=%llu, groups=%llu"
			 " resvd=%llu\n",
		(unsigned long long)req->bfr_totalsize,
		(unsigned long long)req->bfr_blocksize,
		(unsigned long long)req->bfr_groupsize,
		(unsigned long long)number_of_groups,
		(unsigned long long)req->bfr_reserved_groups);

	if (number_of_groups <= req->bfr_reserved_groups) {
		M0_LOG(M0_ERROR, "container is too small\n");
		return -EINVAL;
	}

	gettimeofday(&now, NULL);
	/* TODO verification of these parameters */
	sb->bsb_magic		= M0_BALLOC_SB_MAGIC;
	sb->bsb_state		= 0;
	sb->bsb_version		= M0_BALLOC_SB_VERSION;
	sb->bsb_totalsize	= req->bfr_totalsize;
	sb->bsb_blocksize	= req->bfr_blocksize;/* should be power of 2*/
	sb->bsb_groupsize	= req->bfr_groupsize;/* should be power of 2*/
	sb->bsb_bsbits		= ffs(req->bfr_blocksize) - 1;
	sb->bsb_gsbits		= ffs(req->bfr_groupsize) - 1;
	sb->bsb_groupcount	= number_of_groups;
	sb->bsb_reserved_groups = req->bfr_reserved_groups;
	sb->bsb_freeblocks	= (number_of_groups - sb->bsb_reserved_groups)
				  << sb->bsb_gsbits;
	sb->bsb_prealloc_count	= 16;
	sb->bsb_format_time	= ((uint64_t)now.tv_sec) << 32 | now.tv_usec;
	sb->bsb_write_time	= sb->bsb_format_time;
	sb->bsb_mnt_time	= sb->bsb_format_time;
	sb->bsb_last_check_time	= sb->bsb_format_time;
	sb->bsb_mnt_count	= 0;
	sb->bsb_max_mnt_count	= 1024;
	sb->bsb_stripe_size	= 0;

	m0_db_pair_setup(&pair, &mero->cb_db_sb,
			 &sb->bsb_magic, sizeof sb->bsb_magic,
			 sb, sizeof *sb);
	rc = m0_db_tx_init(&format_tx, mero->cb_dbenv, 0);
	if (rc == 0) {
		rc = m0_table_insert(&format_tx, &pair);
		if (rc == 0)
			rc = m0_db_tx_commit(&format_tx);
		else
			m0_db_tx_abort(&format_tx);
	}
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "insert super_block failed: rc=%d\n", rc);
		return rc;
	}

	sz = number_of_groups * sizeof (struct m0_balloc_group_info);
	mero->cb_group_info = m0_alloc(sz);
	if (mero->cb_group_info == NULL) {
		M0_LOG(M0_ERROR, "create allocate memory for group info\n");
		rc = -ENOMEM;
		return rc;
	}

	for (i = 0; i < number_of_groups; i++) {
		grp = &mero->cb_group_info[i];

		M0_LOG(M0_DEBUG, "creating group_extents for group %llu\n",
		       (unsigned long long)i);
		ext.e_start = i << sb->bsb_gsbits;
		if (i < req->bfr_reserved_groups)
			ext.e_end = ext.e_start;
		else
			ext.e_end = ext.e_start + sb->bsb_groupsize;

		m0_db_pair_setup(&pair, &mero->cb_db_group_extents,
				 &ext.e_start, sizeof ext.e_start,
				 &ext.e_end, sizeof ext.e_end);
		if (!tx_started) {
			rc = m0_db_tx_init(&format_tx, mero->cb_dbenv, 0);
			if (rc != 0)
				break;
			tx_started = 1;
		}
		rc = m0_table_insert(&format_tx, &pair);

		m0_db_pair_release(&pair);
		m0_db_pair_fini(&pair);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "insert extent failed: group=%llu,"
					 " rc=%d\n", (unsigned long long)i, rc);
			break;
		}

		M0_LOG(M0_DEBUG, "creating group_desc for group %llu\n",
		       (unsigned long long)i);
		gd.bgd_groupno = i;
		if (i < req->bfr_reserved_groups) {
			gd.bgd_freeblocks = 0;
			gd.bgd_fragments  = 0;
			gd.bgd_maxchunk	  = 0;
		} else {
			gd.bgd_freeblocks = req->bfr_groupsize;
			gd.bgd_fragments  = 1;
			gd.bgd_maxchunk	  = req->bfr_groupsize;
		}
		m0_db_pair_setup(&pair, &mero->cb_db_group_desc,
				 &gd.bgd_groupno,
				 sizeof gd.bgd_groupno,
				 &gd,
				 sizeof gd);
		grp->bgi_groupno = i;
		grp->bgi_freeblocks = gd.bgd_freeblocks;
		grp->bgi_fragments  = gd.bgd_fragments;
		grp->bgi_maxchunk   = gd.bgd_maxchunk;
		rc = m0_table_insert(&format_tx, &pair);
		m0_db_pair_release(&pair);
		m0_db_pair_fini(&pair);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "insert gd failed: group=%llu, rc=%d\n",
				(unsigned long long)i, rc);
			break;
		}
		if ((i & 0x3f) == 0) {
			if (rc == 0)
				rc = m0_db_tx_commit(&format_tx);
			else
				m0_db_tx_abort(&format_tx);
			tx_started = 0;
		}
	}
	if (tx_started) {
		if (rc == 0)
			rc = m0_db_tx_commit(&format_tx);
		else
			m0_db_tx_abort(&format_tx);
	}
	M0_LEAVE();
	return rc;
}

static int balloc_read_sb(struct m0_balloc *cb, struct m0_db_tx *tx)
{
	struct m0_balloc_super_block	*sb = &cb->cb_sb;
	struct m0_db_pair		 pair;
	int				 rc;

	sb->bsb_magic = M0_BALLOC_SB_MAGIC;
	m0_db_pair_setup(&pair, &cb->cb_db_sb,
			 &sb->bsb_magic, sizeof sb->bsb_magic,
			 sb, sizeof *sb);

	rc = m0_table_lookup(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	return rc;
}

static int balloc_sync_sb(struct m0_balloc *cb, struct m0_db_tx *tx)
{
	struct m0_balloc_super_block	*sb = &cb->cb_sb;
	struct m0_db_pair		 pair;
	struct timeval			 now;
	int				 rc;
	M0_ENTRY();

	if (!(cb->cb_sb.bsb_state & M0_BALLOC_SB_DIRTY)) {
		M0_LEAVE();
		return 0;
	}

	gettimeofday(&now, NULL);
	sb->bsb_write_time = ((uint64_t)now.tv_sec) << 32 | now.tv_usec;

	sb->bsb_magic = M0_BALLOC_SB_MAGIC;
	m0_db_pair_setup(&pair, &cb->cb_db_sb,
			 &sb->bsb_magic, sizeof sb->bsb_magic,
			 sb, sizeof *sb);

	rc = m0_table_update(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	cb->cb_sb.bsb_state &= ~M0_BALLOC_SB_DIRTY;
	M0_LEAVE();
	return rc;
}

static int balloc_sync_group_info(struct m0_balloc *cb,
				  struct m0_db_tx *tx,
				  struct m0_balloc_group_info *gi)
{
	struct m0_balloc_group_desc	gd;
	struct m0_db_pair		pair;
	int				rc;
	M0_ENTRY();

	if (! (gi->bgi_state & M0_BALLOC_GROUP_INFO_DIRTY)) {
		M0_LEAVE();
		return 0;
	}

	gd.bgd_groupno	  = gi->bgi_groupno;
	gd.bgd_freeblocks = gi->bgi_freeblocks;
	gd.bgd_fragments  = gi->bgi_fragments;
	gd.bgd_maxchunk	  = gi->bgi_maxchunk;
	m0_db_pair_setup(&pair, &cb->cb_db_group_desc,
			 &gd.bgd_groupno, sizeof gd.bgd_groupno,
			 &gd, sizeof gd);

	rc = m0_table_update(tx, &pair);
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);
	gi->bgi_state &= ~M0_BALLOC_GROUP_INFO_DIRTY;

	M0_LEAVE();
	return rc;
}


static int balloc_load_group_info(struct m0_balloc *cb,
				  struct m0_db_tx *tx,
				  struct m0_balloc_group_info *gi)
{
	struct m0_balloc_group_desc	gd = { 0 };
	struct m0_db_pair		pair;
	int				rc;

	// M0_LOG(M0_DEBUG, "loading group info for = %llu\n",
	// (unsigned long long)gi->bgi_groupno);

	m0_db_pair_setup(&pair, &cb->cb_db_group_desc,
			 &gd.bgd_groupno, sizeof gd.bgd_groupno,
			 &gd, sizeof gd);
	gd.bgd_groupno = gi->bgi_groupno;
	rc = m0_table_lookup(tx, &pair);

	if (rc == 0) {
		gi->bgi_groupno	   = gd.bgd_groupno;
		gi->bgi_freeblocks = gd.bgd_freeblocks;
		gi->bgi_fragments  = gd.bgd_fragments;
		gi->bgi_maxchunk   = gd.bgd_maxchunk;
		gi->bgi_state	   = M0_BALLOC_GROUP_INFO_INIT;
		gi->bgi_extents	   = NULL;
		m0_list_init(&gi->bgi_prealloc_list);
		m0_mutex_init(&gi->bgi_mutex);
	}
	m0_db_pair_release(&pair);
	m0_db_pair_fini(&pair);

	return rc;
}

/*
 * start transaction for init() and format() respectively.
 * One transaction maybe fail to include all update. Multiple transaction
 * is used here.
 * The same reason for format().
 */
static int balloc_init_internal(struct m0_balloc *mero,
				struct m0_dbenv  *dbenv,
				uint32_t bshift,
				m0_bcount_t container_size,
				m0_bcount_t blocks_per_group,
				m0_bcount_t res_groups)
{
	struct m0_balloc_group_info	*gi;
	struct m0_db_tx			 init_tx;
	struct timeval			 now;
	char                             table_name[MAXPATHLEN];
	uint64_t                         cid = mero->cb_container_id;
	int				 rc;
	m0_bcount_t			 i;
	int				 tx_started = 0;
	M0_ENTRY();

	mero->cb_dbenv = dbenv;
	mero->cb_group_info = NULL;
	m0_mutex_init(&mero->cb_sb_mutex);
	M0_SET0(&mero->cb_sb);
	M0_SET0(&mero->cb_db_group_extents);
	M0_SET0(&mero->cb_db_group_desc);
	M0_SET_ARR0(table_name);

	sprintf(table_name, "%s_%lu", "super_block", cid);
	rc = m0_table_init(&mero->cb_db_sb, dbenv,
			   table_name, 0,
			   &m0_super_block_ops);
	if (rc == 0) {
		M0_SET_ARR0(table_name);
		sprintf(table_name, "%s_%lu", "group_desc", cid);
		rc = m0_table_init(&mero->cb_db_group_desc, dbenv, table_name, 0,
				   &m0_group_desc_ops);
	}
	if (rc == 0) {
		M0_SET_ARR0(table_name);
		sprintf(table_name, "%s_%lu", "group_extents", cid);
		rc = m0_table_init(&mero->cb_db_group_extents, dbenv, table_name,
				   0, &m0_group_extent_ops);
	}

	if (rc != 0)
		goto out;

	rc = m0_db_tx_init(&init_tx, dbenv, 0);
	if (rc == 0) {
		rc = balloc_read_sb(mero, &init_tx);
		m0_db_tx_commit(&init_tx);
	}

	if (rc == -ENOENT) {
		struct m0_balloc_format_req req = { 0 };

		/* let's format this container */
		req.bfr_totalsize = container_size;
		req.bfr_blocksize = 1 << bshift;
		req.bfr_groupsize = blocks_per_group;
		req.bfr_reserved_groups = res_groups;

		rc = balloc_format(mero, &req);
		if (rc != 0)
			balloc_fini_internal(mero, NULL);
		M0_LEAVE();
		return rc;
	} else if (rc != 0)
		goto out;

	/* update the db */
	++ mero->cb_sb.bsb_mnt_count;
	gettimeofday(&now, NULL);
	mero->cb_sb.bsb_mnt_time = ((uint64_t)now.tv_sec) << 32 |
		now.tv_usec;
	mero->cb_sb.bsb_state |= M0_BALLOC_SB_DIRTY;

	rc = m0_db_tx_init(&init_tx, dbenv, 0);
	if (rc == 0) {
		rc = balloc_sync_sb(mero, &init_tx);
		if (rc == 0)
			rc = m0_db_tx_commit(&init_tx);
		else
			m0_db_tx_abort(&init_tx);

	}
	if (rc != 0)
		goto out;

	M0_LOG(M0_INFO, "Group Count = %lu\n", mero->cb_sb.bsb_groupcount);

	if (mero->cb_sb.bsb_blocksize != 1 << bshift) {
		rc = -EINVAL;
		goto out;
	}

	i = mero->cb_sb.bsb_groupcount *
		sizeof (struct m0_balloc_group_info);
	mero->cb_group_info = m0_alloc(i);
	if (mero->cb_group_info == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	M0_LOG(M0_INFO, "Loading group info. Please wait...\n");
	for (i = 0; i < mero->cb_sb.bsb_groupcount; i++ ) {
		gi = &mero->cb_group_info[i];
		gi->bgi_groupno = i;

		if (!tx_started) {
			rc = m0_db_tx_init(&init_tx, dbenv, 0);
			if (rc != 0)
				break;
			tx_started = 1;
		}
		rc = balloc_load_group_info(mero, &init_tx, gi);

		if ((i & 0x3f) == 0) {
			if (rc == 0)
				rc = m0_db_tx_commit(&init_tx);
			else
				m0_db_tx_abort(&init_tx);
			tx_started = 0;
		}
		if (rc != 0)
			break;

		/* TODO verify the super_block info based on the group info */
	}
	if (tx_started) {
		if (rc == 0)
			rc = m0_db_tx_commit(&init_tx);
		else
			m0_db_tx_abort(&init_tx);
	}
out:
	if (rc != 0)
		balloc_fini_internal(mero, NULL);
	M0_LEAVE();
	return rc;
}

enum m0_balloc_allocation_status {
	M0_BALLOC_AC_FOUND    = 1,
	M0_BALLOC_AC_CONTINUE = 2,
	M0_BALLOC_AC_BREAK    = 3,
};

struct m0_balloc_allocation_context {
	struct m0_balloc	      *bac_ctxt;
	struct m0_db_tx		      *bac_tx;
	struct m0_balloc_allocate_req *bac_req;
	struct m0_ext		       bac_orig; /*< original */
	struct m0_ext		       bac_goal; /*< after normalization */
	struct m0_ext		       bac_best; /*< best available */
	struct m0_ext		       bac_final;/*< final results */

	uint64_t		       bac_flags;
	uint64_t		       bac_criteria;
	uint32_t		       bac_order2;  /* order of 2 */
	uint32_t		       bac_scanned; /* groups scanned */
	uint32_t		       bac_found;   /* count of found */
	uint32_t		       bac_status;  /* allocation status */
};

static int balloc_init_ac(struct m0_balloc_allocation_context *bac,
			  struct m0_balloc *mero,
			  struct m0_db_tx * tx,
			  struct m0_balloc_allocate_req *req)
{
	M0_ENTRY();

	M0_SET0(&req->bar_result);

	bac->bac_ctxt	  = mero;
	bac->bac_tx	  = tx;
	bac->bac_req	  = req;
	bac->bac_order2	  = 0;
	bac->bac_scanned  = 0;
	bac->bac_found	  = 0;
	bac->bac_flags	  = req->bar_flags;
	bac->bac_status	  = M0_BALLOC_AC_CONTINUE;
	bac->bac_criteria = 0;

	if (req->bar_goal == 0)
		req->bar_goal = mero->cb_last.e_end;

	bac->bac_orig.e_start	= req->bar_goal;
	bac->bac_orig.e_end	= req->bar_goal + req->bar_len;
	bac->bac_goal = bac->bac_orig;

	M0_SET0(&bac->bac_best);
	M0_SET0(&bac->bac_final);

	M0_LEAVE();
	return 0;
}


static int balloc_use_prealloc(struct m0_balloc_allocation_context *bac)
{
	return 0;
}

static int balloc_claim_free_blocks(struct m0_balloc *mero,
				    m0_bcount_t blocks)
{
	int rc;
	M0_ENTRY();

	M0_LOG(M0_DEBUG, "bsb_freeblocks = %llu, blocks=%llu\n",
		(unsigned long long)mero->cb_sb.bsb_freeblocks,
		(unsigned long long)blocks);
	rc = (mero->cb_sb.bsb_freeblocks >= blocks);

	M0_LEAVE();
	return rc;
}

/*
 * here we normalize request for locality group
 */
static void
balloc_normalize_group_request(struct m0_balloc_allocation_context *bac)
{
}

/*
 * Normalization means making request better in terms of
 * size and alignment
 */
static void
balloc_normalize_request(struct m0_balloc_allocation_context *bac)
{
	m0_bcount_t size = m0_ext_length(&bac->bac_orig);
	M0_ENTRY();

	/* do normalize only for data requests. metadata requests
	   do not need preallocation */
	if (!(bac->bac_flags & M0_BALLOC_HINT_DATA))
		return;

	/* sometime caller may want exact blocks */
	if (bac->bac_flags & M0_BALLOC_HINT_GOAL_ONLY)
		return;

	/* caller may indicate that preallocation isn't
	 * required (it's a tail, for example) */
	if (bac->bac_flags & M0_BALLOC_HINT_NOPREALLOC)
		return;

	if (bac->bac_flags & M0_BALLOC_HINT_GROUP_ALLOC) {
		balloc_normalize_group_request(bac);
		return;
	}

        /* @todo : removing normalisation for time. */
	if (size <= 4 ) {
		size = 4;
	} else if (size <= 8) {
		size = 8;
	} else if (size <= 16) {
		size = 16;
	} else if (size <= 32) {
		size = 32;
	} else if (size <= 64) {
		size = 64;
	} else if (size <= 128) {
		size = 128;
	} else if (size <= 256) {
		size = 256;
	} else if (size <= 512) {
		size = 512;
	} else if (size <= 1024) {
		size = 1024;
	} else if (size <= 2048) {
		size = 2048;
	} else {
		M0_LOG(M0_WARN, "length %llu is too large, truncate to %llu\n",
			(unsigned long long) size, MAX_ALLOCATION_CHUNK);
		size = MAX_ALLOCATION_CHUNK;
	}

	if (size > bac->bac_ctxt->cb_sb.bsb_groupsize)
		size = bac->bac_ctxt->cb_sb.bsb_groupsize;

	/*
          Now prepare new goal. Extra space we get will be consumed and
          reserved by preallocation.
        */
	bac->bac_goal.e_end = bac->bac_goal.e_start + size;

	M0_LOG(M0_DEBUG, "goal: start=%llu=(0x%08llx), size=%llu(was %llu)\n",
		(unsigned long long) bac->bac_goal.e_start,
		(unsigned long long) bac->bac_goal.e_start,
		(unsigned long long) m0_ext_length(&bac->bac_goal),
		(unsigned long long) m0_ext_length(&bac->bac_orig));
	M0_LEAVE();
}

/* called under group lock */
M0_INTERNAL int m0_balloc_load_extents(struct m0_balloc *cb,
				       struct m0_balloc_group_info *grp,
				       struct m0_db_tx *tx)
{
	struct m0_table		*db_ext	  = &cb->cb_db_group_extents;
	struct m0_db_cursor	 cursor;
	struct m0_db_pair	 pair;
	struct m0_ext		*ex;
	struct m0_ext		 start;
	int			 result	  = 0;
	int			 size;
	m0_bcount_t		 maxchunk = 0;
	m0_bcount_t		 count	  = 0;

	M0_ASSERT(m0_mutex_is_locked(&grp->bgi_mutex));
	if (grp->bgi_extents != NULL) {
		/* already loaded */
		return 0;
	}

	size = (grp->bgi_fragments + 1) * sizeof (struct m0_ext);
	grp->bgi_extents = m0_alloc(size);
	if (grp->bgi_extents == NULL)
		return -ENOMEM;

	if (grp->bgi_fragments == 0)
		return 0;

	result = m0_db_cursor_init(&cursor, db_ext, tx, 0);
	if (result != 0) {
		m0_balloc_release_extents(grp);
		return result;
	}

	start.e_start = grp->bgi_groupno << cb->cb_sb.bsb_gsbits;
	m0_db_pair_setup(&pair, db_ext,
			 &start.e_start, sizeof start.e_start,
			 &start.e_end, sizeof start.e_end);
	result = m0_db_cursor_get(&cursor, &pair);
	if (result != 0) {
		m0_db_cursor_fini(&cursor);
		m0_balloc_release_extents(grp);
		return result;
	}
	ex = &grp->bgi_extents[0];
	*ex = start;
	count ++;
	if (m0_ext_length(ex) > maxchunk)
		maxchunk = m0_ext_length(ex);

	while (count < grp->bgi_fragments) {
		ex = &grp->bgi_extents[count];
		m0_db_pair_setup(&pair, db_ext,
				 &ex->e_start, sizeof ex->e_start,
				 &ex->e_end, sizeof ex->e_end);
		result = m0_db_cursor_next(&cursor, &pair);
		if (result != 0)
			break;

		if (m0_ext_length(ex) > maxchunk)
			maxchunk = m0_ext_length(ex);
		//balloc_debug_dump_extent("loading...", ex);

		count++;
	}
	m0_db_cursor_fini(&cursor);

	if (result == -ENOENT && count != grp->bgi_fragments)
		M0_LOG(M0_INFO, "fragments mismatch: count=%llu,"
			" fragments=%lld\n", (unsigned long long)count,
			(unsigned long long)grp->bgi_fragments);
	if (result != 0)
		m0_balloc_release_extents(grp);

	if (grp->bgi_maxchunk != maxchunk) {
		grp->bgi_state |= M0_BALLOC_GROUP_INFO_DIRTY;
		grp->bgi_maxchunk = maxchunk;
		balloc_sync_group_info(cb, tx, grp);
	}

	return result;
}

/* called under group lock */
static int balloc_find_extent_exact(struct m0_balloc_allocation_context *bac,
				    struct m0_balloc_group_info *grp,
				    struct m0_ext *goal,
				    struct m0_ext *ex)
{
	m0_bcount_t	 i;
	int	 	 found = 0;
	struct m0_ext	*fragment;

	M0_ASSERT(m0_mutex_is_locked(&grp->bgi_mutex));

	for (i = 0; i < grp->bgi_fragments; i++) {
		fragment = &grp->bgi_extents[i];

		if (m0_ext_is_partof(fragment, goal)) {
			found = 1;
			*ex = *fragment;
			balloc_debug_dump_extent(__func__, ex);
			break;
		}
		if (fragment->e_start > goal->e_start)
			break;
	}

	return found;
}

/* called under group lock */
static int balloc_find_extent_buddy(struct m0_balloc_allocation_context *bac,
				    struct m0_balloc_group_info *grp,
				    m0_bcount_t len,
				    struct m0_ext *ex)
{
	struct m0_balloc_super_block	*sb    = &bac->bac_ctxt->cb_sb;
	m0_bcount_t			 i;
	m0_bcount_t			 found = 0;
	m0_bindex_t			 start;
	struct m0_ext			*fragment;
	struct m0_ext			min = {
						.e_start = 0,
						.e_end = 0xffffffff };

	M0_ASSERT(m0_mutex_is_locked(&grp->bgi_mutex));

	start = grp->bgi_groupno << sb->bsb_gsbits;

	for (i = 0; i < grp->bgi_fragments; i++) {
		fragment = &grp->bgi_extents[i];
repeat:
		/*
		{
			char msg[128];
			sprintf(msg, "buddy[s=%llu:0x%08llx, l=%u:0x%08x]",
			(unsigned long long)start,
			(unsigned long long)start,
			(int)len, (int)len);
			(void)msg;
			balloc_debug_dump_extent(msg, fragment);
			}
		*/
		if ((fragment->e_start == start) &&
		       (m0_ext_length(fragment) >= len)) {
			found = 1;
			if (m0_ext_length(fragment) < m0_ext_length(&min))
				min = *fragment;
		}
		if (fragment->e_start > start) {
			do {
				start += len;
			} while (fragment->e_start > start);
			if (start >= ((grp->bgi_groupno + 1) << sb->bsb_gsbits))
				break;
			/* we changed the 'start'. let's restart seaching. */
			goto repeat;
		}
	}

	if (found)
		*ex = min;

	return found;
}


static int balloc_use_best_found(struct m0_balloc_allocation_context *bac)
{
	bac->bac_final.e_start = bac->bac_best.e_start;
	bac->bac_final.e_end   = bac->bac_final.e_start +
				 min_check(m0_ext_length(&bac->bac_best),
					   m0_ext_length(&bac->bac_goal));
	bac->bac_status = M0_BALLOC_AC_FOUND;

	return 0;
}

static int balloc_new_preallocation(struct m0_balloc_allocation_context *bac)
{
	/* XXX No Preallocation now. So, trim the result to the original length. */

	bac->bac_final.e_end = bac->bac_final.e_start +
					min_check(m0_ext_length(&bac->bac_orig),
					          m0_ext_length(&bac->bac_final));
	return 0;
}

enum m0_balloc_update_operation {
	M0_BALLOC_ALLOC = 1,
	M0_BALLOC_FREE	= 2,
};


/* the group is under lock now */
static int balloc_update_db(struct m0_balloc *mero,
			    struct m0_db_tx *tx,
			    struct m0_balloc_group_info *grp,
			    struct m0_ext *tgt,
			    enum m0_balloc_update_operation op)
{
	size_t			 keysize = sizeof (tgt->e_start);
	size_t			 recsize = sizeof (tgt->e_end);
	struct m0_table		*db	 = &mero->cb_db_group_extents;
	m0_bcount_t		 i;
	struct m0_db_pair	 pair;
	int			 rc	 = 0;
	M0_ENTRY();

	M0_ASSERT(m0_mutex_is_locked(&grp->bgi_mutex));
	balloc_debug_dump_extent("target=", tgt);
	if (op == M0_BALLOC_ALLOC) {
		struct m0_ext *cur = NULL;

		for (i = 0; i < grp->bgi_fragments; i++) {
			cur = &grp->bgi_extents[i];

			if (m0_ext_is_partof(cur, tgt))
				break;
		}

		M0_ASSERT(i < grp->bgi_fragments);

		balloc_debug_dump_extent("current=", cur);

		if (cur->e_start == tgt->e_start) {
			m0_db_pair_setup(&pair, db,
					 &cur->e_start, keysize,
					 &cur->e_end, recsize);

			/* at the head of a free extent */
			rc = m0_table_delete(tx, &pair);
			m0_db_pair_fini(&pair);
			if (rc) {
				M0_LEAVE();
				return rc;
			}

			if (tgt->e_end < cur->e_end) {
				/* A smaller extent still exists */
				cur->e_start = tgt->e_end;
				m0_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = m0_table_insert(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
			} else {
				grp->bgi_fragments--;
			}
		} else {
			struct m0_ext next = *cur;

			/* in the middle of a free extent. Truncate it */
			cur->e_end = tgt->e_start;

			m0_db_pair_setup(&pair, db,
					 &cur->e_start, keysize,
					 &cur->e_end, recsize);
			rc = m0_table_update(tx, &pair);
			m0_db_pair_fini(&pair);
			if (rc) {
				M0_LEAVE();
				return rc;
			}

			if (next.e_end > tgt->e_end) {
				/* there is still a tail */
				next.e_start = tgt->e_end;
				m0_db_pair_setup(&pair, db,
						 &next.e_start, keysize,
						 &next.e_end, recsize);
				rc = m0_table_update(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				grp->bgi_fragments++;
			}
		}

		mero->cb_sb.bsb_freeblocks -= m0_ext_length(tgt);
		/* TODO update the maxchunk please */
		grp->bgi_freeblocks -= m0_ext_length(tgt);

		grp->bgi_state |= M0_BALLOC_GROUP_INFO_DIRTY;
		mero->cb_sb.bsb_state |= M0_BALLOC_SB_DIRTY;

		rc = balloc_sync_sb(mero, tx);
		if (rc == 0)
			rc = balloc_sync_group_info(mero, tx, grp);

		M0_LEAVE();
	} else if (op == M0_BALLOC_FREE) {
		struct m0_ext *cur = NULL;
		struct m0_ext *pre = NULL;
		int found = 0;

		for (i = 0; i < grp->bgi_fragments; i++) {
			cur = &grp->bgi_extents[i];

			if (tgt->e_start <= cur->e_start) {
				found = 1;
				break;
			}
			pre = cur;
		}
		balloc_debug_dump_extent("prev=", pre);
		balloc_debug_dump_extent("current=", cur);

		if (found && cur && tgt->e_end > cur->e_start) {
			M0_LOG(M0_ERROR, "!!!!!!!!!!!!!double free: "
			                 "tgt_end=%llu cur_start=%llu\n",
			       (unsigned long long)tgt->e_end,
			       (unsigned long long)cur->e_start);
			m0_balloc_debug_dump_group_extent(
				    "double free with cur", grp);
			return -EINVAL;
		}
		if (pre && pre->e_end > tgt->e_start) {
			M0_LOG(M0_ERROR, "!!!!!!!!!!!!!double free: "
			                 "pre_end=%llu tgt_start=%llu\n",
			       (unsigned long long)pre->e_end,
			       (unsigned long long)tgt->e_start);
			m0_balloc_debug_dump_group_extent(
				    "double free with pre", grp);
			return -EINVAL;
		}

		if (!found) {
			if (i == 0) {
				/* no fragments at all */
				m0_db_pair_setup(&pair, db,
						 &tgt->e_start, keysize,
						 &tgt->e_end, recsize);
				rc = m0_table_insert(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				grp->bgi_fragments++;
			} else {
				/* at the tail */
				if (pre->e_end < tgt->e_start) {
					/* to be the last one, standalone*/
					m0_db_pair_setup(&pair, db,
							 &tgt->e_start, keysize,
							 &tgt->e_end, recsize);
					rc = m0_table_insert(tx, &pair);
					m0_db_pair_fini(&pair);
					if (rc) {
						M0_LEAVE();
						return rc;
					}
					grp->bgi_fragments++;
				} else {
					pre->e_end = tgt->e_end;

					m0_db_pair_setup(&pair, db,
							 &pre->e_start, keysize,
							 &pre->e_end, recsize);
					rc = m0_table_update(tx, &pair);
					m0_db_pair_fini(&pair);
					if (rc) {
						M0_LEAVE();
						return rc;
					}
				}
			}
		} else if (found && pre == NULL) {
			/* on the head */
			if (tgt->e_end < cur->e_start) {
				/* to be the first one */

				m0_db_pair_setup(&pair, db,
						 &tgt->e_start, keysize,
						 &tgt->e_end, recsize);
				rc = m0_table_insert(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				grp->bgi_fragments++;
			} else {
				/* join the first one */

				m0_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = m0_table_delete(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				cur->e_start = tgt->e_start;

				m0_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = m0_table_insert(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
			}
		} else {
			/* in the middle */
			if (pre->e_end	 == tgt->e_start &&
			    tgt->e_end == cur->e_start) {
				/* joint to both */

				m0_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = m0_table_delete(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				pre->e_end = cur->e_end;

				m0_db_pair_setup(&pair, db,
						 &pre->e_start, keysize,
						 &pre->e_end, recsize);
				rc = m0_table_update(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				grp->bgi_fragments--;
			} else
			if (pre->e_end == tgt->e_start) {
				/* joint with prev */
				pre->e_end = tgt->e_end;

				m0_db_pair_setup(&pair, db,
						 &pre->e_start, keysize,
						 &pre->e_end, recsize);
				rc = m0_table_update(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
			} else
			if (tgt->e_end == cur->e_start) {
				/* joint with current */

				m0_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = m0_table_delete(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				cur->e_start = tgt->e_start;

				m0_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = m0_table_insert(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
			} else {
				/* add a new one */

				m0_db_pair_setup(&pair, db,
						 &tgt->e_start, keysize,
						 &tgt->e_end, recsize);
				rc = m0_table_insert(tx, &pair);
				m0_db_pair_fini(&pair);
				if (rc) {
					M0_LEAVE();
					return rc;
				}
				grp->bgi_fragments++;
			}
		}

		mero->cb_sb.bsb_freeblocks += m0_ext_length(tgt);
		/* TODO update the maxchunk please */
		grp->bgi_freeblocks += m0_ext_length(tgt);

		grp->bgi_state |= M0_BALLOC_GROUP_INFO_DIRTY;
		mero->cb_sb.bsb_state |= M0_BALLOC_SB_DIRTY;

		rc = balloc_sync_sb(mero, tx);
		if (rc == 0)
			rc = balloc_sync_group_info(mero, tx, grp);

		M0_LEAVE();
	} else {
		rc = -EINVAL;
		M0_LEAVE();
	}
	return rc;
}

static int balloc_find_by_goal(struct m0_balloc_allocation_context *bac)
{
	m0_bindex_t group = balloc_bn2gn(bac->bac_goal.e_start,
					 bac->bac_ctxt);
	struct m0_balloc_group_info *grp = m0_balloc_gn2info(bac->bac_ctxt,
							     group);

	struct m0_db_tx *tx  = bac->bac_tx;
	struct m0_ext	 ex  = { 0 };
	int		 found;
	int		 ret = 0;
	M0_ENTRY();

	if (!(bac->bac_flags & M0_BALLOC_HINT_TRY_GOAL))
		goto out;

	M0_LOG(M0_DEBUG, "groupno=%llu, start=%llu len=%llu, groupsize (%llu)\n",
		(unsigned long long)group,
		(unsigned long long)bac->bac_goal.e_start,
		(unsigned long long)m0_ext_length(&bac->bac_goal),
		(unsigned long long)bac->bac_ctxt->cb_sb.bsb_groupsize
	);

	m0_balloc_lock_group(grp);
	if (grp->bgi_maxchunk < m0_ext_length(&bac->bac_goal)) {
		M0_LEAVE();
		goto out_unlock;
	}
	if (grp->bgi_freeblocks < m0_ext_length(&bac->bac_goal)) {
		M0_LEAVE();
		goto out_unlock;
	}

	ret = m0_balloc_load_extents(bac->bac_ctxt, grp, tx);
	if (ret) {
		M0_LEAVE();
		goto out_unlock;
	}

	found = balloc_find_extent_exact(bac, grp, &bac->bac_goal, &ex);
	M0_LOG(M0_DEBUG, "found?max len = %llu\n",
	       (unsigned long long)m0_ext_length(&ex));

	if (found) {
		bac->bac_found++;
		bac->bac_best.e_start = bac->bac_goal.e_start;
		bac->bac_best.e_end   = ex.e_end;
		ret = balloc_use_best_found(bac);
	}

	/* update db according to the allocation result */
	if (ret == 0 && bac->bac_status == M0_BALLOC_AC_FOUND) {
		if (bac->bac_goal.e_end < bac->bac_best.e_end)
			balloc_new_preallocation(bac);

		ret = balloc_update_db(bac->bac_ctxt, tx, grp,
					  &bac->bac_final, M0_BALLOC_ALLOC);
	}

	m0_balloc_release_extents(grp);
	M0_LEAVE();
out_unlock:
	m0_balloc_unlock_group(grp);
out:
	return ret;
}

/* group is locked */
static int balloc_good_group(struct m0_balloc_allocation_context *bac,
			     struct m0_balloc_group_info *gi)
{
	m0_bcount_t	free	  = gi->bgi_freeblocks;
	m0_bcount_t	fragments = gi->bgi_fragments;

	if (free == 0)
		return 0;

	if (fragments == 0)
		return 0;

	switch (bac->bac_criteria) {
	case 0:
		if (gi->bgi_maxchunk >= m0_ext_length(&bac->bac_goal))
			return 1;
		break;
	case 1:
		if ((free / fragments) >= m0_ext_length(&bac->bac_goal))
			return 2;
		break;
	case 2:
			return 3;
		break;
	default:
		M0_ASSERT(0);
	}

	return 0;
}

/* group is locked */
static int balloc_simple_scan_group(struct m0_balloc_allocation_context *bac,
				    struct m0_balloc_group_info *grp)
{
/*
	struct m0_balloc_super_block *sb = &bac->bac_ctxt->cb_sb;
*/
	struct m0_ext	ex;
	m0_bcount_t	len;
	int		found = 0;
	M0_ENTRY();

	M0_ASSERT(bac->bac_order2 > 0);

	len = 1 << bac->bac_order2;
/*	for (; len <= sb->bsb_groupsize; len = len << 1) {
		M0_LOG(M0_DEBUG, "searching at %d (gs = %d) for order = %d,"
			" len=%d:%x\n",
			(int)grp->bgi_groupno,
			(int)sb->bsb_groupsize,
			(int)bac->bac_order2,
			(int)len,
			(int)len);

		found = balloc_find_extent_buddy(bac, grp, len, &ex);
		if (found)
			break;
	}
*/

	found = balloc_find_extent_buddy(bac, grp, len, &ex);
	if (found) {
		balloc_debug_dump_extent("found at simple scan", &ex);

		bac->bac_found++;
		bac->bac_best = ex;
		balloc_use_best_found(bac);
	}

	M0_LEAVE();
	return 0;
}

__attribute__((unused))
static int balloc_aligned_scan_group(struct m0_balloc_allocation_context *bac,
				     struct m0_balloc_group_info *grp)
{
	return 0;
}

/*
 * How long balloc can look for a best extent (in found extents)
 */
#define M0_BALLOC_DEFAULT_MAX_TO_SCAN	       200

/*
 * How long balloc must look for a best extent
 */
#define M0_BALLOC_DEFAULT_MIN_TO_SCAN	       10

/*
 * How many groups balloc will scan looking for the best chunk
 */
#define M0_BALLOC_DEFAULT_MAX_GROUPS_TO_SCAN   5


static int balloc_check_limits(struct m0_balloc_allocation_context *bac,
			       struct m0_balloc_group_info *grp,
			       int end_of_group)
{
	int max_to_scan = M0_BALLOC_DEFAULT_MAX_TO_SCAN;
	int max_groups	= M0_BALLOC_DEFAULT_MAX_GROUPS_TO_SCAN;
	int min_to_scan = M0_BALLOC_DEFAULT_MIN_TO_SCAN;
	M0_ENTRY();

	M0_LOG(M0_DEBUG, "check limits for group %llu. end = %d\n",
		(unsigned long long)grp->bgi_groupno,
		end_of_group);

	if (bac->bac_status == M0_BALLOC_AC_FOUND)
		return 0;

	if ((bac->bac_found > max_to_scan || bac->bac_scanned > max_groups) &&
		!(bac->bac_flags & M0_BALLOC_HINT_FIRST)) {
		bac->bac_status = M0_BALLOC_AC_BREAK;
		return 0;
	}

	if (m0_ext_length(&bac->bac_best) < m0_ext_length(&bac->bac_goal))
		return 0;

	if ((end_of_group || bac->bac_found >= min_to_scan)) {
		m0_bindex_t group = balloc_bn2gn(bac->bac_best.e_start,
						    bac->bac_ctxt);
		if (group == grp->bgi_groupno)
			balloc_use_best_found(bac);
	}

	return 0;
}

static int balloc_measure_extent(struct m0_balloc_allocation_context *bac,
				 struct m0_balloc_group_info *grp,
				 struct m0_ext *ex)
{
	struct m0_ext *goal = &bac->bac_goal;
	struct m0_ext *best = &bac->bac_best;
	int rc;
	M0_ENTRY();

	balloc_debug_dump_extent(__func__, ex);
	bac->bac_found++;

	if ((bac->bac_flags & M0_BALLOC_HINT_FIRST) ||
	     m0_ext_length(ex) == m0_ext_length(goal)) {
		*best = *ex;
		balloc_use_best_found(bac);
		M0_LEAVE();
		return 0;
	}

	if (m0_ext_length(best) == 0) {
		*best = *ex;
		M0_LEAVE();
		return 0;
	}

	if (m0_ext_length(best) < m0_ext_length(goal)) {
		/* req is still not satisfied. use the larger one */
		if (m0_ext_length(ex) > m0_ext_length(best)) {
			*best = *ex;
		}
	} else if (m0_ext_length(ex) > m0_ext_length(goal)) {
		/* req is satisfied. but it is satisfied again.
		   use the smaller one */
		if (m0_ext_length(ex) < m0_ext_length(best)) {
			*best = *ex;
		}
	}

	rc = balloc_check_limits(bac, grp, 0);
	M0_LEAVE();
	return rc;
}

/**
 * This function scans the specified group for a goal. If maximal
 * group is locked.
 */
static int balloc_wild_scan_group(struct m0_balloc_allocation_context *bac,
				  struct m0_balloc_group_info *grp)
{
	m0_bcount_t	 i;
	m0_bcount_t	 free;
	struct m0_ext	*ex;
	int		 rc;
	M0_ENTRY();

	free = grp->bgi_freeblocks;

	M0_LOG(M0_DEBUG, "Wild scanning at group %llu: freeblocks = %llu\n",
		(unsigned long long)grp->bgi_groupno,
		(unsigned long long)free);

	for (i = 0; i < grp->bgi_fragments; i++) {
		ex = &grp->bgi_extents[i];
		if (m0_ext_length(ex) > free) {
			M0_LOG(M0_DEBUG, "corrupt group = %llu,"
				" ex=[0x%08llx:0x%08llx)\n",
				(unsigned long long)grp->bgi_groupno,
				(unsigned long long)ex->e_start,
				(unsigned long long)ex->e_end);
			M0_LEAVE();
			return -EINVAL;
		}
		balloc_measure_extent(bac, grp, ex);

		free -= m0_ext_length(ex);
		if (free == 0 || bac->bac_status != M0_BALLOC_AC_CONTINUE) {
			M0_LEAVE();
			return 0;
		}
	}

	rc = balloc_check_limits(bac, grp, 1);
	M0_LEAVE();
	return rc;
}

/*
 * TRY to use the best result found in previous iteration.
 * The best result may be already used by others who are more lukcy.
 * Group lock should be taken again.
 */
static int balloc_try_best_found(struct m0_balloc_allocation_context *bac)
{
	struct m0_ext			*best  = &bac->bac_best;
	m0_bindex_t			 group = balloc_bn2gn(best->e_start,
								 bac->bac_ctxt);
	struct m0_balloc_group_info	*grp   =m0_balloc_gn2info(bac->bac_ctxt,
								  group);
	struct m0_ext			*ex;
	m0_bcount_t			 i;
	int				 rc    = -ENOENT;
	M0_ENTRY();

	m0_balloc_lock_group(grp);

	if (grp->bgi_freeblocks < m0_ext_length(best))
		goto out;

	rc = m0_balloc_load_extents(bac->bac_ctxt, grp, bac->bac_tx);
	if (rc != 0)
		goto out;

	rc = -ENOENT;
	for (i = 0; i < grp->bgi_fragments; i++) {
		ex = &grp->bgi_extents[i];
		if (m0_ext_equal(ex, best)) {
			rc = balloc_use_best_found(bac);
			break;
		} else if (ex->e_start > best->e_start)
			goto out_release;
	}

	/* update db according to the allocation result */
	if (rc == 0 && bac->bac_status == M0_BALLOC_AC_FOUND) {
		if (m0_ext_length(&bac->bac_goal) < m0_ext_length(best))
			balloc_new_preallocation(bac);

		balloc_debug_dump_extent(__func__, &bac->bac_final);
		rc = balloc_update_db(bac->bac_ctxt, bac->bac_tx, grp,
					 &bac->bac_final,
					 M0_BALLOC_ALLOC);
	}
out_release:
	m0_balloc_release_extents(grp);
out:
	m0_balloc_unlock_group(grp);
	M0_LEAVE();
	return rc;
}

static int
balloc_regular_allocator(struct m0_balloc_allocation_context *bac)
{
	m0_bcount_t	ngroups, group, i, len;
	int		cr;
	int		rc = 0;
	M0_ENTRY();

	ngroups = bac->bac_ctxt->cb_sb.bsb_groupcount;
	len = m0_ext_length(&bac->bac_goal);

	/* first, try the goal */
	rc = balloc_find_by_goal(bac);
	if (rc != 0 || bac->bac_status == M0_BALLOC_AC_FOUND ||
	    (bac->bac_flags & M0_BALLOC_HINT_GOAL_ONLY)) {
		M0_LEAVE();
		return rc;
	}

	/* XXX ffs works on little-endian platform? */
	i = ffs(len);
	bac->bac_order2 = 0;
	/*
	 * We search using buddy data only if the order of the request
	 * is greater than equal to the threshold.
	 */
	if (i >= 2) {
		/*
		 * This should tell if fe_len is exactly power of 2
		 */
		if ((len & (~(1 << (i - 1)))) == 0)
			bac->bac_order2 = i - 1;
	}

	cr = bac->bac_order2 ? 0 : 1;
	/*
	 * cr == 0 try to get exact allocation,
	 * cr == 1 striped allocation. Not implemented currently.
	 * cr == 2 try to get anything
	 */
repeat:
	for (;cr < 3 && bac->bac_status == M0_BALLOC_AC_CONTINUE; cr++) {
		bac->bac_criteria = cr;
		/*
		 * searching for the right group start
		 * from the goal value specified
		 */
		group = balloc_bn2gn(bac->bac_goal.e_start, bac->bac_ctxt);

		for (i = 0; i < ngroups; group++, i++) {
			struct m0_balloc_group_info *grp;

			if (group >= ngroups)
				group = 0;

			grp = m0_balloc_gn2info(bac->bac_ctxt, group);
			// m0_balloc_debug_dump_group("searching group ...\n",
			//			 grp);

			rc = m0_balloc_trylock_group(grp);
			if (rc != 0) {
				/* This group is under processing by others. */
				continue;
			}

			/* quick check to skip empty groups */
			if (grp->bgi_freeblocks == 0) {
				m0_balloc_unlock_group(grp);
				continue;
			}

			rc = m0_balloc_load_extents(bac->bac_ctxt, grp,
						    bac->bac_tx);
			if (rc != 0) {
				m0_balloc_unlock_group(grp);
				goto out;
			}

			if (!balloc_good_group(bac, grp)) {
				m0_balloc_release_extents(grp);
				m0_balloc_unlock_group(grp);
				continue;
			}
			bac->bac_scanned++;

			m0_balloc_debug_dump_group_extent("AAAAAAAAAAAAAAAAAA",
							  grp);
			if (cr == 0)
				rc = balloc_simple_scan_group(bac, grp);
			else if (cr == 1 &&
				len == bac->bac_ctxt->cb_sb.bsb_stripe_size)
				rc = balloc_simple_scan_group(bac, grp);
			else
				rc = balloc_wild_scan_group(bac, grp);

			/* update db according to the allocation result */
			if (rc == 0 && bac->bac_status == M0_BALLOC_AC_FOUND) {
				if (len < m0_ext_length(&bac->bac_best))
					balloc_new_preallocation(bac);

				rc = balloc_update_db(bac->bac_ctxt,
							 bac->bac_tx, grp,
							 &bac->bac_final,
							 M0_BALLOC_ALLOC);
			}


			m0_balloc_release_extents(grp);
			m0_balloc_unlock_group(grp);

			if (bac->bac_status != M0_BALLOC_AC_CONTINUE)
				break;
		}
	}

	if (m0_ext_length(&bac->bac_best) > 0 &&
	    (bac->bac_status != M0_BALLOC_AC_FOUND) &&
	    !(bac->bac_flags & M0_BALLOC_HINT_FIRST)) {
		/*
		 * We've been searching too long. Let's try to allocate
		 * the best chunk we've found so far
		 */

		rc = balloc_try_best_found(bac);
		if (rc || bac->bac_status != M0_BALLOC_AC_FOUND) {
			/*
			 * Someone more lucky has already allocated it.
			 * Let's just take first found block(s).
			 */
			bac->bac_status = M0_BALLOC_AC_CONTINUE;
			M0_SET0(&bac->bac_best);
			bac->bac_flags |= M0_BALLOC_HINT_FIRST;
			cr = 3;
			M0_LOG(M0_DEBUG, "Let's repeat..........\n");
			goto repeat;
		}
	}
out:
	M0_LEAVE();
	if (rc == 0 && bac->bac_status != M0_BALLOC_AC_FOUND)
		rc = -ENOSPC;
	return rc;
}

/**
   Allocate multiple blocks for some object.

   This routine will search suitable free space, and determine where to allocate
   from.  Caller can provide some hint (goal). Pre-allocation is used depending
   the character of the I/O sequences, and the current state of the active I/O.
   When trying allocate blocks from free space, we will allocate from the
   best suitable chunks, which are represented as buddy.

   Allocation will first try to use pre-allocation if it exists. Pre-allocation
   can be per-object, or group based.

   This routine will first check the group description to see if enough free
   space is available, and if largest contiguous chunk satisfy the request. This
   checking will be done group by group, until allocation succeeded or failed.
   If failed, the largest available contiguous chunk size is returned, and the
   caller can decide whether to use a smaller request.

   While searching free space from group to group, the free space extent will be
   loaded into cache.  We cache as much free space extent up to some
   specified memory limitation.	 This is a configurable parameter, or default
   value will be chosen based on system memory.

   @param ctx balloc operation context environment.
   @param req allocate request which includes all parameters.
   @return 0 means success.
	   Result allocated blocks are again stored in "req":
	   result physical block number = bar_physical,
	   result count of blocks = bar_len.
	   Upon failure, non-zero error number is returned.
 */
static
int balloc_allocate_internal(struct m0_balloc *ctx,
			     struct m0_db_tx *tx,
			     struct m0_balloc_allocate_req *req)
{
	struct m0_balloc_allocation_context	bac;
	int					rc;
	M0_ENTRY();

	while (req->bar_len &&
	       !balloc_claim_free_blocks(ctx, req->bar_len)) {
		req->bar_len = req->bar_len >> 1;
	}
	if (req->bar_len == 0) {
		rc = -ENOSPC;
		goto out;
	}

	balloc_init_ac(&bac, ctx, tx, req);

	/* Step 1. query the pre-allocation */
	if (!balloc_use_prealloc(&bac)) {
		/* we did not find suitable free space in prealloc. */

		balloc_normalize_request(&bac);

		/* Step 2. Iterate over groups */
		rc = balloc_regular_allocator(&bac);
		if (rc == 0 && bac.bac_status == M0_BALLOC_AC_FOUND) {
			/* store the result in req and they will be returned */
			req->bar_result = bac.bac_final;
		}
	}
out:
	M0_LEAVE();
	return rc;
}

/**
   Free multiple blocks owned by some object to free space.

   @param ctx balloc operation context environment.
   @param req block free request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
static int balloc_free_internal(struct m0_balloc *ctx,
				struct m0_db_tx *tx,
				struct m0_balloc_free_req *req)
{
	struct m0_ext			 fex;
	struct m0_balloc_group_info	*grp;
	struct m0_balloc_super_block	*sb = &ctx->cb_sb;
	m0_bcount_t			 group;
	m0_bindex_t			 start, off;
	m0_bcount_t			 len, step;
	int				 rc = 0;
	M0_ENTRY();

	start = req->bfr_physical;
	len = req->bfr_len;

	group = balloc_bn2gn(start + len, ctx);
	M0_LOG(M0_DEBUG, "start=0x%llx, len=0x%llx, start_group=%llu, "
		"end_group=%llu, group count=%llu\n",
		(unsigned long long)start,
		(unsigned long long)len,
		(unsigned long long)balloc_bn2gn(start, ctx),
		(unsigned long long)balloc_bn2gn(start + len, ctx),
		(unsigned long long)sb->bsb_groupcount
		);
	if (group > sb->bsb_groupcount)
		return -EINVAL;

	while (rc == 0 && len > 0) {
		group = balloc_bn2gn(start, ctx);

		grp = m0_balloc_gn2info(ctx, group);
		m0_balloc_lock_group(grp);

		rc = m0_balloc_load_extents(ctx, grp, tx);
		if (rc != 0) {
			m0_balloc_unlock_group(grp);
			goto out;
		}

		m0_balloc_debug_dump_group_extent(
			    "FFFFFFFFFFFFFFFFFFFFFFFFFFFF", grp);
		off = start & (sb->bsb_groupsize - 1);
		step = (off + len > sb->bsb_groupsize) ?
			sb->bsb_groupsize  - off : len;

		fex.e_start = start;
		fex.e_end   = start + step;
		rc = balloc_update_db(ctx, tx, grp,
					 &fex, M0_BALLOC_FREE);
		m0_balloc_release_extents(grp);
		m0_balloc_unlock_group(grp);
		start += step;
		len -= step;
	}

out:
	M0_LEAVE();
	return rc;
}

/**
   Discard the pre-allocation for object.

   @param ctx balloc operation context environment.
   @param req discard request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
__attribute__((unused))
static int balloc_discard_prealloc(struct m0_balloc *ctx,
				   struct m0_balloc_discard_req *req)
{
	return 0;
}

/**
   modify the allocation status forcibly.

   This function may be used by fsck or some other tools to modify the
   allocation status directly.

   @param ctx balloc operation context environment.
   @param alloc true to make the specifed extent as allocated, otherwise make
	  the extent as free.
   @param ext user supplied extent to check.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
__attribute__((unused))
static int balloc_enforce(struct m0_balloc *ctx, bool alloc,
			  struct m0_ext *ext)
{
	return 0;
}


/**
   Query the allocation status.

   @param ctx balloc operation context environment.
   @param ext user supplied extent to check.
   @return true if the extent is fully allocated. Otherwise, false is returned.
 */
__attribute__((unused))
static bool balloc_query(struct m0_balloc *ctx, struct m0_ext *ext)
{

	return false;
}

/**
 * allocate from underlying container.
 * @param count count of bytes. count will be first aligned to block boundry.
 * @param out result is stored there. space is still in bytes.
 */
static int balloc_alloc(struct m0_ad_balloc *ballroom, struct m0_dtx *tx,
			m0_bcount_t count, struct m0_ext *out)
{
	struct m0_balloc		*mero = b2m0(ballroom);
	struct m0_balloc_allocate_req	 req;
	int				 rc;

	M0_LOG(M0_DEBUG, "count=%lu", (unsigned long)count);
	M0_ASSERT(count > 0);

	req.bar_goal  = out->e_start; /* this also plays as the goal */
	req.bar_len   = count;
	req.bar_flags = 0 /*M0_BALLOC_HINT_DATA | M0_BALLOC_HINT_TRY_GOAL*/;

	M0_SET0(out);

	m0_mutex_lock(&mero->cb_sb_mutex);
	rc = balloc_allocate_internal(mero, &tx->tx_dbtx, &req);
	if (rc == 0 && !m0_ext_is_empty(&req.bar_result)) {
		out->e_start = req.bar_result.e_start;
		out->e_end   = req.bar_result.e_end;
		mero->cb_last = *out;
	} else if (rc == 0)
		rc = -ENOENT;
	m0_mutex_unlock(&mero->cb_sb_mutex);

	return rc;
}

/**
 * free spaces to container.
 * @param ext the space to be freed. This space must align to block boundry.
 */
static int balloc_free(struct m0_ad_balloc *ballroom, struct m0_dtx *tx,
		       struct m0_ext *ext)
{
	struct m0_balloc		*mero = b2m0(ballroom);
	struct m0_balloc_free_req	 req;
	int				 rc;

	req.bfr_physical = ext->e_start;
	req.bfr_len	 = m0_ext_length(ext);

	rc = balloc_free_internal(mero, &tx->tx_dbtx, &req);
	return rc;
}

static int balloc_init(struct m0_ad_balloc *ballroom, struct m0_dbenv *db,
		       uint32_t bshift, m0_bcount_t container_size,
		       m0_bcount_t blocks_per_group, m0_bcount_t res_groups)
{
	struct m0_balloc	*mero;
	int			 rc;
	M0_ENTRY();

	mero = b2m0(ballroom);

	rc = balloc_init_internal(mero, db, bshift, container_size,
				     blocks_per_group, res_groups);

	/*
         * Free the memory allocated for mero in
         * m0_balloc_allocate() on initialisation failure.
         */
	if (rc != 0)
             m0_free(mero);

	M0_LEAVE();
	return rc;
}

static void balloc_fini(struct m0_ad_balloc *ballroom)
{
	struct m0_balloc	*mero = b2m0(ballroom);
	struct m0_db_tx		 fini_tx;
	int			 rc;
	M0_ENTRY();

	rc = m0_db_tx_init(&fini_tx, mero->cb_dbenv, 0);
	if (rc == 0) {
		rc = balloc_fini_internal(mero, &fini_tx);
		if (rc == 0)
			rc = m0_db_tx_commit(&fini_tx);
		else
			m0_db_tx_abort(&fini_tx);
	}

	m0_free(mero);

	M0_LEAVE();
}

static const struct m0_ad_balloc_ops balloc_ops = {
	.bo_init  = balloc_init,
	.bo_fini  = balloc_fini,
	.bo_alloc = balloc_alloc,
	.bo_free  = balloc_free,
};

M0_INTERNAL int m0_balloc_allocate(uint64_t cid, struct m0_balloc **out)
{
	struct m0_balloc *cb;
	int               result;

        M0_PRE(out != NULL);

	M0_ALLOC_PTR(cb);
	if (cb != NULL) {
		cb->cb_container_id = cid;
                cb->cb_ballroom.ab_ops = &balloc_ops;
                *out = cb;
                result = 0;
	} else
                result = -ENOMEM;

	return result;
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
