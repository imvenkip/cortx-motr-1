/* -*- C -*- */

#ifndef __COLIBRI_DBA_DBA_H__
#define __COLIBRI_DBA_DBA_H__

#include <db.h>
#include <lib/list.h>
#include <lib/mutex.h>

/**
   @defgroup dba data-block-allocator

   All data structures stored in db in little-endian format (Intel CPU endian).
   To correctly compare the key value, a custom specific comparison function
   is provided to db.
   @{
*/



#define MAXPATHLEN 1024

typedef uint64_t c2_blockno_t;
typedef uint32_t c2_blockcount_t;
typedef uint64_t c2_groupno_t;

/**
   In-memory data structure for the dba environment.

   It includes pointers to db, home dir, various flags and parameters.
 */
struct c2_dba_ctxt {
	DB_ENV         *dc_dbenv;
	char	       *dc_home;

        uint32_t       dc_dbenv_flags;
        uint32_t       dc_db_flags;
        uint32_t       dc_txn_flags;
        uint32_t       dc_cache_size;
        uint32_t       dc_nr_thread;

	DB             *dc_group_extent;
	DB             *dc_group_info;
};

/**
   On-disk and in-momory data structure for extent

   When stored in db, {key, value} = {start, len}
 */
struct c2_dba_extent {
	c2_blockno_t    ext_start;
	c2_blockcount_t ext_len;
};


/**
   On-disk data structure for group, stored in db.

   When stored in db, {key, value} = {group_nr, c2_dba_group_desc}.
   Every group description will stay in a separate db.
 */
struct c2_dba_group_desc {
        c2_blockcount_t  dgd_freeblocks; /* total free blocks */
        c2_blockcount_t  dgd_fragments;  /* nr of freespace fragments */
        c2_blockcount_t  dgd_maxchunk;   /* max bytes of freespace chunk */
};

/**
   In-memory data structure for group
 */
struct c2_dba_group_info {
        unsigned long       dgi_state;
        c2_blockcount_t     dgi_freeblocks; /* total free blocks */
        c2_blockcount_t     dgi_fragments;  /* nr of freespace fragments */
        c2_blockcount_t     dgi_maxchunk;   /* max bytes of freespace chunk */
        struct c2_list_link dgi_prealloc_list;
        struct c2_mutex     dgi_mutex;
        c2_blockcount_t     dgi_counters[]; /* Nr of free power-of-two-block
                                             * regions, index is order.
                                             * bb_counters[3] = 5 means
                                             * 5 free 8-block regions. */
};

/**
   On-disk and in-memory super block stored in db
 */
struct c2_dba_super_block {
	uint64_t	   dsb_magic;
	uint64_t	   dsb_state;
	uint64_t	   dsb_version;
	uint8_t	   	   dsb_uuid[128];

        c2_blockno_t       dsb_totalsize;  /* total size in bytes */
        c2_blockno_t       dsb_freeblocks; /* nr of free blocks */
        c2_blockcount_t    dsb_blocksize;  /* block size in bytes */
        c2_blockcount_t    dsb_groupsize;  /* group size in blocks */
        c2_blockcount_t    dsb_reserved_groups;  /* nr of reserved groups */
        c2_blockcount_t    dsb_prealloc_count;   /* nr of pre-alloc blocks */

	uint64_t	   dsb_format_time;
	uint64_t	   dsb_write_time;
	uint64_t	   dsb_mnt_time;
	uint64_t	   dsb_last_check_time;

	uint64_t	   dsb_mnt_count;
	uint64_t	   dsb_max_mnt_count;

        c2_blockcount_t    dsb_stripe_size;   /* stripe size in blocks */
};

#define C2_DBA_SB_MAGIC   0xC011B21AC08EC08EULL
#define C2_DBA_SB_CLEAN   (1 << 0)
#define C2_DBA_SB_VERSION 1ULL


/**
   Request to format a container.
 */
struct c2_dba_format_req {
	c2_blockno_t 	dfr_totalsize;	      /* total size in bytes */
	uint32_t	dfr_blocksize;        /* block size in bytes */
	uint32_t	dfr_groupsize;        /* block size in blocks */
	uint32_t	dfr_reserved_groups;  /* # of resvered groups */

	char 	       *dfr_db_home;          /* database home dir */
};

struct c2_dba_prealloc {
	c2_blockno_t	dpr_logical;   /* logical offset within the object */
	c2_blockcount_t	dpr_lcount;    /* count of blocks */
	c2_blockno_t	dpr_physical;  /* physical block number */
	c2_blockcount_t	dpr_remaining; /* remaining count of blocks */
	struct c2_list_link dpr_link;  /* pre-allocation is linked together */
};

/**
   Request to allocate multiple blocks from a container.

   Result is stored in dar_physical. On error case, error number is returned
   in dar_err. If all available free chunks are smaller that requested, then
   the maximum available chunk size is returned in dar_max_avail.
 */
struct c2_dba_allocate_req {
	c2_blockno_t	dar_logical;   /* logical offset within the object */
	c2_blockcount_t	dar_lcount;    /* count of blocks */
	c2_blockno_t	dar_goal;      /* prefered physical block number */
	uint32_t	dar_flags;     /* allocation flags */

	c2_blockno_t	dar_physical;  /* result allocated blocks */

	uint32_t	dar_err;       /* error number */
	c2_blockno_t	dar_max_avail; /* max avail blocks */

	void           *dar_prealloc;  /* User opaque prealloc result */
};

enum c2_dba_allocation_flag {
	/* prefer goal again. length */
	C2_DBA_HINT_MERGE              = 1 << 0,

	/* blocks already reserved */
	C2_DBA_HINT_RESERVED           = 1 << 1,

	/* metadata is being allocated */
	C2_DBA_HINT_METADATA           = 1 << 2,

	/* first blocks in the file */
	C2_DBA_HINT_FIRST              = 1 << 3,

	/* search for the best chunk */
	C2_DBA_HINT_BEST               = 1 << 4,

	/* data is being allocated */
	C2_DBA_HINT_DATA               = 1 << 5,

	/* don't preallocate (for tails) */
	C2_DBA_HINT_NOPREALLOC         = 1 << 6,

	/* allocate for locality group */
	C2_DBA_HINT_GROUP_ALLOC        = 1 << 7,

	/* allocate goal blocks or none */
	C2_DBA_HINT_GOAL_ONLY          = 1 << 8,

	/* goal is meaningful */
	C2_DBA_HINT_TRY_GOAL           = 1 << 9,

	/* blocks already pre-reserved by delayed allocation */
	C2_DBA_DELALLOC_RESERVED       = 1 << 10,

	/* We are doing stream allocation */
	C2_DBA_STREAM_ALLOC            = 1 << 11,
};


/**
   Request to free multiple blocks to a container.
 */
struct c2_dba_free_req {
	c2_blockno_t	dfr_logical;  /* logical offset within the object */
	c2_blockcount_t	dfr_lcount;   /* count of blocks */
	c2_blockno_t	dfr_physical; /* physical block number */
	uint32_t	dfr_flags;    /* free flags */
};

struct c2_dba_discard_req {
	void           *ddr_prealloc; /* User opaque prealloc result */
};


int c2_dba_format(struct c2_dba_format_req *req);
int c2_dba_init(struct c2_dba_ctxt *ctxt);
int c2_dba_allocate(struct c2_dba_ctxt *ctxt, struct c2_dba_allocate_req *req);
int c2_dba_free(struct c2_dba_ctxt *ctxt, struct c2_dba_free_req *req);
int c2_dba_discard_prealloc(struct c2_dba_ctxt *ctxt,
			    struct c2_dba_discard_req *req);
int c2_dba_enforce(struct c2_dba_ctxt *ctxt, bool alloc, struct c2_dba_extent *ext);
bool c2_dba_query(struct c2_dba_ctxt *ctxt, struct c2_dba_extent *ext);


/**
   Convert a 64-bit integer into big-endian and place it into @place
 */
static inline void cpu2be(void *place, uint64_t val)
{
	*(uint64_t *)place = val;

	char *area = place;
	int i;

	for (i = 0; i < 8; ++i)
		area[i] = val >> (64 - (i + 1)*8);
}

/**
   Convert big-endian data @place into 64-bit interger and return it
 */
static inline uint64_t be2cpu(void *place)
{
	char *area = place;
	int i;
	uint64_t out;

	for (out = 0, i = 0; i < 8; ++i)
		out = (out << 8) | area[i];
	return out;
}


#define DBA_DEBUG

#ifdef DBA_DEBUG
#define ENTER printf("===>>> %s:%d:%s\n", __FILE__, __LINE__, __func__)
#define LEAVE printf("<<<=== %s:%d:%s\n", __FILE__, __LINE__, __func__)
#define GOTHERE printf("!!! %s:%d:%s\n", __FILE__, __LINE__, __func__)
#else
#define ENTER
#define LEAVE
#define GOTHERE
#endif

/** @} end of addb dba */

#endif /*__COLIBRI_DBA_DBA_H__*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
