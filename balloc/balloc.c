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

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "dtm/dtm.h"	  /* c2_dtx */
#include "lib/misc.h"	  /* C2_SET0 */
#include "lib/errno.h"
#include "lib/types.h"
#include "lib/arith.h"	  /* min_check, c2_is_po2 */
#include "lib/memory.h"
#include "balloc.h"

/**
   C2 Data Block Allocator.
   BALLOC is a multi-block allocator, with pre-allocation. All metadata about
   block allocation is stored in database -- Oracle Berkeley DB.

 */

/* This macro is to control the debug verbose message */
/*
#define BALLOC_DEBUG
*/

#ifdef BALLOC_DEBUG
#define ENTER fprintf(stderr, "===>>> %s:%d:%s\n", __FILE__, __LINE__, __func__)
#define LEAVE fprintf(stderr, "<<<=== %s:%d:%s\n", __FILE__, __LINE__, __func__)
#define GOTHERE fprintf(stderr "!!! %s:%d:%s\n", __FILE__, __LINE__, __func__)

  #define debugp(fmt, a...)					\
	do {							\
		fprintf(stderr, "(%s, %d): %s: ",		\
		       __FILE__, __LINE__, __func__);		\
		fprintf(stderr, fmt, ## a);			\
	} while (0)

#else
  #define ENTER
  #define LEAVE
  #define GOTHERE
  #define debugp(fmt, a...)
#endif

/*
#define BALLOC_ENABLE_DUMP
*/
static void balloc_debug_dump_extent(const char *tag, struct c2_ext *ex)
{
#ifdef BALLOC_ENABLE_DUMP

	if (ex == NULL)
		return;

	fprintf(stderr, "dumping ex@%p:%s\n"
	       "|----[%10llu, %10llu), [0x%08llx, 0x%08llx)\n",
		ex, tag,
		(unsigned long long) ex->e_start,
		(unsigned long long) ex->e_end,
		(unsigned long long) ex->e_start,
		(unsigned long long) ex->e_end);
#endif
}

void c2_balloc_debug_dump_group(const char *tag,
				struct c2_balloc_group_info *grp)
{
#ifdef BALLOC_ENABLE_DUMP
	if (grp == NULL)
		return;

	fprintf(stderr, "dumping group_desc@%p:%s\n"
	       "|-----groupno=%08llx, freeblocks=%08llx, maxchunk=0x%08llx, "
		"fragments=0x%08llx\n",
		grp, tag,
		(unsigned long long) grp->bgi_groupno,
		(unsigned long long) grp->bgi_freeblocks,
		(unsigned long long) grp->bgi_maxchunk,
		(unsigned long long) grp->bgi_fragments);
#endif
}

void c2_balloc_debug_dump_group_extent(const char *tag,
				       struct c2_balloc_group_info *grp)
{
#ifdef BALLOC_ENABLE_DUMP
	c2_bcount_t	 i;
	struct c2_ext	*ex;

	if (grp == NULL || grp->bgi_extents == NULL)
		return;

	fprintf(stderr, "dumping free extents@%p:%s for %08llx\n",
		grp, tag, (unsigned long long) grp->bgi_groupno);
	for (i = 0; i < grp->bgi_fragments; i++) {
		ex = &grp->bgi_extents[i];
		fprintf(stderr, "[0x%08llx, 0x%08llx) ",
			(unsigned long long) ex->e_start,
			(unsigned long long) ex->e_end);
	}
	fprintf(stderr, "\n");
#endif
}

void c2_balloc_debug_dump_sb(const char *tag, struct c2_balloc_super_block *sb)
{
#ifdef BALLOC_ENABLE_DUMP
	if (sb == NULL)
		return;

	fprintf(stderr, "dumping sb@%p:%s\n"
		"|-----magic=%llx, state=%llu, version=%llu\n"
		"|-----total=%llu, free=%llu, bs=%llu(bits=%lu)\n"
		"|-----gs=%llu(bits=%lu), gc=%llu, rsvd=%llu, prealloc=%llu\n"
		"|-----time format=%llu,\n"
		"|-----write=%llu,\n"
		"|-----mnt  =%llu,\n"
		"|-----last =%llu\n"
		"|-----mount=%llu, max_mnt=%llu, stripe_size=%llu\n",
		sb, tag,
		(unsigned long long) sb->bsb_magic,
		(unsigned long long) sb->bsb_state,
		(unsigned long long) sb->bsb_version,
		(unsigned long long) sb->bsb_totalsize,
		(unsigned long long) sb->bsb_freeblocks,
		(unsigned long long) sb->bsb_blocksize,
		(unsigned long	   ) sb->bsb_bsbits,
		(unsigned long long) sb->bsb_groupsize,
		(unsigned long	   ) sb->bsb_gsbits,
		(unsigned long long) sb->bsb_groupcount,
		(unsigned long long) sb->bsb_reserved_groups,
		(unsigned long long) sb->bsb_prealloc_count,
		(unsigned long long) sb->bsb_format_time,
		(unsigned long long) sb->bsb_write_time,
		(unsigned long long) sb->bsb_mnt_time,
		(unsigned long long) sb->bsb_last_check_time,
		(unsigned long long) sb->bsb_mnt_count,
		(unsigned long long) sb->bsb_max_mnt_count,
		(unsigned long long) sb->bsb_stripe_size
		);
#endif
}

static inline c2_bindex_t
balloc_bn2gn(c2_bindex_t blockno, struct c2_balloc *cb)
{
	return blockno >> cb->cb_sb.bsb_gsbits;
}

struct c2_balloc_group_info * c2_balloc_gn2info(struct c2_balloc *cb,
						c2_bindex_t groupno)
{
	if (cb->cb_group_info)
		return &cb->cb_group_info[groupno];
	else
		return NULL;
}

int c2_balloc_release_extents(struct c2_balloc_group_info *grp)
{
	C2_ASSERT(c2_mutex_is_locked(&grp->bgi_mutex));
	if (grp->bgi_extents) {
		c2_free(grp->bgi_extents);
		grp->bgi_extents = NULL;
	}
	return 0;
}

void c2_balloc_lock_group(struct c2_balloc_group_info *grp)
{
	c2_mutex_lock(&grp->bgi_mutex);
}

int c2_balloc_trylock_group(struct c2_balloc_group_info *grp)
{
	return c2_mutex_trylock(&grp->bgi_mutex);
}

void c2_balloc_unlock_group(struct c2_balloc_group_info *grp)
{
	c2_mutex_unlock(&grp->bgi_mutex);
}


#define MAX_ALLOCATION_CHUNK 2048ULL

/**
   finaliazation of the balloc environment.
 */
static int balloc_fini_internal(struct c2_balloc *colibri,
				struct c2_db_tx  *tx)
{
	struct c2_balloc_group_info	*gi;
	int				 i;
	ENTER;

	if (colibri->cb_group_info) {
		for (i = 0 ; i < colibri->cb_sb.bsb_groupcount; i++) {
			gi = &colibri->cb_group_info[i];
			c2_balloc_lock_group(gi);
			c2_balloc_release_extents(gi);
			c2_balloc_unlock_group(gi);
		}

		c2_free(colibri->cb_group_info);
		colibri->cb_group_info = NULL;
	}

	c2_table_fini(&colibri->cb_db_group_extents);
	c2_table_fini(&colibri->cb_db_group_desc);
	c2_table_fini(&colibri->cb_db_sb);
	LEAVE;
	return 0;
}

/**
   Comparison function for block number, supplied to database.
 */
static int balloc_blockno_compare(struct c2_table *t,
				  const void *k0, const void *k1)
{
	const c2_bindex_t *bn0;
	const c2_bindex_t *bn1;

	bn0 = (c2_bindex_t*)k0;
	bn1 = (c2_bindex_t*)k1;
	return C2_3WAY(*bn0, *bn1);
}

static const struct c2_table_ops c2_super_block_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (uint64_t) },
		[TO_REC] = { .max_size = sizeof (struct c2_balloc_super_block) }
	},
	.key_cmp = NULL
};

static const struct c2_table_ops c2_group_extent_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (c2_bindex_t) },
		[TO_REC] = { .max_size = sizeof (c2_bindex_t) }
	},
	.key_cmp = balloc_blockno_compare,
};

static const struct c2_table_ops c2_group_desc_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof (c2_bindex_t) },
		[TO_REC] = { .max_size = sizeof (struct c2_balloc_group_desc) }
	},
	.key_cmp = NULL
};

/**
   Format the container: create database, fill them with initial information.

   This routine will create a "super_block" database to store global parameters
   for this container. It will also create "group free extent" and "group_desc"
   for every group. If some groups are reserved for special purpse, then they
   will be marked as "allocated" at the format time, and those groups will not
   be used by normal allocation routines.

   @param req pointer to this format request. All configuration will be passed
	  by this parameter.
   @return 0 means success. Otherwize, error number will be returned.
 */
static int balloc_format(struct c2_balloc *colibri,
			 struct c2_balloc_format_req *req)
{
	struct timeval			 now;
	struct c2_db_pair		 pair;
	c2_bcount_t			 number_of_groups;
	c2_bcount_t			 i;
	int				 sz;
	int				 rc;
	struct c2_db_tx			 format_tx;
	int				 tx_started = 0;
	struct c2_balloc_group_info	*grp;
	struct c2_ext			 ext;
	struct c2_balloc_group_desc	 gd;
	struct c2_balloc_super_block	*sb	    = &colibri->cb_sb;
	ENTER;

	C2_PRE(c2_is_po2(req->bfr_blocksize));
	C2_PRE(c2_is_po2(req->bfr_groupsize));

	number_of_groups = req->bfr_totalsize / req->bfr_groupsize /
		req->bfr_blocksize;

	if (number_of_groups == 0)
		number_of_groups = 1;

	debugp("total=%llu, bs=%llu, groupsize=%llu, groups=%llu resvd=%llu\n",
		(unsigned long long)req->bfr_totalsize,
		(unsigned long long)req->bfr_blocksize,
		(unsigned long long)req->bfr_groupsize,
		(unsigned long long)number_of_groups,
		(unsigned long long)req->bfr_reserved_groups);

	if (number_of_groups <= req->bfr_reserved_groups) {
		fprintf(stderr, "container is too small\n");
		return -EINVAL;
	}

	gettimeofday(&now, NULL);
	/* TODO verification of these parameters */
	sb->bsb_magic		= C2_BALLOC_SB_MAGIC;
	sb->bsb_state		= 0;
	sb->bsb_version		= C2_BALLOC_SB_VERSION;
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

	c2_db_pair_setup(&pair, &colibri->cb_db_sb,
			 &sb->bsb_magic, sizeof sb->bsb_magic,
			 sb, sizeof *sb);
	rc = c2_db_tx_init(&format_tx, colibri->cb_dbenv, 0);
	if (rc == 0) {
		rc = c2_table_insert(&format_tx, &pair);
		if (rc == 0)
			rc = c2_db_tx_commit(&format_tx);
		else
			c2_db_tx_abort(&format_tx);
	}
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	if (rc != 0) {
		fprintf(stderr, "insert super_block failed:"
			"rc=%d\n", rc);
		return rc;
	}

	sz = sb->bsb_groupcount * sizeof (struct c2_balloc_group_info);
	colibri->cb_group_info = c2_alloc(sz);
	if (colibri->cb_group_info == NULL) {
		fprintf(stderr, "create allocate memory for group info\n");
		rc = -ENOMEM;
		return rc;
	}

	for (i = 0; i < number_of_groups; i++) {
		grp = &colibri->cb_group_info[i];

		debugp("creating group_extents for group %llu\n",
		       (unsigned long long)i);
		ext.e_start = i << sb->bsb_gsbits;
		if (i < req->bfr_reserved_groups) {
			ext.e_end = ext.e_start;
		} else {
			ext.e_end = (i + 1) << sb->bsb_gsbits;
		}
		c2_db_pair_setup(&pair, &colibri->cb_db_group_extents,
				 &ext.e_start, sizeof ext.e_start,
				 &ext.e_end, sizeof ext.e_end);
		if (!tx_started) {
			rc = c2_db_tx_init(&format_tx, colibri->cb_dbenv, 0);
			if (rc != 0)
				break;
			tx_started = 1;
		}
		rc = c2_table_insert(&format_tx, &pair);

		c2_db_pair_release(&pair);
		c2_db_pair_fini(&pair);
		if (rc != 0) {
			fprintf(stderr, "insert extent failed:"
				"group=%llu, rc=%d\n", (unsigned long long)i,
				rc);
			break;
		}

		debugp("creating group_desc for group %llu\n",
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
		c2_db_pair_setup(&pair, &colibri->cb_db_group_desc,
				 &gd.bgd_groupno,
				 sizeof gd.bgd_groupno,
				 &gd,
				 sizeof gd);
		grp->bgi_groupno = i;
		grp->bgi_freeblocks = gd.bgd_freeblocks;
		grp->bgi_fragments  = gd.bgd_fragments;
		grp->bgi_maxchunk   = gd.bgd_maxchunk;
		rc = c2_table_insert(&format_tx, &pair);
		c2_db_pair_release(&pair);
		c2_db_pair_fini(&pair);
		if (rc != 0) {
			fprintf(stderr, "insert gd failed:"
				"group=%llu, rc=%d\n", (unsigned long long)i,
				rc);
			break;
		}
		if ((i & 0x3ff) == 0) {
			if (rc == 0)
				rc = c2_db_tx_commit(&format_tx);
			else
				c2_db_tx_abort(&format_tx);
			tx_started = 0;
		}
	}
	if (tx_started) {
		if (rc == 0)
			rc = c2_db_tx_commit(&format_tx);
		else
			c2_db_tx_abort(&format_tx);
	}
	LEAVE;
	return rc;
}

static int balloc_read_sb(struct c2_balloc *cb, struct c2_db_tx *tx)
{
	struct c2_balloc_super_block	*sb = &cb->cb_sb;
	struct c2_db_pair		 pair;
	int				 rc;

	sb->bsb_magic = C2_BALLOC_SB_MAGIC;
	c2_db_pair_setup(&pair, &cb->cb_db_sb,
			 &sb->bsb_magic, sizeof sb->bsb_magic,
			 sb, sizeof *sb);

	rc = c2_table_lookup(tx, &pair);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

	return rc;
}

static int balloc_sync_sb(struct c2_balloc *cb, struct c2_db_tx *tx)
{
	struct c2_balloc_super_block	*sb = &cb->cb_sb;
	struct c2_db_pair		 pair;
	struct timeval			 now;
	int				 rc;
	ENTER;

	if (!(cb->cb_sb.bsb_state & C2_BALLOC_SB_DIRTY)) {
		LEAVE;
		return 0;
	}

	gettimeofday(&now, NULL);
	sb->bsb_write_time = ((uint64_t)now.tv_sec) << 32 | now.tv_usec;

	sb->bsb_magic = C2_BALLOC_SB_MAGIC;
	c2_db_pair_setup(&pair, &cb->cb_db_sb,
			 &sb->bsb_magic, sizeof sb->bsb_magic,
			 sb, sizeof *sb);

	rc = c2_table_update(tx, &pair);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

	cb->cb_sb.bsb_state &= ~C2_BALLOC_SB_DIRTY;
	LEAVE;
	return rc;
}

static int balloc_sync_group_info(struct c2_balloc *cb,
				  struct c2_db_tx *tx,
				  struct c2_balloc_group_info *gi)
{
	struct c2_balloc_group_desc	gd;
	struct c2_db_pair		pair;
	int				rc;
	ENTER;

	if (! (gi->bgi_state & C2_BALLOC_GROUP_INFO_DIRTY)) {
		LEAVE;
		return 0;
	}

	gd.bgd_groupno	  = gi->bgi_groupno;
	gd.bgd_freeblocks = gi->bgi_freeblocks;
	gd.bgd_fragments  = gi->bgi_fragments;
	gd.bgd_maxchunk	  = gi->bgi_maxchunk;
	c2_db_pair_setup(&pair, &cb->cb_db_group_desc,
			 &gd.bgd_groupno, sizeof gd.bgd_groupno,
			 &gd, sizeof gd);

	rc = c2_table_update(tx, &pair);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	gi->bgi_state &= ~C2_BALLOC_GROUP_INFO_DIRTY;

	LEAVE;
	return rc;
}


static int balloc_load_group_info(struct c2_balloc *cb,
				  struct c2_db_tx *tx,
				  struct c2_balloc_group_info *gi)
{
	struct c2_balloc_group_desc	gd = { 0 };
	struct c2_db_pair		pair;
	int				rc;

	// debugp("loading group info for = %llu\n",
	// (unsigned long long)gi->bgi_groupno);

	c2_db_pair_setup(&pair, &cb->cb_db_group_desc,
			 &gd.bgd_groupno, sizeof gd.bgd_groupno,
			 &gd, sizeof gd);
	gd.bgd_groupno = gi->bgi_groupno;
	rc = c2_table_lookup(tx, &pair);

	if (rc == 0) {
		gi->bgi_groupno	   = gd.bgd_groupno;
		gi->bgi_freeblocks = gd.bgd_freeblocks;
		gi->bgi_fragments  = gd.bgd_fragments;
		gi->bgi_maxchunk   = gd.bgd_maxchunk;
		gi->bgi_state	   = C2_BALLOC_GROUP_INFO_INIT;
		gi->bgi_extents	   = NULL;
		c2_list_init(&gi->bgi_prealloc_list);
		c2_mutex_init(&gi->bgi_mutex);
	}
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

	return rc;
}

/*
 * start transaction for init() and format() respectively.
 * One transaction maybe fail to include all update. Multiple transaction
 * is used here.
 * The same reason for format().
 */
static int balloc_init_internal(struct c2_balloc *colibri,
				struct c2_dbenv  *dbenv,
				uint32_t bshift,
				c2_bcount_t container_size,
				c2_bcount_t blocks_per_group,
				c2_bcount_t res_groups)
{
	struct c2_balloc_group_info	*gi;
	int				 rc;
	c2_bcount_t			 i;
	struct c2_db_tx			 init_tx;
	struct timeval			 now;
	int				 tx_started = 0;
	ENTER;

	colibri->cb_dbenv = dbenv;
	colibri->cb_group_info = NULL;
	c2_mutex_init(&colibri->cb_sb_mutex);
	C2_SET0(&colibri->cb_sb);
	C2_SET0(&colibri->cb_db_group_extents);
	C2_SET0(&colibri->cb_db_group_desc);

	rc = c2_table_init(&colibri->cb_db_sb, dbenv,
			   "super_block", 0,
			   &c2_super_block_ops) ||
	     c2_table_init(&colibri->cb_db_group_desc, dbenv,
			   "group_desc", 0,
			   &c2_group_desc_ops)	||
	     c2_table_init(&colibri->cb_db_group_extents, dbenv,
			   "group_extents", 0,
			   &c2_group_extent_ops);
	if (rc != 0)
		goto out;

	rc = c2_db_tx_init(&init_tx, dbenv, 0);
	if (rc == 0) {
		rc = balloc_read_sb(colibri, &init_tx);
		c2_db_tx_commit(&init_tx);
	}

	if (rc == -ENOENT) {
		struct c2_balloc_format_req req = { 0 };

		/* let's format this container */
		req.bfr_totalsize = container_size;
		req.bfr_blocksize = 1 << bshift;
		req.bfr_groupsize = blocks_per_group;
		req.bfr_reserved_groups = res_groups;

		rc = balloc_format(colibri, &req);
		if (rc != 0)
			balloc_fini_internal(colibri, NULL);
		LEAVE;
		return rc;
	} else if (rc != 0)
		goto out;

	/* update the db */
	++ colibri->cb_sb.bsb_mnt_count;
	gettimeofday(&now, NULL);
	colibri->cb_sb.bsb_mnt_time = ((uint64_t)now.tv_sec) << 32 |
		now.tv_usec;
	colibri->cb_sb.bsb_state |= C2_BALLOC_SB_DIRTY;

	rc = c2_db_tx_init(&init_tx, dbenv, 0);
	if (rc == 0) {
		rc = balloc_sync_sb(colibri, &init_tx);
		if (rc == 0)
			rc = c2_db_tx_commit(&init_tx);
		else
			c2_db_tx_abort(&init_tx);

	}
	if (rc != 0)
		goto out;

	debugp("Group Count = %lu\n", colibri->cb_sb.bsb_groupcount);

	if (colibri->cb_sb.bsb_blocksize != 1 << bshift) {
		rc = -EINVAL;
		goto out;
	}

	i = colibri->cb_sb.bsb_groupcount *
		sizeof (struct c2_balloc_group_info);
	colibri->cb_group_info = c2_alloc(i);
	if (colibri->cb_group_info == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	debugp("Loading group info. Please wait...\n");
	for (i = 0; i < colibri->cb_sb.bsb_groupcount; i++ ) {
		gi = &colibri->cb_group_info[i];
		gi->bgi_groupno = i;

		if (!tx_started) {
			rc = c2_db_tx_init(&init_tx, dbenv, 0);
			if (rc != 0)
				break;
			tx_started = 1;
		}
		rc = balloc_load_group_info(colibri, &init_tx, gi);

		if ((i & 0x3ff) == 0) {
			if (rc == 0)
				rc = c2_db_tx_commit(&init_tx);
			else
				c2_db_tx_abort(&init_tx);
			tx_started = 0;
		}
		if (rc != 0)
			break;

		/* TODO verify the super_block info based on the group info */
	}
	if (tx_started) {
		if (rc == 0)
			rc = c2_db_tx_commit(&init_tx);
		else
			c2_db_tx_abort(&init_tx);
	}
out:
	if (rc != 0)
		balloc_fini_internal(colibri, NULL);
	LEAVE;
	return rc;
}

enum c2_balloc_allocation_status {
	C2_BALLOC_AC_FOUND    = 1,
	C2_BALLOC_AC_CONTINUE = 2,
	C2_BALLOC_AC_BREAK    = 3,
};

struct c2_balloc_allocation_context {
	struct c2_balloc	      *bac_ctxt;
	struct c2_db_tx		      *bac_tx;
	struct c2_balloc_allocate_req *bac_req;
	struct c2_ext		       bac_orig; /*< original */
	struct c2_ext		       bac_goal; /*< after normalization */
	struct c2_ext		       bac_best; /*< best available */
	struct c2_ext		       bac_final;/*< final results */

	uint64_t		       bac_flags;
	uint64_t		       bac_criteria;
	uint32_t		       bac_order2;  /* order of 2 */
	uint32_t		       bac_scanned; /* groups scanned */
	uint32_t		       bac_found;   /* count of found */
	uint32_t		       bac_status;  /* allocation status */
};

static int balloc_init_ac(struct c2_balloc_allocation_context *bac,
			  struct c2_balloc *colibri,
			  struct c2_db_tx * tx,
			  struct c2_balloc_allocate_req *req)
{
	ENTER;

	C2_SET0(&req->bar_result);

	bac->bac_ctxt	  = colibri;
	bac->bac_tx	  = tx;
	bac->bac_req	  = req;
	bac->bac_order2	  = 0;
	bac->bac_scanned  = 0;
	bac->bac_found	  = 0;
	bac->bac_flags	  = req->bar_flags;
	bac->bac_status	  = C2_BALLOC_AC_CONTINUE;
	bac->bac_criteria = 0;

	if (req->bar_goal == 0)
		req->bar_goal = colibri->cb_last.e_end;

	bac->bac_orig.e_start	= req->bar_goal;
	bac->bac_orig.e_end	= req->bar_goal + req->bar_len;
	bac->bac_goal = bac->bac_orig;

	C2_SET0(&bac->bac_best);
	C2_SET0(&bac->bac_final);

	LEAVE;
	return 0;
}


static int balloc_use_prealloc(struct c2_balloc_allocation_context *bac)
{
	return 0;
}

static int balloc_claim_free_blocks(struct c2_balloc *colibri,
				    c2_bcount_t blocks)
{
	int rc;
	ENTER;

	debugp("bsb_freeblocks = %llu, blocks=%llu\n",
		(unsigned long long)colibri->cb_sb.bsb_freeblocks,
		(unsigned long long)blocks);
	c2_mutex_lock(&colibri->cb_sb_mutex);
		rc = (colibri->cb_sb.bsb_freeblocks >= blocks);
	c2_mutex_unlock(&colibri->cb_sb_mutex);

	LEAVE;
	return rc;
}

/*
 * here we normalize request for locality group
 */
static void
balloc_normalize_group_request(struct c2_balloc_allocation_context *bac)
{
}

/*
 * Normalization means making request better in terms of
 * size and alignment
 */
static void
balloc_normalize_request(struct c2_balloc_allocation_context *bac)
{
	c2_bcount_t size = c2_ext_length(&bac->bac_orig);
	ENTER;

	/* do normalize only for data requests. metadata requests
	   do not need preallocation */
	if (!(bac->bac_flags & C2_BALLOC_HINT_DATA))
		return;

	/* sometime caller may want exact blocks */
	if (bac->bac_flags & C2_BALLOC_HINT_GOAL_ONLY)
		return;

	/* caller may indicate that preallocation isn't
	 * required (it's a tail, for example) */
	if (bac->bac_flags & C2_BALLOC_HINT_NOPREALLOC)
		return;

	if (bac->bac_flags & C2_BALLOC_HINT_GROUP_ALLOC) {
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
		debugp("lenth %llu is too large, truncate to %llu\n",
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

	debugp("goal: start=%llu=(0x%08llx), size=%llu(was %llu)\n",
		(unsigned long long) bac->bac_goal.e_start,
		(unsigned long long) bac->bac_goal.e_start,
		(unsigned long long) c2_ext_length(&bac->bac_goal),
		(unsigned long long) c2_ext_length(&bac->bac_orig));
	LEAVE;
}

/* called under group lock */
int c2_balloc_load_extents(struct c2_balloc *cb,
			   struct c2_balloc_group_info *grp,
			   struct c2_db_tx *tx)
{
	struct c2_table		*db_ext	  = &cb->cb_db_group_extents;
	struct c2_db_cursor	 cursor;
	struct c2_db_pair	 pair;
	struct c2_ext		*ex;
	struct c2_ext		 start;
	int			 result	  = 0;
	int			 size;
	c2_bcount_t		 maxchunk = 0;
	c2_bcount_t		 count	  = 0;

	C2_ASSERT(c2_mutex_is_locked(&grp->bgi_mutex));
	if (grp->bgi_extents != NULL) {
		/* already loaded */
		return 0;
	}

	size = (grp->bgi_fragments + 1) * sizeof (struct c2_ext);
	grp->bgi_extents = c2_alloc(size);
	if (grp->bgi_extents == NULL)
		return -ENOMEM;

	if (grp->bgi_fragments == 0)
		return 0;

	result = c2_db_cursor_init(&cursor, db_ext, tx, 0);
	if (result != 0) {
		c2_balloc_release_extents(grp);
		return result;
	}

	start.e_start = grp->bgi_groupno << cb->cb_sb.bsb_gsbits;
	c2_db_pair_setup(&pair, db_ext,
			 &start.e_start, sizeof start.e_start,
			 &start.e_end, sizeof start.e_end);
	result = c2_db_cursor_get(&cursor, &pair);
	if (result != 0) {
		c2_db_cursor_fini(&cursor);
		c2_balloc_release_extents(grp);
		return result;
	}
	ex = &grp->bgi_extents[0];
	*ex = start;
	count ++;
	if (c2_ext_length(ex) > maxchunk)
		maxchunk = c2_ext_length(ex);

	while (count < grp->bgi_fragments) {
		ex = &grp->bgi_extents[count];
		c2_db_pair_setup(&pair, db_ext,
				 &ex->e_start, sizeof ex->e_start,
				 &ex->e_end, sizeof ex->e_end);
		result = c2_db_cursor_next(&cursor, &pair);
		if ( result != 0)
			break;

		if (c2_ext_length(ex) > maxchunk)
			maxchunk = c2_ext_length(ex);
		//balloc_debug_dump_extent("loading...", ex);

		count++;
	}
	c2_db_cursor_fini(&cursor);

	if (result == -ENOENT && count != grp->bgi_fragments)
		debugp("fragments mismatch: count=%llu, fragments=%lld\n",
			(unsigned long long)count,
			(unsigned long long)grp->bgi_fragments);
	if (result != 0)
		c2_balloc_release_extents(grp);

	if (grp->bgi_maxchunk != maxchunk) {
		grp->bgi_state |= C2_BALLOC_GROUP_INFO_DIRTY;
		grp->bgi_maxchunk = maxchunk;
		balloc_sync_group_info(cb, tx, grp);
	}

	return result;
}

/* called under group lock */
static int balloc_find_extent_exact(struct c2_balloc_allocation_context *bac,
				    struct c2_balloc_group_info *grp,
				    struct c2_ext *goal,
				    struct c2_ext *ex)
{
	c2_bcount_t	 i;
	int	 	 found = 0;
	struct c2_ext	*fragment;

	C2_ASSERT(c2_mutex_is_locked(&grp->bgi_mutex));

	for (i = 0; i < grp->bgi_fragments; i++) {
		fragment = &grp->bgi_extents[i];

		if (c2_ext_is_partof(fragment, goal)) {
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
static int balloc_find_extent_buddy(struct c2_balloc_allocation_context *bac,
				    struct c2_balloc_group_info *grp,
				    c2_bcount_t len,
				    struct c2_ext *ex)
{
	struct c2_balloc_super_block	*sb    = &bac->bac_ctxt->cb_sb;
	c2_bcount_t			 i;
	c2_bcount_t			 found = 0;
	c2_bindex_t			 start;
	struct c2_ext			*fragment;
	struct c2_ext			min = {
						.e_start = 0,
						.e_end = 0xffffffff };

	C2_ASSERT(c2_mutex_is_locked(&grp->bgi_mutex));

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
		       (c2_ext_length(fragment) >= len)) {
			found = 1;
			if (c2_ext_length(fragment) < c2_ext_length(&min))
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


static int balloc_use_best_found(struct c2_balloc_allocation_context *bac)
{
	bac->bac_final.e_start = bac->bac_best.e_start;
	bac->bac_final.e_end   = bac->bac_final.e_start +
				 min_check(c2_ext_length(&bac->bac_best),
					   c2_ext_length(&bac->bac_goal));
	bac->bac_status = C2_BALLOC_AC_FOUND;

	return 0;
}

static int balloc_new_preallocation(struct c2_balloc_allocation_context *bac)
{
	/* XXX No Preallocation now. So, trim the result to the original length. */

	bac->bac_final.e_end = bac->bac_final.e_start +
					min_check(c2_ext_length(&bac->bac_orig),
					          c2_ext_length(&bac->bac_final));
	return 0;
}

enum c2_balloc_update_operation {
	C2_BALLOC_ALLOC = 1,
	C2_BALLOC_FREE	= 2,
};


/* the group is under lock now */
static int balloc_update_db(struct c2_balloc *colibri,
			    struct c2_db_tx *tx,
			    struct c2_balloc_group_info *grp,
			    struct c2_ext *tgt,
			    enum c2_balloc_update_operation op)
{
	size_t			 keysize = sizeof (tgt->e_start);
	size_t			 recsize = sizeof (tgt->e_end);
	struct c2_table		*db	 = &colibri->cb_db_group_extents;
	c2_bcount_t		 i;
	struct c2_db_pair	 pair;
	int			 rc	 = 0;
	ENTER;

	C2_ASSERT(c2_mutex_is_locked(&grp->bgi_mutex));
	balloc_debug_dump_extent("target=", tgt);
	if (op == C2_BALLOC_ALLOC) {
		struct c2_ext *cur = NULL;

		for (i = 0; i < grp->bgi_fragments; i++) {
			cur = &grp->bgi_extents[i];

			if (c2_ext_is_partof(cur, tgt))
				break;
		}

		C2_ASSERT(i < grp->bgi_fragments);

		balloc_debug_dump_extent("current=", cur);

		if (cur->e_start == tgt->e_start) {
			c2_db_pair_setup(&pair, db,
					 &cur->e_start, keysize,
					 &cur->e_end, recsize);

			/* at the head of a free extent */
			rc = c2_table_delete(tx, &pair);
			c2_db_pair_fini(&pair);
			if (rc) {
				LEAVE;
				return rc;
			}

			if (tgt->e_end < cur->e_end) {
				/* A smaller extent still exists */
				cur->e_start = tgt->e_end;
				c2_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = c2_table_insert(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
			} else {
				grp->bgi_fragments--;
			}
		} else {
			struct c2_ext next = *cur;

			/* in the middle of a free extent. Truncate it */
			cur->e_end = tgt->e_start;

			c2_db_pair_setup(&pair, db,
					 &cur->e_start, keysize,
					 &cur->e_end, recsize);
			rc = c2_table_update(tx, &pair);
			c2_db_pair_fini(&pair);
			if (rc) {
				LEAVE;
				return rc;
			}

			if (next.e_end > tgt->e_end) {
				/* there is still a tail */
				next.e_start = tgt->e_end;
				c2_db_pair_setup(&pair, db,
						 &next.e_start, keysize,
						 &next.e_end, recsize);
				rc = c2_table_update(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				grp->bgi_fragments++;
			}
		}

		colibri->cb_sb.bsb_freeblocks -= c2_ext_length(tgt);
		/* TODO update the maxchunk please */
		grp->bgi_freeblocks -= c2_ext_length(tgt);

		grp->bgi_state |= C2_BALLOC_GROUP_INFO_DIRTY;
		colibri->cb_sb.bsb_state |= C2_BALLOC_SB_DIRTY;

		rc = balloc_sync_sb(colibri, tx);
		if (rc == 0)
			rc = balloc_sync_group_info(colibri, tx, grp);

		LEAVE;
	} else if (op == C2_BALLOC_FREE) {
		struct c2_ext *cur = NULL;
		struct c2_ext *pre = NULL;
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
			fprintf(stderr, "!!!!!!!!!!!!!double free\n");
			c2_balloc_debug_dump_group_extent(
				    "double free with cur", grp);
			return -EINVAL;
		}
		if (pre && pre->e_end > tgt->e_start) {
			fprintf(stderr, "!!!!!!!!!!!!!double free\n");
			c2_balloc_debug_dump_group_extent(
				    "double free with pre", grp);
			return -EINVAL;
		}

		if (!found) {
			if (i == 0) {
				/* no fragments at all */
				c2_db_pair_setup(&pair, db,
						 &tgt->e_start, keysize,
						 &tgt->e_end, recsize);
				rc = c2_table_insert(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				grp->bgi_fragments++;
			} else {
				/* at the tail */
				if (pre->e_end < tgt->e_start) {
					/* to be the last one, standalone*/
					c2_db_pair_setup(&pair, db,
							 &tgt->e_start, keysize,
							 &tgt->e_end, recsize);
					rc = c2_table_insert(tx, &pair);
					c2_db_pair_fini(&pair);
					if (rc) {
						LEAVE;
						return rc;
					}
					grp->bgi_fragments++;
				} else {
					pre->e_end = tgt->e_end;

					c2_db_pair_setup(&pair, db,
							 &pre->e_start, keysize,
							 &pre->e_end, recsize);
					rc = c2_table_update(tx, &pair);
					c2_db_pair_fini(&pair);
					if (rc) {
						LEAVE;
						return rc;
					}
				}
			}
		} else if (found && pre == NULL) {
			/* on the head */
			if (tgt->e_end < cur->e_start) {
				/* to be the first one */

				c2_db_pair_setup(&pair, db,
						 &tgt->e_start, keysize,
						 &tgt->e_end, recsize);
				rc = c2_table_insert(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				grp->bgi_fragments++;
			} else {
				/* join the first one */

				c2_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = c2_table_delete(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				cur->e_start = tgt->e_start;

				c2_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = c2_table_insert(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
			}
		} else {
			/* in the middle */
			if (pre->e_end	 == tgt->e_start &&
			    tgt->e_end == cur->e_start) {
				/* joint to both */

				c2_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = c2_table_delete(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				pre->e_end = cur->e_end;

				c2_db_pair_setup(&pair, db,
						 &pre->e_start, keysize,
						 &pre->e_end, recsize);
				rc = c2_table_update(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				grp->bgi_fragments--;
			} else
			if (pre->e_end == tgt->e_start) {
				/* joint with prev */
				pre->e_end = tgt->e_end;

				c2_db_pair_setup(&pair, db,
						 &pre->e_start, keysize,
						 &pre->e_end, recsize);
				rc = c2_table_update(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
			} else
			if (tgt->e_end == cur->e_start) {
				/* joint with current */

				c2_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = c2_table_delete(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				cur->e_start = tgt->e_start;

				c2_db_pair_setup(&pair, db,
						 &cur->e_start, keysize,
						 &cur->e_end, recsize);
				rc = c2_table_insert(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
			} else {
				/* add a new one */

				c2_db_pair_setup(&pair, db,
						 &tgt->e_start, keysize,
						 &tgt->e_end, recsize);
				rc = c2_table_insert(tx, &pair);
				c2_db_pair_fini(&pair);
				if (rc) {
					LEAVE;
					return rc;
				}
				grp->bgi_fragments++;
			}
		}

		colibri->cb_sb.bsb_freeblocks += c2_ext_length(tgt);
		/* TODO update the maxchunk please */
		grp->bgi_freeblocks += c2_ext_length(tgt);

		grp->bgi_state |= C2_BALLOC_GROUP_INFO_DIRTY;
		colibri->cb_sb.bsb_state |= C2_BALLOC_SB_DIRTY;

		rc = balloc_sync_sb(colibri, tx);
		if (rc == 0)
			rc = balloc_sync_group_info(colibri, tx, grp);

		LEAVE;
	} else {
		rc = -EINVAL;
		LEAVE;
	}
	return rc;
}

static int balloc_find_by_goal(struct c2_balloc_allocation_context *bac)
{
	c2_bindex_t group = balloc_bn2gn(bac->bac_goal.e_start,
					 bac->bac_ctxt);
	struct c2_balloc_group_info *grp = c2_balloc_gn2info(bac->bac_ctxt,
							     group);

	struct c2_db_tx *tx  = bac->bac_tx;
	struct c2_ext	 ex  = { 0 };
	int		 found;
	int		 ret = 0;
	ENTER;

	if (!(bac->bac_flags & C2_BALLOC_HINT_TRY_GOAL))
		goto out;

	debugp("groupno=%llu, start=%llu len=%llu, groupsize (%llu)\n",
		(unsigned long long)group,
		(unsigned long long)bac->bac_goal.e_start,
		(unsigned long long)c2_ext_length(&bac->bac_goal),
		(unsigned long long)bac->bac_ctxt->cb_sb.bsb_groupsize
	);

	c2_balloc_lock_group(grp);
	if (grp->bgi_maxchunk < c2_ext_length(&bac->bac_goal)) {
		LEAVE;
		goto out_unlock;
	}
	if (grp->bgi_freeblocks < c2_ext_length(&bac->bac_goal)) {
		LEAVE;
		goto out_unlock;
	}

	ret = c2_balloc_load_extents(bac->bac_ctxt, grp, tx);
	if (ret) {
		LEAVE;
		goto out_unlock;
	}

	found = balloc_find_extent_exact(bac, grp, &bac->bac_goal, &ex);
	debugp("found?max len = %llu\n",
	       (unsigned long long)c2_ext_length(&ex));

	if (found) {
		bac->bac_found++;
		bac->bac_best.e_start = bac->bac_goal.e_start;
		bac->bac_best.e_end   = ex.e_end;
		ret = balloc_use_best_found(bac);
	}

	/* update db according to the allocation result */
	if (ret == 0 && bac->bac_status == C2_BALLOC_AC_FOUND) {
		if (bac->bac_goal.e_end < bac->bac_best.e_end)
			balloc_new_preallocation(bac);

		ret = balloc_update_db(bac->bac_ctxt, tx, grp,
					  &bac->bac_final, C2_BALLOC_ALLOC);
	}

	c2_balloc_release_extents(grp);
	LEAVE;
out_unlock:
	c2_balloc_unlock_group(grp);
out:
	return ret;
}

/* group is locked */
static int balloc_good_group(struct c2_balloc_allocation_context *bac,
			     struct c2_balloc_group_info *gi)
{
	c2_bcount_t	free	  = gi->bgi_freeblocks;
	c2_bcount_t	fragments = gi->bgi_fragments;

	if (free == 0)
		return 0;

	if (fragments == 0)
		return 0;

	switch (bac->bac_criteria) {
	case 0:
		if (gi->bgi_maxchunk >= c2_ext_length(&bac->bac_goal))
			return 1;
		break;
	case 1:
		if ((free / fragments) >= c2_ext_length(&bac->bac_goal))
			return 2;
		break;
	case 2:
			return 3;
		break;
	default:
		C2_ASSERT(0);
	}

	return 0;
}

/* group is locked */
static int balloc_simple_scan_group(struct c2_balloc_allocation_context *bac,
				    struct c2_balloc_group_info *grp)
{
/*
	struct c2_balloc_super_block *sb = &bac->bac_ctxt->cb_sb;
*/
	struct c2_ext	ex;
	c2_bcount_t	len;
	int		found = 0;
	ENTER;

	C2_ASSERT(bac->bac_order2 > 0);

	len = 1 << bac->bac_order2;
/*	for (; len <= sb->bsb_groupsize; len = len << 1) {
		debugp("searching at %d (gs = %d) for order = %d, len=%d:%x\n",
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

	LEAVE;
	return 0;
}

__attribute__((unused))
static int balloc_aligned_scan_group(struct c2_balloc_allocation_context *bac,
				     struct c2_balloc_group_info *grp)
{
	return 0;
}

/*
 * How long balloc can look for a best extent (in found extents)
 */
#define C2_BALLOC_DEFAULT_MAX_TO_SCAN	       200

/*
 * How long balloc must look for a best extent
 */
#define C2_BALLOC_DEFAULT_MIN_TO_SCAN	       10

/*
 * How many groups balloc will scan looking for the best chunk
 */
#define C2_BALLOC_DEFAULT_MAX_GROUPS_TO_SCAN   5


static int balloc_check_limits(struct c2_balloc_allocation_context *bac,
			       struct c2_balloc_group_info *grp,
			       int end_of_group)
{
	int max_to_scan = C2_BALLOC_DEFAULT_MAX_TO_SCAN;
	int max_groups	= C2_BALLOC_DEFAULT_MAX_GROUPS_TO_SCAN;
	int min_to_scan = C2_BALLOC_DEFAULT_MIN_TO_SCAN;
	ENTER;

	debugp("check limits for group %llu. end = %d\n",
		(unsigned long long)grp->bgi_groupno,
		end_of_group);

	if (bac->bac_status == C2_BALLOC_AC_FOUND)
		return 0;

	if ((bac->bac_found > max_to_scan || bac->bac_scanned > max_groups) &&
		!(bac->bac_flags & C2_BALLOC_HINT_FIRST)) {
		bac->bac_status = C2_BALLOC_AC_BREAK;
		return 0;
	}

	if (c2_ext_length(&bac->bac_best) < c2_ext_length(&bac->bac_goal))
		return 0;

	if ((end_of_group || bac->bac_found >= min_to_scan)) {
		c2_bindex_t group = balloc_bn2gn(bac->bac_best.e_start,
						    bac->bac_ctxt);
		if (group == grp->bgi_groupno)
			balloc_use_best_found(bac);
	}

	return 0;
}

static int balloc_measure_extent(struct c2_balloc_allocation_context *bac,
				 struct c2_balloc_group_info *grp,
				 struct c2_ext *ex)
{
	struct c2_ext *goal = &bac->bac_goal;
	struct c2_ext *best = &bac->bac_best;
	int rc;
	ENTER;

	balloc_debug_dump_extent(__func__, ex);
	bac->bac_found++;

	if ((bac->bac_flags & C2_BALLOC_HINT_FIRST) ||
	     c2_ext_length(ex) == c2_ext_length(goal)) {
		*best = *ex;
		balloc_use_best_found(bac);
		LEAVE;
		return 0;
	}

	if (c2_ext_length(best) == 0) {
		*best = *ex;
		LEAVE;
		return 0;
	}

	if (c2_ext_length(best) < c2_ext_length(goal)) {
		/* req is still not satisfied. use the larger one */
		if (c2_ext_length(ex) > c2_ext_length(best)) {
			*best = *ex;
		}
	} else if (c2_ext_length(ex) > c2_ext_length(goal)) {
		/* req is satisfied. but it is satisfied again.
		   use the smaller one */
		if (c2_ext_length(ex) < c2_ext_length(best)) {
			*best = *ex;
		}
	}

	rc = balloc_check_limits(bac, grp, 0);
	LEAVE;
	return rc;
}

/**
 * This function scans the specified group for a goal. If maximal
 * group is locked.
 */
static int balloc_wild_scan_group(struct c2_balloc_allocation_context *bac,
				  struct c2_balloc_group_info *grp)
{
	c2_bcount_t	 i;
	c2_bcount_t	 free;
	struct c2_ext	*ex;
	int		 rc;
	ENTER;

	free = grp->bgi_freeblocks;

	debugp("Wild scanning at group %llu: freeblocks = %llu\n",
		(unsigned long long)grp->bgi_groupno,
		(unsigned long long)free);

	for (i = 0; i < grp->bgi_fragments; i++) {
		ex = &grp->bgi_extents[i];
		if (c2_ext_length(ex) > free) {
			fprintf(stderr, "corrupt group = %llu, "
					"ex=[0x%08llx:0x%08llx)\n",
				(unsigned long long)grp->bgi_groupno,
				(unsigned long long)ex->e_start,
				(unsigned long long)ex->e_end);
			LEAVE;
			return -EINVAL;
		}
		balloc_measure_extent(bac, grp, ex);

		free -= c2_ext_length(ex);
		if (free == 0 || bac->bac_status != C2_BALLOC_AC_CONTINUE) {
			LEAVE;
			return 0;
		}
	}

	rc = balloc_check_limits(bac, grp, 1);
	LEAVE;
	return rc;
}

/*
 * TRY to use the best result found in previous iteration.
 * The best result may be already used by others who are more lukcy.
 * Group lock should be taken again.
 */
static int balloc_try_best_found(struct c2_balloc_allocation_context *bac)
{
	struct c2_ext			*best  = &bac->bac_best;
	c2_bindex_t			 group = balloc_bn2gn(best->e_start,
								 bac->bac_ctxt);
	struct c2_balloc_group_info	*grp   =c2_balloc_gn2info(bac->bac_ctxt,
								  group);
	struct c2_ext			*ex;
	c2_bcount_t			 i;
	int				 rc    = -ENOENT;
	ENTER;

	c2_balloc_lock_group(grp);

	if (grp->bgi_freeblocks < c2_ext_length(best))
		goto out;

	rc = c2_balloc_load_extents(bac->bac_ctxt, grp, bac->bac_tx);
	if (rc != 0)
		goto out;

	rc = -ENOENT;
	for (i = 0; i < grp->bgi_fragments; i++) {
		ex = &grp->bgi_extents[i];
		if (c2_ext_equal(ex, best)) {
			rc = balloc_use_best_found(bac);
			break;
		} else if (ex->e_start > best->e_start)
			goto out_release;
	}

	/* update db according to the allocation result */
	if (rc == 0 && bac->bac_status == C2_BALLOC_AC_FOUND) {
		if (c2_ext_length(&bac->bac_goal) < c2_ext_length(best))
			balloc_new_preallocation(bac);

		balloc_debug_dump_extent(__func__, &bac->bac_final);
		rc = balloc_update_db(bac->bac_ctxt, bac->bac_tx, grp,
					 &bac->bac_final,
					 C2_BALLOC_ALLOC);
	}
out_release:
	c2_balloc_release_extents(grp);
out:
	c2_balloc_unlock_group(grp);
	LEAVE;
	return rc;
}

static int
balloc_regular_allocator(struct c2_balloc_allocation_context *bac)
{
	c2_bcount_t	ngroups, group, i, len;
	int		cr;
	int		rc = 0;
	ENTER;

	ngroups = bac->bac_ctxt->cb_sb.bsb_groupcount;
	len = c2_ext_length(&bac->bac_goal);

	/* first, try the goal */
	rc = balloc_find_by_goal(bac);
	if (rc != 0 || bac->bac_status == C2_BALLOC_AC_FOUND ||
	    (bac->bac_flags & C2_BALLOC_HINT_GOAL_ONLY)) {
		LEAVE;
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
	for (;cr < 3 && bac->bac_status == C2_BALLOC_AC_CONTINUE; cr++) {
		bac->bac_criteria = cr;
		/*
		 * searching for the right group start
		 * from the goal value specified
		 */
		group = balloc_bn2gn(bac->bac_goal.e_start, bac->bac_ctxt);

		for (i = 0; i < ngroups; group++, i++) {
			struct c2_balloc_group_info *grp;

			if (group >= ngroups)
				group = 0;

			grp = c2_balloc_gn2info(bac->bac_ctxt, group);
			// c2_balloc_debug_dump_group("searching group ...\n",
			//			 grp);

			rc = c2_balloc_trylock_group(grp);
			if (rc != 0) {
				/* This group is under processing by others. */
				continue;
			}

			/* quick check to skip empty groups */
			if (grp->bgi_freeblocks == 0) {
				c2_balloc_unlock_group(grp);
				continue;
			}

			rc = c2_balloc_load_extents(bac->bac_ctxt, grp,
						    bac->bac_tx);
			if (rc != 0) {
				c2_balloc_unlock_group(grp);
				goto out;
			}

			if (!balloc_good_group(bac, grp)) {
				c2_balloc_release_extents(grp);
				c2_balloc_unlock_group(grp);
				continue;
			}
			bac->bac_scanned++;

			c2_balloc_debug_dump_group_extent("AAAAAAAAAAAAAAAAAA",
							  grp);
			if (cr == 0)
				rc = balloc_simple_scan_group(bac, grp);
			else if (cr == 1 &&
				len == bac->bac_ctxt->cb_sb.bsb_stripe_size)
				rc = balloc_simple_scan_group(bac, grp);
			else
				rc = balloc_wild_scan_group(bac, grp);

			/* update db according to the allocation result */
			if (rc == 0 && bac->bac_status == C2_BALLOC_AC_FOUND) {
				if (len < c2_ext_length(&bac->bac_best))
					balloc_new_preallocation(bac);

				rc = balloc_update_db(bac->bac_ctxt,
							 bac->bac_tx, grp,
							 &bac->bac_final,
							 C2_BALLOC_ALLOC);
			}


			c2_balloc_release_extents(grp);
			c2_balloc_unlock_group(grp);

			if (bac->bac_status != C2_BALLOC_AC_CONTINUE)
				break;
		}
	}

	if (c2_ext_length(&bac->bac_best) > 0 &&
	    (bac->bac_status != C2_BALLOC_AC_FOUND) &&
	    !(bac->bac_flags & C2_BALLOC_HINT_FIRST)) {
		/*
		 * We've been searching too long. Let's try to allocate
		 * the best chunk we've found so far
		 */

		rc = balloc_try_best_found(bac);
		if (rc || bac->bac_status != C2_BALLOC_AC_FOUND) {
			/*
			 * Someone more lucky has already allocated it.
			 * Let's just take first found block(s).
			 */
			bac->bac_status = C2_BALLOC_AC_CONTINUE;
			C2_SET0(&bac->bac_best);
			bac->bac_flags |= C2_BALLOC_HINT_FIRST;
			cr = 3;
			debugp("Let's repeat..........\n");
			goto repeat;
		}
	}
out:
	LEAVE;
	if (rc == 0 && bac->bac_status != C2_BALLOC_AC_FOUND)
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
   space is availabe, and if largest contiguous chunk satisfy the request. This
   checking will be done group by group, until allocation succeeded or failed.
   If failed, the largest available contiguous chunk size is returned, and the
   caller can decide whether to use a smaller request.

   While searching free space from group to group, the free space extent will be
   loaded into cache.  We cache as much free space extent up to some
   specified memory limitation.	 This is a configurable parameter, or default
   value will be choosed based on system memory.

   @param ctxt balloc operation context environment.
   @param req allocate request which includes all parameters.
   @return 0 means success.
	   Result allocated blocks are again stored in "req":
	   result physical block number = bar_physical,
	   result count of blocks = bar_len.
	   Upon failure, non-zero error number is returned.
 */
static
int balloc_allocate_internal(struct c2_balloc *colibri,
			     struct c2_db_tx *tx,
			     struct c2_balloc_allocate_req *req)
{
	struct c2_balloc_allocation_context	bac;
	int					rc;
	ENTER;

	while (req->bar_len &&
	       !balloc_claim_free_blocks(colibri, req->bar_len)) {
		req->bar_len = req->bar_len >> 1;
	}
	if (req->bar_len == 0) {
		rc = -ENOSPC;
		goto out;
	}

	balloc_init_ac(&bac, colibri, tx, req);

	/* Step 1. query the pre-allocation */
	if (!balloc_use_prealloc(&bac)) {
		/* we did not find suitable free space in prealloc. */

		balloc_normalize_request(&bac);

		/* Step 2. Iterate over groups */
		rc = balloc_regular_allocator(&bac);
		if (rc == 0 && bac.bac_status == C2_BALLOC_AC_FOUND) {
			/* store the result in req and they will be returned */
			req->bar_result = bac.bac_final;
		}
	}
out:
	LEAVE;
	return rc;
}

/**
   Free multiple blocks owned by some object to free space.

   @param ctxt balloc operation context environment.
   @param req block free request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
static int balloc_free_internal(struct c2_balloc *colibri,
				struct c2_db_tx *tx,
				struct c2_balloc_free_req *req)
{
	struct c2_ext			 fex;
	struct c2_balloc_group_info	*grp;
	struct c2_balloc_super_block	*sb = &colibri->cb_sb;
	c2_bcount_t			 group;
	c2_bindex_t			 start, off;
	c2_bcount_t			 len, step;
	int				 rc = 0;
	ENTER;

	start = req->bfr_physical;
	len = req->bfr_len;

	group = balloc_bn2gn(start + len, colibri);
	debugp("start=0x%llx, len=0x%llx, start_group=%llu, "
		"end_group=%llu, group count=%llu\n",
		(unsigned long long)start,
		(unsigned long long)len,
		(unsigned long long)balloc_bn2gn(start, colibri),
		(unsigned long long)balloc_bn2gn(start + len, colibri),
		(unsigned long long)sb->bsb_groupcount
		);
	if (group > sb->bsb_groupcount)
		return -EINVAL;

	while (rc == 0 && len > 0) {
		group = balloc_bn2gn(start, colibri);

		grp = c2_balloc_gn2info(colibri, group);
		c2_balloc_lock_group(grp);

		rc = c2_balloc_load_extents(colibri, grp, tx);
		if (rc != 0) {
			c2_balloc_unlock_group(grp);
			goto out;
		}

		c2_balloc_debug_dump_group_extent(
			    "FFFFFFFFFFFFFFFFFFFFFFFFFFFF", grp);
		off = start & (sb->bsb_groupsize - 1);
		step = (off + len > sb->bsb_groupsize) ?
			sb->bsb_groupsize  - off : len;

		fex.e_start = start;
		fex.e_end   = start + step;
		rc = balloc_update_db(colibri, tx, grp,
					 &fex, C2_BALLOC_FREE);
		c2_balloc_release_extents(grp);
		c2_balloc_unlock_group(grp);
		start += step;
		len -= step;
	}

out:
	LEAVE;
	return rc;
}

/**
   Discard the pre-allocation for object.

   @param ctxt balloc operation context environment.
   @param req discard request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
__attribute__((unused))
static int balloc_discard_prealloc(struct c2_balloc *colibri,
				   struct c2_balloc_discard_req *req)
{
	return 0;
}

/**
   modify the allocation status forcibly.

   This function may be used by fsck or some other tools to modify the
   allocation status directly.

   @param ctxt balloc operation context environment.
   @param alloc true to make the specifed extent as allocated, otherwise make
	  the extent as free.
   @param ext user supplied extent to check.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
__attribute__((unused))
static int balloc_enforce(struct c2_balloc *colibri, bool alloc,
			  struct c2_ext *ex)
{
	return 0;
}


/**
   Query the allocation status.

   @param ctxt balloc operation context environment.
   @param ext user supplied extent to check.
   @return true if the extent is fully allocated. Otherwise, false is returned.
 */
__attribute__((unused))
static bool balloc_query(struct c2_balloc *colibri, struct c2_ext *ex)
{

	return false;
}

/**
 * allocate from underlying container.
 * @param count count of bytes. count will be first aligned to block boundry.
 * @param out result is stored there. space is still in bytes.
 */
static int balloc_alloc(struct c2_ad_balloc *ballroom, struct c2_dtx *tx,
			c2_bcount_t count, struct c2_ext *out)
{
	struct c2_balloc		*colibri = b2c2(ballroom);
	struct c2_balloc_allocate_req	 req;
	int				 rc;

	C2_ASSERT(count > 0);

	req.bar_goal  = out->e_start; /* this also plays as the goal */
	req.bar_len   = count;
	req.bar_flags = 0;/*C2_BALLOC_HINT_DATA | C2_BALLOC_HINT_TRY_GOAL;*/

	C2_SET0(out);

	rc = balloc_allocate_internal(colibri, &tx->tx_dbtx, &req);
	if (rc == 0 && !c2_ext_is_empty(&req.bar_result)) {
		out->e_start = req.bar_result.e_start;
		out->e_end   = req.bar_result.e_end;
		colibri->cb_last = *out;
	} else if (rc == 0)
		rc = -ENOENT;

	return rc;
}

/**
 * free spaces to container.
 * @param ext the space to be freed. This space must align to block boundry.
 */
static int balloc_free(struct c2_ad_balloc *ballroom, struct c2_dtx *tx,
		       struct c2_ext *ext)
{
	struct c2_balloc		*colibri = b2c2(ballroom);
	struct c2_balloc_free_req	 req;
	int				 rc;

	req.bfr_physical = ext->e_start;
	req.bfr_len	 = c2_ext_length(ext);

	rc = balloc_free_internal(colibri, &tx->tx_dbtx, &req);
	return rc;
}

static int balloc_init(struct c2_ad_balloc *ballroom, struct c2_dbenv *db,
		       uint32_t bshift, c2_bcount_t container_size,
		       c2_bcount_t blocks_per_group, c2_bcount_t res_groups)
{
	struct c2_balloc	*colibri;
	int			 rc;
	ENTER;

	colibri = b2c2(ballroom);

	rc = balloc_init_internal(colibri, db, bshift, container_size,
				     blocks_per_group, res_groups);

	/*
         * Free the memory allocated for colibri in
         * c2_balloc_allocate() on initialisation failure.
         */
	if (rc != 0)
             c2_free(colibri);

	LEAVE;
	return rc;
}

static void balloc_fini(struct c2_ad_balloc *ballroom)
{
	struct c2_balloc	*colibri = b2c2(ballroom);
	struct c2_db_tx		 fini_tx;
	int			 rc;
	ENTER;

	rc = c2_db_tx_init(&fini_tx, colibri->cb_dbenv, 0);
	if (rc == 0) {
		rc = balloc_fini_internal(colibri, &fini_tx);
		if (rc == 0)
			rc = c2_db_tx_commit(&fini_tx);
		else
			c2_db_tx_abort(&fini_tx);
	}

	c2_free(colibri);

	LEAVE;
}

static const struct c2_ad_balloc_ops balloc_ops = {
	.bo_init  = balloc_init,
	.bo_fini  = balloc_fini,
	.bo_alloc = balloc_alloc,
	.bo_free  = balloc_free,
};

int c2_balloc_allocate(struct c2_balloc **out)
{
	struct c2_balloc *cb;
	int               result;

        C2_PRE(out != NULL);

	C2_ALLOC_PTR(cb);
	if (cb != NULL) {
                cb->cb_ballroom.ab_ops = &balloc_ops;
                *out = cb;
                result = 0;
	} else
                result = -ENOMEM;

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
