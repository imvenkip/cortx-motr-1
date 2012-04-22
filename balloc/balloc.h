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
 * Original creation date: 07/27/2010
 */

#ifndef __COLIBRI_BALLOC_BALLOC_H__
#define __COLIBRI_BALLOC_BALLOC_H__

#include "db/db.h"
#include "lib/ext.h"
#include "lib/types.h"
#include "lib/list.h"
#include "lib/mutex.h"
#include "stob/ad.h"

/**
   @defgroup balloc data-block-allocator

   All data structures stored in db in little-endian format (Intel CPU endian).
   To correctly compare the key value, a custom specific comparison function
   is provided to db.
   @{
*/

/**
   On-disk data structure for group, stored in db.

   When stored in db, {key, value} = {group_nr, c2_balloc_group_desc}.
   Every group description will stay in a separate db.
 */
struct c2_balloc_group_desc {
        c2_bindex_t  bgd_groupno;    /*< group number */
        c2_bcount_t  bgd_freeblocks; /*< total free blocks */
        c2_bcount_t  bgd_fragments;  /*< nr of freespace fragments */
        c2_bcount_t  bgd_maxchunk;   /*< max bytes of freespace chunk */
};

/**
   In-memory data structure for group
 */
struct c2_balloc_group_info {
        uint64_t         bgi_state;         /*< enum c2_balloc_group_info_state */
        c2_bindex_t      bgi_groupno;       /*< group number */
        c2_bcount_t      bgi_freeblocks;    /*< total free blocks */
        c2_bcount_t      bgi_fragments;     /*< nr of freespace fragments */
        c2_bcount_t      bgi_maxchunk;      /*< max bytes of freespace chunk */
        struct c2_list   bgi_prealloc_list; /*< list of pre-alloc */
        struct c2_mutex  bgi_mutex;         /*< per-group lock */

	struct c2_ext   *bgi_extents;       /*< (bgi_fragments+1) of extents */

        /**
	   Nr of free power-of-two-block regions, index is order.
           bb_counters[3] = 5 means 5 free 8-block regions.
	   c2_bcount_t     bgi_counters[];
        */
};

enum c2_balloc_group_info_state {
	/** inited from disk */
	C2_BALLOC_GROUP_INFO_INIT = 1 << 0,
	/** dirty, need sync */
	C2_BALLOC_GROUP_INFO_DIRTY = 1 << 1,
};


/**
   On-disk and in-memory super block stored in db
 */
struct c2_balloc_super_block {
	uint64_t	bsb_magic;
	uint64_t	bsb_state;
	uint64_t	bsb_version;
	uint8_t		bsb_uuid[128];

        c2_bcount_t	bsb_totalsize;        /*< total size in bytes */
        c2_bcount_t	bsb_freeblocks;       /*< nr of free blocks */
        c2_bcount_t	bsb_blocksize;        /*< block size in bytes */
        c2_bcount_t	bsb_groupsize;        /*< group size in blocks */
	uint32_t	bsb_bsbits;           /*< block size bits: power of 2 */
	uint32_t	bsb_gsbits;           /*< group size bits: power of 2 */
        c2_bcount_t	bsb_groupcount;       /*< # of group */
        c2_bcount_t	bsb_reserved_groups;  /*< nr of reserved groups */
        c2_bcount_t	bsb_prealloc_count;   /*< nr of pre-alloc blocks */

	uint64_t	bsb_format_time;
	uint64_t	bsb_write_time;
	uint64_t	bsb_mnt_time;
	uint64_t	bsb_last_check_time;

	uint64_t	bsb_mnt_count;
	uint64_t	bsb_max_mnt_count;

        c2_bcount_t	bsb_stripe_size;      /*< stripe size in blocks */
};

enum c2_balloc_super_block_magic {
	C2_BALLOC_SB_MAGIC =  0xC011B21AC08EC08EULL,
};

enum c2_balloc_super_block_state {
	C2_BALLOC_SB_DIRTY =  1 << 0,
};

enum c2_balloc_super_block_version {
	C2_BALLOC_SB_VERSION = 1ULL,
};

/**
   In-memory data structure for the balloc environment.

   It includes pointers to db, various flags and parameters.
 */
struct c2_balloc {
	struct c2_dbenv             *cb_dbenv;

	struct c2_table              cb_db_sb;            /*< db for sb */
	struct c2_balloc_super_block cb_sb;               /*< the on-disk and in-memory sb */
        struct c2_mutex              cb_sb_mutex;         /*< super block lock */

	struct c2_table              cb_db_group_extents; /*< db for free extent */
	struct c2_table              cb_db_group_desc;    /*< db for group desc */
	struct c2_balloc_group_info *cb_group_info;       /*< array of group info */

	struct c2_ext                cb_last;

	struct c2_ad_balloc          cb_ballroom;
};

static inline struct c2_balloc *b2c2(struct c2_ad_balloc *ballroom)
{
	return container_of(ballroom, struct c2_balloc, cb_ballroom);
}

/**
   Request to format a container.
 */
struct c2_balloc_format_req {
	c2_bcount_t 	bfr_totalsize;	      /*< total size in bytes  */
	c2_bcount_t	bfr_blocksize;        /*< block size in bytes  */
	c2_bcount_t	bfr_groupsize;        /*< group size in blocks */
	c2_bcount_t	bfr_reserved_groups;  /*< # of reserved groups */
};

struct c2_balloc_free_extent {
        c2_bindex_t bfe_logical;       /*< logical offset within the object */
        c2_bindex_t bfe_groupno;       /*< goal group # */
        c2_bindex_t bfe_start;         /*< relative start offset */
        c2_bindex_t bfe_end;           /*< relative start offset */
};

/**
   Request to allocate multiple blocks from a container.

   Result is stored in bar_physical. On error case, error number is returned
   in bar_err. If all available free chunks are smaller that requested, then
   the maximum available chunk size is returned in bar_max_avail.
 */
struct c2_balloc_allocate_req {
	c2_bindex_t	bar_logical; /*< [in]logical offset within the object */
	c2_bcount_t	bar_len;     /*< [in]count of blocks, */
	c2_bindex_t	bar_goal;    /*< [in]prefered physical block number */
	uint64_t	bar_flags;   /*< [in]allocation flags from
				      * c2_balloc_allocation_flag */
        struct c2_ext   bar_result;  /*< [out]physical offset, result */

	void           *bar_prealloc;/*< [in][out]User opaque prealloc result */
};

enum c2_balloc_allocation_flag {
	/** prefer goal again. length */
	C2_BALLOC_HINT_MERGE              = 1 << 0,

	/** blocks already reserved */
	C2_BALLOC_HINT_RESERVED           = 1 << 1,

	/** metadata is being allocated */
	C2_BALLOC_HINT_METADATA           = 1 << 2,

	/** use the first found extent */
	C2_BALLOC_HINT_FIRST              = 1 << 3,

	/** search for the best chunk */
	C2_BALLOC_HINT_BEST               = 1 << 4,

	/** data is being allocated */
	C2_BALLOC_HINT_DATA               = 1 << 5,

	/** don't preallocate (for tails) */
	C2_BALLOC_HINT_NOPREALLOC         = 1 << 6,

	/** allocate for locality group */
	C2_BALLOC_HINT_GROUP_ALLOC        = 1 << 7,

	/** allocate goal blocks or none */
	C2_BALLOC_HINT_GOAL_ONLY          = 1 << 8,

	/** goal is meaningful */
	C2_BALLOC_HINT_TRY_GOAL           = 1 << 9,

	/** blocks already pre-reserved by delayed allocation */
	C2_BALLOC_DELALLOC_RESERVED       = 1 << 10,

	/** We are doing stream allocation */
	C2_BALLOC_STREAM_ALLOC            = 1 << 11,
};


/**
   Request to free multiple blocks to a container.
 */
struct c2_balloc_free_req {
	c2_bindex_t	bfr_logical;  /*< logical offset within the object */
	c2_bcount_t	bfr_len;      /*< count of blocks */
	c2_bindex_t	bfr_physical; /*< physical block number */
	uint32_t	bfr_flags;    /*< free flags */
};

struct c2_balloc_discard_req {
	void           *bdr_prealloc; /*< User opaque prealloc result */
};

/*
 * BALLOC_DEF_BLOCKS_PER_GROUP * 1 << BALLOC_DEF_BLOCK_SHIFT = 128 MB -->
 * which equals group size in ext4
 */

enum {
	BALLOC_DEF_CONTAINER_SIZE	= 4096ULL * 1024 * 1024 * 1000,
	BALLOC_DEF_BLOCK_SHIFT		= 12,// 4K Blocks
	BALLOC_DEF_BLOCKS_PER_GROUP     = 32768,
	BALLOC_DEF_RESERVED_GROUPS	= 2
};

/**
   Allocates struct c2_balloc instance and initialises struct ad_balloc_ops
   vector. One balloc instance is allocated and initialised per storage domain.

   @see struct ad_balloc_ops
   @pre out != NULL
 */
int c2_balloc_locate(struct c2_balloc **out);

/* Interfaces for UT */
void c2_balloc_debug_dump_sb(const char *tag,
			     struct c2_balloc_super_block *sb);
void c2_balloc_debug_dump_group_extent(const char *tag,
				       struct c2_balloc_group_info *grp);

int c2_balloc_release_extents(struct c2_balloc_group_info *grp);
int c2_balloc_load_extents(struct c2_balloc *cb,
			   struct c2_balloc_group_info *grp,
			   struct c2_db_tx *tx);
struct c2_balloc_group_info * c2_balloc_gn2info(struct c2_balloc *cb,
						c2_bindex_t groupno);
void c2_balloc_debug_dump_group(const char *tag,
				struct c2_balloc_group_info *grp);
void c2_balloc_lock_group(struct c2_balloc_group_info *grp);
int c2_balloc_trylock_group(struct c2_balloc_group_info *grp);
void c2_balloc_unlock_group(struct c2_balloc_group_info *grp);

/** @} end of balloc */

#endif /*__COLIBRI_BALLOC_BALLOC_H__*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
